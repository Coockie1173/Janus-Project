//skip cutscenes: a mission's opening cutscene is fast-forwarded
//
//the game has no way past a cutscene, and starting at the checkpoint after it (what this did
//before) isn't the same level: scripts that ran in the opening never run. so the opening plays,
//only very fast: the runtime's clock runs many times as quick (rt_set_speed), so frames, sound,
//timers and the disc all stay in step, most frames aren't drawn and the sound is off
//
//a mission picked in the menu loads at entrance 1 (ge_menu_pending), Restart Mission puts the
//same in the app's object and calls StartLevelChange. after either the fast forward waits for
//the first cutscene that takes the player (state 0x22 with the view and the aim locked) and
//runs until the player gets anything back: the view, the aim, a button to press
//
//GE_SKIP_CUTSCENES=1 on
//GE_SKIP_SPEED=N the highest speed (16)
//GE_TRACE=1 log every checkpoint
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>

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

const double kStartSpeed = 4.0;
const uint32_t kWaitFrames = 150; //a level that gives the player the controls for this long has no opening
const uint32_t kGapFrames = 6; //the lock may let go for a few frames between two shots
const uint32_t kMaxFrames = 30 * 60 * 10; //ten minutes of cutscene, then something is wrong

using Clock = std::chrono::steady_clock;

enum class Skip { Idle, Waiting, Running };

std::atomic<bool> g_skip_cutscenes{false};
std::atomic<bool> g_running{false};
PPCFunc orig_menu_pending = nullptr;
PPCFunc orig_record_restart = nullptr;
PPCFunc orig_level_change = nullptr;
bool g_restarting = false; //the menu's restart action is running
bool g_trace = false;
double g_top_speed = 16.0;
Skip g_state = Skip::Idle;
uint32_t g_frames = 0; //frames in this state
uint32_t g_free_frames = 0; //frames in a row the player wasn't locked
uint32_t g_drawn = 0; //frames since one was drawn
double g_speed = 1.0;
Clock::time_point g_started, g_started_guest, g_measured;

bool StartsCampaignLevel(uint32_t menu)
{
	if (ld8(menu + kPendingReady) <= 2 || ld32(menu + kPendingAction) != kActionLoadLevel) return false;
	if (ld8(menu + kPendingSplit) != 0) return false;
	uint32_t page = ld32(menu + kMenuPages + ld32(menu + kMenuPage) * 4);
	if (!guest_ptr(page)) return false;
	uint16_t item = ld16(page + 0xC);
	return item >= kItemOperative && item <= kItemClassic;
}

void SetSpeed(double speed)
{
	g_speed = speed;
	rt_set_speed(speed);
}

void Stop(const char* why)
{
	if (g_state == Skip::Running)
	{
		SetSpeed(1.0);
		g_gx_skip_frames = false;
		audio_mute(false);
		set_fast_forward(false);
		g_running = false;
		double took = std::chrono::duration<double>(Clock::now() - g_started).count();
		double skipped = std::chrono::duration<double>(rt_now() - g_started_guest).count();
		rt_log("goldeneye: skip cutscenes: %s, %.1f s of cutscene (%u frames) in %.1f s", why, skipped, g_frames, took);
	}
	g_state = Skip::Idle;
}

//a mission starts from its beginning: its opening is the next cutscene
void Arm(uint32_t level)
{
	Stop("a level change");
	if (!g_skip_cutscenes) return;
	g_state = Skip::Waiting;
	g_frames = 0;
	if (g_trace) rt_log("goldeneye: skip cutscenes: level %08X starts, waiting for its opening", level);
}

void Start()
{
	g_state = Skip::Running;
	g_frames = g_free_frames = g_drawn = 0;
	g_started = g_measured = Clock::now();
	g_started_guest = rt_now();
	g_running = true;
	set_fast_forward(true);
	audio_mute(true);
	SetSpeed(std::min(kStartSpeed, g_top_speed));
	rt_log("goldeneye: skip cutscenes: fast-forwarding level %08X's opening", ld32(kCurrentLevel));
}

//the clock may only run as fast as the game makes its frames: faster and the sound and the
//timers get ahead of the picture. the engine says so itself, its rate goes down one for
//every late frame. ten times a second, a step up while it holds its highest rate and a
//step down when it doesn't
void Pace()
{
	auto now = Clock::now();
	if (now - g_measured < std::chrono::milliseconds(100)) return;
	g_measured = now;
	if (engine_rate_held()) SetSpeed(std::min(g_top_speed, g_speed * 1.1));
	else SetSpeed(std::max(1.0, g_speed * 0.85));
}

void Run()
{
	//the pause menu and ours stop the clock's hurry, not the skip
	bool held = ld8(kPaused) != 0 || settings_open();
	if (held)
	{
		if (g_speed != 1.0) SetSpeed(1.0);
		g_gx_skip_frames = false;
		return;
	}
	++g_frames;
	if (!g_skip_cutscenes) { Stop("turned off"); return; }
	if (g_frames > kMaxFrames) { Stop("given up"); return; }
	if (cutscene_input_waiting()) { Stop("a button is wanted"); return; }
	uint32_t state;
	if (!local_player_state(state)) { Stop("the player is gone"); return; }
	g_free_frames = local_cutscene_locked() ? 0 : g_free_frames + 1;
	if (g_free_frames > kGapFrames) { Stop("done"); return; }
	Pace();
	//about fifteen pictures a second get drawn, so it's seen to be moving
	uint32_t every = (uint32_t)std::max(1.0, g_speed * 2.0);
	g_gx_skip_frames = ++g_drawn < every;
	if (!g_gx_skip_frames) g_drawn = 0;
}

void MenuPending(PPCContext& c)
{
	uint32_t menu = c.r[3];
	bool acts = ld8(menu + kPendingReady) > 2;
	g_restarting = acts && ld32(menu + kPendingAction) == kActionRestartLevel;
	if (acts && ld32(menu + kPendingAction) == kActionLoadLevel)
	{
		//a level loaded at a checkpoint or for split screen has no opening to wait for
		if (StartsCampaignLevel(menu) && ld32(menu + kPendingEntrance) == kMissionStart) Arm(ld32(menu + kPendingLevel));
		else Stop("a level change");
	}
	orig_menu_pending(c);
	g_restarting = false;
}

//(app, level hash, ...) the restart action just put the mission's start in the app's entrance
void LevelChange(PPCContext& c)
{
	uint32_t app = ld32(kAppPtr), level = c.r[4];
	if (g_restarting)
	{
		if (guest_ptr(app) && ld32(app + kAppEntrance) <= kMissionStart) Arm(level);
		else Stop("a level change");
	}
	else if (g_state == Skip::Running) Stop("a level change");
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
bool skipping_cutscene() { return g_running; }

void cutscenes_frame()
{
	if (g_state == Skip::Running) { Run(); return; }
	if (g_state != Skip::Waiting) return;
	uint32_t state;
	if (!local_player_state(state)) return; //still loading
	if (local_cutscene_locked()) Start();
	else if (++g_frames > kWaitFrames) g_state = Skip::Idle;
}

void install_cutscenes()
{
	if (const char* e = std::getenv("GE_SKIP_CUTSCENES")) g_skip_cutscenes = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_SKIP_SPEED")) g_top_speed = std::clamp(std::atof(e), 1.0, 64.0);
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_menu_pending = ppc_hook("ge_menu_pending", MenuPending);
	orig_level_change = ppc_hook("StartLevelChange", LevelChange);
	if (!orig_menu_pending || !orig_level_change) rt_die("goldeneye: the menu's pending action is not hooked in this build");
	if (g_trace) orig_record_restart = ppc_hook("ge_record_restart", RecordRestart);
}

} //namespace ge
