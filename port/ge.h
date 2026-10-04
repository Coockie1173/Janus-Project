//what the port's files share, one file per concern:
//goldeneye.cpp installs everything and runs the per-frame stuff
//framerate.cpp the engine's frame limiter
//input.cpp mouse look, the wheel, mouse buttons in menus
//fov.cpp the field of view
//gecko.cpp gecko codes run each frame
//cutscenes.cpp skip cutscenes
//qte.cpp quick time events that press their own buttons
//movies.cpp skip movies
//files.cpp a finished file read is counted once
//autosave.cpp a checkpoint saves the game
//tweaks.cpp the level timer, the unused extras menu, auto start, no tutorials
//tank.cpp the tank: the mouse on its aim, its buttons, driving at higher frame rates
//noclip.cpp fly through the level
//weapons.cpp a key for each weapon slot
//native.cpp the engine's hot math routines written for the host
//net.cpp the hosts file for the game's servers
//overlay.cpp the debug overlay (F3), memory watches from a .dmw file (dmw.cpp)
//settings.cpp the settings menu (F1)
//hotkeys.cpp keys that change the menu's settings in play (hotkeys.h)
//crashdump.cpp crash, die and stall dumps into build/crashes
//diagnostics.cpp the sampling profiler
//debug.cpp aids for finding things in the running game
#pragma once
#include "rt.h"
#include "video.h"
#include <cstdint>
#include <cstring>

namespace ge {

//the game's memory
const uint32_t kFrameCount = 0x806EBED0; //-0x50B0(r13), frames run
const uint32_t kPlayerPtr = 0x806EB698; //set when any player exists, tells play from menus
const uint32_t kPaused = 0x806EB69C; //a byte, 1 while the pause menu is up

inline bool guest_ptr(uint32_t p) {
    return (p >= 0x80003000u && p < 0x81800000u) || (p >= 0x90000000u && p < 0x94000000u);
}
inline float ldf(uint32_t a) { uint32_t b = ld32(a); float v; std::memcpy(&v, &b, 4); return v; }
inline void stf(uint32_t a, float v) { uint32_t b; std::memcpy(&b, &v, 4); st32(a, b); }
inline bool in_play() { return guest_ptr(ld32(kPlayerPtr)) && ld8(kPaused) == 0; }

//the parts
void each_frame(uint32_t frame); //goldeneye.cpp, called by the limiter's hook once a game frame

void install_fps();
//long steps that nothing waits for, to get through a cutscene. easeRate: the engine's rate
//for steps that are shown a frame each instead, 0 for the long ones
void set_fast_forward(bool on, float easeRate = 0.0f);
float skip_rate(); //frames a second of game time in the long steps
uint32_t engine_top_rate();
float engine_rate(); //the rate the engine steps at right now
const uint32_t kGameFps = 30;
uint32_t fps_limit(); //the most frames a second the engine may run
void set_fps_limit(uint32_t fps);
void install_crash_handler(); //crashdump.cpp
void crash_test();
void crash_thread_stack(); //the calling thread's stack for the crash handler
void install_profiler(); //diagnostics.cpp
void install_mouse(); //input.cpp
void mouse_frame(uint32_t frame);
void mouse_applied(uint32_t frame); //the mouse turned something this frame, its motion was taken
bool mouse_on_stick(); //the mouse goes on the right stick (a joined network game)
const float kRadPerPixel = 0.0022f; //what a pixel of mouse turns at sensitivity 1
void mouse_capture(bool on);
const float kMouseSensMax = 5.0f;
float mouse_sens(); //1 is the default, 0 no mouse look
void set_mouse_sens(float s);
int mouse_mode(); //0 auto, 1 direct, 2 stick
void set_mouse_mode(int m);
uint32_t local_controller(); //0 when there's none (menus, loading)
bool local_player_state(uint32_t& state); //the controller's +0x704 for the local player
bool local_cutscene_locked(); //state 0x22 with the view and the aim both the cutscene's
bool mouse_invert();
void set_mouse_invert(bool on);
const float kAdsSensMin = 0.05f, kAdsSensMax = 2.0f;
float ads_sens(); //the mouse's sensitivity aiming down the sight, times the normal one
void set_ads_sens(float s);
bool local_aim_blend(float& blend); //how far the local player is aimed, 0 to 1
bool aim_lock(); //a press of aim stays aimed until the next
void set_aim_lock(bool on);
const char* local_look_name(); //what the mouse does in that state, "view", "aim", "locked" or "cheat"
bool mouse_everywhere(); //cheat, mouse look in every player state
void set_mouse_everywhere(bool on);
void install_hosts(); //net.cpp
void install_gecko(); //gecko.cpp
void gecko_apply();
void install_fov(); //fov.cpp
void fov_apply();
const float kGameFov = 49.0f, kFovMin = 20.0f, kFovMax = 120.0f;
float fov(); //degrees
void set_fov(float degrees);
const float kGameAdsFov = 22.5f, kAdsFovMin = 10.0f; //the pistol's, aimed down the sight
float ads_fov(); //degrees for a weapon the game aims at 22.5, the others in proportion
void set_ads_fov(float degrees);
void ads_fov_apply(uint32_t controller); //after the local controller's update
void install_native(); //native.cpp
void install_cutscenes(); //cutscenes.cpp
bool skip_cutscenes(); //a mission's opening cutscene is skipped
void set_skip_cutscenes(bool on);
void cutscenes_frame(); //once a frame, on the game's thread
bool skipping_cutscene(); //an opening is being skipped right now
bool cutscene_input_waiting(); //qte.cpp, a cutscene asked for a button in the last frames
void install_qte(); //qte.cpp
bool auto_qte(); //quick time events press their own buttons
void set_auto_qte(bool on);
void install_movies(); //movies.cpp
void install_files(); //files.cpp
bool skip_movies(); //a movie that plays once is over before it starts
void set_skip_movies(bool on);
void install_tweaks(); //tweaks.cpp
void install_tank(); //tank.cpp
bool is_tank(uint32_t ctrl); //the controller is the Tank mission's
void tank_buttons(ClassicState& s); //the tank's own button layout, in the classic's filter
void tweaks_apply();
bool level_timer(); //the hud shows the level's time
void set_level_timer(bool on);
bool extras_menu(); //cheat, the menu's unused extras page
void set_extras_menu(bool on);
bool unlock_all(); //cheat, every mission and difficulty can be picked
void set_unlock_all(bool on);
bool auto_start(); //a loaded mission starts without its "Continue" being pressed
void set_auto_start(bool on);
bool no_tutorials(); //the tutorial boxes aren't shown
void set_no_tutorials(bool on);
void install_autosave(); //autosave.cpp
void autosave_frame(PPCContext& c); //once a frame, on the game's thread
bool autosave(); //a checkpoint saves the game
void set_autosave(bool on);
bool joined_game(); //input.cpp
void install_noclip(); //noclip.cpp
bool noclip(); //cheat, the player flies through walls and doesn't fall
void set_noclip(bool on);
void noclip_look(uint32_t controller); //before ge_look_apply, the game's own step is taken out
void noclip_apply(uint32_t controller); //after the local controller's update
void install_weapons(); //weapons.cpp
void weapon_buttons(ClassicState& s); //a slot's key, in the classic's filter
bool sprint_key(); //input.cpp, the sprint key is down this frame
void install_overlay(); //overlay.cpp
bool overlay_open(); //the watches or the settings menu are up, so the mouse is free
bool settings_open(); //settings.cpp
uint32_t settings_buttons(); //classic buttons the menu presses itself (+ to pause as it opens)

} //namespace ge
