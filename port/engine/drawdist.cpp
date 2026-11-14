//draw distance: details stay in view further out
//
//an entity with levels of detail (its row's +0x10 list, rows of 0x18: the entity, the distance
//it's drawn from, flags at +0x16) is picked by ge_lod_pick (8033A940, f1 the distance from
//fn_8032ECE0: the nearest camera's, times its field of view's ratio). it returns how many to
//draw, 0 for none: past the last row (a list ending in an empty one, or the 0x11 kind that fades
//them out through fn_8033AB40) the thing isn't drawn. that's the pop-in of Outpost's rocks: at
//its start a quarter of the picks come back empty
//
//what's picked only changes the picture: the guards' spawners ask whether a room's data is loaded
//(+0x6A of its state), which doesn't come from here
//
//Detail Distance (a slider, 1x the game's up to Unlimited): the game's pick stands whenever it
//draws something. only when it comes back empty is it picked again from the distance divided by
//the slider (Unlimited: from right next to it). so whatever the game draws is drawn as it picks
//it, and something meant to show only from far away keeps doing so
//GE_DRAWDIST=N the same from the environment
//GE_LODTRACE=1 every 600 picks a line: how many came out drawn at a farther level, or not at all
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>

namespace ge
{
namespace
{

PPCFunc orig_pick = nullptr;
std::atomic<float> g_scale{1.0f};
bool g_trace = false;
uint32_t g_picks = 0, g_hidden = 0, g_far = 0;

//(distance in f1, the pick, the entity, the first row it may stop at, the rows it may take)
//-> how many it picked
void LodPick(PPCContext& c)
{
	float scale = g_scale;
	uint32_t pick = c.r[3];
	PPCContext entry;
	if (scale > 1.0f) entry = c;
	orig_pick(c);
	if (scale > 1.0f && !c.r[3])
	{
		c = entry;
		c.f[1] = scale >= kDrawDistanceMax ? 0.0 : c.f[1] / scale;
		orig_pick(c);
	}
	if (!g_trace) return;
	++g_picks;
	if (!c.r[3] || !ld32(pick)) ++g_hidden;
	else if (ld16(pick + 4)) ++g_far;
	if (g_picks == 600)
	{
		rt_log("lod: 600 picks at %.1fx, %u not drawn, %u at a farther level", scale, g_hidden, g_far);
		g_picks = g_hidden = g_far = 0;
	}
}

} //namespace

float draw_distance() { return g_scale; }
void set_draw_distance(float scale) { g_scale = std::clamp(scale, 1.0f, kDrawDistanceMax); }

void install_draw_distance()
{
	if (std::string saved = saved_setting("GE_DRAWDIST", "LAUNCH_DRAWDIST"); !saved.empty()) set_draw_distance((float)std::atof(saved.c_str()));
	g_trace = std::getenv("GE_LODTRACE") != nullptr;
	orig_pick = ppc_hook("ge_lod_pick", LodPick);
	if (!orig_pick) rt_log("goldeneye: ge_lod_pick is not hooked in this build, Detail Distance does nothing");
}

} //namespace ge
