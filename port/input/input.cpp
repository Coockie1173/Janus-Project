//keyboard and mouse: mouse look, the wheel, the mouse buttons in menus
#include "input/mouse.h"
#include "debug/debug.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ge {

//the settings of the mouse and the keys, and installing input's parts (mouse.h lists them)
std::atomic<float> g_mouse_sens{1.0f};
std::atomic<bool> g_mouse_invert{false};
std::atomic<int> g_mouse_mode{0};
std::atomic<float> g_ads_sens{0.5f};
std::atomic<bool> g_mouse_everywhere{false};
std::atomic<bool> g_aim_lock{false};

void install_mouse() {
    if (const char* m = std::getenv("GE_MOUSE")) g_mouse_sens = (float)std::atof(m);
    if (const char* m = std::getenv("GE_MOUSE_INVERT")) g_mouse_invert = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_MOUSE_EVERYWHERE")) g_mouse_everywhere = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_ADS_SENS")) set_ads_sens((float)std::atof(m));
    if (const char* m = std::getenv("GE_AIM_LOCK")) g_aim_lock = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_MOUSE_MODE")) g_mouse_mode = !std::strcmp(m, "direct") ? 1 : !std::strcmp(m, "stick") ? 2 : 0;
    install_buttons();
    //hooked whatever the sensitivity is, the settings menu can turn mouse look on and off in play
    install_players();
    install_look();
    video_keep_cursor(true); //the game draws no pointer, a free cursor is the system's
    mouse_capture(true);
}

float mouse_sens() { return g_mouse_sens; }
void set_mouse_sens(float s) { g_mouse_sens = std::clamp(s, 0.0f, kMouseSensMax); }
int mouse_mode() { return g_mouse_mode; }
void set_mouse_mode(int m) { g_mouse_mode = m; }
bool mouse_invert() { return g_mouse_invert; }
void set_mouse_invert(bool on) { g_mouse_invert = on; }
float ads_sens() { return g_ads_sens; }
void set_ads_sens(float s) { g_ads_sens = std::clamp(s, kAdsSensMin, kAdsSensMax); }
bool aim_lock() { return g_aim_lock; }
void set_aim_lock(bool on) { g_aim_lock = on; }
bool mouse_everywhere() { return g_mouse_everywhere; }
void set_mouse_everywhere(bool on) { g_mouse_everywhere = on; }

//once a game frame, with nobody to turn (a menu, a pause) the mouse's motion is dropped
void mouse_applied(uint32_t frame) { g_look_frame = frame; }
void mouse_frame(uint32_t f) {
    mouse_capture(!overlay_open());
    if (!g_stick_took && !lib_mouse_taken() && g_look_frame != f - 1 && g_look_frame != f) {
        float dx, dy;
        video_take_mouse_motion(dx, dy);
    }
}

//the cursor is the game's while it plays and free while an overlay (F1, F3) is up
//the cursor is captured and hidden in play only: in a menu, a pause or a load it is free and seen
void mouse_capture(bool on) {
    const bool captured = on && g_mouse_sens > 0 && in_play();
    static std::atomic<int> was{-1};
    if (was.exchange(captured) != (int)captured && std::getenv("GE_TRACE")) rt_log("goldeneye: the cursor is %s", captured ? "captured and hidden" : "free");
    video_set_relative_mouse(captured);
}

} //namespace ge
