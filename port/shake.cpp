//the camera's sway goes by time, not by frames
//
//ge_camera_sway (80260B90, once a frame for each camera, before the look-at point is used)
//moves the view by the camera's sway strength (+0x4CC, set again every frame by what shakes
//it: Nightclub's dance floor, an explosion nearby): the look-at point (+0x720) goes up and
//down by strength * 0.0125 * sin(0.5 * frame) and the roll (+0xAA4) gets strength * 0.05 *
//sin(0.05 * frame) added. frame is the game's count of frames (805C9554), so a bob takes
//12.6 frames whatever a frame lasts: 2.4 a second on the console, 11.5 at 144, where the
//same sway is a vibration
//
//above 30 the count it sees is the game's time in console frames instead. that has a part
//after the point, so the sway is worked out for the frame before and the one after and the
//view put between the two
//
//Screen Shake (a slider, off to five times the game's): the sway's strength is scaled before the game uses it, and
//so is what ge_camera_shake (8025EBA0, the spreadsheet's shakes: the strongest of ten slots
//moves the look-at point by a new random step every frame) adds to the look-at point
//GE_SHAKE=0.5 half of it, 0 none, up to 5
//GE_SWAY_FORCE=S every camera sways with that strength all the time, to see what a setting does
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace ge
{
namespace
{

const uint32_t kSwayFrame = 0x805C9554; //the frame count the sway's sines take
const uint32_t kLookAt = 0x720, kRoll = 0xAA4, kSwayStrength = 0x4CC;
PPCFunc orig_sway = nullptr, orig_shake = nullptr;
std::atomic<float> g_scale{1.0f}; //Screen Shake: how much of the game's shaking is left
float g_force = 0.0f; //GE_SWAY_FORCE
bool g_off = false, g_trace = false; //GE_SWAY_OFF: the game's own. GE_SWAYTRACE: a line a call
double g_time = 0.0; //console frames, as the sway counts them
uint32_t g_time_frame = 0; //the game's frame g_time is for

struct View
{
	float look[4], roll, strength;
};

View Read(uint32_t camera)
{
	View v;
	for (uint32_t i = 0; i < 4; ++i) v.look[i] = ldf(camera + kLookAt + i * 4);
	v.roll = ldf(camera + kRoll);
	v.strength = ldf(camera + kSwayStrength);
	return v;
}

void Write(uint32_t camera, const View& v)
{
	for (uint32_t i = 0; i < 4; ++i) stf(camera + kLookAt + i * 4, v.look[i]);
	stf(camera + kRoll, v.roll);
	stf(camera + kSwayStrength, v.strength);
}

//(camera)
void CameraSway(PPCContext& c)
{
	uint32_t camera = c.r[3], frame = ld32(kSwayFrame);
	float rate = engine_rate(), scale = g_scale;
	if (g_force > 0 && guest_ptr(camera)) stf(camera + kSwayStrength, g_force);
	if (scale != 1.0f && guest_ptr(camera)) stf(camera + kSwayStrength, ldf(camera + kSwayStrength) * scale);
	if (g_off || !(rate > kGameFps + 0.5f) || !guest_ptr(camera))
	{
		g_time = frame;
		g_time_frame = frame;
		float strength = g_trace && guest_ptr(camera) ? ldf(camera + kSwayStrength) : 0.0f;
		orig_sway(c);
		if (g_trace && guest_ptr(camera)) rt_log("sway: frame %u rate %.0f strength %.3f look y %.4f roll %.5f (the game's)", frame, rate, strength, ldf(camera + kLookAt + 4), ldf(camera + kRoll));
		return;
	}
	if (frame != g_time_frame)
	{
		//a frame of the game is that much of a console's. after a jump in the count (a new level) it starts from there
		uint32_t frames = frame - g_time_frame;
		g_time = frames < 8 ? g_time + frames * (double)(kGameFps / rate) : (double)frame;
		g_time_frame = frame;
	}
	double whole = std::floor(g_time);
	float share = (float)(g_time - whole);
	const View in = Read(camera);
	PPCContext entry = c;
	st32(kSwayFrame, (uint32_t)whole);
	orig_sway(c);
	const View before = Read(camera);
	Write(camera, in);
	c = entry;
	st32(kSwayFrame, (uint32_t)whole + 1);
	orig_sway(c);
	View out = Read(camera);
	st32(kSwayFrame, frame);
	for (uint32_t i = 0; i < 4; ++i) out.look[i] = before.look[i] + (out.look[i] - before.look[i]) * share;
	out.roll = before.roll + (out.roll - before.roll) * share;
	Write(camera, out);
	if (g_trace) rt_log("sway: frame %u rate %.0f strength %.3f look y %.4f roll %.5f (time %.3f)", frame, rate, in.strength, out.look[1], out.roll, g_time);
}

//(the shakes, camera)
void CameraShake(PPCContext& c)
{
	uint32_t camera = c.r[4];
	float scale = g_scale;
	if (scale == 1.0f || !guest_ptr(camera))
	{
		orig_shake(c);
		return;
	}
	float before[3];
	for (uint32_t i = 0; i < 3; ++i) before[i] = ldf(camera + kLookAt + i * 4);
	orig_shake(c); //run all the same: it marks the slot it took
	for (uint32_t i = 0; i < 3; ++i)
	{
		float after = ldf(camera + kLookAt + i * 4);
		stf(camera + kLookAt + i * 4, before[i] + (after - before[i]) * scale);
	}
}

} //namespace

float shake_scale() { return g_scale; }
void set_shake_scale(float scale) { g_scale = std::clamp(scale, 0.0f, kShakeMax); }

void install_shake()
{
	std::string saved = saved_setting("GE_SHAKE", "LAUNCH_SHAKE");
	if (!saved.empty()) set_shake_scale((float)std::atof(saved.c_str()));
	orig_shake = ppc_hook("ge_camera_shake", CameraShake);
	if (!orig_shake) rt_log("goldeneye: ge_camera_shake is not hooked in this build, Screen Shake only scales the sway");
	g_off = std::getenv("GE_SWAY_OFF") != nullptr;
	g_trace = std::getenv("GE_SWAYTRACE") != nullptr;
	if (const char* e = std::getenv("GE_SWAY_FORCE")) g_force = (float)std::atof(e);
	orig_sway = ppc_hook("ge_camera_sway", CameraSway);
	if (!orig_sway) rt_log("goldeneye: ge_camera_sway is not hooked in this build, the camera's sway gets faster with the frame rate");
}

} //namespace ge
