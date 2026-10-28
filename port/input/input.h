//keyboard and mouse: mouse look, the menus' cursor, the bound keys in the game's prompts, weapon slots
#pragma once
#include "guest.h"

namespace ge
{

//input.cpp
void install_mouse();
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
bool joined_game();
bool network_game(); //hosted or joined
bool multiplayer_game(); //a network game or a split screen level, not the campaign
bool sprint_key(); //the sprint key is down this frame

//prompts.cpp
std::string game_text(const PPCContext& c, uint32_t hash); //one of the game's texts, empty when it has none
void install_prompts();

//pointer.cpp
void install_pointer();
void pointer_frame(uint32_t frame); //once a frame
bool menu_cursor(float& x, float& y, bool& left); //the cursor in the menus' screen of 640 by 448, false when it isn't free

//weapons.cpp
void install_weapons();
void weapon_buttons(ClassicState& s); //a slot's key, in the classic's filter
} //namespace ge
