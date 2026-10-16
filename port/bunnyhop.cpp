//bunnyhopping (F1 > For Fun, off by default): holding the jump key (the key file's Extra 6) hops
//as soon as the player is on the floor, and in the air the move stick steers the way half-life's
//air does, so strafing while turning adds speed
//
//how the game moves the player: see noclip.cpp. ge_look_apply turns the controller's velocity
//into the body's step (+0xBC, three floats, a frame's worth); the body adds its fall speed
//(+0x15C, up is positive) to the step itself in fn_800EECE0, which takes off its gravity
//(+0x160) a second while the body is off the floor and holds a falling one at -6 on it. an
//upward one on the floor is left alone, so that's a hop
//the walk state (PlayerMove, 8010DBE0) only lets go of the player more than 1.25 above the
//floor (806EEC98): it starts the fall (fn_801212A0, state 4) then, which only keeps the speed
//it fell with (+0xE78). a hop stays under that, so the player walks through the air the
//whole time, and the game's walk velocity is the one to start from
//
//the speed is ours from the first hop on: after ge_look_apply the step's x and z are replaced
//by it. the game's own velocity is left as it was, so its walk carries on under ours as what a
//new hop starts from. on the floor a hop never loses speed (it's the same frame, half-life's
//friction never comes into it), in the air quake's air accelerate: the stick's direction adds
//speed only up to kAirCap along it, so turning towards the stick keeps adding
//a wall, a ceiling or a ramp takes the speed into it and leaves the speed along it, which
//surfs a ramp too steep to stand on, and landing without the key held gives the game its player back
//once pre-strafing (below) is no faster than its walk
//
//a hop over 0.5 or 1.25 would have the walk take the player: there it looks for a ladder or a
//ledge (ge_move_grab), and the fall (ge_fall_start) has its own air and half a second of landing.
//so while the key is held there's nothing to grab, and the fall waits until the player is
//kFreeDrop under where they left the floor. then the game's fall takes over, counted from the
//top of the hop: a drop off the dam is still a drop off the dam. landing from it with the key
//held hops on with the speed it had
//
//with it on the local player's gravity is kGravity of the game's, and the fall that kills (the
//distance at +0xE90, PlayerLand: past 40 dead, past 2 a landing hurts) counts each frame's fall
//at half, so it takes twice the drop
//
//and there's no sprint: ge_sprint_update asks ge_sprint_allowed whether one may start and,
//sprinting, whether it goes on, and for the local player the answer is no (its other caller,
//ge_action_update's vault and climb, still gets the game's). the run is kBaseSpeed instead:
//ge_move_speed's 4.2 standing up (806E947C), the sprint's is 6.6, aimed and crouched stay the game's
//
//pre-strafing: on the floor without the key it's half-life's ground move, friction (sv_friction,
//stopspeed) then the accelerate (sv_accelerate) up to the game's walk speed along the stick. that only
//caps the speed along the stick, so strafing while turning gains speed on the floor as in the air,
//and friction takes it back. while that's no faster than the game's walk the player is the game's,
//and a landing without the key slides on it instead of stopping dead
//
//GE_BUNNYHOP=1 start with it on, else what the menu saved
//the speed goes in the log twice a second, and what walls take (GE_BHOP_LOG=0 not)
//GE_BHOP_PRESTRAFE=0 without pre-strafing, GE_BHOP_SPEED, GE_BHOP_HEIGHT, GE_BHOP_AIRCAP, GE_BHOP_AIRACCEL, GE_BHOP_WISH, GE_BHOP_MAX, GE_BHOP_GRAVITY,
//GE_BHOP_FALL, GE_BHOP_DROP, GE_BHOP_ACCEL, GE_BHOP_FRICTION over what the menu saved
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace ge
{
namespace
{
const uint32_t kEntity = 0x4, kPos = 0xF8, kYaw = 0xDC;
const uint32_t kBody = 0x158, kBodyStep = 0xBC, kBodyFloor = 0xA, kBodyHit = 0xA, kBodyPush = 0xEC, kBodyFallSpeed = 0x15C, kBodyGravity = 0x160;
const uint32_t kFallen = 0xE90;
const uint32_t kMoveStick = 0x500, kTimeStep = 0x578, kPlayerState = 0x704;
const uint32_t kSprinting = 0xE59, kCrouched = 0xC54, kSlowed = 0xD00;
const uint32_t kAskStart = 0x8010D9B0, kAskGoOn = 0x8010DA8C; //ge_sprint_update's calls of ge_sprint_allowed
const float kGameRun = 4.2f; //806E947C
const uint32_t kStateStanding = 0, kStateMoving = 2, kStateSprinting = 3, kStateFalling = 4;
//the tunables, F1 > For Fun > Bunnyhopping, saved as LAUNCH_BHOP_*
BhopValue g_values[kBhopValueCount]{
	{"Base Speed", "How fast you run, in metres a second: there's no sprint while bunnyhopping. 4.2 is the game's run, 6.6 its sprint.", "GE_BHOP_SPEED", "LAUNCH_BHOP_SPEED", 6.6f, 1.0f, 15.0f, 0.1f, 1},
	{"Hop Height", "How high a hop goes, in metres.", "GE_BHOP_HEIGHT", "LAUNCH_BHOP_HEIGHT", 1.5f, 0.1f, 5.0f, 0.05f, 2},
	{"Gravity", "Of the game's gravity while bunnyhopping. 1.0 is the game's own.", "GE_BHOP_GRAVITY", "LAUNCH_BHOP_GRAVITY", 0.5f, 0.1f, 1.5f, 0.05f, 2},
	{"Air Cap", "How much speed the stick adds along itself in the air. Half-Life's is 0.76: higher gains speed faster and turns sharper.", "GE_BHOP_AIRCAP", "LAUNCH_BHOP_AIRCAP", 2.5f, 0.1f, 10.0f, 0.05f, 2},
	{"Air Accelerate", "How fast the air cap is reached, Half-Life's sv_airaccelerate.", "GE_BHOP_AIRACCEL", "LAUNCH_BHOP_AIRACCEL", 10.0f, 1.0f, 100.0f, 1.0f, 0},
	{"Stick Speed", "The speed a full stick asks for, in metres a second. 4.2 is the game's run.", "GE_BHOP_WISH", "LAUNCH_BHOP_WISH", 4.2f, 1.0f, 15.0f, 0.1f, 1},
	{"Top Speed", "The fastest you can go, in metres a second. Very high speeds can go through thin walls.", "GE_BHOP_MAX", "LAUNCH_BHOP_MAX", 25.0f, 5.0f, 60.0f, 1.0f, 0},
	{"Fall Damage", "Of each fall that counts towards hurting and killing you. 0.5 takes twice the drop.", "GE_BHOP_FALL", "LAUNCH_BHOP_FALL", 0.5f, 0.05f, 1.0f, 0.05f, 2},
	{"Free Drop", "How far under where you jumped you fall before the game's own fall (and its landing) takes over, in metres.", "GE_BHOP_DROP", "LAUNCH_BHOP_DROP", 4.0f, 1.0f, 50.0f, 0.5f, 1},
	{"Ground Accelerate", "Pre-strafing: how fast the run's speed is reached on the floor, Half-Life's sv_accelerate. Higher gains more speed turning.", "GE_BHOP_ACCEL", "LAUNCH_BHOP_ACCEL", 10.0f, 1.0f, 30.0f, 0.5f, 1},
	{"Friction", "Pre-strafing: how fast the floor takes speed back, Half-Life's sv_friction. Lower keeps more.", "GE_BHOP_FRICTION", "LAUNCH_BHOP_FRICTION", 4.0f, 0.5f, 12.0f, 0.25f, 2},
};
float& kBaseSpeed = g_values[kBhopSpeed].value;
float& kHopHeight = g_values[kBhopHeight].value;
float& kGravity = g_values[kBhopGravity].value;
float& kAirCap = g_values[kBhopAirCap].value; //half-life's 30 units a second is 0.76
float& kAirAccel = g_values[kBhopAirAccel].value;
float& kWishSpeed = g_values[kBhopWish].value;
float& kMaxSpeed = g_values[kBhopMax].value; //more and a step goes through walls
float& kFall = g_values[kBhopFall].value; //of each frame's fall that counts towards the deadly one
float& kFreeDrop = g_values[kBhopDrop].value; //how far under the floor it left the player falls before the game's fall
float& kGroundAccel = g_values[kBhopGroundAccel].value;
float& kFriction = g_values[kBhopFriction].value;
const float kStopSpeed = 2.0f; //under this friction takes as if it were this, half-life's stopspeed of its run
const float kGain = 0.1f; //the floor's speed is ours this much over the game's walk
const float kDeadZone = 0.2f;
const float kWalkable = 0.7f; //a normal this far up is a floor, half-life's
const float kRamp = 0.05f; //and this far up a ramp, not a wall
const uint32_t kExtraJump = 1u << 5;

std::atomic<bool> g_bunnyhop{false};
std::atomic<bool> g_prestrafe{true};
std::atomic<bool> g_jump{false}; //as the pad was last read
std::atomic<float> g_speed{0}; //the speedometer's, read by the renderer's thread
std::atomic<uint32_t> g_speedFrame{~0u};
bool g_trace = false;
bool g_log = true; //the speed in the log, twice a second
uint32_t g_ctrl = 0; //the local controller as of its last update
bool g_active = false; //the speed is ours
float g_vel[2]{}; //x and z, a second
float g_ground[2]{}; //pre-strafing's on the floor, kept while the walk is still the game's
bool g_groundOn = false; //it went on last frame
uint32_t g_entity = 0;
uint32_t g_gravityBody = 0; //the body whose gravity is ours, its own and what we made it
float g_gravityGame = 0, g_gravityOurs = 0;
float g_fallen = 0; //the deadly fall as we left it
float g_floorY = 0, g_topY = 0; //where the player left the floor, and the highest since
PPCFunc orig_fall_start = nullptr, orig_move_grab = nullptr, orig_sprint_allowed = nullptr, orig_move_speed = nullptr;

//the body's gravity back to the game's
void GameGravity()
{
	if (guest_ptr(g_gravityBody) && ldf(g_gravityBody + kBodyGravity) == g_gravityOurs) stf(g_gravityBody + kBodyGravity, g_gravityGame);
	g_gravityBody = 0;
}

void LowGravity(uint32_t body)
{
	if (body != g_gravityBody) GameGravity();
	const float now{ ldf(body + kBodyGravity) };
	if (!std::isfinite(now) || now <= 0) return;
	if (body != g_gravityBody || now != g_gravityOurs) g_gravityGame = now; //the game set it again
	g_gravityBody = body;
	g_gravityOurs = g_gravityGame * kGravity;
	stf(body + kBodyGravity, g_gravityOurs);
}

//PlayerLand has added this frame's fall: only kFall of it stays
void LongerFall(uint32_t ctrl)
{
	const float now{ ldf(ctrl + kFallen) };
	if (!std::isfinite(now)) return;
	if (ld32(ctrl + kPlayerState) != kStateFalling || now < g_fallen)
	{
		g_fallen = ld32(ctrl + kPlayerState) == kStateFalling ? now * kFall : 0; //a new fall
		if (g_fallen != now && g_fallen > 0) stf(ctrl + kFallen, g_fallen);
		return;
	}
	g_fallen += (now - g_fallen) * kFall;
	stf(ctrl + kFallen, g_fallen);
}

bool Free(uint32_t state)
{
	return state == kStateStanding || state == kStateMoving || state == kStateSprinting || state == kStateFalling;
}

void Trace(float gameX, float gameZ, bool floor)
{
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (now - last < std::chrono::milliseconds(500)) return;
	last = now;
	rt_log("goldeneye: bunnyhop %s, speed %.2f (the game's %.2f), %s, yaw %.2f", g_active ? "on" : "off", std::hypot(g_vel[0], g_vel[1]),
		   std::hypot(gameX, gameZ), floor ? "on the floor" : "in the air", ldf(ld32(g_ctrl + kEntity) + kYaw));
}
//the walk asks whether to start the fall
void FallStart(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] };
	const uint32_t entity{ guest_ptr(ctrl) ? ld32(ctrl + kEntity) : 0 };
	if (!g_active || ctrl != g_ctrl || !guest_ptr(entity) || ld32(ctrl + kPlayerState) == kStateFalling)
	{
		orig_fall_start(c);
		return;
	}
	const float y{ ldf(entity + kPos + 4) };
	if (std::isfinite(y) && g_floorY - y < kFreeDrop) return;
	orig_fall_start(c);
	if (std::isfinite(y) && g_topY > y) stf(ctrl + kFallen, g_topY - y); //LongerFall takes its share of it
}

void MoveGrab(PPCContext& c)
{
	if (g_active && g_jump && c.r[3] == g_ctrl)
	{
		c.r[3] = 0;
		return;
	}
	orig_move_grab(c);
}

//ours: bunnyhopping, the local player's, offline
bool Ours(uint32_t ctrl)
{
	return g_bunnyhop && ctrl == g_ctrl && guest_ptr(ctrl) && !network_game();
}

void SprintAllowed(PPCContext& c)
{
	if ((c.lr == kAskStart || c.lr == kAskGoOn) && Ours(c.r[3]))
	{
		c.r[3] = 0;
		return;
	}
	orig_sprint_allowed(c);
}

//the speed (or one over it, the last argument 0) standing up and not sprinting is the base speed's
void MoveSpeed(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] }, out{ c.r[4] };
	const bool speed{ (c.r[6] & 0xFF) != 0 };
	orig_move_speed(c);
	if (!Ours(ctrl) || !guest_ptr(out) || ld8(ctrl + kSprinting) || ld8(ctrl + kCrouched) || ld8(ctrl + kSlowed)) return;
	const float now{ ldf(out) };
	if (!std::isfinite(now) || now == 0) return;
	stf(out, speed ? now * kBaseSpeed / kGameRun : now * kGameRun / kBaseSpeed);
}
//on the floor without the key, half-life's ground move from the speed we had, its own of last frame
//or the game's walk. each frame's gain off a turn is small, so it goes on under the walk until
//it's faster: true then, g_vel is the speed
bool PreStrafe(uint32_t ctrl, float gameX, float gameZ, float wishX, float wishZ, float stick, float dt, bool groundOn)
{
	if (!g_prestrafe || ld8(ctrl + kSprinting) || ld8(ctrl + kCrouched) || ld8(ctrl + kSlowed)) return false;
	float v[2]{ gameX, gameZ };
	if (g_active) v[0] = g_vel[0], v[1] = g_vel[1];
	else if (groundOn) v[0] = g_ground[0], v[1] = g_ground[1];
	const float speed{ std::hypot(v[0], v[1]) };
	if (speed > 0)
	{
		const float keep{ std::max(0.0f, speed - std::max(speed, kStopSpeed) * kFriction * dt) / speed };
		v[0] *= keep;
		v[1] *= keep;
	}
	if (stick >= kDeadZone)
	{
		const float wishSpeed{ std::hypot(gameX, gameZ) }; //the game's own run, slower on a diagonal stick
		const float add{ wishSpeed - (v[0] * wishX + v[1] * wishZ) };
		if (add > 0)
		{
			const float accel{ std::min(add, kGroundAccel * wishSpeed * dt) };
			v[0] += accel * wishX;
			v[1] += accel * wishZ;
		}
	}
	g_ground[0] = v[0];
	g_ground[1] = v[1];
	g_groundOn = true;
	if (std::hypot(v[0], v[1]) <= std::hypot(gameX, gameZ) + kGain) return false;
	g_vel[0] = v[0];
	g_vel[1] = v[1];
	return true;
}
} //namespace

bool bunnyhop() { return g_bunnyhop; }
bool bhop_prestrafe() { return g_prestrafe; }
void set_bhop_prestrafe(bool on) { g_prestrafe = on; }
BhopValue& bhop_value(int which) { return g_values[which]; }

void bhop_defaults()
{
	for (BhopValue& v : g_values) v.value = v.initial;
}

bool bunnyhop_speed(float& speed)
{
	if (!g_bunnyhop || !in_play() || ld32(kFrameCount) - g_speedFrame >= 4) return false;
	speed = g_speed;
	return true;
}
void set_bunnyhop(bool on) { g_bunnyhop = on; }

void bunnyhop_apply(uint32_t ctrl)
{
	g_ctrl = ctrl;
	const uint32_t entity{ ld32(ctrl + kEntity) };
	const uint32_t body{ guest_ptr(entity) ? ld32(entity + kBody) : 0 };
	if (!g_bunnyhop || !guest_ptr(body) || noclip() || network_game())
	{
		GameGravity();
		g_fallen = 0;
		return;
	}
	LowGravity(body);
	LongerFall(ctrl);
}
void bunnyhop_buttons(const ClassicState& s) { g_jump = (s.extra & kExtraJump) != 0; }

//after ge_look_apply, its step is in the body
void bunnyhop_look(uint32_t ctrl)
{
	const uint32_t entity{ ld32(ctrl + kEntity) };
	const uint32_t body{ guest_ptr(entity) ? ld32(entity + kBody) : 0 };
	const float dt{ ldf(ctrl + kTimeStep) };
	const bool groundOn{ g_groundOn };
	g_groundOn = false;
	if (!g_bunnyhop || ctrl != g_ctrl || !guest_ptr(body) || noclip() || network_game() || !Free(ld32(ctrl + kPlayerState)) ||
		!(dt > 0.0005f && dt < 0.2f))
	{
		g_active = false;
		return;
	}
	if (entity != g_entity) g_active = false;
	g_entity = entity;

	const float gameX{ ldf(body + kBodyStep) / dt }, gameZ{ ldf(body + kBodyStep + 8) / dt };
	const float stickX{ ldf(ctrl + kMoveStick) }, stickY{ ldf(ctrl + kMoveStick + 4) };
	const float yaw{ ldf(entity + kYaw) };
	if (!std::isfinite(gameX) || !std::isfinite(gameZ) || !std::isfinite(stickX) || !std::isfinite(stickY) || !std::isfinite(yaw))
	{
		g_active = false;
		return;
	}
	const bool floor{ (ld16(body + kBodyFloor) & 1) != 0 };
	g_speed = std::hypot(gameX, gameZ);
	g_speedFrame = ld32(kFrameCount);
	const float posY{ ldf(entity + kPos + 4) };
	if (std::isfinite(posY))
	{
		if (floor || !g_active) g_floorY = g_topY = posY;
		else g_topY = std::max(g_topY, posY);
	}
	const float stick{ std::min(1.0f, std::hypot(stickX, stickY)) };
	//the stick in the world, as noclip.cpp has it
	const float wishX{ stick > 0 ? (std::cos(yaw) * stickX + std::sin(yaw) * stickY) / stick : 0 };
	const float wishZ{ stick > 0 ? (-std::sin(yaw) * stickX + std::cos(yaw) * stickY) / stick : 0 };

	//a wall, a ceiling or a ramp the body touched takes the speed into it, as half-life's
	//ClipVelocity: the push out of it (+0xEC) is its normal, and the speed is ours across and the
	//body's fall speed up. a ramp too steep to stand on turns the speed across into speed up it,
	//and gravity pulls along it: surfing
	if (g_active && (ld16(body + kBodyFloor) & kBodyHit) != 0)
	{
		float n[3]{ ldf(body + kBodyPush), ldf(body + kBodyPush + 4), ldf(body + kBodyPush + 8) };
		const float push{ std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) };
		if (std::isfinite(push) && push > 1e-5f && n[1] / push < kWalkable)
		{
			for (float& f : n) f /= push;
			float v[3]{ g_vel[0], ldf(body + kBodyFallSpeed), g_vel[1] };
			const float into{ v[0] * n[0] + v[1] * n[1] + v[2] * n[2] };
			if (std::isfinite(v[1]) && into < 0)
			{
				if ((g_trace || g_log) && into < -1.0f)
					rt_log("goldeneye: bunnyhop: the body's flags %X, normal %.2f %.2f %.2f took %.2f of %.2f", ld16(body + kBodyFloor), n[0], n[1], n[2], -into,
						   std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
				for (int i = 0; i < 3; ++i) v[i] -= into * n[i];
				g_vel[0] = v[0];
				g_vel[1] = v[2];
				stf(body + kBodyFallSpeed, v[1]);
			}
			if (n[1] > kRamp && std::isfinite(posY)) //on a ramp, the fall starts over
			{
				g_floorY = g_topY = posY;
				if (ld32(ctrl + kPlayerState) == kStateFalling) stf(ctrl + kFallen, 0.0f);
				g_fallen = 0;
			}
		}
	}

	if (floor)
	{
		if (!g_jump) //a landing from the game's fall with the key held hops on: the hurt is done, its stall runs out in the air
		{
			g_active = PreStrafe(ctrl, gameX, gameZ, wishX, wishZ, stick, dt, groundOn);
			if (!g_active)
			{
				if (g_trace || g_log) Trace(gameX, gameZ, floor);
				return;
			}
		}
		else
		{
			//a hop off the floor: the speed we had, or the game's walk if that's faster
			if (!g_active || std::hypot(g_vel[0], g_vel[1]) < std::hypot(gameX, gameZ))
			{
				g_vel[0] = gameX;
				g_vel[1] = gameZ;
			}
			g_active = true;
			const float gravity{ ldf(body + kBodyGravity) };
			if (std::isfinite(gravity) && gravity > 0) stf(body + kBodyFallSpeed, std::sqrt(2.0f * gravity * kHopHeight));
		}
	}
	else
	{
		if (!g_active)
		{
			if (!g_jump)
			{
				if (g_trace || g_log) Trace(gameX, gameZ, floor);
				return;
			}
			g_vel[0] = gameX;
			g_vel[1] = gameZ;
			g_active = true;
		}
		if (stick >= kDeadZone)
		{
			const float wishSpeed{ kWishSpeed * stick };
			const float add{ std::min(wishSpeed, kAirCap) - (g_vel[0] * wishX + g_vel[1] * wishZ) };
			if (add > 0)
			{
				const float accel{ std::min(add, kAirAccel * wishSpeed * dt) };
				g_vel[0] += accel * wishX;
				g_vel[1] += accel * wishZ;
			}
		}
	}

	const float speed{ std::hypot(g_vel[0], g_vel[1]) };
	if (speed > kMaxSpeed)
	{
		g_vel[0] *= kMaxSpeed / speed;
		g_vel[1] *= kMaxSpeed / speed;
	}
	g_speed = std::hypot(g_vel[0], g_vel[1]);
	stf(body + kBodyStep, g_vel[0] * dt);
	stf(body + kBodyStep + 8, g_vel[1] * dt);
	if (g_trace || g_log) Trace(gameX, gameZ, floor);
}

void install_bunnyhop()
{
	g_bunnyhop = std::atoi(saved_setting("GE_BUNNYHOP", "LAUNCH_BUNNYHOP").c_str()) != 0;
	const std::string prestrafe{ saved_setting("GE_BHOP_PRESTRAFE", "LAUNCH_BHOP_PRESTRAFE") };
	if (!prestrafe.empty()) g_prestrafe = std::atoi(prestrafe.c_str()) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	if (const char* e = std::getenv("GE_BHOP_LOG")) g_log = std::atoi(e) != 0;
	for (BhopValue& v : g_values)
	{
		v.initial = v.value;
		const std::string saved{ saved_setting(v.env, v.launch) };
		if (!saved.empty()) v.value = std::clamp((float)std::atof(saved.c_str()), v.lo, v.hi);
	}
	orig_fall_start = ppc_hook("ge_fall_start", FallStart);
	orig_move_grab = ppc_hook("ge_move_grab", MoveGrab);
	orig_sprint_allowed = ppc_hook("ge_sprint_allowed", SprintAllowed);
	orig_move_speed = ppc_hook("ge_move_speed", MoveSpeed);
	if (!orig_sprint_allowed || !orig_move_speed) rt_log("goldeneye: bunnyhop: ge_sprint_allowed or ge_move_speed isn't hooked, the sprint and the run are the game's");
	if (!orig_fall_start || !orig_move_grab) rt_log("goldeneye: bunnyhop: ge_fall_start or ge_move_grab isn't hooked, hops over 0.5 are the walk's");
}

} //namespace ge
