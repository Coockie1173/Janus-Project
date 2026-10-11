//the tank (the mission of that name): the mouse on its aim, its buttons and its driving at
//frame rates above the game's 30
//
//the player there is another class of controller, XItemHandler_PlayerTank with the vtable at
//80574AB8. it's in state 2 all along and never gets to ge_look_apply
//its own update (ge_tank_update, 80142E60) turns the look stick into the same two rates,
//+0x97C the yaw's and +0x978 the pitch's in radians a second (negative looks up), and
//ge_tank_look (80140600) turns the rates times the time step into the aim: the pitch at
//+0x99C, which it keeps within the game's limits, and the yaw at +0x9A0. the view follows
//the aim, and the left stick drives towards where it points in the view
//for that one call the mouse's motion is added to the rates and taken out again after,
//like ge_look_apply on foot
//
//the buttons: the tank has three (fn_80144BF0 asks for the actions at +0x534
//pressed and +0x538 held). the classic's r is action 0x10, the machine gun while it is held;
//ZR is action 4, a press fires the cannon, or a missile when the lock is held and has a
//target (+0xA00); ZL is action 0x40, the lock while it is held. no other button gives the
//tank an action (each pressed in turn with GE_TRACE)
//which button of the game's actions on foot presses each is a setting (Control Settings): as
//the port starts the fire button (ZR, the left mouse button) is the machine gun, the aim
//button (ZL, the right one) the cannon and the grenade button (r) the lock. a button that is
//given to the tank doesn't do what it did by itself. only in the tank and in play, a menu
//keeps the triggers as select and back
//GE_TANK_BUTTONS=0,1,2 the buttons of the machine gun, the cannon and the lock, numbers in kTankSources
//GE_TRACE=1 also logs the tank's actions (+0x538) whenever they change, with the buttons that gave them
//
//the driving:
//three things in a vehicle's physics happen once a call with no time step in them, so at 144
//they happen five times as often and the tank hardly moves (4 s of full stick: 39 units at
//30, 13 at 60, 5 at 144). each one is scaled to the time step for the call and put back after
//
//the wheels: ge_vehicle_step (8036B310, f1 the time step) slows every wheel's spin (+0x54 of
//a wheel, 0x90 each at the vehicle's +0x1FC, the count at +0x200) by the wheel's drag (+0x84):
//spin *= 1 - drag. the drag becomes what loses the same over a second at any rate
//
//the body: the step it calls (fn_8035D070) does the same to the velocity (times +0x130) and
//the spin (+0x1B0, times the three at +0x1E0). the engine can scale those by the time step,
//but only for a body with flag 0x100 at +0x58, which the tank hasn't. without it the hull
//turns a third as fast at 144 as at 30
//
//the throttle: ge_tank_drive (801478C0, the controller in r3) moves the throttle at +0xA64
//towards the stick's (+0xA5C) by a fixed step a call, 806E96E8 with the byte at 806E96D8 set
//and 806E96F8 without. the wheels' drive comes from it
//
//the cars (every vehicle but the tank, vtable 805925F8) sink into the road above 30:
//a wheel that's in the ground is drawn up by its travel (+0x44, at most +0x7C). a contact
//(ge_car_contact, 8036C060) adds how deep the wheel is in * (1 - stiffness) / (60 * time step),
//and ge_vehicle_step takes (+0x80 + travel squared) * 60 * time step off again. at 30 that's
//more than the travel can be, so every frame starts from none and the travel is just under
//half the depth. at 144 five times as much goes on and a fifth comes off: the travel stays at
//its limit, the stiffness (0.1 below 0.05 of travel, 0.05 * its root above) at a quarter, and
//the car rides 0.85 lower, falling all the while
//so for a shorter step all the travel comes off in the step, and a contact adds what it would at 30
#include "ge.h"
#include "debug.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ge
{
namespace
{

const uint32_t kTankVtable = 0x80574AB8;
const uint32_t kPlayerState = 0x704, kStateMoving = 2;
const uint32_t kLookPitchRate = 0x978, kLookYawRate = 0x97C;
const uint32_t kTankHeading = 0x52C; //where the view looks, radians, as the frame's input has it
const uint32_t kTankAimPitch = 0x99C, kTankAimYaw = 0x9A0;
const uint32_t kClZR = 0x0004, kClZL = 0x0080, kClR = 0x0200, kClL = 0x2000; //KPAD's classic bits
const uint32_t kClA = 0x0010, kClX = 0x0008, kClY = 0x0020;
const uint32_t kActHeld = 0x538;
//what the tank does, the classic button the game has it on, and the button that presses it here
struct TankAction { const char* label; const char* hint; uint32_t button; int source; };
const TankAction kTankActions[kTankActionCount] = {
	{"Tank: Machine Gun", "The button that fires the tank's machine gun while it is held.", kClR, 0},
	{"Tank: Fire Cannon", "The button that fires the tank's cannon, and a missile at what is locked on.", kClZR, 1},
	{"Tank: Lock On", "The button that locks the tank's missiles on a target while it is held.", kClZL, 2}};
struct TankSource { const char* label; uint32_t button; };
const TankSource kTankSources[kTankSourceCount] = {{"Fire", kClZR}, {"Aim", kClZL}, {"Throw Grenade", kClR},
	{"Melee Attack", kClL}, {"Reload", kClY}, {"Crouch", kClA}, {"Cycle Grenades", kClX}};
std::atomic<int> g_sources[kTankActionCount];
std::atomic<uint32_t> g_buttons_in{0}, g_buttons_out{0}; //the last ones, for the trace
PPCFunc orig_tank_look = nullptr;

const uint32_t kVehicleWheels = 0x1FC, kVehicleWheelCount = 0x200, kWheelSize = 0x90, kWheelDrag = 0x84;
const uint32_t kMaxWheels = 32; //the tank has 18
const uint32_t kBodyFlags = 0x58, kBodyDampsByTime = 0x100, kBodyKeep = 0x130, kBodySpinKeep = 0x1E0;
const uint32_t kTankThrottleStep[2] = {0x806E96E8, 0x806E96F8};
const uint32_t kCtrlTimeStep = 0x578;
const uint32_t kCarVtable = 0x805925F8; //every vehicle that isn't the tank
const uint32_t kBodyPos = 0x68, kBodySpeed = 0x88, kBodyShapes = 0x118, kShapePlaces = 0x4C, kShapePlaceSize = 0x40;
const uint32_t kWheelTravel = 0x44, kWheelTravelMax = 0x7C, kWheelGive = 0x80, kWheelShape = 0x8C;
const uint32_t kContactShape = 0x24;
const float kGiveAll = 100.0f, kNoLimit = 1e9f;
PPCFunc orig_vehicle_step = nullptr;
PPCFunc orig_car_contact = nullptr;
PPCFunc orig_tank_drive = nullptr;

//the aim and where the tank is, twice a second
void TraceAim(uint32_t ctrl)
{
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (now - last < std::chrono::milliseconds(500)) return;
	last = now;
	uint32_t tank = ld32(ctrl + 4);
	rt_log("goldeneye: tank %08X, heading %.4f; aim %.4f across, %.4f up and down; hull %.4f at %.2f %.2f %.2f", ctrl, ldf(ctrl + kTankHeading),
		ldf(ctrl + kTankAimYaw), ldf(ctrl + kTankAimPitch), std::atan2(ldf(tank + 0xB8), ldf(tank + 0xC0)),
		ldf(tank + 0xF8), ldf(tank + 0xFC), ldf(tank + 0x100));
}

void TraceActions(uint32_t ctrl)
{
	static uint32_t last = 0;
	uint32_t held = ld32(ctrl + kActHeld);
	if (held == last) return;
	last = held;
	rt_log("goldeneye: tank: actions held %05X, buttons %04X of %04X pressed", held, g_buttons_out.load(), g_buttons_in.load());
}

void TankLook(PPCContext& c)
{
	uint32_t ctrl = c.r[3];
	bool ours = guest_ptr(ctrl) && ctrl == local_controller() && ld32(ctrl + kPlayerState) == kStateMoving;
	if (!ours || !(mouse_sens() > 0) || ld8(kPaused) || overlay_open() || mouse_on_stick())
	{
		orig_tank_look(c);
		return;
	}
	static const bool trace = std::getenv("GE_TRACE") != nullptr;
	if (trace) TraceAim(ctrl);
	if (trace) TraceActions(ctrl);
	mouse_applied(ld32(kFrameCount));
	float dx = 0, dy = 0, dt = ldf(ctrl + kCtrlTimeStep);
	video_take_mouse_motion(dx, dy);
	debug_mouse(dx, dy);
	if (mouse_invert()) dy = -dy;
	if ((dx == 0 && dy == 0) || !(dt > 0.0005f && dt < 0.2f))
	{
		orig_tank_look(c);
		return;
	}
	float k = kRadPerPixel * mouse_sens() / dt, yaw = dx * k, pitch = dy * k;
	stf(ctrl + kLookYawRate, ldf(ctrl + kLookYawRate) + yaw);
	stf(ctrl + kLookPitchRate, ldf(ctrl + kLookPitchRate) + pitch);
	orig_tank_look(c);
	stf(ctrl + kLookYawRate, ldf(ctrl + kLookYawRate) - yaw);
	stf(ctrl + kLookPitchRate, ldf(ctrl + kLookPitchRate) - pitch);
}

//how many of the game's own frames a time step is. only a shorter one gets scaled
bool ShortStep(float seconds, float& frames)
{
	frames = seconds * kGameFps;
	return frames > 0.01f && frames < 0.999f;
}

//GE_VEHTRACE=1: a car's height and its wheels' travel, every eighth step
void TraceCar(uint32_t car, float step)
{
	static uint32_t calls = 0;
	if (++calls & 7) return;
	uint32_t wheels = ld32(car + kVehicleWheels), count = ld32(car + kVehicleWheelCount);
	char travel[128];
	int at = 0;
	travel[0] = 0;
	for (uint32_t i = 0; i < count && i < 8; ++i)
		at += snprintf(travel + at, sizeof travel - at, " %.3f", ldf(wheels + i * kWheelSize + kWheelTravel));
	rt_log("goldeneye: car %08X step %.5f at %.2f %.4f %.2f falling %.3f, wheels%s", car, step, ldf(car + kBodyPos), ldf(car + kBodyPos + 4),
		ldf(car + kBodyPos + 8), -ldf(car + kBodySpeed + 4), travel);
}

void VehicleStep(PPCContext& c)
{
	uint32_t vehicle = c.r[3], wheels = guest_ptr(vehicle) ? ld32(vehicle + kVehicleWheels) : 0;
	uint32_t count = wheels ? ld32(vehicle + kVehicleWheelCount) : 0;
	float frames;
	bool car = guest_ptr(vehicle) && ld32(vehicle) == kCarVtable;
	static const bool trace = std::getenv("GE_VEHTRACE") != nullptr;
	if (trace && car && guest_ptr(wheels)) TraceCar(vehicle, (float)c.f[1]);
	if (!guest_ptr(wheels) || count > kMaxWheels || !ShortStep((float)c.f[1], frames))
	{
		orig_vehicle_step(c);
		return;
	}
	uint32_t drags[kMaxWheels], gives[kMaxWheels];
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t wheel = wheels + i * kWheelSize, at = wheel + kWheelDrag;
		drags[i] = ld32(at);
		float keep = 1.0f - ldf(at);
		if (keep > 0.0f && keep < 1.0f) stf(at, 1.0f - std::pow(keep, frames));
		gives[i] = ld32(wheel + kWheelGive);
		if (car) stf(wheel + kWheelGive, kGiveAll);
	}
	const uint32_t keeps[4] = {vehicle + kBodyKeep, vehicle + kBodySpinKeep, vehicle + kBodySpinKeep + 4, vehicle + kBodySpinKeep + 8};
	uint32_t kept[4];
	bool body = (ld32(vehicle + kBodyFlags) & kBodyDampsByTime) == 0;
	for (int i = 0; body && i < 4; ++i)
	{
		kept[i] = ld32(keeps[i]);
		float keep = ldf(keeps[i]);
		if (keep > 0.0f && keep < 1.0f) stf(keeps[i], std::pow(keep, frames));
	}
	orig_vehicle_step(c);
	for (uint32_t i = 0; i < count; ++i)
	{
		st32(wheels + i * kWheelSize + kWheelDrag, drags[i]);
		st32(wheels + i * kWheelSize + kWheelGive, gives[i]);
	}
	for (int i = 0; body && i < 4; ++i) st32(keeps[i], kept[i]);
}

//a contact of a car with the level. the wheel it's for gets its travel from how deep it's in,
//divided by the time step: the limit is lifted for the call and the travel made what a 30th's gives
void CarContact(PPCContext& c)
{
	uint32_t car = c.r[3], contact = c.r[4], wheels = guest_ptr(car) ? ld32(car + kVehicleWheels) : 0;
	uint32_t count = wheels ? ld32(car + kVehicleWheelCount) : 0;
	float frames;
	if (!guest_ptr(wheels) || !guest_ptr(contact) || count > kMaxWheels || !ShortStep((float)c.f[1], frames))
	{
		orig_car_contact(c);
		return;
	}
	float travels[kMaxWheels];
	uint32_t limits[kMaxWheels];
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t wheel = wheels + i * kWheelSize;
		travels[i] = ldf(wheel + kWheelTravel);
		limits[i] = ld32(wheel + kWheelTravelMax);
		if (travels[i] < ldf(wheel + kWheelTravelMax)) stf(wheel + kWheelTravelMax, kNoLimit);
	}
	uint32_t shape = ld32(contact + kContactShape);
	orig_car_contact(c);
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t wheel = wheels + i * kWheelSize;
		st32(wheel + kWheelTravelMax, limits[i]);
		float travel = ldf(wheel + kWheelTravel);
		if (travel == travels[i]) continue;
		travel = std::min(travels[i] + (travel - travels[i]) * frames, ldf(wheel + kWheelTravelMax));
		stf(wheel + kWheelTravel, travel);
		//the wheel's shape was put where the travel had it
		uint32_t shapes = ld32(car + kBodyShapes), places = guest_ptr(shapes) ? ld32(shapes + kShapePlaces) : 0;
		if (ld32(wheel + kWheelShape) == shape && guest_ptr(places)) stf(places + shape * kShapePlaceSize + 4, ldf(wheel + 4) + travel);
	}
}

void TankDrive(PPCContext& c)
{
	uint32_t ctrl = c.r[3];
	float frames;
	if (!guest_ptr(ctrl) || !ShortStep(ldf(ctrl + kCtrlTimeStep), frames))
	{
		orig_tank_drive(c);
		return;
	}
	uint32_t steps[2];
	for (int i = 0; i < 2; ++i)
	{
		steps[i] = ld32(kTankThrottleStep[i]);
		stf(kTankThrottleStep[i], ldf(kTankThrottleStep[i]) * frames);
	}
	orig_tank_drive(c);
	for (int i = 0; i < 2; ++i) st32(kTankThrottleStep[i], steps[i]);
}

} //namespace

bool is_tank(uint32_t ctrl) { return ld32(ctrl) == kTankVtable; }

const char* tank_action_label(int action) { return kTankActions[action].label; }
const char* tank_action_hint(int action) { return kTankActions[action].hint; }
const char* tank_source_label(int source) { return kTankSources[source].label; }
int tank_source(int action) { return g_sources[action]; }
void set_tank_source(int action, int source) { g_sources[action] = std::clamp(source, 0, kTankSourceCount - 1); }

void tank_buttons(ClassicState& s)
{
	uint32_t ctrl = local_controller();
	if (!ctrl || !is_tank(ctrl) || !in_play()) return;
	uint32_t b = s.buttons;
	for (int i = 0; i < kTankActionCount; ++i) b &= ~(kTankActions[i].button | kTankSources[g_sources[i]].button);
	for (int i = 0; i < kTankActionCount; ++i)
	{
		if (s.buttons & kTankSources[g_sources[i]].button) b |= kTankActions[i].button;
	}
	g_buttons_in = s.buttons;
	g_buttons_out = b;
	s.buttons = b;
}

void install_tank()
{
	for (int i = 0; i < kTankActionCount; ++i) g_sources[i] = kTankActions[i].source;
	std::string saved = saved_setting("GE_TANK_BUTTONS", "LAUNCH_TANKBUTTONS");
	int sources[kTankActionCount];
	if (std::sscanf(saved.c_str(), "%d,%d,%d", &sources[0], &sources[1], &sources[2]) == kTankActionCount)
	{
		for (int i = 0; i < kTankActionCount; ++i) set_tank_source(i, sources[i]);
	}
	orig_tank_look = ppc_hook("ge_tank_look", TankLook);
	if (!orig_tank_look) rt_die("goldeneye: the tank's look is not hooked in this build");
	orig_vehicle_step = ppc_hook("ge_vehicle_step", VehicleStep);
	if (!orig_vehicle_step) rt_die("goldeneye: the vehicles' step is not hooked in this build");
	orig_car_contact = ppc_hook("ge_car_contact", CarContact);
	if (!orig_car_contact) rt_die("goldeneye: the cars' contact is not hooked in this build");
	orig_tank_drive = ppc_hook("ge_tank_drive", TankDrive);
	if (!orig_tank_drive) rt_die("goldeneye: the tank's drive is not hooked in this build");
}

} //namespace ge
