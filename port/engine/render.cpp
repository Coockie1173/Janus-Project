//anisotropic filtering and anti-aliasing: the renderer's (wiikit/runtime/video.cpp), the
//settings menu's rows for them and what the game starts with
//
//anisotropic filtering: a texture seen at a slant (a floor, a wall along a corridor) is
//sampled along the slant, so it stays sharp into the distance. 1 is off, the console's picture
//anti-aliasing: the picture is drawn with that many samples a pixel (MSAA), which smooths the
//edges of what's drawn. 1 is off
//both count right away
//
//GE_ANISO=N 2, 4, 8 or 16
//GE_MSAA=N 2, 4 or 8 samples a pixel
#include "ge.h"
#include <cstdlib>
#include <string>

namespace ge
{

int anisotropy() { return video_anisotropy(); }
void set_anisotropy(int level) { video_set_anisotropy(level); }
int anti_aliasing() { return video_msaa(); }
void set_anti_aliasing(int samples) { video_set_msaa(samples); }

void install_render()
{
	if (std::string saved = saved_setting("GE_ANISO", "LAUNCH_ANISO"); !saved.empty()) set_anisotropy(std::atoi(saved.c_str()));
	if (std::string saved = saved_setting("GE_MSAA", "LAUNCH_MSAA"); !saved.empty()) set_anti_aliasing(std::atoi(saved.c_str()));
}

} //namespace ge
