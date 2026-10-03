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
//                  the right stick, at a very high sensitivity, in a network game someone else hosts)
//     GE_HOSTS=F   the hosts file for the game's name lookups (default config/hosts.txt)
//     GE_NETLOG=1  log sockets, lookups and TLS sessions
//     GE_GECKO=F   the Gecko codes the port runs each frame (default build/patched/gecko.txt,
//                  from tools/gecko.py; empty: none); GE_GECKO_EXTRA=F[:F...] adds files
//     GE_FOV=D     the field of view in play, degrees (the game's is 49)
//     GE_FPS=N     the frame rate the engine may reach (default: its own 30); auto: 60
//     GE_NATIVE=0  the engine's math routines as recompiled, not the host versions (check: compare them)
//     GE_DMW=F     the memory watch list the debug overlay shows (F3); default: the .dmw in the
//                  project's folder
#include "debug.h"
#include "ge.h"
#include <cstdlib>

namespace ge {

void each_frame(uint32_t frame) {
    gecko_apply();
    fov_apply();
    mouse_frame(frame);
}

namespace {

PPCFunc orig_main = nullptr;

void install() {
    install_hosts();
    install_fov();
    install_gecko();
    install_crash_handler();
    install_profiler();
    debug_install();
    install_fps();
    install_native();
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
    install_overlay();
}

RtGameLayer layer("GoldenEye 007", install);

}  // namespace
}  // namespace ge
