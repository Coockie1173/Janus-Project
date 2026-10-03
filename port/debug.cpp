//debugging aids for finding things in the running game
//times are seconds from start
//
//GE_STICK="42-43:r1,0 45-46:l0,1" hold a stick (r or l, then x,y in -1..1 with y up)
//GE_BUTTONS="66-66.3:0004 69-70:2000" hold classic buttons, KPAD's bits in hex
//(ZR 0004, ZL 0080, R 0200, L 2000, a 0010, b 0040, x 0008, y 0020)
//GE_MOUSEMOVE="66-67:300,0" fake mouse motion in pixels a second (x right, y down)
//GE_SCAN="41,43.5,44.5,46.5,49" snapshots guest memory at those times, then lists the floats
//(build/scan.txt) that changed over every odd interval and stood still over every even one
//turn the view with GE_STICK in the odd ones and the view's angles are in there
//GE_SCANINT=1 does it with raw words instead
//GE_POKE="50:80ABCDEF=f1.5 52:80ABCDEF+=f0.2" store or add a float (f) or a hex word (x)
//GE_PROBE="64:816F0270:build/cands.txt" for each address in the file add 0.35 to its float,
//wait 80 ms, say if the watched float followed, then put it back
//finds the value a derived one gets computed from
//GE_OBJ="806EB698:400:79,81.5,84.5" dumps the object a pointer points to (hex byte count)
//as words and floats at each time, into build/obj.txt
//GE_FINDPTR="66:80848000:808491A4" every guest word holding an address in that range
//GE_SETALL="62:49=80:80000000:94000000" every float equal to the first value in the range
//becomes the second, to find which copy of a constant matters by halving the range
//GE_WATCH="64:807807AC" who writes that guest word: the page goes read-only and a write logs
//the host code offset (tools/profile_report.py --offsets names it) and the guest call chain
//GE_TRACEW="80776B10" log that word whenever it changes (checked 200 times a second)
//GE_PEEK="80ABCDEF,80ABCDF0" log those floats every half second
#include "debug.h"
#include "mem.h"
#include "rt.h"
#include "video.h"
#ifndef _WIN32
#include <csignal>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point t0 = Clock::now();
double now_s() { return std::chrono::duration<double>(Clock::now() - t0).count(); }
void sleep_until_s(double t) {
    double d = t - now_s();
    if (d > 0) std::this_thread::sleep_for(std::chrono::duration<double>(d));
}

struct Hold { double a, b; char stick; float x, y; };
std::vector<Hold> holds, moves;
struct Press { double a, b; uint32_t bits; };
std::vector<Press> presses;

const uint32_t kRanges[][2] = {{0x80000000u, 0x81800000u}, {0x90000000u, 0x94000000u}};

float fl(const std::vector<uint8_t>& snap, size_t off) {
    uint32_t v;
    std::memcpy(&v, &snap[off], 4);
    v = __builtin_bswap32(v);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void scan_main(std::vector<double> times) {
    std::vector<std::vector<uint8_t>> snaps;
    for (double t : times) {
        sleep_until_s(t);
        std::vector<uint8_t> s;
        for (auto& r : kRanges) s.insert(s.end(), host(r[0]), host(r[0]) + (r[1] - r[0]));
        snaps.push_back(std::move(s));
        rt_log("scan: snapshot %zu at %.1f s", snaps.size(), now_s());
    }
    FILE* out = std::fopen("build/scan.txt", "w");
    if (!out) return;
    size_t n = snaps[0].size(), found = 0;
    if (std::getenv("GE_SCANINT")) { //words instead (flags, counters), same rule on raw values
        for (size_t off = 0; off + 4 <= n; off += 4) {
            bool ok = true;
            for (size_t i = 0; ok && i + 1 < snaps.size(); ++i) {
                bool same = !std::memcmp(&snaps[i][off], &snaps[i + 1][off], 4);
                ok = i % 2 == 0 ? !same : same;
            }
            if (!ok) continue;
            uint32_t addr = off < 0x01800000u ? 0x80000000u + (uint32_t)off : 0x90000000u + (uint32_t)(off - 0x01800000u);
            std::fprintf(out, "%08X", addr);
            for (auto& sn : snaps) std::fprintf(out, " %02X%02X%02X%02X", sn[off], sn[off + 1], sn[off + 2], sn[off + 3]);
            std::fprintf(out, "\n");
            ++found;
        }
        std::fclose(out);
        rt_log("scan: %zu words in build/scan.txt", found);
        return;
    }
    for (size_t off = 0; off + 4 <= n; off += 4) {
        bool ok = true;
        for (size_t i = 0; ok && i + 1 < snaps.size(); ++i) {
            float a = fl(snaps[i], off), b = fl(snaps[i + 1], off);
            if (!std::isfinite(a) || !std::isfinite(b) || std::fabs(a) > 1e5f || std::fabs(b) > 1e5f) ok = false;
            else if (i % 2 == 0) ok = std::fabs(a - b) > 1e-4f; //turning, so it moved
            else ok = a == b; //still, so it didn't
        }
        if (!ok) continue;
        uint32_t addr = off < 0x01800000u ? 0x80000000u + (uint32_t)off : 0x90000000u + (uint32_t)(off - 0x01800000u);
        std::fprintf(out, "%08X", addr);
        for (auto& s : snaps) std::fprintf(out, " %12.6f", fl(s, off));
        std::fprintf(out, "\n");
        ++found;
    }
    std::fclose(out);
    rt_log("scan: %zu floats in build/scan.txt", found);
}

struct Poke { double t; uint32_t addr; bool add, is_float; float f; uint32_t x; };

void poke_main(std::vector<Poke> pokes) {
    for (auto& p : pokes) {
        sleep_until_s(p.t);
        if (p.is_float) {
            uint32_t bits = ld32(p.addr);
            float v;
            std::memcpy(&v, &bits, 4);
            v = p.add ? v + p.f : p.f;
            std::memcpy(&bits, &v, 4);
            st32(p.addr, bits);
            rt_log("poke: %08X = %f", p.addr, v);
        } else {
            st32(p.addr, p.add ? ld32(p.addr) + p.x : p.x);
            rt_log("poke: %08X = %08X", p.addr, ld32(p.addr));
        }
    }
}

float rdf(uint32_t a) { uint32_t b = ld32(a); float v; std::memcpy(&v, &b, 4); return v; }
void wrf(uint32_t a, float v) { uint32_t b; std::memcpy(&b, &v, 4); st32(a, b); }

void probe_main(double t, uint32_t watch, std::string file) {
    std::vector<uint32_t> addrs;
    if (FILE* f = std::fopen(file.c_str(), "r")) {
        unsigned a;
        while (std::fscanf(f, "%x", &a) == 1) addrs.push_back(a);
        std::fclose(f);
    }
    sleep_until_s(t);
    rt_log("probe: %zu addresses against %08X", addrs.size(), watch);
    for (uint32_t a : addrs) {
        if (a == watch) continue;
        float w0 = rdf(watch), v0 = rdf(a);
        wrf(a, v0 + 0.35f);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        float w1 = rdf(watch), v1 = rdf(a);
        bool kept = std::fabs(v1 - (v0 + 0.35f)) < 0.01f;
        if (std::fabs(w1 - w0) > 0.05f)
            rt_log("probe: %08X (%f) moves %08X: %f -> %f%s", a, v0, watch, w0, w1, kept ? ", and keeps the write" : "");
        if (kept) wrf(a, v0);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    rt_log("probe: done at %.1f s", now_s());
}

void obj_main(uint32_t ptr, uint32_t size, std::vector<double> times) {
    FILE* out = std::fopen("build/obj.txt", "w");
    if (!out) return;
    std::vector<std::vector<uint32_t>> rows;
    for (double t : times) {
        sleep_until_s(t);
        uint32_t base = ld32(ptr);
        std::fprintf(out, "# %.1f s: *%08X = %08X\n", now_s(), ptr, base);
        std::vector<uint32_t> r;
        if (base >= 0x80000000u && base < 0x94000000u)
            for (uint32_t o = 0; o < size; o += 4) r.push_back(ld32(base + o));
        rows.push_back(r);
    }
    for (uint32_t o = 0; o < size; o += 4) {
        std::fprintf(out, "+%04X", o);
        for (auto& r : rows) {
            if (o / 4 >= r.size()) continue;
            float f;
            std::memcpy(&f, &r[o / 4], 4);
            std::fprintf(out, "  %08X %12.5f", r[o / 4], std::fabs(f) < 1e7f && std::fabs(f) > 1e-7f ? f : 0.0f);
        }
        std::fprintf(out, "\n");
    }
    std::fclose(out);
    rt_log("obj: build/obj.txt written");
}

void findptr_main(double t, uint32_t lo, uint32_t hi) {
    sleep_until_s(t);
    int n = 0;
    for (auto& r : kRanges)
        for (uint32_t a = r[0]; a < r[1] && n < 200; a += 4) {
            uint32_t v = ld32(a);
            if (v >= lo && v <= hi) { rt_log("findptr: %08X holds %08X", a, v); ++n; }
        }
    rt_log("findptr: %d found", n);
}

void setall_main(double t, float from, float to, uint32_t lo, uint32_t hi) {
    sleep_until_s(t);
    int n = 0;
    for (auto& r : kRanges)
        for (uint32_t a = std::max(r[0], lo); a < std::min(r[1], hi); a += 4)
            if (rdf(a) == from) {
                wrf(a, to);
                if (n++ < 300) rt_log("setall: %08X", a);
            }
    rt_log("setall: %d floats %g -> %g", n, from, to);
}

#ifndef _WIN32
//GE_WATCH
extern "C" char __executable_start;
uint8_t* watch_page = nullptr;
uint8_t* watch_addr = nullptr;
int watch_hits = 0;
struct sigaction watch_prev_segv;

void watch_segv(int sig, siginfo_t* si, void* uc_) {
    ucontext_t* uc = (ucontext_t*)uc_;
    uint8_t* a = (uint8_t*)si->si_addr;
    if (!watch_page || a < watch_page || a >= watch_page + 4096) {
        if (mem_watch_fault(a)) return;
        if (watch_prev_segv.sa_flags & SA_SIGINFO) watch_prev_segv.sa_sigaction(sig, si, uc_);
        else _exit(128 + sig);
        return;
    }
    mem_watch_fault(a); //the runtime may be watching the page too
    if (a >= watch_addr && a < watch_addr + 4 && watch_hits < 12) {
        ++watch_hits;
        uintptr_t pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
        std::fprintf(stderr, "watch: write from host offset %lX\n", (unsigned long)(pc - (uintptr_t)&__executable_start));
        os_report_running(10);
    }
    mprotect(watch_page, 4096, PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100; //one instruction, then protect again
}
void watch_trap(int, siginfo_t*, void* uc_) {
    ucontext_t* uc = (ucontext_t*)uc_;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    if (watch_page) mprotect(watch_page, 4096, PROT_READ);
}
void watch_main(double t, uint32_t addr) {
    sleep_until_s(t);
    watch_addr = host(addr);
    struct sigaction sa = {};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = watch_trap;
    sigaction(SIGTRAP, &sa, nullptr);
    sa.sa_sigaction = watch_segv;
    sigaction(SIGSEGV, &sa, &watch_prev_segv);
    uint8_t* page = (uint8_t*)((uintptr_t)watch_addr & ~(uintptr_t)4095);
    watch_page = page;
    mprotect(page, 4096, PROT_READ);
    rt_log("watch: %08X", addr);
}
#endif

void tracew_main(uint32_t addr) {
    uint32_t last = ~0u;
    for (;;) {
        uint32_t v = ld32(addr);
        if (v != last) { rt_log("tracew %.2f: %08X = %08X (float %g)", now_s(), addr, v, rdf(addr)); last = v; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void peek_main(std::vector<uint32_t> addrs) {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string line;
        char b[64];
        for (uint32_t a : addrs) {
            uint32_t bits = ld32(a);
            float v;
            std::memcpy(&v, &bits, 4);
            std::snprintf(b, sizeof b, " %08X=%.5f", a, v);
            line += b;
        }
        rt_log("peek %.1f:%s", now_s(), line.c_str());
    }
}

} //namespace

void debug_classic(ClassicState& s) {
    double t = now_s();
    for (auto& p : presses)
        if (t >= p.a && t < p.b) {
            s.buttons |= p.bits;
            if (p.bits & 0x2000) s.lt = 1.0f; //L and R are also analogue triggers
            if (p.bits & 0x0200) s.rt = 1.0f;
        }
    for (auto& h : holds)
        if (t >= h.a && t < h.b) {
            if (h.stick == 'r') { s.rx = h.x; s.ry = h.y; }
            else { s.lx = h.x; s.ly = h.y; }
        }
}

void debug_mouse(float& dx, float& dy) {
    static double last = now_s();
    double t = now_s(), dt = t - last;
    last = t;
    for (auto& h : moves)
        if (t >= h.a && t < h.b) { dx += h.x * (float)dt; dy += h.y * (float)dt; }
}

//GE_TRACELOOK=1 logs the look input vectors (controller+0x968, +0x978) after ge_look_input when they change
PPCFunc orig_look_trace = nullptr;
void look_trace(PPCContext& c) {
    uint32_t ctrl = c.r[3], pad = c.r[4];
    orig_look_trace(c);
    static uint32_t last[8];
    uint32_t now[8];
    for (int i = 0; i < 8; ++i) now[i] = ld32(ctrl + 0x968 + 4 * i);
    static int n = 0, calls = 0;
    if (calls++ < 3) rt_log("look: call %d, ctrl %08X pad %08X -> %d", calls, ctrl, pad, (int)c.r[3]);
    if (std::memcmp(now, last, sizeof now) && n < 60) {
        ++n;
        std::memcpy(last, now, sizeof now);
        rt_log("look %.2f: ctrl %08X pad %08X dt %g | %g %g %g %g | %g %g %g %g", now_s(), ctrl, pad, rdf(ctrl + 0x578),
               rdf(ctrl + 0x968), rdf(ctrl + 0x96C), rdf(ctrl + 0x970), rdf(ctrl + 0x974),
               rdf(ctrl + 0x978), rdf(ctrl + 0x97C), rdf(ctrl + 0x980), rdf(ctrl + 0x984));
    }
}

void debug_install() {
    if (std::getenv("GE_TRACELOOK")) orig_look_trace = ppc_hook("ge_look_input", look_trace);
    if (const char* e = std::getenv("GE_BUTTONS")) {
        for (const char* q = e; *q;) {
            Press p{};
            char* end;
            p.a = std::strtod(q, &end);
            p.b = std::strtod(end + 1, &end);
            p.bits = (uint32_t)std::strtoul(end + 1, &end, 16);
            presses.push_back(p);
            q = end;
            while (*q == ' ') ++q;
        }
    }
    if (const char* e = std::getenv("GE_MOUSEMOVE")) {
        for (const char* q = e; *q;) {
            Hold h{};
            char* end;
            h.a = std::strtod(q, &end);
            h.b = std::strtod(end + 1, &end);
            h.x = std::strtof(end + 1, &end);
            h.y = std::strtof(end + 1, &end);
            moves.push_back(h);
            q = end;
            while (*q == ' ') ++q;
        }
    }
    if (const char* e = std::getenv("GE_STICK")) {
        for (const char* q = e; *q;) {
            Hold h{};
            char* end;
            h.a = std::strtod(q, &end);
            if (*end != '-') break;
            h.b = std::strtod(end + 1, &end);
            if (*end != ':') break;
            h.stick = end[1];
            h.x = std::strtof(end + 2, &end);
            h.y = std::strtof(end + 1, &end);
            holds.push_back(h);
            q = end;
            while (*q == ' ') ++q;
        }
        rt_log("debug: %zu scripted stick holds", holds.size());
    }
    if (const char* e = std::getenv("GE_SCAN")) {
        std::vector<double> times;
        for (const char* q = e; *q;) {
            char* end;
            times.push_back(std::strtod(q, &end));
            q = *end ? end + 1 : end;
        }
        if (times.size() >= 2) std::thread(scan_main, times).detach();
    }
    if (const char* e = std::getenv("GE_POKE")) {
        std::vector<Poke> pokes;
        for (const char* q = e; *q;) {
            Poke p{};
            char* end;
            p.t = std::strtod(q, &end);
            p.addr = (uint32_t)std::strtoul(end + 1, &end, 16);
            if (*end == '+') { p.add = true; ++end; }
            ++end; //'='
            p.is_float = *end == 'f';
            if (p.is_float) p.f = std::strtof(end + 1, &end);
            else p.x = (uint32_t)std::strtoul(end + 1, &end, 16);
            pokes.push_back(p);
            q = end;
            while (*q == ' ') ++q;
        }
        std::thread(poke_main, pokes).detach();
    }
    if (const char* e = std::getenv("GE_PROBE")) {
        char* end;
        double t = std::strtod(e, &end);
        uint32_t watch = (uint32_t)std::strtoul(end + 1, &end, 16);
        std::thread(probe_main, t, watch, std::string(end + 1)).detach();
    }
    if (const char* e = std::getenv("GE_OBJ")) {
        char* end;
        uint32_t ptr = (uint32_t)std::strtoul(e, &end, 16);
        uint32_t size = (uint32_t)std::strtoul(end + 1, &end, 16);
        std::vector<double> times;
        while (*end) times.push_back(std::strtod(end + 1, &end));
        std::thread(obj_main, ptr, size, times).detach();
    }
    if (const char* e = std::getenv("GE_FINDPTR")) {
        char* end;
        double t = std::strtod(e, &end);
        uint32_t lo = (uint32_t)std::strtoul(end + 1, &end, 16), hi = (uint32_t)std::strtoul(end + 1, &end, 16);
        std::thread(findptr_main, t, lo, hi).detach();
    }
    if (const char* e = std::getenv("GE_SETALL")) {
        char* end;
        double t = std::strtod(e, &end);
        float from = std::strtof(end + 1, &end), to = std::strtof(end + 1, &end);
        uint32_t lo = 0x80000000u, hi = 0x94000000u;
        if (*end == ':') { lo = (uint32_t)std::strtoul(end + 1, &end, 16); hi = (uint32_t)std::strtoul(end + 1, &end, 16); }
        std::thread(setall_main, t, from, to, lo, hi).detach();
    }
#ifndef _WIN32
    if (const char* e = std::getenv("GE_WATCH")) {
        char* end;
        double t = std::strtod(e, &end);
        std::thread(watch_main, t, (uint32_t)std::strtoul(end + 1, &end, 16)).detach();
    }
#endif
    if (const char* e = std::getenv("GE_TRACEW"))
        for (const char* q = e; *q;) {
            char* end;
            std::thread(tracew_main, (uint32_t)std::strtoul(q, &end, 16)).detach();
            q = *end ? end + 1 : end;
        }
    if (const char* e = std::getenv("GE_PEEK")) {
        std::vector<uint32_t> addrs;
        for (const char* q = e; *q;) {
            char* end;
            addrs.push_back((uint32_t)std::strtoul(q, &end, 16));
            q = *end ? end + 1 : end;
        }
        std::thread(peek_main, addrs).detach();
    }
}
