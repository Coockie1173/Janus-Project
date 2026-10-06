//the field of view
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <set>

namespace ge {

//806EB694 (the global before the player pointer) points to an object with the view's
//angle in degrees as a float at +0x328, 49 in play
//found it by setting every 49.0 in memory and halving the range, this is the only one
//that widens the world and leaves the HUD alone
//we only replace the game's 49 (or what we wrote last), so a cutscene keeps its own angle
const uint32_t kViewSettingsPtr = 0x806EB694;
std::atomic<float> g_fov{0}; //0 is the game's own
float g_fov_written = 0; //what we last wrote, so we know it's ours to replace

void fov_apply() {
    uint32_t o = ld32(kViewSettingsPtr);
    if (!guest_ptr(o)) return;
    float want = g_fov > 0 ? g_fov.load() : kGameFov, now = ldf(o + 0x328);
    if (now == want || (now != kGameFov && now != g_fov_written)) return;
    stf(o + 0x328, want);
    g_fov_written = want;
}

float fov() { return g_fov > 0 ? g_fov.load() : kGameFov; }
void set_fov(float degrees) { g_fov = degrees == kGameFov ? 0 : std::clamp(degrees, kFovMin, kFovMax); }

//aiming down the sight: the controller keeps the angle the view is drawn with at +0x71C
//(+0x720 holds the same), worked out again every update as
//    the view's angle + (the weapon's aimed angle - the view's angle) * the aim blend
//with the blend (+0xD10) of the update before. the P99's aimed angle is 22.5
//that's the game's number whatever the view's is, so a wide view zooms a lot further than 49 did
//the option scales the weapon's aimed angle: we work it back out of what the game wrote, and
//write the angle it would have got with the scaled one. never wider than the view itself
const uint32_t kCtrlFov = 0x71C, kCtrlAimBlend = 0xD10;
std::atomic<float> g_ads_fov{0}; //0 is the game's own

void ads_fov_apply(uint32_t ctrl) {
    static uint32_t last_ctrl = 0;
    static float last_blend = 0, aimed = 0; //the blend the game just used, the weapon's angle as last worked out
    float blend = last_ctrl == ctrl ? last_blend : 0;
    last_ctrl = ctrl;
    last_blend = ldf(ctrl + kCtrlAimBlend);
    uint32_t o = ld32(kViewSettingsPtr);
    if (g_ads_fov <= 0 || !guest_ptr(o) || !(blend > 0) || blend > 1) { if (!(blend > 0)) aimed = 0; return; }
    float view = ldf(o + 0x328), now = ldf(ctrl + kCtrlFov);
    if (!std::isfinite(view) || !std::isfinite(now) || view < 1 || now < 1 || now > view) return;
    if (blend > 0.2f || aimed == 0) aimed = view + (now - view) / blend; //early in the blend it's mostly rounding
    float want = std::min(view, aimed * g_ads_fov / kGameAdsFov);
    float angle = view + (want - view) * blend;
    stf(ctrl + kCtrlFov, angle);
    stf(ctrl + kCtrlFov + 4, angle);
}

float ads_fov() { return g_ads_fov > 0 ? g_ads_fov.load() : kGameAdsFov; }
void set_ads_fov(float degrees) { g_ads_fov = degrees == kGameAdsFov ? 0 : std::clamp(degrees, kAdsFovMin, kFovMax); }

//a mesh a level glues to the eye is made for the game's 49 degrees
//a level's colour filter is one: a dish half a unit wide and half a unit ahead, drawn over the
//picture with a multiply blend. it reaches 45 degrees from the middle, so past a view of about 59
//the screen's sides look past it and keep their own colours
//draw_delayed(entry, ...) draws a mesh, entry +4 points to the matrix it's drawn with (3 rows of 4):
//the view times the mesh's own, with z turned round. where that has no rotation and no offset
//the mesh sits on the eye, and for that draw its x and y are stretched by the tangents' ratio, so it
//covers the part of the screen it covered at 49
//Facility draws its dish as it is, Tank and Solar size theirs in the matrix (0.88 by 0.49 in Tank),
//so the matrix's own x and y can be anything
//GE_NO_COLOUR_FILTER=1 (Disable Colour Filter) leaves the dish out instead, at any angle
//entry +0x10 is the mesh: +4 its flags (0x1000 it follows the camera, 4 on every overlay),
//+0x10 and +0x20 the corners of its box. the dish's box is a unit wide and from 0.397 behind to
//0.48 or more ahead. the flat pieces Tank draws on the eye next to it aren't the filter and stay
const uint32_t kEntryDrawMtx = 0x4, kEntryMesh = 0x10;
const uint32_t kMeshFlags = 0x4, kMeshBoxMin = 0x10, kMeshBoxMax = 0x20, kMeshOnCameraOverlay = 0x1004;
PPCFunc orig_draw_delayed = nullptr;
std::atomic<bool> g_no_colour_filter{false};
bool g_trace_filter = false;

bool OnTheEye(uint32_t mtx) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) {
            float now = ldf(mtx + r * 16 + c * 4);
            if (r != c || r == 2) { if (!(std::fabs(now - (r == c ? -1.0f : 0.0f)) < 0.01f)) return false; }
            else if (!(now > 0.1f && now < 10)) return false;
        }
    return true;
}

bool IsColourFilter(uint32_t mesh) {
    if (!guest_ptr(mesh) || (ld32(mesh + kMeshFlags) & kMeshOnCameraOverlay) != kMeshOnCameraOverlay) return false;
    for (int i = 0; i < 2; ++i) {
        float lo = ldf(mesh + kMeshBoxMin + i * 4), hi = ldf(mesh + kMeshBoxMax + i * 4);
        if (!(lo > -0.7f && lo < -0.4f && hi > 0.4f && hi < 0.7f)) return false;
    }
    return ldf(mesh + kMeshBoxMin + 8) < -0.3f && ldf(mesh + kMeshBoxMax + 8) > 0.4f;
}

void DrawDelayed(PPCContext& c) {
    uint32_t settings = ld32(kViewSettingsPtr), entry = c.r[3];
    float angle = guest_ptr(settings) ? ldf(settings + 0x328) : 0;
    bool wide = angle > kGameFov && angle <= kFovMax, hidden = g_no_colour_filter;
    if ((!wide && !hidden) || !guest_ptr(entry)) { orig_draw_delayed(c); return; }
    uint32_t mtx = ld32(entry + kEntryDrawMtx);
    if (!guest_ptr(mtx) || !OnTheEye(mtx)) { orig_draw_delayed(c); return; }
    if (hidden && IsColourFilter(ld32(entry + kEntryMesh))) {
        static std::set<uint32_t> said; //GE_TRACE: each dish once
        uint32_t mesh = ld32(entry + kEntryMesh);
        if (g_trace_filter && said.size() < 64 && said.insert(mesh).second)
            rt_log("goldeneye: colour filter %08X left out, drawn %.2f by %.2f", mesh, ldf(mtx), ldf(mtx + 16 + 4));
        return;
    }
    if (!wide) { orig_draw_delayed(c); return; }
    const float kHalf = 3.14159265f / 360;
    float wider = std::tan(angle * kHalf) / std::tan(kGameFov * kHalf), x = ldf(mtx), y = ldf(mtx + 16 + 4);
    stf(mtx, x * wider);
    stf(mtx + 16 + 4, y * wider);
    orig_draw_delayed(c);
    stf(mtx, x);
    stf(mtx + 16 + 4, y);
}

bool no_colour_filter() { return g_no_colour_filter; }
void set_no_colour_filter(bool on) { g_no_colour_filter = on; }

void install_fov() {
    if (const char* e = std::getenv("GE_NO_COLOUR_FILTER")) g_no_colour_filter = std::atoi(e) != 0;
    g_trace_filter = std::getenv("GE_TRACE") != nullptr;
    orig_draw_delayed = ppc_hook("draw_delayed?", DrawDelayed);
    if (!orig_draw_delayed) rt_die("goldeneye: the mesh draw is not hooked in this build");
    if (const char* a = std::getenv("GE_ADS_FOV")) {
        float f = (float)std::atof(a);
        if (f >= kAdsFovMin && f <= kFovMax) set_ads_fov(f);
        else rt_log("goldeneye: GE_ADS_FOV is 10..120 degrees; the game's 22.5 kept");
    }
    const char* e = std::getenv("GE_FOV");
    if (!e) return;
    float f = (float)std::atof(e);
    if (f < kFovMin || f > kFovMax) { rt_log("goldeneye: GE_FOV is 20..120 degrees; the game's 49 kept"); return; }
    if (f != kGameFov) { g_fov = f; rt_log("goldeneye: field of view %.0f degrees", f); }
}

} //namespace ge
