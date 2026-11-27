//a modpack's weapons/NAME.txt, a weapon a file (weapons.cpp has what a weapon is in the game)
//
//a weapon's file, `key = value` a line, # a comment:
//  name = Golden AK           what the menus and the hud call it
//  about = An AK plated in gold.
//  base = AK-47               the game's weapon it is made from, by name or by id (0200xxxx)
//  clip = 40                  rounds in a clip
//  clips = 6                  clips it is given
//  damage = 1.5               the base's damage times this. a guard's shots with it do the
//                             player's, unless the file sets them (float+14C..+158)
//  byte+D = 40, float+10 = 12, word+178 = 04001DB3   any other place of the row
//  model = golden_ak.edb      a gun of its own, a file next to it made by eurochef's `ge weapon`
//                             (the gun's parts from a glTF scene, the base gun's scripts)
//  model offset = 0.02 -0.01 0   the gun moved forward, up and right, in metres (aiming down the
//                             sight brings the base's rear sight to the eye: line the gun's up with it)
//  model scale = 0.9          the gun's size, times this
//  fire sound = mygun.wav     a shot sounds like this file (next to it) in place of the base's,
//                             at the gun's place: the player's, another player's, a guard's
//                             (customsound.cpp)
//  fire volume = 0.8          that sound times this
//  kind = smg                 where it is in the loadout menu: pistol (a secondary), smg, heavy,
//                             rifle or sniper. the base's kind when it isn't given
#include "mods/weapons/weapon.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ge::weapons
{

std::string Trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

std::string Lower(std::string s)
{
	for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
	return s;
}

bool ReadWeapon(const std::filesystem::path& path, Weapon& w, std::vector<uint8_t>& sum)
{
	std::ifstream in(path);
	if (!in) return false;
	w.file = path.filename().string();
	std::string line;
	while (std::getline(in, line))
	{
		sum.insert(sum.end(), line.begin(), line.end());
		sum.push_back('\n');
		if (const size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		const std::string key = Lower(Trim(line.substr(0, eq))), value = Trim(line.substr(eq + 1));
		if (key == "name") w.name = value;
		else if (key == "about") w.about = value;
		else if (key == "base") w.base = value;
		else if (key == "damage") w.damage = std::strtof(value.c_str(), nullptr);
		else if (key == "clip") w.clip = std::atoi(value.c_str());
		else if (key == "clips") w.clips = std::atoi(value.c_str());
		else if (key == "model") w.model = value;
		else if (key == "model offset")
		{
			//forward, up, right in metres: the game's z, y and x
			float forward = 0, up = 0, right = 0;
			std::sscanf(value.c_str(), "%f %f %f", &forward, &up, &right);
			w.offset[0] = right;
			w.offset[1] = up;
			w.offset[2] = forward;
		}
		else if (key == "model scale") w.scale = std::strtof(value.c_str(), nullptr);
		else if (key == "fire sound") w.fireSound = value;
		else if (key == "fire volume") w.fireVolume = std::strtof(value.c_str(), nullptr);
		else if (key == "kind")
		{
			//the loadout list's six places a kind, in its order
			const char* const kinds[] = { "pistol", "smg", "heavy", "rifle", "sniper" };
			const std::string kind = Lower(value);
			w.fileKind = -1;
			for (int k = 0; k < (int)std::size(kinds); ++k)
				if (kind == kinds[k] || kind == std::string(kinds[k]) + "s") w.fileKind = k;
			if (w.fileKind < 0) rt_log("goldeneye: weapons: %s: kind \"%s\" isn't pistol, smg, heavy, rifle or sniper", w.file.c_str(), value.c_str());
		}
		else if (key.rfind("byte+", 0) == 0 || key.rfind("word+", 0) == 0 || key.rfind("float+", 0) == 0)
		{
			Patch p{};
			p.at = (uint32_t)std::strtoul(key.c_str() + key.find('+') + 1, nullptr, 16);
			p.size = key[0] == 'b' ? 1 : key[0] == 'w' ? 4 : 0;
			p.value = p.size == 0 ? as_u32(std::strtof(value.c_str(), nullptr)) : (uint32_t)std::strtoul(value.c_str(), nullptr, p.size == 1 ? 10 : 16);
			if (p.at + (p.size == 1 ? 1 : 4) > kRowSize || p.at == kRowKey || p.at == kRowId) rt_log("goldeneye: weapons: %s: %s is no place to change", w.file.c_str(), key.c_str());
			else w.patches.push_back(p);
		}
		else rt_log("goldeneye: weapons: %s: don't know \"%s\"", w.file.c_str(), key.c_str());
	}
	if (w.name.empty() || w.base.empty())
	{
		rt_log("goldeneye: weapons: %s has no name or no base, left out", w.file.c_str());
		return false;
	}
	return true;
}

//the keys and ids go on in order through the packs: they stay on this console (a network game
//sends a weapon by its place among the installed pack's, weapons.cpp)
void ReadWeapons()
{
	g_sums.assign((size_t)modpack_count(), {});
	for (int pack = 0; pack < modpack_count(); ++pack)
	{
		const std::filesystem::path folder = std::filesystem::path(modpack_folder(pack)) / "weapons";
		std::error_code error;
		if (!std::filesystem::is_directory(folder, error)) continue;
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(folder, error))
			if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".txt") files.push_back(entry.path());
		std::sort(files.begin(), files.end()); //the same places on every console with the same files
		size_t had = g_weapons.size(), ofPack = 0;
		for (const auto& path : files)
		{
			if (ofPack == kMostWeapons || g_weapons.size() == kMostAll) break;
			Weapon w;
			const std::string name = path.filename().string();
			g_sums[pack].insert(g_sums[pack].end(), name.begin(), name.end());
			if (!ReadWeapon(path, w, g_sums[pack])) continue;
			const uint32_t n = (uint32_t)g_weapons.size();
			w.pack = pack;
			w.key = kFirstKey + n;
			w.id = kFirstId + n;
			w.text = kFirstText + n * 2;
			w.aboutText = w.text + 1;
			g_weapons.push_back(w);
			++ofPack;
		}
		if (g_weapons.size() > had) rt_log("goldeneye: %zu custom weapon(s) in %s", g_weapons.size() - had, folder.string().c_str());
	}
}

bool Installed(const Weapon& w) { return w.pack == modpack_active(); }

} //namespace ge::weapons
