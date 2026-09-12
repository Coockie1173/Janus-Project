// GoldenEye 007 (Wii, SJBE52) — the port's own layer on the wiikit runtime.
//
// Everything every Wii game needs lives in wiikit; what only this game needs
// (its hooks, its fixes, its input mapping) lives here. Hooks named here must
// be listed in config/hooks.txt so the recompiler gives them a slot.
//
//     GE_TRACE=1   log the game's own report lines and main()'s entry
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
