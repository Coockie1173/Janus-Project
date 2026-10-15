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
//GE_WEAPON_RANDOMIZER=1 starts with it on, GE_RANDOMIZER_SEED=n the same picks every run,
//GE_RANDOMIZER_WEAPON=id[:flags] the player always gets that weapon (the model flags in hex), GE_TRACE=1 logs every swap
#include "ge.h"
#include <algorithm>
#include <iterator>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <set>
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
const int32_t kPlenty = 999999; //what the game gives for "as much as it holds"

const uint32_t kLoadedFiles = 0x806425F0, kLoadedFileCount = 0x806425EC, kFileSystem = 0x806EC2D0;
const uint32_t kFileInfo = 0x802DDC50, kFpFile = 0x01020016; //GetFileInfo, cp_bondfp.edb
const uint32_t kDefs = 0x80578AB0, kDefSize = 0x2C; //the weapons' definitions, by id
const uint32_t kFind = 0x802E2A00; //(hash or index, a list of a file, its rows' size) -> the row, -1 when it isn't there
const uint32_t kNoParent = 0x02000001;
const uint32_t kStartCommands[] = {0x0C000002, 0x0C000003}; //a mode's commands to start an animation set, its index after them

struct Pick { uint32_t id, row, flags; };

std::atomic<bool> g_on{false};
bool g_trace = false;
PPCFunc find = nullptr, file_info = nullptr;
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

const std::vector<uint32_t>& Loaded(const PPCContext& c);
std::string Hex(uint32_t v)
{
	char b[12];
	std::snprintf(b, sizeof b, "%08X", v);
	return b;
}

//one random attachment of those the weapon can carry, or none
uint32_t RandomFlags(const PPCContext& c, uint32_t id)
{
	const uint32_t allowed{ Allowed(c, id) };
	std::vector<uint32_t> choices{0};
	for (uint32_t one : {kSilencer, kReflex, kAcog, kLaser, kThermal, kLauncher})
	{
		if (allowed & one) choices.push_back(one);
	}
	return choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(g_rng)];
}

//a random weapon of the give-all's whose animations the level has (a guard's is dropped for the
//player to pick up), not one of those taken; a guard's needs a model to hold
Pick RandomWeapon(const PPCContext& c, bool guard, const std::vector<uint32_t>& taken)
{
	std::vector<Pick> pool;
	for (uint32_t id : Loaded(c))
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

//cp_bondfp.edb's data when it is loaded, 0 when not. found as GeoFile_Load (802EC760) finds a
//file, without making or loading one: in its table sorted by hash (806425F0, the count at
//806425EC, a hash and a file each) when there is one, else by GetFileInfo (802DDC50: the file
//system 806EC2D0, a hash -> the file's entry), the file at the entry's +0x28. the data at +0x24
uint32_t FpFile(const PPCContext& c)
{
	uint32_t file = 0;
	if (const uint32_t table{ ld32(kLoadedFiles) }; guest_ptr(table))
	{
		const uint32_t count{ ld32(kLoadedFileCount) };
		for (uint32_t i = 0; i < count && i < 4096 && !file; ++i)
		{
			if (ld32(table + i * 8) == kFpFile) file = ld32(table + i * 8 + 4);
		}
	}
	else if (guest_ptr(ld32(kFileSystem)))
	{
		PPCContext call = c;
		call.r[3] = ld32(kFileSystem);
		call.r[4] = kFpFile;
		file_info(call);
		if (guest_ptr(call.r[3])) file = ld32(call.r[3] + 0x28);
	}
	return guest_ptr(file) && guest_ptr(ld32(file + 0x24)) ? ld32(file + 0x24) : 0;
}

uint32_t DefOf(uint32_t id)
{
	for (uint32_t def = kDefs; ld32(def); def += kDefSize)
	{
		if (ld32(def) == id) return def;
	}
	return 0;
}

//the four lists of the first-person file a weapon's animations go through, by the hash's top byte
struct List { uint32_t type, at, stride; };
const List kLists[] = {{0x03, 0x58, 0x20}, {0x04, 0x68, 0x10}, {0x09, 0x78, 0x14}, {0x0A, 0x80, 0x14}};
const List* ListOf(uint32_t ref)
{
	for (const List& l : kLists)
	{
		if ((ref >> 24 & 0x7F) == l.type) return &l;
	}
	return nullptr;
}

//the sections of cp_bondfp.edb a weapon's first-person animations are in. a level loads the
//sections of what its own weapons do (only section 0 stays loaded), an animation of another one
//has nothing behind it and the first-person view breaks when it plays (the SLY 2020 or the
//WA2000 on Facility, as it's drawn or after a shot). a weapon's modes (its definition's lists at
//+0xC, +0x14 and +0x18, a row of five: the generic mode, then one for each firing mode) and
//those of its parents (+8, +0x10), as fn_80168C50 falls back to them, lead to the file's modes
//(09), which start animation sets (0A, by index after 0C000002 or 0C000003), which play blends
//(04) and animations (03). a row's second word has the section in its top half (bit 31 set on an
//animation that is loaded), the fourth the object's address when it is loaded. an object's data
//is scanned for references as far as the next object
class Walk
{
public:
	Walk(const PPCContext& c, uint32_t file) : m_c(c), m_file(file)
	{
		for (const List& l : kLists)
		{
			const uint32_t rows{ Rows(l) };
			for (int i = 0; i < Count(l); ++i) m_starts.push_back(ld32(rows + i * l.stride + 8));
		}
		std::sort(m_starts.begin(), m_starts.end());
	}

	std::set<int> Weapon(uint32_t id)
	{
		m_sections.clear();
		m_seen.clear();
		m_weapons.clear();
		Weapons(id);
		return m_sections;
	}

private:
	int Count(const List& l) const { return (int16_t)ld16(m_file + l.at); }
	uint32_t Rows(const List& l) const { return m_file + l.at + 4 + ld32(m_file + l.at + 4); }

	void Weapons(uint32_t id)
	{
		const uint32_t def{ DefOf(id) };
		if (!def || !m_weapons.insert(id).second) return;
		for (uint32_t parent : {ld32(def + 8), ld32(def + 0x10)})
		{
			if (parent && parent != kNoParent) Weapons(parent);
		}
		for (uint32_t at : {0xCu, 0x14u, 0x18u})
		{
			uint32_t list{ ld32(def + at) };
			for (int row = 0; guest_ptr(list) && ld32(list) && row < 256; ++row, list += 0x14)
			{
				for (uint32_t mode = 1; mode <= 4; ++mode)
				{
					if (const uint32_t hash{ ld32(list + mode * 4) }) Object(hash);
				}
			}
		}
	}

	void Object(uint32_t ref, const List* list = nullptr)
	{
		if (!list) list = ListOf(ref);
		if (!list) return;
		PPCContext call = m_c;
		call.r[3] = ref;
		call.r[4] = m_file + list->at;
		call.r[5] = list->stride;
		find(call);
		const int32_t index{ (int32_t)call.r[3] };
		if (index < 0 || index >= Count(*list) || !m_seen.insert((uint64_t)list->type << 32 | (uint32_t)index).second) return;
		const uint32_t row{ Rows(*list) + (uint32_t)index * list->stride };
		m_sections.insert(ld32(row + 4) >> 16 & 0x7FFF);
		const uint32_t data{ ld32(row + 12) };
		if (list->type == 0x03 || !guest_ptr(data)) return;
		const auto next = std::upper_bound(m_starts.begin(), m_starts.end(), ld32(row + 8));
		const uint32_t size{ std::min<uint32_t>(next == m_starts.end() ? 0x200 : *next - ld32(row + 8), 0x1000) };
		for (uint32_t at = 0; at + 4 <= size; at += 4)
		{
			const uint32_t v{ ld32(data + at) };
			if ((v == kStartCommands[0] || v == kStartCommands[1]) && at + 8 <= size) Object(ld32(data + at + 4), &kLists[3]);
			else if (v > 0xFFFFFF && ListOf(v)) Object(v);
		}
	}

	const PPCContext& m_c;
	uint32_t m_file;
	std::vector<uint32_t> m_starts;
	std::set<int> m_sections;
	std::set<uint64_t> m_seen;
	std::set<uint32_t> m_weapons;
};

//the weapons whose first-person animations the level has loaded, none before it's loaded.
//worked out once a frame: the guards are set up a lot at a time
const std::vector<uint32_t>& Loaded(const PPCContext& c)
{
	static std::vector<uint32_t> loaded;
	static uint32_t frame = 0, file = 0;
	const uint32_t now{ FpFile(c) };
	if (frame == ld32(kFrameCount) && file == now) return loaded;
	frame = ld32(kFrameCount);
	file = now;
	loaded.clear();
	if (!now) return loaded;
	//what every weapon has is Bond's own (moving, climbing), the level has what it needs of that
	Walk walk(c, now);
	std::vector<std::set<int>> sections;
	std::set<int> common;
	for (uint32_t id : kGiveAll)
	{
		sections.push_back(walk.Weapon(id));
		if (sections.size() == 1) common = sections.back();
		std::set<int> both;
		std::set_intersection(common.begin(), common.end(), sections.back().begin(), sections.back().end(), std::inserter(both, both.begin()));
		common = both;
	}
	const uint32_t rows{ now + 0x44 + ld32(now + 0x44) };
	const int count{ (int16_t)ld16(now + 0x40) };
	for (size_t i = 0; i < std::size(kGiveAll); ++i)
	{
		bool ok = true;
		for (int section : sections[i])
		{
			if (section != 0 && !common.count(section) && (section >= count || !ld32(rows + section * 16 + 12))) ok = false;
		}
		if (ok) loaded.push_back(kGiveAll[i]);
	}
	if (g_trace)
	{
		std::string list;
		for (uint32_t id : loaded) list += " " + Hex(id);
		std::string sections;
		if (now)
		{
			const uint32_t rows{ now + 0x44 + ld32(now + 0x44) };
			for (int i = 0; i < (int16_t)ld16(now + 0x40) && i < 64; ++i)
			{
				if (ld32(rows + i * 16 + 12)) sections += " " + std::to_string(i);
			}
		}
		rt_log("goldeneye: randomizer: %zu weapon(s) with their animations loaded:%s (sections in:%s)", loaded.size(), list.c_str(), sections.c_str());
	}
	return loaded;
}

//GE_TRACE: what each weapon may carry, once
void TraceAllowed(const PPCContext& c)
{
	static bool said = false;
	if (!g_trace || said) return;
	said = true;
	for (uint32_t id : kGiveAll) rt_log("goldeneye: randomizer: %08X may carry %s", id, Describe(Allowed(c, id)).c_str());
}

//(controller, weapon id, ammo, rounds in the clip, the launcher's ammo, flags, from a pickup)
void Give(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] }, id{ c.r[4] };
	const bool pickup{ (c.r[9] & 0xFF) != 0 };
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

void install_randomizer()
{
	g_on = std::atoi(saved_setting("GE_WEAPON_RANDOMIZER", "LAUNCH_WEAPONRANDOMIZER").c_str()) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
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
	find = ppc_lookup(kFind);
	file_info = ppc_lookup(kFileInfo);
	attachment_allowed = ppc_lookup(kAttachmentAllowed);
	if (!orig_give || !orig_attach || !attachment_allowed || !find || !file_info) rt_die("goldeneye: the weapon randomizer is not hooked in this build");
}

} //namespace ge
