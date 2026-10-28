//the game's sound: no clicks, walls and floors in the way, height, and .wav sounds of the port's own
#pragma once
#include "guest.h"

namespace ge
{

//sound.cpp
void install_sound();

//occlusion.cpp
void install_occlusion(); //after install_mods
bool occlusion(); //sounds behind a wall or a floor are quieter
void set_occlusion(bool on);

//height.cpp
void install_height();
bool sound_height(); //sounds above and below the listener are muffled
void set_sound_height(bool on);

//customsound.cpp
int custom_sound_load(const std::string& path); //a .wav, -1 when it can't be read
void custom_sound_play(int sound, const float* pPlace, float volume); //at a place in the world, or nullptr for no place
} //namespace ge
