//a cheat for the campaign: Invisible Enemies
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
//GE_INVISIBLE_ENEMIES=1 start with it on (LAUNCH_INVISIBLEENEMIES)
//GE_TRACESTEALTH=1 logs the enemies in the draw list as they change
#include "ge.h"
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

std::atomic<bool> g_enemies{false};
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

bool Campaign() { return !multiplayer_game(); }

} //namespace

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

void install_stealth()
{
	rt_state_register("stealth", [](StateIO& io) { io.vec(g_hidden); });
	g_trace = std::getenv("GE_TRACESTEALTH") != nullptr;
	g_enemies = std::atoi(saved_setting("GE_INVISIBLE_ENEMIES", "LAUNCH_INVISIBLEENEMIES").c_str()) != 0;
}

} //namespace ge
