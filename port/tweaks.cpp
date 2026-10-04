//small options: the level timer, the unused extras menu (both were gecko codes), auto start
//and no tutorials
//
//level timer (on by default): the hud only shows a clock in a time challenge, counting down
//the code [Mars] patches four instructions of the hud's update to always show the level's time
//recompiled code can't be patched while it runs, so we feed the update what makes it show
//the same thing: the flag set and a limit of twice the time (limit - time = time)
//
//unused extras menu (a cheat, off by default): Extras also lists Mission Select, Character
//Viewer, Movies and the Calibration Menu. two writes into the menu's data, kept up each frame
//
//auto start (off by default): a loaded mission shows its name over "Continue" and waits for
//a button. that is the last step of the level's load (0x1A), asking ge_pad_confirmed each
//frame if the first pad pressed select. while the load is in that step the answer is yes
//
//no tutorials (off by default): a level's triggers ask the hud for a tutorial box by its
//hash (message 0x22 -> ge_show_tutorial), and the hud shows it, most of them with the game
//paused until a button. nothing reads the answer or waits for the box, so with the option on
//the hud is never asked: the box, its pause and the boxes chained after it don't happen
//
//GE_LEVEL_TIMER=0 no level timer
//GE_AUTO_START=1 a loaded mission starts by itself
//GE_EXTRAS_MENU=1 the unused extras menu
//GE_NO_TUTORIALS=1 no tutorial boxes
//GE_TRACE=1 log every tutorial that is asked for
#include "ge.h"
#include <atomic>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kDisplayLevelTimer = 0x805E05AB; //a byte, the hud shows the clock (set in a time challenge)
const uint32_t kMaxTimeChallenge = 0x805E05AC; //a float, the challenge's limit. the clock shows this minus the time
const uint32_t kLevelTimer = 0x805C963C; //a float, seconds played in the level

//the code "Unused extras menu": 04774704 80580990, 0077470B 00000019
struct MenuWrite { uint32_t addr, size, value, game; };
MenuWrite g_extras_writes[] = {{0x80774704, 4, 0x80580990, 0}, {0x8077470B, 1, 0x19, 0}};

std::atomic<bool> g_level_timer{true};
std::atomic<bool> g_extras_menu{false};
std::atomic<bool> g_auto_start{false};
std::atomic<bool> g_no_tutorials{false};
bool g_trace = false;
const uint32_t kAppPtr = 0x806EB694, kLoadStep = 0x354; //the step the level's load is at
const uint32_t kStepContinue = 0x1A; //waiting for a button under the mission's name
bool g_extras_written = false; //the menu's data holds the code's values
PPCFunc orig_hud_update = nullptr;
PPCFunc orig_pad_confirmed = nullptr;
PPCFunc orig_show_tutorial = nullptr;

uint32_t Load(const MenuWrite& w) { return w.size == 1 ? ld8(w.addr) : ld32(w.addr); }
void Store(const MenuWrite& w, uint32_t v)
{
	if (w.size == 1) st8(w.addr, (uint8_t)v);
	else st32(w.addr, v);
}

void HudUpdate(PPCContext& c)
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
	bool skip = g_no_tutorials;
	if (g_trace || skip) rt_log("goldeneye: tutorial %08X%s", c.r[4], skip ? ", not shown" : "");
	if (!skip)
	{
		orig_show_tutorial(c);
		return;
	}
	c.r[3] = 0;
}

} //namespace

bool level_timer() { return g_level_timer; }
void set_level_timer(bool on) { g_level_timer = on; }
bool extras_menu() { return g_extras_menu; }
void set_extras_menu(bool on) { g_extras_menu = on; }
bool auto_start() { return g_auto_start; }
void set_auto_start(bool on) { g_auto_start = on; }
bool no_tutorials() { return g_no_tutorials; }
void set_no_tutorials(bool on) { g_no_tutorials = on; }

void tweaks_apply()
{
	if (g_extras_menu)
	{
		for (auto& w : g_extras_writes)
		{
			uint32_t have = Load(w);
			if (have == w.value) continue;
			w.game = have; //remember what the game had there for when it gets turned off
			Store(w, w.value);
		}
		g_extras_written = true;
	}
	else if (g_extras_written)
	{
		for (auto& w : g_extras_writes)
			if (Load(w) == w.value) Store(w, w.game);
		g_extras_written = false;
	}
}

void install_tweaks()
{
	if (const char* e = std::getenv("GE_LEVEL_TIMER")) g_level_timer = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_EXTRAS_MENU")) g_extras_menu = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_AUTO_START")) g_auto_start = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_NO_TUTORIALS")) g_no_tutorials = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_hud_update = ppc_hook("ge_hud_update", HudUpdate);
	if (!orig_hud_update) rt_die("goldeneye: the hud's update is not hooked in this build");
	orig_pad_confirmed = ppc_hook("ge_pad_confirmed", PadConfirmed);
	if (!orig_pad_confirmed) rt_die("goldeneye: the loading screen's button is not hooked in this build");
	orig_show_tutorial = ppc_hook("ge_show_tutorial", ShowTutorial);
	if (!orig_show_tutorial) rt_die("goldeneye: the tutorials are not hooked in this build");
}

} //namespace ge
