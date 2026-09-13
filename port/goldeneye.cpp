// GoldenEye 007 (Wii, SJBE52) — the port's own layer on the wiikit runtime.
//
// Everything every Wii game needs lives in wiikit; what only this game needs
// (its hooks, its fixes, its input mapping) lives here. Hooks named here must
// be listed in config/hooks.txt so the recompiler gives them a slot.
//
//     GE_TRACE=1   log the game's own report lines and main()'s entry
//     GE_FPS=N     the frame rate the engine may reach (default: its own 30)
#include "rt.h"
#include "video.h"
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <csignal>
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
uint32_t g_fps = 0;
PPCFunc orig_tick = nullptr;

void frame_tick(PPCContext& c) {
    uint32_t max = ld32(kFpsMax);
    if (max && max < g_fps) st32(kFpsMax, g_fps);
    orig_tick(c);
}

void install_fps() {
    const char* e = std::getenv("GE_FPS");
    if (!e) return;
    int fps = std::atoi(e);
    if (fps <= 30 || fps > 1000) { rt_log("goldeneye: GE_FPS is 31..1000; the game's own 30 kept"); return; }
    orig_tick = ppc_hook("ge_frame_tick", frame_tick);
    if (!orig_tick) { rt_log("goldeneye: ge_frame_tick is not hooked in this build"); return; }
    g_fps = (uint32_t)fps;
    if (fps > 60) g_vi_speed = fps / 59.94;
    rt_log("goldeneye: frame rate up to %d%s", fps, fps > 60 ? " (VI retraces at that rate too)" : "");
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
#else
void install_crash_handler() {}
#endif

void install() {
    install_crash_handler();
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
}

RtGameLayer layer("GoldenEye 007", install);

}  // namespace
