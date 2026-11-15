//keyboard and mouse, what input's files share:
//players.cpp whose controller is this console's, the player's state, how far it's aimed
//look.cpp the mouse turns the view: directly, in a command online, or on the right stick
//buttons.cpp the classic controller's buttons: the wheel, sprint, the aim lock, the menus
//input.cpp the settings, installing it all
#pragma once
#include "ge.h"
#include <atomic>

namespace ge {

//input.cpp, the settings
extern std::atomic<float> g_mouse_sens; //1 is 0.0022 radians a pixel
extern std::atomic<bool> g_mouse_invert; //pushing the mouse forward looks down
extern std::atomic<int> g_mouse_mode; //0 auto, 1 direct, 2 stick
extern std::atomic<float> g_ads_sens; //the mouse's sensitivity fully aimed, times the normal one
extern std::atomic<bool> g_mouse_everywhere; //cheat, the mouse turns the view in every state
extern std::atomic<bool> g_aim_lock;

//players.cpp
const uint32_t kPlayerState = 0x704, kStateFlags = 0x708, kStateCutscene = 0x22;
const uint32_t kInCutscene = 0x5E4; //a byte, set with state 0x22 and cleared when the cutscene lets go
const uint32_t kCutsceneLook = 0x1024, kCutsceneAim = 0xBD9;
const uint32_t kAimOn = 0xED4, kAimPitch = 0xEDC, kAimYaw = 0xEE0;
const uint32_t kAimLimits = 0x1014; //four floats in degrees, pitch from and to then yaw from and to
const uint32_t kAimLimitsPlay = 0x806E95F8, kAimLimitsNarrow = 0x806E9608; //the same four outside a cutscene, and with flag 0x80
const float kRadPerDegree = 0.01745329f;
const uint32_t kAimBlend = 0xD10;
const uint32_t kCtrlHostCopyOf = 0x98C, kCtrlHostCopy = 0x988; //a joined game's second controller, see controller_update
extern uint32_t g_local_frame, g_local_ctrl; //the local controller as of that frame
bool net_turns(); //a network game between players of Connect to WFC Modded
float aim_blend(uint32_t ctrl);
float aim_sens_at(float blend);
float aim_sens_scale(uint32_t ctrl);
enum Look { kLookLocked, kLookView, kLookAim, kLookForced };
Look look_mode(uint32_t ctrl);
const char* look_name(Look l);
bool stick_look_allowed();
void install_players();

//look.cpp
extern float g_pitch_limit_now; //what the two floats hold for the controller being updated
float pitch_limit(); //how far up and down the local view goes, radians
void mouse_moved(float dx); //the view was turned (the aim snap lets go of a turning view)
void set_pitch_limit(float limit);
float pitch_limit_of(uint32_t ctrl);
extern uint32_t g_look_frame; //the frame the mouse was last applied in
extern uint32_t g_look_ctrl; //the controller it's for, and what's still left to turn
extern float g_look_yaw;
extern bool g_stick_took; //the mouse is on the stick this frame
void mouse_aim(uint32_t ctrl);
void mouse_look(uint32_t ctrl, uint32_t player);
void mouse_stick(ClassicState& s);
void install_look();

//buttons.cpp
void sprint_frame(); //once a frame for the local controller, before its update
void aim_lock_frame(uint32_t ctrl); //once a frame for the local controller, after its update
void install_buttons();

} //namespace ge
