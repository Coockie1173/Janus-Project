//a mod level's picture: mt_yard.png next to mt_yard.edb is shown where the game shows a map's
//picture (a level select's panel, the two maps of a network game's lobby)
//
//the game's pictures are textures of the front end's file (01080004), 256 x 256 CMPR, and one
//animation a panel that has a picture every five frames: a panel is put on frame number * 5
//the numbers: 0 to 13 the missions (0, 1, 7, 8, 9, 11 and 13 are multiplayer's maps too), 14 to
//16 three more maps in the lobby's two animations, 40 for a level that has none: the grey panel
//a level select (ge_level_picture) finds the number by the lit level's hash, the lobby
//(ge_lobby_picture) by the map's name
//
//a mod's picture is lent the texture of a mission that is no multiplayer map (kLenders): its
//pixels are written over that texture and the panel is put on that mission's number. the
//mission gets its pixels back as soon as the game asks for its number itself. seven of them,
//and no more than three panels are ever up, so every mod on screen has one
//
//the .png is cut to the panel's shape around its middle (it is shown 0.85 times as wide as
//high) and made 256 x 256
#include "ge.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace ge
{
namespace
{

const uint32_t kFrontEnd = 0x01080004, kGeom = 0x47454F4D; //fe_main.edb
const uint32_t kFileBase = 0x14, kFileHash = 0x20; //of the game's object for a file: where it is once it's in
const uint32_t kTextureList = 0xB8; //of the file: how many (a short), then where the entries are from here + 4
const uint32_t kEntrySize = 0x1C, kEntryHash = 0x0, kEntryPlace = 0xC, kEntrySides = 0x10; //the place is an address once the file is in
const uint32_t kMostTextures = 4096;
const uint32_t kTexLevels = 0x12, kTexFormat = 0x13, kTexPixels = 0x80; //of a texture: mip levels less one, 0 for CMPR
const int kSide = 256;
const uint32_t kBytes = kSide * kSide / 2;
const float kShownAspect = 0.85f; //the panel's width over its height

const uint32_t kNoPicture = 40, kFramesAPicture = 5;
struct Lender { uint32_t number, texture; }; //a mission's number, its texture's hash in the file
const Lender kLenders[] = { {2, 0x86000049}, {3, 0x8600004A}, {4, 0x8600004B}, {5, 0x8600004C}, {6, 0x8600004D}, {10, 0x86000051}, {12, 0x86000052} };
const size_t kLenderCount = sizeof kLenders / sizeof kLenders[0];

const uint32_t kNumbers = 0x8054C998, kNumberCount = 34; //a level's hash and its number
const uint32_t kMissionSelect = 0x7D0000F3; //the lit item is the number there
const uint32_t kMenuPage = 0x214, kMenuLit = 0x220, kMenuRows = 0x2C8, kMenuPanels = 0x914;
const uint32_t kRowHash = 0x4, kRowKind = 0xC, kKindLevel = 0xB;
const uint32_t kSetFrame = 0x8023F8B0; //(menu + 0x914, which panel, a frame)

struct Picture
{
	uint32_t hash{};
	std::string name, shown; //its file, and its name in the menus in small letters
	std::vector<uint8_t> pixels; //CMPR, as the texture has them
};

struct Loan
{
	const Picture* pPicture{};
	std::vector<uint8_t> own; //the mission's pixels
	uint32_t used{}; //the frame it was asked for last, by a mod or by the game
};

std::vector<Picture> g_pictures;
Loan g_loans[kLenderCount];
uint32_t g_front_end = 0; //the game's object for the file
PPCFunc orig_level_picture = nullptr, orig_lobby_picture = nullptr, set_frame = nullptr;
bool g_trace = false;

//a 4 x 4 block of CMPR: the two colours furthest apart along the block's widest channel, and
//for each pixel the nearest of them and the two between
void Block(const uint8_t* rgba, int stride, uint8_t* out)
{
	int low[3] = { 255, 255, 255 }, high[3] = { 0, 0, 0 };
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
			for (int ch = 0; ch < 3; ++ch)
			{
				const int v = rgba[y * stride + x * 4 + ch];
				low[ch] = std::min(low[ch], v); high[ch] = std::max(high[ch], v);
			}
	auto pack = [](const int* colour) { return uint16_t((colour[0] >> 3) << 11 | (colour[1] >> 2) << 5 | colour[2] >> 3); };
	uint16_t first = pack(high), second = pack(low);
	if (first < second) std::swap(first, second);
	if (first == second) //one colour: both the same, every pixel the first
	{
		out[0] = out[2] = uint8_t(first >> 8); out[1] = out[3] = uint8_t(first);
		out[4] = out[5] = out[6] = out[7] = 0;
		return;
	}
	int palette[4][3];
	auto unpack = [](uint16_t packed, int* colour)
	{
		colour[0] = (packed >> 11) * 255 / 31; colour[1] = (packed >> 5 & 63) * 255 / 63; colour[2] = (packed & 31) * 255 / 31;
	};
	unpack(first, palette[0]); unpack(second, palette[1]);
	for (int ch = 0; ch < 3; ++ch)
	{
		palette[2][ch] = (palette[0][ch] * 2 + palette[1][ch]) / 3;
		palette[3][ch] = (palette[0][ch] + palette[1][ch] * 2) / 3;
	}
	out[0] = uint8_t(first >> 8); out[1] = uint8_t(first); out[2] = uint8_t(second >> 8); out[3] = uint8_t(second);
	for (int y = 0; y < 4; ++y)
	{
		uint8_t row = 0;
		for (int x = 0; x < 4; ++x)
		{
			const uint8_t* pixel = rgba + y * stride + x * 4;
			int best = 0, bestError = INT32_MAX;
			for (int i = 0; i < 4; ++i)
			{
				const int r = pixel[0] - palette[i][0], g = pixel[1] - palette[i][1], b = pixel[2] - palette[i][2];
				const int error = r * r * 2 + g * g * 4 + b * b;
				if (error < bestError) { bestError = error; best = i; }
			}
			row = uint8_t(row << 2 | best);
		}
		out[4 + y] = row;
	}
}

//CMPR: 8 x 8 tiles of four blocks each
std::vector<uint8_t> Cmpr(const std::vector<uint8_t>& rgba)
{
	std::vector<uint8_t> out(kBytes);
	uint8_t* at = out.data();
	for (int ty = 0; ty < kSide; ty += 8)
		for (int tx = 0; tx < kSide; tx += 8)
			for (int b = 0; b < 4; ++b, at += 8)
				Block(&rgba[((ty + (b >> 1) * 4) * kSide + tx + (b & 1) * 4) * 4], kSide * 4, at);
	return out;
}

//the .png's middle in the panel's shape, each of the 256 x 256 pixels the average of what it covers
bool ReadPng(const std::string& path, std::vector<uint8_t>& rgba)
{
	SDL_Surface* file = SDL_LoadPNG(path.c_str());
	if (!file) return false;
	SDL_Surface* image = SDL_ConvertSurface(file, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(file);
	if (!image) return false;
	const bool ok = image->w > 0 && image->h > 0 && SDL_LockSurface(image);
	if (ok)
	{
		double w = image->w, h = image->h;
		if (w > h * kShownAspect) w = h * kShownAspect; else h = w / kShownAspect;
		const double left = (image->w - w) / 2, top = (image->h - h) / 2;
		const uint8_t* pixels = (const uint8_t*)image->pixels;
		rgba.resize(size_t(kSide) * kSide * 4);
		for (int y = 0; y < kSide; ++y)
			for (int x = 0; x < kSide; ++x)
			{
				const int x0 = std::clamp(int(left + x * w / kSide), 0, image->w - 1), y0 = std::clamp(int(top + y * h / kSide), 0, image->h - 1);
				const int x1 = std::clamp(int(std::ceil(left + (x + 1) * w / kSide)), x0 + 1, image->w), y1 = std::clamp(int(std::ceil(top + (y + 1) * h / kSide)), y0 + 1, image->h);
				uint32_t sum[3] = {};
				for (int sy = y0; sy < y1; ++sy)
					for (int sx = x0; sx < x1; ++sx)
						for (int ch = 0; ch < 3; ++ch) sum[ch] += pixels[size_t(sy) * image->pitch + sx * 4 + ch];
				const uint32_t count = uint32_t(x1 - x0) * uint32_t(y1 - y0);
				for (int ch = 0; ch < 3; ++ch) rgba[(size_t(y) * kSide + x) * 4 + ch] = uint8_t(sum[ch] / count);
				rgba[(size_t(y) * kSide + x) * 4 + 3] = 255;
			}
		SDL_UnlockSurface(image);
	}
	SDL_DestroySurface(image);
	return ok;
}

//where a lender's pixels are, 0 while the front end's file isn't in or isn't what it was
uint32_t PixelsOf(const Lender& lender)
{
	const uint32_t file = guest_ptr(g_front_end) && ld32(g_front_end + kFileHash) == kFrontEnd ? ld32(g_front_end + kFileBase) : 0;
	if (!guest_ptr(file) || ld32(file) != kGeom || ld32(file + 4) != kFrontEnd) return 0;
	const uint32_t count = ld16(file + kTextureList), entries = file + kTextureList + 4 + ld32(file + kTextureList + 4);
	if (count > kMostTextures || !guest_ptr(entries) || !guest_ptr(entries + count * kEntrySize)) return 0;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t entry = entries + i * kEntrySize;
		if (ld32(entry + kEntryHash) != lender.texture) continue;
		const uint32_t texture = ld32(entry + kEntryPlace);
		if (!guest_ptr(texture) || !guest_ptr(texture + kTexPixels + kBytes)) return 0;
		if (ld32(entry + kEntrySides) != (uint32_t(kSide) << 16 | kSide) || ld8(texture + kTexLevels) != 0 || ld8(texture + kTexFormat) != 0) return 0;
		return texture + kTexPixels;
	}
	return 0;
}

bool Holds(uint32_t pixels, const std::vector<uint8_t>& bytes) { return std::memcmp(PPC_MEM(pixels), bytes.data(), kBytes) == 0; }

//the game asks for a number itself: a lender shows its own picture again
void Asked(uint32_t number)
{
	for (size_t i = 0; i < kLenderCount; ++i)
	{
		if (kLenders[i].number != number) continue;
		Loan& loan = g_loans[i];
		loan.used = ld32(kFrameCount);
		if (!loan.pPicture) return;
		const uint32_t pixels = PixelsOf(kLenders[i]);
		//a file that was loaded again has the mission's pixels already
		if (pixels && Holds(pixels, loan.pPicture->pixels)) std::memcpy(PPC_MEM(pixels), loan.own.data(), kBytes);
		if (g_trace) rt_log("goldeneye: picture %u is the game's again (was %s's)", number, loan.pPicture->name.c_str());
		loan.pPicture = nullptr;
		return;
	}
}

//the number that shows a mod's picture, kNoPicture when it can't be shown
uint32_t Lend(const Picture& picture)
{
	const uint32_t now = ld32(kFrameCount);
	size_t pick = kLenderCount;
	for (size_t i = 0; i < kLenderCount; ++i)
		if (g_loans[i].pPicture == &picture) pick = i;
	if (pick == kLenderCount)
	{
		//the one nobody asked for the longest
		uint32_t longest = 0;
		for (size_t i = 0; i < kLenderCount; ++i)
		{
			const uint32_t since = now - g_loans[i].used;
			if (pick == kLenderCount || since > longest) { pick = i; longest = since; }
		}
	}
	Loan& loan = g_loans[pick];
	const uint32_t pixels = PixelsOf(kLenders[pick]);
	if (!pixels) return kNoPicture;
	if (!Holds(pixels, picture.pixels))
	{
		//whatever is there that isn't the mod it had is the mission's own
		if (!loan.pPicture || !Holds(pixels, loan.pPicture->pixels)) loan.own.assign(PPC_MEM(pixels), PPC_MEM(pixels) + kBytes);
		std::memcpy(PPC_MEM(pixels), picture.pixels.data(), kBytes);
		if (g_trace) rt_log("goldeneye: picture %u shows %s", kLenders[pick].number, picture.name.c_str());
	}
	loan.pPicture = &picture;
	loan.used = now;
	return kLenders[pick].number;
}

void LevelPicture(PPCContext& c)
{
	const uint32_t menu = c.r[4];
	orig_level_picture(c);
	if (g_pictures.empty() || !guest_ptr(menu)) return;
	const uint32_t lit = ld32(menu + kMenuLit);
	const uint32_t row = lit < 4096 ? ld32(menu + kMenuRows + lit * 4) : 0;
	const uint32_t level = guest_ptr(row) && ld16(row + kRowKind) == kKindLevel ? ld32(row + kRowHash) : 0;
	auto picture = std::find_if(g_pictures.begin(), g_pictures.end(), [&](const Picture& p) { return p.hash == level; });
	if (picture == g_pictures.end() || !set_frame)
	{
		//what the game just looked up
		uint32_t number = kNoPicture;
		if (ld32(menu + kMenuPage) == kMissionSelect) number = lit;
		else
			for (uint32_t i = 0; i < kNumberCount; ++i)
				if (ld32(kNumbers + i * 8) == level) { number = ld32(kNumbers + i * 8 + 4); break; }
		Asked(number);
		return;
	}
	PPCContext call = c;
	const uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	call.r[3] = menu + kMenuPanels; call.r[4] = 0; call.r[5] = Lend(*picture) * kFramesAPicture;
	set_frame(call);
}

void LobbyPicture(PPCContext& c)
{
	const uint32_t text = guest_ptr(c.r[4]) ? ld32(c.r[4]) : 0;
	orig_lobby_picture(c);
	if (g_pictures.empty()) return;
	//the name's characters, their count in the short four before them
	std::string name;
	if (guest_ptr(text))
		for (uint32_t i = 0, count = ld16(text - 4); i < count && i < 256; ++i) name.push_back(char(ld16(text + i * 2)));
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
	auto picture = std::find_if(g_pictures.begin(), g_pictures.end(), [&](const Picture& p) { return p.shown == name; });
	if (c.r[3] != kNoPicture || picture == g_pictures.end()) { Asked(c.r[3]); return; }
	c.r[3] = Lend(*picture);
}

} //namespace

void pictures_file(uint32_t hash, uint32_t file)
{
	if (hash == kFrontEnd) g_front_end = file;
}

void install_pictures()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	for (const ModLevel& mod : mod_files())
	{
		const std::string path = (std::filesystem::path(mods_folder()) / std::filesystem::path(mod.name).replace_extension(".png")).string();
		std::error_code error;
		if (!std::filesystem::exists(path, error)) continue;
		std::vector<uint8_t> rgba;
		if (!ReadPng(path, rgba)) { rt_log("goldeneye: mods: %s isn't a .png that can be read, %s has no picture", path.c_str(), mod.name.c_str()); continue; }
		std::string shown = mod_shown_name(mod.name);
		std::transform(shown.begin(), shown.end(), shown.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
		g_pictures.push_back({ mod.hash, mod.name, shown, Cmpr(rgba) });
		rt_log("goldeneye: mod %s has a picture", mod.name.c_str());
	}
	if (g_pictures.empty()) return;
	orig_level_picture = ppc_hook("ge_level_picture", LevelPicture);
	orig_lobby_picture = ppc_hook("ge_lobby_picture", LobbyPicture);
	set_frame = ppc_lookup(kSetFrame);
}

} //namespace ge
