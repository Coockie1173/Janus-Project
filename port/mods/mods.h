//mods: loose level files, the Mods and Level Select menus, a level's picture, custom weapons
#pragma once
#include "guest.h"

namespace ge
{

//modmenu.cpp
bool modded_lobby(); //network games are the ones of Connect to WFC Modded
void install_mods_menu(); //after install_mods
void mods_menu_frame(PPCContext& c); //once a frame, on the game's thread
uint32_t mods_menu_text(uint32_t hash); //the characters of one of the Mods menu's texts, 0 for any other
std::string mod_shown_name(const std::string& file); //a mod level's name in the menus, from its file's
bool custom_start_row(uint32_t row); //the menu row is 007 Custom's "Start Mission"
bool level_loads(uint32_t hash); //not one of the disc's levels that end the game as they load
std::string map_shown_name(const std::string& file, int& mode); //a multiplayer level's map ("Airfield") and which gamemode it is for, -1 for none
const char* map_mode_name(int mode);
const int kMapModeCount = 8;

//mods.cpp
void install_mods();
float world_trace(const PPCContext& c, const float start[3], const float way[3]); //the first hit of the level's collision along way from start, 0 to 1 of it, negative for none. on the game's thread
//a level among the mods: its file's hash, name and path, and the gamemode folders it is in, a bit each (0: only in the mods folder itself)
struct ModLevel { uint32_t hash; std::string name; std::string path; uint32_t modes; };
const int kModeCount = 8; //the online gamemodes, each a folder in the mods folder
extern const char* const kModeFolders[kModeCount];
const std::vector<ModLevel>& mod_levels();
const std::vector<ModLevel>& mod_files(); //every mod
const std::vector<ModLevel>& disc_levels(); //every level the disc has (mt_ and mpt_ files), the ones no menu leads to as well. no path
const std::string& mods_folder(); //empty when there's none
std::string mod_side_file(const ModLevel& mod, const char* extension); //mt_yard.png for mt_yard.edb, next to it or in the mods folder

//pictures.cpp
void install_pictures(); //after install_mods
void pictures_shoot(); //F12: the picture on screen becomes the loaded mod level's .png
bool pictures_hiding(); //the hud and the gun are left out for a picture
void pictures_frame(); //once a frame on the game's thread
std::string pictures_draw(); //on the renderer's thread before the overlay, what to say when a picture was made
void pictures_file(uint32_t hash, uint32_t file); //a file (the game's object for it) was given its memory

//weapons/weapons.cpp
void install_custom_weapons(); //after install_mods and before install_mods_menu (its version has them): the weapons of mods/weapons
void custom_weapons_frame(PPCContext& c); //once a frame, on the game's thread
uint32_t custom_weapon_text(uint32_t hash); //the characters of a custom weapon's name or about, 0 for any other
bool custom_weapon(uint32_t id); //the inventory id is one of the custom weapons'
int custom_weapon_count(); //the custom weapons, by their number
uint32_t custom_weapon_id(int n);
const char* custom_weapon_name(int n);
const std::vector<uint8_t>& custom_weapons_sum(); //what their files say, for the network game's version
} //namespace ge
