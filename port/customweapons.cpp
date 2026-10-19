//custom weapons: mods/weapons/NAME.txt is a weapon of its own, made from one of the game's (its
//base) with a name, a key, an id and the stats the file changes. it is in the online loadout
//menu's lists next to the game's guns
//
//what a gun is, all of it by the game's own data:
//the weapons table (805D1A4C: room +0, count +8, rows +0xC, 0x254 a row sorted by key) is
//  spreadsheet 1400001C of o_guns.edb, eight sheets of 69 rows that ge_weapons_load (800CC9D0)
//  puts together into one row each (fn_800CD950 inserts one by its key, copying it). a row:
//  +0 the key (4B00xxxx, what a loadout and the menus keep), +4 its class (4B2000xx), +8 a byte
//  set on a silenced one, +0xD the clip, +0xE the clips, +0x44..+0x50 the player's damage near,
//  the near and far range, the far damage, +0xC4..+0xD0 the same once more, +0x14C..+0x158 a
//  guard's, +0x174 the inventory's id (0200xxxx), +0x178 the gun as a script of o_guns (its
//  parts: the body, the slide, the clip, each an entity of the file on a locator), +0x17C and
//  +0x180 the reload as scripts of cp_bondfp.edb, +0x188 and +0x18C the ammo's and the clip's
//  items, +0x190 the name's text, +0x194..+0x1E0 the shots' scripts of o_guns (flash, shells),
//  +0x1E4..+0x240 the sounds, +0x244 the gun's own sound bank, +0x248..+0x250 particles
//the definitions (80578AB0, 0x2C each by id, ending with a 0 id): +8 a parent, +0xC the
//  animations, +0x10 a second parent, +0x14 the animations when aimed in, +0x18 more of them,
//  +0x1C the gun's scripts in place of the parent's, +0x28 the attachments it may carry (a bit by
//  the attachment hash's low byte). an animation list is rows of five: what is asked for (an
//  anim mode of cp_bondfp, 09xxxxxx), then the weapon's own mode for each firing mode.
//  fn_80168C50 looks one up and falls back to the parents, so a weapon has only what differs
//  (the Torka's three rows, the rest are the P99's). the game looks a definition up in a map
//  sorted by id (805D4624) that is filled from the table on first use (the byte 806EBB38), with
//  fn_80169480 for each one, the last one 02000001 (unarmed's parent)
//the loadout menu: the pages are rows of kind 0x10 with a weapon's key as the hash, a primary's
//  page, a secondary's (the five pistols) and one with them all. a key is also looked for in
//  the list at 80585C80 (16 bytes: key, name, about, a picture's number; 0x41 of them): six a
//  kind of gun, pistols first, and a key that isn't there is taken out of the loadout again
//  (fn_80256A80). the kinds have free places (FFFFFFFF), which is where these go
//the network game gives a player the weapons of its loadout by their keys (fn_800C2740), so a
//console has to have the same custom weapons as the others: their files go into the version of
//a modded network game (modmenu.cpp)
//
//a weapon's file, `key = value` a line, # a comment:
//  name = Golden AK           what the menus and the hud call it
//  about = An AK plated in gold.
//  base = AK-47               the game's weapon it is made from, by name or by id (0200xxxx)
//  clip = 40                  rounds in a clip
//  clips = 6                  clips it is given
//  damage = 1.5               the base's damage times this, the player's and a guard's
//  byte+D = 40, float+10 = 12, word+178 = 04001DB3   any other place of the row
//
//GE_TRACE=1 logs the weapons made and the pages they go into
#include "ge.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kWeapons = 0x805D1A4C, kRowSize = 0x254;
const uint32_t kRowKey = 0, kRowSilenced = 8, kRowClip = 0xD, kRowClips = 0xE, kRowId = 0x174, kRowName = 0x190;
const uint32_t kRowDamage[] = {0x44, 0x50, 0xC4, 0xD0, 0x14C, 0x158};
const uint32_t kDefs = 0x80578AB0, kDefSize = 0x2C, kDefLast = 0x02000001;
const uint32_t kList = 0x80585C80, kListSize = 0x10, kListCount = 0x41, kListKind = 6, kListFree = 0xFFFFFFFF;
const uint32_t kListWeapons = 32; //the weapons' places, the attachments and gadgets after them
const uint32_t kPages = 0x805E0590, kPagesCount = 0x8, kPagesData = 0xC;
const uint32_t kPageSize = 0x10, kPageHash = 0, kPageRows = 0x4, kPageRowCount = 0x8;
const uint32_t kMenuRow = 20, kMenuRowHash = 4, kMenuRowKind = 0xC, kKindWeapon = 0x0010;
const uint32_t kAlloc = 0x803428C0; //(bytes, flags) -> a block of the game's heap
const uint32_t kRowInsert = 0x800CD950; //(table, row)
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
};

std::vector<Weapon> g_weapons;
std::vector<uint8_t> g_sum; //what the files say, for the network game's version
bool g_trace = false;
bool g_listed = false, g_failed = false;
struct OwnPage { uint32_t gameRows, gameCount, rows, count; };
std::map<uint32_t, OwnPage> g_pages; //a weapon page's hash, its rows with these in them
PPCFunc orig_loadout_give = nullptr;
const uint32_t kLoadoutPrimary = 8, kLoadoutSecondary = 0xC; //a loadout row's keys
bool g_traceGives = false;
PPCFunc orig_find = nullptr, orig_poses = nullptr, weapon_pose = nullptr;
const uint32_t kPosesDue = 0x806E9460, kWeaponPose = 0x800FE680; //(r3, character, id, aimed, firing mode)
const uint32_t kFiringModes = 3;
PPCFunc orig_load = nullptr, orig_def_add = nullptr, row_insert = nullptr, game_alloc = nullptr;
uint32_t g_defs = 0; //the definitions made, one block

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

bool ReadWeapon(const std::filesystem::path& path, Weapon& w)
{
	std::ifstream in(path);
	if (!in) return false;
	w.file = path.filename().string();
	std::string line;
	while (std::getline(in, line))
	{
		g_sum.insert(g_sum.end(), line.begin(), line.end());
		g_sum.push_back('\n');
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

void ReadWeapons()
{
	if (mods_folder().empty()) return;
	const std::filesystem::path folder = std::filesystem::path(mods_folder()) / "weapons";
	std::error_code error;
	if (!std::filesystem::is_directory(folder, error)) return;
	std::vector<std::filesystem::path> files;
	for (const auto& entry : std::filesystem::directory_iterator(folder, error))
		if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".txt") files.push_back(entry.path());
	std::sort(files.begin(), files.end()); //the same keys on every console with the same files
	for (const auto& path : files)
	{
		if (g_weapons.size() == kMostWeapons) break;
		Weapon w;
		const std::string name = path.filename().string();
		g_sum.insert(g_sum.end(), name.begin(), name.end());
		if (!ReadWeapon(path, w)) continue;
		const uint32_t n = (uint32_t)g_weapons.size();
		w.key = kFirstKey + n;
		w.id = kFirstId + n;
		w.text = kFirstText + n * 2;
		w.aboutText = w.text + 1;
		g_weapons.push_back(w);
	}
	if (!g_weapons.empty()) rt_log("goldeneye: %zu custom weapon(s) in %s", g_weapons.size(), folder.string().c_str());
}

uint32_t Alloc(const PPCContext& c, uint32_t bytes)
{
	PPCContext call = c;
	const uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	call.r[3] = bytes;
	call.r[4] = 0;
	game_alloc(call);
	return guest_ptr(call.r[3]) ? call.r[3] : 0;
}

uint32_t Chars(const PPCContext& c, const std::string& text)
{
	const uint32_t at = Alloc(c, uint32_t(text.size() + 1) * 2);
	if (!at) return 0;
	for (size_t i = 0; i <= text.size(); ++i) st16(at + uint32_t(i) * 2, i < text.size() ? (uint8_t)text[i] : 0);
	return at;
}

uint32_t RowOfKey(uint32_t key)
{
	const uint32_t count = ld32(kWeapons + 8), rows = ld32(kWeapons + 0xC);
	for (uint32_t i = 0; guest_ptr(rows) && i < count && i < 512; ++i)
		if (ld32(rows + i * kRowSize + kRowKey) == key) return rows + i * kRowSize;
	return 0;
}

uint32_t DefOf(uint32_t id)
{
	for (uint32_t def = kDefs; ld32(def); def += kDefSize)
		if (ld32(def) == id) return def;
	return 0;
}

//the base weapon: by id, or by its name in the game's texts. the row is the one the loadout
//list has (the AK-47 has three rows of its id), else the first unsilenced one
bool FindBase(const PPCContext& c, Weapon& w)
{
	const uint32_t count = ld32(kWeapons + 8), rows = ld32(kWeapons + 0xC);
	char* rest = nullptr;
	uint32_t id = (uint32_t)std::strtoul(w.base.c_str(), &rest, 16);
	if (!rest || *rest || id >> 24 != 2) id = 0;
	for (uint32_t i = 0; !id && i < count && i < 512; ++i)
	{
		const uint32_t row = rows + i * kRowSize;
		if (ld32(row + kRowId) >> 24 == 2 && Lower(game_text(c, ld32(row + kRowName))) == Lower(w.base)) id = ld32(row + kRowId);
	}
	if (!id || !DefOf(id)) return false;
	w.baseId = id;
	for (uint32_t i = 0; i < kListWeapons; ++i)
	{
		const uint32_t key = ld32(kList + i * kListSize), row = key == kListFree ? 0 : RowOfKey(key);
		if (row && ld32(row + kRowId) == id) { w.baseKey = key; w.kind = (int)(i / kListKind); }
	}
	for (uint32_t i = 0; !w.baseKey && i < count && i < 512; ++i)
	{
		const uint32_t row = rows + i * kRowSize;
		if (ld32(row + kRowId) == id && !ld8(row + kRowSilenced)) w.baseKey = ld32(row);
	}
	return w.baseKey != 0;
}

//the weapons table is filled: each weapon's row goes in, the base's with what the file changes
void Load(PPCContext& c)
{
	const bool fresh = ld32(kWeapons + 8) == 0;
	orig_load(c);
	if (!fresh || g_weapons.empty() || !ld32(kWeapons + 8)) return;
	const uint32_t stack = c.r[1], row = stack - 0x400, frame = row - 0x40; //the row on the stack, below what the call uses
	for (Weapon& w : g_weapons)
	{
		if (!w.baseKey && !FindBase(c, w))
		{
			rt_log("goldeneye: weapons: %s: no weapon \"%s\" to make it from", w.file.c_str(), w.base.c_str());
			continue;
		}
		const uint32_t base = RowOfKey(w.baseKey);
		if (!base) continue;
		for (uint32_t i = 0; i < kRowSize; i += 4) st32(row + i, ld32(base + i));
		st32(row + kRowKey, w.key);
		st32(row + kRowId, w.id);
		st32(row + kRowName, w.text);
		st8(row + kRowSilenced, 0);
		if (w.clip >= 0) st8(row + kRowClip, (uint8_t)std::min(w.clip, 255));
		if (w.clips >= 0) st8(row + kRowClips, (uint8_t)std::min(w.clips, 255));
		for (uint32_t at : kRowDamage) st32(row + at, as_u32(as_f32(ld32(row + at)) * w.damage));
		for (const Patch& p : w.patches)
		{
			if (p.size == 1) st8(row + p.at, (uint8_t)p.value);
			else st32(row + p.at, p.value);
		}
		PPCContext call = c;
		st32(frame, stack);
		call.r[1] = frame;
		call.r[3] = kWeapons;
		call.r[4] = row;
		row_insert(call);
		if (g_trace) rt_log("goldeneye: weapons: %s is %08X, id %08X, made from %08X (%s)", w.name.c_str(), w.key, w.id, w.baseKey, w.base.c_str());
	}
}

//the definitions' map: after the game's last one, one for each weapon, the base's with its id
void DefAdd(PPCContext& c)
{
	const uint32_t map = c.r[3], pair = c.r[4];
	const uint32_t id = guest_ptr(pair) ? ld32(pair) : 0;
	orig_def_add(c);
	if (id != kDefLast || g_weapons.empty()) return;
	if (!g_defs) g_defs = Alloc(c, kDefSize * (uint32_t)g_weapons.size());
	if (!g_defs) return;
	const uint32_t stack = c.r[1], mine = stack - 0x80, frame = mine - 0x40;
	for (size_t i = 0; i < g_weapons.size(); ++i)
	{
		const Weapon& w = g_weapons[i];
		const uint32_t base = DefOf(w.baseId), def = g_defs + uint32_t(i) * kDefSize;
		if (!base) continue;
		for (uint32_t at = 0; at < kDefSize; at += 4) st32(def + at, ld32(base + at));
		st32(def, w.id);
		PPCContext call = c;
		st32(frame, stack);
		call.r[1] = frame;
		call.r[3] = map;
		st32(mine, w.id);
		st32(mine + 4, def);
		call.r[4] = mine;
		orig_def_add(call);
	}
	if (g_trace) rt_log("goldeneye: weapons: %zu definition(s) made", g_weapons.size());
}

//a free place among the guns of the base's kind of the loadout list, the base's picture
void ListWeapons(const PPCContext& c)
{
	for (Weapon& w : g_weapons)
	{
		if (!w.baseKey && !FindBase(c, w)) continue;
		w.chars = Chars(c, w.name);
		w.aboutChars = Chars(c, w.about);
		uint32_t picture = 0;
		for (uint32_t i = 0; i < kListWeapons; ++i)
			if (ld32(kList + i * kListSize) == w.baseKey) picture = ld32(kList + i * kListSize + 0xC);
		int place = -1;
		for (uint32_t i = 0; i < kListWeapons && place < 0; ++i)
		{
			const bool ofKind = w.kind < 0 || (int)(i / kListKind) == w.kind;
			if (ofKind && ld32(kList + i * kListSize) == kListFree) place = (int)i;
		}
		for (uint32_t i = 0; i < kListWeapons && place < 0; ++i)
			if (ld32(kList + i * kListSize) == kListFree) place = (int)i;
		if (place < 0)
		{
			rt_log("goldeneye: weapons: the loadout menu has no room for %s", w.name.c_str());
			continue;
		}
		const uint32_t at = kList + (uint32_t)place * kListSize;
		st32(at, w.key);
		st32(at + 4, w.text);
		st32(at + 8, w.aboutText);
		st32(at + 0xC, picture);
		if (g_trace) rt_log("goldeneye: weapons: %s in the loadout list's place %d", w.name.c_str(), place);
	}
}

bool InList(uint32_t key)
{
	for (uint32_t i = 0; i < kListWeapons; ++i)
		if (ld32(kList + i * kListSize) == key) return true;
	return false;
}

bool Pistol(uint32_t key)
{
	for (uint32_t i = 0; i < kListKind; ++i)
		if (ld32(kList + i * kListSize) == key) return true;
	return false;
}

//a page of the loadout menu with these in it: the game's rows, the weapons after its last gun
void WeaponPage(const PPCContext& c, uint32_t page)
{
	const uint32_t hash = ld32(page + kPageHash), rows = ld32(page + kPageRows), count = ld32(page + kPageRowCount);
	auto own = g_pages.find(hash);
	if (!modded_lobby())
	{
		//a game over Nintendo WFC: the others don't have them
		if (own != g_pages.end() && rows == own->second.rows)
		{
			st32(page + kPageRows, own->second.gameRows);
			st32(page + kPageRowCount, own->second.gameCount);
		}
		return;
	}
	if (own != g_pages.end() && rows == own->second.rows) return;
	if (own != g_pages.end() && rows == own->second.gameRows)
	{
		st32(page + kPageRows, own->second.rows);
		st32(page + kPageRowCount, own->second.count);
		return;
	}
	if (!guest_ptr(rows) || count == 0 || count > 256) return;
	int last = -1;
	bool pistols = false, others = false;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t row = rows + i * kMenuRow, key = ld32(row + kMenuRowHash);
		if (ld32(row + kMenuRowKind) >> 16 != kKindWeapon || key >> 24 != 0x4B) continue;
		if (Pistol(key)) pistols = true;
		else others = true;
		last = (int)i;
	}
	if (last < 0) return;
	std::vector<const Weapon*> adds;
	for (const Weapon& w : g_weapons)
	{
		if (!w.chars || !InList(w.key)) continue;
		const bool pistol = Pistol(w.key);
		if ((pistol && pistols) || (!pistol && others)) adds.push_back(&w);
	}
	if (adds.empty()) return;
	const uint32_t fresh = Alloc(c, (count + (uint32_t)adds.size()) * kMenuRow);
	if (!fresh) return;
	const uint32_t item = rows + (uint32_t)last * kMenuRow;
	uint32_t out = fresh;
	for (uint32_t i = 0; i < count; ++i)
	{
		for (uint32_t at = 0; at < kMenuRow; at += 4) st32(out + at, ld32(rows + i * kMenuRow + at));
		out += kMenuRow;
		if (i != (uint32_t)last) continue;
		for (const Weapon* pW : adds)
		{
			for (uint32_t at = 0; at < kMenuRow; at += 4) st32(out + at, ld32(item + at));
			st32(out, pW->text);
			st32(out + kMenuRowHash, pW->key);
			out += kMenuRow;
		}
	}
	const uint32_t total = count + (uint32_t)adds.size();
	g_pages[hash] = { rows, count, fresh, total };
	st32(page + kPageRows, fresh);
	st32(page + kPageRowCount, total);
	if (g_trace)
	{
		rt_log("goldeneye: weapons: page %08X (%u rows at %08X) gets %zu weapon(s) after row %d", hash, count, rows, adds.size(), last);
		for (uint32_t i = 0; i < total; ++i)
		{
			const uint32_t row = fresh + i * kMenuRow;
			rt_log("goldeneye: weapons:   %2u %08X %08X %08X %08X %08X", i, ld32(row), ld32(row + 4), ld32(row + 8), ld32(row + 12), ld32(row + 16));
		}
	}
}

//the poses of every weapon by its idle animation, made once for the definitions of the table:
//the custom ones' as well, or the hands and the gun aren't drawn
void Poses(PPCContext& c)
{
	const bool due = ld8(kPosesDue) != 0;
	const uint32_t r3 = c.r[3], character = c.r[4];
	orig_poses(c);
	if (!due) return;
	for (const Weapon& w : g_weapons)
	{
		if (!DefOf(w.baseId) || !ld8(DefOf(w.baseId) + 0x24)) continue;
		for (uint32_t mode = 0; mode < kFiringModes; ++mode)
			for (uint32_t aimed = 0; aimed < 2; ++aimed)
			{
				PPCContext call = c;
				call.r[3] = r3;
				call.r[4] = character;
				call.r[5] = w.id;
				call.r[6] = aimed;
				call.r[7] = mode;
				weapon_pose(call);
			}
	}
}

//a player's loadout is given (from a copy on the stack). outside a modded game the others don't
//have these, a custom weapon is its base
void LoadoutGive(PPCContext& c)
{
	const uint32_t ctrl = c.r[3], row = c.r[4];
	if (guest_ptr(row) && !modded_lobby())
		for (uint32_t at : {kLoadoutPrimary, kLoadoutSecondary})
		{
			const uint32_t key = ld32(row + at);
			const auto w = std::find_if(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return w.key == key; });
			if (w == g_weapons.end()) continue;
			st32(row + at, w->baseKey);
			if (g_trace) rt_log("goldeneye: weapons: %s is %08X outside a modded game", w->name.c_str(), w->baseKey);
		}
	if (g_traceGives && guest_ptr(row)) rt_log("goldeneye: weapons: controller %08X%s gets loadout %08X: %08X and %08X", ctrl, ctrl == local_controller() ? " (local)" : "", ld32(row), ld32(row + 8), ld32(row + 0xC));
	orig_loadout_give(c);
}

void Find(PPCContext& c)
{
	const uint32_t hash = c.r[3];
	const bool ours = std::any_of(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return hash == w.id || hash == w.key; });
	const std::string chain = ours ? call_chain(c) : std::string();
	orig_find(c);
	if (ours) rt_log("goldeneye: weapons: %08X looked for -> %d %s", hash, (int)c.r[3], chain.c_str());
}

//a network game sends an inventory's items by their place in the list at 80574E0C (6 bits:
//fn_80129780 and fn_80129830, and the same search inside fn_801266A0, fn_80127A20,
//fn_80128C00, fn_80128D90 and fn_8012AD60), so another console's copy of a player holds what
//its place in the list is there. the game's 37 end with a 0 at 80574EA0; these follow it, places
//38 to 63. behind the list is the P99 definition's table of its +0x20 (80574EA8, 0xA8 bytes,
//nothing else points at it), which moves to the heap to make the room
const uint32_t kNetItemsEnd = 0x80574EA0, kNetMostItems = 63, kNetGameItems = 37;
const uint32_t kP99Table = 0x80574EA8, kP99TableSize = 0xA8, kP99TableAt = 0x80578B08 + 0x20;
bool g_netListed = false;

void NetItems(const PPCContext& c)
{
	if (ld32(kP99TableAt) == kP99Table)
	{
		const uint32_t moved = Alloc(c, kP99TableSize);
		if (!moved) return;
		for (uint32_t at = 0; at < kP99TableSize; at += 4) st32(moved + at, ld32(kP99Table + at));
		st32(kP99TableAt, moved);
		for (size_t i = 0; g_defs && i < g_weapons.size(); ++i)
			if (ld32(g_defs + uint32_t(i) * kDefSize + 0x20) == kP99Table) st32(g_defs + uint32_t(i) * kDefSize + 0x20, moved);
	}
	uint32_t place = kNetGameItems;
	for (const Weapon& w : g_weapons)
	{
		if (place == kNetMostItems)
		{
			rt_log("goldeneye: weapons: a network game has room for %u custom weapons, %s isn't sent", kNetMostItems - kNetGameItems, w.name.c_str());
			continue;
		}
		st32(kNetItemsEnd + (place - kNetGameItems) * 4, w.id);
		++place;
	}
	st32(kNetItemsEnd + (place - kNetGameItems) * 4, 0);
	g_netListed = true;
	if (g_trace) rt_log("goldeneye: weapons: network items %u to %u", kNetGameItems + 1, place);
}

} //namespace

uint32_t custom_weapon_text(uint32_t hash)
{
	for (const Weapon& w : g_weapons)
	{
		if (hash == w.text) return w.chars;
		if (hash == w.aboutText) return w.aboutChars;
	}
	return 0;
}

const std::vector<uint8_t>& custom_weapons_sum() { return g_sum; }
bool custom_weapon(uint32_t id) { return std::any_of(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return w.id == id; }); }

//once a frame on the game's thread: the list's places and the loadout pages
void custom_weapons_frame(PPCContext& c)
{
	if (g_weapons.empty() || g_failed) return;
	if (!g_netListed) NetItems(c);
	if (!ld32(kWeapons + 8)) return;
	if (!g_listed)
	{
		g_listed = true;
		ListWeapons(c);
	}
	const uint32_t count = ld32(kPages + kPagesCount), data = ld32(kPages + kPagesData);
	if (!guest_ptr(data) || count == 0 || count > kMostPages) return;
	for (uint32_t i = 0; i < count; ++i) WeaponPage(c, data + i * kPageSize);
}

void install_custom_weapons()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	ReadWeapons();
	if (g_weapons.empty()) return;
	game_alloc = ppc_lookup(kAlloc);
	row_insert = ppc_lookup(kRowInsert);
	orig_load = ppc_hook("ge_weapons_load", Load);
	orig_def_add = ppc_hook("ge_weapon_def_add", DefAdd);
	orig_poses = ppc_hook("ge_weapon_poses", Poses);
	weapon_pose = ppc_lookup(kWeaponPose);
	orig_loadout_give = ppc_hook("ge_loadout_give", LoadoutGive);
	g_traceGives = std::getenv("GE_TRACEWEAPONS") != nullptr;
	if (g_traceGives) orig_find = ppc_hook("ge_hash_find", Find);
	if (!game_alloc || !row_insert || !orig_load || !orig_def_add || !orig_poses || !weapon_pose || !orig_loadout_give) rt_die("goldeneye: custom weapons are not hooked in this build");
}

} //namespace ge
