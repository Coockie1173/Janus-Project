//weapons: a key for each of the three weapon slots
//the inventory is at the controller's +0xC40: the count at +8, the items at +0xC, 0x10 each
//with the id at +0, how many at +4 and the slot's number in the byte at +0xE (0xFF none)
//+0xC96 is the slot's item in the hand, +0xC97 the one being changed to
//ge_weapon_input (8015A430) reads the buttons once a frame: action bit 0x200 (d-pad right)
//calls ge_weapon_cycle (8014F490) with 1, bit 0x4000 (left) with -1. the cycle asks
//ge_weapon_next_slot (8014B0A0) for the item of the slot after this one and changes to it
//a slot's key presses "next" for that one call and the search answers with the key's slot
//so everything the game does around a change (the sprint ends, the phone goes away) still happens
#include "ge.h"
#include <atomic>
#include <chrono>

namespace ge
{
namespace
{

const uint32_t kExtraSlot = 4; //the key file's Extra 3, 4 and 5
const int kSlotCount = 3;
const uint32_t kActPressed = 0x534, kActHeld = 0x538, kActNext = 0x200, kActPrevious = 0x4000;
const uint32_t kItems = 0xC4C, kItemCount = 0xC48, kItemSize = 0x10, kItemSlot = 0xE, kItemNone = 0x2000000;
const uint32_t kCtrlHolster = 0x5ED;
const uint32_t kWeaponUsable = 0x8014AEC0; //(controller, item, weapons only, holstered, with ammo) -> 1 if it can be changed to
const uint32_t kCycleReturns[2] = {0x8014F5A0, 0x8014F600}; //the cycle's two calls of the search
const auto kPressLasts = std::chrono::milliseconds(250); //a press nothing took (a menu, a cutscene) is dropped

using Clock = std::chrono::steady_clock;
std::atomic<int> g_pressed{-1}; //the slot whose key went down, as the pad was last read
std::atomic<Clock::rep> g_pressedAt{};
int g_slot = -1; //the slot the search answers with, during ge_weapon_input
PPCFunc orig_weapon_input = nullptr;
PPCFunc orig_next_slot = nullptr;
PPCFunc weapon_usable = nullptr;

void WeaponInput(PPCContext& c)
{
	uint32_t ctrl = c.r[3];
	int slot = guest_ptr(ctrl) && ctrl == local_controller() ? g_pressed.exchange(-1) : -1;
	if (slot >= 0 && Clock::now().time_since_epoch().count() - g_pressedAt > Clock::duration(kPressLasts).count()) slot = -1;
	if (slot < 0)
	{
		orig_weapon_input(c);
		return;
	}
	uint32_t pressed = ld32(ctrl + kActPressed), held = ld32(ctrl + kActHeld), both = kActNext | kActPrevious;
	st32(ctrl + kActPressed, (pressed & ~both) | kActNext);
	st32(ctrl + kActHeld, (held & ~both) | kActNext);
	g_slot = slot;
	orig_weapon_input(c);
	g_slot = -1;
	st32(ctrl + kActPressed, (ld32(ctrl + kActPressed) & ~both) | (pressed & both));
	st32(ctrl + kActHeld, (ld32(ctrl + kActHeld) & ~both) | (held & both));
}

//the item in the wanted slot, -1 when there's none the game would change to
void NextSlot(PPCContext& c)
{
	bool cycle = c.lr == kCycleReturns[0] || c.lr == kCycleReturns[1];
	if (g_slot < 0 || !cycle || c.r[3] != local_controller())
	{
		orig_next_slot(c);
		return;
	}
	uint32_t ctrl = c.r[3], items = ld32(ctrl + kItems), count = ld32(ctrl + kItemCount);
	int found = -1;
	for (uint32_t i = 0; guest_ptr(items) && i < count && i < 64 && found < 0; ++i)
	{
		uint32_t item = items + i * kItemSize;
		if (ld32(item) == kItemNone || ld8(item + kItemSlot) != (uint32_t)g_slot) continue;
		PPCContext call = c;
		call.r[4] = item;
		call.r[5] = c.r[7];
		call.r[6] = (uint32_t)(int8_t)ld8(ctrl + kCtrlHolster);
		call.r[7] = c.r[6];
		weapon_usable(call);
		if ((int8_t)call.r[3] != 0) found = (int)i;
	}
	static const bool trace = std::getenv("GE_TRACE") != nullptr;
	if (trace) rt_log("goldeneye: weapon slot %d: item %d of %u", g_slot + 1, found, count);
	c.r[3] = (uint32_t)found;
}

} //namespace

//in the classic's filter: a slot's key going down is kept for the game's next frame
void weapon_buttons(ClassicState& s)
{
	static uint32_t was = 0;
	uint32_t keys = s.extra / kExtraSlot & ((1u << kSlotCount) - 1), down = keys & ~was;
	was = keys;
	if (!down || !in_play()) return;
	int slot = 0;
	while (!(down >> slot & 1)) ++slot;
	g_pressedAt = Clock::now().time_since_epoch().count();
	g_pressed = slot;
}

void install_weapons()
{
	orig_weapon_input = ppc_hook("ge_weapon_input", WeaponInput);
	orig_next_slot = ppc_hook("ge_weapon_next_slot", NextSlot);
	weapon_usable = ppc_lookup(kWeaponUsable);
	if (!orig_weapon_input || !orig_next_slot || !weapon_usable) rt_die("goldeneye: the weapon slots are not hooked in this build");
}

} //namespace ge
