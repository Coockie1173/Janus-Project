//the mouse's cursor picks the menus' items
//
//the menus have a pointer of their own, the Wii Remote's (MenuPointer, the menu's +0x1A98):
//every item and every arrow a page draws is a hotspot of it (fn_8023E510), and while it is in
//use (+0x24) the menu draws a cursor at its place (+0 and +4, 0 to 1 across the screen), the
//item under it is the lit one and select picks that. its input (ge_menu_pointer_update,
//8023D740) takes the place from the Remote only when no classic controller is there, and the
//port's keys, mouse and gamepads are all a classic controller, so it was never in use
//
//after that input the cursor's place is the pointer's: a cursor that moves puts it in use,
//a direction pressed (the game's own rule) takes it out again until the cursor moves
//
//GE_POINTER="33-34:0.62,0.33,1 34-35:0.7,0.33,0" a cursor for a test: seconds from start, where across and down the picture, the left button
//GE_POINTER=0 the menus have no pointer
#include "ge.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kPointerX = 0x0, kPointerY = 0x4, kPointerUsed = 0x24;
const float kScreenWide = 640.0f, kScreenHigh = 448.0f; //the menus' screen, what a hotspot's place is in

struct Fake { double from, to; float x, y; bool left; };
std::vector<Fake> g_fakes;
const auto g_began = std::chrono::steady_clock::now();
PPCFunc orig_pointer_update = nullptr;
bool g_trace = false;
float g_last_x = -1, g_last_y = -1;
uint32_t g_used_frame = 0; //the last frame the pointer was in use

//the cursor across the picture, 0 to 1
bool Cursor(float& x, float& y, bool& left)
{
	const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_began).count();
	for (const Fake& fake : g_fakes)
		if (now >= fake.from && now < fake.to) { x = fake.x; y = fake.y; left = fake.left; return true; }
	return g_fakes.empty() && !overlay_open() && !speedrun_dragging() && video_mouse(x, y, left);
}

void PointerUpdate(PPCContext& c)
{
	const uint32_t pointer = c.r[3];
	orig_pointer_update(c);
	float x = 0, y = 0;
	bool left = false;
	if (!guest_ptr(pointer) || !Cursor(x, y, left)) { g_last_x = g_last_y = -1; return; }
	const bool inside = x >= 0 && x <= 1 && y >= 0 && y <= 1;
	const bool moved = x != g_last_x || y != g_last_y;
	g_last_x = x;
	g_last_y = y;
	if (moved && inside && !ld8(pointer + kPointerUsed))
	{
		st8(pointer + kPointerUsed, 1);
		if (g_trace) rt_log("goldeneye: the menus' pointer is in use, at %.2f %.2f", x, y);
	}
	if (!ld8(pointer + kPointerUsed)) return;
	g_used_frame = ld32(kFrameCount);
	stf(pointer + kPointerX, std::min(std::max(x, 0.0f), 1.0f));
	stf(pointer + kPointerY, std::min(std::max(y, 0.0f), 1.0f));
}

} //namespace

//once a frame: the menu draws a cursor of its own while its pointer is in use, the system's is left out then
void pointer_frame(uint32_t frame)
{
	const uint32_t kFrames = 3;
	video_keep_cursor(g_used_frame == 0 || frame - g_used_frame > kFrames);
}

bool menu_cursor(float& x, float& y, bool& left)
{
	if (!Cursor(x, y, left)) return false;
	x *= kScreenWide;
	y *= kScreenHigh;
	return true;
}

void install_pointer()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	const char* e = std::getenv("GE_POINTER");
	if (e && std::string(e) == "0") return;
	if (e)
	{
		Fake fake{};
		int left = 0, read = 0;
		for (const char* at = e; std::sscanf(at, " %lf-%lf:%f,%f,%d%n", &fake.from, &fake.to, &fake.x, &fake.y, &left, &read) == 5; at += read)
		{
			fake.left = left != 0;
			g_fakes.push_back(fake);
		}
	}
	orig_pointer_update = ppc_hook("ge_menu_pointer_update", PointerUpdate);
	if (!orig_pointer_update) rt_log("goldeneye: the menus' pointer isn't in this build, the cursor picks nothing");
}

} //namespace ge
