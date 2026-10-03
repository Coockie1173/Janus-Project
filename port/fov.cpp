// GoldenEye 007 — the field of view.
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace ge {

// The field of view. The word at 806EB694 (the global before the player
// pointer) points to an object whose float at +0x328 is the view's angle in
// degrees: 49 in play. (Found by setting every 49.0 in memory and halving the
// range: this one alone widens the world, and leaves the HUD as it is; the
// executable's own 49.0 constants are not read once a level is up.) Only the
// game's 49 (or what the port put there last: the settings menu changes it in play) is replaced, each frame, so whatever else sets an angle of its own
// (a cutscene) keeps it.
const uint32_t kViewSettingsPtr = 0x806EB694;
std::atomic<float> g_fov{0};                         // 0: the game's own
float g_fov_written = 0;                             // what the view was last given: ours to replace again

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

void install_fov() {
    const char* e = std::getenv("GE_FOV");
    if (!e) return;
    float f = (float)std::atof(e);
    if (f < kFovMin || f > kFovMax) { rt_log("goldeneye: GE_FOV is 20..120 degrees; the game's 49 kept"); return; }
    if (f != kGameFov) { g_fov = f; rt_log("goldeneye: field of view %.0f degrees", f); }
}

}  // namespace ge
