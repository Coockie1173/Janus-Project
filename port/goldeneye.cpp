//the game layer on top of wiikit, anything only GoldenEye needs goes in port/
//hooks used here have to be in config/hooks.txt or the recompiler won't give them a slot
//
//the environment variables:
//GE_TRACE=1 log the game's own report lines and main()'s entry
//GE_PROFILE=F sampling profile written to F every 10 s, tools/profile_report.py reads it
//GE_MOUSE=S mouse look sensitivity (1 by default, 0 leaves the mouse free)
//GE_MOUSE_MODE=auto|direct|stick how the mouse turns the view
//GE_MOUSE_INVERT=1 pushing the mouse forward looks down
//GE_MOUSE_EVERYWHERE=1 cheat, mouse look in every player state
//GE_HOSTS=F hosts file for the game's servers (config/hosts.txt by default)
//GE_NETLOG=1 log sockets, lookups and TLS sessions
//GE_GECKO=F gecko codes to run each frame (build/patched/gecko.txt by default, empty for none)
//GE_GECKO_EXTRA=F[:F...] more gecko files
//GE_FOV=D field of view in degrees (the game's is 49)
//GE_SKIP_CUTSCENES=1 missions start at the checkpoint after the opening cutscene
//GE_LEVEL_TIMER=0 no level timer in the hud
//GE_EXTRAS_MENU=1 cheat, the menu's unused extras page
//GE_SETTINGS=N start with the settings menu (F1) open on page N
//GE_FPS=N frame rate the engine may reach (30 by default, auto is 60)
//GE_NATIVE=0 run the recompiled math routines instead of the host ones
//GE_CRASH_MEM=1 put the guest's memory in crash dumps (build/crashes)
//GE_DMW=F watch list for the debug overlay (F3), the .dmw in the project folder by default
#include "debug.h"
#include "ge.h"
#include <cstdlib>

namespace ge {

void each_frame(uint32_t frame) {
    crash_test();
    gecko_apply();
    tweaks_apply();
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
    install_cutscenes();
    install_tweaks();
    if (std::getenv("GE_TRACE")) {
        orig_main = ppc_hook("main", [](PPCContext& c) {
            rt_log("goldeneye: main() reached");
            orig_main(c);
        });
    }
    //start on the classic controller, that's what keys, mouse and gamepads map to
    wpad_set_classic(true);
    install_mouse();
    install_overlay();
}

RtGameLayer layer("GoldenEye 007", install);

} //namespace
} //namespace ge
