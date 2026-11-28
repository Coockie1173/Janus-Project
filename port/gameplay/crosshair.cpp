//dot only crosshair (off by default): the crosshair is a dot with four arrows round it, the
//arrows all drawn from one texture of the hud's file (o_bondhud.edb, 01090029): 86000037, 16x16
//RGB5A3 with its mips. the dot is the texture after it (86000038)
//the gecko code [Dot Only Cursor] found the file in MEM2 by its GEOM header and zeroed 768 bytes
//at +0xCE600: the arrow's pixels and, past their end, the start of the dot's texture header
//here the file is the one the game loaded (ge_geofile_onload, mods.cpp), the texture is found by
//its hash, and only its pixels are cleared: alpha 0, nothing drawn. the game's pixels are kept
//and put back when the option is turned off
//the texture cache sees the change by itself (it watches the pages textures are in)
//
//GE_DOT_CROSSHAIR=1 the same from the environment (kept in the settings file as LAUNCH_DOTCROSSHAIR)
//GE_TRACE=1 log the arrow being cleared and put back
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kHudFile = 0x01090029, kGeom = 0x47454F4D; //o_bondhud.edb
const uint32_t kArrow = 0x86000037; //the crosshair's arrow
const uint32_t kFileBase = 0x14, kFileHash = 0x20; //of the game's object for a file: where it is once it's in
const uint32_t kTextureList = 0xB8; //of the file: how many (a short), then where the entries are from here + 4
const uint32_t kEntrySize = 0x1C, kEntryHash = 0x0, kEntryPlace = 0xC; //the place is an address once the file is in
const uint32_t kMostTextures = 4096;
const uint32_t kTexDataSize = 0x28, kTexData = 0x40, kTexPixels = 0x80; //of a texture: its data's size (a TPL's), where the data and the pixels start
const uint32_t kMostPixels = 0x1000;

std::atomic<bool> g_dot_only{false};
uint32_t g_hud_file = 0; //the game's object for the hud's file, once it was loaded
uint32_t g_cleared = 0; //the pixels we cleared, 0: none
std::vector<uint8_t> g_game_pixels; //what was there
bool g_trace = false;

//the arrow's pixels in memory and how many bytes they are, 0 while the file isn't in
uint32_t ArrowPixels(uint32_t& size)
{
	if (!guest_ptr(g_hud_file) || ld32(g_hud_file + kFileHash) != kHudFile) return 0;
	const uint32_t file = ld32(g_hud_file + kFileBase);
	if (!guest_ptr(file) || ld32(file) != kGeom || ld32(file + 4) != kHudFile) return 0;
	const uint32_t count = ld16(file + kTextureList), entries = file + kTextureList + 4 + ld32(file + kTextureList + 4);
	if (count > kMostTextures || !guest_ptr(entries) || !guest_ptr(entries + count * kEntrySize)) return 0;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t entry = entries + i * kEntrySize;
		if (ld32(entry + kEntryHash) != kArrow) continue;
		const uint32_t texture = ld32(entry + kEntryPlace);
		if (!guest_ptr(texture)) return 0;
		const uint32_t data = ld32(texture + kTexDataSize);
		if (data <= kTexPixels - kTexData || data - (kTexPixels - kTexData) > kMostPixels) return 0;
		size = data - (kTexPixels - kTexData);
		return guest_ptr(texture + kTexPixels + size) ? texture + kTexPixels : 0;
	}
	return 0;
}

bool Cleared(uint32_t at, uint32_t size)
{
	for (uint32_t i = 0; i < size; i += 4)
		if (ld32(at + i)) return false;
	return true;
}

} //namespace

bool dot_crosshair() { return g_dot_only; }
void set_dot_crosshair(bool on) { g_dot_only = on; }

void crosshair_file(uint32_t hash, uint32_t file)
{
	if (hash == kHudFile) g_hud_file = file;
}

//once a frame on the game's thread
void crosshair_apply()
{
	uint32_t size = 0;
	const uint32_t at = ArrowPixels(size);
	if (g_dot_only)
	{
		if (!at || Cleared(at, size)) return;
		g_game_pixels.resize(size);
		for (uint32_t i = 0; i < size; ++i) g_game_pixels[i] = ld8(at + i);
		for (uint32_t i = 0; i < size; i += 4) st32(at + i, 0);
		g_cleared = at;
		if (g_trace) rt_log("goldeneye: the crosshair's arrow cleared at %08X (%u bytes)", at, size);
	}
	else if (g_cleared)
	{
		//the file may have been loaded again since, with the game's pixels
		if (at == g_cleared && size == g_game_pixels.size() && Cleared(at, size))
		{
			for (uint32_t i = 0; i < size; ++i) st8(at + i, g_game_pixels[i]);
			if (g_trace) rt_log("goldeneye: the crosshair's arrow put back at %08X", at);
		}
		g_cleared = 0;
	}
}

void install_crosshair()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	g_dot_only = std::atoi(saved_setting("GE_DOT_CROSSHAIR", "LAUNCH_DOTCROSSHAIR").c_str()) != 0;
}

} //namespace ge
