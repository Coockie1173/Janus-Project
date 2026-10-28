//debugging aids for finding things in the running game
//times are seconds from start
//
//GE_STICK="42-43:r1,0 45-46:l0,1" hold a stick (r or l, then x,y in -1..1 with y up)
//GE_BUTTONS="66-66.3:0004 69-70:2000" hold classic buttons, KPAD's bits in hex (10000 on: Extra 1...)
//(ZR 0004, ZL 0080, R 0200, L 2000, a 0010, b 0040, x 0008, y 0020)
//GE_KEYS="5:F1 6:Down 7:Return 9:Left_Shift" press keys, SDL's names with _ for a space
//GE_MOUSEMOVE="66-67:300,0" fake mouse motion in pixels a second (x right, y down)
//GE_SCAN="41,43.5,44.5,46.5,49" snapshots guest memory at those times, then lists the floats
//(build/scan.txt) that changed over every odd interval and stood still over every even one
//turn the view with GE_STICK in the odd ones and the view's angles are in there
//GE_SCANINT=1 does it with raw words instead
//GE_SCANWATCH="55,65" after a GE_SCANPAT scan: the first float that rose by that much over the
//first pair gets GE_WATCH'ed and the 0x300 bytes around it go to build/scan-obj.txt
//GE_SCANPAT="==+.-" gives the rule itself, a letter for each pair of snapshots: = the same word,
//! another, + - a float gone up or down, u d a word gone up or down, anything else: no rule
//GE_POKE="50:80ABCDEF=f1.5 52:80ABCDEF+=f0.2" store or add a float (f) or a hex word (x)
//GE_PROBE="64:816F0270:build/cands.txt" for each address in the file add 0.35 to its float,
//wait 80 ms, say if the watched float followed, then put it back
//finds the value a derived one gets computed from
//GE_OBJ="806EB698:400:79,81.5,84.5" dumps the object a pointer points to (hex byte count)
//as words and floats at each time, into build/obj.txt
//GE_FINDPTR="66:80848000:808491A4" every guest word holding an address in that range
//GE_DUMP="66:80700000:80800000:build/heap.bin" the guest's memory in that range, into a file
//GE_SETALL="62:49=80:80000000:94000000" every float equal to the first value in the range
//becomes the second, to find which copy of a constant matters by halving the range
//GE_WATCH="64:807807AC" who writes that guest word: the page goes read-only and a write logs
//the host code offset (tools/profile_report.py --offsets names it) and the guest call chain
//"64:*806EB698+FC" watches the word that far into what the pointer holds at that time
//GE_TRACEW="80776B10" log that word whenever it changes (checked 200 times a second)
//GE_PEEK="80ABCDEF,80ABCDF0" log those floats every half second
//GE_DEBUGPRINT=1 logs what the game's debugPrint is given (the sound layer's "Loaded soundbank
//%08X", "Element %08X not found in soundbank %08X"...): integers and strings, floats as ?
//GE_RECORD=F writes the classic controller's state, the mouse's motion and what the hotkeys
//set (noclip, the frame rate...) as played into F
//GE_REPLAY=F plays F back in their place, then hands the controls back. with it every time
//above counts from the replay's end (tools/record_run.sh, and REPLAY=name for scripted_run.sh)
//GE_REPLAY_FPS=N the frame rate from the replay's end on (a recording only fits the rate it was made at)
//GE_REPLAY_FROM=N leaves out the recording's first N parts (a part ends where a level loads or
//is left), the controls are free until then. the menus before a level run at another pace
//with other settings, so they're better driven by WIIKIT_PAD and the level by the recording
#include "debug/debug.h"
#include "ge.h"
#include "mem.h"
#include "rt.h"
#include "video.h"
#include <SDL3/SDL.h>
#ifndef _WIN32
#include <csignal>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point t0 = Clock::now();
//the guest's time: a fast-forwarded cutscene counts for as long as it would have taken
double now_s() { return std::chrono::duration<double>(rt_now() - t0).count(); }

//a replay (GE_REPLAY) gets the game somewhere first, the scripted times count from its end
std::atomic<bool> replaying{false};
std::atomic<double> script_base{0};
double script_s() { return replaying ? -1.0 : now_s() - script_base; }
void sleep_until_s(double t) {
    while (replaying) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    double d = t - script_s();
    if (d > 0) std::this_thread::sleep_for(std::chrono::duration<double>(d));
}

struct Hold { double a, b; char stick; float x, y; };

struct KeyPress { double t; SDL_Scancode key; };
void KeysMain(std::vector<KeyPress> keys)
{
	for (const KeyPress& k : keys)
	{
		sleep_until_s(k.t);
		for (bool down : {true, false})
		{
			SDL_Event e{};
			e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
			e.key.scancode = k.key;
			e.key.down = down;
			SDL_PushEvent(&e);
			std::this_thread::sleep_for(std::chrono::milliseconds(60));
		}
	}
}
std::vector<Hold> holds, moves;
struct Press { double a, b; uint32_t bits; };
std::vector<Press> presses;

//MEM1 and MEM2. asked for when used: MEM2's size is set while the program starts (port/engine/heap.cpp)
struct Ranges
{
    uint32_t m_Ranges[2][2]{ { 0x80000000u, 0x81800000u }, { 0x90000000u, 0x90000000u + g_mem2_size } };
    const uint32_t (*begin() const)[2] { return m_Ranges; }
    const uint32_t (*end() const)[2] { return m_Ranges + 2; }
};

float fl(const std::vector<uint8_t>& snap, size_t off) {
    uint32_t v;
    std::memcpy(&v, &snap[off], 4);
    v = __builtin_bswap32(v);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void watch_main(double t, uint32_t addr, uint32_t offset = 0, bool pointer = false);
void scan_main(std::vector<double> times) {
    std::vector<std::vector<uint8_t>> snaps;
    for (double t : times) {
        sleep_until_s(t);
        std::vector<uint8_t> s;
        for (auto& r : Ranges()) s.insert(s.end(), host(r[0]), host(r[0]) + (r[1] - r[0]));
        snaps.push_back(std::move(s));
        rt_log("scan: snapshot %zu at %.1f s", snaps.size(), now_s());
    }
    FILE* out = std::fopen("build/scan.txt", "w");
    if (!out) return;
    size_t n = snaps[0].size(), found = 0;
    if (const char* pat = std::getenv("GE_SCANPAT")) { //its own rule a pair of snapshots
        size_t pairs = std::min(std::strlen(pat), snaps.size() - 1);
        float rise_lo = 0, rise_hi = 0;
        uint32_t rise_addr = 0;
        if (const char* w = std::getenv("GE_SCANWATCH")) std::sscanf(w, "%f,%f", &rise_lo, &rise_hi);
        for (size_t off = 0; off + 4 <= n; off += 4) {
            bool ok = true;
            for (size_t i = 0; ok && i < pairs; ++i) {
                uint32_t wa, wb;
                std::memcpy(&wa, &snaps[i][off], 4);
                std::memcpy(&wb, &snaps[i + 1][off], 4);
                float a = fl(snaps[i], off), b = fl(snaps[i + 1], off);
                bool sane = std::isfinite(a) && std::isfinite(b) && std::fabs(a) < 1e6f && std::fabs(b) < 1e6f;
                switch (pat[i]) {
                case '=': ok = wa == wb; break;
                case '!': ok = wa != wb; break;
                case '+': ok = sane && b > a; break;
                case '-': ok = sane && b < a; break;
                case 'u': ok = __builtin_bswap32(wb) > __builtin_bswap32(wa); break;
                case 'd': ok = __builtin_bswap32(wb) < __builtin_bswap32(wa); break;
                default: break; //anything
                }
            }
            if (!ok) continue;
            uint32_t addr = off < 0x01800000u ? 0x80000000u + (uint32_t)off : 0x90000000u + (uint32_t)(off - 0x01800000u);
            std::fprintf(out, "%08X", addr);
            for (auto& sn : snaps) std::fprintf(out, " %02X%02X%02X%02X", sn[off], sn[off + 1], sn[off + 2], sn[off + 3]);
            for (auto& sn : snaps) std::fprintf(out, " %g", fl(sn, off));
            std::fprintf(out, "\n");
            ++found;
            float rise = fl(snaps[1], off) - fl(snaps[0], off);
            if (!rise_addr && rise_hi > 0 && rise >= rise_lo && rise <= rise_hi && addr >= 0x80700000u) rise_addr = addr;
        }
        std::fclose(out);
        rt_log("scan: %zu words in build/scan.txt", found);
        if (rise_addr) {
            if (FILE* obj = std::fopen("build/scan-obj.txt", "w")) {
                for (uint32_t a = rise_addr - 0x200; a < rise_addr + 0x100; a += 4) std::fprintf(obj, "%08X %08X\n", a, ld32(a));
                std::fclose(obj);
            }
#ifndef _WIN32
            watch_main(0, rise_addr);
#endif
        }
        return;
    }
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
        if (base >= 0x80000000u && base < 0x90000000u + g_mem2_size)
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

//the guest's memory from lo to hi as it is at time t, into a file
void dump_main(double t, uint32_t lo, uint32_t hi, std::string path) {
    sleep_until_s(t);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { rt_log("dump: can't write %s", path.c_str()); return; }
    for (uint32_t a = lo; a < hi; a += 4) {
        uint32_t v = ld32(a);
        const uint8_t be[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
        std::fwrite(be, 1, 4, f);
    }
    std::fclose(f);
    rt_log("dump: %08X to %08X in %s", lo, hi, path.c_str());
}

void findptr_main(double t, uint32_t lo, uint32_t hi) {
    sleep_until_s(t);
    int n = 0;
    for (auto& r : Ranges())
        for (uint32_t a = r[0]; a < r[1] && n < 200; a += 4) {
            uint32_t v = ld32(a);
            if (v >= lo && v <= hi) { rt_log("findptr: %08X holds %08X", a, v); ++n; }
        }
    rt_log("findptr: %d found", n);
}

void setall_main(double t, float from, float to, uint32_t lo, uint32_t hi) {
    sleep_until_s(t);
    int n = 0;
    for (auto& r : Ranges())
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
void watch_main(double t, uint32_t addr, uint32_t offset, bool pointer) {
    sleep_until_s(t);
    if (pointer) addr = ld32(addr) + offset; //what the pointer holds at that time
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

//recorded input. the file is lines of text:
//"sens S I" the mouse's sensitivity and invert it was played with
//"c T buttons lx ly rx ry lt rt" the classic controller from T on, written when it changes
//"m T dx dy" mouse motion in pixels
//"h T id value" a hotkey left the setting with that id (hotkeys.h) at that value
//"a" the local player appeared or went (a level loaded or was left), the times after it count from there
//loads never take the same time twice, so the replay waits for each "a" to happen
struct RecPad { double t; uint32_t buttons; float lx, ly, rx, ry, lt, rt; uint32_t extra; }; //extra: the sprint key and its like, 0 in an old recording
struct RecMove { double t; float dx, dy; };
struct RecSet { size_t part; double t; std::string id; float value; };
struct RecPart { std::vector<RecPad> pads; std::vector<RecMove> moves; };
std::mutex rec_lock;
FILE* rec_out = nullptr;
std::vector<RecPart> rec_parts;
std::vector<RecSet> rec_sets; //every part's, they go in order whatever part they were in
size_t rec_part = 0, rec_pad = 0, rec_move = 0, rec_set = 0, rec_from = 0;
double rec_start = 0; //when the current part began
bool rec_player = false;
float rec_sens = 1.0f;
bool rec_invert = false;
RecPad rec_now{};

bool same_pad(const RecPad& a, const ClassicState& s) {
    return a.buttons == s.buttons && a.lx == s.lx && a.ly == s.ly && a.rx == s.rx && a.ry == s.ry && a.lt == s.lt && a.rt == s.rt && a.extra == s.extra;
}

bool load_recording(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    rec_parts.emplace_back();
    char line[160];
    while (std::fgets(line, sizeof line, f)) {
        RecPad p{};
        RecMove m{};
        RecSet h{};
        char id[32];
        int inv = 0;
        if (line[0] == 'a') rec_parts.emplace_back();
        else if (std::sscanf(line, "c %lf %x %f %f %f %f %f %f %x", &p.t, &p.buttons, &p.lx, &p.ly, &p.rx, &p.ry, &p.lt, &p.rt, &p.extra) >= 8)
            rec_parts.back().pads.push_back(p);
        else if (std::sscanf(line, "m %lf %f %f", &m.t, &m.dx, &m.dy) == 3) rec_parts.back().moves.push_back(m);
        else if (std::sscanf(line, "h %lf %31s %f", &h.t, id, &h.value) == 3) {
            h.part = rec_parts.size() - 1;
            h.id = id;
            rec_sets.push_back(h);
        }
        else if (std::sscanf(line, "sens %f %d", &rec_sens, &inv) == 2) rec_invert = inv != 0;
    }
    std::fclose(f);
    return true;
}

void replay_done() {
    script_base = now_s();
    replaying = false;
    if (const char* e = std::getenv("GE_REPLAY_FPS")) ge::set_fps_limit((uint32_t)std::atoi(e));
    rt_log("replay: done at %.1f s, the controls are free", now_s());
}

//a level loading or ending starts the next part, on both sides
void record_part() {
    uint32_t state;
    bool player = ge::local_player_state(state);
    if (player == rec_player) return;
    rec_player = player;
    rec_start = now_s();
    if (rec_out) { std::fprintf(rec_out, "a\n"); rec_now.buttons = ~0u; }
    else if (rec_part + 1 < rec_parts.size()) { ++rec_part; rec_pad = rec_move = 0; }
}

void record_classic(ClassicState& s) {
    std::lock_guard<std::mutex> lk(rec_lock);
    record_part();
    double t = now_s() - rec_start;
    if (rec_out) {
        static bool head = false; //written here, the mouse's settings aren't read yet at install
        if (!head) { head = true; std::fprintf(rec_out, "sens %g %d\n", ge::mouse_sens(), (int)ge::mouse_invert()); }
        if (same_pad(rec_now, s)) return;
        rec_now = {t, s.buttons, s.lx, s.ly, s.rx, s.ry, s.lt, s.rt, s.extra};
        std::fprintf(rec_out, "c %.3f %04X %g %g %g %g %g %g %X\n", t, s.buttons, s.lx, s.ly, s.rx, s.ry, s.lt, s.rt, s.extra);
        std::fflush(rec_out);
        return;
    }
    if (!replaying || rec_part < rec_from) return;
    const RecPart& part = rec_parts[rec_part];
    while (rec_pad < part.pads.size() && part.pads[rec_pad].t <= t) rec_now = part.pads[rec_pad++];
    s.buttons = rec_now.buttons;
    s.extra = rec_now.extra;
    s.lx = rec_now.lx; s.ly = rec_now.ly; s.rx = rec_now.rx; s.ry = rec_now.ry; s.lt = rec_now.lt; s.rt = rec_now.rt;
    if (rec_part + 1 == rec_parts.size() && rec_pad == part.pads.size() && rec_move == part.moves.size() && rec_set == rec_sets.size()) replay_done();
}

void record_mouse(float& dx, float& dy) {
    std::lock_guard<std::mutex> lk(rec_lock);
    double t = now_s() - rec_start;
    if (rec_out) {
        if (dx != 0 || dy != 0) std::fprintf(rec_out, "m %.3f %g %g\n", t, dx, dy);
        return;
    }
    if (!replaying || rec_part < rec_from) return;
    //the same turn whatever the sensitivity is now
    float k = ge::mouse_sens() > 0 ? rec_sens / ge::mouse_sens() : 0;
    const RecPart& part = rec_parts[rec_part];
    dx = dy = 0; //only what was recorded, not the real mouse
    for (; rec_move < part.moves.size() && part.moves[rec_move].t <= t; ++rec_move) {
        dx += part.moves[rec_move].dx * k;
        dy += part.moves[rec_move].dy * (rec_invert != ge::mouse_invert() ? -k : k);
    }
}

} //namespace

void debug_classic(ClassicState& s) {
    if (rec_out || replaying) record_classic(s);
    double t = script_s();
    for (auto& p : presses)
        if (t >= p.a && t < p.b) {
            s.buttons |= p.bits & 0xFFFF;
            s.extra |= p.bits >> 16; //10000 is Extra 1, the sprint
            if (p.bits & 0x2000) s.lt = 1.0f; //L and R are also analogue triggers
            if (p.bits & 0x0200) s.rt = 1.0f;
        }
    for (auto& h : holds)
        if (t >= h.a && t < h.b) {
            if (h.stick == 'r') { s.rx = h.x; s.ry = h.y; }
            else { s.lx = h.x; s.ly = h.y; }
        }
}

void debug_setting(const char* id, float value) {
    std::lock_guard<std::mutex> lk(rec_lock);
    if (!rec_out) return;
    std::fprintf(rec_out, "h %.3f %s %g\n", now_s() - rec_start, id, value);
    std::fflush(rec_out);
}

//a part left out (GE_REPLAY_FROM) or over still sets what it set, right away
bool debug_setting_due(std::string& id, float& value) {
    std::lock_guard<std::mutex> lk(rec_lock);
    if (!replaying || rec_set == rec_sets.size()) return false;
    const RecSet& h = rec_sets[rec_set];
    if (h.part > rec_part || (h.part == rec_part && rec_part >= rec_from && h.t > now_s() - rec_start)) return false;
    id = h.id;
    value = h.value;
    ++rec_set;
    return true;
}

void debug_mouse(float& dx, float& dy) {
    if (rec_out || replaying) record_mouse(dx, dy);
    static double last = now_s();
    double now = now_s(), dt = now - last, t = script_s();
    last = now;
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

//(format, ...)
PPCFunc orig_debug_print = nullptr;
void debug_print(PPCContext& c) {
    std::string out;
    int arg = 4;
    for (uint32_t p = c.r[3]; ge::guest_ptr(p) && ld8(p) && out.size() < 512; ++p) {
        const char ch = (char)ld8(p);
        if (ch != '%') { out += ch; continue; }
        std::string spec{"%"};
        while (ge::guest_ptr(p + 1) && ld8(p + 1) && std::strchr("0123456789.-lh", (char)ld8(p + 1))) spec += (char)ld8(++p);
        const char kind = (char)ld8(++p);
        if (!kind) break;
        if (kind == '%') { out += '%'; continue; }
        if (arg > 10) { out += '?'; continue; }
        if (kind == 's') {
            for (uint32_t q = c.r[arg++], n = 0; ge::guest_ptr(q) && ld8(q) && n < 80; ++q, ++n) out += (char)ld8(q);
        } else if (std::strchr("dixXuc", kind)) {
            char buf[48];
            std::snprintf(buf, sizeof buf, (spec + kind).c_str(), c.r[arg++]);
            out += buf;
        } else {
            out += '?';
        }
    }
    if (!out.empty()) rt_log("debugPrint: %s", out.c_str());
    orig_debug_print(c);
}

void debug_install() {
    if (const char* e = std::getenv("GE_REPLAY")) {
        if (!load_recording(e)) rt_die("debug: no recording %s", e);
        replaying = true;
        if (const char* f = std::getenv("GE_REPLAY_FROM")) rec_from = (size_t)std::atoi(f);
        rt_log("debug: replaying %s, %zu parts", e, rec_parts.size());
    } else if (const char* e = std::getenv("GE_RECORD")) {
        rec_out = std::fopen(e, "w");
        if (!rec_out) rt_die("debug: cannot write %s", e);
        rt_log("debug: recording input into %s", e);
    }
    if (std::getenv("GE_TRACELOOK")) orig_look_trace = ppc_hook("ge_look_input", look_trace);
    if (std::getenv("GE_DEBUGPRINT")) orig_debug_print = ppc_hook("debugPrint", debug_print);
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
    if (const char* e = std::getenv("GE_KEYS")) {
        std::vector<KeyPress> keys;
        std::stringstream all(e);
        for (std::string part; all >> part;) {
            size_t colon = part.find(':');
            if (colon == std::string::npos) continue;
            std::string name = part.substr(colon + 1);
            std::replace(name.begin(), name.end(), '_', ' ');
            SDL_Scancode key = SDL_GetScancodeFromName(name.c_str());
            if (key == SDL_SCANCODE_UNKNOWN) rt_log("goldeneye: GE_KEYS: no key called %s", name.c_str());
            else keys.push_back({std::atof(part.c_str()), key});
        }
        std::thread(KeysMain, keys).detach();
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
    if (const char* e = std::getenv("GE_DUMP")) {
        char* end;
        double t = std::strtod(e, &end);
        uint32_t lo = (uint32_t)std::strtoul(end + 1, &end, 16), hi = (uint32_t)std::strtoul(end + 1, &end, 16);
        std::thread(dump_main, t, lo, hi, std::string(*end ? end + 1 : "dump.bin")).detach();
    }
    if (const char* e = std::getenv("GE_SETALL")) {
        char* end;
        double t = std::strtod(e, &end);
        float from = std::strtof(end + 1, &end), to = std::strtof(end + 1, &end);
        uint32_t lo = 0x80000000u, hi = 0x90000000u + g_mem2_size;
        if (*end == ':') { lo = (uint32_t)std::strtoul(end + 1, &end, 16); hi = (uint32_t)std::strtoul(end + 1, &end, 16); }
        std::thread(setall_main, t, from, to, lo, hi).detach();
    }
#ifndef _WIN32
    if (const char* e = std::getenv("GE_WATCH")) {
        char* end;
        double t = std::strtod(e, &end);
        bool pointer = end[1] == '*';
        uint32_t addr = (uint32_t)std::strtoul(end + (pointer ? 2 : 1), &end, 16);
        uint32_t offset = *end == '+' ? (uint32_t)std::strtoul(end + 1, &end, 16) : 0;
        std::thread(watch_main, t, addr, offset, pointer).detach();
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
