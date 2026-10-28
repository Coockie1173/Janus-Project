//custom weapons: a modpack's weapons/NAME.txt is a weapon of its own, made from one of the game's (its
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
//a weapon's file and its keys: read.cpp
//
//GE_TRACE=1 logs the weapons made and the pages they go into
#include "mods/weapons/weapon.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ge
{

using namespace weapons;

namespace
{

const uint32_t kRowDamage[] = {0x44, 0x50, 0xC4, 0xD0, 0x14C, 0x158};
const uint32_t kRowPlayerShot = 0x44, kRowGuardShot = 0x14C, kShotSize = 0x10; //near damage, near and far range, far damage
const uint32_t kDefs = 0x80578AB0, kDefSize = 0x2C, kDefLast = 0x02000001;
const uint32_t kList = 0x80585C80, kListSize = 0x10, kListCount = 0x41, kListKind = 6, kListFree = 0xFFFFFFFF;
const uint32_t kListWeapons = 32; //the weapons' places, the attachments and gadgets after them
const uint32_t kPages = 0x805E0590, kPagesCount = 0x8, kPagesData = 0xC;
const uint32_t kPageSize = 0x10, kPageHash = 0, kPageRows = 0x4, kPageRowCount = 0x8;
const uint32_t kMenuRow = 20, kMenuRowHash = 4, kMenuRowKind = 0xC, kKindWeapon = 0x0010, kKindSlot = 0x00A3;
const uint32_t kLayoutScript = 8, kFiveSlots = 0x04002B3C, kSixSlots = 0x04002B3B, kFiveSlotCount = 5; //a page's first row: its layout's script
const uint32_t kAlloc = 0x803428C0; //(bytes, flags) -> a block of the game's heap
const uint32_t kRowInsert = 0x800CD950; //(table, row)
const uint32_t kProfiles = 0x805DE788, kProfileSize = 0x590; //a pad's profile, its loadouts in it


bool g_trace = false;
bool g_failed = false;
int g_listed = -2; //the pack whose weapons the loadout list has
std::vector<uint32_t> g_gameList; //the list as the game has it
struct OwnPage { uint32_t gameRows, gameCount, rows, count; };
std::map<std::pair<int, uint32_t>, OwnPage> g_pages; //a pack and a weapon page's hash, its rows with the pack's weapons in them
PPCFunc orig_loadout_give = nullptr, orig_script_set = nullptr;
const uint32_t kLoadoutPrimary = 8, kLoadoutSecondary = 0xC; //a loadout row's keys
bool g_traceGives = false;
PPCFunc orig_find = nullptr, orig_node_point = nullptr, orig_poses = nullptr, weapon_pose = nullptr;
const uint32_t kPosesDue = 0x806E9460, kWeaponPose = 0x800FE680; //(r3, character, id, aimed, firing mode)
const uint32_t kFiringModes = 3;
PPCFunc orig_load = nullptr, orig_def_add = nullptr, row_insert = nullptr, game_alloc = nullptr;
uint32_t g_defs = 0; //the definitions made, one block

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

//a gun of its own (eurochef's `ge weapon`): a file of the weapons folder with the gun's parts and
//copies of the base gun's scripts out of o_guns, under the base's hashes. as the file is loaded
//its scripts get hashes of the weapon's own (04F0 + its number in the top half, so their order
//stays), the row's model and the definition's +0x1C list name those, and ge_script_set is given
//the file for them in place of o_guns. a script the file doesn't have stays the base's
const uint32_t kOwnScripts = 0x04F0, kGameScripts = 0x0400;
const uint32_t kRowModel = 0x178, kDefScripts = 0x1C, kDefParent = 8, kScriptRowWords = 5;
const uint32_t kFilesManager = 0x805D36A4, kGetFile = 0x800E3740; //(manager, file, 0): the file, loaded there and then when it isn't
const uint32_t kFixUp = 0x802E9610; //(file's data, which lists: -1 all): each list's things get their place in memory, set up as what they are
const uint32_t kRowLoaded = 0xC, kRowFixed = 0x8000; //a list row's thing in memory, and its section's bit once it is set up
const uint32_t kFileData = 0x24, kScriptList = 0x68, kScriptRow = 0x10;
PPCFunc get_file = nullptr, fix_up = nullptr, orig_file_object = nullptr;

uint32_t OwnScript(size_t n, uint32_t script) { return (kOwnScripts + (uint32_t)n) << 16 | (script & 0xFFFF); }

uint32_t Remap(size_t n, uint32_t script)
{
	const Weapon& w = g_weapons[n];
	return w.modelFile && w.modelScripts.count(script) ? OwnScript(n, script) : script;
}

//the gun a guard holds, the row's +0x178: the base's, or the file's copy of it. a base without one
//(the Golden Gun) or a file without the copy gets the gun the hands hold, what the definitions
//give for the P99's (04001DAC), from the file: a guard draws its body's far level of detail,
//which eurochef makes the whole gun. without a file the Golden Gun's gets the one Golden Gun mode
//lays in the level (0400293D), as the randomizer gives the game's
const uint32_t kHandModel = 0x04001DAC, kGoldenGun = 0x02000827, kGoldenGunModel = 0x0400293D;

uint32_t HandModel(uint32_t id)
{
	for (uint32_t def = DefOf(id), steps = 0; def && steps < 8; def = DefOf(ld32(def + kDefParent)), ++steps)
		for (uint32_t row = ld32(def + kDefScripts); guest_ptr(row) && ld32(row); row += kScriptRowWords * 4)
		{
			if (ld32(row) != kHandModel) continue;
			for (uint32_t k = 1; k < kScriptRowWords; ++k)
				if (ld32(row + k * 4)) return ld32(row + k * 4);
		}
	return kHandModel;
}

uint32_t WorldModel(size_t n, uint32_t model)
{
	const Weapon& w = g_weapons[n];
	const bool none = model == 0 || model == 0xFFFFFFFF;
	if (w.modelFile && !none && w.modelScripts.count(model)) return OwnScript(n, model);
	if (w.modelFile && w.modelScripts.count(HandModel(w.baseId)))
	{
		const uint32_t made = OwnScript(n, HandModel(w.baseId));
		if (g_trace) rt_log("goldeneye: weapons: %s's guards hold its own gun %08X (the base's %08X isn't in %s)", w.name.c_str(), made, model, w.model.c_str());
		return made;
	}
	if (none && w.baseId == kGoldenGun) return kGoldenGunModel;
	return model;
}

//the scripts a gun file has, read from the file itself
void ReadModel(Weapon& w)
{
	const std::filesystem::path path = std::filesystem::path(modpack_folder(w.pack)) / "weapons" / w.model;
	std::ifstream in(path, std::ios::binary);
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	auto be32 = [&](size_t at) { return at + 4 <= d.size() ? (uint32_t)d[at] << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3] : 0u; };
	if (d.size() < 0xD8 || be32(0) != 0x47454F4D)
	{
		rt_log("goldeneye: weapons: %s: %s isn't a gun's .edb, the base's gun is drawn", w.file.c_str(), w.model.c_str());
		return;
	}
	const uint32_t count = be32(kScriptList) >> 16, list = kScriptList + 4 + be32(kScriptList + 4);
	for (uint32_t i = 0; i < count && i < 256; ++i) w.modelScripts.insert(be32(list + i * kScriptRow));
	std::error_code error;
	for (const ModLevel& file : modpack_files(w.pack))
		if (std::filesystem::equivalent(file.path, path, error)) w.modelFile = file.hash;
	if (!w.modelFile) rt_log("goldeneye: weapons: %s: %s isn't among the mods, the base's gun is drawn", w.file.c_str(), w.model.c_str());
	else if (g_trace) rt_log("goldeneye: weapons: %s is drawn with %s (%08X, %zu script(s))", w.name.c_str(), w.model.c_str(), w.modelFile, w.modelScripts.size());
}

//a gun file's parts (0x601 meshes as eurochef writes them) sized and moved: every position (0x10
//bytes each from the pointer at +0x5C, as many as +0xA8 says) and the copies of the bounds
//(+0x10 and +0x20 the box, +0x30 and +0x7C a sphere, +0x8C and +0x98 the box again)
const uint32_t kEntityList = 0x50, kEntityRow = 0x14, kRowAddress = 8;
const uint32_t kMeshPositions = 0x5C, kMeshCount = 0xA8, kPositionSize = 0x10;
const uint32_t kMeshPoints[] = {0x10, 0x20, 0x30, 0x7C, 0x8C, 0x98}, kMeshRadii[] = {0x3C, 0x88};
//and the mesh's points (what a guard holds the gun by and fires from, the base part's as eurochef
//copies them): block 5 of the blocks the mesh's +0x40 word lists (its low byte which there are,
//above it where their table is), a table row where its block is; the block's count at +5, from
//+6 where the 0x30 byte points are, a point's position at +0x14
const uint32_t kMeshBlocks = 0x40, kPointsBlock = 5, kPointSize = 0x30, kPointPosition = 0x14;

uint32_t MeshPoints(uint32_t mesh, uint32_t& count)
{
	const uint32_t word = ld32(mesh + kMeshBlocks), bits = word & 0xFF;
	count = 0;
	if (!(bits & 1 << kPointsBlock)) return 0;
	const uint32_t table = mesh + kMeshBlocks + (uint32_t)((int32_t)word >> 8);
	const uint32_t row = table + 4 * (uint32_t)__builtin_popcount(bits & ((1 << kPointsBlock) - 1));
	if (!guest_ptr(row)) return 0;
	const uint32_t block = row + (uint32_t)((int32_t)ld32(row) >> 8);
	if (!guest_ptr(block)) return 0;
	count = ld8(block + 5);
	return block + 6 + (uint32_t)(int32_t)(int16_t)ld16(block + 6);
}

void MoveModel(uint32_t data, const Weapon& w)
{
	if (w.scale == 1.0f && !w.offset[0] && !w.offset[1] && !w.offset[2]) return;
	auto move = [&](uint32_t at)
	{
		for (int k = 0; k < 3; ++k) st32(at + k * 4, as_u32(as_f32(ld32(at + k * 4)) * w.scale + w.offset[k]));
	};
	const uint32_t count = ld16(data + kEntityList), rows = data + kEntityList + 4 + ld32(data + kEntityList + 4);
	uint32_t moved = 0;
	for (uint32_t i = 0; i < count && i < 256; ++i)
	{
		//the entities are set up as the file comes in (their first word their class by then): a
		//gun file has nothing but meshes
		const uint32_t row = rows + i * kEntityRow, mesh = guest_ptr(ld32(row + kRowLoaded)) ? ld32(row + kRowLoaded) : data + ld32(row + kRowAddress);
		if (!guest_ptr(mesh) || ld32(mesh + kMeshCount) > 0xFFFF) continue;
		const uint32_t positions = mesh + kMeshPositions + ld32(mesh + kMeshPositions), vertices = ld32(mesh + kMeshCount);
		for (uint32_t v = 0; v < vertices && v < 0x10000; ++v) move(positions + v * kPositionSize);
		for (uint32_t at : kMeshPoints) move(mesh + at);
		for (uint32_t at : kMeshRadii) st32(mesh + at, as_u32(as_f32(ld32(mesh + at)) * w.scale));
		uint32_t points = 0;
		const uint32_t at = MeshPoints(mesh, points);
		for (uint32_t p = 0; guest_ptr(at) && p < points && p < 64; ++p, ++moved) move(at + p * kPointSize + kPointPosition);
	}
	if (g_trace) rt_log("goldeneye: weapons: %s's gun times %.3f, moved %.3f %.3f %.3f, %u point(s) with it", w.name.c_str(), w.scale, w.offset[2], w.offset[1], w.offset[0], moved);
}

//the gun's file with its scripts under the weapon's hashes, loaded there and then the first time
//it is asked for in a level (as fn_801461C0 gets o_guns). 0 when it can't be
uint32_t ModelFile(const PPCContext& c, size_t n)
{
	PPCContext call = c;
	const uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	call.r[3] = kFilesManager;
	call.r[4] = g_weapons[n].modelFile;
	call.r[5] = 0;
	get_file(call);
	const uint32_t file = call.r[3], data = guest_ptr(file) ? ld32(file + kFileData) : 0;
	if (!guest_ptr(data))
	{
		static uint32_t told = 0;
		if (told++ < 5) rt_log("goldeneye: weapons: %s's gun (%s) doesn't load", g_weapons[n].name.c_str(), g_weapons[n].model.c_str());
		return 0;
	}
	const uint32_t count = ld16(data + kScriptList), rows = data + kScriptList + 4 + ld32(data + kScriptList + 4);
	for (uint32_t i = 0; i < count && i < 256; ++i)
	{
		const uint32_t row = rows + i * kScriptRow, hash = ld32(row);
		if (hash >> 16 == kGameScripts) st32(row, OwnScript(n, hash));
	}
	//its things get their pointers as the game sets them up when a file has come in
	//(fn_802E9610, from fn_802EC4C0): the file loaded this way hasn't been through that
	if (count && !(ld16(rows + 4) & kRowFixed))
	{
		PPCContext fix = call;
		fix.r[3] = data;
		fix.r[4] = 0xFFFFFFFF;
		MoveModel(data, g_weapons[n]);
		fix_up(fix);
		if (g_trace) rt_log("goldeneye: weapons: %s's gun is set up (%s)", g_weapons[n].name.c_str(), guest_ptr(ld32(rows + kRowLoaded)) ? "its scripts are there" : "its scripts aren't there");
	}
	if (!guest_ptr(ld32(rows + kRowLoaded))) return 0;
	return file;
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
		const size_t n = size_t(&w - g_weapons.data());
		st32(row + kRowModel, WorldModel(n, ld32(row + kRowModel)));
		if (w.clip >= 0) st8(row + kRowClip, (uint8_t)std::min(w.clip, 255));
		if (w.clips >= 0) st8(row + kRowClips, (uint8_t)std::min(w.clips, 255));
		for (uint32_t at : kRowDamage) st32(row + at, as_u32(as_f32(ld32(row + at)) * w.damage));
		for (const Patch& p : w.patches)
		{
			if (p.size == 1) st8(row + p.at, (uint8_t)p.value);
			else st32(row + p.at, p.value);
		}
		//a guard's shots do the player's, unless the file says what they do
		const bool guardSet = std::any_of(w.patches.begin(), w.patches.end(), [](const Patch& p) { return p.at >= kRowGuardShot && p.at < kRowGuardShot + kShotSize; });
		if (!guardSet)
			for (uint32_t at = 0; at < kShotSize; at += 4) st32(row + kRowGuardShot + at, ld32(row + kRowPlayerShot + at));
		PPCContext call = c;
		st32(frame, stack);
		call.r[1] = frame;
		call.r[3] = kWeapons;
		call.r[4] = row;
		row_insert(call);
		if (g_trace) rt_log("goldeneye: weapons: %s is %08X, id %08X, made from %08X (%s)", w.name.c_str(), w.key, w.id, w.baseKey, w.base.c_str());
	}
}

//a gun of its own: what the base and its parents' +0x1C lists give for each generic script (the
//nearest one's row), with the file's copies in place of the base's
uint32_t ScriptList(const PPCContext& c, size_t n)
{
	std::vector<std::array<uint32_t, kScriptRowWords>> rows;
	for (uint32_t def = DefOf(g_weapons[n].baseId), steps = 0; def && steps < 8; def = DefOf(ld32(def + kDefParent)), ++steps)
	{
		const uint32_t list = ld32(def + kDefScripts);
		for (uint32_t row = list; guest_ptr(list) && ld32(row); row += kScriptRowWords * 4)
		{
			if (std::any_of(rows.begin(), rows.end(), [&](const auto& r) { return r[0] == ld32(row); })) continue;
			std::array<uint32_t, kScriptRowWords> r{};
			r[0] = ld32(row);
			for (uint32_t k = 1; k < kScriptRowWords; ++k) r[k] = ld32(row + k * 4) ? Remap(n, ld32(row + k * 4)) : 0;
			rows.push_back(r);
		}
	}
	const uint32_t list = Alloc(c, uint32_t(rows.size() + 1) * kScriptRowWords * 4);
	if (!list) return 0;
	for (size_t i = 0; i <= rows.size(); ++i)
		for (uint32_t k = 0; k < kScriptRowWords; ++k) st32(list + uint32_t(i * kScriptRowWords + k) * 4, i < rows.size() ? rows[i][k] : 0);
	return list;
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
		if (w.modelFile) st32(def + kDefScripts, ScriptList(c, i));
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

//a free place among the guns of the base's kind of the loadout list, the base's picture: the
//installed pack's weapons, in the game's list as it was
void ListWeapons(const PPCContext& c)
{
	if (g_gameList.empty())
		for (uint32_t at = 0; at < kListWeapons * kListSize; at += 4) g_gameList.push_back(ld32(kList + at));
	for (uint32_t i = 0; i < (uint32_t)g_gameList.size(); ++i) st32(kList + i * 4, g_gameList[i]);
	for (Weapon& w : g_weapons)
	{
		if (!Installed(w) || (!w.baseKey && !FindBase(c, w))) continue;
		if (!w.chars) w.chars = Chars(c, w.name);
		if (!w.aboutChars) w.aboutChars = Chars(c, w.about);
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
	const uint32_t hash = ld32(page + kPageHash);
	uint32_t rows = ld32(page + kPageRows), count = ld32(page + kPageRowCount);
	//a pack's rows in it go back to the game's: outside a game over WFC Modded (the others don't
	//have them), and for another pack's
	const int pack = modpack_active();
	for (const auto& [which, made] : g_pages)
	{
		if (which.second != hash || rows != made.rows || (which.first == pack && modded_lobby())) continue;
		st32(page + kPageRows, rows = made.gameRows);
		st32(page + kPageRowCount, count = made.gameCount);
	}
	if (!modded_lobby()) return;
	auto own = g_pages.find({ pack, hash });
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
		if (!Installed(w) || !w.chars || !InList(w.key)) continue;
		const bool pistol = Pistol(w.key);
		if ((pistol && pistols) || (!pistol && others)) adds.push_back(&w);
	}
	if (adds.empty()) return;
	//the secondary's page is laid out for its five pistols (script 04002B3C, five slots) and doesn't
	//scroll: past five it gets the other pages' layout (04002B3B, six slots, scrolls)
	uint32_t slots = 0, lastSlot = 0, items = (uint32_t)adds.size();
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t kind = ld32(rows + i * kMenuRow + kMenuRowKind) >> 16;
		if (kind == kKindSlot) { ++slots; lastSlot = i; }
		if (kind == kKindWeapon) ++items;
	}
	const bool relayout = ld32(rows + kLayoutScript) == kFiveSlots && slots == kFiveSlotCount && items > slots;
	const uint32_t fresh = Alloc(c, (count + (uint32_t)adds.size() + relayout) * kMenuRow);
	if (!fresh) return;
	const uint32_t item = rows + (uint32_t)last * kMenuRow;
	uint32_t out = fresh;
	for (uint32_t i = 0; i < count; ++i)
	{
		for (uint32_t at = 0; at < kMenuRow; at += 4) st32(out + at, ld32(rows + i * kMenuRow + at));
		if (relayout && i == 0) st32(out + kLayoutScript, kSixSlots);
		out += kMenuRow;
		if (relayout && i == lastSlot)
		{
			for (uint32_t at = 0; at < kMenuRow; at += 4) st32(out + at, ld32(rows + i * kMenuRow + at));
			st32(out + kMenuRowHash, ld32(rows + i * kMenuRow + kMenuRowHash) + 1); //the next slot's widget
			out += kMenuRow;
		}
		if (i != (uint32_t)last) continue;
		for (const Weapon* pW : adds)
		{
			for (uint32_t at = 0; at < kMenuRow; at += 4) st32(out + at, ld32(item + at));
			st32(out, pW->text);
			st32(out + kMenuRowHash, pW->key);
			out += kMenuRow;
		}
	}
	const uint32_t total = count + (uint32_t)adds.size() + relayout;
	g_pages[{ pack, hash }] = { rows, count, fresh, total };
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
//have these, a custom weapon is its base. so is one of a pack that isn't installed
void LoadoutGive(PPCContext& c)
{
	const uint32_t ctrl = c.r[3], row = c.r[4];
	if (guest_ptr(row))
		for (uint32_t at : {kLoadoutPrimary, kLoadoutSecondary})
		{
			const uint32_t key = ld32(row + at);
			const auto w = std::find_if(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return w.key == key; });
			if (w == g_weapons.end() || (modded_lobby() && Installed(*w))) continue;
			st32(row + at, w->baseKey);
			if (g_trace) rt_log("goldeneye: weapons: %s is %08X outside a modded game", w->name.c_str(), w->baseKey);
		}
	if (g_traceGives && guest_ptr(row)) rt_log("goldeneye: weapons: controller %08X%s gets loadout %08X: %08X and %08X", ctrl, ctrl == local_controller() ? " (local)" : "", ld32(row), ld32(row + 8), ld32(row + 0xC));
	orig_loadout_give(c);
}

//a file asked for by one of its objects (a guard's gun): a custom gun's script is in its own file
void FileObject(PPCContext& c)
{
	const uint32_t hash = c.r[5], n = (hash >> 16) - kOwnScripts;
	if (n < g_weapons.size() && g_weapons[n].modelFile)
	{
		c.r[3] = ModelFile(c, n);
		return;
	}
	orig_file_object(c);
}

//a script object is given its script: one of a custom gun's comes from its file
void ScriptSet(PPCContext& c)
{
	const uint32_t hash = c.r[5], n = (hash >> 16) - kOwnScripts;
	if (n < g_weapons.size() && g_weapons[n].modelFile)
		if (const uint32_t file = ModelFile(c, n)) c.r[4] = file;
	if (g_traceGives)
	{
		static std::set<uint64_t> seen;
		const uint32_t file = c.r[4], data = guest_ptr(file) ? ld32(file + kFileData) : 0, fileHash = guest_ptr(data) ? ld32(data + 4) : 0;
		if (seen.insert((uint64_t)fileHash << 32 | hash).second)
		{
			//the object keeps the script it took at +0x20
			const uint32_t object = c.r[3];
			const std::string chain = call_chain(c);
			orig_script_set(c);
			rt_log("goldeneye: weapons: script %08X of %08X %s %s", hash, fileHash, ld32(object + 0x20) == hash ? "taken" : "not taken", chain.c_str());
			return;
		}
	}
	orig_script_set(c);
}

void Find(PPCContext& c)
{
	const uint32_t hash = c.r[3];
	const bool ours = std::any_of(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return hash == w.id || hash == w.key; });
	const std::string chain = ours ? call_chain(c) : std::string();
	orig_find(c);
	if (ours) rt_log("goldeneye: weapons: %08X looked for -> %d %s", hash, (int)c.r[3], chain.c_str());
}

//which points are asked for (a muzzle, a sight...), each hash once with what asked
void NodePoint(PPCContext& c)
{
	static std::set<uint64_t> seen;
	const uint32_t node = c.r[3], hash = c.r[4], out = c.r[5], flags = c.r[6];
	const bool first = seen.insert((uint64_t)node << 32 | hash).second;
	const std::string chain = first ? call_chain(c) : std::string();
	orig_node_point(c);
	if (!first) return;
	rt_log("goldeneye: weapons: point %08X of node %08X (mesh entity %08X, flags %X) -> %u: %.3f %.3f %.3f %s", hash, node,
		ld32(ld32(node + 0x1C)), flags, c.r[3], ldf(out), ldf(out + 4), ldf(out + 8), chain.c_str());
}

//a network game sends an inventory's items by their place in the list at 80574E0C (6 bits:
//fn_80129780 and fn_80129830, and the same search inside fn_801266A0, fn_80127A20,
//fn_80128C00, fn_80128D90 and fn_8012AD60), so another console's copy of a player holds what
//its place in the list is there. the game's 37 end with a 0 at 80574EA0; these follow it, places
//38 to 63. behind the list is the P99 definition's table of its +0x20 (80574EA8, 0xA8 bytes,
//nothing else points at it), which moves to the heap to make the room
const uint32_t kNetItemsEnd = 0x80574EA0, kNetMostItems = 63, kNetGameItems = 37;
const uint32_t kP99Table = 0x80574EA8, kP99TableSize = 0xA8, kP99TableAt = 0x80578B08 + 0x20;
int g_netListed = -2; //the pack whose weapons follow the game's

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
		if (!Installed(w)) continue;
		if (place == kNetMostItems)
		{
			rt_log("goldeneye: weapons: a network game has room for %u custom weapons, %s isn't sent", kNetMostItems - kNetGameItems, w.name.c_str());
			continue;
		}
		st32(kNetItemsEnd + (place - kNetGameItems) * 4, w.id);
		++place;
	}
	st32(kNetItemsEnd + (place - kNetGameItems) * 4, 0);
	g_netListed = modpack_active();
	if (g_trace) rt_log("goldeneye: weapons: network items %u to %u", kNetGameItems + 1, place);
}

//the loadout menu takes a weapon (fn_80256A80): a secondary that isn't one of the game's five
//pistols becomes the Torka, on every pick (a gadget, an attachment, the primary), so a custom pistol
//is put back: the one taken there, or the one the loadout had. a custom gun that isn't a pistol
//still goes when the Multi Task does
const uint32_t kMenuLit = 0x2C8, kMenuLitIndex = 0x220, kMenuPad = 0x1A4, kMenuPage = 0x214, kItemLoadout = 8;
const uint32_t kSecondaryPages[] = {0x7D0000C1, 0x7D0000E8}, kSecondarySlot = 0x15C, kTorka = 0x4B00002D;
PPCFunc orig_loadout_pick = nullptr;

void LoadoutPick(PPCContext& c)
{
	const uint32_t item = c.r[3], menu = c.r[4];
	if (!guest_ptr(menu) || !guest_ptr(item))
	{
		orig_loadout_pick(c);
		return;
	}
	const uint32_t row = ld32(menu + kMenuLit + ld32(menu + kMenuLitIndex) * 4);
	const uint32_t key = guest_ptr(row) ? ld32(row + 4) : 0, page = ld32(menu + kMenuPage);
	const uint32_t slot = kProfiles + ld8(menu + kMenuPad) * kProfileSize + ld32(item + kItemLoadout) * 4 + kSecondarySlot;
	const bool secondaryPage = std::find(std::begin(kSecondaryPages), std::end(kSecondaryPages), page) != std::end(kSecondaryPages);
	const uint32_t wanted = secondaryPage ? key : ld32(slot);
	orig_loadout_pick(c);
	const bool custom = std::any_of(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return w.key == wanted; });
	if (!custom || ld32(slot) != kTorka || (!secondaryPage && !Pistol(wanted))) return;
	st32(slot, wanted);
	if (g_trace) rt_log("goldeneye: weapons: %08X kept as the secondary", wanted);
}

//a shot's sounds (fn_8012E3A0: controller, the shot's place): the weapon's own .wav in place of the
//base's. the game's still runs for the rest (the noise the guards hear), with its sounds left out
//by its own switch, the byte 806EBA7C. the controller's weapon is its row by the byte +0xC98
const uint32_t kNoShotSounds = 0x806EBA7C, kHeldRow = 0xC98;
PPCFunc orig_fire_sound = nullptr;

void FireSound(PPCContext& c)
{
	const uint32_t ctrl = c.r[3], place = c.r[4];
	const int8_t held = guest_ptr(ctrl) ? (int8_t)ld8(ctrl + kHeldRow) : -1;
	const uint32_t rows = ld32(kWeapons + 0xC), key = held >= 0 && guest_ptr(rows) ? ld32(rows + (uint32_t)held * kRowSize + kRowKey) : 0;
	const auto w = std::find_if(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return w.key == key && w.fireClip >= 0; });
	if (g_trace) rt_log("goldeneye: weapons: a shot of %08X by %08X%s", key, ctrl, ctrl == local_controller() ? " (local)" : "");
	if (w == g_weapons.end() || !guest_ptr(place))
	{
		orig_fire_sound(c);
		return;
	}
	const uint8_t was = ld8(kNoShotSounds);
	st8(kNoShotSounds, 1);
	orig_fire_sound(c);
	st8(kNoShotSounds, was);
	if (was) return;
	//the shot is at the gun, about where the eyes are: one trace to it and one to the body below
	const float at[3]{ ldf(place), ldf(place + 4), ldf(place + 8) };
	custom_sound_play(w->fireClip, at, w->fireVolume * occlusion_gain(c, at, -0.7f, 0.0f));
}

//a guard's shot doesn't come through there: fn_80057060 plays its row's (the guard's +0x754) three
//sounds +0x214..+0x21C one by one through fn_80283F60 (guard, sound, 0), the sound at the guard's
//entity (+4) +0xF8. the loop doesn't look at what it answers. a custom weapon's are left out and
//its .wav plays once, for the first of them
const uint32_t kGuardShotLoop = 0x800577A0, kGuardRow = 0x754, kGuardSounds = 0x214, kGuardSoundCount = 3;
const uint32_t kEntity = 4, kEntityPlace = 0xF8;
PPCFunc orig_character_sound = nullptr;

void CharacterSound(PPCContext& c)
{
	const uint32_t guard = c.r[3], sound = c.r[4];
	const uint32_t row = c.lr == kGuardShotLoop && guest_ptr(guard) ? ld32(guard + kGuardRow) : 0;
	const uint32_t key = guest_ptr(row) ? ld32(row + kRowKey) : 0;
	const auto w = std::find_if(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return w.key == key && w.fireClip >= 0; });
	if (w == g_weapons.end())
	{
		orig_character_sound(c);
		return;
	}
	c.r[3] = 0;
	uint32_t first = 0xFFFFFFFF;
	for (uint32_t k = 0; k < kGuardSoundCount && first == 0xFFFFFFFF; ++k) first = ld32(row + kGuardSounds + k * 4);
	const uint32_t entity = ld32(guard + kEntity);
	if (sound != first || !guest_ptr(entity)) return;
	const float at[3]{ ldf(entity + kEntityPlace), ldf(entity + kEntityPlace + 4), ldf(entity + kEntityPlace + 8) };
	custom_sound_play(w->fireClip, at, w->fireVolume * occlusion_gain(c, at, 0.4f, 1.1f));
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

const std::vector<uint8_t>& custom_weapons_sum()
{
	static const std::vector<uint8_t> none;
	const int pack = modpack_active();
	return pack >= 0 && pack < (int)g_sums.size() ? g_sums[pack] : none;
}

bool custom_weapon(uint32_t id) { return std::any_of(g_weapons.begin(), g_weapons.end(), [&](const Weapon& w) { return w.id == id; }); }

//the installed pack's, the nth of them
static const Weapon* InstalledWeapon(int n)
{
	for (const Weapon& w : g_weapons)
		if (Installed(w) && n-- == 0) return &w;
	return nullptr;
}

int custom_weapon_count() { return (int)std::count_if(g_weapons.begin(), g_weapons.end(), [](const Weapon& w) { return Installed(w); }); }
uint32_t custom_weapon_id(int n) { const Weapon* w = InstalledWeapon(n); return w ? w->id : 0; }
const char* custom_weapon_name(int n) { const Weapon* w = InstalledWeapon(n); return w ? w->name.c_str() : ""; }

//once a frame on the game's thread: the list's places and the loadout pages
void custom_weapons_frame(PPCContext& c)
{
	if (g_weapons.empty() || g_failed) return;
	if (g_netListed != modpack_active()) NetItems(c);
	if (!ld32(kWeapons + 8)) return;
	if (g_listed != modpack_active())
	{
		g_listed = modpack_active();
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
	if (g_traceGives) orig_node_point = ppc_hook("ge_node_point", NodePoint);
	orig_script_set = ppc_hook("ge_script_set", ScriptSet);
	orig_file_object = ppc_hook("ge_file_object", FileObject);
	orig_loadout_pick = ppc_hook("ge_loadout_pick", LoadoutPick);
	get_file = ppc_lookup(kGetFile);
	fix_up = ppc_lookup(kFixUp);
	for (Weapon& w : g_weapons)
	{
		if (!w.model.empty()) ReadModel(w);
		if (!w.fireSound.empty()) w.fireClip = custom_sound_load((std::filesystem::path(modpack_folder(w.pack)) / "weapons" / w.fireSound).string());
	}
	if (std::any_of(g_weapons.begin(), g_weapons.end(), [](const Weapon& w) { return w.fireClip >= 0; }))
	{
		orig_fire_sound = ppc_hook("ge_weapon_fire_sound", FireSound);
		orig_character_sound = ppc_hook("ge_character_sound", CharacterSound);
		if (!orig_fire_sound || !orig_character_sound) rt_log("goldeneye: ge_weapon_fire_sound or ge_character_sound is not hooked in this build, custom weapons fire with their base's sound");
	}
	if (!game_alloc || !row_insert || !orig_load || !orig_def_add || !orig_poses || !weapon_pose || !orig_loadout_give || !orig_script_set || !orig_file_object || !orig_loadout_pick || !get_file || !fix_up) rt_die("goldeneye: custom weapons are not hooked in this build");
}

} //namespace ge
