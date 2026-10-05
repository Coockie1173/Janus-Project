//the player's body against the level at frame rates above the game's 30
//
//a body (the entity's +0x158, its entity at +4) is moved by its controller, position += speed
//times the time step, and ge_body_update (800F0FC0) then has ge_body_collide (800F12D0) push
//it out of the triangles it's in: ge_contacts_solve (802CF760), two passes over the contacts,
//the push at +0xEC, flags at +0xA (1 on a floor, 2 at a wall)
//how far it gets pushed depends on how deep the step went in, so a step isn't the sum of
//smaller ones. the levels lean on that: a vent's frame is a hair smaller than the body
//(level 01810150's exit, mesh 912CAF00 under a low ceiling) and a 30th of a second's step goes deep
//enough to come out the far side, with a jitter you can see on the console too. a 144th's
//gets pushed straight back, the body swings between two contacts and never gets through
//
//so a body in a tight spot (a ceiling pushes on it as well) that's pushed at something and
//gets nowhere moves the way the console's does:
//it stays put until a 30th of a second's moves have added up and then takes them as one step,
//the drop that keeps it on the floor (6 a second) included. that goes on until it's clear of
//the walls or isn't pushed any more, or has slid along one for half a second. in the
//open (a wall with nothing overhead), in a cutscene and in every state but walking and
//sprinting the body is left alone. the game has no word for being in a vent, and
//crouching doesn't tell either: in the recorded one the player stands (+0x390 stays 0.15) and
//crouched at a wall in the open isn't stuck in anything. the ceiling on the body is what tells
#include "ge.h"
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kBodyEntity = 4, kEntityPos = 0xF8;
const uint32_t kCtrlEntity = 4, kCtrlTimeStep = 0x578;
const uint32_t kStateMoving = 2, kStateSprinting = 3;
const uint32_t kEntityCrouch = 0x390; //0.15 standing, 0.65 crouched, eases between the two (only traced)
const float kConsoleStep = 1.0f / 30;
const int kWindow = 8; //frames of pushing looked at
const float kStuck = 0.1f; //blocked when less than this much of the pushing got anywhere
const float kFree = 0.5f; //a console step that got this much of its way moved
const int kFreeSteps = 15; //and that many in a row is sliding along a wall, not being stuck
const uint32_t kBodyFlags = 0xA, kBodyAtWall = 0xA, kBodyAtCeiling = 8; //a wall (2) or a ceiling (8) pushed it
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

struct Blocked
{
	uint32_t m_Entity{};
	Vec3 m_Last{}; //where the last step left the body
	float m_Wanted{}, m_Got{}; //over the window
	int m_Frames{};
	bool m_Tight{}; //a ceiling pushed the body in the window
	bool m_Blocked{};
	Vec3 m_Move{}; //what has added up since the last console step
	float m_Time{};
	int m_Moved{}; //console steps in a row that got somewhere

	void Reset(uint32_t entity)
	{
		*this = Blocked{};
		m_Entity = entity;
	}
};
Blocked g_body;

void BodyUpdate(PPCContext& c)
{
	uint32_t body = c.r[3], ctrl = local_controller(), state = 0;
	uint32_t entity = ld32(body + kBodyEntity);
	bool ours = ctrl && guest_ptr(entity) && ld32(ctrl + kCtrlEntity) == entity && local_player_state(state);
	float step = ours ? ldf(ctrl + kCtrlTimeStep) : 0;
	//only on foot, a cutscene (state 0x22) has the body to itself
	bool walking = ours && (state == kStateMoving || state == kStateSprinting);
	if (!walking || !(step > 0) || step > kConsoleStep * 0.9f)
	{
		if (ours) g_body.Reset(0);
		orig_body_update(c);
		return;
	}
	Vec3 pos = ReadPos(entity);
	if (g_body.m_Entity != entity)
	{
		//first frame on foot: nothing to compare with yet
		g_body.Reset(entity);
		orig_body_update(c);
		g_body.m_Last = ReadPos(entity);
		return;
	}
	Vec3 move{pos.x - g_body.m_Last.x, pos.y - g_body.m_Last.y, pos.z - g_body.m_Last.z};

	if (!g_body.m_Blocked)
	{
		orig_body_update(c);
		Vec3 now = ReadPos(entity);
		float length = std::sqrt(move.x * move.x + move.z * move.z);
		if (length > kPushing)
		{
			g_body.m_Wanted += length;
			g_body.m_Got += Progress(g_body.m_Last, now, move, length);
			if (ld16(body + kBodyFlags) & kBodyAtCeiling) g_body.m_Tight = true;
			if (++g_body.m_Frames >= kWindow)
			{
				if (g_body.m_Tight && g_body.m_Got < g_body.m_Wanted * kStuck)
				{
					g_body.m_Blocked = true;
					g_body.m_Move = {};
					g_body.m_Time = 0;
					g_body.m_Moved = 0;
					if (g_trace) rt_log("goldeneye: body: blocked at %.2f %.2f %.2f, the console's steps from here", now.x, now.y, now.z);
				}
				g_body.m_Wanted = g_body.m_Got = 0;
				g_body.m_Frames = 0;
				g_body.m_Tight = false;
			}
		}
		else
		{
			g_body.m_Wanted = g_body.m_Got = 0;
			g_body.m_Frames = 0;
			g_body.m_Tight = false;
		}
		g_body.m_Last = now;
		return;
	}

	g_body.m_Move.x += move.x;
	g_body.m_Move.y += move.y;
	g_body.m_Move.z += move.z;
	g_body.m_Time += step;
	if (g_body.m_Time + step * 0.5f < kConsoleStep)
	{
		//not a console frame's worth yet: the body waits where it was
		WritePos(entity, g_body.m_Last);
		return;
	}
	Vec3 total = g_body.m_Move;
	WritePos(entity, {g_body.m_Last.x + total.x, g_body.m_Last.y + total.y, g_body.m_Last.z + total.z});
	orig_body_update(c);
	Vec3 now = ReadPos(entity);
	float length = std::sqrt(total.x * total.x + total.z * total.z);
	//free again: nothing pushed at any more, clear of the walls, or sliding along one for a while
	bool atWall = ld16(body + kBodyFlags) & kBodyAtWall;
	if (Progress(g_body.m_Last, now, total, length) > length * kFree) ++g_body.m_Moved;
	else g_body.m_Moved = 0;
	if (length <= kPushing || !atWall || g_body.m_Moved >= kFreeSteps)
	{
		g_body.m_Blocked = false;
		if (g_trace) rt_log("goldeneye: body: free at %.2f %.2f %.2f", now.x, now.y, now.z);
	}
	g_body.m_Move = {};
	g_body.m_Time = 0;
	g_body.m_Last = now;
}

//GE_TRACEPOS=1: where the local player's body is after each update
void BodyUpdateTraced(PPCContext& c)
{
	uint32_t body = c.r[3], entity = ld32(body + kBodyEntity), ctrl = local_controller();
	BodyUpdate(c);
	if (!ctrl || ld32(ctrl + kCtrlEntity) != entity) return;
	Vec3 pos = ReadPos(entity);
	rt_log("pos: %.3f %.3f %.3f step %.4f crouch %.2f flags %X%s", pos.x, pos.y, pos.z, ldf(ctrl + kCtrlTimeStep), ldf(entity + kEntityCrouch), ld16(body + kBodyFlags), g_body.m_Blocked ? " blocked" : "");
}

} //namespace

void install_body()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_body_update = ppc_hook("ge_body_update", std::getenv("GE_TRACEPOS") ? BodyUpdateTraced : BodyUpdate);
	if (!orig_body_update) rt_log("goldeneye: ge_body_update is not hooked in this build, a blocked player keeps the frame rate's steps");
}

} //namespace ge
