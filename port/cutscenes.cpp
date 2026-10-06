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
//
//with the fade off (GE_SKIP_FADE=0) the fast forward is seen, one frame in four, and the last
//second is eased into: from a second before the end the steps shrink from a third of a second
//to a frame of the engine's rate, each one shown. a level that isn't in the list just cuts
//
//GE_SKIP_CUTSCENES=1 on
//GE_SKIP_ZUKOVSKY=1, GE_SKIP_BRIGGS=1 the cutscenes of config/cutscenes.txt are skipped the same
//way (Nightclub's meeting with Zukovsky, Carrier's with Sky Briggs), each an option of its own
//GE_SKIP_FADE=0 no fade
//GE_SKIP_RATE=N the frames a second of game time while skipping (3)
//GE_ENTRANCE=N start every mission at entrance N instead of 1, for trying out a level's
//entrances (0 is one too). nothing is skipped then
//GE_TRACE=1 log every checkpoint
//GE_TRACECUT=1 log what a cutscene's timeline tells its player
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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
const uint32_t kActionRestartLevel = 3, kActionLoadLevel = 5, kActionContinue = 6; //6: the main menu's Continue Game
//the item picked on the page the level starts from, 0x13..0x16 are the campaign's difficulties, 0x17 its Time Trial
const uint16_t kItemOperative = 0x13, kItemTimeTrial = 0x17;
const uint32_t kMissionStart = 1; //the entrance a mission begins at
const uint32_t kCurrentLevel = 0x805D1804; //the hash of the level that is loaded
const uint32_t kAppPtr = 0x806EB694, kAppEntrance = 0x338; //the entrance StartLevelChange loads the level at

const uint32_t kNextMissionCall = 0x80009334; //where fn_800092F0 (the app's update) returns to from StartLevelChange(app, the level at app+0x35C, 0)
const uint32_t kWaitFrames = 150; //a level that gives the player the controls for this long has no opening
const uint32_t kCountingFrames = 3; //the level's time went up this many frames in a row: it runs
const double kMaxSeconds = 600; //ten minutes of cutscene, then something is wrong
const float kFadeSeconds = 0.5f; //the picture and the sound going out, and coming back
const float kEaseSeconds = 1.0f; //of the cutscene's end, slowing down to normal speed when nothing fades
const uint32_t kShowEvery = 4; //one frame in this many is drawn, so it's seen to be moving
const uint32_t kLevelTimer = 0x805C963C; //a float, seconds played in the level
const uint32_t kTimelineHash = 0x20; //of a cutscene's timeline, the hash that names it

using Clock = std::chrono::steady_clock;

enum class Skip { Idle, Waiting, FadingOut, Running };

std::atomic<bool> g_skip_cutscenes{false};
std::atomic<bool> g_skip_zukovsky{false};
std::atomic<bool> g_skip_briggs{false};
std::atomic<bool> g_skip_fade{true};
bool g_fading = false; //this skip is behind the fade
std::atomic<bool>* g_option = &g_skip_cutscenes; //the option what is waited for or skipped belongs to
std::atomic<bool> g_running{false};
PPCFunc orig_menu_pending = nullptr;
PPCFunc orig_level_change = nullptr;
PPCFunc orig_frame_draw = nullptr;
bool g_restarting = false; //the menu's restart action is running, or its Continue Game: both go straight to StartLevelChange
bool g_trace = false, g_tracecut = false;
int g_forced_entrance = -1; //GE_ENTRANCE
uint32_t g_forced_level = 0; //GE_LEVEL, a level's hash: whichever mission the menu starts, this one loads
Skip g_state = Skip::Idle;
uint32_t g_frames = 0; //frames in this state
uint32_t g_drawn = 0; //frames since one was drawn
uint32_t g_counting = 0; //frames in a row the level's time went up
float g_played = 0;
float g_ends = 0; //the level time this cutscene ends at, 0 if we don't know it
float g_armed_ends = 0; //the same for the cutscene that is waited for
std::map<uint32_t, float> g_openings; //level hash -> the level time its opening ends at

//lines of "level hash (hex), seconds"
void LoadOpenings(const std::string& path)
{
	std::ifstream f(path);
	for (std::string l; std::getline(f, l);)
	{
		unsigned level = 0;
		float seconds = 0;
		if (std::sscanf(l.c_str(), "%x %f", &level, &seconds) == 2) g_openings[level] = seconds;
	}
}
//a cutscene in the middle of a level that is skipped as well: lines of "level hash (hex),
//the hash of the timeline that takes the player (hex), how long the player is kept in
//seconds, the option it is skipped with (zukovsky, briggs)"
struct Inside
{
	float seconds;
	std::atomic<bool>* option;
};
std::map<std::pair<uint32_t, uint32_t>, Inside> g_cutscenes;
void LoadCutscenes(const std::string& path)
{
	std::ifstream f(path);
	for (std::string l; std::getline(f, l);)
	{
		unsigned level = 0, cutscene = 0;
		float seconds = 0;
		char option[32] = "";
		if (std::sscanf(l.c_str(), "%x %x %f %31s", &level, &cutscene, &seconds, option) != 4) continue;
		if (std::string(option) == "zukovsky") g_cutscenes[{level, cutscene}] = {seconds, &g_skip_zukovsky};
		else if (std::string(option) == "briggs") g_cutscenes[{level, cutscene}] = {seconds, &g_skip_briggs};
	}
}
Clock::time_point g_started, g_started_guest;

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

void Stop(const char* why)
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
	if (g_state == Skip::Running || g_state == Skip::FadingOut)
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
	if (!g_skip_cutscenes) return;
	g_state = Skip::Waiting;
	g_option = &g_skip_cutscenes;
	g_frames = g_counting = 0;
	auto known = g_openings.find(level);
	g_armed_ends = known != g_openings.end() ? known->second : 0.0f;
	if (g_trace) rt_log("goldeneye: skip cutscenes: level %08X starts, waiting for its opening", level);
}

//the player was just taken by a cutscene: one of config/cutscenes.txt is skipped like an opening
void ArmCutscene(uint32_t cutscene)
{
	if (g_state != Skip::Idle) return;
	auto known = g_cutscenes.find({ld32(kCurrentLevel), cutscene});
	if (known == g_cutscenes.end() || !*known->second.option) return;
	g_state = Skip::Waiting;
	g_option = known->second.option;
	g_frames = g_counting = 0;
	g_armed_ends = known->second.seconds > 0 ? ldf(kLevelTimer) + known->second.seconds : 0.0f;
	if (g_trace) rt_log("goldeneye: skip cutscenes: cutscene %08X of level %08X starts", cutscene, ld32(kCurrentLevel));
}

void StartSteps()
{
	g_state = Skip::Running;
	g_frames = g_drawn = 0;
	g_started = Clock::now();
	g_started_guest = rt_now();
	set_fast_forward(true);
	audio_mute(true);
}

//the cutscene has the player: the picture and the sound go out first, at normal speed
void StartFade()
{
	g_ends = g_armed_ends;
	g_fading = g_skip_fade;
	if (g_fading && g_ends > 0 && g_ends - ldf(kLevelTimer) < kFadeSeconds * 3)
	{
		g_state = Skip::Idle; //too short to fade out of and back into
		return;
	}
	g_running = true;
	rt_log("goldeneye: skip cutscenes: skipping a cutscene of level %08X", ld32(kCurrentLevel));
	if (!g_fading) { StartSteps(); return; }
	g_state = Skip::FadingOut;
	FadeTo(1.0f);
	audio_mute(true, kFadeSeconds);
}

//what ends a skip whenever it happens
bool Over()
{
	uint32_t state;
	if (!*g_option) Stop("turned off");
	else if (cutscene_input_waiting()) Stop("a button is wanted");
	else if (!local_player_state(state)) Stop("the player is gone");
	else if (!local_cutscene_locked()) Stop("done");
	return g_state == Skip::Idle;
}

void FadeOut()
{
	if (!Over() && FadeNow() >= 1.0f) StartSteps();
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
	if (!g_fading && g_ends > 0 && left < kEaseSeconds)
	{
		//the rate goes from the long steps' to the engine's own, by equal factors over the second
		float along = std::clamp(1.0f - left / kEaseSeconds, 0.0f, 1.0f), from = skip_rate(), to = (float)engine_top_rate();
		set_fast_forward(true, from * std::pow(std::max(1.0f, to / from), along));
		g_gx_skip_frames = false;
		return;
	}
	if (g_fading && g_ends > 0 && left <= kFadeSeconds) { Stop("the fade in"); return; }
	if (g_fading && g_ends > 0 && left < kFadeSeconds + 2.0f / skip_rate())
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
	if (g_trace)
		rt_log("goldeneye: skip cutscenes: level change to %08X, entrance %u, %s, from %08X", level, guest_ptr(app) ? ld32(app + kAppEntrance) : 0,
			g_restarting ? "a restart" : "not a restart", c.lr);
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
	//"Save and Continue": the next mission is loaded by the app's update, not by the menu
	else if (c.lr == kNextMissionCall && guest_ptr(app) && ld32(app + kAppEntrance) == kMissionStart) Arm(level);
	else if (g_state == Skip::Running || g_state == Skip::FadingOut) Stop("a level change");
	orig_level_change(c);
}

//(skip the scene, ...) a frame the fast forward doesn't show isn't drawn by the game either
void FrameDraw(PPCContext& c)
{
	if (g_gx_skip_frames) c.r[3] = 1;
	orig_frame_draw(c);
}

} //namespace

bool skip_cutscenes() { return g_skip_cutscenes; }
void set_skip_cutscenes(bool on) { g_skip_cutscenes = on; }
bool skip_zukovsky() { return g_skip_zukovsky; }
void set_skip_zukovsky(bool on) { g_skip_zukovsky = on; }
bool skip_briggs() { return g_skip_briggs; }
void set_skip_briggs(bool on) { g_skip_briggs = on; }
bool skip_fade() { return g_skip_fade; }
void set_skip_fade(bool on) { g_skip_fade = on; }
bool skipping_cutscene() { return g_running; }
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
	bool ending = g_state == Skip::Running && g_ends > 0 && g_ends - ldf(kLevelTimer) < kEaseSeconds;
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
	g_fade_hidden = ld8(kPaused) != 0;
	if (g_state == Skip::Running) { Run(); return; }
	if (g_state == Skip::FadingOut) { FadeOut(); return; }
	if (g_state != Skip::Waiting) return;
	uint32_t state;
	if (!local_player_state(state)) return; //still loading
	//the level is there but stands still behind the loading screen until its "Continue" is
	//pressed: the mission has begun once its time counts
	float played = ldf(kLevelTimer);
	g_counting = played > g_played ? g_counting + 1 : 0;
	g_played = played;
	if (g_counting < kCountingFrames) return;
	if (local_cutscene_locked()) StartFade();
	else if (++g_frames > kWaitFrames)
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
	if (g_tracecut || g_trace) rt_log("cut: t %.3f frame %u the player %08X is taken by cutscene %08X", ldf(kLevelTimer), ld32(kFrameCount), c.r[3], cutscene);
	orig_player_take(c);
	ArmCutscene(cutscene);
}
void PlayerRelease(PPCContext& c)
{
	rt_log("cut: t %.3f frame %u the player %08X is let go", ldf(kLevelTimer), ld32(kFrameCount), c.r[3]);
	orig_player_release(c);
}

void install_cutscenes()
{
	if (const char* e = std::getenv("GE_SKIP_CUTSCENES")) g_skip_cutscenes = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_SKIP_ZUKOVSKY")) g_skip_zukovsky = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_SKIP_BRIGGS")) g_skip_briggs = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_SKIP_FADE")) g_skip_fade = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_ENTRANCE")) g_forced_entrance = std::atoi(e);
	if (const char* e = std::getenv("GE_LEVEL")) g_forced_level = (uint32_t)std::strtoul(e, nullptr, 16);
	g_trace = std::getenv("GE_TRACE") != nullptr;
	LoadOpenings(std::string(GE_ROOT) + "/config/openings.txt");
	orig_menu_pending = ppc_hook("ge_menu_pending", MenuPending);
	orig_level_change = ppc_hook("StartLevelChange", LevelChange);
	orig_frame_draw = ppc_hook("ge_frame_draw", FrameDraw);
	g_tracecut = std::getenv("GE_TRACECUT") != nullptr;
	LoadCutscenes(std::string(GE_ROOT) + "/config/cutscenes.txt");
	orig_cutscene_message = ppc_hook("ge_cutscene_message", CutsceneMessage);
	orig_player_take = ppc_hook("ge_cutscene_take", PlayerTake);
	if (g_tracecut) orig_player_release = ppc_hook("ge_cutscene_release", PlayerRelease);
	if (!orig_menu_pending || !orig_level_change || !orig_frame_draw || !orig_cutscene_message || !orig_player_take) rt_die("goldeneye: the menu's pending action is not hooked in this build");
}

} //namespace ge
