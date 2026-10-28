//007 Custom: a mission played as 007 Classic with the guards set by four sliders, after the
//N64 game's 007 mode. Select Difficulty gets the item under Time Trial, its page has the
//sliders and "Start Mission" (port/mods/modmenu.cpp makes both)
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
//Enemy Health      0 to 2000: the player's shot is divided by it, at 0 any shot kills
//Enemy Damage      0 to 2000: a guard's bullet and its melee times it
//Enemy Reactions   0 to 100: both waits are longer the lower it is, twice as long at 0
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
#include <string>
#include <vector>

namespace ge
{
namespace
{

enum { Health, Damage, Reaction, Protection };
struct Slider { const char* label; const char* about; int most, step; };
const Slider kSliders[kCustomSliders] = {
	{"Enemy Health", "How much it takes to bring an enemy down. At 0% one shot does.", 2000, 10},
	{"Enemy Damage", "How hard an enemy's bullets and melee hit you.", 2000, 10},
	{"Enemy Reactions", "How fast an enemy that has seen you starts to fight and to fire. At 0% it takes twice as long.", 100, 5},
	{"Hit Protection", "How long after a hit weaker hits don't hurt you.", 1000, 10}};
const int kNormal = 100;
const double kOneShot = 1.0e6; //the player's shot times this with the health at 0
const auto kStepEvery = std::chrono::milliseconds(40); //a held direction moves a slider this often, at any frame rate

std::atomic<int> g_value[kCustomSliders] = {{kNormal}, {kNormal}, {kNormal}, {kNormal}};
std::atomic<bool> g_on{false};
bool g_always = false; //GE_CUSTOM_ON
PPCFunc orig_bullet = nullptr, orig_shot = nullptr, orig_fight_wait = nullptr, orig_protection = nullptr, orig_melee = nullptr, orig_fire_wait = nullptr;
PPCFunc orig_setting_get = nullptr, orig_setting_step = nullptr, orig_choice_text = nullptr, orig_choice_step = nullptr;
std::atomic<bool> g_headshots{true}; //a shot to a guard's head kills, as in the game
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

//GE_TRACEHIT: every hit a guard takes
PPCFunc orig_npc_hit = nullptr, orig_slider_draw = nullptr;
bool g_tracehit = false;
void NpcHit(PPCContext& c)
{
	const uint32_t npc = c.r[3], hit = c.r[4];
	const float before = ldf(npc + 0x3A8);
	const uint32_t flags = ld32(hit + 0x3C);
	const float damage = ldf(hit + 0x44);
	//a shot to the head (the flags' bit 0) is taken as it comes, with no factor for the place and
	//none for the difficulty, and comes with the most of the weapon's damages by hit kind (the
	//floats at the attacker's +0x1D0). without headshots that kill it is a shot at the body: the
	//least of those damages, and bit 0 gone for the call so the difficulty's factor counts
	const uint32_t kHead = 1;
	const bool body = Active() && !g_headshots && (flags & kHead) != 0;
	if (body)
	{
		float least = damage;
		const uint32_t by = ld32(hit + 0x30);
		for (uint32_t i = 0; guest_ptr(by) && i < 15; ++i)
		{
			const float one = ldf(by + 0x1D0 + i * 4);
			if (one > 0 && one < least) least = one;
		}
		st32(hit + 0x3C, flags & ~kHead);
		stf(hit + 0x44, least);
	}
	orig_npc_hit(c);
	if (body) { st32(hit + 0x3C, flags); stf(hit + 0x44, damage); }
	if (!g_tracehit) return;
	const uint32_t by = ld32(hit + 0x30);
	std::string table;
	char number[24];
	for (uint32_t i = 0; guest_ptr(by) && i < 15; ++i) { std::snprintf(number, sizeof number, " %g", ldf(by + 0x1D0 + i * 4)); table += number; }
	rt_log("goldeneye: hit: guard %08X flags %08X damage %g by %08X (vtable %08X), health %g of %g, then %g; its damages%s", npc, flags, damage, by, guest_ptr(by) ? ld32(by) : 0, before, ldf(npc + 0x3AC), ldf(npc + 0x3A8), table.c_str());
}

//the mouse on the sliders. the menu draws a slider's bar (ge_slider_draw: the menu, x, y, width,
//height in a screen of 640 by 448) right after it asked for its value, so a bar drawn after
//one of the four was asked for is that slider's. the cursor is free in a menu (input.cpp):
//a press on a bar takes hold of the slider and it follows the cursor until the button is let
//go. lighting the row under the cursor and select are the menus' own pointer's (pointer.cpp)
const float kGrabAcross = 6.0f, kGrabDown = 6.0f; //how far outside a bar a press still takes hold of it
const float kRowLeft = 125.0f, kRowRight = 500.0f; //a row of the list, across
const uint32_t kUpFrames = 3; //the page is up when a bar was drawn this many frames ago at most
struct Bar { float x, y, wide, high; };
Bar g_bars[kCustomSliders] = {};
int g_asked = -1; //the slider ge_setting_get was asked for last
int g_held = -1; //the slider the mouse has hold of
uint32_t g_stepped_frame = 0, g_clicked_frame = 0; //the frames the switch was changed through the menu, and by a click on its row
uint32_t g_drawn_frame = 0, g_mouse_frame = 0;
bool g_up = false, g_pressed = false, g_was_left = false;
std::atomic<bool> g_on_bar{false}; //the button went down on a bar and is still down: not the menu's select

//the page's row at a place of the screen, -1 for none
int RowAt(float x, float y)
{
	const Bar& first = g_bars[0];
	if (first.wide <= 0 || x < kRowLeft || x > kRowRight) return -1;
	const float pitch = g_bars[1].y > first.y ? g_bars[1].y - first.y : 28.0f;
	const float top = first.y + first.high / 2 - pitch / 2;
	const int row = y >= top ? int((y - top) / pitch) : -1;
	return row < kCustomRows ? row : -1;
}

void SliderDraw(PPCContext& c)
{
	const int slider = g_asked;
	g_asked = -1;
	if (slider >= 0)
	{
		const uint32_t menu = c.r[3], frame = ld32(kFrameCount);
		Bar& bar = g_bars[slider];
		bar = { float((int32_t)c.r[4]), float((int32_t)c.r[5]), float((int32_t)c.r[6]), float((int32_t)c.r[7]) };
		g_drawn_frame = frame;
		static bool said[kCustomSliders] = {};
		if (g_trace && !said[slider] && (said[slider] = true)) rt_log("goldeneye: 007 Custom: slider %d's bar is at %g %g, %g by %g", slider, bar.x, bar.y, bar.wide, bar.high);
		float x = 0, y = 0;
		bool left = false;
		const bool cursor = menu_cursor(x, y, left);
		if (frame != g_mouse_frame)
		{
			//once a frame: a press, and whether the cursor moved
			g_mouse_frame = frame;
			g_pressed = cursor && left && !g_was_left;
			g_was_left = cursor && left;
			if (!g_was_left) g_held = -1;
			g_on_bar = g_held >= 0;
		}
		if (cursor && bar.wide > 0)
		{
			const bool over = x >= bar.x - kGrabAcross && x <= bar.x + bar.wide + kGrabAcross && y >= bar.y - kGrabDown && y <= bar.y + bar.high + kGrabDown;
			if (g_pressed && over) { g_held = slider; g_on_bar = true; }
			if (g_held == slider)
			{
				const Slider& s = kSliders[slider];
				const float part = std::clamp((x - bar.x) / bar.wide, 0.0f, 1.0f);
				g_value[slider] = std::clamp(int(part * s.most / s.step + 0.5f) * s.step, 0, s.most);
				c.f[1] = double(g_value[slider]) / s.most;
			}
			//a click on the switch's row is the switch
			//(not when the press was on one of its arrows, which the menu's pointer has just taken as a step)
			if (g_pressed && slider == 0 && RowAt(x, y) == kCustomSliders && frame - g_stepped_frame > 1)
			{
				g_headshots = !g_headshots;
				g_clicked_frame = frame;
			}
		}
	}
	orig_slider_draw(c);
}

int SliderOf(uint32_t setting)
{
	return setting >= kCustomSetting && setting < kCustomSetting + kCustomSliders ? int(setting - kCustomSetting) : -1;
}

//(profile, a setting's hash, the pad, -, as it is) -> f1 how full its bar is, 0 to 1
void SettingGet(PPCContext& c)
{
	const int slider = SliderOf(c.r[4]);
	g_asked = slider;
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

//(profile, a setting's hash, the pad) -> the text of its value
void ChoiceText(PPCContext& c)
{
	if (c.r[4] != kCustomHeadshots) { orig_choice_text(c); return; }
	c.r[3] = g_headshots ? kCustomTextOn : kCustomTextOff;
}

//(profile, a setting's hash, -1 or 1, the pad)
void ChoiceStep(PPCContext& c)
{
	if (c.r[4] != kCustomHeadshots) { orig_choice_step(c); return; }
	const uint32_t frame = ld32(kFrameCount);
	if (frame - g_clicked_frame <= 1) return; //that press has changed it already
	g_headshots = !g_headshots;
	g_stepped_frame = frame;
}

} //namespace

//once a frame: the page with the sliders went, nothing is held any more
void custom_frame(uint32_t frame)
{
	const bool up = g_drawn_frame != 0 && frame - g_drawn_frame <= kUpFrames;
	if (up == g_up) return;
	g_up = up;
	if (up) return;
	g_held = -1;
	g_on_bar = false;
	g_was_left = false;
}

//in the classic's filter: a press that has hold of a slider isn't the menu's select too
void custom_buttons(ClassicState& s)
{
	const uint32_t kZR = 0x0004, kA = 0x0010;
	if (g_on_bar) s.buttons &= ~(kZR | kA);
}

bool custom_mode() { return g_on; }

void set_custom_mode(bool on)
{
	if (g_always) return;
	const bool was = g_on.exchange(on);
	if (on)
	{
		//a mission is started with them: kept for the next time the game runs
		rt_log("goldeneye: 007 Custom: enemy health %d%%, damage %d%%, reaction speed %d%%, hit protection %d%%, headshots that kill %s", g_value[Health].load(), g_value[Damage].load(), g_value[Reaction].load(), g_value[Protection].load(), g_headshots ? "on" : "off");
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
	std::snprintf(text, sizeof text, "%d,%d,%d,%d,%d", g_value[0].load(), g_value[1].load(), g_value[2].load(), g_value[3].load(), g_headshots ? 1 : 0);
	return text;
}

void install_custom()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	//GE_CUSTOM=health,damage,reaction,protection[,headshots that kill]
	const std::string saved = saved_setting("GE_CUSTOM", "LAUNCH_CUSTOM");
	int values[kCustomSliders], headshots = 1;
	if (std::sscanf(saved.c_str(), "%d,%d,%d,%d,%d", &values[0], &values[1], &values[2], &values[3], &headshots) >= kCustomSliders)
	{
		for (int i = 0; i < kCustomSliders; ++i) g_value[i] = std::clamp(values[i], 0, kSliders[i].most);
		g_headshots = headshots != 0;
	}
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
	orig_choice_text = ppc_hook("ge_choice_text", ChoiceText);
	orig_choice_step = ppc_hook("ge_choice_step", ChoiceStep);
	orig_npc_hit = ppc_hook("ge_npc_hit", NpcHit);
	orig_slider_draw = ppc_hook("ge_slider_draw", SliderDraw);
	g_tracehit = std::getenv("GE_TRACEHIT") != nullptr;
	if (!orig_bullet || !orig_shot || !orig_fight_wait || !orig_protection || !orig_melee || !orig_fire_wait || !orig_setting_get || !orig_setting_step)
		rt_log("goldeneye: 007 Custom: a function of it isn't in this build, some of it won't work");
}

} //namespace ge
