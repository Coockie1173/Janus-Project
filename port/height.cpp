//a sound above the listener is thinner, one below is duller
//
//the game's sounds have a direction around the listener and nothing else: fn_80380050 works a
//voice's values out, once a sound frame for each voice of a playing sound, from the angle
//fn_803983B0 answers for the sound's place (0 to 360 around the nearest listener), and
//fn_803999E0 makes the pan and the surround pan of that. how high the sound is doesn't come
//into it: someone a floor up and someone a floor down are both "over there, to the left"
//
//two speakers can't place a sound up or down, what an ear goes by is its colour: from above
//there is more of the high frequencies, from below less. the voice has both filters and the
//game sets them with the rest of the values (fn_803A4F50, the block's +0x30 and +0x34):
//+0x30 is a low-pass, 0 off, the one the game uses to muffle what's behind the listener (AX's
//one-pole, a0 = 27145 * (1 - value): 0.8 is 3 dB down at 1 kHz and 13 at 4 kHz), +0x34 a
//high-pass in 24 steps of the table at 80593158 (0.5 is step 11: 5 dB down at 500 Hz, 17 at
//250). so a sound above the listener gets the high-pass and one below the low-pass, more the
//further up or down it is, on top of what the game set
//
//up and down is by height, not by angle: it's the floor someone is on that matters, however
//far along it. the listener hears from its camera (+0x1C of a listener, the five at
//806474B0's +0xC, 0xC0 each, there with the bytes at +0 and +2). on its own floor footsteps
//are 1.5 below that and guns and voices about level with it, so from kLevelLow to kLevelHigh
//nothing changes, and kFull further it's all there: feet on the floor above start at about
//1 up, a gun on the floor below at about 3 down
//
//a sound within kOwn of the listener is its own. one with a range past kFarthest (music, the
//menus) or one that isn't placed in the world (the byte at +0x40: bit 7 placed, bit 6 no
//direction) is left alone. in a cutscene (the player's state 0x22) every sound is: it's mixed
//for the cutscene's camera, not for where the player stands
//
//GE_SOUND_HEIGHT=0 the game's own
//GE_SOUND_HEIGHT_FORCE=1 or -1 everything in the world counts as fully above or below, to hear what it does
//GE_TRACE=1 log each voice as it goes above or below
#include "ge.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kListeners = 0x806474B0, kListenerFirst = 0xC, kListenerSize = 0xC0, kListenerHears = 0x2, kListenerEar = 0x1C;
const int kListenerCount = 5;
const uint32_t kSoundFlags = 0x40, kSoundFar = 0xD0;
const uint8_t kPlaced = 0x80, kNoDirection = 0x40;
const uint32_t kValuesLowPass = 0x30, kValuesHighPass = 0x34;
const float kOwn = 2.5f, kFarthest = 150.0f;
const float kLevelLow = -2.2f, kLevelHigh = 0.3f; //from the listener's camera: its own floor in between
const float kFull = 1.0f; //this much further up or down the filter is all there
const float kAbove = 0.5f, kBelow = 0.8f; //the high-pass of a sound above, the low-pass of one below

PPCFunc orig_pan = nullptr;
bool g_on = true, g_trace = false;
float g_force = 0.0f;

//how far the place is above (positive) or below the nearest listener's own floor, -1 to 1
float Height(const float place[3])
{
	const uint32_t listeners = ld32(kListeners);
	if (!guest_ptr(listeners)) return 0.0f;
	float up = 0.0f, distance = -1.0f;
	for (int i = 0; i < kListenerCount; ++i)
	{
		const uint32_t l = listeners + kListenerFirst + i * kListenerSize;
		if (!ld8(l) || !ld8(l + kListenerHears)) continue;
		const float x = place[0] - ldf(l + kListenerEar), y = place[1] - ldf(l + kListenerEar + 4), z = place[2] - ldf(l + kListenerEar + 8);
		const float d = std::sqrt(x * x + y * y + z * z);
		if (distance >= 0.0f && d >= distance) continue;
		distance = d;
		up = y;
	}
	if (distance < kOwn) return 0.0f;
	if (up > kLevelHigh) return std::min(1.0f, (up - kLevelHigh) / kFull);
	if (up < kLevelLow) return std::max(-1.0f, (up - kLevelLow) / kFull);
	return 0.0f;
}

//ge_sound_pan(sound, place, values, voice), f1 how close
void Pan(PPCContext& c)
{
	const uint32_t sound = c.r[3], place = c.r[4], values = c.r[5];
	orig_pan(c);
	if (!g_on || !in_play() || local_in_cutscene()) return;
	if ((ld8(sound + kSoundFlags) & (kPlaced | kNoDirection)) != kPlaced) return;
	const float far = ldf(sound + kSoundFar);
	if (!(far > 0.0f) || far > kFarthest) return;

	const float at[3] = { ldf(place), ldf(place + 4), ldf(place + 8) };
	const float height = g_force != 0.0f ? g_force : Height(at);
	if (height > 0.0f) stf(values + kValuesHighPass, std::max(ldf(values + kValuesHighPass), height * kAbove));
	if (height < 0.0f) stf(values + kValuesLowPass, std::max(ldf(values + kValuesLowPass), -height * kBelow));

	if (!g_trace) return;
	static uint32_t s_sound[64];
	static int8_t s_side[64];
	const int slot = (sound >> 4) % 64;
	const int8_t side = height >= 1.0f ? 1 : height <= -1.0f ? -1 : 0;
	if (s_sound[slot] == sound && s_side[slot] == side) return;
	s_sound[slot] = sound; s_side[slot] = side;
	if (side) rt_log("goldeneye: sound at %.1f %.1f %.1f is %s the listener: low-pass %.2f, high-pass %.2f", at[0], at[1], at[2], side > 0 ? "above" : "below", ldf(values + kValuesLowPass), ldf(values + kValuesHighPass));
}

} //namespace

bool sound_height() { return g_on; }
void set_sound_height(bool on) { g_on = on; }

void install_height()
{
	if (const char* e = std::getenv("GE_SOUND_HEIGHT")) g_on = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_SOUND_HEIGHT_FORCE")) g_force = std::atoi(e) > 0 ? 1.0f : -1.0f;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_pan = ppc_hook("ge_sound_pan", Pan);
	if (!orig_pan) rt_log("goldeneye: ge_sound_pan is not hooked in this build, sounds above and below sound the same");
}

} //namespace ge
