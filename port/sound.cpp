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
//
//an animation's sound plays once at any frame rate
//
//an animation has events at its frames, a sound among them (kind 6, fn_8031B560 from the
//events' dispatch fn_8031AEC0). the event's slot remembers what it started and isn't started
//again while that lives. the player's own animations (the gun's reload: the magazine out, in,
//the slide) give the sound to ge_player_event_sound first, which plays it at the player with
//no place in the world and answers that it did: the slot is left with nothing in it, and the
//event is started again on every frame the animation is still at that frame of its own
//the animation goes by time, so on the console that is one frame and the sound plays once. at
//144 it is five frames and five of the same sound on top of each other, louder and smeared
//
//so an event's sound that played on the frame before isn't played again, for as many frames
//as a console frame lasts and one more. at 30 too: there the animation is now and then still
//at the event's frame a second time (seen: the magazine's sound on frames 2386 and 2387)
//GE_TRACESOUND=1 log every sound whose details are looked up, with who asked
#include "ge.h"
#include <cstdlib>
#include <unordered_map>

namespace ge
{
namespace
{

const uint32_t kSilent = 0xFFFFFC40; //-96.0 dB, MIX counts in tenths
PPCFunc orig_init_channel = nullptr, orig_sound_details = nullptr;
PPCFunc orig_player_event_sound = nullptr;
const uint32_t kEventGap = 2; //frames apart that are still the same run: one in a run was seen left out
struct EventSound { uint32_t first, last; }; //the frames it played on and was last asked for
std::unordered_map<uint64_t, EventSound> g_event_sounds; //the player's sounds and the event -> its last run

//MIXInitChannel(voice, mode, input, aux A, aux B, aux C, pan, surround pan, fader)
void InitChannel(PPCContext& c)
{
	c.r[5] = kSilent;
	orig_init_channel(c);
}

//(player's sounds, the animation's owner, the event, the sound's hash) -> it was played here
void PlayerEventSound(PPCContext& c)
{
	const uint64_t key = (uint64_t)c.r[3] << 32 | c.r[5];
	const uint32_t frame = ld32(kFrameCount);
	const float rate = engine_rate();
	auto it = g_event_sounds.find(key);
	if (it != g_event_sounds.end() && frame - it->second.last <= kEventGap && (float)(frame - it->second.first) < rate / kGameFps + 1.0f)
	{
		it->second.last = frame;
		c.r[3] = 1;
		return;
	}
	orig_player_event_sound(c);
	if (!(c.r[3] & 0xFF)) return;
	if (g_event_sounds.size() > 256) g_event_sounds.clear();
	g_event_sounds[key] = EventSound{frame, frame};
}

//(sound manager, a sound's hash) -> its details
void SoundDetails(PPCContext& c)
{
	rt_log("sound: %08X frame %u: %s", c.r[4], ld32(kFrameCount), call_chain(c).c_str());
	orig_sound_details(c);
}

} //namespace

void install_sound()
{
	if (std::getenv("GE_TRACESOUND")) orig_sound_details = ppc_hook("ge_sound_details", SoundDetails);
	if (!std::getenv("GE_SOUND_REPEATS")) //the game's own, to compare
	{
		orig_player_event_sound = ppc_hook("ge_player_event_sound", PlayerEventSound);
		if (!orig_player_event_sound) rt_log("goldeneye: ge_player_event_sound is not hooked in this build, a reload's sounds play several times at high frame rates");
	}
	if (std::getenv("GE_SOUND_CLICKS")) return; //the game's own, to compare
	orig_init_channel = ppc_hook("MIXInitChannel", InitChannel);
	if (!orig_init_channel) rt_log("goldeneye: MIXInitChannel is not hooked in this build, sounds click at high frame rates");
}

} //namespace ge
