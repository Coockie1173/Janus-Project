//teleports: a volume that puts whoever touches it somewhere else (eurochef's teleport_<name> and
//destination_<name> nodes). only the port reads them: a trigger the game never runs, the generic
//one (type 3) asleep (game flag 1), with a collision shape, kMark in data 1, data 5 to 7 where it
//goes and data 8 the yaw, as floats. it all goes in the trigger, so nothing here has a state:
//online every console steps every player through the same test
//
//the range (+0x7C) only says how near counts for the game's tests, the shape's own test
//(fn_80024450: trigger, point) is all that's asked here
//
//the position is the entity's (+0xF8), see noclip.cpp: the body goes on from there. what it would
//still take along is zeroed: the frame's step, the fall speed, the walk's velocity and the fall
#include "ge.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ge
{
namespace
{
const uint32_t kWorld = 0x805F4CCC, kTriggerCount = 0xE0, kTriggers = 0xE4, kTriggerType = 0x58;
const uint32_t kGameFlags = 0x68, kDataFlags = 0x64, kHasShape = 0x8000000, kAsleep = 1;
const uint32_t kTriggerData = 0x98, kGenericTrigger = 3, kMark = 0x54454C45; //TELE
const int kMarkSlot = 1, kToSlot = 5, kYawSlot = 8;
const uint32_t kCurrentLevel = 0x805D1804;
const uint32_t kShapeTestFn = 0x80024450;
const uint32_t kEntity = 0x4, kPos = 0xF8, kYaw = 0xDC, kBody = 0x158, kBodyStep = 0xBC, kBodyFallSpeed = 0x15C;
const uint32_t kPlayerState = 0x704;
const uint32_t kVelocity = 0x968, kStep = 0x590, kFallVelocity = 0xE78; //kFallen is its fifth

PPCFunc g_shape_test = nullptr;
std::vector<uint32_t> g_teleports;
uint32_t g_looked = 0, g_count = 0;

//the states the player moves themselves in, as in noclip.cpp
bool Free(uint32_t state) { return state == 0 || state == 2 || state == 3 || state == 4; }

//when the level or the number of triggers changes
void Find()
{
	const uint32_t level{ ld32(kCurrentLevel) }, count{ ld32(kWorld + kTriggerCount) }, list{ ld32(kWorld + kTriggers) };
	if (level == g_looked && count == g_count) return;
	g_looked = level;
	g_count = count;
	g_teleports.clear();
	for (uint32_t i = 0; i < count && count < 0x4000 && guest_ptr(list); ++i)
	{
		const uint32_t t{ ld32(list + i * 4) };
		if (!guest_ptr(t) || ld32(t + kTriggerType) != kGenericTrigger || ld32(t + kTriggerData + kMarkSlot * 4) != kMark) continue;
		if (ld32(t + kDataFlags) & kHasShape) g_teleports.push_back(t);
	}
	if (!g_teleports.empty()) rt_log("goldeneye: teleport: level %08X has %d", level, (int)g_teleports.size());
}

bool Inside(PPCContext& c, uint32_t trigger, uint32_t entity)
{
	PPCContext call{ c };
	call.r[3] = trigger;
	call.r[4] = entity + kPos;
	g_shape_test(call);
	return (call.r[3] & 0xFF) != 0;
}

void Go(uint32_t ctrl, uint32_t entity, uint32_t body, uint32_t trigger)
{
	const float y{ ldf(trigger + kTriggerData + (kToSlot + 1) * 4) };
	for (int i = 0; i < 3; ++i) stf(entity + kPos + 4 * i, ldf(trigger + kTriggerData + (kToSlot + i) * 4));
	stf(entity + kYaw, ldf(trigger + kTriggerData + kYawSlot * 4));
	for (int i = 0; i < 3; ++i) stf(body + kBodyStep + 4 * i, 0.0f);
	stf(body + kBodyFallSpeed, 0.0f);
	for (int i = 0; i < 4; ++i)
	{
		stf(ctrl + kVelocity + 4 * i, 0.0f);
		stf(ctrl + kStep + 4 * i, 0.0f);
	}
	for (int i = 0; i < 5; ++i) stf(ctrl + kFallVelocity + 4 * i, 0.0f);
	bunnyhop_stop(ctrl, y);
	rt_log("goldeneye: teleport: trigger %08X to %.2f %.2f %.2f, yaw %.2f", trigger, ldf(entity + kPos), y, ldf(entity + kPos + 8), ldf(entity + kYaw));
}
} //namespace

void teleport_apply(PPCContext& c, uint32_t ctrl)
{
	Find();
	if (g_teleports.empty() || !guest_ptr(ctrl)) return;
	const uint32_t entity{ ld32(ctrl + kEntity) };
	const uint32_t body{ guest_ptr(entity) ? ld32(entity + kBody) : 0 };
	if (!guest_ptr(body) || !Free(ld32(ctrl + kPlayerState))) return;
	for (uint32_t t : g_teleports)
	{
		const bool inside{ Inside(c, t, entity) };
		trigger_asked(t, inside);
		if (!inside) continue;
		Go(ctrl, entity, body, t);
		return;
	}
}

void install_teleport()
{
	g_shape_test = ppc_lookup(kShapeTestFn);
	if (!g_shape_test) rt_die("goldeneye: the trigger shape's test is not in this build");
}

} //namespace ge
