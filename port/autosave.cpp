//a checkpoint saves the game
//
//the game only writes a mission's progress from the pause menu's "Save and Quit": its screen
//(menu 0x7D000035, MenuLoadSave::UpdateNewSaveGame fn_8024D520) calls ge_save_game each frame
//until it answers. a crash in between loses every checkpoint since
//
//ge_save_game (memcard manager 805D10B8, slot, delete, rename, checkpoint) is a state machine
//(+8: 0 or 10 idle, 0x12 writing, 0x13 written). with the checkpoint byte set it fills the save
//through the save's vtable +0x20 (fn_800AA260, "Creating a real save"): the restart point at
//806EB9DC (0x37F8 bytes, what ge_record_restart just filled) goes in as it is. it answers 0
//while it writes, 1 written, 5 failed, 6 no room
//
//so after ge_record_restart we make the same calls the screen makes, a frame each, in play
//
//the file itself is written by ge_save_write (file 80643080, slot, buffer, flags). once it is
//(its state 806EC00C is 11) it puts up a "Saved" box and waits for its button, unless bit 0 of
//the flags is set. the screen leaves it clear, we set it for our own calls
//
//GE_AUTOSAVE=0 checkpoints don't save
//GE_TRACE=1 log every checkpoint
#include "ge.h"
#include <atomic>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kSaveGame = 0x800AF650; //ge_save_game
const uint32_t kSaveManager = 0x805D10B8;
const uint32_t kManagerState = 0x8, kManagerSlot = 0x18; //the slot of the profile that is loaded
const uint32_t kStateNone = 0, kStateIdle = 10;
const uint32_t kSlots = 3;
const uint32_t kCurrentLevel = 0x805D1804; //the hash of the level that is loaded
const uint32_t kLevelKind = 0xFFFF0000, kMultiplayerLevel = 0x01A00000; //the campaign's are 0181xxxx
const uint32_t kFrontEnd = 0x01080004; //the menus are a level too, with a restart point of their own
const uint32_t kTimeChallenge = 0x805E05AB; //a byte, the screen saves without the checkpoint then
const uint32_t kWriteQuiet = 1; //ge_save_write's flag: no "Saved" box
const uint32_t kGiveUpFrames = 1800;

std::atomic<bool> g_autosave{ true };
bool g_trace{ false };
bool g_pending{ false }; //a checkpoint was recorded and isn't saved yet
uint32_t g_level{ 0 }; //the level it was recorded in
bool g_saving{ false };
bool g_stepping{ false }; //ge_save_game is running for us
uint32_t g_frames{ 0 };
PPCFunc orig_record_restart = nullptr;
PPCFunc orig_save_write = nullptr;
PPCFunc save_game = nullptr;

void RecordRestart(PPCContext& c)
{
	if (g_trace) rt_log("goldeneye: checkpoint: level %08X entrance %d", ld32(kCurrentLevel), (int32_t)c.r[4]);
	orig_record_restart(c);
	g_level = ld32(kCurrentLevel);
	g_pending = g_level != kFrontEnd;
}

void SaveWrite(PPCContext& c)
{
	if (g_stepping) c.r[6] |= kWriteQuiet;
	else speedrun_menu_save();
	orig_save_write(c);
}

bool ManagerIdle()
{
	uint32_t state = ld32(kSaveManager + kManagerState);
	return state == kStateNone || state == kStateIdle;
}

//no profile (continue without save), a time challenge and a multiplayer game have nothing to save
//a hosted network game got past the joined one's test and wrote its map into the campaign's
//save (run(7).log of 2026-10-06, level 01A00005), so it's any network game and any multiplayer map
bool CanSave()
{
	bool multiplayer = network_game() || (ld32(kCurrentLevel) & kLevelKind) == kMultiplayerLevel;
	return ld32(kSaveManager + kManagerSlot) < kSlots && ld8(kTimeChallenge) == 0 && !multiplayer;
}

uint32_t Step(PPCContext& c)
{
	PPCContext saved = c;
	c.r[3] = kSaveManager;
	c.r[4] = ld32(kSaveManager + kManagerSlot);
	c.r[5] = 0;
	c.r[6] = 0;
	c.r[7] = 1;
	g_stepping = true;
	save_game(c);
	g_stepping = false;
	uint32_t result = c.r[3];
	c = saved;
	return result;
}

} //namespace

bool autosave() { return g_autosave; }
void set_autosave(bool on) { g_autosave = on; }

void autosave_frame(PPCContext& c)
{
	if (!save_game) return;
	if (!g_saving)
	{
		if (!g_pending) return;
		if (!g_autosave) { g_pending = false; return; }
		//the level is still loading, or the pause menu is up and may be saving itself
		if (!in_play() || !ManagerIdle()) return;
		g_pending = false;
		if (!CanSave() || ld32(kCurrentLevel) != g_level) return;
		g_saving = true;
		g_frames = 0;
	}
	uint32_t result = Step(c);
	++g_frames;
	if (result == 0 && g_frames < kGiveUpFrames) return;
	g_saving = false;
	if (result == 1) rt_log("goldeneye: checkpoint saved (level %08X, %u frames)", ld32(kCurrentLevel), g_frames);
	else rt_log("goldeneye: checkpoint not saved: the game's save answered %u after %u frames", result, g_frames);
}

void install_autosave()
{
	if (const char* e = std::getenv("GE_AUTOSAVE")) g_autosave = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	save_game = ppc_lookup(kSaveGame);
	orig_record_restart = ppc_hook("ge_record_restart", RecordRestart);
	orig_save_write = ppc_hook("ge_save_write", SaveWrite);
	if (!save_game || !orig_record_restart || !orig_save_write) rt_die("goldeneye: the checkpoint is not hooked in this build");
}

} //namespace ge
