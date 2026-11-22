//how the game plays: fov, cutscenes and movies, small tweaks, autosave, the tank, 007 Custom, the picture's effects
#pragma once
#include "guest.h"

namespace ge
{

//teleport.cpp
void install_teleport();
void teleport_apply(PPCContext& c, uint32_t controller); //after a controller's update, see bunnyhop_apply

//custom.cpp
void install_custom();
const int kCustomSliders = 5; //enemy health, enemy damage, enemy reaction speed, hit protection, enemy accuracy
const int kCustomRows = 7; //the page's items: the sliders, the headshots' switch, Start Mission
void custom_frame(uint32_t frame); //once a frame, on the game's thread
void custom_buttons(ClassicState& s); //in the classic's filter: a press on a slider isn't a select
const uint32_t kCustomSetting = 0x7D01F000; //the sliders as settings of the game's menu, a hash each from here. the game's own stop at 7D0100xx
const uint32_t kCustomHeadshots = kCustomSetting + kCustomSliders; //the switch for headshots that kill, a setting with values to pick from
const uint32_t kCustomTextOn = 0x4300F0E4, kCustomTextOff = 0x4300F0E5; //the texts of its two values, modmenu.cpp has them
bool custom_mode(); //the mission is played with them
void set_custom_mode(bool on);
std::string custom_label(int slider); //"Enemy Health: 250%"
const char* custom_about(int slider); //the line that says what it is
std::string custom_values(); //the sliders and the switch as the settings file keeps them
bool set_custom_values(const std::string& text, bool always); //and back, always: every mission played with them (GE_CUSTOM_ON)
bool custom_always();
const int kCustomShown = kCustomSliders + 1; //what a mission was played with: the sliders and the switch
std::string custom_shown_name(int i); //"Enemy Health"
std::string custom_shown_value(int i); //"250%", "On"
void custom_page_drawing(bool on); //mods.cpp: the Mission Complete page draws itself between the two

//fov.cpp
void install_fov();
void fov_apply();
const float kGameFov = 49.0f, kFovMin = 20.0f, kFovMax = 120.0f;
float fov(); //degrees
void set_fov(float degrees);
const float kGameAdsFov = 22.5f, kAdsFovMin = 10.0f; //the pistol's, aimed down the sight
float ads_fov(); //degrees for a weapon the game aims at 22.5, the others in proportion
void set_ads_fov(float degrees);
float aim_zoom(); //the aimed picture's size next to the pistol's sight, 1 or less: what a scope turns the mouse down by
float host_drawn_fov(); //the angle a console draws the local player's aim with, 0 when it isn't aimed
bool no_colour_filter(); //a level's colour filter (a mesh on the eye) isn't drawn
void set_no_colour_filter(bool on);

//cutscenes.cpp
void install_cutscenes();
void load_level(uint32_t hash); //the game's menu loads that level at its next update, from a menu or from play
//the cutscenes that can be skipped, each on its own: a mission a row (config/openings.txt), its
//intro, a cutscene in its middle and its ending (config/cutscenes.txt)
enum SkipPart { kSkipIntro, kSkipMiddle, kSkipEnding, kSkipParts };
int skip_missions();
const char* skip_mission_name(int mission);
bool skip_exists(int mission, int part); //the mission has that cutscene
bool skip_on(int mission, int part);
void set_skip_on(int mission, int part, bool on);
void set_skip_all(bool on);
std::string skip_list(); //the ones on, as GE_SKIP_LIST takes them
float level_time(); //seconds played in the level
void cutscenes_frame(); //once a frame, on the game's thread
bool skipping_cutscene(); //an opening is being skipped right now
float cutscene_fade(); //how black the picture is for it, 0 to 1

//qte.cpp
bool cutscene_input_waiting(); //a cutscene asked for a button in the last frames
void install_qte();
bool auto_qte(); //quick time events press their own buttons
void set_auto_qte(bool on);

//movies.cpp
void install_movies();
bool skip_movies(); //a movie that plays once is over before it starts
void set_skip_movies(bool on);

//tweaks.cpp
void install_tweaks();
void tweaks_apply();
bool hud(); //the hud is drawn
void set_hud(bool on);
bool level_timer(); //the hud shows the level's time
void set_level_timer(bool on);
//the mission timer's place on screen, for drawing beside it: true when the timer is on screen now,
//with its top-right corner in window pixels (win_w x win_h the window, letterboxing minded)
bool mission_timer_anchor(float win_w, float win_h, float& right_x, float& top_y);
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

//tank.cpp
void install_tank();
bool is_tank(uint32_t ctrl); //the controller is the Tank mission's
void tank_buttons(ClassicState& s); //the tank's own button layout, in the classic's filter
const int kTankActionCount = 3, kTankSourceCount = 7; //what the tank does, and the buttons of the actions on foot that can do it
const char* tank_action_label(int action);
const char* tank_action_hint(int action);
const char* tank_source_label(int source);
int tank_source(int action); //the button that does it, a number of the sources
void set_tank_source(int action, int source);

//screen.cpp
void install_screen();
bool screen_draw(PPCContext& c); //from the mesh draw's hook: true when the entry is left out
bool no_bloom(); //the bloom pass isn't drawn
void set_no_bloom(bool on);
int depth_of_field(); //0 off, 1 the game's, 2 at full resolution
void set_depth_of_field(int mode);
bool invert_flash(); //a flash grenade's white-out is the picture of the moment it went off, with white edges
void set_invert_flash(bool on);
float film_grain(); //the film grain's strength, 0 none to 1 the game's
void set_film_grain(float amount);
#ifdef GE_IMGUI
bool flash_showing(); //on the renderer's thread: the inverted flash is drawn this frame
void flash_draw(); //inside the overlay's frame, first
#endif

//autosave.cpp
void install_autosave();
void autosave_frame(PPCContext& c); //once a frame, on the game's thread
bool autosave(); //a checkpoint saves the game
void set_autosave(bool on);

} //namespace ge
