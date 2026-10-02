// GoldenEye 007 — the field of view.
#include "ge.h"
#include <cstdlib>

namespace ge {

// The field of view. The word at 806EB694 (the global before the player
// pointer) points to an object whose float at +0x328 is the view's angle in
// degrees: 49 in play. (Found by setting every 49.0 in memory and halving the
// range: this one alone widens the world, and leaves the HUD as it is; the
// executable's own 49.0 constants are not read once a level is up.) Only the
// game's 49 is replaced, each frame, so whatever else sets an angle of its own
// (a cutscene) keeps it.
const uint32_t kViewSettingsPtr = 0x806EB694;
const float kGameFov = 49.0f;
float g_fov = 0;

void fov_apply() {
    if (g_fov <= 0) return;
    uint32_t o = ld32(kViewSettingsPtr);
    if (guest_ptr(o) && ldf(o + 0x328) == kGameFov) stf(o + 0x328, g_fov);
}

void install_fov() {
    const char* e = std::getenv("GE_FOV");
    if (!e) return;
    float f = (float)std::atof(e);
    if (f < 20 || f > 140) { rt_log("goldeneye: GE_FOV is 20..140 degrees; the game's 49 kept"); return; }
    if (f != kGameFov) { g_fov = f; rt_log("goldeneye: field of view %.0f degrees", f); }
}

}  // namespace ge
