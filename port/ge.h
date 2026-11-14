//what the port's parts share, a folder each, a header each listing what its files offer:
//goldeneye.cpp  installs everything and runs the per-frame stuff
//guest.h        the game's memory
//engine/        the frame rate, the hot math on the host, fixes for any frame rate, the heap
//sound/         clicks, occlusion, height, the port's own .wav sounds
//input/         mouse look, the menus' cursor, prompts with the bound key, weapon slots
//gameplay/      fov, cutscenes, qte, movies, tweaks, autosave, the tank, 007 Custom, bloom, flashes, grain
//cheats/        god mode and the rest, noclip, stamina, collision and trigger views, gecko codes
//fun/           bunnyhopping, the weapon randomizer
//mods/          modpacks of loose .edb files, the Mods and Level Select menus, pictures, custom weapons
//net/           the hosts file, the server's live patch, client-owned players
//ui/            the settings menu (F1, ui/settings/), the debug overlay (F3), the window's icon
//speedrun/      the speedrun timer
//tas/           TASLib, the private library, loaded at run time when it's there
//debug/         crash dumps, the profiler, the debug aids (GE_ variables)
//package/       what the Windows and Linux packages ship next to the executable
#pragma once
#include "guest.h"
#include "cheats/cheats.h"
#include "debug/debug.h"
#include "engine/engine.h"
#include "fun/fun.h"
#include "gameplay/gameplay.h"
#include "input/input.h"
#include "mods/mods.h"
#include "net/net.h"
#include "sound/sound.h"
#include "speedrun/speedrun.h"
#include "tas/tas.h"
#include "ui/ui.h"

namespace ge
{

void each_frame(uint32_t frame); //goldeneye.cpp, called by the limiter's hook once a game frame

} //namespace ge
