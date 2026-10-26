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
//online, through Connect to WFC Modded: the host's bunnyhopping is everyone's. the host steps
//every player from their commands and a joined console steps its own ahead of it, so each
//player has its hop of its own (Hopper) wherever it's stepped, and the jump key goes with the
//command: the bits 31 (ours) and 30 (the key) of its +0x3C, which the game fills but never
//sends (ge_command_fill, ge_command_read puts 0 there), taken off for ge_input_copy and put
//back after. ge_command_write and ge_command_read add the key behind the game's own
//the host's switch and values go with its own player's state (behind the ping, net.cpp), each
//value as its step on the menu's slider, the same numbers the host plays by. a joined console
//plays by them while it's in the game and its menu shows them, locked; its own are kept for
//later. with the host's off, or before they came, nobody hops. the others a joined console sees
//are drawn towards where the host says they are, hops and all, nothing to do there
//over Nintendo WFC (a console's game) it stays off
//
//the tank's mission is on foot: the game makes its player XItemHandler_PlayerTank where
//ge_tank_level (800BFFB0) says the level is one, Bond himself (XItemHandler_PlayerNew) elsewhere.
//so in the campaign (0181xxxx) the answer is no while bunnyhopping, for every one of its callers
//(the hud, the commands...) alike. it's made while there's no player, as a level loads, and kept
//until the player is gone again: switching it in play changes the next level, not this one
//a tank's level names its player's skin in its row (+0x78): the tank's (4D0001A8, io_tank) for
//Tank. ge_player_skin (8000F8E0) takes it over ge_tank_level's answer, and Bond made with it has
//neither his body nor his first-person arms (cp_bondfp, whose head bone the camera sits at): the
//view stays at 0,0,0, the streets never come in and he falls. so for that call the row says
//nothing and the game picks Bond's (4D0001B9) itself, his files load after it
//the mission's way is one objective's markers (type 0x3F, 22 on Tank), each woken by the one
//before as the player reaches it, the last waking the ending's cutscene and that the exit. one
//of them hands on through the tank's own part of the street (Tank's marker at -398 27 -151 to a
//static object), which Bond never gets past. so on foot the way is followed: a marker that was
//awake and sleeps again went off, and when one has and a second later none of its objective is
//awake, the nearest one that hasn't gone off yet is woken (an awake marker draws its icon, so
//only ever the one ahead). a checkpoint starts them over, and the way with them
//and the opening plays with the tank bound to it (its slot 0, 4D0001E6), Bond is left free to walk
//off while it does. so while it plays (the byte at 806EB6C8, which OB_Cutscene sets as one starts
//and clears as it ends) and no cutscene has Bond himself (+0x5E4), his input is empty: the sticks
//and the buttons ge_input_copy takes, and the hop's key
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
	{"Base Speed", "How fast you run, in metres a second: there's no sprint while bunnyhopping. 4.2 is the game's run, 6.6 its sprint.", "GE_BHOP_SPEED", "LAUNCH_BHOP_SPEED", 7.0f, 1.0f, 15.0f, 0.1f, 1},
	{"Hop Height", "How high a hop goes, in metres.", "GE_BHOP_HEIGHT", "LAUNCH_BHOP_HEIGHT", 0.9f, 0.1f, 5.0f, 0.05f, 2},
	{"Gravity", "Of the game's gravity while bunnyhopping. 1.0 is the game's own.", "GE_BHOP_GRAVITY", "LAUNCH_BHOP_GRAVITY", 0.5f, 0.1f, 1.5f, 0.05f, 2},
	{"Air Cap", "How much speed the stick adds along itself in the air. Half-Life's is 0.76: higher gains speed faster and turns sharper.", "GE_BHOP_AIRCAP", "LAUNCH_BHOP_AIRCAP", 1.1f, 0.1f, 10.0f, 0.05f, 2},
	{"Air Accelerate", "How fast the air cap is reached, Half-Life's sv_airaccelerate.", "GE_BHOP_AIRACCEL", "LAUNCH_BHOP_AIRACCEL", 8.0f, 1.0f, 100.0f, 1.0f, 0},
	{"Stick Speed", "The speed a full stick asks for, in metres a second. 4.2 is the game's run.", "GE_BHOP_WISH", "LAUNCH_BHOP_WISH", 4.2f, 1.0f, 15.0f, 0.1f, 1},
	{"Top Speed", "The fastest you can go, in metres a second. Very high speeds can go through thin walls.", "GE_BHOP_MAX", "LAUNCH_BHOP_MAX", 60.0f, 5.0f, 120.0f, 1.0f, 0},
	{"Fall Damage", "Of each fall that counts towards hurting and killing you. 0.5 takes twice the drop.", "GE_BHOP_FALL", "LAUNCH_BHOP_FALL", 0.5f, 0.05f, 1.0f, 0.05f, 2},
	{"Free Drop", "How far under where you jumped you fall before the game's own fall (and its landing) takes over, in metres.", "GE_BHOP_DROP", "LAUNCH_BHOP_DROP", 4.0f, 1.0f, 50.0f, 0.5f, 1},
	{"Ground Accelerate", "Pre-strafing: how fast the run's speed is reached on the floor, Half-Life's sv_accelerate. Higher gains more speed turning.", "GE_BHOP_ACCEL", "LAUNCH_BHOP_ACCEL", 10.0f, 1.0f, 30.0f, 0.5f, 1},
	{"Friction", "Pre-strafing: how fast the floor takes speed back, Half-Life's sv_friction. Lower keeps more.", "GE_BHOP_FRICTION", "LAUNCH_BHOP_FRICTION", 3.5f, 0.5f, 12.0f, 0.25f, 2},
};
//what play goes by this frame: the menu's offline, the host's in a modded network game
struct Rules
{
	bool on = false, prestrafe = true;
	float v[kBhopValueCount]{};
};
Rules g_now;
float& kBaseSpeed = g_now.v[kBhopSpeed];
float& kHopHeight = g_now.v[kBhopHeight];
float& kGravity = g_now.v[kBhopGravity];
float& kAirCap = g_now.v[kBhopAirCap]; //half-life's 30 units a second is 0.76
float& kAirAccel = g_now.v[kBhopAirAccel];
float& kWishSpeed = g_now.v[kBhopWish];
float& kMaxSpeed = g_now.v[kBhopMax]; //more and a step goes through walls
float& kFall = g_now.v[kBhopFall]; //of each frame's fall that counts towards the deadly one
float& kFreeDrop = g_now.v[kBhopDrop]; //how far under the floor it left the player falls before the game's fall
float& kGroundAccel = g_now.v[kBhopGroundAccel];
float& kFriction = g_now.v[kBhopFriction];
const float kStopSpeed = 2.0f; //under this friction takes as if it were this, half-life's stopspeed of its run
const float kGain = 0.1f; //the floor's speed is ours this much over the game's walk
const float kDeadZone = 0.2f;
const float kWalkable = 0.7f; //a normal this far up is a floor, half-life's
const float kRamp = 0.05f; //and this far up a ramp, not a wall
const uint32_t kExtraJump = 1u << 5;
const uint32_t kCmdExtra = 0x3C, kCmdOurs = 0x80000000, kCmdJump = 0x40000000; //see the top

enum Mode { kOffline, kOff, kHosting, kJoined };
Mode g_mode = kOffline;
uint32_t g_rulesFrame = ~0u;
Rules g_host; //as the host last sent them
bool g_hostKnown = false;

std::atomic<bool> g_bunnyhop{false}; //the menu's, ours
std::atomic<bool> g_prestrafe{true};
std::atomic<bool> g_onNow{false}, g_locked{false}; //for the menu's and the renderer's threads
std::atomic<bool> g_jump{false}; //as the pad was last read
std::atomic<float> g_speed{0}; //the speedometer's, read by the renderer's thread
std::atomic<uint32_t> g_speedFrame{~0u};
bool g_trace = false;
bool g_log = true; //the speed in the log, twice a second
PPCFunc orig_fall_start = nullptr, orig_move_grab = nullptr, orig_sprint_allowed = nullptr, orig_move_speed = nullptr;
PPCFunc orig_command_write = nullptr, orig_command_read = nullptr;
PPCFunc orig_tank_level = nullptr, orig_player_skin = nullptr;
const uint32_t kLevels = 0x805D1804, kLevelRow = 0x805D1820, kRowSkin = 0x78; //the level manager, its row of the level
const uint32_t kLevelKind = 0xFFFF0000, kCampaignLevel = 0x01810000;
bool g_onFoot = false; //this level's player isn't the tank, see the top
uint32_t g_saidOnFoot = 0; //the level it was logged for
bool g_tankOnFoot = false; //a tank's level is on foot now
bool g_held = false; //Bond waits for the cutscene, as last said
const uint32_t kCutscenePlaying = 0x806EB6C8, kInCutscene = 0x5E4;
const uint32_t kInputSticks = 0x8, kInputStickCount = 6, kInputBytes = 0x30, kInputButtons = 0x34, kInputHeld = 0x38;
//the markers of this level's way, see the top
struct Way
{
	std::vector<uint32_t> reached; //markers that went off
	uint32_t awake = 0, objective = 0, last = 0, before = 0; //a marker seen awake, the ones reached last and before it
	std::chrono::steady_clock::time_point stalled{}; //none awake since
	bool waiting = false;
};
Way g_way;
uint32_t g_wayFrame = ~0u;
const auto kWayGap = std::chrono::seconds(1); //the game's own link comes the frame after
const uint32_t kTriggerAt = 0x30;
const uint32_t kWorld = 0x805F4CCC, kTriggerCount = 0xE0, kTriggers = 0xE4;
const uint32_t kTriggerType = 0x58, kTriggerFlags = 0x68, kAsleep = 1, kMarkerObjective = 0x9C;
const uint32_t kObjectiveMarker = 0x3F;

//a player's hop: offline the local one's, online every one stepped here
struct Hopper
{
	uint32_t ctrl = 0;
	uint32_t seen = 0; //the frame it was last asked for
	bool commanded = false; //stepped from commands that had the key in them
	bool jump = false; //the key, as its last input had it
	bool active = false; //the speed is ours
	float vel[2]{}; //x and z, a second
	float ground[2]{}; //pre-strafing's on the floor, kept while the walk is still the game's
	bool groundOn = false; //it went on last frame
	uint32_t entity = 0;
	uint32_t gravityBody = 0; //the body whose gravity is ours, its own and what we made it
	float gravityGame = 0, gravityOurs = 0;
	float fallen = 0; //the deadly fall as we left it
	float floorY = 0, topY = 0; //where the player left the floor, and the highest since
};
const int kHoppers = 16;
Hopper g_hoppers[kHoppers];

Hopper* Of(uint32_t ctrl)
{
	for (Hopper& h : g_hoppers)
	{
		if (h.ctrl == ctrl && ctrl != 0) return &h;
	}
	return nullptr;
}

//the body's gravity back to the game's
void GameGravity(Hopper& h)
{
	if (guest_ptr(h.gravityBody) && ldf(h.gravityBody + kBodyGravity) == h.gravityOurs) stf(h.gravityBody + kBodyGravity, h.gravityGame);
	h.gravityBody = 0;
}

//the controller's own, else a free one or the one longest not asked for
Hopper& Make(uint32_t ctrl)
{
	if (Hopper* h = Of(ctrl))
	{
		h->seen = ld32(kFrameCount);
		return *h;
	}
	Hopper* pFree = &g_hoppers[0];
	for (Hopper& h : g_hoppers)
	{
		if (h.ctrl == 0) { pFree = &h; break; }
		if (h.seen < pFree->seen) pFree = &h;
	}
	GameGravity(*pFree);
	*pFree = Hopper{};
	pFree->ctrl = ctrl;
	pFree->seen = ld32(kFrameCount);
	return *pFree;
}

//a value as its step on the slider: what the host sends and plays by
uint32_t StepOf(int which, float value)
{
	const BhopValue& b = g_values[which];
	return (uint32_t)std::clamp(std::lround((value - b.lo) / b.by), 0L, std::lround((b.hi - b.lo) / b.by));
}
float AtStep(int which, uint32_t step) { return g_values[which].lo + (float)step * g_values[which].by; }

Rules Own(bool steps)
{
	Rules r;
	r.on = g_bunnyhop;
	r.prestrafe = g_prestrafe;
	for (int i = 0; i < kBhopValueCount; ++i) r.v[i] = steps ? AtStep(i, StepOf(i, g_values[i].value)) : g_values[i].value;
	return r;
}

//once a frame, before anything reads the rules
void Refresh()
{
	const uint32_t frame = ld32(kFrameCount);
	if (frame == g_rulesFrame) return;
	g_rulesFrame = frame;
	Mode mode = kOffline;
	if (network_game()) mode = !modded_lobby() ? kOff : joined_game() ? kJoined : kHosting;
	else g_hostKnown = false;
	switch (mode)
	{
	case kOffline: g_now = Own(false); break;
	case kHosting: g_now = Own(true); break;
	case kJoined: g_now = g_hostKnown ? g_host : Rules{}; break;
	case kOff: g_now.on = false; break;
	}
	static int said = -1;
	if (mode * 2 + g_now.on != said)
	{
		said = mode * 2 + g_now.on;
		static const char* names[] = { "offline", "a game over Nintendo WFC, off", "hosting, ours go to the others", "joined, the host's" };
		if (mode != kOffline) rt_log("goldeneye: bunnyhop: %s, %s%s", names[mode], g_now.on ? "on" : "off", mode == kJoined && !g_hostKnown ? " until the host's come" : "");
	}
	g_mode = mode;
	g_onNow = g_now.on;
	g_locked = mode == kJoined;
	if (!g_now.on || !guest_ptr(ld32(kPlayerPtr)))
	{
		for (Hopper& h : g_hoppers)
		{
			GameGravity(h);
			h.fallen = 0;
			h.active = false;
		}
	}
	if (!guest_ptr(ld32(kPlayerPtr))) //between levels the controllers are gone
	{
		for (Hopper& h : g_hoppers) h = Hopper{};
	}
}

//on foot in a tank's level a cutscene that doesn't have Bond holds him where he is, see the top
bool Held(uint32_t ctrl)
{
	const bool held{ g_tankOnFoot && guest_ptr(ctrl) && ctrl == local_controller() && ld8(kCutscenePlaying) && !ld8(ctrl + kInCutscene) };
	if (held != g_held) rt_log("goldeneye: bunnyhop: Bond %s", held ? "waits for the cutscene" : "is let go");
	g_held = held;
	return held;
}

//the controller hops here: offline the local one, online one stepped from commands
Hopper* Hops(uint32_t ctrl)
{
	Refresh();
	if (!g_now.on || !guest_ptr(ctrl)) return nullptr;
	if (g_mode == kOffline)
	{
		if (ctrl != local_controller()) return nullptr;
		if (noclip())
		{
			if (Hopper* h = Of(ctrl)) h->active = false;
			return nullptr;
		}
		Hopper& h = Make(ctrl);
		h.jump = g_jump && !Held(ctrl);
		return &h;
	}
	Hopper* h = Of(ctrl);
	return h && h->commanded ? h : nullptr;
}

void LowGravity(Hopper& h, uint32_t body)
{
	if (body != h.gravityBody) GameGravity(h);
	const float now{ ldf(body + kBodyGravity) };
	if (!std::isfinite(now) || now <= 0) return;
	if (body != h.gravityBody || now != h.gravityOurs) h.gravityGame = now; //the game set it again
	h.gravityBody = body;
	h.gravityOurs = h.gravityGame * kGravity;
	stf(body + kBodyGravity, h.gravityOurs);
}

//PlayerLand has added this frame's fall: only kFall of it stays
void LongerFall(Hopper& h, uint32_t ctrl)
{
	const float now{ ldf(ctrl + kFallen) };
	if (!std::isfinite(now)) return;
	if (ld32(ctrl + kPlayerState) != kStateFalling || now < h.fallen)
	{
		h.fallen = ld32(ctrl + kPlayerState) == kStateFalling ? now * kFall : 0; //a new fall
		if (h.fallen != now && h.fallen > 0) stf(ctrl + kFallen, h.fallen);
		return;
	}
	h.fallen += (now - h.fallen) * kFall;
	stf(ctrl + kFallen, h.fallen);
}

bool Free(uint32_t state)
{
	return state == kStateStanding || state == kStateMoving || state == kStateSprinting || state == kStateFalling;
}

void Trace(const Hopper& h, float gameX, float gameZ, bool floor)
{
	if (!(g_trace || g_log) || h.ctrl != local_controller()) return;
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (now - last < std::chrono::milliseconds(500)) return;
	last = now;
	rt_log("goldeneye: bunnyhop %s, speed %.2f (the game's %.2f), %s, yaw %.2f", h.active ? "on" : "off", std::hypot(h.vel[0], h.vel[1]),
		   std::hypot(gameX, gameZ), floor ? "on the floor" : "in the air", ldf(ld32(h.ctrl + kEntity) + kYaw));
}
//the walk asks whether to start the fall
void FallStart(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] };
	Hopper* h = Hops(ctrl);
	const uint32_t entity{ h ? ld32(ctrl + kEntity) : 0 };
	if (!h || !h->active || !guest_ptr(entity) || ld32(ctrl + kPlayerState) == kStateFalling)
	{
		orig_fall_start(c);
		return;
	}
	const float y{ ldf(entity + kPos + 4) };
	if (std::isfinite(y) && h->floorY - y < kFreeDrop) return;
	orig_fall_start(c);
	if (std::isfinite(y) && h->topY > y) stf(ctrl + kFallen, h->topY - y); //LongerFall takes its share of it
}

void MoveGrab(PPCContext& c)
{
	Hopper* h = Hops(c.r[3]);
	if (h && h->active && h->jump)
	{
		c.r[3] = 0;
		return;
	}
	orig_move_grab(c);
}

void SprintAllowed(PPCContext& c)
{
	if ((c.lr == kAskStart || c.lr == kAskGoOn) && Hops(c.r[3]))
	{
		c.r[3] = 0;
		return;
	}
	orig_sprint_allowed(c);
}

//on foot in a tank's level: the way's next marker where the game's own link doesn't wake it
void WakeWay()
{
	const uint32_t frame{ ld32(kFrameCount) };
	if (frame == g_wayFrame) return;
	g_wayFrame = frame;
	const uint32_t count{ std::min(ld32(kWorld + kTriggerCount), 4096u) }, triggers{ ld32(kWorld + kTriggers) };
	if (!guest_ptr(triggers)) return;
	auto marker = [&](uint32_t t) { return guest_ptr(t) && ld32(t + kTriggerType) == kObjectiveMarker && (!g_way.objective || ld32(t + kMarkerObjective) == g_way.objective); };
	auto reached = [&](uint32_t t) { return std::find(g_way.reached.begin(), g_way.reached.end(), t) != g_way.reached.end(); };
	if (g_way.awake && (ld32(g_way.awake + kTriggerFlags) & kAsleep)) //went off, maybe waking the next this frame
	{
		if (!reached(g_way.awake)) g_way.reached.push_back(g_way.awake);
		g_way.before = g_way.last;
		g_way.last = g_way.awake;
		g_way.awake = 0;
	}
	for (uint32_t i = 0; i < count && !g_way.awake; ++i)
	{
		const uint32_t t{ ld32(triggers + i * 4) };
		if (marker(t) && !(ld32(t + kTriggerFlags) & kAsleep)) g_way.awake = t;
	}
	if (g_way.awake)
	{
		g_way.objective = ld32(g_way.awake + kMarkerObjective);
		g_way.waiting = false;
		return;
	}
	if (!g_way.last) return;
	const auto now{ std::chrono::steady_clock::now() };
	if (!g_way.waiting)
	{
		g_way.waiting = true;
		g_way.stalled = now;
		return;
	}
	if (now - g_way.stalled < kWayGap) return;
	g_way.waiting = false;
	const float x{ ldf(g_way.last + kTriggerAt) }, y{ ldf(g_way.last + kTriggerAt + 4) }, z{ ldf(g_way.last + kTriggerAt + 8) };
	//the way goes on ahead: away from the one reached before, when there is one
	float ax = 0.0f, az = 0.0f;
	if (g_way.before) ax = x - ldf(g_way.before + kTriggerAt), az = z - ldf(g_way.before + kTriggerAt + 8);
	uint32_t next = 0;
	float best = 0.0f;
	bool ahead = false;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t t{ ld32(triggers + i * 4) };
		if (!marker(t) || reached(t)) continue;
		const float dx{ ldf(t + kTriggerAt) - x }, dy{ ldf(t + kTriggerAt + 4) - y }, dz{ ldf(t + kTriggerAt + 8) - z };
		const float d{ dx * dx + dy * dy + dz * dz };
		const bool front{ dx * ax + dz * az >= 0.0f };
		if (!next || (front && !ahead) || (front == ahead && d < best)) next = t, best = d, ahead = front;
	}
	g_way.last = g_way.before = 0;
	if (!next) return;
	st32(next + kTriggerFlags, ld32(next + kTriggerFlags) & ~kAsleep);
	rt_log("goldeneye: bunnyhop: on foot, the way of objective %08X goes on at marker %08X (%.0f %.0f %.0f)", g_way.objective, next, ldf(next + kTriggerAt), ldf(next + kTriggerAt + 4), ldf(next + kTriggerAt + 8));
}

//(levels, hash) -> is the player a tank there: not while bunnyhopping in the campaign
void TankLevel(PPCContext& c)
{
	const uint32_t hash{ c.r[4] };
	orig_tank_level(c);
	if (!guest_ptr(ld32(kPlayerPtr)))
	{
		g_onFoot = g_bunnyhop && !network_game();
		g_way = {};
		g_tankOnFoot = false;
	}
	if (c.r[3] == 0 || !g_onFoot || (hash & kLevelKind) != kCampaignLevel) return;
	c.r[3] = 0;
	g_tankOnFoot = true;
	if (guest_ptr(ld32(kPlayerPtr))) WakeWay();
	if (g_saidOnFoot != hash) rt_log("goldeneye: bunnyhop: level %08X is on foot, not in the tank", hash);
	g_saidOnFoot = hash;
}

//(manager, level) the player's skin as a level loads: on foot in a tank's level not the row's
void PlayerSkin(PPCContext& c)
{
	const uint32_t row{ ld32(kLevelRow) };
	bool tank{ false };
	if (g_bunnyhop && !network_game() && (c.r[4] & kLevelKind) == kCampaignLevel && guest_ptr(row))
	{
		PPCContext ask{ c };
		ask.r[3] = kLevels;
		orig_tank_level(ask);
		tank = ask.r[3] != 0;
	}
	if (!tank)
	{
		orig_player_skin(c);
		return;
	}
	const uint32_t skin{ ld32(row + kRowSkin) };
	st32(row + kRowSkin, 0);
	orig_player_skin(c);
	st32(row + kRowSkin, skin);
}

//the speed (or one over it, the last argument 0) standing up and not sprinting is the base speed's
void MoveSpeed(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] }, out{ c.r[4] };
	const bool speed{ (c.r[6] & 0xFF) != 0 };
	orig_move_speed(c);
	turbo_speed(ctrl, out, speed);
	if (!Hops(ctrl) || !guest_ptr(out) || ld8(ctrl + kSprinting) || ld8(ctrl + kCrouched) || ld8(ctrl + kSlowed)) return;
	const float now{ ldf(out) };
	if (!std::isfinite(now) || now == 0) return;
	stf(out, speed ? now * kBaseSpeed / kGameRun : now * kGameRun / kBaseSpeed);
}

//(r3 the command, r4 the stream): the key behind the game's own, in a modded network game
void CommandWrite(PPCContext& c)
{
	const uint32_t cmd{ c.r[3] }, stream{ c.r[4] };
	orig_command_write(c);
	owned_command_write(c, cmd, stream);
	if (modded_lobby()) net_write_number(c, stream, guest_ptr(cmd) && (ld32(cmd + kCmdExtra) & kCmdJump) ? 1 : 0, 1);
}

void CommandRead(PPCContext& c)
{
	const uint32_t cmd{ c.r[3] }, stream{ c.r[4] };
	orig_command_read(c);
	owned_command_read(c, cmd, stream);
	if (!modded_lobby()) return;
	const uint32_t jump{ net_read_number(c, stream, 1, 0) };
	if (guest_ptr(cmd)) st32(cmd + kCmdExtra, (ld32(cmd + kCmdExtra) & ~kCmdJump) | kCmdOurs | (jump ? kCmdJump : 0));
}

//on the floor without the key, half-life's ground move from the speed we had, its own of last frame
//or the game's walk. each frame's gain off a turn is small, so it goes on under the walk until
//it's faster: true then, the hopper's vel is the speed
bool PreStrafe(Hopper& h, uint32_t ctrl, float gameX, float gameZ, float wishX, float wishZ, float stick, float dt, bool groundOn)
{
	if (!g_now.prestrafe || ld8(ctrl + kSprinting) || ld8(ctrl + kCrouched) || ld8(ctrl + kSlowed)) return false;
	float v[2]{ gameX, gameZ };
	if (h.active) v[0] = h.vel[0], v[1] = h.vel[1];
	else if (groundOn) v[0] = h.ground[0], v[1] = h.ground[1];
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
	h.ground[0] = v[0];
	h.ground[1] = v[1];
	h.groundOn = true;
	if (std::hypot(v[0], v[1]) <= std::hypot(gameX, gameZ) + kGain) return false;
	h.vel[0] = v[0];
	h.vel[1] = v[1];
	return true;
}

} //namespace

bool bunnyhop() { return g_bunnyhop; }
bool bunnyhop_now() { return g_onNow; }
bool bhop_locked() { return g_locked; }
bool bhop_prestrafe() { return g_prestrafe; }
bool bhop_prestrafe_now() { return g_locked ? g_now.prestrafe : (bool)g_prestrafe; }
void set_bhop_prestrafe(bool on) { g_prestrafe = on; }
BhopValue& bhop_value(int which) { return g_values[which]; }
float bhop_value_now(int which) { return g_locked ? g_now.v[which] : g_values[which].value; }

bool bhop_custom_now()
{
	if (!g_onNow) return false;
	if (!g_now.prestrafe) return true;
	for (int i = 0; i < kBhopValueCount; ++i)
		if (std::fabs(g_now.v[i] - g_values[i].initial) > g_values[i].by * 0.5f) return true; //the host's come in steps
	return false;
}

void bhop_defaults()
{
	for (BhopValue& v : g_values) v.value = v.initial;
}

bool bunnyhop_speed(float& speed)
{
	if (!g_onNow || !in_play() || ld32(kFrameCount) - g_speedFrame >= 4) return false;
	speed = g_speed;
	return true;
}
void set_bunnyhop(bool on) { g_bunnyhop = on; }

//after a controller's update: the local one's offline, any one's online
void bunnyhop_apply(uint32_t ctrl)
{
	Refresh();
	const uint32_t entity{ guest_ptr(ctrl) ? ld32(ctrl + kEntity) : 0 };
	const uint32_t body{ guest_ptr(entity) ? ld32(entity + kBody) : 0 };
	Hopper* h = Hops(ctrl);
	if (!h || !guest_ptr(body))
	{
		if (Hopper* had = Of(ctrl))
		{
			GameGravity(*had);
			had->fallen = 0;
		}
		return;
	}
	LowGravity(*h, body);
	LongerFall(*h, ctrl);
}
void bunnyhop_buttons(const ClassicState& s) { g_jump = (s.extra & kExtraJump) != 0; }

//a command of this console's is filled: the key goes in
void bunnyhop_command(uint32_t cmd)
{
	if (guest_ptr(cmd)) st32(cmd + kCmdExtra, (ld32(cmd + kCmdExtra) & ~kCmdJump) | kCmdOurs | (g_jump ? kCmdJump : 0));
}

//(controller, command) before ge_input_copy: the key is the controller's, and the game copies
//+0x3C without it. what was there, for bunnyhop_input_done
uint32_t bunnyhop_input(uint32_t ctrl, uint32_t cmd)
{
	if (guest_ptr(cmd) && Held(ctrl))
	{
		for (uint32_t i = 0; i < kInputStickCount; ++i) stf(cmd + kInputSticks + 4 * i, 0.0f);
		for (uint32_t i = 0; i < 3; ++i) st8(cmd + kInputBytes + i, 0);
		st32(cmd + kInputButtons, 0);
		st32(cmd + kInputHeld, 0);
	}
	if (!network_game() || !guest_ptr(ctrl) || !guest_ptr(cmd)) return 0;
	const uint32_t extra{ ld32(cmd + kCmdExtra) };
	if (!(extra & kCmdOurs)) return 0;
	st32(cmd + kCmdExtra, extra & ~(kCmdOurs | kCmdJump));
	Hopper& h = Make(ctrl);
	if (g_trace && !h.commanded) rt_log("goldeneye: bunnyhop: %08X is stepped from commands with the jump key in them", ctrl);
	h.commanded = true;
	h.jump = (extra & kCmdJump) != 0;
	return extra;
}
void bunnyhop_input_done(uint32_t cmd, uint32_t kept)
{
	if (kept) st32(cmd + kCmdExtra, kept);
}

//a player's state goes out from the host: its own player's has the rules behind it, a bit says so
void bunnyhop_write(PPCContext& c, uint32_t stream, bool rules)
{
	Refresh();
	net_write_number(c, stream, rules ? 1 : 0, 1);
	if (!rules) return;
	net_write_number(c, stream, g_now.on ? 1 : 0, 1);
	net_write_number(c, stream, g_now.prestrafe ? 1 : 0, 1);
	for (int i = 0; i < kBhopValueCount; ++i) net_write_number(c, stream, StepOf(i, g_now.v[i]), StepOf(i, g_values[i].hi));
}

void bunnyhop_read(PPCContext& c, uint32_t stream)
{
	if (net_read_number(c, stream, 1, 0))
	{
		Rules r;
		r.on = net_read_number(c, stream, 1, 0) != 0;
		r.prestrafe = net_read_number(c, stream, 1, 0) != 0;
		for (int i = 0; i < kBhopValueCount; ++i) r.v[i] = AtStep(i, net_read_number(c, stream, StepOf(i, g_values[i].hi), StepOf(i, g_values[i].initial)));
		const bool news{ !g_hostKnown || r.on != g_host.on };
		g_host = r;
		g_hostKnown = true;
		if (news) rt_log("goldeneye: bunnyhop: the host's is %s (base speed %.1f, hop %.2f, gravity %.2f, air cap %.2f, top speed %.0f)", r.on ? "on" : "off",
						 r.v[kBhopSpeed], r.v[kBhopHeight], r.v[kBhopGravity], r.v[kBhopAirCap], r.v[kBhopMax]);
	}
}

//after ge_look_apply, its step is in the body
void bunnyhop_look(uint32_t ctrl)
{
	Hopper* hp = Hops(ctrl);
	const uint32_t entity{ ld32(ctrl + kEntity) };
	const uint32_t body{ guest_ptr(entity) ? ld32(entity + kBody) : 0 };
	const float dt{ ldf(ctrl + kTimeStep) };
	if (!guest_ptr(body) || !(dt > 0.0005f && dt < 0.2f))
	{
		if (hp) hp->active = false;
		return;
	}
	if (!hp) return;
	Hopper& h = *hp;
	const bool groundOn{ h.groundOn };
	h.groundOn = false;
	if (!Free(ld32(ctrl + kPlayerState)))
	{
		h.active = false;
		return;
	}
	if (entity != h.entity) h.active = false;
	h.entity = entity;
	const bool local{ ctrl == local_controller() };

	const float gameX{ ldf(body + kBodyStep) / dt }, gameZ{ ldf(body + kBodyStep + 8) / dt };
	const float stickX{ ldf(ctrl + kMoveStick) }, stickY{ ldf(ctrl + kMoveStick + 4) };
	const float yaw{ ldf(entity + kYaw) };
	if (!std::isfinite(gameX) || !std::isfinite(gameZ) || !std::isfinite(stickX) || !std::isfinite(stickY) || !std::isfinite(yaw))
	{
		h.active = false;
		return;
	}
	const bool floor{ (ld16(body + kBodyFloor) & 1) != 0 };
	if (local)
	{
		g_speed = std::hypot(gameX, gameZ);
		g_speedFrame = ld32(kFrameCount);
	}
	const float posY{ ldf(entity + kPos + 4) };
	if (std::isfinite(posY))
	{
		if (floor || !h.active) h.floorY = h.topY = posY;
		else h.topY = std::max(h.topY, posY);
	}
	const float stick{ std::min(1.0f, std::hypot(stickX, stickY)) };
	//the stick in the world, as noclip.cpp has it
	const float wishX{ stick > 0 ? (std::cos(yaw) * stickX + std::sin(yaw) * stickY) / stick : 0 };
	const float wishZ{ stick > 0 ? (-std::sin(yaw) * stickX + std::cos(yaw) * stickY) / stick : 0 };

	//a wall, a ceiling or a ramp the body touched takes the speed into it, as half-life's
	//ClipVelocity: the push out of it (+0xEC) is its normal, and the speed is ours across and the
	//body's fall speed up. a ramp too steep to stand on turns the speed across into speed up it,
	//and gravity pulls along it: surfing
	if (h.active && (ld16(body + kBodyFloor) & kBodyHit) != 0)
	{
		float n[3]{ ldf(body + kBodyPush), ldf(body + kBodyPush + 4), ldf(body + kBodyPush + 8) };
		const float push{ std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) };
		if (std::isfinite(push) && push > 1e-5f && n[1] / push < kWalkable)
		{
			for (float& f : n) f /= push;
			float v[3]{ h.vel[0], ldf(body + kBodyFallSpeed), h.vel[1] };
			const float into{ v[0] * n[0] + v[1] * n[1] + v[2] * n[2] };
			if (std::isfinite(v[1]) && into < 0)
			{
				if ((g_trace || g_log) && local && into < -1.0f)
					rt_log("goldeneye: bunnyhop: the body's flags %X, normal %.2f %.2f %.2f took %.2f of %.2f", ld16(body + kBodyFloor), n[0], n[1], n[2], -into,
						   std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
				for (int i = 0; i < 3; ++i) v[i] -= into * n[i];
				h.vel[0] = v[0];
				h.vel[1] = v[2];
				stf(body + kBodyFallSpeed, v[1]);
			}
			if (n[1] > kRamp && std::isfinite(posY)) //on a ramp, the fall starts over
			{
				h.floorY = h.topY = posY;
				if (ld32(ctrl + kPlayerState) == kStateFalling) stf(ctrl + kFallen, 0.0f);
				h.fallen = 0;
			}
		}
	}

	if (floor)
	{
		if (!h.jump) //a landing from the game's fall with the key held hops on: the hurt is done, its stall runs out in the air
		{
			h.active = PreStrafe(h, ctrl, gameX, gameZ, wishX, wishZ, stick, dt, groundOn);
			if (!h.active)
			{
				Trace(h, gameX, gameZ, floor);
				return;
			}
		}
		else
		{
			//a hop off the floor: the speed we had, or the game's walk if that's faster
			if (!h.active || std::hypot(h.vel[0], h.vel[1]) < std::hypot(gameX, gameZ))
			{
				h.vel[0] = gameX;
				h.vel[1] = gameZ;
			}
			h.active = true;
			const float gravity{ ldf(body + kBodyGravity) };
			if (std::isfinite(gravity) && gravity > 0) stf(body + kBodyFallSpeed, std::sqrt(2.0f * gravity * kHopHeight));
		}
	}
	else
	{
		if (!h.active)
		{
			if (!h.jump)
			{
				Trace(h, gameX, gameZ, floor);
				return;
			}
			h.vel[0] = gameX;
			h.vel[1] = gameZ;
			h.active = true;
		}
		if (stick >= kDeadZone)
		{
			const float wishSpeed{ kWishSpeed * stick };
			const float add{ std::min(wishSpeed, kAirCap) - (h.vel[0] * wishX + h.vel[1] * wishZ) };
			if (add > 0)
			{
				const float accel{ std::min(add, kAirAccel * wishSpeed * dt) };
				h.vel[0] += accel * wishX;
				h.vel[1] += accel * wishZ;
			}
		}
	}

	const float speed{ std::hypot(h.vel[0], h.vel[1]) };
	if (speed > kMaxSpeed)
	{
		h.vel[0] *= kMaxSpeed / speed;
		h.vel[1] *= kMaxSpeed / speed;
	}
	if (local) g_speed = std::hypot(h.vel[0], h.vel[1]);
	stf(body + kBodyStep, h.vel[0] * dt);
	stf(body + kBodyStep + 8, h.vel[1] * dt);
	Trace(h, gameX, gameZ, floor);
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
	orig_player_skin = ppc_hook("ge_player_skin", PlayerSkin);
	if (orig_player_skin) orig_tank_level = ppc_hook("ge_tank_level", TankLevel); //without it Bond would get the tank's skin
	if (!orig_tank_level) rt_log("goldeneye: bunnyhop: ge_tank_level or ge_player_skin isn't hooked, the tank's mission is in the tank");
	//both or neither: a host that reads the key and a console that doesn't write it can't play together
	orig_command_write = ppc_hook("ge_command_write", CommandWrite);
	orig_command_read = ppc_hook("ge_command_read", CommandRead);
	if (!orig_command_write || !orig_command_read) rt_die("goldeneye: ge_command_write and ge_command_read are not hooked in this build");
}

} //namespace ge
