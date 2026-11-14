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
//GE_MODPACKS=DIR the modpacks, a folder each of loose .edb files in place of the disc's or on top of them (modpacks by default), GE_MODPACK=NAME the one installed first, GE_MODS=DIR one folder as the only pack
//GE_NETLOG=1 log sockets, lookups and TLS sessions
//GE_GECKO=F gecko codes to run each frame (build/patched/gecko.txt by default, empty for none)
//GE_GECKO_EXTRA=F[:F...] more gecko files
//GE_FOV=D field of view in degrees (the game's is 49)
//GE_ADS_FOV=D field of view aiming down the sight (the game's is 22.5 with the pistol)
//GE_SKIP_CUTSCENES=1 a mission's opening cutscene is skipped
//GE_SKIP_ZUKOVSKY=1 the meeting with Zukovsky in Nightclub is skipped
//GE_SKIP_BRIGGS=1 the meeting with Sky Briggs in Carrier is skipped
//GE_SKIP_ENDINGS=1 a mission's ending cutscene is skipped
//GE_AUTO_QTE=1 quick time events press their own buttons
//GE_LEVEL_TIMER=0 no level timer in the hud
//GE_AUTO_START=1 a loaded mission starts without its "Continue" being pressed
//GE_SKIP_MOVIES=1 movies are skipped
//GE_NO_TUTORIALS=1 no tutorial boxes
//GE_ONLINE_UNLOCKS=0 the loadouts' items and the game modes open at their levels
//GE_NO_COLOUR_FILTER=1 a level's colour filter over the picture isn't drawn
//GE_NO_BLOOM=1 no bloom, GE_DOF=0 no depth of field (1 the game's, 2 high quality), GE_INVERT_FLASH=1 a flash grenade freezes the picture instead of whiting it out, GE_GRAIN=0.5 half the film grain (port/gameplay/screen.cpp)
//GE_AUTOSAVE=0 a checkpoint doesn't save the game
//GE_EXTRAS_MENU=1 the menu's unused extras page
//GE_UNLOCK_ALL=1 every mission and difficulty unlocked
//GE_NOCLIP=1 cheat, start flying through walls
//GE_STAMINA=1 cheat, the sprint that's left as a bar
//GE_SPEEDOMETER=1 cheat, the speed under the crosshair
//GE_GOD=1 GE_INFINITE_AMMO=1 GE_INFINITE_SPRINT=1 cheats, the campaign's player isn't hurt, the ammo and the sprint never run out
//GE_TURBO=x GE_GRENADES=1 cheats, the campaign's player moves x times as fast (1 to 5), carries frag grenades
//GE_COLLISION=1 cheat, the level's collision is drawn in place of the world
//GE_TRIGGERS=1 cheat, the triggers that go off by being walked into are drawn over the world
//GE_SETTINGS=N start with the settings menu (F1) open on page N
//GE_FPS=N frame rate the engine may reach (30 by default, auto is 60)
//GE_VSYNC=1 frames wait for the screen's refresh
//GE_ANISO=N anisotropic filtering, 2 to 16
//GE_MSAA=N anti-aliasing, 2, 4 or 8 samples a pixel
//GE_SHAKE=S the game's screen shake times S, 0 (none) to 5
//GE_DRAWDIST=N details pop in N times further away, 1 (the game's) to 5 (unlimited)
//GE_HUD=0 no hud
//GE_TANK_BUTTONS=A,B,C the buttons of the tank's machine gun, cannon and lock (port/gameplay/tank.cpp)
//GE_SAVED_SETTINGS=0 those five only from the environment, not from the settings the menu saved
//GE_PING=0 no ping in the scoreboard of a modded network game
//GE_OCCLUSION=0 sounds are heard through walls and floors, as on the console
//GE_SOUND_HEIGHT=0 sounds above and below aren't muffled, as on the console
//GE_NATIVE=0 run the recompiled math routines instead of the host ones
//GE_CRASH_MEM=1 put the guest's memory in crash dumps (build/crashes)
//GE_TRACETEXT=1 log the texts the game looks up and the buttons its prompts ask for
//GE_SOUND_CLICKS=1 a new sound's channel starts at 0 dB as in the game, it clicks at high frame rates
//GE_TASLIB=F load TASLib from F (beside the executable by default), 0 for none
//GE_DMW=F watch list for the debug overlay (F3), the .dmw in the project folder by default
#include "debug/debug.h"
#include "ge.h"
#include "ge_version.h"
#include <cstdlib>

namespace ge {

#ifdef GE_IMGUI
void settings_replay_headless(); //ui/settings/menu.cpp
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
    speedrun_frame();
    mouse_frame(frame);
    custom_frame(frame);
    pointer_frame(frame);
    net_frame();
    heap_frame();
    tas_frame(frame);
}

#ifdef GE_DEV
void install_dev(); //port/debug/dev.cpp, local only
#endif

namespace {

PPCFunc orig_main = nullptr;

void install() {
    rt_log("goldeneye: Janus-Project %s", GE_VERSION);
    install_hosts();
    install_live_patch();
    install_command_time();
    install_blend_time();
    install_ping();
    install_mods();
    install_custom_weapons();
    install_mods_menu();
    install_custom();
    install_pointer();
    install_pictures();
    install_fov();
    install_screen();
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
    install_shake();
    install_draw_distance();
    install_typing();
    install_render();
    install_sound();
    install_occlusion();
    install_height();
    install_noclip();
    install_stamina();
    install_cheats();
    install_owned();
    install_randomizer();
    install_bunnyhop();
    install_teleport();
    install_collision();
    install_triggers();
    install_stealth();
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
    install_tas();
#ifdef GE_DEV
    install_dev();
#endif
}

RtGameLayer layer("GoldenEye 007", install);

} //namespace
} //namespace ge
