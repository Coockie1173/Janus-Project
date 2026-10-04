//the field of view
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

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

void install_fov() {
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
