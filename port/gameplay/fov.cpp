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
//(+0x720 holds the same), worked out by PlayerCamera (80137690) every update as
//    the view's angle + (the weapon's aimed angle - the view's angle) * the aim blend (+0xD10)
//the P99's aimed angle is 22.5
//that's the game's number whatever the view's is, so a wide view zooms a lot further than 49 did
//the option scales the weapon's aimed angle: we work it back out of what the game wrote, and
//write the angle it would have got with the scaled one. never wider than the view itself
//right after PlayerCamera, not after the controller's update: where a cutscene has the player
//(state 0x22: Airfield's motorbike, Dam's truck, a breach) the camera takes the angle inside
//that update, and one written after it was never drawn
const uint32_t kCtrlFov = 0x71C, kCtrlAimBlend = 0xD10;
const uint32_t kStepAgain = 0x806EBA7C; //a byte, set while a controller's step is done again or the host's copy is stepped
std::atomic<float> g_ads_fov{0}; //0 is the game's own
std::atomic<float> g_host_drawn{0}; //the angle a console draws this player's aim with, 0 when it isn't aimed
std::atomic<float> g_aim_zoom{1}; //how much further than the pistol's sight the weapon in the hand zooms, 1 or less
const float kHalfRadPerDegree = 0.00872665f, kSightFov = 20.0f; //anything aimed narrower than that counts as a scope
PPCFunc orig_player_camera = nullptr;

void ads_fov_apply(uint32_t ctrl) {
    static float aimed = 0; //the weapon's angle as last worked out
    float blend = ldf(ctrl + kCtrlAimBlend);
    g_host_drawn = 0;
    g_aim_zoom = 1;
    uint32_t o = ld32(kViewSettingsPtr);
    if (!guest_ptr(o) || !(blend > 0) || blend > 1) { if (!(blend > 0)) aimed = 0; return; }
    float view = ldf(o + 0x328), now = ldf(ctrl + kCtrlFov);
    if (!std::isfinite(view) || !std::isfinite(now) || view < 1 || now < 1 || now > view) return;
    if (blend > 0.2f || aimed == 0) aimed = view + (now - view) / blend; //early in the blend it's mostly rounding
    g_host_drawn = kGameFov + (aimed - kGameFov) * blend;
    //a scope's angle is a few degrees (the sniper's goes on with its zoom, +0xCF4), and the same
    //turn crosses that much more of the screen: the mouse turns by the two pictures' sizes less
    //a plain sight stays as it was, the pistol aims at 22.5 in training and at 21 on Dam
    float option = g_ads_fov > 0 ? g_ads_fov / kGameAdsFov : 1.0f;
    float want = std::min(view, aimed * option), sight = std::min(view, kSightFov * option);
    if (want >= 1 && want < sight) g_aim_zoom = std::tan(want * kHalfRadPerDegree) / std::tan(sight * kHalfRadPerDegree);
    if (g_ads_fov <= 0) return;
    float angle = view + (want - view) * blend;
    stf(ctrl + kCtrlFov, angle);
    stf(ctrl + kCtrlFov + 4, angle);
}

//(controller): only the player on screen, a tank has other things where the aim's blend is
void PlayerCamera(PPCContext& c) {
    uint32_t ctrl = c.r[3];
    orig_player_camera(c);
    if (!guest_ptr(ctrl) || ctrl != local_controller() || ld8(kStepAgain) != 0) return;
    if (is_tank(ctrl)) g_aim_zoom = 1;
    else ads_fov_apply(ctrl);
}

float aim_zoom() { return g_aim_zoom; }

//the same aim on a console, whose view is the game's 49 and whose weapon aims at its own angle:
//what the host of a network game works this player's aimed turning out with (input/look.cpp)
float host_drawn_fov() { return g_host_drawn; }

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
//Dam, Airfield, Nightclub and the others tint the picture with an orb instead: a mesh with a box of
//exactly -1 to 1, following the camera, drawn round the eye and turned with the world (so not on
//the eye the way the dish is). it surrounds the eye, so it covers any view and isn't widened
const uint32_t kEntryDrawMtx = 0x4, kEntryMesh = 0x10;
const uint32_t kMeshFlags = 0x4, kMeshBoxMin = 0x10, kMeshBoxMax = 0x20, kMeshOnCameraOverlay = 0x1004, kMeshFollowsCamera = 0x1000;
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

bool IsColourOrb(uint32_t mesh, uint32_t mtx) {
    if (!guest_ptr(mesh) || !guest_ptr(mtx) || !(ld32(mesh + kMeshFlags) & kMeshFollowsCamera)) return false;
    for (int i = 0; i < 3; ++i) {
        float lo = ldf(mesh + kMeshBoxMin + i * 4), hi = ldf(mesh + kMeshBoxMax + i * 4);
        if (!(std::fabs(lo + 1) < 0.01f && std::fabs(hi - 1) < 0.01f && std::fabs(ldf(mtx + i * 16 + 12)) < 0.01f)) return false;
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

//for a level's picture (pictures.cpp) the hud and the player's own gun and body are left out
//the hud's meshes have 0x40000000 in their flags and are drawn 28 ahead of the eye, the gun's
//parts are within a unit of it
//the arms and the body are a character's skinned parts (3 in the flags), all drawn from the
//body's own place: 0.86 from the eye at the game's frame rate, 1.53 at 144
const uint32_t kMeshOnHud = 0x40000000, kMeshSkinned = 0x3;
const float kHudAhead = 28.0f, kOwnReach = 1.0f, kBodyReach = 2.5f;
bool OutOfPicture(uint32_t entry) {
    if (!guest_ptr(entry)) return false;
    uint32_t mtx = ld32(entry + kEntryDrawMtx), mesh = ld32(entry + kEntryMesh);
    if (!guest_ptr(mtx) || !guest_ptr(mesh)) return false;
    float x = ldf(mtx + 12), y = ldf(mtx + 28), z = ldf(mtx + 44);
    uint32_t flags = ld32(mesh + kMeshFlags);
    if ((flags & kMeshOnHud) && std::fabs(z + kHudAhead) < 0.5f) return true;
    if (flags & kMeshFollowsCamera) return false; //the sky, a colour filter
    float reach = (flags & kMeshSkinned) == kMeshSkinned ? kBodyReach : kOwnReach;
    return x * x + y * y + z * z < reach * reach;
}

void DrawDelayed(PPCContext& c) {
    if (pictures_hiding() && OutOfPicture(c.r[3])) return;
    if (screen_draw(c)) return;
    uint32_t settings = ld32(kViewSettingsPtr), entry = c.r[3];
    float angle = guest_ptr(settings) ? ldf(settings + 0x328) : 0;
    bool wide = angle > kGameFov && angle <= kFovMax, hidden = g_no_colour_filter;
    if ((!wide && !hidden) || !guest_ptr(entry)) { orig_draw_delayed(c); return; }
    uint32_t mtx = ld32(entry + kEntryDrawMtx), mesh = ld32(entry + kEntryMesh);
    if (hidden && IsColourOrb(mesh, mtx)) {
        static std::set<uint32_t> said; //GE_TRACE: each orb once
        if (g_trace_filter && said.size() < 64 && said.insert(mesh).second) rt_log("goldeneye: colour filter orb %08X left out", mesh);
        return;
    }
    if (!guest_ptr(mtx) || !OnTheEye(mtx)) { orig_draw_delayed(c); return; }
    if (hidden && IsColourFilter(mesh)) {
        static std::set<uint32_t> said; //GE_TRACE: each dish once
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
    orig_player_camera = ppc_hook("PlayerCamera?", PlayerCamera);
    if (!orig_player_camera) rt_die("goldeneye: the player's camera is not hooked in this build");
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
