//Weapon Randomizer (F1 > For Fun): in the campaign every weapon the player is handed and every
//guard's gun is swapped for a random one, with a random attachment or none
//
//the player: ge_give_weapon (8014B460: controller, weapon id, ammo, rounds in the clip or -1,
//  the grenade launcher's ammo, flags, from a pickup) puts a weapon in the inventory. a level's
//  start gives through it (fn_800C0030: Dam's P99 with flags 8), so do the scripts. in the
//  campaign the player is the only one it gives to. a weapon the level gives is swapped for the
//  same random one each time, a second give fills it up to what it holds. a pickup is left
//  alone: what a guard drops is already random
//the guards: ge_npc_attach_gun (80048890, XItemHandler_ArmedNPC::AttachGun: guard, weapon row,
//  flags) gives a guard its gun as it is set up. an unarmed one stays unarmed
//the flags: the inventory's are the model's shifted up by 3 (fn_800CA680), the low three bits
//  the firing mode. the model's: 1 Silencer, 2 Reflex Sight, 4 ACOG Scope, 8 Laser Pointer,
//  0x10 Thermal Scope, 0x20 Grenade Launcher (the loadout's attachments by ge_attachment_flags
//  and their texts 43001235..4300123B). what a weapon may carry is what the multiplayer loadout
//  menu allows (fn_80256EB0 greys out, fn_80256A80 takes away the rest): fn_801691D0 (weapon id,
//  attachment hash) -> 1 when the weapon's entry has the hash's bit. a weapon without an entry
//  carries none (the Golden Gun, the MJR-409). fn_80169310, the guard's AttachGun's mask, leaves
//  everything on for those: a sight on one breaks the view. one attachment at most
//the weapons: the ones the game's own give-all (fn_800C02A0) hands out, by their row of the
//  weapons table (805D1A4C: the count at +8, the rows at +0xC, 0x254 each; the id at +0x174,
//  +0xD the clip, +0xE the clips, +0x178 the model a guard holds, none for the Golden Gun)
//
//No Attachment Limit (experimental): any of them together
//GE_WEAPON_RANDOMIZER=1 starts with it on, GE_NO_ATTACHMENT_LIMIT=1 that, GE_RANDOMIZER_SEED=n the same picks every run,
//GE_RANDOMIZER_WEAPON=id[:flags] the player always gets that weapon (the model flags in hex), GE_TRACE=1 logs every swap
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kWeapons = 0x805D1A4C, kRowSize = 0x254;
const uint32_t kRowId = 0x174, kRowClip = 0xD, kRowClips = 0xE, kRowModel = 0x178;
const uint32_t kCurrentLevel = 0x805D1804;
const uint32_t kAttachmentAllowed = 0x801691D0; //(weapon id, attachment hash) -> 1 when the loadout menu allows it
struct Attachment { uint32_t hash, flag; };
const Attachment kAttachments[] = {{0x7D070002, 1}, {0x7D070004, 2}, {0x7D070007, 4}, {0x7D070005, 8}, {0x7D070003, 0x10}, {0x7D070006, 0x20}};
const uint32_t kGiveAll[] = { //fn_800C02A0's weapons, in its order
	0x0200016E, 0x0200092E, 0x0200031D, 0x0200092F, 0x0200074E, 0x02000827, 0x02000930, 0x0200031E, 0x02000931,
	0x0200048A, 0x02000932, 0x02000933, 0x0200074F, 0x02000934, 0x02000935, 0x02000936, 0x02000319, 0x02000937,
	0x02000489, 0x0200031F, 0x02000938, 0x02000750, 0x02000939, 0x0200093A, 0x0200048B, 0x0200077C, 0x02000828};
const uint32_t kSilencer = 1, kReflex = 2, kAcog = 4, kLaser = 8, kThermal = 0x10, kLauncher = 0x20;
const uint32_t kInventoryShift = 3;
const uint32_t kItems = 0xC4C, kItemCount = 0xC48, kItemSize = 0x10; //the inventory: an item's id at +0
const int32_t kPlenty = 999999; //what the game gives for "as much as it holds"

const uint32_t kLoadedFiles = 0x806425F0, kLoadedFileCount = 0x806425EC, kFileSystem = 0x806EC2D0;
const uint32_t kFileInfo = 0x802DDC50; //GetFileInfo
const uint32_t kFilesManager = 0x805D36A4, kRequestSection = 0x800E3A90;
const uint32_t kWeaponFiles[] = {0x01020016, 0x0109000F}; //cp_bondfp.edb, o_guns.edb

struct Pick { uint32_t id, row, flags; };

std::atomic<bool> g_on{false}, g_unlimited{false};
bool g_trace = false;
PPCFunc file_info = nullptr, request_section = nullptr;
PPCFunc orig_select = nullptr;
PPCFunc orig_give = nullptr, orig_attach = nullptr, attachment_allowed = nullptr;
std::mt19937 g_rng{std::random_device{}()};
std::map<uint32_t, Pick> g_swapped; //the level's weapon -> what the player got for it
uint32_t g_swappedLevel = 0, g_swappedPlayer = 0;
uint32_t g_forced = 0, g_forcedFlags = 0; //GE_RANDOMIZER_WEAPON, the player's pick

bool Active() { return g_on && !multiplayer_game(); }

//the first row with the id, as ge_give_weapon finds it (fn_800CCE80)
uint32_t RowOf(uint32_t id)
{
	const uint32_t count{ ld32(kWeapons + 8) }, rows{ ld32(kWeapons + 0xC) };
	if (!guest_ptr(rows)) return 0;
	for (uint32_t i = 0; i < count && i < 512; ++i)
	{
		if (ld32(rows + i * kRowSize + kRowId) == id) return rows + i * kRowSize;
	}
	return 0;
}

bool IsWeapon(uint32_t id) { return std::find(std::begin(kGiveAll), std::end(kGiveAll), id) != std::end(kGiveAll); }

//the model flags of the attachments the loadout menu allows on the weapon
uint32_t Allowed(const PPCContext& c, uint32_t id)
{
	uint32_t allowed = 0;
	for (const Attachment& a : kAttachments)
	{
		PPCContext call = c;
		call.r[3] = id;
		call.r[4] = a.hash;
		attachment_allowed(call);
		if (call.r[3] & 0xFF) allowed |= a.flag;
	}
	return allowed;
}

//one random attachment of those the weapon can carry, or none. No Attachment Limit: any of them
//together
uint32_t RandomFlags(const PPCContext& c, uint32_t id)
{
	const uint32_t allowed{ Allowed(c, id) };
	if (g_unlimited) //each of them or not
	{
		uint32_t flags = 0;
		for (uint32_t one : {kSilencer, kReflex, kAcog, kLaser, kThermal, kLauncher})
		{
			if ((allowed & one) && std::uniform_int_distribution<int>(0, 1)(g_rng)) flags |= one;
		}
		return flags;
	}
	std::vector<uint32_t> choices{0};
	for (uint32_t one : {kSilencer, kReflex, kAcog, kLaser, kThermal, kLauncher})
	{
		if (allowed & one) choices.push_back(one);
	}
	return choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(g_rng)];
}

//a random weapon of the give-all's, not one of those taken; a guard's needs a model to hold
Pick RandomWeapon(const PPCContext& c, bool guard, const std::vector<uint32_t>& taken)
{
	std::vector<Pick> pool;
	for (uint32_t id : kGiveAll)
	{
		const uint32_t row{ RowOf(id) };
		if (!row || std::find(taken.begin(), taken.end(), id) != taken.end()) continue;
		const uint32_t model{ ld32(row + kRowModel) };
		if (guard && (model == 0 || model == 0xFFFFFFFF)) continue;
		pool.push_back({id, row, 0});
	}
	if (pool.empty()) return {0, 0, 0};
	if (!guard && g_forced && RowOf(g_forced)) return {g_forced, RowOf(g_forced), g_forcedFlags};
	Pick pick{ pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(g_rng)] };
	pick.flags = RandomFlags(c, pick.id);
	return pick;
}

std::string Describe(uint32_t flags)
{
	static const char* const kNames[] = {"Silencer", "Reflex Sight", "ACOG Scope", "Laser Pointer", "Thermal Scope", "Grenade Launcher"};
	std::string s;
	for (int i = 0; i < 6; ++i)
	{
		if (flags >> i & 1) s += std::string(s.empty() ? "" : ", ") + kNames[i];
	}
	return s.empty() ? "no attachments" : s;
}

//a file's data when it is loaded, 0 when not. found as GeoFile_Load (802EC760) finds a
//file, without making or loading one: in its table sorted by hash (806425F0, the count at
//806425EC, a hash and a file each) when there is one, else by GetFileInfo (802DDC50: the file
//system 806EC2D0, a hash -> the file's entry), the file at the entry's +0x28. the data at +0x24
uint32_t FileData(const PPCContext& c, uint32_t hash)
{
	uint32_t file = 0;
	if (const uint32_t table{ ld32(kLoadedFiles) }; guest_ptr(table))
	{
		const uint32_t count{ ld32(kLoadedFileCount) };
		for (uint32_t i = 0; i < count && i < 4096 && !file; ++i)
		{
			if (ld32(table + i * 8) == hash) file = ld32(table + i * 8 + 4);
		}
	}
	else if (guest_ptr(ld32(kFileSystem)))
	{
		PPCContext call = c;
		call.r[3] = ld32(kFileSystem);
		call.r[4] = hash;
		file_info(call);
		if (guest_ptr(call.r[3])) file = ld32(call.r[3] + 0x28);
	}
	return guest_ptr(file) && guest_ptr(ld32(file + 0x24)) ? ld32(file + 0x24) : 0;
}

//the sections of the weapons' files: a level loads the sections of what its own weapons do (only
//section 0 stays loaded), an animation of another one has nothing behind it and the first-person
//view breaks when it plays (the SLY 2020, the WA2000, the Sigmus, the Toros on Facility, as it's
//drawn or after a shot). a multiplayer game has them all. so do the levels here, while the
//randomizer is on: every section of cp_bondfp.edb (the first-person animations) and o_guns.edb
//(the guns) that isn't loaded is asked for as a weapon is given. fn_800E3A90 (the files manager
//805D36A4, file, section, flags 0: now) is the game's own request of a section (fn_800E27D0 the
//preloader's). a file's sections are listed at its data's +0x40 (a count, a relative pointer;
//0x10 each: the hash 0800000N, start, end, the section's file when it is loaded)
void LoadSections(const PPCContext& c)
{
	for (uint32_t hash : kWeaponFiles)
	{
		const uint32_t data{ FileData(c, hash) };
		if (!data) continue;
		const uint32_t rows{ data + 0x44 + ld32(data + 0x44) };
		const int count{ (int16_t)ld16(data + 0x40) };
		int asked = 0;
		for (int i = 1; i < count && i < 256; ++i)
		{
			if (ld32(rows + i * 16 + 12)) continue;
			PPCContext call = c;
			call.r[3] = kFilesManager;
			call.r[4] = hash;
			call.r[5] = ld32(rows + i * 16);
			call.r[6] = 0;
			request_section(call);
			++asked;
		}
		int loaded = 0;
		for (int i = 1; i < count && i < 256; ++i) loaded += ld32(rows + i * 16 + 12) != 0;
		if (asked) rt_log("goldeneye: randomizer: %08X: %d section(s) asked for, %d of %d loaded", hash, asked, loaded, count - 1);
	}
}

//GE_TRACE: what each weapon may carry, once
void TraceAllowed(const PPCContext& c)
{
	static bool said = false;
	if (!g_trace || said) return;
	said = true;
	for (uint32_t id : kGiveAll) rt_log("goldeneye: randomizer: %08X may carry %s", id, Describe(Allowed(c, id)).c_str());
}

bool Holds(uint32_t ctrl, uint32_t id)
{
	const uint32_t items{ ld32(ctrl + kItems) }, count{ ld32(ctrl + kItemCount) };
	for (uint32_t i = 0; guest_ptr(items) && i < count && i < 64; ++i)
	{
		if (ld32(items + i * kItemSize) == id) return true;
	}
	return false;
}

//(player, weapon id, inventory index or -1, ...): the level asks for its own weapon (the P99 in
//the hand as a level starts), the one it was swapped for is taken out. else the weapon isn't
//held, it's given again (another of the swapped one) and nothing is taken out
void Select(PPCContext& c)
{
	if (Active() && c.r[3] == g_swappedPlayer && ld32(kCurrentLevel) == g_swappedLevel)
	{
		if (const auto found = g_swapped.find(c.r[4]); found != g_swapped.end())
		{
			if (g_trace) rt_log("goldeneye: randomizer: %08X taken out for %08X", found->second.id, c.r[4]);
			c.r[4] = found->second.id;
			c.r[5] = 0xFFFFFFFF;
		}
	}
	orig_select(c);
}

//(controller, weapon id, ammo, rounds in the clip, the launcher's ammo, flags, from a pickup)
void Give(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] }, id{ c.r[4] };
	const bool pickup{ (c.r[9] & 0xFF) != 0 };
	if (Active()) LoadSections(c);
	if (!Active() || pickup || !IsWeapon(id))
	{
		orig_give(c);
		return;
	}
	const uint32_t level{ ld32(kCurrentLevel) };
	if (level != g_swappedLevel || ctrl != g_swappedPlayer)
	{
		g_swapped.clear();
		g_swappedLevel = level;
		g_swappedPlayer = ctrl;
	}
	auto found = g_swapped.find(id);
	if (found == g_swapped.end())
	{
		std::vector<uint32_t> taken;
		for (auto& [from, to] : g_swapped) taken.push_back(to.id);
		const Pick pick{ RandomWeapon(c, false, taken) };
		if (!pick.id)
		{
			orig_give(c);
			return;
		}
		found = g_swapped.emplace(id, pick).first;
	}
	TraceAllowed(c);
	const Pick& pick{ found->second };
	//one of each: the give adds an item even for a weapon that's held
	if (Holds(ctrl, pick.id))
	{
		if (g_trace) rt_log("goldeneye: randomizer: the player's %08X is %08X, already held", id, pick.id);
		return;
	}
	const int32_t ammo{ (int32_t)c.r[5] };
	//a give of a weapon the player has sets its ammo as it is, the game's 999999 too (a level's
	//script gives its weapon again that way when it's missing): no more than the new one holds
	if (ammo > 0) c.r[5] = (uint32_t)(ld8(pick.row + kRowClip) * ld8(pick.row + kRowClips));
	if (ammo > 0) c.r[6] = 0xFFFFFFFF;
	c.r[7] = pick.flags & kLauncher ? (uint32_t)kPlenty : 0;
	c.r[8] = pick.flags << kInventoryShift;
	if (g_trace) rt_log("goldeneye: randomizer: the player's %08X is %08X with %s (ammo %d)", id, pick.id, Describe(pick.flags).c_str(), (int)c.r[5]);
	c.r[4] = pick.id;
	orig_give(c);
}

//(guard, weapon row, flags, ?)
void Attach(PPCContext& c)
{
	const uint32_t row{ c.r[4] };
	const uint32_t id{ guest_ptr(row) ? ld32(row + kRowId) : 0 };
	if (Active() && IsWeapon(id))
	{
		LoadSections(c);
		const Pick pick{ RandomWeapon(c, true, {}) };
		if (pick.id)
		{
			if (g_trace) rt_log("goldeneye: randomizer: guard %08X's %08X is %08X with %s", c.r[3], id, pick.id, Describe(pick.flags).c_str());
			c.r[4] = pick.row;
			c.r[5] = pick.flags;
		}
	}
	orig_attach(c);
}

} //namespace

bool weapon_randomizer() { return g_on; }
void set_weapon_randomizer(bool on) { g_on = on; }
bool no_attachment_limit() { return g_unlimited; }
void set_no_attachment_limit(bool on) { g_unlimited = on; }

void install_randomizer()
{
	g_on = std::atoi(saved_setting("GE_WEAPON_RANDOMIZER", "LAUNCH_WEAPONRANDOMIZER").c_str()) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	g_unlimited = std::atoi(saved_setting("GE_NO_ATTACHMENT_LIMIT", "LAUNCH_NOATTACHMENTLIMIT").c_str()) != 0;
	if (const char* forced = std::getenv("GE_RANDOMIZER_WEAPON"))
	{
		char* rest = nullptr;
		g_forced = (uint32_t)std::strtoul(forced, &rest, 16);
		if (rest && *rest == ':') g_forcedFlags = (uint32_t)std::strtoul(rest + 1, nullptr, 16);
	}
	if (const char* seed = std::getenv("GE_RANDOMIZER_SEED")) g_rng.seed((uint32_t)std::strtoul(seed, nullptr, 0));
	if (g_on) rt_log("goldeneye: weapon randomizer on");
	orig_give = ppc_hook("ge_give_weapon", Give);
	orig_attach = ppc_hook("ge_npc_attach_gun", Attach);
	orig_select = ppc_hook("ge_select_weapon", Select);
	file_info = ppc_lookup(kFileInfo);
	request_section = ppc_lookup(kRequestSection);
	attachment_allowed = ppc_lookup(kAttachmentAllowed);
	if (!orig_give || !orig_attach || !attachment_allowed || !file_info || !request_section || !orig_select) rt_die("goldeneye: the weapon randomizer is not hooked in this build");
}

} //namespace ge
