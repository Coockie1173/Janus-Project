//noclip (a cheat, off by default): the player flies where they look, through walls and floors,
//and nothing pulls them down
//
//how the game moves the player: the controller's update (ge_controller_update) ends up in
//ge_look_apply, which hands the frame's step to the entity's body (the entity's +0x158) as
//three floats at +0xBC. the body is moved through the level's collision later in the frame and
//the entity's position (+0xF8) follows it
//off the ground the player's state is 4 (PlayerLand): the fall's velocity at the controller's
//+0xE78 becomes that step, the distance fallen adds up at +0xE90, and past a limit it kills
//
//the body has two parts, both inside that update, and a short of flags at +8:
//- fn_800EECE0 adds gravity to its fall speed (+0x15C) unless bit 1 is set, and the fall speed
//  to the step
//- its step (the vtable's +0x18, fn_800F0FC0, called by the player's fn_8012A2B0) takes the
//  step through the collision, and is only called while bit 2 is clear
//
//with noclip on, for the local controller: both bits are kept set and the fall speed at 0, so
//nothing collides and nothing falls; ge_look_apply gets no velocity and no step; and after
//the update the fall's velocity and distance are zeroed and the position is the one kept
//here, moved by the move stick (+0x500) along the view, faster with the sprint key
//the game's own position is taken again whenever the game has the player (a cutscene, a
//climb, death) or puts them somewhere else, and the body's bits are cleared again
//turned off inside a wall or under the floor the player falls out of the level, as in any noclip
//
//GE_NOCLIP=1 start with it on
//GE_TRACE=1 log the position twice a second
#include "ge.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{
const uint32_t kEntity = 0x4, kPos = 0xF8, kYaw = 0xDC;
const uint32_t kView = 0x16C, kViewPitch = 0x768; //the entity's view object, its pitch
const uint32_t kBody = 0x158, kBodyFallSpeed = 0x15C;
const uint32_t kBodyFlags = 0x8, kBodyOff = 3; //a short: bit 1 no gravity, bit 2 no step through the collision
const uint32_t kMoveStick = 0x500; //two floats, x then y
const uint32_t kTimeStep = 0x578;
const uint32_t kVelocity = 0x968, kStep = 0x590; //four floats each, what ge_look_apply makes the body's step of
const uint32_t kPlayerState = 0x704;
const uint32_t kFallVelocity = 0xE78, kFallen = 0xE90; //four floats, then the distance fallen
const float kSpeed = 6.0f, kFastSpeed = 20.0f; //units a second
const float kJump = 4.0f; //further than this in a frame and the game put the player somewhere else

std::atomic<bool> g_noclip{false};
bool g_trace = false;
uint32_t g_entity = 0, g_body = 0; //the entity the position is for and its body, 0 when there's none
float g_pos[3]{};
uint32_t g_ctrl = 0; //the local controller as of its last update, for player_state

//the states the player moves themselves in: standing, moving, sprinting, in the air
bool Free(uint32_t state) { return state == 0 || state == 2 || state == 3 || state == 4; }

//the body takes over again, from where the player is now
void Land(uint32_t body)
{
	st16(body + kBodyFlags, ld16(body + kBodyFlags) & ~kBodyOff);
	stf(body + kBodyFallSpeed, 0.0f);
}

void Trace(uint32_t ctrl, const float* game)
{
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (now - last < std::chrono::milliseconds(500)) return;
	last = now;
	rt_log("goldeneye: noclip at %.2f %.2f %.2f (the game had %.2f %.2f %.2f), state %X", g_pos[0], g_pos[1], g_pos[2],
		   game[0], game[1], game[2], ld32(ctrl + kPlayerState));
}
} //namespace

bool noclip() { return g_noclip; }
void set_noclip(bool on) { g_noclip = on; }

void noclip_look(uint32_t ctrl)
{
	if (!g_noclip || !g_entity || ld32(ctrl + kEntity) != g_entity) return;
	for (int i = 0; i < 4; ++i)
	{
		stf(ctrl + kVelocity + 4 * i, 0.0f);
		stf(ctrl + kStep + 4 * i, 0.0f);
	}
}

//what the portal walk's log says of the player: the state, the distance fallen, the body's flags
bool player_state(uint32_t& state, float& fallen, uint32_t& bodyFlags)
{
	if (!guest_ptr(g_ctrl)) return false;
	const uint32_t entity{ ld32(g_ctrl + kEntity) };
	const uint32_t body{ guest_ptr(entity) ? ld32(entity + kBody) : 0 };
	state = ld32(g_ctrl + kPlayerState);
	fallen = ldf(g_ctrl + kFallen);
	bodyFlags = guest_ptr(body) ? ld16(body + kBodyFlags) : 0xFFFFu;
	return true;
}

void noclip_apply(uint32_t ctrl)
{
	g_ctrl = ctrl;
	uint32_t entity = ld32(ctrl + kEntity);
	uint32_t body = guest_ptr(entity) ? ld32(entity + kBody) : 0;
	if (!g_noclip || !guest_ptr(body) || !Free(ld32(ctrl + kPlayerState)))
	{
		if (g_entity && entity == g_entity && body == g_body) Land(body);
		g_entity = g_body = 0;
		return;
	}
	float game[3];
	bool moved = entity != g_entity || body != g_body;
	for (int i = 0; i < 3; ++i)
	{
		game[i] = ldf(entity + kPos + 4 * i);
		if (!std::isfinite(game[i])) return;
		if (std::fabs(game[i] - g_pos[i]) > kJump) moved = true;
	}
	if (moved)
	{
		g_entity = entity;
		g_body = body;
		for (int i = 0; i < 3; ++i) g_pos[i] = game[i];
	}

	float dt = ldf(ctrl + kTimeStep);
	float x = ldf(ctrl + kMoveStick), y = ldf(ctrl + kMoveStick + 4);
	uint32_t view = ld32(entity + kView);
	float yaw = ldf(entity + kYaw), pitch = guest_ptr(view) ? ldf(view + kViewPitch) : 0.0f;
	if (std::isfinite(dt) && dt > 0 && dt < 0.5f && std::isfinite(x) && std::isfinite(y) && std::isfinite(yaw) && std::isfinite(pitch))
	{
		float step = (sprint_key() ? kFastSpeed : kSpeed) * dt;
		float forward[3] = {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
		float right[3] = {std::cos(yaw), 0.0f, -std::sin(yaw)};
		for (int i = 0; i < 3; ++i) g_pos[i] += (forward[i] * y + right[i] * x) * step;
	}

	for (int i = 0; i < 3; ++i) stf(entity + kPos + 4 * i, g_pos[i]);
	st16(body + kBodyFlags, ld16(body + kBodyFlags) | kBodyOff);
	stf(body + kBodyFallSpeed, 0.0f);
	for (int i = 0; i < 5; ++i) stf(ctrl + kFallVelocity + 4 * i, 0.0f); //kFallen is the fifth
	if (g_trace) Trace(ctrl, game);
}

void install_noclip()
{
	rt_state_register("noclip", [](StateIO& io) { io.pod(g_entity); io.pod(g_body); io.pod(g_pos); io.pod(g_ctrl); });
	if (const char* e = std::getenv("GE_NOCLIP")) g_noclip = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
}

} //namespace ge
