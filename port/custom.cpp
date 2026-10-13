//007 Custom: a mission played as 007 Classic with the guards set by four sliders, after the
//N64 game's 007 mode. Select Difficulty gets the item under Time Trial, its page has the
//sliders and "Start Mission" (port/modmenu.cpp makes both)
//
//a mission has a row of floats by difficulty (levels 805D1804 +0x34, four each, the difficulty
//is the short at 805DE6CC), each read by a function of its own:
//fn_800C0B70 +0x04 what a guard's bullet does to the player: the place hit's factor times this
//  times the weapon's damage (fn_80102530, fn_80149D90). 0.2 on Dam as 007 Classic, more each mission
//fn_800C0B90 +0x14 what the player's shot does to a guard, times the place hit's factor
//  (fn_800517E0, fn_800A9AC0). 1.4 1.2 1 1
//fn_800C0BB0 +0x24 the seconds a guard that has seen the player waits before it fights, after
//  its shout (fn_80081330, the state at +0x7D8 going 3 to 4). 2 1.5 1 1
//fn_800C0BD0 +0x34 the seconds after a hit in which the player is only hurt by one 1.1 times
//  as hard (fn_80135040 sets the player's +0xEA4, fn_8013A1C0 asks). 0.26 0.22 0.15 0.28
//fn_800C0C50 +0x44 how an enemy's missile homes (LaunchMissile, fn_8028B750). left alone
//fn_800C0C70 +0x54 what a guard's melee does, into four of its hit kinds as it is made
//  (fn_8004EA60, the table at +0x1D0 that fn_800B7BA0 takes a hit's damage from). 30 40 45 50
//fn_800C0C90 +0x64 times the weapon's own time a guard has to have had its target before it
//  fires (fn_80086430; XItemHandler_NPC's target time +0x728). 1 on every difficulty
//
//the sliders, a hundred being 007 Classic as the game has it:
//Enemy Health      0 to 1000: the player's shot is divided by it, at 0 any shot kills
//Enemy Damage      0 to 1000: a guard's bullet and its melee times it
//Enemy Reaction    0 to 100: both waits are longer the lower it is, twice as long at 0
//Hit Protection    0 to 1000: the time after a hit times it
//
//the sliders are the game's own (a row of kind 5 with a setting's hash): the menu asks
//ge_setting_get (8022F4D0) how full the bar is and tells ge_setting_step (8022E2F0) to move it,
//both by the setting's hash. four hashes the game doesn't have are answered here
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace ge
{
namespace
{

enum { Health, Damage, Reaction, Protection };
struct Slider { const char* label; const char* about; int most, step; };
const Slider kSliders[kCustomSliders] = {
	{"Enemy Health", "How much it takes to bring an enemy down. At 0% one shot does.", 1000, 10},
	{"Enemy Damage", "How hard an enemy's bullets and melee hit you.", 1000, 10},
	{"Enemy Reaction Speed", "How fast an enemy that has seen you starts to fight and to fire. At 0% it takes twice as long.", 100, 5},
	{"Hit Protection", "How long after a hit weaker hits don't hurt you.", 1000, 10}};
const int kNormal = 100;
const double kOneShot = 1.0e6; //the player's shot times this with the health at 0
const auto kStepEvery = std::chrono::milliseconds(40); //a held direction moves a slider this often, at any frame rate

std::atomic<int> g_value[kCustomSliders] = {{kNormal}, {kNormal}, {kNormal}, {kNormal}};
std::atomic<bool> g_on{false};
bool g_always = false; //GE_CUSTOM_ON
PPCFunc orig_bullet = nullptr, orig_shot = nullptr, orig_fight_wait = nullptr, orig_protection = nullptr, orig_melee = nullptr, orig_fire_wait = nullptr;
PPCFunc orig_setting_get = nullptr, orig_setting_step = nullptr;
bool g_trace = false;

bool Active() { return g_on && !multiplayer_game(); }

//GE_TRACE: the first few of each value as the game had it and as it got it
const int kTraced = 6;
void Trace(const char* what, double game, double got)
{
	static std::map<const char*, int> counts;
	if (!g_trace) return;
	int& said = counts[what];
	if (said < kTraced && ++said) rt_log("goldeneye: 007 Custom: %s %g, the game's %g (difficulty %u)", what, got, game, ld16(0x805DE6CC));
}
double Part(int slider) { return g_value[slider] / double(kNormal); }
//a wait at that reaction speed: as the game has it at 100, twice at 0
double Slow() { return 2.0 - std::min(Part(Reaction), 1.0); }

void Bullet(PPCContext& c)
{
	orig_bullet(c);
	if (!Active()) return;
	const double game = c.f[1];
	c.f[1] = game * Part(Damage);
	Trace("a guard's bullet times", game, c.f[1]);
}

void Shot(PPCContext& c)
{
	orig_shot(c);
	if (!Active()) return;
	const double game = c.f[1];
	const int health = g_value[Health];
	c.f[1] = game * (health <= 0 ? kOneShot : double(kNormal) / health);
	Trace("the player's shot times", game, c.f[1]);
}

void FightWait(PPCContext& c)
{
	orig_fight_wait(c);
	if (!Active()) return;
	const double game = c.f[1];
	c.f[1] = game * Slow();
	Trace("a guard waits to fight for", game, c.f[1]);
}

void ProtectionTime(PPCContext& c)
{
	orig_protection(c);
	if (!Active()) return;
	const double game = c.f[1];
	c.f[1] = game * Part(Protection);
	Trace("a hit protects for", game, c.f[1]);
}

void Melee(PPCContext& c)
{
	orig_melee(c);
	if (!Active()) return;
	const double game = c.f[1];
	c.f[1] = game * Part(Damage);
	Trace("a guard's melee does", game, c.f[1]);
}

void FireWait(PPCContext& c)
{
	orig_fire_wait(c);
	if (!Active()) return;
	const double game = c.f[1];
	c.f[1] = game * Slow();
	Trace("a guard's wait to fire times", game, c.f[1]);
}

int SliderOf(uint32_t setting)
{
	return setting >= kCustomSetting && setting < kCustomSetting + kCustomSliders ? int(setting - kCustomSetting) : -1;
}

//(profile, a setting's hash, the pad, -, as it is) -> f1 how full its bar is, 0 to 1
void SettingGet(PPCContext& c)
{
	const int slider = SliderOf(c.r[4]);
	if (slider < 0) { orig_setting_get(c); return; }
	c.f[1] = double(g_value[slider]) / kSliders[slider].most;
}

//(profile, a setting's hash, -1 or 1, the pad) -> 1 when it moved
void SettingStep(PPCContext& c)
{
	const int slider = SliderOf(c.r[4]);
	if (slider < 0) { orig_setting_step(c); return; }
	static std::chrono::steady_clock::time_point last;
	const auto now = std::chrono::steady_clock::now();
	const int way = (int32_t)c.r[5];
	c.r[3] = 0;
	if (way == 0 || now - last < kStepEvery) return;
	const Slider& s = kSliders[slider];
	const int to = std::clamp(g_value[slider] + (way < 0 ? -s.step : s.step), 0, s.most);
	if (to == g_value[slider]) return;
	last = now;
	g_value[slider] = to;
	c.r[3] = 1;
}

} //namespace

bool custom_mode() { return g_on; }

void set_custom_mode(bool on)
{
	if (g_always) return;
	const bool was = g_on.exchange(on);
	if (on)
	{
		//a mission is started with them: kept for the next time the game runs
		rt_log("goldeneye: 007 Custom: enemy health %d%%, damage %d%%, reaction speed %d%%, hit protection %d%%", g_value[Health].load(), g_value[Damage].load(), g_value[Reaction].load(), g_value[Protection].load());
		save_settings();
	}
	else if (was) rt_log("goldeneye: 007 Custom is off");
}

std::string custom_label(int slider)
{
	char text[64];
	std::snprintf(text, sizeof text, "%s: %d%%", kSliders[slider].label, g_value[slider].load());
	return text;
}

const char* custom_about(int slider) { return kSliders[slider].about; }

std::string custom_values()
{
	char text[64];
	std::snprintf(text, sizeof text, "%d,%d,%d,%d", g_value[0].load(), g_value[1].load(), g_value[2].load(), g_value[3].load());
	return text;
}

void install_custom()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	//GE_CUSTOM=health,damage,reaction,protection
	const std::string saved = saved_setting("GE_CUSTOM", "LAUNCH_CUSTOM");
	int values[kCustomSliders];
	if (std::sscanf(saved.c_str(), "%d,%d,%d,%d", &values[0], &values[1], &values[2], &values[3]) == kCustomSliders)
		for (int i = 0; i < kCustomSliders; ++i) g_value[i] = std::clamp(values[i], 0, kSliders[i].most);
	//GE_CUSTOM_ON=1 every mission is played with them, whatever it was started from
	if (const char* e = std::getenv("GE_CUSTOM_ON")) g_on = g_always = std::atoi(e) != 0;

	orig_bullet = ppc_hook("ge_enemy_bullet_scale", Bullet);
	orig_shot = ppc_hook("ge_player_shot_scale", Shot);
	orig_fight_wait = ppc_hook("ge_enemy_fight_wait", FightWait);
	orig_protection = ppc_hook("ge_hit_protection_time", ProtectionTime);
	orig_melee = ppc_hook("ge_enemy_melee_damage", Melee);
	orig_fire_wait = ppc_hook("ge_enemy_fire_wait", FireWait);
	orig_setting_get = ppc_hook("ge_setting_get", SettingGet);
	orig_setting_step = ppc_hook("ge_setting_step", SettingStep);
	if (!orig_bullet || !orig_shot || !orig_fight_wait || !orig_protection || !orig_melee || !orig_fire_wait || !orig_setting_get || !orig_setting_step)
		rt_log("goldeneye: 007 Custom: a function of it isn't in this build, some of it won't work");
}

} //namespace ge
