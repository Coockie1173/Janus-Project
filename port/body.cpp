//the player's body against the level at frame rates above the game's 30
//
//a body (the entity's +0x158, its entity at +4) is moved by its controller, position += speed
//times the time step, and ge_body_update (800F0FC0) then has ge_body_collide (800F12D0) push
//it out of the triangles it's in: ge_contacts_solve (802CF760), two passes over the contacts,
//the push at +0xEC, flags at +0xA (1 on a floor, 2 at a wall, 8 under a ceiling)
//how far it gets pushed depends on how deep the step went in, so a step isn't the sum of
//smaller ones, and the levels lean on that:
//- a vent's frame is a hair smaller than the body (level 01810150's exit, mesh 912CAF00 under
//  a low ceiling). a 30th of a second's step goes deep enough to come out the far side, with
//  a jitter you can see on the console too. a 144th's gets pushed straight back
//- the lip into level 01810144's vent is 10 cm. the console's step takes it in one frame, a
//  144th's lifts the body 2 cm and leaves it there
//- a ladder (the carrier's, level 01810147) takes a body that's in the air next to it:
//  PlayerMove (8010DBE0) only looks for one with the body's flags at 0, then fn_801141F0
//  starts state 0x18. the console's step goes off the edge, a 144th's stays at a wall there
//
//the game has nothing that says vent or ladder before it's on one, and neither crouching, a
//ceiling nor a lift is common to the three. what is: the console's step gets somewhere
//so a body that's pushed at something and gets nowhere tries the console's steps: it stays put
//until a 30th of a second's moves have added up and takes them as one, the drop that keeps it
//on the floor (6 a second) included. that goes on while they get it somewhere, until it's
//clear of the walls, isn't pushed any more or has slid along one for half a second
//at a real wall they get nowhere either: after four of them it's back to a step a frame, and
//tries again a quarter of a second later if it's still pushed. the body doesn't move there
//only walking and sprinting: a cutscene (state 0x22) and every other state has the body to itself
//and not with noclip on: nothing is in the way then, and holding the body back would hold the flight back
#include "ge.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kBodyEntity = 4, kEntityPos = 0xF8;
const uint32_t kBodyFlags = 0xA, kBodyAtWall = 0xA; //a wall (2) or a ceiling (8) pushed it
const uint32_t kCtrlEntity = 4, kCtrlTimeStep = 0x578;
const uint32_t kStateMoving = 2, kStateSprinting = 3;
const float kConsoleStep = 1.0f / 30;
const int kWindow = 8; //frames of pushing looked at
const float kStuck = 0.1f; //stuck when less than this much of the pushing got anywhere
const float kMoved = 0.5f; //a console step that got this much of its way moved
const int kMovedSteps = 15; //that many in a row is sliding along a wall, not being stuck
const int kWallSteps = 4; //and that many stuck ones in a row is a wall
const float kWallWait = 0.25f; //seconds before a wall is tried again
const float kPushing = 1e-4f; //a frame's move that counts as pushing
PPCFunc orig_body_update = nullptr;
bool g_trace = false;

struct Vec3
{
	float x, y, z;
};

Vec3 ReadPos(uint32_t entity)
{
	return {ldf(entity + kEntityPos), ldf(entity + kEntityPos + 4), ldf(entity + kEntityPos + 8)};
}

void WritePos(uint32_t entity, const Vec3& p)
{
	stf(entity + kEntityPos, p.x);
	stf(entity + kEntityPos + 4, p.y);
	stf(entity + kEntityPos + 8, p.z);
}

//how far along the move (on the ground plane) the body really went
float Progress(const Vec3& from, const Vec3& to, const Vec3& move, float length)
{
	return ((to.x - from.x) * move.x + (to.z - from.z) * move.z) / length;
}

struct Stuck
{
	uint32_t m_Entity{};
	Vec3 m_Last{}; //where the last step left the body
	float m_Wanted{}, m_Got{}; //over the window
	int m_Frames{};
	float m_Wait{}; //seconds until a wall is tried again
	bool m_Console{}; //taking the console's steps
	Vec3 m_Move{}; //what has added up since the last one
	float m_Time{};
	int m_Moved{}, m_Failed{}; //console steps in a row that got somewhere, and that didn't

	void Reset(uint32_t entity)
	{
		*this = Stuck{};
		m_Entity = entity;
	}

	void NewWindow()
	{
		m_Wanted = m_Got = 0;
		m_Frames = 0;
	}
};
Stuck g_body;

//a step a frame, and a look at whether the pushing gets anywhere
void FrameStep(PPCContext& c, uint32_t entity, const Vec3& move, float step)
{
	orig_body_update(c);
	Vec3 now = ReadPos(entity);
	Vec3 last = g_body.m_Last;
	g_body.m_Last = now;
	g_body.m_Wait = std::max(g_body.m_Wait - step, 0.0f);
	float length = std::sqrt(move.x * move.x + move.z * move.z);
	if (length <= kPushing)
	{
		g_body.NewWindow();
		return;
	}
	g_body.m_Wanted += length;
	g_body.m_Got += Progress(last, now, move, length);
	if (++g_body.m_Frames < kWindow) return;
	if (g_body.m_Got < g_body.m_Wanted * kStuck && g_body.m_Wait <= 0)
	{
		g_body.m_Console = true;
		g_body.m_Move = {};
		g_body.m_Time = 0;
		g_body.m_Moved = g_body.m_Failed = 0;
		if (g_trace) rt_log("goldeneye: body: stuck at %.2f %.2f %.2f, the console's steps from here", now.x, now.y, now.z);
	}
	g_body.NewWindow();
}

//the frame's move is kept until a console frame's worth has added up, then taken as one step
void ConsoleStep(PPCContext& c, uint32_t body, uint32_t entity, const Vec3& move, float step)
{
	g_body.m_Move.x += move.x;
	g_body.m_Move.y += move.y;
	g_body.m_Move.z += move.z;
	g_body.m_Time += step;
	if (g_body.m_Time + step * 0.5f < kConsoleStep)
	{
		WritePos(entity, g_body.m_Last);
		return;
	}
	Vec3 total = g_body.m_Move, last = g_body.m_Last;
	WritePos(entity, {last.x + total.x, last.y + total.y, last.z + total.z});
	orig_body_update(c);
	Vec3 now = ReadPos(entity);
	g_body.m_Last = now;
	g_body.m_Move = {};
	g_body.m_Time = 0;

	float length = std::sqrt(total.x * total.x + total.z * total.z);
	float got = length > kPushing ? Progress(last, now, total, length) : 0;
	g_body.m_Moved = got > length * kMoved ? g_body.m_Moved + 1 : 0;
	g_body.m_Failed = got < length * kStuck ? g_body.m_Failed + 1 : 0;
	bool atWall = ld16(body + kBodyFlags) & kBodyAtWall;
	const char* why = nullptr;
	if (length <= kPushing) why = "not pushed any more";
	else if (!atWall) why = "clear of the walls";
	else if (g_body.m_Moved >= kMovedSteps) why = "sliding along a wall";
	else if (g_body.m_Failed >= kWallSteps)
	{
		why = "a wall";
		g_body.m_Wait = kWallWait;
	}
	if (!why) return;
	g_body.m_Console = false;
	if (g_trace) rt_log("goldeneye: body: %s at %.2f %.2f %.2f, a step a frame again", why, now.x, now.y, now.z);
}

void BodyUpdate(PPCContext& c)
{
	uint32_t body = c.r[3], ctrl = local_controller(), state = 0;
	uint32_t entity = ld32(body + kBodyEntity);
	bool ours = ctrl && guest_ptr(entity) && ld32(ctrl + kCtrlEntity) == entity && local_player_state(state);
	float step = ours ? ldf(ctrl + kCtrlTimeStep) : 0;
	bool walking = ours && (state == kStateMoving || state == kStateSprinting);
	if (!walking || noclip() || !(step > 0) || step > kConsoleStep * 0.9f)
	{
		if (ours) g_body.Reset(0);
		orig_body_update(c);
		return;
	}
	if (g_body.m_Entity != entity)
	{
		//first frame on foot: nothing to compare with yet
		g_body.Reset(entity);
		orig_body_update(c);
		g_body.m_Last = ReadPos(entity);
		return;
	}
	Vec3 pos = ReadPos(entity);
	Vec3 move{pos.x - g_body.m_Last.x, pos.y - g_body.m_Last.y, pos.z - g_body.m_Last.z};
	if (g_body.m_Console) ConsoleStep(c, body, entity, move, step);
	else FrameStep(c, entity, move, step);
}

//GE_TRACEPOS=1: where the local player's body is after each update
void BodyUpdateTraced(PPCContext& c)
{
	uint32_t body = c.r[3], entity = ld32(body + kBodyEntity), ctrl = local_controller();
	BodyUpdate(c);
	if (!ctrl || ld32(ctrl + kCtrlEntity) != entity) return;
	Vec3 pos = ReadPos(entity);
	rt_log("pos: %.3f %.3f %.3f step %.4f flags %X%s", pos.x, pos.y, pos.z, ldf(ctrl + kCtrlTimeStep), ld16(body + kBodyFlags), g_body.m_Console ? " console" : "");
}

} //namespace

void install_body()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_body_update = ppc_hook("ge_body_update", std::getenv("GE_TRACEPOS") ? BodyUpdateTraced : BodyUpdate);
	if (!orig_body_update) rt_log("goldeneye: ge_body_update is not hooked in this build, a stuck player keeps the frame rate's steps");
}

} //namespace ge
