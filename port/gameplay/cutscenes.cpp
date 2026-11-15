//skip cutscenes: a mission's opening cutscene is played through in about a second
//
//the game has no way past a cutscene, and starting at the checkpoint after it (what this did
//first, and what Station's own debug switch does: entrance 666) isn't the same level, scripts
//that ran in the opening never run. so the opening does play, in very long steps: the engine's
//time step comes from its frame rate (ge_frame_tick), which is held at 3 frames a second, and
//nothing waits for a frame: the runtime's clock jumps to where the next one is due
//(rt_advance), so timers and the disc see the same time go by. the sound is off, and every
//jump waits for the audio to mix what fell due, so no cutscene sound is left over after it
//most frames aren't drawn: ge_frame_draw (802BBE70) leaves the scene out when its r3 is 1,
//as the game does it under a loading screen, and the runtime drops what is still sent
//
//a mission picked in the menu loads at entrance 1 (ge_menu_pending), Restart Mission puts the
//same in the app's object and calls StartLevelChange. after either this waits for the level's
//time to count (it stands still until the loading screen's "Continue" is pressed), then for
//the first cutscene that takes the player (state 0x22 with the view and the aim locked) and
//runs until the player gets anything back: the view, the aim, a button to press
//
//the skip is behind a fade: the picture and the sound go out over half a second with the game
//at its normal speed, the long steps run under the black, and the fade back in is at normal
//speed again. config/openings.txt has the level time each opening ends at, so the long steps
//stop half a second before it and the picture is back when the cutscene ends. a level that
//isn't in there fades in after its cutscene
//Tank's opening never takes the player: the tank (or Bond, on foot with the bunnyhop) is bound to
//it as an actor, the view and the aim stay his. its row says "any": the opening is the time a
//cutscene plays at all (the byte at 806EB6C8), it's waited for and skipped while that is set
//
//which cutscenes are skipped is a choice for each: a mission's intro (config/openings.txt, which
//names the missions), a cutscene in its middle (Nightclub's meeting with Zukovsky, Carrier's with
//Sky Briggs) and its ending (config/cutscenes.txt). an ending's long steps run until the game
//changes to the next level, the player isn't let go before it; in a level with quick time events
//in its ending that is the cutscene after the last of them. the settings menu's Skip Cutscenes grid
//GE_SKIP_LIST=dam-intro,nightclub-ending,... the ones skipped, the others aren't; "all", "none",
//"intros", "middles" and "endings" stand for those
//without it: GE_SKIP_CUTSCENES=1 every intro, GE_SKIP_ZUKOVSKY=1 and GE_SKIP_BRIGGS=1 the two
//middles, GE_SKIP_ENDINGS=1 every ending
//GE_SKIP_RATE=N the frames a second of game time while skipping (3)
//GE_ENTRANCE=N start every mission at entrance N instead of 1, for trying out a level's
//entrances (0 is one too). nothing is skipped then
//GE_TRACE=1 log every checkpoint
//GE_TRACECUT=1 log what a cutscene's timeline tells its player
//always logged ("cut: t ..."): a cutscene taking the player and letting go, the mouse lost to a
//cutscene and given back, quick time events (port/gameplay/qte.cpp) and level changes, with the level's time
#include "ge.h"
#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

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
const uint32_t kActionRestartLevel = 3, kActionLoadLevel = 5, kActionContinue = 6; //6: the main menu's Continue Game
//the item picked on the page the level starts from, 0x13..0x16 are the campaign's difficulties, 0x17 its Time Trial
const uint16_t kItemOperative = 0x13, kItemTimeTrial = 0x17;
const uint32_t kMissionStart = 1; //the entrance a mission begins at
const uint32_t kCurrentLevel = 0x805D1804; //the hash of the level that is loaded
const uint32_t kAppPtr = 0x806EB694, kAppEntrance = 0x338; //the entrance StartLevelChange loads the level at

const uint32_t kNextMissionCall = 0x80009334; //where fn_800092F0 (the app's update) returns to from StartLevelChange(app, the level at app+0x35C, 0)
const uint32_t kWaitFrames = 150; //a level that gives the player the controls for this long has no opening
const float kWaitSeconds = 5; //a listed cutscene that took the player has this long to take the mouse as well
const float kHoldSeconds = 1; //of level time the black stays up after an ending's skip ran into another map, for its cutscene
const uint32_t kCountingFrames = 3; //the level's time went up this many frames in a row: it runs
const double kMaxSeconds = 600; //ten minutes of cutscene, then something is wrong
const float kFadeSeconds = 0.5f; //the picture and the sound going out, and coming back
const uint32_t kShowEvery = 4; //one frame in this many is drawn, so it's seen to be moving
const uint32_t kLevelTimer = 0x805C963C; //a float, seconds played in the level
const uint32_t kTimelineHash = 0x20; //of a cutscene's timeline, the hash that names it
const uint32_t kCutscenePlaying = 0x806EB6C8; //a byte OB_Cutscene sets as one starts and clears as it ends

using Clock = GameClock; //the skip's fade and the rest go by the game's time

enum class Skip { Idle, Waiting, FadingOut, Running };

//the missions, in the order of config/openings.txt, and which of their cutscenes are skipped
const int kMaxMissions = 32;
const char* const kPartNames[kSkipParts] = {"intro", "middle", "ending"};
struct Mission
{
	uint32_t level;
	std::string name;
	bool has[kSkipParts];
};
std::vector<Mission> g_missions;
std::atomic<bool> g_on[kMaxMissions][kSkipParts];
std::atomic<bool> g_never{false};
std::atomic<bool>* g_option = &g_never; //the choice what is waited for or skipped belongs to
int g_part = kSkipIntro; //and which part it is
std::atomic<bool> g_running{false};
PPCFunc orig_menu_pending = nullptr;
PPCFunc orig_level_change = nullptr;
PPCFunc orig_frame_draw = nullptr;
bool g_restarting = false; //the menu's restart action is running, or its Continue Game: both go straight to StartLevelChange
bool g_trace = false, g_tracecut = false;
int g_forced_entrance = -1; //GE_ENTRANCE
std::atomic<uint32_t> g_asked_level{0}; //load_level
uint32_t g_forced_level = 0; //GE_LEVEL, a level's hash: whichever mission the menu starts, this one loads
Skip g_state = Skip::Idle;
uint32_t g_frames = 0; //frames in this state
uint32_t g_drawn = 0; //frames since one was drawn
uint32_t g_counting = 0; //frames in a row the level's time went up
float g_played = 0;
float g_ends = 0; //the level time this cutscene ends at, 0 if we don't know it
float g_armed_ends = 0; //the same for the cutscene that is waited for
bool g_through = false, g_armed_through = false; //it runs to its end even where the mouse is given back
bool g_back_first = false; //the mouse has to be given back before the cutscene that is waited for takes it
float g_wait_until = 0; //the level time a cutscene of config/cutscenes.txt is waited for until, 0 an opening (kWaitFrames)
bool g_holding = false; //the black and the mute are kept up after a level change, for the next map's cutscene
float g_hold_from = -1; //the level time the next map was first seen at
std::map<uint32_t, float> g_openings; //level hash -> the level time its opening ends at, 0 not known
std::map<uint32_t, bool> g_any_openings; //level hash -> its opening doesn't take the player, see the top
bool g_any = false, g_armed_any = false; //the cutscene is one that plays without the player
bool g_qte = false, g_armed_qte = false; //its quick time events pass under the skip

int MissionOf(uint32_t level)
{
	for (size_t m = 0; m < g_missions.size(); ++m)
	{
		if (g_missions[m].level == level) return (int)m;
	}
	return -1;
}

//lines of "level hash (hex), seconds, the mission's name"
void LoadOpenings(const std::string& path)
{
	std::ifstream f(path);
	for (std::string l; std::getline(f, l);)
	{
		unsigned level = 0;
		float seconds = 0;
		char name[32] = "", more[32] = "";
		if (std::sscanf(l.c_str(), "%x %f %31s %31s", &level, &seconds, name, more) < 3 || g_missions.size() >= kMaxMissions) continue;
		g_openings[level] = seconds;
		g_any_openings[level] = std::string(more) == "any";
		g_missions.push_back({level, name, {true, false, false}});
	}
}
//a cutscene after the opening that is skipped as well: lines of "level hash (hex), the hash of
//the timeline that takes the player (hex), the seconds until the picture is back, the part of
//the mission it is (middle, ending), and the mission's level when it's another map of it"
//"through" after them: the skip runs to its seconds even where the mouse is given back on the way,
//an ending's with 0 seconds to the level change
//"again": the mouse is still lost to the cutscene before when it takes the player, the skip waits
//for it to come back and be lost again
//"qte": only with Auto Quick Time Events on, and its quick time events don't end the skip, they
//pass by themselves under it
struct Inside
{
	float seconds;
	int mission, part;
	bool through, again, qte;
};
std::map<std::pair<uint32_t, uint32_t>, Inside> g_cutscenes;
void LoadCutscenes(const std::string& path)
{
	std::ifstream f(path);
	for (std::string l; std::getline(f, l);)
	{
		unsigned level = 0, cutscene = 0, mission = 0;
		float seconds = 0;
		char part[32] = "", more[2][32] = {"", ""};
		l = l.substr(0, l.find('#'));
		int n = std::sscanf(l.c_str(), "%x %x %f %31s %31s %31s", &level, &cutscene, &seconds, part, more[0], more[1]);
		if (n < 4) continue;
		bool through = false, again = false, qte = false;
		for (int i = 0; i < n - 4; ++i)
		{
			if (std::string(more[i]) == "through") through = seconds > 0 || std::string(part) == "ending";
			else if (std::string(more[i]) == "again") again = true;
			else if (std::string(more[i]) == "qte") qte = true;
			else mission = (unsigned)std::strtoul(more[i], nullptr, 16);
		}
		int m = MissionOf(mission ? mission : level);
		int p = std::find(kPartNames, kPartNames + kSkipParts, std::string(part)) - kPartNames;
		if (m < 0 || p == kSkipIntro || p == kSkipParts) continue;
		g_cutscenes[{level, cutscene}] = {seconds, m, p, through, again, qte};
		g_missions[m].has[p] = true;
	}
}

//an id of the list: the mission's name in lower case, a dash, the part
std::string SkipId(int m, int p)
{
	std::string id = g_missions[m].name;
	for (char& ch : id) ch = (char)std::tolower((unsigned char)ch);
	return id + "-" + kPartNames[p];
}
//ids, or every one (all), none, a part of every mission (intros, middles, endings), apart by commas or spaces
void SetList(const std::string& list)
{
	for (size_t m = 0; m < g_missions.size(); ++m)
	{
		for (int p = 0; p < kSkipParts; ++p) g_on[m][p] = false;
	}
	std::string word;
	for (size_t i = 0; i <= list.size(); ++i)
	{
		if (i < list.size() && list[i] != ',' && list[i] != ' ')
		{
			word += (char)std::tolower((unsigned char)list[i]);
			continue;
		}
		for (size_t m = 0; m < g_missions.size(); ++m)
		{
			for (int p = 0; p < kSkipParts; ++p)
			{
				if (word == "all" || word == std::string(kPartNames[p]) + "s" || word == SkipId((int)m, p)) g_on[m][p] = g_missions[m].has[p];
			}
		}
		word.clear();
	}
}

Clock::time_point g_started, g_started_guest;
std::string g_takes; //the cutscenes that took the player since it was last let go, for the line a won level logs, * a quick time event

//the black over the picture, read by the renderer's thread
std::atomic<float> g_fade_from{0.0f}, g_fade_to{0.0f};
std::atomic<double> g_fade_started{0.0}; //in seconds of the steady clock
std::atomic<bool> g_fade_hidden{false}; //the pause menu is up, it has to be seen

double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
float FadeNow()
{
	float along = std::clamp((float)(Now() - g_fade_started) / kFadeSeconds, 0.0f, 1.0f);
	return g_fade_from + (g_fade_to - g_fade_from) * along;
}
void FadeTo(float to)
{
	g_fade_from = FadeNow(); //a fade that is turned round goes on from where it is
	g_fade_to = to;
	g_fade_started = Now();
}

bool StartsCampaignLevel(uint32_t menu)
{
	if (ld8(menu + kPendingReady) <= 2 || ld32(menu + kPendingAction) != kActionLoadLevel) return false;
	if (ld8(menu + kPendingSplit) != 0) return false;
	uint32_t page = ld32(menu + kMenuPages + ld32(menu + kMenuPage) * 4);
	if (!guest_ptr(page)) return false;
	uint16_t item = ld16(page + 0xC);
	return item >= kItemOperative && item <= kItemTimeTrial;
}

//hold: the black and the mute stay up, the next map's cutscene is skipped under them (or they
//come off kHoldSeconds after its player is there)
void Stop(const char* why, bool hold = false)
{
	if (g_state == Skip::Running)
	{
		g_gx_skip_frames = false;
		set_fast_forward(false);
		double took = std::chrono::duration<double>(Clock::now() - g_started).count();
		double skipped = std::chrono::duration<double>(rt_now() - g_started_guest).count();
		rt_log("goldeneye: skip cutscenes: %s, %.1f s of cutscene (%u frames) in %.1f s, level %08X's cutscene left at %.2f",
			why, skipped, g_frames, took, ld32(kCurrentLevel), ldf(kLevelTimer));
	}
	else if (g_state == Skip::FadingOut) rt_log("goldeneye: skip cutscenes: not skipped, %s", why);
	else if (g_state == Skip::Waiting && g_trace) rt_log("goldeneye: skip cutscenes: not waiting any more, %s", why);
	if ((g_state == Skip::Running || g_state == Skip::FadingOut) && hold)
	{
		g_holding = true;
		g_hold_from = -1;
	}
	else if (g_state == Skip::Running || g_state == Skip::FadingOut)
	{
		audio_mute(false, kFadeSeconds);
		FadeTo(0.0f);
		g_running = false;
	}
	g_state = Skip::Idle;
}

//a mission starts from its beginning: its opening is the next cutscene
void Arm(uint32_t level)
{
	Stop("a level change");
	int m = MissionOf(level);
	if (m < 0 || !g_on[m][kSkipIntro]) return;
	g_state = Skip::Waiting;
	g_option = &g_on[m][kSkipIntro];
	g_part = kSkipIntro;
	g_frames = g_counting = 0;
	g_wait_until = 0;
	g_armed_through = g_back_first = g_armed_qte = false;
	auto known = g_openings.find(level);
	g_armed_ends = known != g_openings.end() ? known->second : 0.0f;
	g_armed_any = g_any_openings[level];
	if (g_trace) rt_log("goldeneye: skip cutscenes: level %08X starts, waiting for its opening", level);
}

//the player was just taken by a cutscene: one of config/cutscenes.txt is skipped like an opening
void ArmCutscene(uint32_t cutscene)
{
	if (g_state != Skip::Idle) return;
	auto known = g_cutscenes.find({ld32(kCurrentLevel), cutscene});
	if (known == g_cutscenes.end() || !g_on[known->second.mission][known->second.part]) return;
	if (known->second.qte && !auto_qte()) return;
	g_state = Skip::Waiting;
	g_option = &g_on[known->second.mission][known->second.part];
	g_part = known->second.part;
	g_frames = g_counting = 0;
	g_armed_ends = known->second.seconds > 0 ? ldf(kLevelTimer) + known->second.seconds : 0.0f;
	g_armed_through = known->second.through;
	g_armed_any = false;
	g_armed_qte = known->second.qte;
	g_back_first = known->second.again;
	//one that keeps the mouse a while longer is waited for until shortly before its end
	g_wait_until = std::max(ldf(kLevelTimer) + kWaitSeconds, g_armed_ends - kFadeSeconds * 3);
	if (g_trace) rt_log("goldeneye: skip cutscenes: cutscene %08X of level %08X starts", cutscene, ld32(kCurrentLevel));
}

//the cutscene has the player: the picture and the sound go out first, at normal speed
void StartFade()
{
	g_ends = g_armed_ends;
	g_through = g_armed_through;
	g_any = g_armed_any;
	g_qte = g_armed_qte;
	if (g_ends > 0 && g_ends - ldf(kLevelTimer) < kFadeSeconds * 3)
	{
		g_state = Skip::Idle; //too short to fade out of and back into
		return;
	}
	g_state = Skip::FadingOut;
	g_running = true;
	g_holding = false;
	FadeTo(1.0f);
	audio_mute(true, kFadeSeconds);
	rt_log("goldeneye: skip cutscenes: skipping a cutscene of level %08X", ld32(kCurrentLevel));
}

//the cutscene that is waited for or skipped has the player, or plays at all for one of "any"
bool Held(bool any) { return local_cutscene_locked() || (any && ld8(kCutscenePlaying)); }

//what ends a skip whenever it happens
bool Over()
{
	uint32_t state;
	if (!*g_option) Stop("turned off");
	else if (cutscene_input_waiting() && !(g_qte && auto_qte())) Stop("a button is wanted");
	else if (!local_player_state(state)) Stop("the player is gone");
	else if (!Held(g_any) && !g_through) Stop("done");
	return g_state == Skip::Idle;
}

void FadeOut()
{
	if (Over() || FadeNow() < 1.0f) return;
	g_state = Skip::Running;
	g_frames = g_drawn = 0;
	g_started = Clock::now();
	g_started_guest = rt_now();
	set_fast_forward(true);
	audio_mute(true);
}

void Run()
{
	//the pause menu and ours run at their own pace
	bool held = ld8(kPaused) != 0 || settings_open();
	if (held) set_fast_forward(false);
	if (held)
	{
		g_gx_skip_frames = false;
		return;
	}
	++g_frames;
	if (std::chrono::duration<double>(rt_now() - g_started_guest).count() > kMaxSeconds) { Stop("given up"); return; }
	if (Over()) return;
	float left = g_ends - ldf(kLevelTimer);
	if (g_ends > 0 && left <= kFadeSeconds) { Stop("the fade in"); return; }
	if (g_ends > 0 && left < kFadeSeconds + 2.0f / skip_rate())
	{
		//long steps would go past where the fade in starts: the engine's own steps up to there,
		//still as a fast forward so the last long one isn't waited for. the rate only counts
		//from the step after the next, so that one is still long
		set_fast_forward(true, (float)engine_top_rate());
		g_gx_skip_frames = false;
		return;
	}
	set_fast_forward(true);
	g_gx_skip_frames = ++g_drawn < kShowEvery;
	if (!g_gx_skip_frames) g_drawn = 0;
}

//the menu's update (the function the symbols call WIFI_FUN_80234a60, the menu in r3) calls
//ge_menu_pending while the byte at +0x7E8 is set. a level asked for in the settings menu's grid
//is put there as the game's own menus put one: the level, the entrance, action 5 and the two bytes
const uint32_t kPendingSet = 0x7E8;
PPCFunc orig_menu_update = nullptr;
void MenuUpdate(PPCContext& c)
{
	uint32_t menu = c.r[3];
	if (uint32_t asked = guest_ptr(menu) ? g_asked_level.exchange(0) : 0)
	{
		rt_log("goldeneye: level %08X asked for in the settings menu", asked);
		st32(menu + kPendingLevel, asked);
		st32(menu + kPendingEntrance, kMissionStart);
		st32(menu + kPendingAction, kActionLoadLevel);
		st8(menu + kPendingSplit, 0);
		st8(menu + kPendingReady, 3);
		st8(menu + kPendingSet, 1);
	}
	orig_menu_update(c);
}

void MenuPending(PPCContext& c)
{
	uint32_t menu = c.r[3];
	bool acts = ld8(menu + kPendingReady) > 2;
	g_restarting = acts && (ld32(menu + kPendingAction) == kActionRestartLevel || ld32(menu + kPendingAction) == kActionContinue);
	if (acts && g_trace)
	{
		uint32_t page = ld32(menu + kMenuPages + ld32(menu + kMenuPage) * 4);
		rt_log("goldeneye: skip cutscenes: menu action %u, level %08X, entrance %u, item %04X, split %u", ld32(menu + kPendingAction),
			ld32(menu + kPendingLevel), ld32(menu + kPendingEntrance), guest_ptr(page) ? ld16(page + 0xC) : 0xFFFF, ld8(menu + kPendingSplit));
	}
	if (acts && ld32(menu + kPendingAction) == kActionLoadLevel)
	{
		//a level started from 007 Custom's page is played with its sliders, one started from anywhere else isn't
		set_custom_mode(custom_start_row(ld32(menu + kMenuPages + ld32(menu + kMenuPage) * 4)));
		if (g_forced_level && StartsCampaignLevel(menu))
		{
			rt_log("goldeneye: level %08X loads in place of %08X (GE_LEVEL)", g_forced_level, ld32(menu + kPendingLevel));
			st32(menu + kPendingLevel, g_forced_level);
		}
		//a level loaded at a checkpoint or for split screen has no opening to wait for
		if (StartsCampaignLevel(menu) && ld32(menu + kPendingEntrance) == kMissionStart)
		{
			if (g_forced_entrance >= 0)
			{
				st32(menu + kPendingEntrance, (uint32_t)g_forced_entrance);
				rt_log("goldeneye: level %08X starts at entrance %d (GE_ENTRANCE)", ld32(menu + kPendingLevel), g_forced_entrance);
			}
			else Arm(ld32(menu + kPendingLevel));
		}
		else Stop("a level change");
	}
	orig_menu_pending(c);
	g_restarting = false;
}

//(app, level hash, ...) the restart action just put the mission's start in the app's entrance
void LevelChange(PPCContext& c)
{
	uint32_t app = ld32(kAppPtr), level = c.r[4];
	rt_log("cut: t %.3f level change from %08X to %08X, entrance %u, %s, from %08X", ldf(kLevelTimer), ld32(kCurrentLevel), level,
		guest_ptr(app) ? ld32(app + kAppEntrance) : 0, g_restarting ? "a restart" : "not a restart", c.lr);
	if (g_restarting)
	{
		if (guest_ptr(app) && ld32(app + kAppEntrance) <= kMissionStart)
		{
			if (g_forced_entrance >= 0)
			{
				st32(app + kAppEntrance, (uint32_t)g_forced_entrance);
				rt_log("goldeneye: level %08X restarts at entrance %d (GE_ENTRANCE)", level, g_forced_entrance);
			}
			else Arm(level);
		}
		else Stop("a level change");
	}
	//"Save and Continue": the next mission is loaded by the app's update, not by the menu. the
	//level before it was won, its ending is the last cutscene that had the player
	else if (c.lr == kNextMissionCall && guest_ptr(app) && ld32(app + kAppEntrance) == kMissionStart)
	{
		rt_log("goldeneye: level %08X won, the player was kept by cutscene%s", ld32(kCurrentLevel), g_takes.empty() ? " none" : g_takes.c_str());
		Arm(level);
	}
	//an ending that runs into another map of the same mission (Cradle's), not the next mission
	else if (g_state == Skip::Running || g_state == Skip::FadingOut) Stop("a level change", g_part == kSkipEnding && c.lr == kNextMissionCall);
	g_takes.clear();
	speedrun_level_change(ld32(kCurrentLevel), level, guest_ptr(app) ? ld32(app + kAppEntrance) : 0, !g_restarting && c.lr == kNextMissionCall);
	orig_level_change(c);
}

//(skip the scene, ...) a frame the fast forward doesn't show isn't drawn by the game either
void FrameDraw(PPCContext& c)
{
	if (g_gx_skip_frames) c.r[3] = 1;
	orig_frame_draw(c);
}

} //namespace

int skip_missions() { return (int)g_missions.size(); }
const char* skip_mission_name(int m) { return m >= 0 && m < skip_missions() ? g_missions[m].name.c_str() : ""; }
bool skip_exists(int m, int part) { return m >= 0 && m < skip_missions() && part >= 0 && part < kSkipParts && g_missions[m].has[part]; }
bool skip_on(int m, int part) { return skip_exists(m, part) && g_on[m][part]; }
void set_skip_on(int m, int part, bool on)
{
	if (skip_exists(m, part)) g_on[m][part] = on;
}
void set_skip_all(bool on)
{
	for (int m = 0; m < skip_missions(); ++m)
	{
		for (int p = 0; p < kSkipParts; ++p) set_skip_on(m, p, on);
	}
}
std::string skip_list()
{
	std::string list;
	for (int m = 0; m < skip_missions(); ++m)
	{
		for (int p = 0; p < kSkipParts; ++p)
		{
			if (skip_on(m, p)) list += (list.empty() ? "" : ",") + SkipId(m, p);
		}
	}
	return list;
}
bool skipping_cutscene() { return g_running; }
float level_time() { return ldf(kLevelTimer); }
float cutscene_fade() { return g_fade_hidden ? 0.0f : FadeNow(); }

//GE_TRACE: how long each frame took from the last long steps on to a while after the skip
void TraceFrames()
{
	static Clock::time_point last = Clock::now();
	static int after = -1;
	static std::string line;
	auto now = Clock::now();
	double ms = std::chrono::duration<double, std::milli>(now - last).count();
	last = now;
	bool ending = g_state == Skip::Running && g_ends > 0 && g_ends - ldf(kLevelTimer) < kFadeSeconds * 2;
	if (ending) { after = 90; line += "e"; }
	else if (after <= 0) return;
	else --after;
	char text[32];
	std::snprintf(text, sizeof text, "%.0f(%.0f) ", ms, engine_rate());
	line += text;
	if (after == 0) { rt_log("goldeneye: skip cutscenes: frame times in ms (engine rate): %s", line.c_str()); line.clear(); }
}

void cutscenes_frame()
{
	if (g_trace) TraceFrames();
	if (cutscene_input_waiting() && !g_takes.empty() && g_takes.back() != '*') g_takes += " *"; //a quick time event, the ending comes after
	//the mouse taken by a cutscene and given back: a cutscene with the view or the aim open counts as given back
	static int locked = -1;
	uint32_t state;
	int now = local_player_state(state) ? local_cutscene_locked() : -1;
	if (now != locked && now >= 0 && locked >= 0)
		rt_log("cut: t %.3f level %08X the mouse is %s (state %X)", ldf(kLevelTimer), ld32(kCurrentLevel), now ? "lost to the cutscene" : "back", state);
	locked = now;
	//a cutscene playing at all, whether or not it has the player (Tank's opening has the tank, not him)
	static int playing = -1;
	int plays = now >= 0 ? ld8(kCutscenePlaying) != 0 : -1;
	if (plays != playing && plays >= 0) rt_log("cut: t %.3f level %08X a cutscene %s", ldf(kLevelTimer), ld32(kCurrentLevel), plays ? "plays" : "is over");
	playing = plays;
	g_fade_hidden = ld8(kPaused) != 0;
	if (g_holding && now >= 0)
	{
		if (g_hold_from < 0) g_hold_from = ldf(kLevelTimer);
		if (ldf(kLevelTimer) - g_hold_from > kHoldSeconds)
		{
			if (g_trace) rt_log("goldeneye: skip cutscenes: no cutscene to skip in the next map");
			g_holding = false;
			g_running = false;
			audio_mute(false, kFadeSeconds);
			FadeTo(0.0f);
		}
	}
	if (g_state == Skip::Running) { Run(); return; }
	if (g_state == Skip::FadingOut) { FadeOut(); return; }
	if (g_state != Skip::Waiting) return;
	if (now < 0) return; //still loading
	//the level is there but stands still behind the loading screen until its "Continue" is
	//pressed: the mission has begun once its time counts
	float played = ldf(kLevelTimer);
	g_counting = played > g_played ? g_counting + 1 : 0;
	g_played = played;
	if (g_counting < kCountingFrames) return;
	if (g_back_first && !local_cutscene_locked()) g_back_first = false;
	if (Held(g_armed_any) && !g_back_first) StartFade();
	else if (g_wait_until > 0 ? played > g_wait_until : ++g_frames > kWaitFrames)
	{
		g_state = Skip::Idle;
		if (g_trace) rt_log("goldeneye: skip cutscenes: no opening, the player has the controls");
	}
}

//the messages a cutscene's timeline sends its player (the id at +0xC, the actor's slot in the
//byte at +3), and the player being taken as an actor and let go. the timeline that sent the
//message the player is taken in names the cutscene: its hash is at +0x20
//GE_TRACECUT logs them all
PPCFunc orig_cutscene_message = nullptr, orig_player_take = nullptr, orig_player_release = nullptr;
uint32_t g_sender = 0;
void CutsceneMessage(PPCContext& c)
{
	uint32_t msg = c.r[4];
	if (g_tracecut && guest_ptr(msg))
		rt_log("cut: t %.3f frame %u msg %08X slot %d %08X %08X %08X from %08X", ldf(kLevelTimer), ld32(kFrameCount), ld32(msg + 0xC), (int8_t)ld8(msg + 3),
		       ld32(msg + 0x14), ld32(msg + 0x18), ld32(msg + 0x1C), c.r[5]);
	g_sender = c.r[5]; //kept: the player is taken after the messages, not in one
	orig_cutscene_message(c);
}
void PlayerTake(PPCContext& c)
{
	uint32_t cutscene = guest_ptr(g_sender) ? ld32(g_sender + kTimelineHash) : 0;
	rt_log("cut: t %.3f level %08X the player is taken by cutscene %08X", ldf(kLevelTimer), ld32(kCurrentLevel), cutscene);
	char take[48];
	std::snprintf(take, sizeof take, " %08X at %.1f", cutscene, ldf(kLevelTimer));
	g_takes += take;
	orig_player_take(c);
	ArmCutscene(cutscene);
}
void PlayerRelease(PPCContext& c)
{
	rt_log("cut: t %.3f level %08X the player is let go", ldf(kLevelTimer), ld32(kCurrentLevel));
	g_takes.clear();
	orig_player_release(c);
}

void load_level(uint32_t hash) { g_asked_level = hash; }

void install_cutscenes()
{
	rt_state_register("cutscenes", [](StateIO& io) {
		bool running = g_running;
		uint32_t asked = g_asked_level;
		float from = g_fade_from, to = g_fade_to;
		double started = g_fade_started;
		io.pod(g_part); io.pod(running); io.pod(g_restarting); io.pod(asked); io.pod(g_state); io.pod(g_frames); io.pod(g_drawn);
		io.pod(g_counting); io.pod(g_played); io.pod(g_ends); io.pod(g_armed_ends); io.pod(g_through); io.pod(g_armed_through);
		io.pod(g_back_first); io.pod(g_wait_until); io.pod(g_holding); io.time(g_started); io.time(g_started_guest);
		io.pod(from); io.pod(to); io.pod(started);
		if (io.applying) { g_running = running; g_asked_level = asked; g_fade_from = from; g_fade_to = to; g_fade_started = started; }
	});
	if (const char* e = std::getenv("GE_ENTRANCE")) g_forced_entrance = std::atoi(e);
	if (const char* e = std::getenv("GE_LEVEL")) g_forced_level = (uint32_t)std::strtoul(e, nullptr, 16);
	g_trace = std::getenv("GE_TRACE") != nullptr;
	LoadOpenings(std::string(GE_ROOT) + "/config/openings.txt");
	LoadCutscenes(std::string(GE_ROOT) + "/config/cutscenes.txt");
	auto on = [](const char* name) { const char* e = std::getenv(name); return e && std::atoi(e) != 0; };
	for (int m = 0; m < skip_missions(); ++m)
	{
		set_skip_on(m, kSkipIntro, on("GE_SKIP_CUTSCENES"));
		set_skip_on(m, kSkipEnding, on("GE_SKIP_ENDINGS"));
	}
	set_skip_on(MissionOf(0x01810144), kSkipMiddle, on("GE_SKIP_ZUKOVSKY")); //Nightclub
	set_skip_on(MissionOf(0x01810147), kSkipMiddle, on("GE_SKIP_BRIGGS")); //Carrier
	if (const char* e = std::getenv("GE_SKIP_LIST")) SetList(e);
	orig_menu_pending = ppc_hook("ge_menu_pending", MenuPending);
	orig_menu_update = ppc_hook("WIFI_FUN_80234a60", MenuUpdate);
	if (!orig_menu_update) rt_log("goldeneye: the menu's update is not hooked in this build, the settings menu's level grids load nothing");
	orig_level_change = ppc_hook("StartLevelChange", LevelChange);
	orig_frame_draw = ppc_hook("ge_frame_draw", FrameDraw);
	g_tracecut = std::getenv("GE_TRACECUT") != nullptr;
	orig_cutscene_message = ppc_hook("ge_cutscene_message", CutsceneMessage);
	orig_player_take = ppc_hook("ge_cutscene_take", PlayerTake);
	orig_player_release = ppc_hook("ge_cutscene_release", PlayerRelease);
	if (!orig_menu_pending || !orig_level_change || !orig_frame_draw || !orig_cutscene_message || !orig_player_take || !orig_player_release) rt_die("goldeneye: the menu's pending action is not hooked in this build");
}

} //namespace ge
