// GoldenEye 007 — two Gecko codes as options of the port: the level timer and the unused extras menu.
//
// Level timer (an enhancement, on by default): the hud shows a clock only in a time
// challenge, counting down from its limit. The code [Mars] makes it show the level's
// own time, always, by changing four instructions of the hud's update. Recompiled code
// cannot be changed while it runs, so the update is given what makes the game's own
// code show the same: the flag set, and a limit of twice the time (limit - time = time),
// for as long as the call lasts.
//
// Unused extras menu (a cheat, off by default): Extras lists Mission Select, Character
// Viewer, Movies and the Calibration Menu too. Two writes into the menu's data, kept
// up each frame; what the game had there is put back when it is turned off.
//
//     GE_LEVEL_TIMER=0   no level timer (the F1 menu's Enhancements has it too)
//     GE_EXTRAS_MENU=1   the unused extras menu (the F1 menu's Cheats has it too)
#include "ge.h"
#include <atomic>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kDisplayLevelTimer = 0x805E05AB;      //a byte: the hud shows the clock (a time challenge)
const uint32_t kMaxTimeChallenge = 0x805E05AC;       //a float: the challenge's limit, the clock is this less the time
const uint32_t kLevelTimer = 0x805C963C;             //a float: seconds played in the level

//the code "Unused extras menu": 04774704 80580990, 0077470B 00000019
struct MenuWrite { uint32_t addr, size, value, game; };
MenuWrite g_extras_writes[] = {{0x80774704, 4, 0x80580990, 0}, {0x8077470B, 1, 0x19, 0}};

std::atomic<bool> g_level_timer{true};
std::atomic<bool> g_extras_menu{false};
bool g_extras_written = false;                       //the menu's data holds the code's values
PPCFunc orig_hud_update = nullptr;

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

}  // namespace

bool level_timer() { return g_level_timer; }
void set_level_timer(bool on) { g_level_timer = on; }
bool extras_menu() { return g_extras_menu; }
void set_extras_menu(bool on) { g_extras_menu = on; }

void tweaks_apply()
{
	if (g_extras_menu)
	{
		for (auto& w : g_extras_writes)
		{
			uint32_t have = Load(w);
			if (have == w.value) continue;
			w.game = have;                           //what the game itself put there: for when it is turned off
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
	orig_hud_update = ppc_hook("ge_hud_update", HudUpdate);
	if (!orig_hud_update) rt_die("goldeneye: the hud's update is not hooked in this build");
}

}  // namespace ge
