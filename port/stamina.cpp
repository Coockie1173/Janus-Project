//the stamina meter (a cheat, off by default): the sprint that's left as a bar at the bottom of
//the picture, with a line where a sprint is best cancelled
//
//how the game keeps it: the controller's float at +0xE5C is the sprint left in seconds, the
//byte at +0xE59 is set while sprinting
//- sprinting, ge_sprint_update takes the frame's time (+0x578) off it
//- not sprinting, PlayerSprint refills it: full is 3.1 (806E9490, or 806E9494 with the byte at
//  806EBA7D set, 3.1 as well), a second gives back a tenth of that (806EEC80 is the 10). a
//  refill that gets from under that tenth (0.31) to it or past it makes it 0.36 of full
//  (806EEC84), 1.116 seconds, at once. then it's kept between 0 and full
//- a new sprint needs 1.0 (806EEC20, in fn_8010D6D0)
//so an empty sprint is back at 1.1 seconds after a second, and one cancelled right under 0.31
//(a reload ends it) is there a frame later: sprinting again the frame after loses next to
//nothing. the line is at that tenth, the bar turns green under it
//above the line a cancel only gets the slow refill, and the further under it the longer the
//wait: a frame's refill is 0.31 of what a frame of sprinting takes, so the first frame under
//the line is the one
//
//the tank isn't a player that sprints, it has other things there (the caller leaves it out)
//
//GE_STAMINA=1 start with it on
//GE_TRACE=1 log a sprint's start and end and every jump of the refill
#include "ge.h"
#include <atomic>
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{
const uint32_t kSprinting = 0xE59, kSprintLeft = 0xE5C;
const uint32_t kSprintFull = 0x806E9490, kSprintFullOther = 0x806E9494, kSprintOther = 0x806EBA7D;
const uint32_t kRefillParts = 0x806EEC80; //full over this is a second's refill, and where the refill jumps

std::atomic<bool> g_meter{false};
//the local player's as of its last update, read by the renderer's thread
std::atomic<float> g_left{0}, g_full{0}, g_cancel{0};
std::atomic<bool> g_sprinting{false};
std::atomic<uint32_t> g_frame{~0u};
bool g_trace = false;

void Trace(float left, bool sprinting)
{
	const float was{ g_left };
	if (ld32(kFrameCount) - g_frame >= 4) return; //the first update of a level has nothing before it
	if (sprinting != g_sprinting) rt_log("goldeneye: stamina: sprint %s with %.3f left", sprinting ? "starts" : "ends", left);
	else if (left > was + 0.5f) rt_log("goldeneye: stamina: the refill jumped from %.3f to %.3f", was, left);
}
} //namespace

bool stamina_meter() { return g_meter; }
void set_stamina_meter(bool on) { g_meter = on; }

void stamina_apply(uint32_t ctrl)
{
	if (!g_meter) return;
	const float left{ ldf(ctrl + kSprintLeft) };
	const float full{ ldf(ld8(kSprintOther) ? kSprintFullOther : kSprintFull) };
	const float parts{ ldf(kRefillParts) };
	if (!std::isfinite(left) || !std::isfinite(full) || !(full > 0) || !(parts > 1)) return;
	if (g_trace) Trace(left, ld8(ctrl + kSprinting) != 0);
	g_left = left;
	g_full = full;
	g_cancel = full / parts;
	g_sprinting = ld8(ctrl + kSprinting) != 0;
	g_frame = ld32(kFrameCount);
}

bool stamina(Stamina& s)
{
	if (!g_meter || !in_play() || !local_controller() || ld32(kFrameCount) - g_frame >= 4) return false;
	s.left = g_left;
	s.full = g_full;
	s.cancel = g_cancel;
	s.sprinting = g_sprinting;
	return true;
}

void install_stamina()
{
	if (const char* e = std::getenv("GE_STAMINA")) g_meter = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
}

} //namespace ge
