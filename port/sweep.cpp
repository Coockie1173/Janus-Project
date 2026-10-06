//a moving shape's sweep: its steps are kept to a number that ends
//
//a physics shape that moved since the last step is swept: ge_sweep_update (80351AD0) works
//out how many places between the old and the new position get looked at, the count at +0x58:
//the way its body went (+0x68 now, +0x78 before, of the body at +0x40, or +0x44 for one that
//doesn't turn) over 0.75 of the shape's size (+0x64). nothing keeps that number down
//fn_80354500 then walks the count for every query (fn_803580A0 calls it up to r5 + 1 times a
//shape, the world's step fn_80364B20 for every shape)
//a body that the physics threw far in one step gets millions of them: in the tank mission
//(level 01810145, 144 frames a second, build/crashes run(5).log) one frame took 35 s and the
//next never ended, the main thread in fn_80354500 under fn_803580A0 all along
//so the count is kept to kMaxSteps, and a body that asked for more is logged
#include "ge.h"
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kShapeMove = 0x20, kShapeTurningBody = 0x40, kShapeBody = 0x44, kShapeSteps = 0x58, kShapeSize = 0x64;
const uint32_t kBodyPlace = 0x68, kBodyPlaceBefore = 0x78;
const uint32_t kCurrentLevel = 0x805D1804;
const uint32_t kMaxSteps = 64;
const int kMaxReports = 32;
PPCFunc orig_sweep_update = nullptr;
bool g_log = false;
uint32_t g_most = 0;
int g_reports = 0;

//(shape)
void SweepUpdate(PPCContext& c)
{
	uint32_t shape = c.r[3];
	orig_sweep_update(c);
	uint32_t steps = ld32(shape + kShapeSteps);
	if (g_log && steps > g_most)
	{
		g_most = steps;
		rt_log("goldeneye: sweep of %u steps (shape %08X, frame %u)", steps, shape, ld32(kFrameCount));
	}
	if (steps <= kMaxSteps) return;
	st32(shape + kShapeSteps, kMaxSteps);
	if (g_reports++ >= kMaxReports) return;
	uint32_t body = ld32(shape + kShapeTurningBody);
	if (!body) body = ld32(shape + kShapeBody);
	if (!guest_ptr(body)) body = 0;
	rt_log("goldeneye: a shape's sweep of %u steps is cut to %u (shape %08X size %g, it moved %g %g %g; body %08X from %g %g %g to %g %g %g, level %08X frame %u)",
		steps, kMaxSteps, shape, ldf(shape + kShapeSize), ldf(shape + kShapeMove), ldf(shape + kShapeMove + 4), ldf(shape + kShapeMove + 8), body,
		body ? ldf(body + kBodyPlaceBefore) : 0, body ? ldf(body + kBodyPlaceBefore + 4) : 0, body ? ldf(body + kBodyPlaceBefore + 8) : 0,
		body ? ldf(body + kBodyPlace) : 0, body ? ldf(body + kBodyPlace + 4) : 0, body ? ldf(body + kBodyPlace + 8) : 0,
		ld32(kCurrentLevel), ld32(kFrameCount));
}

} //namespace

void install_sweep()
{
	g_log = std::getenv("GE_SWEEPLOG") != nullptr;
	orig_sweep_update = ppc_hook("ge_sweep_update", SweepUpdate);
	if (!orig_sweep_update) rt_log("goldeneye: ge_sweep_update is not hooked in this build, a shape thrown far can hang the game");
}

} //namespace ge
