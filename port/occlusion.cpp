//a sound behind a wall or a floor is quieter
//
//the game's sounds only know how far away they are: a sound (fn_80391090 updates each one, once
//a sound frame, on the game's thread) has its place at +0xCC and a near and a far range at +0xC4
//and +0xC8, and nothing in between counts. another player's footsteps are 3 to 35, so someone a
//floor up, four metres away, sounds the same as someone standing next to you. the engine has an
//input for it (fn_80397DB0, 16 wall distances around a listener) that the game never feeds
//
//here the level's collision is asked: from the listener (the five at 806474B0's +0xC, 0xC0
//each, the byte at +0 says it's there, its place at +4 and the point it hears from at +0x1C) to
//the sound, with the game's own trace. two of them, a bit above the sound's place and higher
//(a footstep's place is on the floor), so a sound half behind a corner is half as much in the
//way. what is hit right at the sound doesn't count, that's what it sits on
//
//a sound that's in the way loses kWall decibels behind a wall and kFloor straight above or
//below, where it's a floor that is in between. the sound's own value for it is +0x1F0, 0 as
//loud as it is and 1 silent, and what is heard goes by about the power kPower of what's left
//of it (measured: this player's own steps with 0.05, 0.1, 0.15 and 0.2 added came out 2.6, 4.9,
//8 and 10.4 dB below the others in a recording). the game puts its distance in there:
//fn_80390A50 adds by the byte at +0x17 of the sound's entry (+0x66C), the low 6 bits of 63 how
//much at the far range and the top two how: 2 evenly from near to far, 3 with the square
//(footsteps: half at the far range, with the square), anything else not by distance. the same
//is worked out here, and the decibels are taken off what the distance leaves. the working
//value is copied from the set one (+0x11C) as the update begins, so the set one is raised
//before the update and put back after it
//
//a sound within kOwn of the listener is its own (its steps are 1.5 below the camera). one
//with a range past kFarthest (music, the menus: 300 and 5000) or one the game doesn't make
//quieter by distance (2D7011AF, 30 to 30 at 0 -10 0 in every level) is not in the world.
//people and doors aren't in the trace
//
//GE_OCCLUSION=0 the game's own
//GE_OCCLUSION_FORCE=1 everything in the world counts as fully in the way, to hear what it does
//GE_TRACE=1 log each sound that starts in the way
#include "ge.h"
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kListeners = 0x806474B0, kListenerFirst = 0xC, kListenerSize = 0xC0, kListenerPlace = 0x4, kListenerEar = 0x1C;
const int kListenerCount = 5;
const uint32_t kSoundNear = 0xC4, kSoundFar = 0xC8, kSoundPlace = 0xCC, kSoundEntry = 0x66C, kSoundHash = 0x670;
const uint32_t kSoundQuieter = 0x11C; //what the sound is set to: 0 as loud as it is, 1 silent
const uint32_t kEntryFalloff = 0x17;
const float kOwn = 2.5f, kFarthest = 150.0f;
const float kLow = 0.4f, kHigh = 1.1f; //above the sound's place, where the two traces end
const float kShort = 0.35f; //a hit this close to the sound is what it sits on
const float kWall = 9.0f, kFloor = 16.0f; //decibels a wall and a floor take off
const float kPower = 5.5f; //what is heard is this power of what the sound's value leaves
const double kEvery = 0.08, kEase = 0.12; //seconds: between a sound's traces, and to get to what they say
const int kHeardCount = 128;

struct Heard
{
	uint32_t m_Sound = 0, m_Hash = 0;
	float m_Blocked = 0.0f, m_Wanted = 0.0f;
	double m_Traced = 0.0, m_Updated = 0.0;
};

PPCFunc orig_update = nullptr;
bool g_on = true, g_force = false, g_trace = false;
Heard g_heard[kHeardCount];

double Now()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

//how much of the way from the listener to the sound the level blocks, 0 to 1
float Traced(const PPCContext& c, const float listener[3], const float place[3])
{
	float blocked = 0.0f;
	for (const float above : { kLow, kHigh })
	{
		const float way[3] = { place[0] - listener[0], place[1] + above - listener[1], place[2] - listener[2] };
		const float length = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
		if (length <= kShort) continue;
		const float hit = world_trace(c, listener, way);
		if (hit >= 0.0f && hit * length < length - kShort) blocked += 0.5f;
	}
	return blocked;
}

//how much quieter the game makes this sound at a distance, as fn_80390A50 does. negative for
//one it doesn't
float Falloff(uint32_t sound, float distance)
{
	const float near = ldf(sound + kSoundNear), far = ldf(sound + kSoundFar);
	const uint32_t entry = ld32(sound + kSoundEntry);
	const uint8_t falloff = guest_ptr(entry) ? ld8(entry + kEntryFalloff) : 0;
	const int how = falloff >> 6;
	if (how < 2 || !(near < far)) return -1.0f;
	float closeness = distance >= far ? 0.0f : distance <= near ? 1.0f : 1.0f - (distance - near) / (far - near);
	if (how == 3) closeness *= closeness;
	return (1.0f - closeness) * ((falloff & 0x3F) / 63.0f);
}

//what is added to the sound's value this update, 0 for one that's heard as the game has it
float Quieter(const PPCContext& c, uint32_t sound)
{
	const float far = ldf(sound + kSoundFar);
	if (!(far > 0.0f) || far > kFarthest) return 0.0f;
	const float place[3] = { ldf(sound + kSoundPlace), ldf(sound + kSoundPlace + 4), ldf(sound + kSoundPlace + 8) };

	const uint32_t listeners = ld32(kListeners);
	if (!guest_ptr(listeners)) return 0.0f;
	float listener[3] = {}, distance = -1.0f;
	for (int i = 0; i < kListenerCount; ++i)
	{
		const uint32_t l = listeners + kListenerFirst + i * kListenerSize;
		if (!ld8(l)) continue;
		const float x = ldf(l + kListenerEar) - place[0], y = ldf(l + kListenerEar + 4) - place[1], z = ldf(l + kListenerEar + 8) - place[2];
		const float d = std::sqrt(x * x + y * y + z * z);
		if (distance >= 0.0f && d >= distance) continue;
		distance = d;
		for (int k = 0; k < 3; ++k) listener[k] = ldf(l + kListenerPlace + k * 4);
	}
	if (distance < kOwn || distance > far) return 0.0f;
	const float own = Falloff(sound, distance);
	if (own < 0.0f) return 0.0f;

	const uint32_t hash = ld32(sound + kSoundHash);
	Heard& heard = g_heard[(sound >> 4) % kHeardCount];
	const double now = Now();
	const bool fresh = heard.m_Sound != sound || heard.m_Hash != hash || now - heard.m_Updated > 0.5;
	if (fresh || now - heard.m_Traced >= kEvery)
	{
		heard.m_Wanted = g_force ? 1.0f : Traced(c, listener, place);
		heard.m_Traced = now;
		//a new sound starts where it should be: a footstep is over before it would have eased in
		if (fresh) { heard.m_Sound = sound; heard.m_Hash = hash; heard.m_Blocked = heard.m_Wanted; }
	}
	if (!fresh) heard.m_Blocked += (heard.m_Wanted - heard.m_Blocked) * (float)std::fmin(1.0, (now - heard.m_Updated) / kEase);
	heard.m_Updated = now;
	if (heard.m_Blocked <= 0.01f) return 0.0f;

	//straight above or below is a floor, level with the listener a wall
	const float upright = std::fmin(1.0f, std::fabs(place[1] + kLow - listener[1]) / distance);
	const float decibels = heard.m_Blocked * (kWall + (kFloor - kWall) * upright);
	const float quieter = (1.0f - own) * (1.0f - std::pow(10.0f, -decibels / (20.0f * kPower)));
	if (fresh && g_trace) rt_log("goldeneye: sound %08X at %.1f %.1f %.1f, %.1f away, is %.0f%% behind the level: %.1f dB off, %.3f on the game's %.3f", hash, place[0], place[1], place[2], distance, heard.m_Blocked * 100.0f, decibels, quieter, own);
	return quieter;
}

void Update(PPCContext& c)
{
	const uint32_t sound = c.r[3];
	const float quieter = g_on && in_play() ? Quieter(c, sound) : 0.0f;
	if (quieter <= 0.0f) { orig_update(c); return; }
	const float set = ldf(sound + kSoundQuieter);
	stf(sound + kSoundQuieter, set + quieter);
	orig_update(c);
	stf(sound + kSoundQuieter, set);
}

} //namespace

bool occlusion() { return g_on; }
void set_occlusion(bool on) { g_on = on; }

void install_occlusion()
{
	if (const char* e = std::getenv("GE_OCCLUSION")) g_on = std::atoi(e) != 0;
	g_force = std::getenv("GE_OCCLUSION_FORCE") != nullptr;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_update = ppc_hook("ge_sound_update", Update);
	if (!orig_update) rt_log("goldeneye: ge_sound_update is not hooked in this build, sounds are heard through walls as on the console");
}

} //namespace ge
