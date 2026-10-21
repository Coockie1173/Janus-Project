//sounds of the port's own: a .wav file played over the game's mix, at a place in the world
//
//the game's sounds are AX voices from its soundbanks. a file of the mods is mixed in afterwards,
//on the host (wiikit's audio_set_mixer: each block the AI plays, before it goes out), so it
//needs nothing of the game's sound memory. a sound with a place is as loud as the nearest
//listener (806474B0's five, see height.cpp) is close to it: all there within kOwn, gone at kFar,
//and panned by where it is around the listener's camera: its +0x34 axis is the right (+0x28 is
//its back, the player's own shot is half a metre the other way; fn_803983B0 gives the game's
//sounds their angle by the two)
//
//the game mixes quietly: its shots peak at about 2400 of 32768, the loudest of a level at 4650.
//a file is made for the full range, so it's played at kGameLevel: a full-scale shot peaks about
//where the game's own do
//
//any .wav of PCM 8, 16, 24 or 32 bits or of floats, mono or stereo, at any rate
//
//GE_TRACE=1 logs each file read and each sound played with its distance, gain and pan
#include "ge.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

namespace ge
{
namespace
{

struct Clip
{
	std::string m_Name;
	std::vector<float> m_Lr; //stereo frames, -1 to 1
	uint32_t m_Rate{};
};

struct Voice
{
	const Clip* m_pClip{};
	double m_At{}; //frames of the clip played
	float m_Left{}, m_Right{};
};

const uint32_t kListeners = 0x806474B0, kListenerFirst = 0xC, kListenerSize = 0xC0, kListenerHears = 0x2, kListenerEar = 0x1C, kListenerRight = 0x34;
const int kListenerCount = 5;
const float kOwn = 2.5f, kFar = 100.0f;
const float kGameLevel = 0.08f;
const float kPanWidth = 0.7f; //a sound fully to one side still has some of it in the other ear
const size_t kMostVoices = 24;

std::vector<std::unique_ptr<Clip>> g_clips; //read at start, kept for good: the voices point at them
std::mutex g_lock;
std::vector<Voice> g_voices;
std::vector<float> g_mix;
bool g_trace = std::getenv("GE_TRACE") != nullptr;

uint32_t Le(const std::vector<uint8_t>& d, size_t at, int bytes)
{
	uint32_t v = 0;
	for (int i = 0; i < bytes; ++i) v |= (uint32_t)d[at + i] << (8 * i);
	return v;
}

//one sample of the file as -1 to 1
float Sample(const std::vector<uint8_t>& d, size_t at, int bits, bool isFloat)
{
	if (isFloat)
	{
		const uint32_t v = Le(d, at, 4);
		float f;
		std::memcpy(&f, &v, 4);
		return f;
	}
	switch (bits)
	{
	case 8: return ((int)d[at] - 128) / 128.0f;
	case 16: return (int16_t)Le(d, at, 2) / 32768.0f;
	case 24: return (int32_t)(Le(d, at, 3) << 8) / 2147483648.0f;
	default: return (int32_t)Le(d, at, 4) / 2147483648.0f;
	}
}

bool ReadWav(const std::string& path, Clip& clip)
{
	std::ifstream in(path, std::ios::binary);
	const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;
	int format = 0, channels = 0, bits = 0;
	size_t data = 0, dataSize = 0;
	for (size_t at = 12; at + 8 <= d.size();)
	{
		const uint32_t size = Le(d, at + 4, 4);
		const size_t body = at + 8;
		if (!std::memcmp(d.data() + at, "fmt ", 4) && size >= 16 && body + 16 <= d.size())
		{
			format = (int)Le(d, body, 2);
			channels = (int)Le(d, body + 2, 2);
			clip.m_Rate = Le(d, body + 4, 4);
			bits = (int)Le(d, body + 14, 2);
			//extensible: the real format is the start of its sub-format
			if (format == 0xFFFE && size >= 26 && body + 26 <= d.size()) format = (int)Le(d, body + 24, 2);
		}
		else if (!std::memcmp(d.data() + at, "data", 4))
		{
			data = body;
			dataSize = std::min<size_t>(size, d.size() - body);
		}
		at = body + size + (size & 1);
	}
	const bool isFloat = format == 3 && bits == 32;
	if (!data || channels < 1 || !clip.m_Rate || (format != 1 && !isFloat) || (bits != 8 && bits != 16 && bits != 24 && bits != 32)) return false;
	const size_t bytes = (size_t)bits / 8, frame = bytes * channels, frames = dataSize / frame;
	clip.m_Lr.resize(frames * 2);
	for (size_t i = 0; i < frames; ++i)
	{
		const size_t at = data + i * frame;
		const float left = Sample(d, at, bits, isFloat);
		clip.m_Lr[i * 2] = left;
		clip.m_Lr[i * 2 + 1] = channels > 1 ? Sample(d, at + bytes, bits, isFloat) : left;
	}
	return frames > 0;
}

//on the audio thread
void Mix(int16_t* lr, uint32_t frames, uint32_t rate)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_voices.empty()) return;
	g_mix.assign((size_t)frames * 2, 0.0f);
	for (Voice& v : g_voices)
	{
		const std::vector<float>& s = v.m_pClip->m_Lr;
		const size_t count = s.size() / 2;
		const double step = (double)v.m_pClip->m_Rate / rate;
		for (uint32_t i = 0; i < frames; ++i, v.m_At += step)
		{
			const size_t at = (size_t)v.m_At;
			if (at + 1 >= count)
			{
				v.m_At = (double)count;
				break;
			}
			const float t = (float)(v.m_At - (double)at);
			g_mix[i * 2] += (s[at * 2] + (s[at * 2 + 2] - s[at * 2]) * t) * v.m_Left;
			g_mix[i * 2 + 1] += (s[at * 2 + 1] + (s[at * 2 + 3] - s[at * 2 + 1]) * t) * v.m_Right;
		}
	}
	g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(), [](const Voice& v) { return v.m_At + 1 >= v.m_pClip->m_Lr.size() / 2; }), g_voices.end());
	for (size_t i = 0; i < g_mix.size(); ++i) lr[i] = (int16_t)std::clamp(lr[i] + g_mix[i] * 32767.0f, -32768.0f, 32767.0f);
}

} //namespace

int custom_sound_load(const std::string& path)
{
	for (size_t i = 0; i < g_clips.size(); ++i)
		if (g_clips[i]->m_Name == path) return (int)i;
	auto clip = std::make_unique<Clip>();
	clip->m_Name = path;
	if (!ReadWav(path, *clip))
	{
		rt_log("goldeneye: sound: %s isn't a .wav that can be played", path.c_str());
		return -1;
	}
	if (g_trace) rt_log("goldeneye: sound: %s, %.2f s at %u Hz", path.c_str(), clip->m_Lr.size() / 2.0 / clip->m_Rate, clip->m_Rate);
	if (g_clips.empty()) audio_set_mixer(Mix);
	g_clips.push_back(std::move(clip));
	return (int)g_clips.size() - 1;
}

void custom_sound_play(int sound, const float* pPlace, float volume)
{
	if (sound < 0 || sound >= (int)g_clips.size()) return;
	float gain = volume * kGameLevel, pan = 0.0f, distance = 0.0f;
	const uint32_t listeners = ld32(kListeners);
	if (pPlace && guest_ptr(listeners))
	{
		distance = -1.0f;
		for (int i = 0; i < kListenerCount; ++i)
		{
			const uint32_t l = listeners + kListenerFirst + i * kListenerSize;
			if (!ld8(l) || !ld8(l + kListenerHears)) continue;
			float way[3];
			for (int k = 0; k < 3; ++k) way[k] = pPlace[k] - ldf(l + kListenerEar + k * 4);
			const float d = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
			if (distance >= 0.0f && d >= distance) continue;
			distance = d;
			pan = 0.0f;
			if (d > kOwn)
				for (int k = 0; k < 3; ++k) pan += way[k] / d * ldf(l + kListenerRight + k * 4);
		}
		if (distance > kOwn)
		{
			const float left = std::clamp((kFar - distance) / (kFar - kOwn), 0.0f, 1.0f);
			gain *= left * left;
		}
	}
	if (g_trace) rt_log("goldeneye: sound: %s at %.1f m, gain %.2f, pan %.2f", g_clips[sound]->m_Name.c_str(), distance, gain, pan);
	if (gain <= 0.0f) return;
	pan = std::clamp(pan, -1.0f, 1.0f) * kPanWidth;
	Voice v;
	v.m_pClip = g_clips[sound].get();
	v.m_Left = gain * std::min(1.0f, 1.0f - pan);
	v.m_Right = gain * std::min(1.0f, 1.0f + pan);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_voices.size() == kMostVoices) g_voices.erase(g_voices.begin()); //the oldest makes room
	g_voices.push_back(v);
}

} //namespace ge
