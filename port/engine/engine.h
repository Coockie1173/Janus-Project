//the engine itself: its frame rate, its hot math on the host, and what keeps it right at any frame rate
#pragma once
#include "guest.h"

namespace ge
{

//framerate.cpp
void install_fps();
//long steps that nothing waits for, to get through a cutscene. easeRate: the engine's rate
//for steps that are shown a frame each instead, 0 for the long ones
void set_fast_forward(bool on, float easeRate = 0.0f);
float skip_rate(); //frames a second of game time in the long steps
uint32_t engine_top_rate();
float engine_rate(); //the rate the engine steps at right now
const uint32_t kGameFps = 30;
uint32_t fps_limit(); //the most frames a second the engine may run
void set_fps_limit(uint32_t fps);
bool vsync(); //frames wait for the screen's refresh
void set_vsync(bool on);

//native/: the engine's hot math on the host
void install_native();

//files.cpp
void install_files();

//records.cpp
void install_records();
void records_freed(const PPCContext& c, uint32_t block); //a block is about to be freed

//heap.cpp
void install_heap();
void heap_frame(); //once a frame: the heaps are walked (every frame in a network game)
std::string call_chain(const PPCContext& c); //who called, from the guest's stack

//sweep.cpp
void install_sweep();

//zones.cpp
void install_zones();
bool zones_mark(); //F9 with GE_ZONELOG: a mark in the log, false when there is none

//body.cpp
void install_body();

//unload.cpp
void install_unload();

//timeline.cpp
void install_timeline();

//flipbooks.cpp
void install_flipbooks();

//shake.cpp
void install_shake();
const float kShakeMax = 5.0f;
float shake_scale(); //the game's screen shake times this, 0 is none
void set_shake_scale(float scale);

//drawdist.cpp
void install_draw_distance();
const float kDrawDistanceMax = 5.0f; //the top is Unlimited
float draw_distance(); //levels of detail picked as if that many times nearer, 1 is the game's
void set_draw_distance(float scale);

//typing.cpp
void install_typing();

//render.cpp
void install_render(); //anisotropic filtering and anti-aliasing
int anisotropy(); //1 is off, up to 16
void set_anisotropy(int level);
int anti_aliasing(); //samples a pixel, 1 is off
void set_anti_aliasing(int samples);

} //namespace ge
