//the portal walk: the level behind a portal the camera has just gone through stays drawn
//
//a level's map is zones (0x8C bytes each) joined by portals (0x44 bytes each, a quad's four
//corners at +0x14). for a view, ge_zone_root finds the zone the camera's position lies in
//(ge_zone_at, a lookup by position, nothing is remembered between frames) and draws it, then
//ge_zone_walk goes through the zone's portals: ge_portal_clip cuts each one to the screen
//rectangle the walk came in with, and only a portal that leaves something gets its zone drawn
//and walked in turn (16 deep at most). a portal with a corner behind the eye isn't cut: the
//rectangle goes through as it came. one with all four behind it is dropped
//
//so the moment the camera is past a portal's plane and turned from it, the zone it came out of
//is gone, with whatever of it is still in the picture: climbing into or out of a vent the
//level went black for up to half a second (Facility, the ducts by the fan room: zones 94 to 98,
//the display lists a frame went from ~100 to ~40 and back)
//the fix: a portal the clip drops is kept after all, with the rectangle as it came (what the
//game does for one that straddles the eye), while the eye is within kNear of the portal's
//plane and over the quad or within kNear of its edge. it only ever draws more
//
//GE_PORTAL_NEAR=N that distance (1.5 unless given), 0 leaves the game's answer alone
//GE_ZONELOG=file writes a line for every view: the game's frame, seconds, the camera's
//position, the zone it was found in (-1 for none), zones walked, portals kept and dropped,
//portals kept by the fix. a view that walks fewer zones than the one before it also gets what
//the clip worked from, for both views: each portal's 0x44 bytes, its link, the rectangle going
//in, the answer (2: kept by the fix), and the two matrices at 806421E0 and 80642220 with the
//screen's size and offset (the first 600 such views)
#include "ge.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ge
{
namespace
{

PPCFunc orig_root = nullptr, orig_at = nullptr, orig_walk = nullptr, orig_clip = nullptr;
FILE* g_log = nullptr;
bool g_inRoot = false;
int32_t g_zone = -2; //-2 is never asked
uint32_t g_walked = 0, g_kept = 0, g_dropped = 0, g_forced = 0;
float g_near = 1.5f; //GE_PORTAL_NEAR

const uint32_t kClipMatrices = 0x806421E0; //two 4x4 the clip multiplies the corners with
const uint32_t kViewPtr = 0x806EC2C8; //-0x4CB8(r13), the view: its width at +6, height at +8
const uint32_t kScreenOffset = 0x806EC298; //x, then y at +4
struct ClipCall
{
	uint32_t m_Portal, m_Link, m_Result;
	uint8_t m_PortalBytes[0x44], m_LinkBytes[8];
	float m_Rect[6];
};
std::vector<ClipCall> g_calls, g_callsBefore;
std::string g_viewBefore;
uint32_t g_dumps = 0, g_walkedBefore = 0;

std::string ViewState()
{
	std::string text{ "  matrices" };
	char word[32];
	for (uint32_t i = 0; i < 32; ++i)
	{
		std::snprintf(word, sizeof word, " %.9g", ldf(kClipMatrices + i * 4));
		text += word;
	}
	const uint32_t view{ ld32(kViewPtr) };
	std::snprintf(word, sizeof word, "\n  screen %u %u", guest_ptr(view) ? ld16(view + 6) : 0u, guest_ptr(view) ? ld16(view + 8) : 0u);
	text += word;
	std::snprintf(word, sizeof word, " offset %d %d\n", (int32_t)ld32(kScreenOffset), (int32_t)ld32(kScreenOffset + 4));
	text += word;
	return text;
}

void Dump(const char* what, const std::string& view, const std::vector<ClipCall>& calls)
{
	std::fprintf(g_log, " %s\n%s", what, view.c_str());
	for (const ClipCall& call : calls)
	{
		std::fprintf(g_log, "  clip portal %08X link %08X gives %u rect %.9g %.9g %.9g %.9g %.9g %.9g\n   portal", call.m_Portal, call.m_Link,
			call.m_Result, call.m_Rect[0], call.m_Rect[1], call.m_Rect[2], call.m_Rect[3], call.m_Rect[4], call.m_Rect[5]);
		for (uint8_t byte : call.m_PortalBytes) std::fprintf(g_log, " %02X", byte);
		std::fprintf(g_log, "\n   link");
		for (uint8_t byte : call.m_LinkBytes) std::fprintf(g_log, " %02X", byte);
		std::fprintf(g_log, "\n");
	}
}

//(world, position)
void Root(PPCContext& c)
{
	const uint32_t pos{ c.r[4] };
	const float x{ ldf(pos) }, y{ ldf(pos + 4) }, z{ ldf(pos + 8) };
	g_inRoot = true;
	g_zone = -2;
	g_walked = g_kept = g_dropped = g_forced = 0;
	g_calls.clear();
	orig_root(c);
	g_inRoot = false;
	if (!g_log) return;
	const std::string view{ ViewState() }; //as the walk left them
	static const auto start = std::chrono::steady_clock::now();
	const double seconds{ std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() };
	std::fprintf(g_log, "%u %.4f pos %.3f %.3f %.3f zone %d walked %u kept %u dropped %u forced %u\n",
		ld32(kFrameCount), seconds, x, y, z, g_zone, g_walked, g_kept, g_dropped, g_forced);
	if (g_walked < g_walkedBefore && g_dumps < 600)
	{
		++g_dumps;
		Dump("the view before", g_viewBefore, g_callsBefore);
		Dump("this view", view, g_calls);
	}
	g_walkedBefore = g_walked;
	g_callsBefore = g_calls;
	g_viewBefore = view;
}

//(tree, position) gives the zone's number
void At(PPCContext& c)
{
	orig_at(c);
	if (g_inRoot && g_zone == -2) g_zone = (int16_t)c.r[3];
}

//(world, map, zone, rectangle, depth)
void Walk(PPCContext& c)
{
	if (g_inRoot) ++g_walked;
	orig_walk(c);
}

//where the eye is for the matrix the clip uses: the point its x, y and w rows all give 0 for
bool Eye(float eye[3])
{
	double m[3][4];
	const int rows[3]{ 0, 1, 3 };
	for (int r = 0; r < 3; ++r)
	{
		for (int k = 0; k < 3; ++k) m[r][k] = ldf(kClipMatrices + (k * 4 + rows[r]) * 4);
		m[r][3] = -ldf(kClipMatrices + (12 + rows[r]) * 4);
	}
	for (int i = 0; i < 3; ++i)
	{
		int pivot{ i };
		for (int r = i + 1; r < 3; ++r) if (std::fabs(m[r][i]) > std::fabs(m[pivot][i])) pivot = r;
		if (std::fabs(m[pivot][i]) < 1e-9) return false;
		std::swap(m[i], m[pivot]);
		for (int r = 0; r < 3; ++r)
		{
			if (r == i) continue;
			const double f{ m[r][i] / m[i][i] };
			for (int k = i; k < 4; ++k) m[r][k] -= f * m[i][k];
		}
	}
	for (int i = 0; i < 3; ++i) eye[i] = (float)(m[i][3] / m[i][i]);
	return std::isfinite(eye[0]) && std::isfinite(eye[1]) && std::isfinite(eye[2]);
}

//is the eye within g_near of the portal's quad
bool NearPortal(uint32_t portal)
{
	float eye[3], corner[3][3]; //corners 0, 1 and 3: the quad's two edges from its first corner
	if (!Eye(eye)) return false;
	const int which[3]{ 0, 1, 3 };
	for (int i = 0; i < 3; ++i)
		for (int k = 0; k < 3; ++k) corner[i][k] = ldf(portal + 0x14 + which[i] * 12 + k * 4);
	float u[3], v[3], d[3];
	for (int k = 0; k < 3; ++k)
	{
		u[k] = corner[1][k] - corner[0][k];
		v[k] = corner[2][k] - corner[0][k];
		d[k] = eye[k] - corner[0][k];
	}
	const float n[3]{ u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
	const float nLen{ std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) };
	const float uLen{ std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]) }, vLen{ std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) };
	if (!(nLen > 1e-6f) || !(uLen > 1e-3f) || !(vLen > 1e-3f)) return false;
	const float off{ (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]) / nLen }; //from the plane
	const float along{ (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / uLen }, up{ (d[0] * v[0] + d[1] * v[1] + d[2] * v[2]) / vLen };
	return std::fabs(off) < g_near && along > -g_near && along < uLen + g_near && up > -g_near && up < vLen + g_near;
}

//(world, portal, link, rectangle, out) gives 0 when nothing of the portal is left
void Clip(PPCContext& c)
{
	ClipCall call{};
	call.m_Portal = c.r[4];
	call.m_Link = c.r[5];
	const uint32_t rect{ c.r[6] }, out{ c.r[7] };
	if (g_inRoot && g_log)
	{
		for (uint32_t i = 0; i < sizeof call.m_PortalBytes; ++i) call.m_PortalBytes[i] = ld8(call.m_Portal + i);
		for (uint32_t i = 0; i < sizeof call.m_LinkBytes; ++i) call.m_LinkBytes[i] = ld8(call.m_Link + i);
		for (uint32_t i = 0; i < 6; ++i) call.m_Rect[i] = ldf(rect + i * 4);
	}
	orig_clip(c);
	bool forced{ false };
	if (!c.r[3] && g_near > 0 && guest_ptr(call.m_Portal) && guest_ptr(rect) && guest_ptr(out) && NearPortal(call.m_Portal))
	{
		for (uint32_t i = 0; i < 6; ++i) st32(out + i * 4, ld32(rect + i * 4)); //as the game does for one that straddles the eye
		c.r[3] = 1;
		forced = true;
	}
	if (!g_inRoot) return;
	call.m_Result = forced ? 2 : c.r[3];
	if (g_log && g_calls.size() < 64) g_calls.push_back(call);
	if (forced) ++g_forced;
	if (c.r[3]) ++g_kept;
	else ++g_dropped;
}

} //namespace

void install_zones()
{
	if (const char* text{ std::getenv("GE_PORTAL_NEAR") }) g_near = std::clamp((float)std::atof(text), 0.0f, 10.0f);
	const char* path{ std::getenv("GE_ZONELOG") };
	if (path)
	{
		g_log = std::fopen(path, "w");
		if (!g_log) rt_log("goldeneye: GE_ZONELOG can't write %s", path);
		else rt_log("goldeneye: the portal walk is logged to %s", path);
	}
	if (!g_log && g_near <= 0) return;
	orig_root = ppc_hook("ge_zone_root", Root);
	orig_at = ppc_hook("ge_zone_at", At);
	orig_walk = ppc_hook("ge_zone_walk", Walk);
	orig_clip = ppc_hook("ge_portal_clip", Clip);
	if (!orig_root || !orig_at || !orig_walk || !orig_clip) rt_die("goldeneye: the portal walk is not hooked in this build");
}

} //namespace ge
