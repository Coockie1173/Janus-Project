//skip cutscenes: a mission starts at the checkpoint after its opening
//
//a mission picked in the menu loads at entrance 1. checkpoints are entrances too, and which
//one the opening ends at differs per level (2 on Dam, 10 on Severnaya)
//so they live in config/checkpoints.txt, a level that isn't in there starts like normal
//Restart Mission loads the level through the same code with the entrance in the app's
//object, so that one gets changed too
//
//GE_SKIP_CUTSCENES=1 on
//GE_ENTRANCE=N start every mission at entrance N instead, for trying out a level's entrances
//GE_TRACE=1 log the entrance a mission starts at and every checkpoint
#include "ge.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#ifndef GE_ROOT
#define GE_ROOT "."
#endif

namespace ge
{
namespace
{

//the menu's object, as ge_menu_pending takes it in r3
const uint32_t kPendingReady = 0x7E9; //a byte, the pending action runs once this is over 2
const uint32_t kPendingAction = 0x7EC; //5: load the pending level
const uint32_t kPendingSplit = 0x7E7; //a byte, split screen goes its own way
const uint32_t kPendingLevel = 0x7D4; //the level's hash
const uint32_t kPendingEntrance = 0x7DC; //where in the level the player starts
const uint32_t kMenuPage = 0x220, kMenuPages = 0x2C8;
const uint32_t kActionRestartLevel = 3, kActionLoadLevel = 5;
//the item picked on the page the level starts from, 0x13..0x16 are the campaign's difficulties
const uint16_t kItemOperative = 0x13, kItemClassic = 0x16;
const uint32_t kMissionStart = 1; //the entrance a mission begins at
const uint32_t kCurrentLevel = 0x805D1804; //the hash of the level that is loaded
const uint32_t kAppPtr = 0x806EB694, kAppEntrance = 0x338; //the entrance StartLevelChange loads the level at

std::atomic<bool> g_skip_cutscenes{false};
PPCFunc orig_menu_pending = nullptr;
PPCFunc orig_record_restart = nullptr;
PPCFunc orig_level_change = nullptr;
bool g_restarting = false; //the menu's restart action is running
bool g_trace = false;
uint32_t g_forced_entrance = 0; //GE_ENTRANCE
std::map<uint32_t, uint32_t> g_checkpoints; //level hash -> the entrance its opening ends at

//lines of "level hash (hex), entrance"
void LoadCheckpoints(const std::string& path)
{
	std::ifstream f(path);
	for (std::string l; std::getline(f, l);)
	{
		unsigned level = 0, entrance = 0;
		if (std::sscanf(l.c_str(), "%x %u", &level, &entrance) == 2) g_checkpoints[level] = entrance;
	}
}

bool StartsCampaignLevel(uint32_t menu)
{
	if (ld8(menu + kPendingReady) <= 2 || ld32(menu + kPendingAction) != kActionLoadLevel) return false;
	if (ld8(menu + kPendingSplit) != 0) return false;
	uint32_t page = ld32(menu + kMenuPages + ld32(menu + kMenuPage) * 4);
	if (!guest_ptr(page)) return false;
	uint16_t item = ld16(page + 0xC);
	return item >= kItemOperative && item <= kItemClassic;
}

//the entrance a mission's opening ends at, 0 if we don't know it
uint32_t FirstCheckpoint(uint32_t level)
{
	if (g_forced_entrance) return g_forced_entrance;
	auto known = g_checkpoints.find(level);
	return known != g_checkpoints.end() ? known->second : 0;
}

void MenuPending(PPCContext& c)
{
	uint32_t menu = c.r[3];
	g_restarting = ld8(menu + kPendingReady) > 2 && ld32(menu + kPendingAction) == kActionRestartLevel;
	if (StartsCampaignLevel(menu) && ld32(menu + kPendingEntrance) == kMissionStart)
	{
		uint32_t level = ld32(menu + kPendingLevel);
		uint32_t entrance = FirstCheckpoint(level);
		if (g_skip_cutscenes && entrance)
		{
			st32(menu + kPendingEntrance, entrance);
			rt_log("goldeneye: skip cutscenes: level %08X starts at entrance %u", level, entrance);
		}
		else if (g_trace) rt_log("goldeneye: level %08X starts at entrance %u", level, kMissionStart);
	}
	orig_menu_pending(c);
	g_restarting = false;
}

//(app, level hash, ...) the restart action just put the mission's start in the app's entrance
void LevelChange(PPCContext& c)
{
	uint32_t app = ld32(kAppPtr), level = c.r[4];
	if (g_restarting && g_skip_cutscenes && guest_ptr(app) && ld32(app + kAppEntrance) <= kMissionStart)
	{
		if (uint32_t entrance = FirstCheckpoint(level))
		{
			st32(app + kAppEntrance, entrance);
			rt_log("goldeneye: skip cutscenes: level %08X restarts at entrance %u", level, entrance);
		}
	}
	orig_level_change(c);
}

void RecordRestart(PPCContext& c)
{
	rt_log("goldeneye: checkpoint: level %08X entrance %d", ld32(kCurrentLevel), (int32_t)c.r[4]);
	orig_record_restart(c);
}

} //namespace

bool skip_cutscenes() { return g_skip_cutscenes; }
void set_skip_cutscenes(bool on) { g_skip_cutscenes = on; }

void install_cutscenes()
{
	if (const char* e = std::getenv("GE_SKIP_CUTSCENES")) g_skip_cutscenes = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_ENTRANCE")) g_forced_entrance = (uint32_t)std::atoi(e);
	g_trace = std::getenv("GE_TRACE") != nullptr;
	LoadCheckpoints(std::string(GE_ROOT) + "/config/checkpoints.txt");
	orig_menu_pending = ppc_hook("ge_menu_pending", MenuPending);
	orig_level_change = ppc_hook("StartLevelChange", LevelChange);
	if (!orig_menu_pending || !orig_level_change) rt_die("goldeneye: the menu's pending action is not hooked in this build");
	if (g_trace) orig_record_restart = ppc_hook("ge_record_restart", RecordRestart);
}

} //namespace ge
