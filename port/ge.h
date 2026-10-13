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
//records.cpp tells who frees an animator's collision records while they are held
//heap.cpp a free of something that isn't a block is logged and left out
//sweep.cpp a moving shape's sweep is kept to a number of steps that ends
//zones.cpp keeps the level behind a portal drawn while the camera is right at it, and logs the portal walk
//tweaks.cpp the level timer, the unused extras menu, auto start, no tutorials, online unlocks
//tank.cpp the tank: the mouse on its aim, its buttons, driving at higher frame rates
//noclip.cpp fly through the level
//stamina.cpp the sprint that's left as a bar
//weapons.cpp a key for each weapon slot
//prompts.cpp the bound key in the game's prompts, where it shows a controller button
//collision.cpp the level's collision drawn in place of the world
//body.cpp a blocked player moves in the console's steps, so vents can be got through at any frame rate
//unload.cpp a file that's let go stays loaded as long as on the console, so cutscenes end at any frame rate
//timeline.cpp a timeline that waits at an event stays at its frame, so doors are shut after a skipped cutscene
//flipbooks.cpp animated textures (fire, screens) play at the console's speed at any frame rate
//shake.cpp the camera's sway (Nightclub's dance floor) goes by time, not by frames
//typing.cpp a typed text (a mission's place and time, an objective) comes at the console's speed at any frame rate
//sound.cpp a sound starts silent, so it doesn't click at higher frame rates
//occlusion.cpp a sound behind a wall or a floor is quieter
//height.cpp a sound above or below the listener is muffled
//mods.cpp loose .edb files in place of the disc's, or on top of them
//modmenu.cpp Extras > Mods and Extras > Level Select in the game's menu: the levels among the mods, every level of the disc
//pictures.cpp a mod level's .png as its picture in the menus
//native.cpp the engine's hot math routines written for the host
//net.cpp the hosts file for the game's servers
//overlay.cpp the debug overlay (F3), memory watches from a .dmw file (dmw.cpp)
//settings.cpp the settings menu (F1)
//render.cpp anisotropic filtering and anti-aliasing, the renderer's
//hotkeys.cpp keys that change the menu's settings in play (hotkeys.h)
//custom.cpp 007 Custom: a mission as 007 Classic with the guards set by sliders
//crashdump.cpp crash, die and stall dumps into build/crashes
//diagnostics.cpp the sampling profiler
//debug.cpp aids for finding things in the running game
#pragma once
#include "rt.h"
#include "video.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ge {

//the game's memory
const uint32_t kFrameCount = 0x806EBED0; //-0x50B0(r13), frames run
const uint32_t kPlayerPtr = 0x806EB698; //set when any player exists, tells play from menus
const uint32_t kPaused = 0x806EB69C; //a byte, 1 while the pause menu is up

inline bool guest_ptr(uint32_t p) {
    return (p >= 0x80003000u && p < 0x81800000u) || (p >= 0x90000000u && p < 0x90000000u + g_mem2_size);
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
bool vsync(); //frames wait for the screen's refresh
void set_vsync(bool on);
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
bool local_in_cutscene(); //state 0x22, whatever the cutscene leaves the player
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
void install_live_patch(); //the server's patch of the game's code is left out
void install_blend_time(); //the others online are drawn to each update over a little more than the updates are apart
void install_command_time(); //a player's step online keeps the part of a millisecond its command cuts off
void net_frame(); //once a frame: the range a player's pitch is sent in
void net_trace_controller(uint32_t ctrl, uint32_t player, bool own); //GE_NETTRACE: a player's place and angles after its update
float net_pitch_range(); //how far up and down it goes to the others, radians
void install_ping(); //the scoreboard of a modded network game has every player's ping next to the name, the host sends them
bool show_ping();
void set_show_ping(bool on);
bool modded_lobby(); //modmenu.cpp: network games are the ones of Connect to WFC Modded
void install_mods(); //mods.cpp
float world_trace(const PPCContext& c, const float start[3], const float way[3]); //the first hit of the level's collision along way from start, 0 to 1 of it, negative for none. on the game's thread
//a level among the mods: its file's hash, name and path, and the gamemode folders it is in, a bit each (0: only in the mods folder itself)
struct ModLevel { uint32_t hash; std::string name; std::string path; uint32_t modes; };
const int kModeCount = 8; //the online gamemodes, each a folder in the mods folder
extern const char* const kModeFolders[kModeCount];
const std::vector<ModLevel>& mod_levels();
const std::vector<ModLevel>& mod_files(); //every mod
const std::vector<ModLevel>& disc_levels(); //every level the disc has (mt_ and mpt_ files), the ones no menu leads to as well. no path
const std::string& mods_folder(); //empty when there's none
void install_mods_menu(); //modmenu.cpp, after install_mods
void mods_menu_frame(PPCContext& c); //once a frame, on the game's thread
std::string game_text(const PPCContext& c, uint32_t hash); //prompts.cpp: one of the game's texts, empty when it has none
std::string mod_side_file(const ModLevel& mod, const char* extension); //mods.cpp: mt_yard.png for mt_yard.edb, next to it or in the mods folder
uint32_t mods_menu_text(uint32_t hash); //the characters of one of the Mods menu's texts, 0 for any other
std::string mod_shown_name(const std::string& file); //a mod level's name in the menus, from its file's
void install_custom(); //custom.cpp
const int kCustomSliders = 4; //enemy health, enemy damage, enemy reaction speed, hit protection
const int kCustomRows = 6; //the page's items: the sliders, the headshots' switch, Start Mission
void custom_frame(uint32_t frame); //once a frame, on the game's thread
void custom_buttons(ClassicState& s); //in the classic's filter: a press on a slider isn't a select
const uint32_t kCustomSetting = 0x7D01F000; //the sliders as settings of the game's menu, a hash each from here. the game's own stop at 7D0100xx
const uint32_t kCustomHeadshots = kCustomSetting + kCustomSliders; //the switch for headshots that kill, a setting with values to pick from
const uint32_t kCustomTextOn = 0x4300F0E4, kCustomTextOff = 0x4300F0E5; //the texts of its two values, modmenu.cpp has them
bool custom_mode(); //the mission is played with them
void set_custom_mode(bool on);
std::string custom_label(int slider); //"Enemy Health: 250%"
const char* custom_about(int slider); //the line that says what it is
std::string custom_values(); //the four as the settings file keeps them
bool custom_start_row(uint32_t row); //modmenu.cpp: the menu row is 007 Custom's "Start Mission"
void install_pictures(); //pictures.cpp, after install_mods
void pictures_shoot(); //F12: the picture on screen becomes the loaded mod level's .png
bool pictures_hiding(); //the hud and the gun are left out for a picture
void pictures_frame(); //once a frame on the game's thread
std::string pictures_draw(); //on the renderer's thread before the overlay, what to say when a picture was made
void pictures_file(uint32_t hash, uint32_t file); //a file (the game's object for it) was given its memory
void install_gecko(); //gecko.cpp
void gecko_apply();
bool gecko_cheats(); //codes of the launcher's Cheats tab are running
void install_fov(); //fov.cpp
void fov_apply();
const float kGameFov = 49.0f, kFovMin = 20.0f, kFovMax = 120.0f;
float fov(); //degrees
void set_fov(float degrees);
const float kGameAdsFov = 22.5f, kAdsFovMin = 10.0f; //the pistol's, aimed down the sight
float ads_fov(); //degrees for a weapon the game aims at 22.5, the others in proportion
void set_ads_fov(float degrees);
float host_drawn_fov(); //the angle a console draws the local player's aim with, 0 when it isn't aimed
bool no_colour_filter(); //a level's colour filter (a mesh on the eye) isn't drawn
void set_no_colour_filter(bool on);
void install_native(); //native.cpp
void install_cutscenes(); //cutscenes.cpp
void load_level(uint32_t hash); //the game's menu loads that level at its next update, from a menu or from play
bool level_loads(uint32_t hash); //modmenu.cpp: not one of the disc's levels that end the game as they load
std::string map_shown_name(const std::string& file, int& mode); //a multiplayer level's map ("Airfield") and which gamemode it is for, -1 for none
const char* map_mode_name(int mode);
const int kMapModeCount = 8;
bool skip_cutscenes(); //a mission's opening cutscene is skipped
void set_skip_cutscenes(bool on);
bool skip_zukovsky(); //Nightclub's meeting with Zukovsky is skipped (config/cutscenes.txt)
void set_skip_zukovsky(bool on);
bool skip_briggs(); //Carrier's meeting with Sky Briggs is skipped (config/cutscenes.txt)
void set_skip_briggs(bool on);
void cutscenes_frame(); //once a frame, on the game's thread
bool skipping_cutscene(); //an opening is being skipped right now
float cutscene_fade(); //how black the picture is for it, 0 to 1
bool cutscene_input_waiting(); //qte.cpp, a cutscene asked for a button in the last frames
void install_qte(); //qte.cpp
bool auto_qte(); //quick time events press their own buttons
void set_auto_qte(bool on);
void install_movies(); //movies.cpp
void install_files(); //files.cpp
void install_records(); //records.cpp
void records_freed(const PPCContext& c, uint32_t block); //a block is about to be freed
void install_heap(); //heap.cpp
void heap_frame(); //once a frame: the heaps are walked in a network game
std::string call_chain(const PPCContext& c); //who called, from the guest's stack
void install_sweep(); //sweep.cpp
void install_zones(); //zones.cpp
bool zones_mark(); //F9 with GE_ZONELOG: a mark in the log, false when there is none
bool skip_movies(); //a movie that plays once is over before it starts
void set_skip_movies(bool on);
void install_tweaks(); //tweaks.cpp
void install_tank(); //tank.cpp
bool is_tank(uint32_t ctrl); //the controller is the Tank mission's
void tank_buttons(ClassicState& s); //the tank's own button layout, in the classic's filter
const int kTankActionCount = 3, kTankSourceCount = 7; //what the tank does, and the buttons of the actions on foot that can do it
const char* tank_action_label(int action);
const char* tank_action_hint(int action);
const char* tank_source_label(int source);
int tank_source(int action); //the button that does it, a number of the sources
void set_tank_source(int action, int source);
void tweaks_apply();
bool hud(); //the hud is drawn
void set_hud(bool on);
bool level_timer(); //the hud shows the level's time
void set_level_timer(bool on);
bool extras_menu(); //the menu's unused extras page
void set_extras_menu(bool on);
bool unlock_all(); //every mission and difficulty can be picked
void set_unlock_all(bool on);
bool auto_start(); //a loaded mission starts without its "Continue" being pressed
void set_auto_start(bool on);
bool no_tutorials(); //the tutorial boxes aren't shown
void set_no_tutorials(bool on);
bool online_unlocks(); //a loadout's items and the game modes are open at any level, but for the Proximity Mine
void set_online_unlocks(bool on);
void install_autosave(); //autosave.cpp
void autosave_frame(PPCContext& c); //once a frame, on the game's thread
bool autosave(); //a checkpoint saves the game
void set_autosave(bool on);
bool joined_game(); //input.cpp
bool network_game(); //hosted or joined
bool multiplayer_game(); //a network game or a split screen level, not the campaign
void install_noclip(); //noclip.cpp
bool noclip(); //cheat, the player flies through walls and doesn't fall
void set_noclip(bool on);
void noclip_look(uint32_t controller); //before ge_look_apply, the game's own step is taken out
void noclip_apply(uint32_t controller); //after the local controller's update
bool player_state(uint32_t& state, float& fallen, uint32_t& bodyFlags); //the local player as of its last update, false before there is one
void install_stamina(); //stamina.cpp
bool stamina_meter(); //cheat, the sprint that's left as a bar
void set_stamina_meter(bool on);
void stamina_apply(uint32_t controller); //after the local controller's update
struct Stamina { float left, full, cancel, needs; bool sprinting; }; //seconds: left of full, where a cancelled sprint's refill jumps, what a new sprint needs
bool stamina(Stamina& s); //the local player's as of its last update, false with the meter off or nobody in play
void install_collision(); //collision.cpp
void install_body(); //body.cpp
void install_unload(); //unload.cpp
void install_timeline(); //timeline.cpp
void install_flipbooks(); //flipbooks.cpp
void install_shake(); //shake.cpp
const float kShakeMax = 5.0f;
float shake_scale(); //the game's screen shake times this, 0 is none
void set_shake_scale(float scale);
void install_typing(); //typing.cpp
void install_sound(); //sound.cpp
void install_occlusion(); //occlusion.cpp, after install_mods
bool occlusion(); //sounds behind a wall or a floor are quieter
void set_occlusion(bool on);
void install_height(); //height.cpp
bool sound_height(); //sounds above and below the listener are muffled
void set_sound_height(bool on);
bool collision_view(); //cheat, the level's collision is drawn in place of the world
void set_collision_view(bool on);
void collision_tick(); //every tick of the game's loop
void install_weapons(); //weapons.cpp
void install_prompts(); //prompts.cpp
void weapon_buttons(ClassicState& s); //a slot's key, in the classic's filter
bool sprint_key(); //input.cpp, the sprint key is down this frame
void install_overlay(); //overlay.cpp
bool overlay_open(); //the watches or the settings menu are up, so the mouse is free
bool settings_open(); //settings.cpp
void save_settings(); //the settings file is written
std::string saved_setting(const char* env, const char* name); //a setting as the game starts: the environment's, or what the menu saved under name in the launcher's file. empty for neither
void install_render(); //render.cpp: anisotropic filtering and anti-aliasing
int anisotropy(); //1 is off, up to 16
void set_anisotropy(int level);
int anti_aliasing(); //samples a pixel, 1 is off
void set_anti_aliasing(int samples);
uint32_t settings_buttons(); //classic buttons the menu presses itself (+ to pause as it opens)

} //namespace ge
