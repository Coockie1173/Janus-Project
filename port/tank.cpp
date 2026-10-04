//the tank's driving at frame rates above the game's 30
//
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
#include "ge.h"
#include <cmath>

namespace ge
{
namespace
{

const uint32_t kVehicleWheels = 0x1FC, kVehicleWheelCount = 0x200, kWheelSize = 0x90, kWheelDrag = 0x84;
const uint32_t kMaxWheels = 32; //the tank has 18
const uint32_t kBodyFlags = 0x58, kBodyDampsByTime = 0x100, kBodyKeep = 0x130, kBodySpinKeep = 0x1E0;
const uint32_t kTankThrottleStep[2] = {0x806E96E8, 0x806E96F8};
const uint32_t kCtrlTimeStep = 0x578;
PPCFunc orig_vehicle_step = nullptr;
PPCFunc orig_tank_drive = nullptr;

//how many of the game's own frames a time step is. only a shorter one gets scaled
bool ShortStep(float seconds, float& frames)
{
	frames = seconds * kGameFps;
	return frames > 0.01f && frames < 0.999f;
}

void VehicleStep(PPCContext& c)
{
	uint32_t vehicle = c.r[3], wheels = guest_ptr(vehicle) ? ld32(vehicle + kVehicleWheels) : 0;
	uint32_t count = wheels ? ld32(vehicle + kVehicleWheelCount) : 0;
	float frames;
	if (!guest_ptr(wheels) || count > kMaxWheels || !ShortStep((float)c.f[1], frames))
	{
		orig_vehicle_step(c);
		return;
	}
	uint32_t drags[kMaxWheels];
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t at = wheels + i * kWheelSize + kWheelDrag;
		drags[i] = ld32(at);
		float keep = 1.0f - ldf(at);
		if (keep > 0.0f && keep < 1.0f) stf(at, 1.0f - std::pow(keep, frames));
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
	for (uint32_t i = 0; i < count; ++i) st32(wheels + i * kWheelSize + kWheelDrag, drags[i]);
	for (int i = 0; body && i < 4; ++i) st32(keeps[i], kept[i]);
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

void install_tank()
{
	orig_vehicle_step = ppc_hook("ge_vehicle_step", VehicleStep);
	if (!orig_vehicle_step) rt_die("goldeneye: the vehicles' step is not hooked in this build");
	orig_tank_drive = ppc_hook("ge_tank_drive", TankDrive);
	if (!orig_tank_drive) rt_die("goldeneye: the tank's drive is not hooked in this build");
}

} //namespace ge
