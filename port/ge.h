// GoldenEye 007 (Wii, SJBE52) — what the port's files share.
//
//   goldeneye.cpp    the layer: what is installed, what runs each frame
//   framerate.cpp    the engine's frame limiter (GE_FPS)
//   input.cpp        mouse look, the wheel, mouse buttons in menus (GE_MOUSE, GE_MOUSE_MODE)
//   fov.cpp          the field of view (GE_FOV)
//   gecko.cpp        Gecko codes run each frame (GE_GECKO, GE_GECKO_EXTRA)
//   native.cpp       the engine's hot math routines written for the host (GE_NATIVE)
//   net.cpp          the hosts file for the game's servers (GE_HOSTS, GE_NETLOG)
//   overlay.cpp      the debug overlay (F3): memory watches from a .dmw file (dmw.cpp)
//   crashdump.cpp    crash, die and stall dumps into build/crashes (GE_CRASH_MEM, GE_NO_CRASHDUMP)
//   diagnostics.cpp  the sampling profiler (GE_PROFILE)
//   debug.cpp        aids for finding things in the running game (scans, probes, scripted input)
#pragma once
#include "rt.h"
#include "video.h"
#include <cstdint>
#include <cstring>

namespace ge {

// ---- the game's memory -------------------------------------------------------------------------
const uint32_t kFrameCount = 0x806EBED0;             // -0x50B0(r13): frames run
const uint32_t kPlayerPtr = 0x806EB698;              // a player exists (any one): tells play from menus
const uint32_t kPaused = 0x806EB69C;                 // a byte: 1 while the pause menu is up

inline bool guest_ptr(uint32_t p) {
    return (p >= 0x80003000u && p < 0x81800000u) || (p >= 0x90000000u && p < 0x94000000u);
}
inline float ldf(uint32_t a) { uint32_t b = ld32(a); float v; std::memcpy(&v, &b, 4); return v; }
inline void stf(uint32_t a, float v) { uint32_t b; std::memcpy(&b, &v, 4); st32(a, b); }
inline bool in_play() { return guest_ptr(ld32(kPlayerPtr)) && ld8(kPaused) == 0; }

// ---- the parts ---------------------------------------------------------------------------------
void each_frame(uint32_t frame);                     // goldeneye.cpp: called by the limiter's hook, once a game frame

void install_fps();                                  // framerate.cpp
void install_crash_handler();                        // crashdump.cpp
void crash_test();
void install_profiler();                             // diagnostics.cpp
void install_mouse();                                // input.cpp
void mouse_frame(uint32_t frame);
void mouse_capture(bool on);
void install_hosts();                                // net.cpp
void install_gecko();                                // gecko.cpp
void gecko_apply();
void install_fov();                                  // fov.cpp
void fov_apply();
void install_native();                               // native.cpp
void install_overlay();                              // overlay.cpp
bool overlay_open();

}  // namespace ge
