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
//a guard's aim isn't in the row, it's the weapon's: a shot leaves at a random angle off where it
//aims, up to 45 degrees times (1 - accuracy), spun round the aim at random. EnemyNew's FireBullet
//(fn_80057060) asks ge_guard_accuracy (fn_80057040) for it: the weapon row's (+0x754) +0x144 / 100,
//bent by the guard's skill (+0x5FC) and clamped (fn_80086D70). ArmedNPC's (ge_npc_fire_bullet,
//8004B7E0) works out 1 - row +0x144 / 100 itself
//
//the sliders, a hundred being 007 Classic as the game has it:
//Enemy Health      0 to 2000: the player's shot is divided by it, at 0 any shot kills
//Enemy Damage      0 to 2000: a guard's bullet and its melee times it
//Enemy Reactions   0 to 100: both waits are longer the lower it is, twice as long at 0
//Hit Protection    0 to 1000: the time after a hit times it
//Enemy Accuracy    0 to 200: the angle off the aim times 2 - it, twice as wide at 0, none at 200
//
//the sliders are the game's own (a row of kind 5 with a setting's hash): the menu asks
//ge_setting_get (8022F4D0) how full the bar is and tells ge_setting_step (8022E2F0) to move it,
//both by the setting's hash. four hashes the game doesn't have are answered here
//
//what a mission is played with is shown: under the hud's timer in play (port/ui/overlay.cpp),
//and on the Mission Complete page, where left and right switch between the game's results and
//the settings. the page (ge_mission_page) draws each of its texts into a widget of its layout
//with ge_page_text (the page, its widgets, a widget's hash, a text's hash or -1 and a string, a
//style, a font, the alignment 0 left 1 right 2 centred, a word on the stack): the title 150000B1,
//the level's line 15000082, a row each from 15000083 to 15000087 (the time, the time to beat, the
//objectives), a name on the left and its value on the right, and what was unlocked 15000080 at
//the bottom. as 007 Custom the level's line ends with "007 Custom" in place of the difficulty and
//the bottom line says how to switch, with the d-pad's characters that prompts.cpp makes the keys.
//with the settings up they take the level's line and the rows, the six the layout has stripes
//under (the stripes and the list's bars are drawn from somewhere else, they don't move with the
//widgets). the strings go on the stack
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

enum { Health, Damage, Reaction, Protection, Accuracy };
struct Slider { const char* label; const char* about; int most, step; };
const Slider kSliders[kCustomSliders] = {
	{"Enemy Health", "How much it takes to bring an enemy down. At 0% one shot does.", 2000, 10},
	{"Enemy Damage", "How hard an enemy's bullets and melee hit you.", 2000, 10},
	{"Enemy Reactions", "How fast an enemy that has seen you starts to fight and to fire. At 0% it takes twice as long.", 100, 5},
	{"Hit Protection", "How long after a hit weaker hits don't hurt you.", 1000, 10},
	{"Enemy Accuracy", "How close an enemy's shots fly to where it aims. At 0% they spread twice as wide, at 200% none miss.", 200, 5}};
const int kNormal = 100;
const double kOneShot = 1.0e6; //the player's shot times this with the health at 0
const auto kStepEvery = std::chrono::milliseconds(40); //a held direction moves a slider this often, at any frame rate

std::atomic<int> g_value[kCustomSliders] = {{kNormal}, {kNormal}, {kNormal}, {kNormal}, {kNormal}};
std::atomic<bool> g_on{false};
bool g_always = false; //GE_CUSTOM_ON
PPCFunc orig_accuracy = nullptr, orig_npc_fire = nullptr;
PPCFunc orig_bullet = nullptr, orig_shot = nullptr, orig_fight_wait = nullptr, orig_protection = nullptr, orig_melee = nullptr, orig_fire_wait = nullptr;
PPCFunc orig_page_text = nullptr;
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
//how wide shots spread at that accuracy: as the game has it at 100, twice at 0, none at 200
double Spread() { return std::max(2.0 - Part(Accuracy), 0.0); }

//(a guard) -> f1 its accuracy, 0 to 1: the spread is 1 - it
void GuardAccuracy(PPCContext& c)
{
	orig_accuracy(c);
	if (!Active()) return;
	const double game = c.f[1];
	c.f[1] = 1.0 - (1.0 - game) * Spread();
	Trace("a guard's accuracy", game, c.f[1]);
}

//(an armed npc) fires: its weapon row's accuracy, in percent, is what it gets for the call
void NpcFire(PPCContext& c)
{
	const uint32_t row = guest_ptr(ld32(c.r[3] + 0x754)) ? ld32(c.r[3] + 0x754) : 0;
	if (!Active() || !row) { orig_npc_fire(c); return; }
	const uint32_t kRowAccuracy = 0x144;
	const float game = ldf(row + kRowAccuracy);
	const float got = float(100.0 - (100.0 - game) * Spread());
	stf(row + kRowAccuracy, got);
	Trace("an armed npc's accuracy", game, got);
	orig_npc_fire(c);
	stf(row + kRowAccuracy, game);
}

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

//the Mission Complete page with the settings up
const uint32_t kPageTitle = 0x150000B1, kPageLevel = 0x15000082, kPageOutcome = 0x15000080;
const uint32_t kPageRow = 0x15000083, kPageRows = 0x1D; //the rows' widgets, as many as the page could have
//the page's widgets (fn_8023F420): 0x24 bytes each, +0 there, +4 to +0x10 the box in 640 by 448
const uint32_t kFirstWidget = 0x15000080, kWidgetCount = 0x32, kWidgets = 0x914 + 0x9B4, kWidgetSize = 0x24;
const uint32_t kRowStyle = 0x700002E, kRowFont = 0x109002A, kLeft = 0, kRight = 1, kCentred = 2;
const uint32_t kPageUpFrames = 3; //the page is up when it drew this many frames ago at most
const uint32_t kLeftButton = 0x0002, kRightButton = 0x8000; //the classic's d-pad
const uint32_t kDifficultyTexts = 0x805861F8, kPageDifficulty = 0x1A; //a text's hash a difficulty, the page's difficulty (a short)
const char16_t kLeftGlyph = 0x2738, kRightGlyph = 0x2739; //the d-pad in the font: prompts.cpp makes them the keys
bool g_page_drawing = false; //inside ge_mission_page
std::atomic<uint32_t> g_page_frame{0};
std::atomic<bool> g_page_settings{false}; //the settings are up instead of the results
uint32_t g_page_buttons = 0;
bool g_listed = false;

bool PageUp() { return Active() && g_page_frame != 0 && ld32(kFrameCount) - g_page_frame <= kPageUpFrames; }

uint32_t Widget(uint32_t widgets, uint32_t hash) { return widgets + kWidgets + (hash - kFirstWidget) * kWidgetSize; }

//one text into a widget of the page: the call as the game made it, with a string and an alignment, and
//in another widget a row's style. the string goes on the stack under the call's frame, the callee's frame under it
void PageString(const PPCContext& c, uint32_t widget, const std::u16string& text, uint32_t align)
{
	const uint32_t kRoom = 0x200, kFrame = 0x40;
	const uint32_t stack = c.r[1], string = stack - kRoom, frame = string - kFrame;
	const size_t count = std::min(text.size(), size_t(kRoom / 2 - 1));
	for (size_t i = 0; i <= count; ++i) st16(string + uint32_t(i) * 2, i < count ? (uint16_t)text[i] : 0);
	PPCContext call = c;
	st32(frame, stack);
	st32(frame + 8, ld32(stack + 8)); //the ninth argument
	call.r[1] = frame;
	call.r[5] = widget;
	call.r[6] = 0xFFFFFFFF;
	call.r[7] = string;
	if (widget != c.r[5]) { call.r[8] = kRowStyle; call.r[9] = kRowFont; }
	call.r[10] = align;
	orig_page_text(call);
}

std::u16string Wide(const std::string& text) { return std::u16string(text.begin(), text.end()); }

//(the widgets) the rows' last, 0 for none
uint32_t LastRow(uint32_t widgets)
{
	uint32_t last = 0;
	for (uint32_t row = kPageRow; row < kPageRow + kPageRows; ++row)
		if (ld8(Widget(widgets, row))) last = row;
	return last;
}

//the level's line ends with the difficulty's name: 007 Custom's instead
std::u16string LevelLine(const PPCContext& c)
{
	std::u16string line;
	for (uint32_t at = c.r[7]; guest_ptr(at) && line.size() < 0x100; at += 2)
	{
		const uint16_t ch = ld16(at);
		if (!ch) break;
		line += char16_t(ch);
	}
	const std::u16string difficulty = Wide(game_text(c, ld32(kDifficultyTexts + ld16(c.r[3] + kPageDifficulty) * 4)));
	if (difficulty.empty() || line.size() < difficulty.size() || line.compare(line.size() - difficulty.size(), difficulty.size(), difficulty) != 0) return line;
	return line.substr(0, line.size() - difficulty.size()) + u"007 Custom";
}

void PageText(PPCContext& c)
{
	if (!g_page_drawing || !Active()) { orig_page_text(c); return; }
	const PPCContext call = c; //as it came: the call returns with its registers changed
	const uint32_t widget = c.r[5];
	if (g_trace && widget == kPageTitle && !g_listed && (g_listed = true))
		for (uint32_t i = 0; i < kWidgetCount; ++i)
		{
			const uint32_t w = c.r[4] + kWidgets + i * kWidgetSize;
			if (ld8(w)) rt_log("goldeneye: 007 Custom: the page's widget %08X at %g %g to %g %g", kFirstWidget + i, ldf(w + 4), ldf(w + 8), ldf(w + 0xC), ldf(w + 0x10));
		}
	if (widget == kPageLevel && c.r[7] && !g_page_settings) { PageString(call, widget, LevelLine(call), (uint32_t)c.r[10]); return; }
	if (widget == kPageOutcome)
	{
		//the way between the two in place of what the mission unlocked
		const std::u16string keys = {kLeftGlyph, u'/', kRightGlyph, u' '};
		PageString(call, widget, keys + (g_page_settings ? u"Mission Results" : u"007 Custom Settings"), (uint32_t)c.r[10]);
		if (!g_page_settings) return;
	}
	else if (!g_page_settings) { orig_page_text(c); return; }
	if (widget >= kPageLevel && widget < kPageRow + kPageRows) return; //the level's line and the results' rows, the settings are in them
	if (widget == kPageTitle) { PageString(call, widget, u"007 Custom Settings", (uint32_t)c.r[10]); return; }
	if (widget != kPageOutcome) { orig_page_text(c); return; }
	//a row each from the level's line down (the layout's stripes are drawn under those six), the name on the
	//left and the value on the right
	const uint32_t last = LastRow(c.r[4]);
	for (int i = 0; i < kCustomShown && kPageLevel + i <= last; ++i)
	{
		PageString(call, kPageLevel + i, Wide(custom_shown_name(i, false)), kLeft);
		PageString(call, kPageLevel + i, Wide(custom_shown_value(i)), kRight);
	}
}

} //namespace

//once a frame: the page with the sliders went, nothing is held any more
void custom_frame(uint32_t frame)
{
	if (!PageUp()) g_page_settings = false; //a page comes up with the results
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
	//on the Mission Complete page left and right switch between the results and the settings
	if (!PageUp()) { g_page_buttons = 0; return; }
	const uint32_t sides = s.buttons & (kLeftButton | kRightButton);
	if (sides & ~g_page_buttons) g_page_settings = !g_page_settings;
	g_page_buttons = sides;
	s.buttons &= ~(kLeftButton | kRightButton);
}

void custom_page_drawing(bool on)
{
	g_page_drawing = on;
	if (on) g_page_frame = ld32(kFrameCount);
}

std::string custom_shown_name(int i, bool brief)
{
	static const char* const kBrief[kCustomSliders] = {"Health", "Damage", "Reactions", "Protection", "Accuracy"};
	if (i == kCustomSliders) return brief ? "Headshots" : "Instant Kill Headshots";
	return brief ? kBrief[i] : kSliders[i].label;
}

std::string custom_shown_value(int i)
{
	if (i == kCustomSliders) return g_headshots ? "On" : "Off";
	return std::to_string(g_value[i].load()) + "%";
}

bool custom_showing()
{
	//not while a level loads: there's a player from step 0x15 on, behind the loading screen
	const uint32_t kAppPtr = 0x806EB694, kLoadStep = 0x354, kStepPlaying = 0x19;
	const uint32_t app = ld32(kAppPtr);
	return Active() && hud() && hud_drawn() && in_play() && guest_ptr(app) && ld32(app + kLoadStep) == kStepPlaying;
}

bool custom_mode() { return g_on; }

void set_custom_mode(bool on)
{
	if (g_always) return;
	const bool was = g_on.exchange(on);
	if (on)
	{
		//a mission is started with them: kept for the next time the game runs
		rt_log("goldeneye: 007 Custom: enemy health %d%%, damage %d%%, reaction speed %d%%, hit protection %d%%, accuracy %d%%, headshots that kill %s", g_value[Health].load(), g_value[Damage].load(), g_value[Reaction].load(), g_value[Protection].load(), g_value[Accuracy].load(), g_headshots ? "on" : "off");
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
	//the switch before accuracy: a file from before accuracy was there still reads
	char text[64];
	std::snprintf(text, sizeof text, "%d,%d,%d,%d,%d,%d", g_value[Health].load(), g_value[Damage].load(), g_value[Reaction].load(), g_value[Protection].load(), g_headshots ? 1 : 0, g_value[Accuracy].load());
	return text;
}

void install_custom()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	//GE_CUSTOM=health,damage,reaction,protection[,headshots that kill[,accuracy]]
	const std::string saved = saved_setting("GE_CUSTOM", "LAUNCH_CUSTOM");
	int values[kCustomSliders] = {kNormal, kNormal, kNormal, kNormal, kNormal}, headshots = 1;
	if (std::sscanf(saved.c_str(), "%d,%d,%d,%d,%d,%d", &values[Health], &values[Damage], &values[Reaction], &values[Protection], &headshots, &values[Accuracy]) >= 4)
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
	orig_accuracy = ppc_hook("ge_guard_accuracy", GuardAccuracy);
	orig_page_text = ppc_hook("ge_page_text", PageText);
	orig_npc_fire = ppc_hook("ge_npc_fire_bullet", NpcFire);
	g_tracehit = std::getenv("GE_TRACEHIT") != nullptr;
	if (!orig_bullet || !orig_shot || !orig_fight_wait || !orig_protection || !orig_melee || !orig_fire_wait || !orig_setting_get || !orig_setting_step || !orig_accuracy || !orig_npc_fire)
		rt_log("goldeneye: 007 Custom: a function of it isn't in this build, some of it won't work");
}

} //namespace ge
