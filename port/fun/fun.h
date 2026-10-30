//the settings menu's For Fun page: bunnyhopping and the weapon randomizer
#pragma once
#include "guest.h"

namespace ge
{

//randomizer.cpp
void install_randomizer();
bool weapon_randomizer(); //for fun, the campaign's weapons and the guards' guns are random, with a random attachment
void set_weapon_randomizer(bool on);
bool no_attachment_limit(); //experimental, the randomizer's weapons may have more than one attachment
void set_no_attachment_limit(bool on);
int forced_weapon(); //for fun, everyone gets this weapon in the campaign: 0 none, else 1 + its place in the list
void set_forced_weapon(int weapon);
int forced_weapon_count(); //"Off" and the weapons
const char* forced_weapon_name(int weapon);
uint32_t forced_weapon_id(); //its id, 0 for none

//bunnyhop.cpp
void install_bunnyhop();
bool bunnyhop(); //for fun, the player hops by itself and strafing in the air adds speed
void set_bunnyhop(bool on);
void bunnyhop_look(PPCContext& c, uint32_t controller); //after ge_look_apply, the body's step is ours
void bunnyhop_apply(uint32_t controller); //after a controller's update: the local one's offline, every one's online
void bunnyhop_command(uint32_t command); //a command of this console's was filled: the jump key goes in
uint32_t bunnyhop_crouch(uint32_t controller); //before ge_action_update: half-life's crouch, what the buttons were
void bunnyhop_crouch_done(uint32_t controller, uint32_t kept); //after it
uint32_t bunnyhop_input(uint32_t controller, uint32_t command); //before ge_input_copy: the key is the controller's
void bunnyhop_input_done(uint32_t command, uint32_t kept); //after it, with what bunnyhop_input gave
void bunnyhop_write(PPCContext& c, uint32_t stream, bool rules); //the host's rules behind its own player's state
void bunnyhop_read(PPCContext& c, uint32_t stream);
bool bunnyhop_now(); //on for this game: the menu's, the host's online
bool bhop_locked(); //a joined modded network game: the host's settings are the ones played by
bool bhop_prestrafe_now();
bool bhop_custom_now(); //on for this game and a rule isn't its default: the level timer flashes grey
float bhop_value_now(int which);
void bunnyhop_buttons(const ClassicState& s); //the jump key, as the pad was read
bool bunnyhop_speed(float& speed); //the speedometer's, false with it off or nobody in play
enum { kBhopSpeed, kBhopHeight, kBhopGravity, kBhopAirCap, kBhopAirAccel, kBhopWish, kBhopMax, kBhopFall, kBhopDrop, kBhopGroundAccel, kBhopFriction, kBhopValueCount };
struct BhopValue { const char* label; const char* hint; const char* env; const char* launch; float value, lo, hi, by; int decimals; float initial = 0; };
BhopValue& bhop_value(int which); //its tunables, by the enum above
void bhop_defaults(); //every tunable back to its own
bool bhop_prestrafe(); //half-life's ground move on the floor, so turning while strafing gains speed there too
void set_bhop_prestrafe(bool on);
bool bhop_crouch(); //half-life's crouch: held, in the air the legs come up
bool bhop_crouch_now();
void set_bhop_crouch(bool on);
bool bhop_hover(); //a crouch or a fall's start in the air stops the fall, as in the game
bool bhop_hover_now();
void set_bhop_hover(bool on);

} //namespace ge
