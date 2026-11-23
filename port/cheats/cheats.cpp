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
//Turbo Mode: ge_move_speed (8010C290: the walk's 4.2, the sprint's 6.6, crouched, aimed) answers
//  the local player's speed times the slider (bunnyhop.cpp's hook calls turbo_speed). asked for
//  the time a step takes (its last argument 0) it answers one over the speed, so that's divided
//Grenades: a network game's loadout (fn_800C2740) gives its grenades through fn_8014BC00
//  (controller, item, how many, the most, 0, 0xFF): 0x2000756 the frag grenade, 2 to 4 of them
//  by the gadget. the campaign gives none, so the player gets kGrenades the frame its inventory
//  has no frag item (a level's start, a checkpoint's restart). Infinite Ammo keeps them full
//  the throw (fn_80157990, from ge_weapon_input on action bit 8) takes the inventory's item
//  number at +0xC99, which the loadout's cycle sets (fn_8014B240) and the campaign leaves at -1:
//  it's the frag's while nothing else is chosen
//
//they stay on from one start to the next (LAUNCH_GOD, _INFINITEAMMO, _INFINITESPRINT, _TURBO, _GRENADES in the settings file)
//GE_GOD=1 GE_INFINITE_AMMO=1 GE_INFINITE_SPRINT=1 GE_GRENADES=1 start with them on, GE_TURBO=2.5 the turbo
//GE_TRACE=1 logs the inventory when it changes and the first hurts not taken
#include "ge.h"
#include <algorithm>
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
const uint32_t kGiveThrown = 0x8014BC00, kFrag = 0x02000756, kThrownFlags = 0xFF;
const uint32_t kGrenades = 4; //a loadout's most
const uint32_t kThrownChosen = 0xC99; //the item Throw Grenade throws, -1 none

std::atomic<bool> g_god{false}, g_ammo{false}, g_sprint{false}, g_grenades{false};
std::atomic<float> g_turbo{1.0f};
PPCFunc orig_damage = nullptr, give_thrown = nullptr;
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

int FragAt(uint32_t ctrl)
{
	const uint32_t count{ ld32(ctrl + kItemCount) };
	const uint32_t items{ ld32(ctrl + kItems) };
	if (!guest_ptr(items) || count > kMostItems) return -2;
	for (uint32_t i = 0; i < count; ++i)
		if (ld32(items + i * kItemSize) == kFrag) return (int)i;
	return -1;
}

void Grenades(PPCContext& c, uint32_t ctrl)
{
	int at{ FragAt(ctrl) };
	if (at == -2) return;
	if (at < 0)
	{
		PPCContext call = c;
		call.r[3] = ctrl;
		call.r[4] = kFrag;
		call.r[5] = kGrenades;
		call.r[6] = kGrenades;
		call.r[7] = 0;
		call.r[8] = kThrownFlags;
		give_thrown(call);
		at = FragAt(ctrl);
		if (g_trace) rt_log("goldeneye: cheats: %u frag grenades for %08X, item %d", kGrenades, ctrl, at);
		if (at < 0) return;
	}
	if ((int8_t)ld8(ctrl + kThrownChosen) < 0) st8(ctrl + kThrownChosen, (uint8_t)at);
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
float turbo() { return g_turbo; }
void set_turbo(float scale) { g_turbo = std::isfinite(scale) ? std::clamp(scale, 1.0f, kTurboMax) : 1.0f; }
bool grenades() { return g_grenades; }
void set_grenades(bool on) { g_grenades = on; }

void turbo_speed(uint32_t ctrl, uint32_t out, bool asSpeed)
{
	const float scale{ g_turbo };
	if (scale == 1.0f || !Campaign() || !guest_ptr(out) || ctrl != local_controller()) return;
	const float now{ ldf(out) };
	if (std::isfinite(now) && now != 0) stf(out, asSpeed ? now * scale : now / scale);
}

void cheats_apply(PPCContext& c, uint32_t ctrl)
{
	cheats_unlock_check();
	if (!Campaign()) return;
	if (g_sprint) Sprint(ctrl);
	if (g_grenades) Grenades(c, ctrl);
	if (g_ammo || g_trace) Ammo(ctrl);
}

void install_cheats()
{
	g_god = std::atoi(saved_setting("GE_GOD", "LAUNCH_GOD").c_str()) != 0;
	g_ammo = std::atoi(saved_setting("GE_INFINITE_AMMO", "LAUNCH_INFINITEAMMO").c_str()) != 0;
	g_sprint = std::atoi(saved_setting("GE_INFINITE_SPRINT", "LAUNCH_INFINITESPRINT").c_str()) != 0;
	g_grenades = std::atoi(saved_setting("GE_GRENADES", "LAUNCH_GRENADES").c_str()) != 0;
	const std::string turbo{ saved_setting("GE_TURBO", "LAUNCH_TURBO") };
	if (!turbo.empty()) set_turbo((float)std::atof(turbo.c_str()));
	if (g_god || g_ammo || g_sprint || g_grenades || g_turbo != 1.0f) rt_log("goldeneye: cheats on:%s%s%s%s, turbo %.1fx", g_god ? " god mode" : "", g_ammo ? " infinite ammo" : "", g_sprint ? " infinite sprint" : "", g_grenades ? " grenades" : "", g_turbo.load());
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_damage = ppc_hook("ge_player_damage", Damage);
	if (!orig_damage) rt_die("goldeneye: ge_player_damage is not hooked in this build");
	give_thrown = ppc_lookup(kGiveThrown);
	if (!give_thrown) rt_die("goldeneye: fn_8014BC00 is not in this build");
}

} //namespace ge
