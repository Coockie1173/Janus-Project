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
//Half-Life Crouch (off by default, then crouch is the game's): crouched with the key held, stood up
//without it, when there's room. it goes through the game's own (fn_8010FF60), so it comes no
//faster than the game's 0.25 s (+0xC88). in the air it's at once and it's the legs: the crouched capsule is 0.62 shorter with its feet in the same
//place, half-life's hull shrinks to the middle, so the body goes up kTuck crouching and down
//kTuck standing up, nothing gained by doing it over and over. it doesn't stop the speed: a landing
//crouched slides on half-life's friction like any other, and crouched the air accelerates at kDuck
//
//Hovering (off by default): fn_801066C0 puts the fall speed at 0. the game calls it for a crouch, for
//the move state's setter (a reload, a weapon cycle) and as the fall starts, which is the hover
//kFreeDrop down. off, in the air those three keep the fall speed (the hook's others are climbs
//and the like, which want it gone)
//
//Ledge Hops (off by default): half-life's feet are a box, ours the bottom of a capsule, so a landing
//on a thin ledge or the rim of one meets its edge, not its top. ge_contacts_solve drops a floor's
//contact that isn't flat on (806F1DA4) and only the wall under it pushes, up and off, with no floor
//flag. so a floor's contact with the push going up (kLedgeUp, a capsule's side passing a ledge
//has it level) is seen before the solve, and falling with the key held that's a floor for the
//hop: it's not clipped as a ramp, and like a ramp the fall starts over. real surf ramps aren't
//floors, so they have no such contact
//
//ladders are half-life's: the game's own (walked into from the foot, ge_ladder_foot, or backed off
//the top, ge_move_grab's 0x100) are never taken. a ladder is its pieces, polygons with flags 0x1000
//(see eurochef's goldeneye_wii.md), and the player is on one while a piece is within kLadderReach
//in front of the feet or the chest: the search ge_ladder_foot makes, asked at those two points.
//on it there's no gravity, the stick moves the player along where they look, pitch and all, and
//what of that goes into the ladder goes up it instead (half-life's PM_LadderMove): so forward
//looking up climbs, looking far down goes down, sideways strafes along it. backing off on the
//floor walks away, past the top the speed up carries the player over, and the jump key pushes
//off it (held, as while hopping, the player doesn't stay on it). the climb is kClimb of the base
//speed, half-life's 200 of its 320
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
//GE_BHOP_PRESTRAFE=0 without pre-strafing, GE_BHOP_CROUCH=1, GE_BHOP_HOVER=1 and GE_BHOP_LEDGE=1 with those, GE_BHOP_SPEED, GE_BHOP_HEIGHT, GE_BHOP_AIRCAP, GE_BHOP_AIRACCEL, GE_BHOP_WISH, GE_BHOP_MAX, GE_BHOP_GRAVITY,
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
const uint32_t kCrouchBlend = 0xC80; //the view's way down, 1 crouched
const uint32_t kActPressed = 0x534, kActHeld = 0x538, kActCrouch = 0x20; //ge_action_update's buttons, see the top
const uint32_t kActions = 0x805D4020, kActionSize = 0x1C, kActionNone = 0x46500001;
const uint32_t kCrouchOurs = 1, kCrouchPressed = 2, kCrouchHeld = 4; //what bunnyhop_crouch keeps
const uint32_t kAskStart = 0x8010D9B0, kAskGoOn = 0x8010DA8C; //ge_sprint_update's calls of ge_sprint_allowed
const float kGameRun = 4.2f; //806E947C
const uint32_t kView = 0x16C, kViewPitch = 0x768; //the entity's view, its pitch (down is positive)
const uint32_t kGrabbed = 0xA30, kGrabLadder = 0x100; //ge_move_grab's find, a ladder's top
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
	bool on = false, prestrafe = true, crouch = false, hover = false, ledge = false;
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
const float kDuck = 0.333f; //of the stick's speed crouched, half-life's
const float kTuck = 0.31f; //half of what the crouched capsule is shorter (1.65 and 1.03), the feet stay where they are in both
const float kClimb = 0.625f, kPushOff = 0.84f; //of the base speed: half-life's climb and jump off a ladder, of its run
const float kLadderReach = 0.6f; //a ladder's piece this far in front of the player has them
const float kLadderAt[2]{ 0.3f, 1.1f }; //the points above the feet it's looked for at
const float kWalkable = 0.7f; //a normal this far up is a floor, half-life's
const float kRamp = 0.05f; //and this far up a ramp, not a wall
const float kLedgeUp = 0.1f; //a floor's edge pushing this far up is under the player
const uint32_t kExtraJump = 1u << 5;
const uint32_t kCmdExtra = 0x3C, kCmdOurs = 0x80000000, kCmdJump = 0x40000000; //see the top

enum Mode { kOffline, kOff, kHosting, kJoined };
Mode g_mode = kOffline;
uint32_t g_rulesFrame = ~0u;
Rules g_host; //as the host last sent them
bool g_hostKnown = false;

std::atomic<bool> g_bunnyhop{false}; //the menu's, ours
std::atomic<bool> g_prestrafe{true}, g_crouch{false}, g_hover{false}, g_ledge{false};
std::atomic<bool> g_onNow{false}, g_locked{false}; //for the menu's and the renderer's threads

//a level can have its own rules (eurochef's bhop... nodes): a trigger the game never runs, the
//generic one (type 3) asleep (game flag 1), with kPresetMark in data 1. data 2 has bit 0 for
//bunnyhopping on, bit 1 for pre-strafing, 2 Half-Life Crouch, 3 Hovering and 4 Ledge Hops, data 5 to 15 the tunables as floats in the enum's order,
//a value that isn't there being its default (the ones after the mark that the type's maker
//leaves alone: it changes data 0 and 4). what's in the menu isn't touched, so the player's own
//are back as soon as the level is left. while there the menu shows them, locked, a host plays by
//them and sends them as its own, and a joiner has them from the host as ever
const uint32_t kWorld = 0x805F4CCC, kTriggerCount = 0xE0, kTriggers = 0xE4, kTriggerType = 0x58;
const uint32_t kTriggerValues = 0x64, kTriggerData = 0x98, kGenericTrigger = 3, kPresetMark = 0x42484F50; //BHOP
const uint32_t kCurrentLevel = 0x805D1804;
const int kPresetMarkSlot = 1, kPresetFlagsSlot = 2, kPresetFirstValue = 5;
Rules g_mapRules;
std::atomic<uint32_t> g_mapLevel{0}; //the level g_mapRules are for, 0 with none
uint32_t g_mapLooked = 0, g_mapCount = 0;

//when the level or the number of triggers changes
void FindMapRules()
{
	const uint32_t level = ld32(kCurrentLevel), count = ld32(kWorld + kTriggerCount), list = ld32(kWorld + kTriggers);
	if (level == g_mapLooked && count == g_mapCount) return;
	g_mapLooked = level;
	g_mapCount = count;
	const uint32_t before = g_mapLevel;
	g_mapLevel = 0;
	for (uint32_t i = 0; i < count && count < 0x4000 && guest_ptr(list); ++i)
	{
		const uint32_t t = ld32(list + i * 4);
		if (!guest_ptr(t) || ld32(t + kTriggerType) != kGenericTrigger || ld32(t + kTriggerData + kPresetMarkSlot * 4) != kPresetMark) continue;
		const uint32_t flags = ld32(t + kTriggerData + kPresetFlagsSlot * 4), given = ld32(t + kTriggerValues);
		g_mapRules.on = flags & 1;
		g_mapRules.prestrafe = flags & 2;
		g_mapRules.crouch = flags & 4;
		g_mapRules.hover = flags & 8;
		g_mapRules.ledge = flags & 16;
		std::string said;
		for (int v = 0; v < kBhopValueCount; ++v)
		{
			const int slot = kPresetFirstValue + v;
			const float value = (given >> slot & 1) ? ldf(t + kTriggerData + slot * 4) : g_values[v].initial;
			g_mapRules.v[v] = std::clamp(value, g_values[v].lo, g_values[v].hi);
			said += " " + std::to_string(g_mapRules.v[v]);
		}
		g_mapLevel = level;
		rt_log("goldeneye: bunnyhop: level %08X has its own rules, %s, pre-strafing %s:%s", level, g_mapRules.on ? "on" : "off", g_mapRules.prestrafe ? "on" : "off", said.c_str());
		break;
	}
	if (before && !g_mapLevel) rt_log("goldeneye: bunnyhop: back to the player's rules");
}
std::atomic<bool> g_jump{false}; //as the pad was last read
std::atomic<float> g_speed{0}; //the speedometer's, read by the renderer's thread
std::atomic<uint32_t> g_speedFrame{~0u};
bool g_trace = false;
bool g_log = true; //the speed in the log, twice a second
PPCFunc orig_body_still = nullptr;
const uint32_t kStillCrouch = 0x80110128, kStillFall = 0x80121360, kStillMove = 0x8010D69C; //its callers that hover, see the top
PPCFunc orig_fall_start = nullptr, orig_move_grab = nullptr, orig_sprint_allowed = nullptr, orig_move_speed = nullptr;
PPCFunc orig_command_write = nullptr, orig_command_read = nullptr, orig_contacts_solve = nullptr;
const uint32_t kScoopContacts = 0x70, kScoopCount = 0x74, kContactSize = 0x50, kContactFrom = 0x10, kContactTo = 0x20, kContactDepth = 0x40, kContactFlags = 0x4E;
const uint32_t kContactLive = 1, kContactFloor = 2, kContactIgnored = 8;
PPCFunc orig_ladder_foot = nullptr, search_near = nullptr, search_all = nullptr;
const uint32_t kSearchNear = 0x8027D6F0, kSearchAll = 0x8032E5E0; //what ge_ladder_foot asks the world
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
const uint32_t kTriggerFlags = 0x68, kAsleep = 1, kMarkerObjective = 0x9C;
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
	bool ladder = false; //on one, see the top
	bool crouched = false, inAir = false; //as last frame left them
	bool ledge = false; //a floor's edge under it since the last look, see the top
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
	r.crouch = g_crouch;
	r.hover = g_hover;
	r.ledge = g_ledge;
	for (int i = 0; i < kBhopValueCount; ++i) r.v[i] = steps ? AtStep(i, StepOf(i, g_values[i].value)) : g_values[i].value;
	return r;
}

//once a frame, before anything reads the rules
void Refresh()
{
	const uint32_t frame = ld32(kFrameCount);
	if (frame == g_rulesFrame) return;
	g_rulesFrame = frame;
	FindMapRules();
	Mode mode = kOffline;
	if (network_game()) mode = !modded_lobby() ? kOff : joined_game() ? kJoined : kHosting;
	else g_hostKnown = false;
	switch (mode)
	{
	case kOffline: g_now = g_mapLevel ? g_mapRules : Own(false); break;
	case kHosting: g_now = g_mapLevel ? g_mapRules : Own(true); break;
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
	g_locked = mode == kJoined || (mode != kOff && g_mapLevel);
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
	const bool ours{ body == h.gravityBody && now == h.gravityOurs };
	if (!std::isfinite(now) || (now <= 0 && !ours)) return;
	if (!ours) h.gravityGame = now; //the game set it again
	h.gravityBody = body;
	h.gravityOurs = h.ladder ? 0.0f : h.gravityGame * kGravity;
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
	if (h->ladder) return;
	const float y{ ldf(entity + kPos + 4) };
	if (std::isfinite(y) && h->floorY - y < kFreeDrop) return;
	orig_fall_start(c);
	if (std::isfinite(y) && h->topY > y) stf(ctrl + kFallen, h->topY - y); //LongerFall takes its share of it
}

//(controller) the fall speed at 0, unless Hovering is off and it's one that hovers
void BodyStill(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] };
	const bool hovers{ c.lr == kStillCrouch || c.lr == kStillFall || c.lr == kStillMove };
	Hopper* h = hovers ? Hops(ctrl) : nullptr;
	const uint32_t entity{ h ? ld32(ctrl + kEntity) : 0 };
	const uint32_t body{ guest_ptr(entity) ? ld32(entity + kBody) : 0 };
	const bool keep{ guest_ptr(body) && h && !g_now.hover && h->inAir && !h->ladder };
	const float fall{ keep ? ldf(body + kBodyFallSpeed) : 0.0f };
	orig_body_still(c);
	if (keep) stf(body + kBodyFallSpeed, fall);
}

//a floor's edge under the player, before the solve drops it: see the top
void ContactsSolve(PPCContext& c)
{
	const uint32_t scoop{ c.r[3] }, body{ c.r[4] };
	const uint32_t entity{ g_now.on && g_now.ledge && guest_ptr(body) ? ld32(body + 4) : 0 };
	for (Hopper& h : g_hoppers)
	{
		if (!entity || h.entity != entity || !h.jump || h.ledge) continue;
		const uint32_t contacts{ ld32(scoop + kScoopContacts) }, count{ ld32(scoop + kScoopCount) };
		for (uint32_t i = 0; i < count && guest_ptr(contacts); ++i)
		{
			const uint32_t contact{ contacts + i * kContactSize };
			if ((ld8(contact + kContactFlags) & (kContactLive | kContactFloor | kContactIgnored)) != (kContactLive | kContactFloor) || !(ldf(contact + kContactDepth) > 0)) continue;
			float push[3];
			for (uint32_t k = 0; k < 3; ++k) push[k] = ldf(contact + kContactTo + k * 4) - ldf(contact + kContactFrom + k * 4);
			if (ldf(contact) * push[0] + ldf(contact + 4) * push[1] + ldf(contact + 8) * push[2] < 0) push[1] = -push[1];
			const float length{ std::sqrt(push[0] * push[0] + push[1] * push[1] + push[2] * push[2]) };
			if (length > 1e-5f && push[1] / length > kLedgeUp) h.ledge = true;
		}
	}
	orig_contacts_solve(c);
}

void MoveGrab(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] };
	Hopper* h = Hops(ctrl);
	if (h && h->active && h->jump)
	{
		c.r[3] = 0;
		return;
	}
	orig_move_grab(c);
	if (h && search_all && (ld16(ctrl + kGrabbed) & kGrabLadder)) c.r[3] = 0;
}

void LadderFoot(PPCContext& c)
{
	if (search_all && Hops(c.r[3]))
	{
		c.r[3] = 0;
		return;
	}
	orig_ladder_foot(c);
}

//a ladder's piece within reach of the point: ge_ladder_foot's search, in a frame below the caller's.
//its normal (towards the side it's climbed from) and how far in front of it the place is
bool LadderAt(PPCContext& c, const float at[3], const float place[3], float n[3], float& ahead)
{
	const uint32_t stack{ c.r[1] }, frame{ (stack - 0x120) & ~0xFu }, record{ frame + 0x10 };
	st32(frame, stack);
	for (uint32_t i = 0; i < 0xF0; i += 4) st32(record + i, 0);
	for (int k = 0; k < 3; ++k) stf(record + 4 * k, at[k]);
	st32(record + 0x10, 2); //polygons
	stf(record + 0x14, kLadderReach);
	st32(record + 0x1C, 0x400);
	PPCContext call{ c };
	call.r[1] = frame;
	call.r[3] = kWorld;
	call.r[4] = record;
	search_near(call);
	call = c;
	call.r[1] = frame;
	call.r[3] = kWorld;
	call.r[4] = record;
	search_all(call);
	const uint32_t type{ ld32(record + 0x24) };
	if ((type != 2 && type != 4) || !(ld16(record + 0x28) & 0x1000)) return false;
	float corner[3][3];
	for (int i = 0; i < 3; ++i)
		for (int k = 0; k < 3; ++k) corner[i][k] = ldf(record + 0x50 + 0x10 * i + 4 * k);
	float u[3], v[3];
	for (int k = 0; k < 3; ++k) u[k] = corner[1][k] - corner[0][k], v[k] = corner[2][k] - corner[0][k];
	n[0] = u[1] * v[2] - u[2] * v[1];
	n[1] = u[2] * v[0] - u[0] * v[2];
	n[2] = u[0] * v[1] - u[1] * v[0];
	const float length{ std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) };
	if (!std::isfinite(length) || length < 1e-6f) return false;
	for (int k = 0; k < 3; ++k) n[k] /= length;
	if (std::fabs(n[1]) > kWalkable) return false; //a ladder stands up
	ahead = 0;
	for (int k = 0; k < 3; ++k) ahead += n[k] * (place[k] - corner[0][k]);
	return ahead > 0.05f && ahead < kLadderReach;
}

//on a ladder: the stick along the look, what goes into the ladder up it (half-life's PM_LadderMove)
void Climb(Hopper& h, uint32_t ctrl, uint32_t entity, uint32_t body, const float n[3], float stickX, float stickY, float yaw, bool floor, float dt)
{
	const uint32_t view{ ld32(entity + kView) };
	float pitch{ guest_ptr(view) ? ldf(view + kViewPitch) : 0.0f };
	if (!std::isfinite(pitch)) pitch = 0;
	const float climb{ kBaseSpeed * kClimb };
	//half-life's forward and strafe are keys, each the whole climb: the stick's circle made a square,
	//so a full diagonal at a ladder seen at 45 degrees climbs at 1.41 of it (strafing up it)
	const float stick{ std::hypot(stickX, stickY) };
	const float square{ stick >= kDeadZone ? std::min(stick, 1.0f) / std::max(std::fabs(stickX), std::fabs(stickY)) : 0.0f };
	const float forward{ stickY * square * climb }, side{ stickX * square * climb };
	float v[3]{ std::sin(yaw) * std::cos(pitch) * forward + std::cos(yaw) * side, -std::sin(pitch) * forward, std::cos(yaw) * std::cos(pitch) * forward - std::sin(yaw) * side };
	const float into{ v[0] * n[0] + v[1] * n[1] + v[2] * n[2] };
	//along the ladder and level, and up it
	float across[3]{ n[2], 0.0f, -n[0] };
	const float length{ std::hypot(across[0], across[2]) };
	if (length > 1e-6f) across[0] /= length, across[2] /= length;
	const float up[3]{ n[1] * across[2] - n[2] * across[1], n[2] * across[0] - n[0] * across[2], n[0] * across[1] - n[1] * across[0] };
	for (int k = 0; k < 3; ++k) v[k] += -into * n[k] - into * up[k];
	if (floor && into > 0) //backing off it on the floor walks away
	{
		for (int k = 0; k < 3; ++k) v[k] += climb * n[k];
	}
	h.vel[0] = v[0];
	h.vel[1] = v[2];
	h.active = true;
	h.fallen = 0;
	stf(ctrl + kFallen, 0.0f);
	stf(body + kBodyStep, v[0] * dt);
	stf(body + kBodyStep + 8, v[2] * dt);
	stf(body + kBodyFallSpeed, v[1]);
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
	if (!g_now.prestrafe || ld8(ctrl + kSprinting) || (ld8(ctrl + kCrouched) && !g_now.crouch) || ld8(ctrl + kSlowed)) return false;
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
//out of play nothing calls Refresh, so a level left is seen here
bool bhop_map_locked() { const uint32_t level = g_mapLevel; return level && level == ld32(kCurrentLevel) && (g_mode == kOffline || g_mode == kHosting); }
bool bhop_locked() { return g_mode == kJoined ? (bool)g_locked : bhop_map_locked(); }
bool bhop_prestrafe() { return g_prestrafe; }
bool bhop_prestrafe_now() { return bhop_locked() ? g_now.prestrafe : (bool)g_prestrafe; }
void set_bhop_prestrafe(bool on) { g_prestrafe = on; }
bool bhop_crouch() { return g_crouch; }
bool bhop_crouch_now() { return bhop_locked() ? g_now.crouch : (bool)g_crouch; }
void set_bhop_crouch(bool on) { g_crouch = on; }
bool bhop_hover() { return g_hover; }
bool bhop_hover_now() { return bhop_locked() ? g_now.hover : (bool)g_hover; }
void set_bhop_hover(bool on) { g_hover = on; }
bool bhop_ledge() { return g_ledge; }
bool bhop_ledge_now() { return bhop_locked() ? g_now.ledge : (bool)g_ledge; }
void set_bhop_ledge(bool on) { g_ledge = on; }
BhopValue& bhop_value(int which) { return g_values[which]; }
float bhop_value_now(int which) { return bhop_locked() ? g_now.v[which] : g_values[which].value; }

bool bhop_custom_now()
{
	if (!g_onNow || bhop_map_locked()) return false; //the level's own rules are the rules
	if (!g_now.prestrafe || g_now.crouch || g_now.hover || g_now.ledge) return true;
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

void bunnyhop_stop(uint32_t ctrl, float y)
{
	Hopper* h = Of(ctrl);
	if (!h) return;
	h->active = false;
	h->vel[0] = h->vel[1] = h->ground[0] = h->ground[1] = 0.0f;
	h->fallen = 0.0f;
	h->floorY = h->topY = y;
}

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

//before ge_action_update: crouched with the key held, stood up without it. while the two differ
//the game gets a press of its crouch, so it takes its own wait and looks for room itself. what the
//buttons were, for bunnyhop_crouch_done
uint32_t bunnyhop_crouch(uint32_t ctrl)
{
	if (!Hops(ctrl) || !g_now.crouch) return 0;
	for (uint32_t a = kActions; a < kActions + 4 * kActionSize; a += kActionSize)
	{
		if (ld32(a) != kActionNone && (ld32(a + 0x10) & kActCrouch)) return 0; //the key does that now
	}
	const uint32_t pressed{ ld32(ctrl + kActPressed) }, held{ ld32(ctrl + kActHeld) };
	const bool press{ ((held & kActCrouch) != 0) != (ld8(ctrl + kCrouched) != 0) };
	st32(ctrl + kActPressed, press ? pressed | kActCrouch : pressed & ~kActCrouch);
	st32(ctrl + kActHeld, press ? held | kActCrouch : held);
	return kCrouchOurs | (pressed & kActCrouch ? kCrouchPressed : 0) | (held & kActCrouch ? kCrouchHeld : 0);
}
void bunnyhop_crouch_done(uint32_t ctrl, uint32_t kept)
{
	if (!kept) return;
	st32(ctrl + kActPressed, (ld32(ctrl + kActPressed) & ~kActCrouch) | (kept & kCrouchPressed ? kActCrouch : 0));
	st32(ctrl + kActHeld, (ld32(ctrl + kActHeld) & ~kActCrouch) | (kept & kCrouchHeld ? kActCrouch : 0));
}

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
	net_write_number(c, stream, g_now.crouch ? 1 : 0, 1);
	net_write_number(c, stream, g_now.hover ? 1 : 0, 1);
	net_write_number(c, stream, g_now.ledge ? 1 : 0, 1);
	for (int i = 0; i < kBhopValueCount; ++i) net_write_number(c, stream, StepOf(i, g_now.v[i]), StepOf(i, g_values[i].hi));
}

void bunnyhop_read(PPCContext& c, uint32_t stream)
{
	if (net_read_number(c, stream, 1, 0))
	{
		Rules r;
		r.on = net_read_number(c, stream, 1, 0) != 0;
		r.prestrafe = net_read_number(c, stream, 1, 0) != 0;
		r.crouch = net_read_number(c, stream, 1, 0) != 0;
		r.hover = net_read_number(c, stream, 1, 0) != 0;
		r.ledge = net_read_number(c, stream, 1, 0) != 0;
		for (int i = 0; i < kBhopValueCount; ++i) r.v[i] = AtStep(i, net_read_number(c, stream, StepOf(i, g_values[i].hi), StepOf(i, g_values[i].initial)));
		const bool news{ !g_hostKnown || r.on != g_host.on };
		g_host = r;
		g_hostKnown = true;
		if (news) rt_log("goldeneye: bunnyhop: the host's is %s (base speed %.1f, hop %.2f, gravity %.2f, air cap %.2f, top speed %.0f)", r.on ? "on" : "off",
						 r.v[kBhopSpeed], r.v[kBhopHeight], r.v[kBhopGravity], r.v[kBhopAirCap], r.v[kBhopMax]);
	}
}

//after ge_look_apply, its step is in the body
void bunnyhop_look(PPCContext& c, uint32_t ctrl)
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
	const bool groundOn{ h.groundOn }, ledge{ h.ledge };
	h.groundOn = false;
	h.ledge = false;
	const bool crouched{ ld8(ctrl + kCrouched) != 0 }, crouchChanged{ crouched != h.crouched };
	h.crouched = crouched;
	if (!Free(ld32(ctrl + kPlayerState)))
	{
		h.active = false;
		if (h.ladder) h.ladder = false, LowGravity(h, body);
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
	const bool onFloor{ (ld16(body + kBodyFloor) & 1) != 0 };
	const bool onLedge{ !onFloor && ledge && h.jump && !h.ladder && ldf(body + kBodyFallSpeed) <= 0 };
	const bool floor{ onFloor || onLedge };
	if (local)
	{
		g_speed = std::hypot(gameX, gameZ);
		g_speedFrame = ld32(kFrameCount);
	}
	float posY{ ldf(entity + kPos + 4) };
	if (g_now.crouch && crouchChanged && h.inAir && !h.ladder && std::isfinite(posY)) //half-life's: in the air it's at once, the legs and not the head
	{
		posY += crouched ? kTuck : -kTuck;
		stf(entity + kPos + 4, posY);
		stf(ctrl + kCrouchBlend, crouched ? 1.0f : 0.0f);
	}
	h.inAir = !floor;
	if (std::isfinite(posY))
	{
		if (floor || !h.active) h.floorY = h.topY = posY;
		else h.topY = std::max(h.topY, posY);
	}
	const float stick{ std::min(1.0f, std::hypot(stickX, stickY)) };
	//the stick in the world, as noclip.cpp has it
	const float wishX{ stick > 0 ? (std::cos(yaw) * stickX + std::sin(yaw) * stickY) / stick : 0 };
	const float wishZ{ stick > 0 ? (-std::sin(yaw) * stickX + std::cos(yaw) * stickY) / stick : 0 };

	const bool wasLadder{ h.ladder };
	float n[3]{};
	h.ladder = false;
	if (search_near && search_all && std::isfinite(posY))
	{
		const float place[3]{ ldf(entity + kPos), posY, ldf(entity + kPos + 8) };
		float ahead{ 0 };
		for (float above : kLadderAt)
		{
			const float at[3]{ place[0], posY + above, place[2] };
			if (LadderAt(c, at, place, n, ahead)) { h.ladder = true; break; }
		}
	}
	if (h.ladder && h.jump) //off it
	{
		h.ladder = false;
		h.vel[0] = n[0] * kBaseSpeed * kPushOff;
		h.vel[1] = n[2] * kBaseSpeed * kPushOff;
		h.active = true;
		h.floorY = h.topY = posY;
	}
	if (h.ladder != wasLadder)
	{
		LowGravity(h, body);
		if ((g_trace || g_log) && local) rt_log("goldeneye: bunnyhop: %s a ladder at %.2f %.2f %.2f", h.ladder ? "on" : "off", ldf(entity + kPos), posY, ldf(entity + kPos + 8));
	}
	if (h.ladder)
	{
		Climb(h, ctrl, entity, body, n, stickX, stickY, yaw, floor, dt);
		h.floorY = h.topY = posY;
		if (local) g_speed = std::hypot(h.vel[0], h.vel[1]);
		return;
	}

	//a wall, a ceiling or a ramp the body touched takes the speed into it, as half-life's
	//ClipVelocity: the push out of it (+0xEC) is its normal, and the speed is ours across and the
	//body's fall speed up. a ramp too steep to stand on turns the speed across into speed up it,
	//and gravity pulls along it: surfing
	if (h.active && !onLedge && (ld16(body + kBodyFloor) & kBodyHit) != 0)
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
			h.inAir = true;
			if (onLedge)
			{
				if ((g_trace || g_log) && local) rt_log("goldeneye: bunnyhop: a hop off a ledge at %.2f %.2f %.2f", ldf(entity + kPos), posY, ldf(entity + kPos + 8));
				if (ld32(ctrl + kPlayerState) == kStateFalling) stf(ctrl + kFallen, 0.0f);
				h.fallen = 0;
			}
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
			const float wishSpeed{ kWishSpeed * stick * (crouched && g_now.crouch ? kDuck : 1.0f) };
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
	g_crouch = std::atoi(saved_setting("GE_BHOP_CROUCH", "LAUNCH_BHOP_CROUCH").c_str()) != 0;
	g_hover = std::atoi(saved_setting("GE_BHOP_HOVER", "LAUNCH_BHOP_HOVER").c_str()) != 0;
	g_ledge = std::atoi(saved_setting("GE_BHOP_LEDGE", "LAUNCH_BHOP_LEDGE").c_str()) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	if (const char* e = std::getenv("GE_BHOP_LOG")) g_log = std::atoi(e) != 0;
	for (BhopValue& v : g_values)
	{
		v.initial = v.value;
		const std::string saved{ saved_setting(v.env, v.launch) };
		if (!saved.empty()) v.value = std::clamp((float)std::atof(saved.c_str()), v.lo, v.hi);
	}
	orig_fall_start = ppc_hook("ge_fall_start", FallStart);
	orig_body_still = ppc_hook("ge_body_still", BodyStill);
	if (!orig_body_still) rt_die("goldeneye: ge_body_still is not hooked in this build");
	orig_contacts_solve = ppc_hook("ge_contacts_solve", ContactsSolve);
	if (!orig_contacts_solve) rt_die("goldeneye: ge_contacts_solve is not hooked in this build");
	orig_move_grab = ppc_hook("ge_move_grab", MoveGrab);
	orig_ladder_foot = ppc_hook("ge_ladder_foot", LadderFoot);
	search_near = ppc_lookup(kSearchNear);
	search_all = orig_ladder_foot && search_near ? ppc_lookup(kSearchAll) : nullptr;
	if (!search_all) rt_log("goldeneye: bunnyhop: ge_ladder_foot or the world's search isn't in this build, ladders are the game's");
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
