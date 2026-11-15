//TASLib, the private library: loaded at run time when it's beside the game, nothing happens without it
//the game offers it hooks and primitives (taslib_api.h), what it does with them is the library's
#pragma once
#include "guest.h"

union SDL_Event;

namespace ge
{

//tas.cpp
void install_tas();
bool tas_loaded();
void tas_frame(uint32_t frame); //once a frame
void tas_boundary(PPCContext& c); //between two frames on the game's thread (framerate.cpp)
void tas_controller(uint32_t ctrl); //the local controller's update starts (players.cpp)
bool tas_input(int chan, ClassicState& s); //the raw pad: true when the library's replaced it
void tas_buttons(int chan, ClassicState& s); //the classic's filter, last
uint32_t lib_fixed_rate(); //the rate the library holds the engine at, 0 none
bool lib_frames_as_drawn();
bool lib_mouse_taken(); //the mouse is the library's: it turns nothing and works no menu by itself
bool tas_event(const SDL_Event* e); //a window's key, the library's first (overlay.cpp)
bool tas_showing(); //the library has text up
void tas_draw(); //inside the overlay's ImGui frame

} //namespace ge
