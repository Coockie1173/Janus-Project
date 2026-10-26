//small options: the level timer, the unused extras menu, unlock everything (all three were
//gecko codes), auto start, no tutorials and the online unlocks
//
//level timer (on by default): the hud only shows a clock in a time challenge, counting down
//the code [Mars] patches four instructions of the hud's update to always show the level's time
//recompiled code can't be patched while it runs, so we feed the update what makes it show
//the same thing: the flag set and a limit of twice the time (limit - time = time)
//
//unused extras menu (off by default): Extras also lists Mission Select, Character
//Viewer, Movies and the Calibration Menu. the Extras page gets other rows (port/modmenu.cpp)
//
//unlock everything (off by default): every mission and every difficulty can be picked
//the two codes patch a menu callback (8024BB80) and the progress getter (80230154) to store
//-1 into the progress before it is read: the missions' mask (+0x38) and the four masks of
//missions done per difficulty (+0x40). the same five words are kept at -1 each frame here
//
//auto start (off by default): a loaded mission shows its name over "Continue" and waits for
//a button. that is the last step of the level's load (0x1A), asking ge_pad_confirmed each
//frame if the first pad pressed select. while the load is in that step the answer is yes
//
//no tutorials (off by default): a level's triggers ask the hud for a tutorial box by its
//hash (message 0x22 -> ge_show_tutorial), and the hud shows it, most of them with the game
//paused until a button. nothing reads the answer or waits for the box, so with the option on
//the hud is never asked: the box, its pause and the boxes chained after it don't happen,
//in the training mission too (its boxes say what to do next, the player asked for them gone)
//
//online unlocks (on by default): a loadout's weapons, attachments and gadgets open at a level,
//and so do three game modes
//the menu asks ge_unlock_level for an item's level (a table of hash and level, 0x10 a row at
//805D1908) and locks it above the profile's (+0x434). the answer is 0 here, so they are all
//open, but for the Proximity Mine (level 53, as it was). Choose Game Mode locks License to
//Kill, Team License to Kill and Classic Conflict under level 25, 30 and 35 by itself, their
//rows' bytes are cleared after the menu's update
//
//hud (on by default): the hud's draw (ge_hud_update) hides nearly all of itself when the level's
//flags (+0x198 of the object 805D5474 points to) have bit 4, which the game sets for a
//cutscene: the elements' shown bits and the texts' shown bytes are cleared before they are
//drawn. with the option off the bit is set for that call and put back after, so nothing else
//the bit means happens
//that leaves what the draw shows whatever the bit says (the tank's reticle and its targets'
//names), so for the same call the two functions everything of the hud is drawn with,
//ge_hud_element_draw and ge_hud_text_draw, do nothing either. the fades the hud draws over
//the picture are neither and stay
//the player's own view draws two more things over the world itself: the objectives' marks
//with their distances (ge_waypoints_draw) and the names over people (the function the
//symbols call Radar_Enemy_Name). those are left out as long as the hud's last draw was
//the hud is drawn all the same while a tutorial box is up (the game stands still until its
//button) and while a quick time event waits for a button
//
//while a cheat is on (cheats_active) the level timer and the ammunition count flash red: their
//texts' colour (+0x24) is red for the draw, every other half second, and the game's after it
//while bunnyhopping by rules that aren't the defaults (bhop_custom_now) the level timer flashes
//a lighter grey the same way, so a run on other rules is told apart without getting in the way
//
//GE_HUD=0 no hud
//GE_LEVEL_TIMER=0 no level timer
//GE_ONLINE_UNLOCKS=0 the loadouts' items and the game modes open at their levels
//GE_AUTO_START=1 a loaded mission starts by itself
//GE_EXTRAS_MENU=1 the unused extras menu
//GE_UNLOCK_ALL=1 every mission and difficulty unlocked
//GE_NO_TUTORIALS=1 no tutorial boxes
//GE_TRACE=1 log every tutorial that is asked for
#include "ge.h"
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <string>

namespace ge
{
namespace
{

const uint32_t kDisplayLevelTimer = 0x805E05AB; //a byte, the hud shows the clock (set in a time challenge)
const uint32_t kMaxTimeChallenge = 0x805E05AC; //a float, the challenge's limit. the clock shows this minus the time
const uint32_t kLevelTimer = 0x805C963C; //a float, seconds played in the level

//the code "Unused extras menu" (04774704 80580990, 0077470B 00000019) wrote the Extras page's
//rows where the menu's pages were on the heap. modmenu.cpp finds the page and does it now
struct MenuWrite { uint32_t addr, size, value, game; };

//the codes "Unlock all difficulties" (C224BB80) and "Unlock all missions" (C2230154)
const uint32_t kProgress = 0x805DE6A0; //the profile's progress
const uint32_t kAll = 0xFFFFFFFF;
MenuWrite g_unlock_writes[] = {
	{kProgress + 0x38, 4, kAll, 0}, //a bit a mission, it can be picked
	{kProgress + 0x40, 4, kAll, 0}, {kProgress + 0x44, 4, kAll, 0}, //a mask a difficulty, a bit a mission done on it
	{kProgress + 0x48, 4, kAll, 0}, {kProgress + 0x4C, 4, kAll, 0}};

std::atomic<bool> g_level_timer{true};
std::atomic<bool> g_hud{true};
const uint32_t kLevelFlagsPtr = 0x805D5474, kLevelFlags = 0x198, kFlagNoHud = 4;
const uint32_t kHudTutorialUp = 0x2523; //a byte of the hud, a tutorial box is shown
const uint32_t kTextSize = 0x58, kTextColour = 0x24; //a hud text, its colour (RGBA)
const uint32_t kTimerText = 0x1810; //the hud's text of the level timer
const uint32_t kAmmoText = 0x13E8, kAmmoTexts = 4; //the ammunition count's texts (fn_8018F250 fills them)
const uint32_t kCheatRed = 0x80101000; //the hud's colours go to 0x80
const uint32_t kCustomGrey = 0x60606000;
bool g_hud_hidden = false; //inside the hud's draw with the option off
bool g_hud_was_hidden = false; //the hud's last draw was one
PPCFunc orig_element_draw = nullptr, orig_text_draw = nullptr, orig_waypoints_draw = nullptr, orig_names_draw = nullptr;
std::atomic<bool> g_extras_menu{false};
std::atomic<bool> g_unlock_all{false};
std::atomic<bool> g_auto_start{false};
std::atomic<bool> g_no_tutorials{false};
const uint32_t kCurrentLevel = 0x805D1804; //the hash of the level that is loaded
std::atomic<bool> g_online_unlocks{true};
const uint32_t kProximityMine = 0x7D060005; //the gadget's hash (the loadout items' table at 80585C80)
const uint32_t kMenuPage = 0x214, kMenuRowLocked = 0x548; //the menu's page, a byte a row: it is locked
const uint32_t kGameModesPage = 0x7D000047; //Choose Game Mode
const uint32_t kFirstLevelRow = 6, kLevelRows = 3; //the game modes that open at level 25, 30 and 35
bool g_trace = false;
const uint32_t kAppPtr = 0x806EB694, kLoadStep = 0x354; //the step the level's load is at
const uint32_t kStepContinue = 0x1A; //waiting for a button under the mission's name
bool g_unlock_written = false; //the progress does
PPCFunc orig_hud_update = nullptr;
PPCFunc orig_pad_confirmed = nullptr;
PPCFunc orig_show_tutorial = nullptr;
PPCFunc orig_unlock_level = nullptr;
PPCFunc orig_menu_locks = nullptr;

uint32_t Load(const MenuWrite& w) { return w.size == 1 ? ld8(w.addr) : ld32(w.addr); }
void Store(const MenuWrite& w, uint32_t v)
{
	if (w.size == 1) st8(w.addr, (uint8_t)v);
	else st32(w.addr, v);
}

//keeps a code's values in memory while it is on, the game's own come back when it is turned off
template <size_t N>
void Keep(MenuWrite (&writes)[N], bool on, bool& written)
{
	if (on)
	{
		for (auto& w : writes)
		{
			uint32_t have = Load(w);
			if (have == w.value) continue;
			w.game = have; //remember what the game had there for when it gets turned off
			Store(w, w.value);
		}
		written = true;
	}
	else if (written)
	{
		for (auto& w : writes)
			if (Load(w) == w.value) Store(w, w.game);
		written = false;
	}
}

void HudDraw(PPCContext& c)
{
	if (!g_level_timer)
	{
		orig_hud_update(c);
		return;
	}
	uint8_t shown = ld8(kDisplayLevelTimer);
	uint32_t limit = ld32(kMaxTimeChallenge);
	st8(kDisplayLevelTimer, 1);
	stf(kMaxTimeChallenge, ldf(kLevelTimer) * 2.0f);
	orig_hud_update(c);
	st8(kDisplayLevelTimer, shown);
	st32(kMaxTimeChallenge, limit);
}

void HudUpdate(PPCContext& c)
{
	uint32_t hud = c.r[3], level = ld32(kLevelFlagsPtr);
	if (g_hud || !guest_ptr(hud) || ld8(hud + kHudTutorialUp) || cutscene_input_waiting())
	{
		g_hud_was_hidden = false;
		HudDraw(c);
		return;
	}
	g_hud_was_hidden = true;
	bool set = guest_ptr(level) && !(ld32(level + kLevelFlags) & kFlagNoHud);
	if (set) st32(level + kLevelFlags, ld32(level + kLevelFlags) | kFlagNoHud);
	g_hud_hidden = true;
	HudDraw(c);
	g_hud_hidden = false;
	if (set && ld32(kLevelFlagsPtr) == level) st32(level + kLevelFlags, ld32(level + kLevelFlags) & ~kFlagNoHud);
}

void HudElementDraw(PPCContext& c)
{
	if (!g_hud_hidden) orig_element_draw(c);
}

//the level timer or a part of the ammunition count
bool CheatText(uint32_t text, uint32_t hud)
{
	uint32_t at = text - hud;
	return at == kTimerText || (at >= kAmmoText && at < kAmmoText + kAmmoTexts * kTextSize && (at - kAmmoText) % kTextSize == 0);
}

void HudTextDraw(PPCContext& c)
{
	if (g_hud_hidden) return;
	uint32_t text = c.r[3];
	const bool cheat{ cheats_active() && CheatText(text, c.r[4]) };
	const bool custom{ !cheat && text - c.r[4] == kTimerText && bhop_custom_now() };
	if (!cheat && !custom)
	{
		orig_text_draw(c);
		return;
	}
	//red (grey) for half of each second, the game's own colour for the other half
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	uint32_t colour = ld32(text + kTextColour);
	if (ms % 1000 < 500) st32(text + kTextColour, (cheat ? kCheatRed : kCustomGrey) | (colour & 0xFF));
	orig_text_draw(c);
	st32(text + kTextColour, colour);
}

void WaypointsDraw(PPCContext& c)
{
	if (g_hud || !g_hud_was_hidden) orig_waypoints_draw(c);
}

void NamesDraw(PPCContext& c)
{
	if (g_hud || !g_hud_was_hidden) orig_names_draw(c);
}

//(front end, pad) -> the pad pressed select
void PadConfirmed(PPCContext& c)
{
	orig_pad_confirmed(c);
	uint32_t app = ld32(kAppPtr);
	if (g_auto_start && guest_ptr(app) && ld32(app + kLoadStep) == kStepContinue) c.r[3] = 1;
}

//(hud, the tutorial's hash) -> it is shown
void ShowTutorial(PPCContext& c)
{
	uint32_t level = ld32(kCurrentLevel);
	bool skip = g_no_tutorials;
	if (g_trace || skip) rt_log("goldeneye: tutorial %08X of level %08X%s", c.r[4], level, skip ? ", not shown" : "");
	if (!skip)
	{
		orig_show_tutorial(c);
		return;
	}
	c.r[3] = 0;
}

//(the item's hash) -> the level before the one it opens at, 0 when it has none
void UnlockLevel(PPCContext& c)
{
	if (g_online_unlocks && c.r[3] != kProximityMine)
	{
		c.r[3] = 0;
		return;
	}
	orig_unlock_level(c);
}

//(menu): sets the locked rows of the page it is on
void MenuLocks(PPCContext& c)
{
	uint32_t menu = c.r[3];
	orig_menu_locks(c);
	if (!g_online_unlocks || ld32(menu + kMenuPage) != kGameModesPage) return;
	for (uint32_t row = kFirstLevelRow; row < kFirstLevelRow + kLevelRows; ++row) st8(menu + kMenuRowLocked + row, 0);
}

} //namespace

bool level_timer() { return g_level_timer; }
void set_level_timer(bool on) { g_level_timer = on; }
bool hud() { return g_hud; }
void set_hud(bool on) { g_hud = on; }
bool extras_menu() { return g_extras_menu; }
void set_extras_menu(bool on) { g_extras_menu = on; }
bool unlock_all() { return g_unlock_all; }
void set_unlock_all(bool on) { g_unlock_all = on; }
bool auto_start() { return g_auto_start; }
void set_auto_start(bool on) { g_auto_start = on; }
bool no_tutorials() { return g_no_tutorials; }
void set_no_tutorials(bool on) { g_no_tutorials = on; }
bool online_unlocks() { return g_online_unlocks; }
void set_online_unlocks(bool on) { g_online_unlocks = on; }

void tweaks_apply()
{
	Keep(g_unlock_writes, g_unlock_all, g_unlock_written);
}

void install_tweaks()
{
	if (const char* e = std::getenv("GE_LEVEL_TIMER")) g_level_timer = std::atoi(e) != 0;
	if (std::string saved = saved_setting("GE_HUD", "LAUNCH_HUD"); !saved.empty()) g_hud = std::atoi(saved.c_str()) != 0;
	if (const char* e = std::getenv("GE_EXTRAS_MENU")) g_extras_menu = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_UNLOCK_ALL")) g_unlock_all = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_AUTO_START")) g_auto_start = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_NO_TUTORIALS")) g_no_tutorials = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_ONLINE_UNLOCKS")) g_online_unlocks = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_hud_update = ppc_hook("ge_hud_update", HudUpdate);
	if (!orig_hud_update) rt_die("goldeneye: the hud's update is not hooked in this build");
	orig_element_draw = ppc_hook("ge_hud_element_draw", HudElementDraw);
	orig_text_draw = ppc_hook("ge_hud_text_draw", HudTextDraw);
	orig_waypoints_draw = ppc_hook("ge_waypoints_draw", WaypointsDraw);
	orig_names_draw = ppc_hook("Radar_Enemy_Name_FUN_8016b9b0", NamesDraw);
	if (!orig_element_draw || !orig_text_draw || !orig_waypoints_draw || !orig_names_draw) rt_log("goldeneye: the hud's draws are not hooked in this build, the HUD option leaves some of it");
	orig_pad_confirmed = ppc_hook("ge_pad_confirmed", PadConfirmed);
	if (!orig_pad_confirmed) rt_die("goldeneye: the loading screen's button is not hooked in this build");
	orig_show_tutorial = ppc_hook("ge_show_tutorial", ShowTutorial);
	if (!orig_show_tutorial) rt_die("goldeneye: the tutorials are not hooked in this build");
	orig_unlock_level = ppc_hook("ge_unlock_level", UnlockLevel);
	if (!orig_unlock_level) rt_die("goldeneye: the unlock levels are not hooked in this build");
	orig_menu_locks = ppc_hook("ge_menu_locks", MenuLocks);
	if (!orig_menu_locks) rt_die("goldeneye: the menu's locks are not hooked in this build");
}

} //namespace ge
