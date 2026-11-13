//the settings menu's Cheats page and the gecko codes
#pragma once
#include "guest.h"

namespace ge
{

//gecko.cpp
void install_gecko();
void gecko_apply();
bool gecko_cheats(); //codes of the launcher's Cheats tab are running

//noclip.cpp
void install_noclip();
bool noclip(); //cheat, the player flies through walls and doesn't fall
void set_noclip(bool on);
void noclip_look(uint32_t controller); //before ge_look_apply, the game's own step is taken out
void noclip_apply(uint32_t controller); //after the local controller's update
bool player_state(uint32_t& state, float& fallen, uint32_t& bodyFlags); //the local player as of its last update, false before there is one

//cheats.cpp
void install_cheats();
bool god_mode(); //cheat, the campaign's player takes no hurt
void set_god_mode(bool on);
bool infinite_ammo(); //cheat, every kind of ammo stays at the most it holds
void set_infinite_ammo(bool on);
bool infinite_sprint(); //cheat, the sprint never runs out
void set_infinite_sprint(bool on);
const float kTurboMax = 5.0f;
float turbo(); //cheat, the campaign's player moves this many times as fast, 1 is the game's
void set_turbo(float scale);
bool grenades(); //cheat, the campaign's player has frag grenades
void set_grenades(bool on);
void turbo_speed(uint32_t controller, uint32_t out, bool asSpeed); //ge_move_speed's answer, times the turbo
void cheats_apply(PPCContext& c, uint32_t controller); //after the local controller's update

//stamina.cpp
void install_stamina();
bool stamina_meter(); //cheat, the sprint that's left as a bar
void set_stamina_meter(bool on);
void stamina_apply(uint32_t controller); //after the local controller's update
struct Stamina { float left, full, cancel, needs; bool sprinting; }; //seconds: left of full, where a cancelled sprint's refill jumps, what a new sprint needs
bool stamina(Stamina& s); //the local player's as of its last update, false with the meter off or nobody in play

//collision.cpp
void install_collision();
bool collision_view(); //cheat, the level's collision is drawn in place of the world
void set_collision_view(bool on);
void collision_tick(); //every tick of the game's loop

//triggers.cpp
void install_triggers();
bool trigger_view(); //cheat, the triggers that go off by being walked into are drawn over the world
void set_trigger_view(bool on);
bool player_clip(float* m); //collision.cpp: the clip matrix of the player's view as last drawn (16 floats, a row vector times it), false before one was
void triggers_update(PPCContext& c); //collision.cpp, the player's view is about to be drawn
std::vector<std::string> trigger_labels(); //the trigger the player is in or looks at and what it does, a line each (a tab first: a detail)
struct TriggerTag { float x, y; uint32_t rgb; std::string text; }; //a name over a trigger: where on the picture (-1 to 1, up), its colour (0xBBGGRR)
std::vector<TriggerTag> trigger_tags();
const char* trigger_type_name(uint32_t type); //what the type does, nullptr when not known
void trigger_asked(uint32_t trigger, bool inside); //a test of the port's own: the viewer draws it as any other
bool trigger_xray(); //what is behind the level's walls is drawn too, fainter
void set_trigger_xray(bool on);
bool trigger_loads(); //the triggers that load and unload files are drawn too, with what they load
void set_trigger_loads(bool on);
float trigger_opacity(); //how solid the shapes are, in percent
void set_trigger_opacity(float percent);
struct TriggerType { uint32_t type, count, shaped; }; //a type the game asked about in the level: how many, how many with a shape
std::vector<TriggerType> trigger_types(); //by type
uint32_t trigger_types_version(); //changes with the list
bool trigger_type_shown(uint32_t type);
void set_trigger_type_shown(uint32_t type, bool on);
void set_trigger_types_shown(bool on); //every type, and the ones not asked about yet
std::string trigger_types_saved(); //the types' choices as the settings file keeps them
} //namespace ge
