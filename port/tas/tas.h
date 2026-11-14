//TASLib, the private library: loaded at run time when it's beside the game, nothing happens without it
#pragma once
#include "guest.h"

namespace ge
{

//tas.cpp
void install_tas();
void tas_frame(uint32_t frame); //once a frame
void tas_buttons(int chan, ClassicState& s); //the classic's filter, last
bool tas_loaded();

} //namespace ge
