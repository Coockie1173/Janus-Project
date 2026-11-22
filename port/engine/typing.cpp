//a typed text comes a character a console frame at any frame rate
//
//the texts that are typed out (a mission's place and time over its opening, a new objective,
//a checkpoint) are one kind of object (vtable 8057A900): the string at +4, how many of its
//characters are shown at +0x6C and how many it has at +0x70. ge_typed_text_update (801A68C0,
//once a frame for each that is up) shows one more for every call until they are all there,
//starting and stopping the typing sound at the spaces, and then counts the time the text
//stays (+0x60) down by the frame's length
//
//so the typing took the same number of frames at any rate: a second for 30 characters on the
//console, a fifth of that at 144. the time it stays was right already
//
//above 30 a text that is still being typed is only updated when a console frame's time has
//gone by since its last character
//GE_TRACETYPE=1 log how long each text took to type
#include "ge.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kHash = 0x64, kShown = 0x6C, kLength = 0x70;
PPCFunc orig_typed_update = nullptr;
bool g_trace = false;

struct Typing
{
	float due; //console frames gone by that no character was shown for
	uint32_t frames; //calls since its first character
	std::chrono::steady_clock::time_point since;
};
std::unordered_map<uint32_t, Typing> g_typing; //the texts being typed

//(text)
void TypedUpdate(PPCContext& c)
{
	uint32_t text = c.r[3];
	float rate = engine_rate();
	if (!guest_ptr(text) || (int32_t)ld32(text + kShown) >= (int32_t)ld32(text + kLength))
	{
		auto it = g_typing.find(text);
		if (it != g_typing.end())
		{
			if (g_trace)
				rt_log("goldeneye: text %08X, %u characters, was typed in %u frames, %.2f s", ld32(text + kHash), ld32(text + kLength), it->second.frames,
					std::chrono::duration<double>(std::chrono::steady_clock::now() - it->second.since).count());
			g_typing.erase(it);
		}
		orig_typed_update(c);
		return;
	}
	//a text that starts again is typed from its first frame
	auto it = g_typing.find(text);
	if (it == g_typing.end() || ld32(text + kShown) == 0) it = g_typing.insert_or_assign(text, Typing{1.0f, 0, std::chrono::steady_clock::now()}).first;
	else it->second.due += rate > kGameFps ? kGameFps / rate : 1.0f;
	++it->second.frames;
	if (it->second.due < 1.0f - 1e-3f) return;
	it->second.due -= 1.0f;
	orig_typed_update(c);
}

} //namespace

void install_typing()
{
	rt_state_register("typing", [](StateIO& io) {
		std::vector<std::pair<uint32_t, std::pair<float, uint32_t>>> v;
		for (const auto& [text, t] : g_typing) v.push_back({text, {t.due, t.frames}});
		std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		io.vec(v);
		if (!io.applying) return;
		g_typing.clear();
		for (const auto& [text, t] : v) g_typing[text] = Typing{t.first, t.second, std::chrono::steady_clock::now()};
	});
	g_trace = std::getenv("GE_TRACETYPE") != nullptr;
	orig_typed_update = ppc_hook("ge_typed_text_update", TypedUpdate);
	if (!orig_typed_update) rt_log("goldeneye: ge_typed_text_update is not hooked in this build, typed texts get faster with the frame rate");
}

} //namespace ge
