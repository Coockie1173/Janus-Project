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
//
//a body whose place isn't a number is worse: the faces around it are scooped up by a box
//test (fn_802CEB00 under fn_80352010) that nothing fails with a nan, the 512 there's room
//for are full at once, and the game prints "EXCollisionScoopTemp::DoCollisionPlatform. Attempt
//to scoop more than 512 faces" and stops in its halt (ge_halt, 80297C20, a loop on itself):
//a mod level in a network game froze that way (build/crashes run.log of 2026-10-07, frame
//23542, a body of size 0.1 that was nan the step before as well)
//so the step of such an object (ge_object_step, the world's step calls it for each) is left
//out and what it is gets logged, and the halt ends the game with a report instead of a
//window that stands still
#include "ge.h"
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kShapeMove = 0x20, kShapeTurningBody = 0x40, kShapeBody = 0x44, kShapeSteps = 0x58, kShapeSize = 0x64;
const uint32_t kBodyPlace = 0x68, kBodyPlaceBefore = 0x78;
const uint32_t kCurrentLevel = 0x805D1804;
const uint32_t kObjectShape = 0x118; //the object's swept shape
const uint32_t kBodyTurn = 0x190; //a turning body's quaternion
const uint32_t kMaxSteps = 64;
const int kMaxReports = 32;
PPCFunc orig_sweep_update = nullptr, orig_object_step = nullptr, orig_halt = nullptr;
int g_lost_reports = 0;
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

uint32_t ShapeBody(uint32_t shape)
{
	uint32_t body = ld32(shape + kShapeTurningBody);
	if (!body) body = ld32(shape + kShapeBody);
	return guest_ptr(body) ? body : 0;
}

bool Finite(uint32_t at)
{
	return std::isfinite(ldf(at)) && std::isfinite(ldf(at + 4)) && std::isfinite(ldf(at + 8));
}

//(object, f1 time step)
void ObjectStep(PPCContext& c)
{
	uint32_t object = c.r[3], shape = guest_ptr(object) ? ld32(object + kObjectShape) : 0;
	uint32_t body = guest_ptr(shape) ? ShapeBody(shape) : 0;
	if (!body || Finite(body + kBodyPlace))
	{
		orig_object_step(c);
		return;
	}
	if (g_lost_reports++ >= kMaxReports) return;
	bool turning = ld32(shape + kShapeTurningBody) != 0;
	rt_log("goldeneye: a physics object's place isn't a number, its step is left out (object %08X vtable %08X +4 %08X, shape %08X size %g, %s body %08X vtable %08X flags %08X "
		"at %g %g %g, before %g %g %g, turned %g %g %g %g, level %08X frame %u)",
		object, ld32(object), ld32(object + 4), shape, ldf(shape + kShapeSize), turning ? "turning" : "plain", body, ld32(body), ld32(body + 0x58),
		ldf(body + kBodyPlace), ldf(body + kBodyPlace + 4), ldf(body + kBodyPlace + 8),
		ldf(body + kBodyPlaceBefore), ldf(body + kBodyPlaceBefore + 4), ldf(body + kBodyPlaceBefore + 8),
		turning ? ldf(body + kBodyTurn) : 0, turning ? ldf(body + kBodyTurn + 4) : 0, turning ? ldf(body + kBodyTurn + 8) : 0, turning ? ldf(body + kBodyTurn + 12) : 0,
		ld32(kCurrentLevel), ld32(kFrameCount));
}

void Halt(PPCContext&)
{
	rt_die("goldeneye: the game stopped itself (its halt, 80297C20: the line it printed before says why), level %08X frame %u", ld32(kCurrentLevel), ld32(kFrameCount));
}

} //namespace

void install_sweep()
{
	g_log = std::getenv("GE_SWEEPLOG") != nullptr;
	orig_sweep_update = ppc_hook("ge_sweep_update", SweepUpdate);
	orig_object_step = ppc_hook("ge_object_step", ObjectStep);
	orig_halt = ppc_hook("ge_halt", Halt);
	if (!orig_object_step) rt_log("goldeneye: ge_object_step is not hooked in this build, a physics object that isn't anywhere can stop the game");
	if (!orig_sweep_update) rt_log("goldeneye: ge_sweep_update is not hooked in this build, a shape thrown far can hang the game");
}

} //namespace ge
