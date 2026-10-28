//custom weapons, what their two files share: read.cpp reads mods/weapons/NAME.txt, weapons.cpp
//puts them in the game
#pragma once
#include "ge.h"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ge::weapons
{

const uint32_t kWeapons = 0x805D1A4C, kRowSize = 0x254;
const uint32_t kRowKey = 0, kRowSilenced = 8, kRowClip = 0xD, kRowClips = 0xE, kRowId = 0x174, kRowName = 0x190;
const uint32_t kFirstKey = 0x4B000100, kFirstId = 0x0200F000, kFirstText = 0x4300F300; //the game's stop at 4B00005A, 02001EBC, 430017xx
const uint32_t kMostWeapons = 64, kMostPages = 4096;

struct Patch { uint32_t at, size; uint32_t value; }; //size 1, 4, or 0 for a float
struct Weapon
{
	std::string file, name, about, base;
	float damage{ 1.0f };
	int clip{ -1 }, clips{ -1 };
	std::vector<Patch> patches;
	uint32_t key{}, id{}, text{}, aboutText{};
	uint32_t baseId{}, baseKey{};
	int kind{ -1 }; //which six of the loadout list
	uint32_t chars{}, aboutChars{}; //the texts in the game's memory
	std::string model; //its gun's file in the weapons folder, empty: the base's
	uint32_t modelFile{}; //that file's hash, as the mods have it
	std::set<uint32_t> modelScripts; //the base's scripts the file has copies of
	float offset[3]{}, scale{ 1.0f }; //the gun moved (the game's x, y, z) and sized as it loads
	std::string fireSound;
	float fireVolume{ 1.0f };
	int fireClip{ -1 }; //customsound.cpp's
};

inline std::vector<Weapon> g_weapons;
inline std::vector<uint8_t> g_sum; //what the files say, for the network game's version

//read.cpp
std::string Lower(std::string s);
void ReadWeapons(); //the weapons folder's files, sorted, into g_weapons

} //namespace ge::weapons
