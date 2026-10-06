//the game layer on top of wiikit, anything only GoldenEye needs goes in port/
//hooks used here have to be in config/hooks.txt or the recompiler won't give them a slot
//
//the environment variables:
//GE_TRACE=1 log the game's own report lines and main()'s entry
//GE_PROFILE=F sampling profile written to F every 10 s, tools/profile_report.py reads it
//GE_MOUSE=S mouse look sensitivity (1 by default, 0 leaves the mouse free)
//GE_MOUSE_MODE=auto|direct|stick how the mouse turns the view
//GE_MOUSE_INVERT=1 pushing the mouse forward looks down
//GE_ADS_SENS=S the mouse's sensitivity aiming down the sight, times the normal one (0.5 by default)
//GE_AIM_LOCK=1 a press of aim stays aimed until the next
//GE_MOUSE_EVERYWHERE=1 cheat, mouse look in every player state
//GE_HOSTS=F hosts file for the game's servers (config/hosts.txt by default)
//GE_MODS=DIR loose .edb files in place of the disc's, or on top of them (mods by default)
//GE_NETLOG=1 log sockets, lookups and TLS sessions
//GE_GECKO=F gecko codes to run each frame (build/patched/gecko.txt by default, empty for none)
//GE_GECKO_EXTRA=F[:F...] more gecko files
//GE_FOV=D field of view in degrees (the game's is 49)
//GE_ADS_FOV=D field of view aiming down the sight (the game's is 22.5 with the pistol)
//GE_SKIP_CUTSCENES=1 a mission's opening cutscene is skipped
//GE_SKIP_ZUKOVSKY=1 the meeting with Zukovsky in Nightclub is skipped
//GE_SKIP_BRIGGS=1 the meeting with Sky Briggs in Carrier is skipped
//GE_AUTO_QTE=1 quick time events press their own buttons
//GE_LEVEL_TIMER=0 no level timer in the hud
//GE_AUTO_START=1 a loaded mission starts without its "Continue" being pressed
//GE_SKIP_MOVIES=1 movies are skipped
//GE_NO_TUTORIALS=1 no tutorial boxes
//GE_NO_COLOUR_FILTER=1 a level's colour filter over the picture isn't drawn
//GE_AUTOSAVE=0 a checkpoint doesn't save the game
//GE_EXTRAS_MENU=1 cheat, the menu's unused extras page
//GE_UNLOCK_ALL=1 cheat, every mission and difficulty unlocked
//GE_NOCLIP=1 cheat, start flying through walls
//GE_COLLISION=1 cheat, the level's collision is drawn in place of the world
//GE_SETTINGS=N start with the settings menu (F1) open on page N
//GE_FPS=N frame rate the engine may reach (30 by default, auto is 60)
//GE_NATIVE=0 run the recompiled math routines instead of the host ones
//GE_CRASH_MEM=1 put the guest's memory in crash dumps (build/crashes)
//GE_TRACETEXT=1 log the texts the game looks up and the buttons its prompts ask for
//GE_DMW=F watch list for the debug overlay (F3), the .dmw in the project folder by default
#include "debug.h"
#include "ge.h"
#include <cstdlib>

namespace ge {

#ifdef GE_IMGUI
void settings_replay_headless(); //settings.cpp
#endif

void each_frame(uint32_t frame) {
#ifdef GE_IMGUI
    if (!video_enabled()) settings_replay_headless();
#endif
    crash_test();
    gecko_apply();
    tweaks_apply();
    fov_apply();
    cutscenes_frame();
    mouse_frame(frame);
    heap_frame();
}

namespace {

PPCFunc orig_main = nullptr;

void install() {
    install_hosts();
    install_mods();
    install_mods_menu();
    install_fov();
    install_gecko();
    install_crash_handler();
    install_profiler();
    debug_install();
    install_fps();
    install_native();
    install_cutscenes();
    install_qte();
    install_tweaks();
    install_movies();
    install_files();
    install_heap();
    install_records();
    install_sweep();
    install_zones();
    install_tank();
    install_body();
    install_unload();
    install_timeline();
    install_flipbooks();
    install_noclip();
    install_collision();
    install_autosave();
    install_weapons();
    install_prompts();
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
