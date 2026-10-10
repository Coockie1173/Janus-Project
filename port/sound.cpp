//a sound starts silent, so it doesn't click at higher frame rates
//
//the game's sound update (fn_803A46D0, once a frame) makes a new sound's voice (fn_803A54A0):
//an AX voice and its channel of the SDK's MIX library, set up with MIXInitChannel at 0 dB in
//the middle. the volume and pan it should have are set right after, and MIX gets there the
//way it always does: in the AX frame callback (fn_803A41E0, every 3 ms) MIXUpdateSettings
//writes a ramp from where the channel is to where it should be, which the mixer plays in the
//frame after. as long as the voice isn't running the callback asks for -96 dB
//
//the voice is set running by the game's update one frame later. on the console that's 33 ms,
//eleven AX frames: the channel has long gone from 0 dB to silence and the sound fades in from
//there. above 166 frames a second there are less than two AX frames in between, the voice
//runs while the ramp down from 0 dB is still to be played and the sound starts with 3 ms at
//full volume, 20 times what the menu's cursor should be: a click, and with all the sounds of
//a fight a loud crackle. it gets more likely the higher the rate is
//
//so the channel is made at -96 dB, where the console has it by the time the voice runs
#include "ge.h"
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kSilent = 0xFFFFFC40; //-96.0 dB, MIX counts in tenths
PPCFunc orig_init_channel = nullptr;

//MIXInitChannel(voice, mode, input, aux A, aux B, aux C, pan, surround pan, fader)
void InitChannel(PPCContext& c)
{
	c.r[5] = kSilent;
	orig_init_channel(c);
}

} //namespace

void install_sound()
{
	if (std::getenv("GE_SOUND_CLICKS")) return; //the game's own, to compare
	orig_init_channel = ppc_hook("MIXInitChannel", InitChannel);
	if (!orig_init_channel) rt_log("goldeneye: MIXInitChannel is not hooked in this build, sounds click at high frame rates");
}

} //namespace ge
