//online: the hosts file, the server's live patch, client-owned players
#pragma once
#include "guest.h"

namespace ge
{

//net.cpp
void install_hosts();
void install_live_patch(); //the server's patch of the game's code is left out
void install_blend_time(); //the others online are drawn to each update over a little more than the updates are apart
void install_command_time(); //a player's step online keeps the part of a millisecond its command cuts off
void net_frame(); //once a frame: the range a player's pitch is sent in
void net_trace_controller(uint32_t ctrl, uint32_t player, bool own); //GE_NETTRACE: a player's place and angles after its update
float net_pitch_range(); //how far up and down it goes to the others, radians
void install_ping(); //the scoreboard of a modded network game has every player's ping next to the name, the host sends them
bool show_ping();
void set_show_ping(bool on);
void net_write_number(PPCContext& c, uint32_t stream, uint32_t value, uint32_t most); //a ranged number 0..most into a player's state or a command (its stream)
uint32_t net_read_number(PPCContext& c, uint32_t stream, uint32_t most, uint32_t otherwise); //and back, otherwise when it can't be read

//owned.cpp
void install_owned(); //client-owned players online
void owned_from_patch(int version); //net.cpp, the server's live patch has it (1, 2) or not (0)
void owned_command_fill(uint32_t input, uint32_t cmd); //after ge_command_fill (input/look.cpp)
void owned_command_write(PPCContext& c, uint32_t cmd, uint32_t stream); //behind the game's part of a command (bunnyhop.cpp)
void owned_command_read(PPCContext& c, uint32_t cmd, uint32_t stream);
void owned_input(PPCContext& c, uint32_t ctrl, uint32_t cmd); //before ge_input_copy (input/look.cpp)
//character.cpp
const uint32_t kCharacterSetting = 0x7D01F010; //the Character row's setting (a choice), the game's own stop at 7D0100xx
const uint32_t kTextMapsCharacter = 0x4300F0C0; //"Map's Default", the pick that leaves the map's characters
void install_character(); //online characters: a player's pick goes to the others in a modded game
void character_frame(); //once a frame: the picks are forgotten outside a network game
void character_skins(PPCContext& c); //after ge_player_skin: the picks' skins load with the level
bool character_choice_text(PPCContext& c); //ge_choice_text and ge_choice_step for the Character row, false for another setting
bool character_choice_step(PPCContext& c);
void character_write(PPCContext& c, uint32_t ctrl, uint32_t stream); //behind a player's state: the skin the host gave it
void character_read(PPCContext& c, uint32_t ctrl, uint32_t stream);
uint32_t online_character(); //the pick, 0 for the map's
void set_online_character(uint32_t hash);
} //namespace ge
