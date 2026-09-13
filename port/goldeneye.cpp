// GoldenEye 007 (Wii, SJBE52) — the port's own layer on the wiikit runtime.
//
// Everything every Wii game needs lives in wiikit; what only this game needs
// (its hooks, its fixes, its input mapping) lives here. Hooks named here must
// be listed in config/hooks.txt so the recompiler gives them a slot.
//
//     GE_TRACE=1   log the game's own report lines and main()'s entry
//     GE_PROFILE=F a sampling profile of the whole process (CPU time, 1 kHz), written to
//                  the file F every 10 s: tools/profile_report.py names the samples
//     GE_MOUSE=G   mouse look: stick deflection per pixel a second of motion (default 0.004;
//                  0 leaves the mouse free and the right stick to the pad)
//     GE_FPS=N     the frame rate the engine may reach (default: its own 30)
#include "rt.h"
#include "video.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
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

void frame_tick(PPCContext& c) {
    uint32_t max = ld32(kFpsMax);
    if (max && max < g_fps) st32(kFpsMax, g_fps);
    if (sleep_ticks) {
        // the limiter spins until the next frame's time: sleep up to just before it
        uint64_t next = (uint64_t)ld32(kNextFrame) << 32 | ld32(kNextFrame + 4), now = os_tb_now();
        uint64_t margin = tb_hz() / 5000;            // 0.2 ms
        if (next > now + margin && next - now < tb_hz()) guest_sleep(c, next - now - margin);
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
    const char* e = std::getenv("GE_FPS");
    if (!e) return;
    int fps = std::atoi(e);
    if (fps <= 30 || fps > 1000) { rt_log("goldeneye: GE_FPS is 31..1000; the game's own 30 kept"); return; }
    orig_tick = ppc_hook("ge_frame_tick", frame_tick);
    if (!orig_tick) { rt_log("goldeneye: ge_frame_tick is not hooked in this build"); return; }
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
    if (once++) _exit(128 + sig);
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
// stick moves (W A S D in the key file), the right stick looks. The mouse's
// motion goes on channel 1's right stick, added to the pad's. A stick sets a
// turning speed, not an angle, so the deflection is the mouse's speed
// (pixels a second, measured between two reads of the controller): the same
// motion then turns the view as far at any frame rate. The stick's travel
// caps a fast flick; looking through the view itself is the step after this.
float g_mouse_gain = 0.004f;

void mouse_on_right_stick(int chan, ClassicState& s) {
    if (chan != 0) return;
    // The game may read the controller more than once a frame (KPAD's
    // samples, WPAD's own): the speed is measured over at least 4 ms and
    // every read in between gets the same deflection.
    static auto last = std::chrono::steady_clock::now();
    static float ax = 0, ay = 0, defl_x = 0, defl_y = 0;
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    float dx = 0, dy = 0;
    video_take_mouse_motion(dx, dy);
    ax += dx;
    ay += dy;
    if (dt >= 0.004f) {
        if (dt > 0.25f) defl_x = defl_y = 0;         // after a pause: no speed to speak of
        else { defl_x = ax / dt * g_mouse_gain; defl_y = ay / dt * g_mouse_gain; }
        ax = ay = 0;
        last = now;
    }
    s.rx = std::clamp(s.rx + defl_x, -1.0f, 1.0f);
    s.ry = std::clamp(s.ry - defl_y, -1.0f, 1.0f);
}

void install_mouse() {
    if (const char* m = std::getenv("GE_MOUSE")) g_mouse_gain = (float)std::atof(m);
    if (g_mouse_gain <= 0) return;
    video_set_relative_mouse(true);
    wpad_set_classic_filter(mouse_on_right_stick);
}

void install() {
    install_crash_handler();
    install_profiler();
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
