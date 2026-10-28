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
//the weapons: the ones the game's own give-all (fn_800C02A0) hands out and the custom ones of
//mods/weapons (mods/weapons/weapons.cpp, a guard holds the gun it makes for them), by their row of the
//  weapons table (805D1A4C: the count at +8, the rows at +0xC, 0x254 each; the id at +0x174,
//  +0xD the clip, +0xE the clips, +0x178 the model a guard holds, none for the Golden Gun)
//
//the sounds: a gun's own shots are in its bank (+0x244: sb_wpn_ak47...), which the game loads into
//one of two slots for the gun the player holds (fn_80227AD0 from the player's update fn_801312C0).
//all else, a guard's shots, the reload, the shell, the shot from afar, is only in the banks of
//the levels that have the gun (sb_lvl_01_dam...) and in sb_multiplayer_weapons (2D500019, every
//gun, what a network game loads in the level's place): a gun the level doesn't have is quiet.
//so that bank is loaded too, as a weapon is given or a guard gets one. a bank needs a slot, a
//piece of the sound memory (the memory maps: 8 slots each, the level's map has all 8 taken), so
//it gets one no map has, answered here: ge_soundbank_slot lets it load there, the memory comes
//from ge_soundbank_slot_memory, 4.6 MB of the sound heap (the Large Data Heap, from its top
//like the game's own slots) taken once and kept. the banks' sounds are looked up in every
//loaded bank (fn_803A0260), the level's first. a bank of another memory map (the menus)
//unloads it with the rest, the next level asks again
//
//No Attachment Limit (experimental): any of them together
//Forced Weapon: with the randomizer off, one of the weapons for the player and every guard, without
//attachments. the names are the rows' +0x190 texts
//the Golden Gun has no model a guard holds (+0x178 FFFFFFFF). it gets the one Golden Gun mode lays
//in the level, 0400293D of o_guns.edb, so guards can have it, forced or random. ge_gun_parts shows
//the parts of a gun model its list names (805730D0: the body, the attachments by the flags) and
//hides the others when a guard's AttachGun asks it to (its last argument, 1 for most guards): the
//world model names none of its seven, so they're all shown again after it
//a bullet gets its damage from the row by the distance (fn_800B2C60, into the bullet's +0x1D0): the
//player's from +0x44..+0x50 (the Golden Gun's 1000), a guard's from +0x14C..+0x158 (near damage, near
//and far range, far damage), a pistol's 13.125 for the Golden Gun. the guard's damages are made a kill
//GE_WEAPON_RANDOMIZER=1 starts with it on, GE_NO_ATTACHMENT_LIMIT=1 that, GE_RANDOMIZER_SEED=n the same picks every run,
//GE_RANDOMIZER_WEAPON=id[:flags] the forced weapon (the model flags in hex), GE_TRACE=1 logs every swap and a guard that didn't take its gun
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
const uint32_t kGuardGun = 0x754; //the row of the gun AttachGun gave (the silenced one's with flag 1), 0 when it failed
const uint32_t kGoldenGun = 0x02000827, kGoldenGunModel = 0x0400293D;
const uint32_t kRowGuardNear = 0x14C, kRowGuardFar = 0x158; //a guard's bullet's damage up close and far off
const float kGoldenGunKill = 100000.0f; //times the place's factor (0.9 at least) and the difficulty's (0.2 at least): more than health and armour
const uint32_t kAttachedModel = 0x80302A60; //(entity, model hash) -> the model attached to it
const uint32_t kModelParts = 0x114, kModelPartCount = 0x128, kPartSize = 0x20; //a part's node at +8
const uint32_t kNodeFlags = 0xC, kNodeShown = 1, kNodeFade = 0x8E;
const uint32_t kCurrentLevel = 0x805D1804;
const uint32_t kAttachmentAllowed = 0x801691D0; //(weapon id, attachment hash) -> 1 when the loadout menu allows it
struct Attachment { uint32_t hash, flag; };
const Attachment kAttachments[] = {{0x7D070002, 1}, {0x7D070004, 2}, {0x7D070007, 4}, {0x7D070005, 8}, {0x7D070003, 0x10}, {0x7D070006, 0x20}};
const uint32_t kGiveAll[] = { //fn_800C02A0's weapons, in its order
	0x0200016E, 0x0200092E, 0x0200031D, 0x0200092F, 0x0200074E, 0x02000827, 0x02000930, 0x0200031E, 0x02000931,
	0x0200048A, 0x02000932, 0x02000933, 0x0200074F, 0x02000934, 0x02000935, 0x02000936, 0x02000319, 0x02000937,
	0x02000489, 0x0200031F, 0x02000938, 0x02000750, 0x02000939, 0x0200093A, 0x0200048B, 0x0200077C, 0x02000828};
const char* const kNames[] = { //their texts, the row's +0x190
	"P99", "Torka T3", "Hawksman M5A", "Kunara V", "Wolfe .44", "Golden Gun", "Sigmus", "Sigmus 9", "Stauger UA-1",
	"Vargen FH-7", "Strata SV-400", "SLY 2020", "SEGS 550", "Drumhead Type-12", "PT-9 Interdictus", "Masterton M-557", "AK-47", "Kallos-TT9",
	"Anova DP3", "Terralite III", "Ivana Spec-R", "Pavlov ASR", "Toros AV-400", "Talon HL 450", "WA2000", "Gambit CP-208", "MJR-409"};
const int kWeaponCount = (int)std::size(kGiveAll);
static_assert(std::size(kNames) == std::size(kGiveAll));
const uint32_t kSilencer = 1, kReflex = 2, kAcog = 4, kLaser = 8, kThermal = 0x10, kLauncher = 0x20;
const uint32_t kInventoryShift = 3;
const uint32_t kItems = 0xC4C, kItemCount = 0xC48, kItemSize = 0x10; //the inventory: an item's id at +0
const int32_t kPlenty = 999999; //what the game gives for "as much as it holds"

const uint32_t kLoadedFiles = 0x806425F0, kLoadedFileCount = 0x806425EC, kFileSystem = 0x806EC2D0;
const uint32_t kFileInfo = 0x802DDC50; //GetFileInfo
const uint32_t kFilesManager = 0x805D36A4, kRequestSection = 0x800E3A90;
const uint32_t kWeaponFiles[] = {0x01020016, 0x0109000F}; //cp_bondfp.edb, o_guns.edb

const uint32_t kSoundManager = 0x806EC3F8, kSoundbanks = 0x806474BC; //the sound layer, its soundbanks
const uint32_t kLoadSoundbank = 0x80371770, kSoundAlloc = 0x80372F50; //(manager, bank, slot), (bytes, alignment, heap)
const uint32_t kWeaponsBank = 0x2D500019, kWeaponsSlot = 0x2D9000F0; //sb_multiplayer_weapons, a slot of no map
const uint32_t kWeaponsBankSize = 4861952; //its wav data, as big as the level bank's slot
const uint32_t kSoundHeap = 1, kSoundAlign = 0x40;
const uint32_t kBanks = 0x14, kBankNext = 0, kBankState = 8, kBankHash = 0x18, kBankSlot = 0x1C; //the requests and loaded banks
const uint32_t kBankUnloading = 5, kBankGone = 7;
const uint32_t kAskAgain = 300; //frames: a load is a command for the sound thread, it shows in the list later

struct Pick { uint32_t id, row, flags; };

std::atomic<bool> g_on{false}, g_unlimited{false};
bool g_trace = false;
PPCFunc file_info = nullptr, request_section = nullptr;
PPCFunc orig_select = nullptr;
PPCFunc orig_give = nullptr, orig_attach = nullptr, attachment_allowed = nullptr;
PPCFunc orig_gun_parts = nullptr, attached_model = nullptr;
PPCFunc orig_slot = nullptr, orig_slot_memory = nullptr, load_soundbank = nullptr, sound_alloc = nullptr;
uint32_t g_slotMemory = 0; //the weapons bank's slot, once taken
uint32_t g_bankAsked = 0; //the frame it was last asked for
bool g_noSlot = false;
std::mt19937 g_rng{std::random_device{}()};
std::map<uint32_t, Pick> g_swapped; //the level's weapon -> what the player got for it
uint32_t g_swappedLevel = 0, g_swappedPlayer = 0;
std::atomic<uint32_t> g_forced{0}; //Forced Weapon's id, 0 for none
uint32_t g_forcedFlags = 0; //GE_RANDOMIZER_WEAPON's flags

bool Active() { return (g_on || g_forced) && !multiplayer_game(); }

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

bool IsWeapon(uint32_t id) { return std::find(std::begin(kGiveAll), std::end(kGiveAll), id) != std::end(kGiveAll) || custom_weapon(id); }

//the give-all's weapons, then the custom ones of mods/weapons
std::vector<uint32_t> Weapons()
{
	std::vector<uint32_t> ids(std::begin(kGiveAll), std::end(kGiveAll));
	for (int i = 0; i < custom_weapon_count(); ++i) ids.push_back(custom_weapon_id(i));
	return ids;
}

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

//the model a guard holds; the Golden Gun's row gets its own first, and a guard's shot with it kills
bool HasModel(uint32_t row)
{
	if (ld32(row + kRowId) == kGoldenGun)
	{
		if (ld32(row + kRowModel) == 0xFFFFFFFF) st32(row + kRowModel, kGoldenGunModel);
		stf(row + kRowGuardNear, kGoldenGunKill);
		stf(row + kRowGuardFar, kGoldenGunKill);
	}
	const uint32_t model{ ld32(row + kRowModel) };
	return model != 0 && model != 0xFFFFFFFF;
}

//a random weapon of the give-all's, not one of those taken; a guard's needs a model to hold. the
//forced one with the randomizer off. none: the weapon is left as it is
Pick RandomWeapon(const PPCContext& c, bool guard, const std::vector<uint32_t>& taken)
{
	if (!g_on)
	{
		const uint32_t forced{ g_forced }, row{ forced ? RowOf(forced) : 0 };
		if (!row) return {0, 0, 0};
		if (guard && !HasModel(row)) return {0, 0, 0};
		return {forced, row, g_forcedFlags};
	}
	std::vector<Pick> pool;
	for (uint32_t id : Weapons())
	{
		const uint32_t row{ RowOf(id) };
		if (!row || std::find(taken.begin(), taken.end(), id) != taken.end()) continue;
		if (guard && !HasModel(row)) continue;
		pool.push_back({id, row, 0});
	}
	if (pool.empty()) return {0, 0, 0};
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

//the weapons bank's request in the manager's list, one that isn't going
uint32_t WeaponsBank()
{
	for (uint32_t q = ld32(kSoundbanks) ? ld32(ld32(kSoundbanks) + kBanks) : 0, n = 0; guest_ptr(q) && n < 64; q = ld32(q + kBankNext), ++n)
	{
		const uint32_t state{ ld32(q + kBankState) };
		if (ld32(q + kBankHash) == kWeaponsBank && ld32(q + kBankSlot) == kWeaponsSlot && (state < kBankUnloading || state > kBankGone)) return q;
	}
	return 0;
}

//sb_multiplayer_weapons into its own slot, when it isn't there or on its way
void LoadWeaponSounds(const PPCContext& c)
{
	if (g_noSlot || !guest_ptr(ld32(kSoundbanks)) || WeaponsBank()) return;
	const uint32_t frame{ ld32(kFrameCount) };
	if (g_bankAsked && frame - g_bankAsked < kAskAgain) return;
	if (!g_slotMemory)
	{
		PPCContext call = c;
		call.r[3] = kWeaponsBankSize;
		call.r[4] = kSoundAlign;
		call.r[5] = kSoundHeap;
		sound_alloc(call);
		if (!guest_ptr(call.r[3]))
		{
			g_noSlot = true;
			rt_log("goldeneye: randomizer: no %u bytes of sound memory for the weapons' sounds, guns the level doesn't have stay quiet", kWeaponsBankSize);
			return;
		}
		g_slotMemory = call.r[3];
	}
	PPCContext call = c;
	call.r[3] = kSoundManager;
	call.r[4] = kWeaponsBank;
	call.r[5] = kWeaponsSlot;
	load_soundbank(call);
	g_bankAsked = frame;
	rt_log("goldeneye: randomizer: the weapons' sounds (%08X) asked for, into %08X", kWeaponsBank, g_slotMemory);
}

//(soundbank manager, a bank's request) -> 1 when it may load into its slot. the weapons
//bank's slot is no map's and only that bank's: 0 when it's there already
void Slot(PPCContext& c)
{
	const uint32_t manager{ c.r[3] }, request{ c.r[4] };
	if (ld32(request + kBankSlot) != kWeaponsSlot)
	{
		orig_slot(c);
		return;
	}
	c.r[3] = 1;
	for (uint32_t q = ld32(manager + kBanks), n = 0; guest_ptr(q) && n < 64; q = ld32(q + kBankNext), ++n)
	{
		const uint32_t state{ ld32(q + kBankState) };
		if (q != request && ld32(q + kBankSlot) == kWeaponsSlot && (state < kBankUnloading || state > kBankGone)) c.r[3] = 0;
	}
}

//(soundbank manager, a slot, &address, &size) -> 1 when it has memory
void SlotMemory(PPCContext& c)
{
	if (c.r[4] != kWeaponsSlot)
	{
		orig_slot_memory(c);
		return;
	}
	st32(c.r[5], g_slotMemory);
	st32(c.r[6], g_slotMemory ? kWeaponsBankSize : 0);
	c.r[3] = g_slotMemory ? 1 : 0;
}

//GE_TRACE: what each weapon may carry, once
void TraceAllowed(const PPCContext& c)
{
	static bool said = false;
	if (!g_trace || said) return;
	said = true;
	for (uint32_t id : Weapons()) rt_log("goldeneye: randomizer: %08X may carry %s", id, Describe(Allowed(c, id)).c_str());
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
	static const bool trace = std::getenv("GE_TRACEWEAPONS") != nullptr;
	if (trace) rt_log("goldeneye: weapons: %08X given %08X %s", ctrl, id, call_chain(c).c_str());
	if (Active())
	{
		LoadSections(c);
		LoadWeaponSounds(c);
	}
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

//(weapon row, entity, model flags, hide the unnamed parts)
void GunParts(PPCContext& c)
{
	const uint32_t row{ c.r[3] }, entity{ c.r[4] };
	orig_gun_parts(c);
	const uint32_t gun{ guest_ptr(row) ? ld32(row + kRowModel) : 0 };
	if (gun != kGoldenGunModel) return;
	PPCContext call = c;
	call.r[3] = entity;
	call.r[4] = gun;
	attached_model(call);
	const uint32_t model{ call.r[3] };
	if (!guest_ptr(model)) return;
	for (uint32_t i = 0; i < ld8(model + kModelPartCount); ++i)
	{
		const uint32_t node{ ld32(ld32(model + kModelParts) + i * kPartSize + 8) };
		if (!guest_ptr(node)) continue;
		st16(node + kNodeFlags, ld16(node + kNodeFlags) | kNodeShown);
		st8(node + kNodeFade, 0);
	}
	if (g_trace) rt_log("goldeneye: randomizer: %08X's %u parts shown on %08X", gun, ld8(model + kModelPartCount), entity);
}

//(guard, weapon row, flags, hide the gun model's unnamed parts)
void Attach(PPCContext& c)
{
	const uint32_t row{ c.r[4] };
	const uint32_t id{ guest_ptr(row) ? ld32(row + kRowId) : 0 };
	if (Active() && IsWeapon(id))
	{
		LoadSections(c);
		LoadWeaponSounds(c);
		const Pick pick{ RandomWeapon(c, true, {}) };
		if (pick.id)
		{
			if (g_trace) rt_log("goldeneye: randomizer: guard %08X's %08X is %08X with %s", c.r[3], id, pick.id, Describe(pick.flags).c_str());
			c.r[4] = pick.row;
			c.r[5] = pick.flags;
		}
	}
	const uint32_t guard{ c.r[3] }, given{ c.r[4] };
	orig_attach(c);
	if (g_trace && given != row && !ld32(guard + kGuardGun)) rt_log("goldeneye: randomizer: guard %08X didn't take its gun", guard);
}

} //namespace

bool weapon_randomizer() { return g_on; }
void set_weapon_randomizer(bool on) { g_on = on; }
bool no_attachment_limit() { return g_unlimited; }
void set_no_attachment_limit(bool on) { g_unlimited = on; }
//the give-all's, then the custom ones
int forced_weapon()
{
	const std::vector<uint32_t> ids{ Weapons() };
	const auto found{ std::find(ids.begin(), ids.end(), g_forced.load()) };
	return found == ids.end() ? 0 : (int)(found - ids.begin()) + 1;
}
void set_forced_weapon(int weapon)
{
	const std::vector<uint32_t> ids{ Weapons() };
	g_forced = weapon > 0 && weapon <= (int)ids.size() ? ids[weapon - 1] : 0;
}
int forced_weapon_count() { return kWeaponCount + custom_weapon_count() + 1; }
const char* forced_weapon_name(int weapon)
{
	if (weapon > kWeaponCount) return custom_weapon_name(weapon - kWeaponCount - 1);
	return weapon > 0 ? kNames[weapon - 1] : "Off";
}
uint32_t forced_weapon_id() { return g_forced; }

void install_randomizer()
{
	g_on = std::atoi(saved_setting("GE_WEAPON_RANDOMIZER", "LAUNCH_WEAPONRANDOMIZER").c_str()) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	g_unlimited = std::atoi(saved_setting("GE_NO_ATTACHMENT_LIMIT", "LAUNCH_NOATTACHMENTLIMIT").c_str()) != 0;
	{
		const std::string forced{ saved_setting("GE_RANDOMIZER_WEAPON", "LAUNCH_FORCEDWEAPON") };
		char* rest = nullptr;
		const uint32_t id{ (uint32_t)std::strtoul(forced.c_str(), &rest, 16) };
		if (rest && *rest == ':') g_forcedFlags = (uint32_t)std::strtoul(rest + 1, nullptr, 16);
		if (id && IsWeapon(id)) g_forced = id;
		else if (id) rt_log("goldeneye: randomizer: %08X is not one of the weapons, nothing is forced", id);
	}
	if (const char* seed = std::getenv("GE_RANDOMIZER_SEED")) g_rng.seed((uint32_t)std::strtoul(seed, nullptr, 0));
	if (g_on) rt_log("goldeneye: weapon randomizer on");
	if (g_forced) rt_log("goldeneye: forced weapon %08X (%s)", g_forced.load(), forced_weapon_name(forced_weapon()));
	orig_give = ppc_hook("ge_give_weapon", Give);
	orig_attach = ppc_hook("ge_npc_attach_gun", Attach);
	orig_select = ppc_hook("ge_select_weapon", Select);
	orig_gun_parts = ppc_hook("ge_gun_parts", GunParts);
	attached_model = ppc_lookup(kAttachedModel);
	file_info = ppc_lookup(kFileInfo);
	request_section = ppc_lookup(kRequestSection);
	attachment_allowed = ppc_lookup(kAttachmentAllowed);
	orig_slot = ppc_hook("ge_soundbank_slot", Slot);
	orig_slot_memory = ppc_hook("ge_soundbank_slot_memory", SlotMemory);
	load_soundbank = ppc_lookup(kLoadSoundbank);
	sound_alloc = ppc_lookup(kSoundAlloc);
	if (!orig_give || !orig_attach || !attachment_allowed || !file_info || !request_section || !orig_select || !orig_gun_parts || !attached_model || !orig_slot || !orig_slot_memory || !load_soundbank || !sound_alloc) rt_die("goldeneye: the weapon randomizer is not hooked in this build");
}

} //namespace ge
