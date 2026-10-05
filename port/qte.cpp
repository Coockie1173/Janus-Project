//auto qte: the game's quick time events press their own buttons
//
//a cutscene that wants input has an input object, updated once a frame by ge_cutscene_input
//(8001C4F0, the one with "ButtonBash success" and "Cut Scene Input : 'Button Action' not set")
//it holds up to four buttons, each the number of an action bit plus one (-1: none), and a kind:
//1, 3, 5 the buttons one after the other, 2, 4 all of them at once, 6 a button bash
//the update tests them against the local controller's pressed actions (+0x534)
//
//the bash keeps a level at +0x3C that climbs by itself and drops with every frame the button
//is pressed in, by an amount that goes with the frame's time. under the low mark it's won,
//over the high one once the time left (+0x40) ran out it's lost
//the drop is 30 * 5 * the frame's time: 5 a press at the game's 30 frames a second, 1 at 144,
//and no hand wins it there. the smoothing (+0x44 goes a fifth of the way to the level every
//frame) is a frame's too. so above 30 the rest of the 5 is taken off the level before the
//call, and the smoothed level is set up so that the fifth it moves by lands where the step's
//share would have. nothing changes at 30
//
//for that one call the wanted bits are put into the pressed actions and taken out after
//
//GE_AUTO_QTE=1 on
//GE_TRACE=1 log every input a cutscene starts and how it ends
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{

//the input object, as ge_cutscene_input takes it in r3
const uint32_t kButtons = 0x00; //four words
const uint32_t kKind = 0x14;
const uint32_t kBashLevel = 0x3C, kBashTimeLeft = 0x40, kBashSmooth = 0x44, kBashClimb = 0x48; //floats
const uint32_t kCurrent = 0x50; //which of the four is next
const uint32_t kSucceeded = 0x60, kDone = 0x62; //bytes
const uint32_t kKindBase = 0x7B000000, kKindAllFirst = 2, kKindAllSecond = 4, kKindBash = 6;
const uint32_t kNoButton = 0xFFFFFFFF;
const uint32_t kActPressed = 0x534; //the controller's actions pressed this frame

//the bash's numbers in the game's data
const uint32_t kFrameTime = 0x806E9E50, kTimeScale = 0x806E9E64; //-0x7130(r13) seconds, -0x711C(r13)
const uint32_t kBashRate = 0x806ED29C; //30, the level's units are a frame's at 30
const uint32_t kBashPress = 0x806ED2A0; //5, a press at 30 (and the mark it's won under)
const uint32_t kBashSmoothing = 0x806ED2A4; //0.2
const float kBashTop = 100.0f;

std::atomic<bool> g_auto_qte{false};
PPCFunc orig_cutscene_input = nullptr;
bool g_trace = false;
std::atomic<uint32_t> g_waiting_frame{~0u}; //the last frame an input waited for a button in

uint32_t ButtonBit(uint32_t button)
{
	return button == kNoButton ? 0 : 1u << (((button & 0xFF) - 1) & 31);
}

//the actions the input waits for right now
uint32_t Wanted(uint32_t input)
{
	uint32_t kind = ld32(input + kKind) - kKindBase;
	if (kind == 0 || kind > kKindBash) return 0;
	if (kind == kKindAllFirst || kind == kKindAllSecond)
	{
		uint32_t all = 0;
		for (uint32_t i = 0; i < 4; ++i) all |= ButtonBit(ld32(input + kButtons + i * 4));
		return all;
	}
	uint32_t current = ld32(input + kCurrent);
	return current < 4 ? ButtonBit(ld32(input + kButtons + current * 4)) : 0;
}

//makes this call's bash step the same as a frame's at 30 would be. pressed: the button is down
//held: it's down every frame (auto qte), which drops the level by the same a second at any rate
//as it is, so only the smoothing is set up
void BashStep(uint32_t input, bool pressed, bool held)
{
	float scale = ldf(kTimeScale), frames = ldf(kFrameTime) * kGameFps;
	if (!(frames > 0.01f && frames < 0.999f) || !(scale > 0.0f)) return;
	float step = frames * scale, press = ldf(kBashPress), smoothing = ldf(kBashSmoothing);
	float level = ldf(input + kBashLevel), left = ldf(input + kBashTimeLeft);
	bool open = left > 0.0f && left - step / kGameFps > 0.0f; //out of time the game puts the level at the top
	if (open && pressed && !held)
	{
		level = std::max(level - press * (scale - step), 0.0f);
		stf(input + kBashLevel, level);
	}
	//the level the game will get to, and the smoothed one that takes its fifth to the right place
	if (!open) level = kBashTop;
	else if (pressed || held) level = std::max(level - press * step, 0.0f);
	level = std::min(level + ldf(input + kBashClimb) * step, kBashTop);
	if (!(smoothing > 0.0f && smoothing < 1.0f)) return;
	float share = 1.0f - std::pow(1.0f - smoothing, step), smooth = ldf(input + kBashSmooth);
	stf(input + kBashSmooth, (smooth * (1.0f - share) + (share - smoothing) * level) / (1.0f - smoothing));
}

void Trace(uint32_t input, bool wasDone)
{
	static uint32_t seen = 0;
	bool done = ld8(input + kDone) != 0;
	if (!done && seen != input)
	{
		seen = input;
		rt_log("goldeneye: qte: kind %u, buttons %X %X %X %X, bash time %.2f climb %.2f", ld32(input + kKind) - kKindBase, ld32(input + kButtons),
			ld32(input + kButtons + 4), ld32(input + kButtons + 8), ld32(input + kButtons + 12), ldf(input + kBashTimeLeft), ldf(input + kBashClimb));
	}
	if (done && !wasDone)
	{
		rt_log("goldeneye: qte: %s", ld8(input + kSucceeded) ? "passed" : "over");
		seen = 0;
	}
}

void CutsceneInput(PPCContext& c)
{
	uint32_t input = c.r[3], ctrl = local_controller();
	if (!guest_ptr(input)) { orig_cutscene_input(c); return; }
	bool wasDone = ld8(input + kDone) != 0;
	if (!wasDone && Wanted(input)) g_waiting_frame = ld32(kFrameCount);
	uint32_t waits = !wasDone && guest_ptr(ctrl) ? Wanted(input) : 0, wanted = g_auto_qte ? waits : 0;
	if (waits && ld32(input + kKind) - kKindBase == kKindBash) BashStep(input, (ld32(ctrl + kActPressed) & waits) == waits, wanted != 0);
	if (!wanted)
	{
		orig_cutscene_input(c);
		if (g_trace) Trace(input, wasDone);
		return;
	}
	uint32_t pressed = ld32(ctrl + kActPressed);
	st32(ctrl + kActPressed, pressed | wanted);
	orig_cutscene_input(c);
	st32(ctrl + kActPressed, pressed);
	if (g_trace) Trace(input, wasDone);
}

} //namespace

bool cutscene_input_waiting() { return ld32(kFrameCount) - g_waiting_frame < 4; }

bool auto_qte() { return g_auto_qte; }
void set_auto_qte(bool on) { g_auto_qte = on; }

void install_qte()
{
	if (const char* e = std::getenv("GE_AUTO_QTE")) g_auto_qte = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_cutscene_input = ppc_hook("ge_cutscene_input", CutsceneInput);
	if (!orig_cutscene_input) rt_die("goldeneye: the cutscene input is not hooked in this build");
}

} //namespace ge
