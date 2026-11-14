//two cheats for the campaign: Invisible Enemies and Invisible Player
//
//who's who: a thing in the level (+0x154) has a handler, the handler's class is its vtable's
//third function (+8), a lis/addi/blr that returns the class's descriptor: +0 the parent, +4 the
//name, +8 the size, +0xC the constructor. the people:
//  XItemHandler_NPC 8053D160
//    ArmedNPC 8053C9F0: EnemyNew 8053CCE0 (the guards), Friend 8053E858, ScriptedNPC 8053EF28
//    Dog 8053E608, Civilian 8053EDF8
//an enemy is an EnemyNew, a Dog or a plain ArmedNPC
//
//Invisible Enemies: ge_draw_things (zones.cpp hooks it) draws a thing only when its flags
//(+0xC) & 0x11 are 1. for the call every enemy's bit 0 is cleared and put back after, so they
//aren't drawn but stay in the level as ever: their shots, flashes and tracers are still seen
//
//Invisible Player: the game has a way of its own to hide the player: a character's handler has
//a byte at +0x4E5, 1 when it can be seen (fn_800B4C30 sets it as it's made), and a hiding trigger
//(fn_800435D0) clears it while the player is inside. a guard with a target (+0x6D4, its +0x578
//bit 0x4000) asks TargetVisible (8006B6C0), which is that byte of the target's handler, and its
//sight of the target (fn_80053EF0) goes no further without it. the byte is held at 0 while the
//cheat is on, and back at 1 when it's turned off
//that byte isn't enough: the sight test (ge_guard_sense_ray, the NPC vtable +0x12C) only asks
//TargetVisible when the guard's brain (+0x57C) has +0x4CC at 0, and TargetVisible says yes when
//the guard's +0x578 bit 0x4000 is clear, so some guards saw the player anyway. the test is left
//out for a guard whose target (+0x6D4) is the local player, as the game does when it can't be seen
//a guard that's already alert (the brain's +0x4CC 1: Dam's checkpoints start some that way, aimed
//at the player) fires without seeing anyone: its behaviours ask ge_guard_fire_ready (80086430,
//the brain, whose +0x3D4 is the guard) first, which says no while its target is the local player
//what a guard takes in (a sight, a noise, another guard telling it where the player is) goes
//through ge_npc_perceive (8006A570) as a stimulus: +8 its kind (5210000D 60070003 a sighting,
//52100004 60060001, 52100001 and 52100005 60030011 a noise...), +0x18 where, +0x38 the id of who
//it's about. ids are the things' table at 805F4C54 (+8 how many, +0xC pairs of id and thing,
//sorted by id; fn_8027BD70 looks one up). one about the local player is left out too, so the
//shots and steps aren't heard
//GE_INVISIBLE_ENEMIES=1 GE_INVISIBLE_PLAYER=1 start with them on (LAUNCH_INVISIBLEENEMIES, LAUNCH_INVISIBLEPLAYER)
//GE_TRACESTEALTH=1 logs the first stimuli and what came of them
#include "ge.h"
#include "input/mouse.h"
#include <atomic>
#include <cstdlib>
#include <map>
#include <vector>

namespace ge
{
namespace
{
const uint32_t kThingHandler = 0x154, kThingFlags = 0xC, kThingShown = 1;
const uint32_t kClassNpc = 0x8053D160, kClassArmed = 0x8053C9F0, kClassEnemy = 0x8053CCE0, kClassDog = 0x8053E608;
const uint32_t kStimulusAbout = 0x38;
const uint32_t kIdTable = 0x805F4C54, kIdCount = 0x8, kIdPairs = 0xC;

std::atomic<bool> g_enemies{false}, g_player{false};
PPCFunc orig_perceive = nullptr, orig_sense = nullptr, orig_fire_ready = nullptr;
bool g_trace = false;
std::vector<uint32_t> g_hidden; //the things ge_draw_things has with bit 0 cleared

//a handler's class descriptor, 0 when its GetClass isn't the usual three instructions
uint32_t ClassOf(uint32_t handler)
{
	if (!guest_ptr(handler) || !guest_ptr(ld32(handler))) return 0;
	const uint32_t get = ld32(ld32(handler) + 8);
	if (!guest_ptr(get)) return 0;
	const uint32_t hi = ld32(get), lo = ld32(get + 4);
	if (hi >> 16 != 0x3C60 || lo >> 16 != 0x3863) return 0; //lis r3, addi r3, r3
	return ((hi & 0xFFFF) << 16) + (uint32_t)(int32_t)(int16_t)(lo & 0xFFFF);
}

bool IsA(uint32_t cls, uint32_t base)
{
	for (int depth = 0; guest_ptr(cls) && depth < 16; ++depth, cls = ld32(cls))
		if (cls == base) return true;
	return false;
}

bool Enemy(uint32_t handler)
{
	const uint32_t cls = ClassOf(handler);
	return cls == kClassArmed || IsA(cls, kClassEnemy) || IsA(cls, kClassDog);
}

//the thing an id stands for, as fn_8027BD70 finds it
uint32_t ThingOf(uint32_t id)
{
	const uint32_t count = ld32(kIdTable + kIdCount), pairs = ld32(kIdTable + kIdPairs);
	if (!guest_ptr(pairs)) return 0;
	int lo = 0, hi = (int)count - 1;
	while (lo <= hi)
	{
		const int mid = (lo + hi) / 2;
		const uint32_t at = ld32(pairs + mid * 8);
		if (at == id) return ld32(pairs + mid * 8 + 4);
		if (id > at) lo = mid + 1;
		else hi = mid - 1;
	}
	return 0;
}

bool Campaign() { return !multiplayer_game(); }

//(npc, stimulus, r5 seen)
void Perceive(PPCContext& c)
{
	const uint32_t npc = c.r[3], stimulus = c.r[4];
	const uint32_t player = guest_ptr(g_local_ctrl) ? ld32(g_local_ctrl + 4) : 0;
	const uint32_t about = guest_ptr(stimulus) ? ThingOf(ld32(stimulus + kStimulusAbout)) : 0;
	const bool mine = player && about == player;
	const bool dropped = g_player && mine && Campaign();
	if (g_trace)
	{
		static int said = 0;
		if (said < 200 && ++said)
			rt_log("stealth: npc %08X (class %08X) takes %08X %08X about %X%s%s", npc, ClassOf(npc), ld32(stimulus + 8), ld32(stimulus + 0xC),
				ld32(stimulus + kStimulusAbout), mine ? " (the player)" : "", dropped ? ", left out" : "");
	}
	if (dropped) return;
	orig_perceive(c);
}
//the cheat is on and a guard's handler has the local player as its target
bool AfterPlayer(uint32_t guard)
{
	const uint32_t kTarget = 0x6D4;
	if (!g_player || !Campaign() || !guest_ptr(g_local_ctrl) || !guest_ptr(guard)) return false;
	const uint32_t player = ld32(g_local_ctrl + 4);
	return player && ThingOf(ld32(guard + kTarget)) == player;
}

//(brain) -> r3 1 when the guard may fire
void FireReady(PPCContext& c)
{
	const uint32_t kGuard = 0x3D4;
	const uint32_t brain = c.r[3];
	if (guest_ptr(brain) && AfterPlayer(ld32(brain + kGuard)))
	{
		if (g_trace)
		{
			static int said = 0;
			if (said < 20 && ++said) rt_log("stealth: guard %08X held its fire", ld32(brain + kGuard));
		}
		c.r[3] = 0;
		return;
	}
	orig_fire_ready(c);
}

//(guard, sense, result) the guard's test of one sense towards its target
void Sense(PPCContext& c)
{
	const uint32_t guard = c.r[3];
	if (!AfterPlayer(guard)) { orig_sense(c); return; }
	if (g_trace)
	{
		static int said = 0;
		const uint32_t brain = ld32(guard + 0x57C);
		if (said < 100 && ++said)
			rt_log("stealth: guard %08X sense %u at the player left out (flags %08X, brain +0x4CC %X)", guard, c.r[4], ld32(guard + 0x578),
				guest_ptr(brain) ? ld32(brain + 0x4CC) : 0);
	}
}
} //namespace

//after the local controller's update: the player's handler can't be seen
void stealth_apply(uint32_t controller)
{
	const uint32_t kVisible = 0x4E5;
	static bool held = false;
	const uint32_t thing = ld32(controller + 4), handler = guest_ptr(thing) ? ld32(thing + kThingHandler) : 0;
	if (!guest_ptr(handler)) { held = false; return; }
	static uint32_t said = 0;
	if (g_trace && said != thing && (said = thing)) rt_log("stealth: the player's thing %08X at %.2f %.2f %.2f", thing, ldf(thing + 0xF8), ldf(thing + 0xFC), ldf(thing + 0x100));
	const bool hide = g_player && Campaign();
	if (hide) st8(handler + kVisible, 0);
	else if (held) st8(handler + kVisible, 1);
	held = hide;
}

//ge_draw_things is about to draw the world's things (zones.cpp), and has
void stealth_things(uint32_t world, bool before)
{
	if (!before)
	{
		for (uint32_t thing : g_hidden) st16(thing + kThingFlags, ld16(thing + kThingFlags) | kThingShown);
		g_hidden.clear();
		return;
	}
	if (g_trace && guest_ptr(world) && guest_ptr(ld32(world + 0xA0)))
	{
		//the enemies in the list when they change: flags 0007 and a result (+0x169) of 0 is one drawn,
		//1C06 one of the level's that isn't in play yet
		static std::map<uint32_t, uint32_t> said;
		uint32_t n = 0;
		for (uint32_t thing = ld32(ld32(world + 0xA0)); guest_ptr(thing) && n < 4096; thing = ld32(thing + 4), ++n)
		{
			if (!Enemy(ld32(thing + kThingHandler))) continue;
			const uint32_t now = ld16(thing + kThingFlags) << 8 | (ld8(thing + 0x169) & ~2u);
			if (said[thing] != now && (said[thing] = now))
				rt_log("stealth: enemy %08X (class %08X) flags %04X, hidden by %u, at %.2f %.2f %.2f, level time %.2f", thing, ClassOf(ld32(thing + kThingHandler)), ld16(thing + kThingFlags), ld8(thing + 0x168),
					ldf(thing + 0xF8), ldf(thing + 0xFC), ldf(thing + 0x100), ldf(0x805C963C));
		}
	}
	if (!g_enemies || !Campaign() || !guest_ptr(world) || !guest_ptr(ld32(world + 0xA0))) return;
	uint32_t count = 0;
	for (uint32_t thing = ld32(ld32(world + 0xA0)); guest_ptr(thing) && count < 4096; thing = ld32(thing + 4), ++count)
	{
		if (!(ld16(thing + kThingFlags) & kThingShown) || !Enemy(ld32(thing + kThingHandler))) continue;
		st16(thing + kThingFlags, ld16(thing + kThingFlags) & ~kThingShown);
		g_hidden.push_back(thing);
	}
}

bool invisible_enemies() { return g_enemies; }
void set_invisible_enemies(bool on) { g_enemies = on; }
bool invisible_player() { return g_player; }
void set_invisible_player(bool on) { g_player = on; }

void install_stealth()
{
	g_trace = std::getenv("GE_TRACESTEALTH") != nullptr;
	g_enemies = std::atoi(saved_setting("GE_INVISIBLE_ENEMIES", "LAUNCH_INVISIBLEENEMIES").c_str()) != 0;
	g_player = std::atoi(saved_setting("GE_INVISIBLE_PLAYER", "LAUNCH_INVISIBLEPLAYER").c_str()) != 0;
	orig_perceive = ppc_hook("ge_npc_perceive", Perceive);
	orig_sense = ppc_hook("ge_guard_sense_ray", Sense);
	orig_fire_ready = ppc_hook("ge_guard_fire_ready", FireReady);
	if (!orig_perceive || !orig_sense || !orig_fire_ready) rt_log("goldeneye: Invisible Player's hooks aren't all in this build, it isn't whole");
}

} //namespace ge
