//the game's memory, what every part of the port reads it with
#pragma once
#include "rt.h"
#include "video.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ge
{

const uint32_t kFrameCount = 0x806EBED0; //-0x50B0(r13), frames run
const uint32_t kPlayerPtr = 0x806EB698; //set when any player exists, tells play from menus
const uint32_t kPaused = 0x806EB69C; //a byte, 1 while the pause menu is up

inline bool guest_ptr(uint32_t p) {
    return (p >= 0x80003000u && p < 0x81800000u) || (p >= 0x90000000u && p < 0x90000000u + g_mem2_size);
}
inline float ldf(uint32_t a) { uint32_t b = ld32(a); float v; std::memcpy(&v, &b, 4); return v; }
inline void stf(uint32_t a, float v) { uint32_t b; std::memcpy(&b, &v, 4); st32(a, b); }
inline bool in_play() { return guest_ptr(ld32(kPlayerPtr)) && ld8(kPaused) == 0; }

//the guest's clock (rt_now) as a std::chrono clock: what goes by the game's own time, so a
//timer of ours keeps step with it (a skip's jumps, the deterministic mode's virtual clock)
struct GameClock
{
	using duration = std::chrono::steady_clock::duration;
	using rep = duration::rep;
	using period = duration::period;
	using time_point = std::chrono::steady_clock::time_point;
	static constexpr bool is_steady = true;
	static time_point now() { return rt_now(); }
};

} //namespace ge
