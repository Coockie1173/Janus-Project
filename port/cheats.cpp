//three cheats of the kind every shooter has, off by default and for the campaign only: in a
//network game the host has the players and a console can't be told otherwise
//
//God Mode: every hurt the player takes goes through ge_player_damage (801032C0: the one hurt,
//  who did it, the kind, the damage in f1). it takes the armour (+0x5E8) and the health
//  (+0x3A8) down, calls the hurt's reaction (the vtable's +0x17C: the red screen, the shake)
//  and at no health left the death. for the local player it isn't called at all
//  what kills without hurting still does: a fall out of the level, a mission's own failure
//Infinite Sprint: the sprint left (+0xE5C, stamina.cpp) is kept full
//Infinite Ammo: the inventory (+0xC40: the count at +8, the items at +0xC, 0x10 each) has an
//  item for each kind of ammo, its id at +0, how many at +4 and the most it holds at +8
//  (fn_8010B7A0 reads one, ammo_FUN_8010b960 adds to one). every one that holds more than one
//  is kept at its most: the rounds in the gun are taken out of it as they are fired
//
//they stay on from one start to the next (LAUNCH_GOD, _INFINITEAMMO, _INFINITESPRINT in the settings file)
//GE_GOD=1 GE_INFINITE_AMMO=1 GE_INFINITE_SPRINT=1 start with them on
//GE_TRACE=1 logs the inventory when it changes and the first hurts not taken
#include "ge.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace ge
{
namespace
{
const uint32_t kSprintLeft = 0xE5C;
const uint32_t kSprintFull = 0x806E9490, kSprintFullOther = 0x806E9494, kSprintOther = 0x806EBA7D;
const uint32_t kItemCount = 0xC48, kItems = 0xC4C, kItemSize = 0x10, kItemHas = 0x4, kItemMost = 0x8;
const uint32_t kMostItems = 64;
const uint32_t kCtrlHostCopyOf = 0x98C;

std::atomic<bool> g_god{false}, g_ammo{false}, g_sprint{false};
PPCFunc orig_damage = nullptr;
bool g_trace = false;

bool Campaign() { return !multiplayer_game(); }

void TraceItems(uint32_t ctrl, uint32_t items, uint32_t count)
{
	static std::vector<uint32_t> said;
	std::vector<uint32_t> now;
	for (uint32_t i = 0; i < count * kItemSize; i += 4) now.push_back(ld32(items + i));
	if (now == said) return;
	said = now;
	rt_log("goldeneye: cheats: the inventory of %08X, %u item(s)", ctrl, count);
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t at{ items + i * kItemSize };
		rt_log("goldeneye: cheats:   %08X has %d of %d, %04X %02X %02X", ld32(at), (int)ld32(at + kItemHas), (int)ld32(at + kItemMost), ld16(at + 0xC), ld8(at + 0xE), ld8(at + 0xF));
	}
}

void Ammo(uint32_t ctrl)
{
	const uint32_t count{ ld32(ctrl + kItemCount) };
	const uint32_t items{ ld32(ctrl + kItems) };
	if (!guest_ptr(items) || count == 0 || count > kMostItems) return;
	if (g_trace) TraceItems(ctrl, items, count);
	if (!g_ammo) return;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t at{ items + i * kItemSize };
		const int32_t most{ (int32_t)ld32(at + kItemMost) };
		if (most > 1 && (int32_t)ld32(at + kItemHas) < most) st32(at + kItemHas, (uint32_t)most);
	}
}

void Sprint(uint32_t ctrl)
{
	const float full{ ldf(ld8(kSprintOther) ? kSprintFullOther : kSprintFull) };
	if (std::isfinite(full) && full > 0) stf(ctrl + kSprintLeft, full);
}

//(the one hurt, who did it, the kind; f1 the damage)
void Damage(PPCContext& c)
{
	const uint32_t hurt{ c.r[3] };
	const uint32_t local{ local_controller() };
	const bool ours{ local && guest_ptr(hurt) && (hurt == local || ld32(hurt + kCtrlHostCopyOf) == local) };
	const bool taken{ !g_god || !ours || !Campaign() };
	static int said = 0;
	if (g_trace && ours && said < 8 && ++said) rt_log("goldeneye: cheats: a hurt of %.2f (kind %u) by %08X %s, health %.2f", c.f[1], c.r[5], c.r[4], taken ? "taken" : "not taken (god mode)", ldf(hurt + 0x3A8));
	if (taken) orig_damage(c);
}
} //namespace

bool god_mode() { return g_god; }
void set_god_mode(bool on) { g_god = on; }
bool infinite_ammo() { return g_ammo; }
void set_infinite_ammo(bool on) { g_ammo = on; }
bool infinite_sprint() { return g_sprint; }
void set_infinite_sprint(bool on) { g_sprint = on; }

void cheats_apply(uint32_t ctrl)
{
	if (!Campaign()) return;
	if (g_sprint) Sprint(ctrl);
	if (g_ammo || g_trace) Ammo(ctrl);
}

void install_cheats()
{
	g_god = std::atoi(saved_setting("GE_GOD", "LAUNCH_GOD").c_str()) != 0;
	g_ammo = std::atoi(saved_setting("GE_INFINITE_AMMO", "LAUNCH_INFINITEAMMO").c_str()) != 0;
	g_sprint = std::atoi(saved_setting("GE_INFINITE_SPRINT", "LAUNCH_INFINITESPRINT").c_str()) != 0;
	if (g_god || g_ammo || g_sprint) rt_log("goldeneye: cheats on:%s%s%s", g_god ? " god mode" : "", g_ammo ? " infinite ammo" : "", g_sprint ? " infinite sprint" : "");
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_damage = ppc_hook("ge_player_damage", Damage);
	if (!orig_damage) rt_die("goldeneye: ge_player_damage is not hooked in this build");
}

} //namespace ge
