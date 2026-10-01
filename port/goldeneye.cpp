// GoldenEye 007 (Wii, SJBE52) — the port's own layer on the wiikit runtime.
//
// Everything every Wii game needs lives in wiikit; what only this game needs
// (its hooks, its fixes, its input mapping) lives here. Hooks named here must
// be listed in config/hooks.txt so the recompiler gives them a slot.
//
//     GE_TRACE=1   log the game's own report lines and main()'s entry
//     GE_PROFILE=F a sampling profile of the whole process (CPU time, 1 kHz), written to
//                  the file F every 10 s: tools/profile_report.py names the samples
//     GE_MOUSE=S   mouse look sensitivity (default 1; 0 leaves the mouse free)
//     GE_MOUSE_MODE=auto|direct|stick   how the mouse turns the view (auto: direct, but through
//                  the right stick in a network game someone else hosts)
//     GE_HOSTS=F   the hosts file for the game's name lookups (default config/hosts.txt)
//     GE_NETLOG=1  log sockets, lookups and TLS sessions
//     GE_GECKO=F   the Gecko codes the port runs each frame (default build/patched/gecko.txt,
//                  from tools/gecko.py; empty: none); GE_GECKO_EXTRA=F[:F...] adds files
//     GE_FOV=D     the field of view in play, degrees (the game's is 49)
//     GE_FPS=N     the frame rate the engine may reach (default: its own 30); auto: the
//                  screen's refresh rate
#include "debug.h"
#include "rt.h"
#include "video.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifndef _WIN32
#include <atomic>
#include <csignal>
#include <string>
#include <sys/time.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace {

PPCFunc orig_main = nullptr;

// The frame rate. The engine is variable-rate: each pass of its main loop
// (ge_frame_tick) runs a frame when OSGetTime has passed the next frame's
// time, and moves its current rate (a float, frames a second, from which
// every time step is derived) one up when the frame was on time and one down
// when it was late, between a lowest and a highest rate. The game sets both
// to 30 in play. Raising the highest is all an unlock takes below VI's
// 59.94 Hz; above it the frame also has to be shown, and the game waits for
// a retrace per frame, so VI itself is run faster (g_vi_speed).
const uint32_t kFpsMax = 0x806E9E44;                 // -0x713C(r13); the lowest is the word before
const uint32_t kFpsNow = 0x806E9E4C;                 // -0x7134(r13): the current rate, a float
const uint32_t kFrameCount = 0x806EBED0;             // -0x50B0(r13): frames run
uint32_t g_fps = 0;
bool g_fps_log = false;
PPCFunc orig_tick = nullptr;

// OSSleepTicks(OSTime): the thread asleep on an alarm, the others running.
const uint32_t kOSSleepTicks = 0x804BBE30;
const uint32_t kNextFrame = 0x806EBEE8;              // -0x5098(r13): the next frame's time, 64-bit ticks
PPCFunc sleep_ticks = nullptr;

void guest_sleep(PPCContext& c, uint64_t ticks) {
    if (!sleep_ticks || !(c.msr & 0x8000)) return;   // only with interrupts on: the alarm ends it
    uint32_t r3 = c.r[3], r4 = c.r[4];
    c.r[3] = (uint32_t)(ticks >> 32);
    c.r[4] = (uint32_t)ticks;
    sleep_ticks(c);
    c.r[3] = r3;
    c.r[4] = r4;
}

extern uint32_t g_look_frame;
extern bool g_stick_took;
void gecko_apply();
void fov_apply();
extern float g_mouse_sens;
void frame_tick(PPCContext& c) {
    uint32_t max = ld32(kFpsMax);
    if (max && max < g_fps) st32(kFpsMax, g_fps);
    if (sleep_ticks) {
        // the limiter spins until the next frame's time: sleep up to just before it
        uint64_t next = (uint64_t)ld32(kNextFrame) << 32 | ld32(kNextFrame + 4), now = os_tb_now();
        uint64_t margin = tb_hz() / 5000;            // 0.2 ms
        if (next > now + margin && next - now < tb_hz()) guest_sleep(c, next - now - margin);
    }
    static uint32_t frame = 0;                       // the tick spins between frames: look once a frame
    uint32_t f = ld32(kFrameCount);
    if (f != frame) {
        frame = f;
        gecko_apply();
        fov_apply();
        if (!g_stick_took && g_look_frame != f - 1 && g_look_frame != f) {   // nobody to turn (a menu, a pause): the motion is dropped
            float dx, dy;
            video_take_mouse_motion(dx, dy);
        }
    }
    orig_tick(c);
    if (g_fps_log) {                                 // GE_TRACE: frames run and the engine's rate, every 5 s
        static auto t0 = std::chrono::steady_clock::now();
        static uint32_t n0 = ld32(kFrameCount);
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - t0).count();
        if (dt >= 5) {
            uint32_t n = ld32(kFrameCount), bits = ld32(kFpsNow);
            float rate;
            std::memcpy(&rate, &bits, 4);
            rt_log("goldeneye: %.1f frames a second; the engine's rate %.0f (lowest %u, highest %u)",
                   (n - n0) / dt, rate, ld32(kFpsMax - 4), ld32(kFpsMax));
            t0 = now;
            n0 = n;
        }
    }
}

void install_fps() {
    orig_tick = ppc_hook("ge_frame_tick", frame_tick);
    if (!orig_tick) rt_die("goldeneye: ge_frame_tick is not hooked in this build");
    const char* e = std::getenv("GE_FPS");
    if (!e) return;
    int fps = std::atoi(e);
    if (!std::strcmp(e, "auto")) {                   // the screen's refresh rate
        fps = 60;
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay()))
                if (m->refresh_rate >= 30) fps = (int)std::lround(m->refresh_rate);
        }
    }
    if (fps <= 30 || fps > 1000) { rt_log("goldeneye: GE_FPS is 31..1000; the game's own 30 kept"); return; }
    g_fps = (uint32_t)fps;
    g_fps_log = std::getenv("GE_TRACE") != nullptr;
    g_gx_early_done = true;                          // the game's thread does not wait for the renderer
    if (fps > 60) {
        // Above VI's rate. VI stays as it is (the SDK and the game time things
        // by retraces); frames are shown as they are drawn, and the game's
        // wait for a retrace once a frame, which is also where its other
        // threads get to run, becomes a sleep until the next frame is due.
        g_present_on_frame = true;
        sleep_ticks = ppc_lookup(kOSSleepTicks);
        ppc_hook("VIWaitForRetrace", [](PPCContext& c) { guest_sleep(c, tb_hz() / 4000); });
    }
    rt_log("goldeneye: frame rate up to %d%s", fps, fps > 60 ? " (frames shown as drawn, not at VI's retrace)" : "");
}

#ifndef _WIN32
// A host fault in recompiled code (a guest null pointer, a jump the
// recompiler did not know): say where the guest was, then die. wiikit has
// this for Windows only. The report takes the OS layer's lock; if the fault
// came from under it, the alarm ends the process anyway.
void on_crash(int sig, siginfo_t* si, void*) {
    static volatile sig_atomic_t once = 0;
    if (once) _exit(128 + sig);
    once = 1;
    alarm(3);
    uintptr_t a = (uintptr_t)si->si_addr, base = (uintptr_t)g_mem;
    if (a >= base && a - base <= 0xFFFFFFFFull)
        std::fprintf(stderr, "\ncrash: signal %d, guest address %08X\n", sig, (uint32_t)(a - base));
    else
        std::fprintf(stderr, "\ncrash: signal %d, host address %p (not guest memory)\n", sig, si->si_addr);
    os_report_running(32);
    std::fflush(stderr);
    _exit(128 + sig);
}

void install_crash_handler() {
    struct sigaction sa = {};
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (int s : {SIGSEGV, SIGBUS, SIGFPE, SIGILL}) sigaction(s, &sa, nullptr);
}

// GE_PROFILE: SIGPROF every millisecond of CPU time, on whichever thread is
// running; the handler keeps the program counter as an offset in the executable.
extern "C" char __executable_start;
std::atomic<uint32_t> g_nsamples{0};
uint32_t* g_samples = nullptr;
const uint32_t kMaxSamples = 4u << 20;

void on_prof(int, siginfo_t*, void* uc) {
    uintptr_t pc = (uintptr_t)((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP];
    uint32_t i = g_nsamples.fetch_add(1, std::memory_order_relaxed);
    if (i < kMaxSamples) g_samples[i] = (uint32_t)(pc - (uintptr_t)&__executable_start);   // outside it: a huge offset
}

void install_profiler() {
    const char* path = std::getenv("GE_PROFILE");
    if (!path) return;
    g_samples = new uint32_t[kMaxSamples];
    struct sigaction sa = {};
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, nullptr);
    itimerval it = {{0, 1000}, {0, 1000}};
    setitimer(ITIMER_PROF, &it, nullptr);
    std::thread([path = std::string(path)] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                uint32_t n = std::min(g_nsamples.load(), kMaxSamples);
                std::fwrite(g_samples, 4, n, f);
                std::fclose(f);
            }
        }
    }).detach();
    rt_log("goldeneye: profiling to %s", path);
}
#else
void install_crash_handler() {}
void install_profiler() {}
#endif

// Keyboard and mouse. The game plays with the Classic Controller: the left
// stick moves (W A S D in the key file), the right stick looks, and still
// does for a pad. The mouse does not go through the stick: its motion is
// added to the view's angles themselves, once a frame, before the game's own
// frame runs. One pixel is one step of the angle at any frame rate, and
// there is no turning-speed cap.
//
// Whose view: every player in a match has a controller object, updated once
// a frame by ge_controller_update (80108A60; controller+4 is its player). The
// game itself decides which controllers take this console's input: before
// the look update fn_8012A1D0 calls fn_80108D70(controller), which in a
// network game looks the controller up among the console's own players and
// returns 0 for those. The mouse is applied in a hook on the update, to the
// first controller of the frame for which that function returns 0. (Globals
// that point at "the" player or camera, 806EB698 and 805D43EC, hold whichever
// was made last: online that is another player.)
// The angles (found with port/debug.cpp's scans and the community's memory
// watch list): the player's yaw is the float at +0xDC, radians, growing to
// the right (the rotation vector at +0xD8; +0x250 and the matrix at +0x98
// are computed from it). The pitch is in the object the player's +0x16C
// points to, at +0x768, radians, negative up (+0x760 follows it).
const uint32_t kControllerNotLocal = 0x80108D70;     // (controller) -> 0 if this console plays it
const uint32_t kPlayerPtr = 0x806EB698;              // a player exists (any): used only to tell menus from play
const uint32_t kPaused = 0x806EB69C;                 // a byte: 1 while the pause menu is up
const float kPi = 3.14159265f;
float g_mouse_sens = 1.0f;                           // GE_MOUSE: 1 is 0.0022 radians a pixel
const float kRadPerPixel = 0.0022f, kPitchLimit = 1.35f;

bool guest_ptr(uint32_t p) { return (p >= 0x80003000u && p < 0x81800000u) || (p >= 0x90000000u && p < 0x94000000u); }
float ldf(uint32_t a) { uint32_t b = ld32(a); float v; std::memcpy(&v, &b, 4); return v; }
void stf_(uint32_t a, float v) { uint32_t b; std::memcpy(&b, &v, 4); st32(a, b); }

// In a network game this console did not start, the host turns the players
// from what each console sends, and a view turned here is not taken. There
// the mouse goes on the right stick instead: real input, with the stick's
// limits (its top turning speed). "A joined game" is what fn_80108D70 itself
// tests: the word at 805D5490 is 1 or 2 in a network game, the one at
// 805D5470 non-zero on the host. GE_MOUSE_MODE=direct or stick forces one.
const uint32_t kNetState = 0x805D5460;
int g_mouse_mode = 0;                                // 0 auto, 1 direct, 2 stick
float g_stick_gain = 0.001f;                         // stick deflection per pixel a second: full stick turns
                                                     // 2.3 radians a second, a pixel is to be 0.0022 radians

bool joined_game() {
    uint32_t mode = ld32(kNetState + 0x30);
    return (mode == 1 || mode == 2) && ld32(kNetState + 0x10) == 0;
}
bool mouse_on_stick() {
    bool stick = g_mouse_mode == 2 || (g_mouse_mode == 0 && joined_game());
    static int said = -1;
    if (said != (int)stick) {
        said = stick;
        rt_log("goldeneye: the mouse %s", stick ? "is on the right stick (a joined network game)" : "turns the view itself");
    }
    return stick;
}

PPCFunc orig_controller_update = nullptr, controller_not_local = nullptr, orig_look_apply = nullptr;
uint32_t g_look_frame = ~0u;                         // the frame the mouse was last applied in
uint32_t g_look_ctrl = 0;                            // the controller it is for, and what is still to turn
float g_look_yaw = 0;

// The mouse's motion for this frame. The pitch is written (the view's own
// angle); the yaw is handed to the game as input: ge_look_apply (80101EC0)
// turns the controller's angular velocity, the float at +0x97C in radians a
// second (what the stick's deflection becomes, smoothed), times the frame's
// time step (+0x578) into rotation. For the one call the mouse's turn over
// that time step is added to it, and taken out again: the game turns the
// player by stick and mouse together, through its own code.
void mouse_look(uint32_t ctrl, uint32_t player) {
    float dx = 0, dy = 0;
    video_take_mouse_motion(dx, dy);
    debug_mouse(dx, dy);
    if (dx == 0 && dy == 0) return;
    static uint32_t seen = 0;
    if (player != seen && std::getenv("GE_TRACE")) {
        uint32_t v = ld32(player + 0x16C);
        rt_log("goldeneye: controller %08X, player %08X, yaw %f; view %08X, pitch %f", ctrl, player,
               ldf(player + 0xDC), v, guest_ptr(v) ? ldf(v + 0x768) : 0.0f);
    }
    seen = player;
    g_look_ctrl = ctrl;
    g_look_yaw += dx * kRadPerPixel * g_mouse_sens;
    uint32_t view = ld32(player + 0x16C);
    if (guest_ptr(view)) {
        float pitch = ldf(view + 0x768);
        if (std::isfinite(pitch) && std::fabs(pitch) < 2.0f)
            stf_(view + 0x768, std::clamp(pitch + dy * kRadPerPixel * g_mouse_sens, -kPitchLimit, kPitchLimit));
    }
}

void look_apply(PPCContext& c) {
    uint32_t ctrl = c.r[3];
    float dt = guest_ptr(ctrl) ? ldf(ctrl + 0x578) : 0;
    if (ctrl != g_look_ctrl || g_look_yaw == 0 || !(dt > 0.0005f && dt < 0.2f)) { orig_look_apply(c); return; }
    float rate = ldf(ctrl + 0x97C), add = g_look_yaw / dt;
    g_look_yaw = 0;
    stf_(ctrl + 0x97C, rate + add);
    orig_look_apply(c);
    stf_(ctrl + 0x97C, ldf(ctrl + 0x97C) - add);
}

void controller_update(PPCContext& c) {
    uint32_t ctrl = c.r[3], frame = ld32(kFrameCount);
    uint32_t player = guest_ptr(ctrl) ? ld32(ctrl + 4) : 0;
    if (g_mouse_sens > 0 && frame != g_look_frame && guest_ptr(player) && !ld8(kPaused) && !mouse_on_stick()) {
        PPCContext saved = c;                        // the game's own test, its registers kept
        controller_not_local(c);
        bool local = (int8_t)c.r[3] == 0;
        c = saved;
        if (local) {
            g_look_frame = frame;
            mouse_look(ctrl, player);
        }
    }
    orig_controller_update(c);
    static const bool angles = std::getenv("GE_TRACE") != nullptr;
    if (angles && ctrl == g_look_ctrl && guest_ptr(player)) {     // the turned player's angles, twice a second
        static auto last = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - last >= std::chrono::milliseconds(500)) {
            last = now;
            uint32_t v = ld32(player + 0x16C);
            rt_log("goldeneye: yaw %.4f pitch %.4f", ldf(player + 0xDC), guest_ptr(v) ? ldf(v + 0x768) : 0.0f);
        }
    }
    static const bool trace = std::getenv("GE_TRACELOOK") != nullptr;
    if (trace && guest_ptr(ctrl)) {                  // the controller's look vectors, when they change
        static uint32_t last[8];
        static int n = 0;
        uint32_t now[8];
        for (int i = 0; i < 8; ++i) now[i] = ld32(ctrl + 0x968 + 4 * i);
        if (std::memcmp(now, last, sizeof now) && n++ < 80) {
            std::memcpy(last, now, sizeof now);
            rt_log("look: ctrl %08X player %08X dt %g flags %02X %02X | %g %g %g %g | %g %g %g %g", ctrl, player,
                   ldf(ctrl + 0x578), ld8(ctrl + 0x57C), ld8(ctrl + 0x57D), ldf(ctrl + 0x968), ldf(ctrl + 0x96C),
                   ldf(ctrl + 0x970), ldf(ctrl + 0x974), ldf(ctrl + 0x978), ldf(ctrl + 0x97C), ldf(ctrl + 0x980),
                   ldf(ctrl + 0x984));
        }
    }
}

// The wheel changes weapon: a notch up is a press of the Classic's D-pad
// right (the next weapon), a notch down of D-pad left (the previous one),
// held 80 ms with 60 ms between presses, so that every notch of a fast spin
// is a press of its own.
const uint32_t kClLeft = 0x0002, kClRight = 0x8000;  // KPAD's Classic bits
void wheel_buttons(ClassicState& s) {
    static int queued = 0;                           // notches still to press: + next, - previous
    static uint32_t held = 0;
    static auto until = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    queued = std::clamp(queued + video_take_wheel(), -4, 4);
    if (now >= until) {
        if (held) { held = 0; until = now + std::chrono::milliseconds(60); }
        else if (queued) {
            held = queued > 0 ? kClRight : kClLeft;
            queued += queued > 0 ? -1 : 1;
            until = now + std::chrono::milliseconds(80);
        }
    }
    s.buttons |= held;
}

// In play the triggers fire (ZR) and aim (ZL); in a menu they do nothing,
// and there they are taken as a (select) and b (back), so that the mouse's
// buttons work the menus. "In play" is a player and no pause: the byte at
// 806EB69C, next to the player pointer, is 1 while the pause menu is up
// (found by pausing and resuming under a word scan). A trigger held when a
// menu closes stays out of play until it is let go: selecting "Continue"
// does not fire a shot.
const uint32_t kClA = 0x0010, kClB = 0x0040, kClZR = 0x0004, kClZL = 0x0080;

bool in_play() { return guest_ptr(ld32(kPlayerPtr)) && ld8(kPaused) == 0; }

void menu_buttons(ClassicState& s) {
    static uint32_t held_from_menu = 0;
    uint32_t triggers = s.buttons & (kClZR | kClZL);
    if (!in_play()) {
        if (triggers & kClZR) s.buttons |= kClA;
        if (triggers & kClZL) s.buttons |= kClB;
        s.buttons &= ~(kClZR | kClZL);
        held_from_menu = triggers;
    } else {
        held_from_menu &= triggers;
        s.buttons &= ~held_from_menu;
    }
}

// The mouse on the right stick (a joined network game): its speed, in pixels
// a second measured over at least 4 ms, as deflection; every read of the
// controller in between gets the same.
bool g_stick_took = false;
void mouse_stick(ClassicState& s) {
    g_stick_took = g_mouse_sens > 0 && in_play() && mouse_on_stick();
    if (!g_stick_took) return;
    static auto last = std::chrono::steady_clock::now();
    static float ax = 0, ay = 0, defl_x = 0, defl_y = 0;
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    float dx = 0, dy = 0;
    video_take_mouse_motion(dx, dy);
    debug_mouse(dx, dy);
    ax += dx;
    ay += dy;
    if (dt >= 0.004f) {
        float g = g_stick_gain * g_mouse_sens;
        if (dt > 0.25f) defl_x = defl_y = 0;
        else { defl_x = ax / dt * g; defl_y = ay / dt * g; }
        ax = ay = 0;
        last = now;
    }
    s.rx = std::clamp(s.rx + defl_x, -1.0f, 1.0f);
    s.ry = std::clamp(s.ry - defl_y, -1.0f, 1.0f);
}

void classic_filter(int chan, ClassicState& s) {
    if (chan != 0) return;
    mouse_stick(s);
    wheel_buttons(s);
    debug_classic(s);
    menu_buttons(s);
}

void install_mouse() {
    if (const char* m = std::getenv("GE_MOUSE")) g_mouse_sens = (float)std::atof(m);
    if (const char* m = std::getenv("GE_MOUSE_MODE")) g_mouse_mode = !std::strcmp(m, "direct") ? 1 : !std::strcmp(m, "stick") ? 2 : 0;
    wpad_set_classic_filter(classic_filter);
    if (g_mouse_sens <= 0) return;
    controller_not_local = ppc_lookup(kControllerNotLocal);
    orig_controller_update = ppc_hook("ge_controller_update", controller_update);
    orig_look_apply = ppc_hook("ge_look_apply", look_apply);
    if (!orig_controller_update || !controller_not_local || !orig_look_apply) rt_die("goldeneye: the controller update is not hooked in this build");
    video_set_relative_mouse(true);
}

// Online. The game's servers (Nintendo WFC's authentication, DemonWare's
// lobby) are gone; a community server answers for them at another address.
// The names are given that address here, as lines of /etc/hosts would.
#ifndef GE_HOSTS_DEFAULT
#define GE_HOSTS_DEFAULT "config/hosts.txt"
#endif
void install_hosts() {
    const char* path = std::getenv("GE_HOSTS");
    if (!path) path = GE_HOSTS_DEFAULT;
    ios_net_log(std::getenv("GE_NETLOG") != nullptr);
    if (!*path) return;
    FILE* f = std::fopen(path, "r");
    if (!f) { rt_log("goldeneye: no hosts file %s: the game's servers as the system resolves them", path); return; }
    char line[512], ip[64], name[256];
    int n = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (char* h = std::strchr(line, '#')) *h = 0;
        if (std::sscanf(line, "%63s %255s", ip, name) == 2) { ios_net_add_host(name, ip); ++n; }
    }
    std::fclose(f);
    rt_log("goldeneye: %d host name(s) from %s", n, path);
}

// Gecko codes. Writes into the game's code are made in the executable before
// it is recompiled (tools/gecko.py, config/gecko.txt); everything else is run
// here once a frame, as a code handler would: RAM writes (00, 02, 04, 08
// serial), conditions (20-2E), the on/off switch (CC), E0 and E2. The files
// are in Dolphin's notation; GE_GECKO_EXTRA adds optional ones
// (config/gecko/*.txt, chosen in the launcher).
#ifndef GE_GECKO_DEFAULT
#define GE_GECKO_DEFAULT "build/patched/gecko.txt"
#endif
// A CC line's switch starts on (the handler keeps it in the line's second word, where 0 is
// "execute"): what follows it runs from the start, and stops the first time the condition
// before it becomes true.
struct GeckoLine { uint32_t a, b; bool was = false, on = true; };
std::vector<std::vector<GeckoLine>> g_gecko;

bool gecko_ok(uint32_t addr, uint32_t size) {
    return (addr >= 0x80003000u && addr + size <= 0x81800000u) || (addr >= 0x90000000u && addr + size <= 0x94000000u);
}
void gecko_store(uint32_t addr, uint32_t size, uint32_t v) {
    if (!gecko_ok(addr, size)) return;
    if (size == 1) st8(addr, (uint8_t)v);
    else if (size == 2) st16(addr, (uint16_t)v);
    else st32(addr, v);
}

void gecko_apply() {
    for (auto& code : g_gecko) {
        int skip = 0;                                // > 0: inside that many conditions that failed
        for (size_t i = 0; i < code.size(); ++i) {
            GeckoLine& l = code[i];
            uint32_t type = l.a >> 24 & 0xFE, addr = 0x80000000u | (l.a & 0x01FFFFFFu);
            if (type >= 0x20 && type <= 0x2E) {      // if: 32-bit (20-26) or 16-bit with a mask (28-2E)
                if (l.a & 1) { skip = std::max(0, skip - 1); addr &= ~1u; }   // an endif first
                if (skip) { ++skip; continue; }
                bool wide = type < 0x28, ok = gecko_ok(addr, wide ? 4 : 2);
                uint32_t have = !ok ? 0 : wide ? ld32(addr) : (uint32_t)ld16(addr) & ~(l.b >> 16);
                uint32_t want = wide ? l.b : l.b & 0xFFFF;
                switch (type & 6) {
                case 0: ok = ok && have == want; break;
                case 2: ok = ok && have != want; break;
                case 4: ok = ok && have > want; break;
                case 6: ok = ok && have < want; break;
                }
                if (!ok) skip = 1;
            } else if (type == 0xCC) {               // switch: flips when what precedes it becomes true
                bool now = skip == 0;
                if (now && !l.was) l.on = !l.on;
                l.was = now;
                skip = l.on ? 0 : 1;
            } else if (type == 0xE0) skip = 0;
            else if (type == 0xE2) skip = std::max(0, skip - (int)(l.b & 0xFF));
            else if (type == 0x08) {                 // serial write: this line and the next
                if (i + 1 >= code.size()) break;
                GeckoLine& n = code[++i];
                if (skip) continue;
                uint32_t size = n.a >> 28 == 0 ? 1 : n.a >> 28 == 1 ? 2 : 4;
                for (uint32_t k = 0; k <= (n.a >> 16 & 0xFFF); ++k)
                    gecko_store(addr + k * (n.a & 0xFFFF), size, l.b + k * n.b);
            } else if (skip) continue;
            else if (type == 0x00) { for (uint32_t k = 0; k <= l.b >> 16; ++k) gecko_store(addr + k, 1, l.b); }
            else if (type == 0x02) { for (uint32_t k = 0; k <= l.b >> 16; ++k) gecko_store(addr + 2 * k, 2, l.b); }
            else if (type == 0x04) gecko_store(addr, 4, l.b);
        }
    }
}

void gecko_load(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) { rt_log("goldeneye: no Gecko file %s", path); return; }
    char line[512];
    size_t before = g_gecko.size();
    while (std::fgets(line, sizeof line, f)) {
        unsigned a, b;
        if (line[0] == '$') g_gecko.emplace_back();
        else if (line[0] != '#' && line[0] != '*' && std::sscanf(line, "%8x %8x", &a, &b) == 2 && !g_gecko.empty()) {
            uint32_t type = a >> 24 & 0xFE;
            bool known = type <= 0x08 || (type >= 0x20 && type <= 0x2E) || type == 0xCC || type == 0xE0 || type == 0xE2;
            if (!known) rt_log("goldeneye: Gecko line %08X %08X in %s is not supported, skipped", a, b, path);
            else g_gecko.back().push_back({a, b});
        }
    }
    std::fclose(f);
    if (g_gecko.size() > before) rt_log("goldeneye: %zu Gecko code(s) from %s", g_gecko.size() - before, path);
}

void install_gecko() {
    const char* path = std::getenv("GE_GECKO");
    if (!path) path = GE_GECKO_DEFAULT;
    if (*path) {
        if (FILE* f = std::fopen(path, "r")) { std::fclose(f); gecko_load(path); }
    }
    if (const char* extra = std::getenv("GE_GECKO_EXTRA")) {   // more files, ':' between them
        std::string s = extra;
        for (size_t p = 0; p < s.size();) {
            size_t e = s.find(':', p);
            if (e == std::string::npos) e = s.size();
            if (e > p) gecko_load(s.substr(p, e - p).c_str());
            p = e + 1;
        }
    }
}

// The field of view. The word at 806EB694 (the global before the player
// pointer) points to an object whose float at +0x328 is the view's angle in
// degrees: 49 in play. (Found by setting every 49.0 in memory and halving the
// range: this one alone widens the world, and leaves the HUD as it is; the
// executable's own 49.0 constants are not read once a level is up.) Only the
// game's 49 is replaced, each frame, so whatever else sets an angle of its own
// (a cutscene) keeps it.
const uint32_t kViewSettingsPtr = 0x806EB694;
const float kGameFov = 49.0f;
float g_fov = 0;

void fov_apply() {
    if (g_fov <= 0) return;
    uint32_t o = ld32(kViewSettingsPtr);
    if (guest_ptr(o) && ldf(o + 0x328) == kGameFov) stf_(o + 0x328, g_fov);
}

void install_fov() {
    const char* e = std::getenv("GE_FOV");
    if (!e) return;
    float f = (float)std::atof(e);
    if (f < 20 || f > 140) { rt_log("goldeneye: GE_FOV is 20..140 degrees; the game's 49 kept"); return; }
    if (f != kGameFov) { g_fov = f; rt_log("goldeneye: field of view %.0f degrees", f); }
}

void install() {
    install_hosts();
    install_fov();
    install_gecko();
    install_crash_handler();
    install_profiler();
    debug_install();
    install_fps();
    if (std::getenv("GE_TRACE")) {
        orig_main = ppc_hook("main", [](PPCContext& c) {
            rt_log("goldeneye: main() reached");
            orig_main(c);
        });
    }
    // The game is played with the Remote and Nunchuk or the Classic
    // Controller; start with the Classic (keys, mouse, gamepads).
    wpad_set_classic(true);
    install_mouse();
}

RtGameLayer layer("GoldenEye 007", install);

}  // namespace
