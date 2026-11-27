//the loadout menu's picture of a weapon: the gun itself, turning round, in place of its picture. a
//custom weapon with a gun of its own shows that, the game's guns (and a custom weapon without one,
//its base) the gun a guard holds out of the disc's o_guns.edb
//
//the lit row's info, ge_loadout_info (80256710: r4 the menu, r5 the row's key), puts the picture's
//animation (the menu's +0x914, panel 0) on the frame of the loadout list's picture number * 16, and a
//custom weapon has its base's number. the 26 guns' pictures are textures of fe_main (kGunPictures,
//128 x 64 or 256 x 64, RGB5A3), given to GX by ge_texture_bind (802BE580: r3 the map, r4 the pixels,
//r5 and r6 the size, r7 the format, the caller's +8 the texture's texobj or 0 for one made from the
//rest; it keeps the pixels a map had last at 806185E8 and does nothing for the same again). the
//info only runs when the lit row changes, so from a custom weapon's row on a gun picture's pixels
//become ours, until another row is lit or the menu (the root menu the info gets, fn_80234780) is
//on another page
//
//ours: the gun file's whole gun, the far level of detail of its body that a guard holds (the row
//+0x10 lists' second entities, the biggest of them), drawn here on the cpu with its textures into
//a block of the game's heap. two of them in turn, so the renderer never reads one while it's written.
//a game's gun is o_guns' entity of its inventory id (the weapons table row's +0x174 by the key):
//the whole gun in one mesh, what that list's second entity of the body is (the P99's 0200016E).
//o_guns is read off the disc once, whole (its sections too), the first time one is lit
//
//a mesh as eurochef writes it (its ge/mesh.rs): +0x58 the strips, +0x5C the
//points (three floats and the normal as three bytes of 6 fraction bits), +0x60 the texture
//coordinates (two shorts, 16 - the top four bits of +0xB0 fraction bits), +0x64 the colours (0x80 is
//1), all from where they are; +0xA4 how many strips, +0xA8 points. a strip is 0x20 bytes (+0
//triangles, +2 the file's texture, +4 flags, +8 the display list's bytes) and its display list:
//triangle strips (0x98) of four shorts a corner, the point, its normal (the point again), colour,
//coordinate. a texture is a TPL from its +0x2C
//
//GE_TRACE=1 logs the gun each weapon's picture is made of
//GE_PREVIEWS=dir writes every custom gun's picture there as it starts, turned four ways (NAME_0.png...),
//and every gun of o_guns (by its id, 0200016E_0.png...)
#include "mods/weapons/weapon.h"
#include "gxtex.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

namespace ge
{

using namespace weapons;

namespace
{

const uint32_t kGunPictures[] = { 0x86000001, 0x86000002, 0x8600000C, 0x86000010, 0x86000012, 0x86000017, 0x86000045, 0x86000046, 0x86000054,
	0x86000055, 0x86000056, 0x86000058, 0x8600005E, 0x8600005F, 0x86000060, 0x86000063, 0x86000064, 0x86000067, 0x86000068, 0x86000069,
	0x8600006A, 0x8600006B, 0x8600006C, 0x8600006E, 0x8600006F, 0x86000070 };
const uint32_t kTexPixels = 0x80; //of a texture of the file in memory: its header, then the TPL's
const uint32_t kRgb5a3 = 5, kMostSide = 256, kMostLines = 64;
const uint32_t kAlloc = 0x803428C0; //(bytes, flags) -> a block of the game's heap
const uint32_t kMenuPage = 0x214;
const uint32_t kArgTexObj = 8; //of the caller's frame
const float kTurnSeconds = 6.0f;
const int kSamples = 2; //each pixel the average of 2 x 2

//the file's layout
const uint32_t kEntityList = 0x50, kTextureList = 0xB8, kListAt = 4, kEntityRow = 0x14, kTextureRow = 0x1C, kRowAddress = 8, kRowLods = 0x10;
const uint32_t kLodSize = 0x18;
const uint32_t kMeshStrips = 0x58, kMeshPoints = 0x5C, kMeshCoords = 0x60, kMeshColours = 0x64;
const uint32_t kMeshBox = 0x8C, kMeshStripCount = 0xA4, kMeshPointCount = 0xA8, kMeshFormat = 0xB0;
const uint32_t kStripHead = 0x20, kStripTexture = 2, kStripFlags = 4, kStripBytes = 8, kStripLightmap = 0x05;
const uint32_t kPointSize = 0x10;
const uint32_t kTexFrame = 0x2C, kTplImages = 8; //the TPL's table of images, a row the image's header and its palette
const uint32_t kMostPoints = 0x10000, kMostStrips = 4096;

struct Image { int w{}, h{}; std::vector<uint8_t> rgba; };
struct Corner { float pos[3]{}, normal[3]{}, uv[2]{}, colour[4]{}; };
struct Triangle { Corner c[3]; int texture{-1}; };

struct Gun
{
	bool tried{}, ok{};
	std::vector<Triangle> triangles;
	std::vector<Image> textures;
	float centre[3]{}, flat{}, high{}; //the middle of its box, how far it reaches round the upright axis, half its height
};

const uint32_t kGuns = 0x0109000F; //o_guns.edb
const uint32_t kRowInventory = 0x174; //of the weapons table's row
const uint64_t kCustom = 1ull << 32; //a gun: a custom weapon's own (kCustom + its number) or the game's (its inventory id)
const int64_t kNone = -1;

std::map<uint64_t, Gun> g_guns; //a gun once read stays as it is, the renderer's thread draws it too (loadout.cpp)
std::mutex g_gunsLock;
std::vector<uint8_t> g_gunsFile; //o_guns.edb
bool g_gunsRead = false;
PPCFunc orig_info = nullptr, orig_bind = nullptr, game_alloc = nullptr;
bool g_trace = false;
int64_t g_lit = kNone; //the gun of the lit row
uint32_t g_menu = 0, g_page = 0; //the menu it is lit in and its page then
uint32_t g_front = 0; //the front end's textures' file, as they were looked up
std::array<uint32_t, sizeof kGunPictures / sizeof kGunPictures[0]> g_pictures{}; //the gun pictures' pixels
uint32_t g_blocks[2]{}, g_drawnFrame = ~0u, g_drawnSize = 0;
int64_t g_drawnGun = kNone;
int g_block = 0;
const auto g_start = std::chrono::steady_clock::now();

struct File
{
	const std::vector<uint8_t>& d;
	bool In(size_t at, size_t n) const { return at <= d.size() && n <= d.size() - at; }
	uint32_t U32(size_t at) const { return In(at, 4) ? uint32_t(d[at]) << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3] : 0; }
	uint16_t U16(size_t at) const { return In(at, 2) ? uint16_t(d[at] << 8 | d[at + 1]) : 0; }
	float F32(size_t at) const { const uint32_t u = U32(at); float f; std::memcpy(&f, &u, 4); return f; }
	size_t Rel(size_t at) const { return at + (int32_t)U32(at); } //a pointer from where it is
};

Image ReadTexture(const File& f, size_t tex)
{
	Image image;
	const size_t tpl = f.Rel(tex + kTexFrame);
	const size_t head = tpl + f.U32(tpl + f.U32(tpl + kTplImages));
	const int h = f.U16(head), w = f.U16(head + 2);
	const uint32_t format = f.U32(head + 4);
	const size_t pixels = tpl + f.U32(head + 8);
	if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || !gxtex_known(format) || gxtex_is_ci(format) || !f.In(pixels, gxtex_size(format, w, h))) return image;
	image.w = w;
	image.h = h;
	image.rgba.resize(size_t(w) * h * 4);
	gxtex_decode(format, w, h, &f.d[pixels], image.rgba.data());
	return image;
}

//the mesh to draw: the biggest far level of detail (the body's is the whole gun, the others are parts
//of it), else the biggest mesh. a mesh's box is at +0x8C and +0x98
float Size(const File& f, size_t mesh)
{
	if (f.U32(mesh) != 0x601 || f.U32(mesh + kMeshPointCount) >= kMostPoints) return -1;
	float sum = 0;
	for (int k = 0; k < 3; ++k)
	{
		const float side = f.F32(mesh + kMeshBox + 12 + k * 4) - f.F32(mesh + kMeshBox + k * 4);
		sum += side * side;
	}
	return std::isfinite(sum) ? sum : -1;
}

size_t PickMesh(const File& f)
{
	const uint32_t count = f.U16(kEntityList);
	const size_t rows = f.Rel(kEntityList + kListAt);
	size_t best = 0, bestLod = 0;
	float biggest = 0, biggestLod = 0;
	for (uint32_t i = 0; i < count && i < 256; ++i)
	{
		const size_t row = rows + i * kEntityRow, mesh = f.U32(row + kRowAddress);
		if (const float size = Size(f, mesh); size > biggest) { biggest = size; best = mesh; }
		const uint32_t word = f.U32(row + kRowLods);
		if (!(word >> 8)) continue;
		const size_t second = row + kRowLods + ((int32_t)word >> 8) + kLodSize;
		if (!f.In(second, 4) || !f.U32(second)) continue;
		const size_t lod = f.U32(f.Rel(second) + kRowAddress);
		if (const float size = Size(f, lod); size > biggestLod) { biggestLod = size; bestLod = lod; }
	}
	return bestLod ? bestLod : best;
}

//the entity of that hash, 0 when there's none
size_t EntityOf(const File& f, uint32_t hash)
{
	const uint32_t count = f.U16(kEntityList);
	const size_t rows = f.Rel(kEntityList + kListAt);
	for (uint32_t i = 0; i < count && i < 4096; ++i)
		if (f.U32(rows + i * kEntityRow) == hash) return Size(f, f.U32(rows + i * kEntityRow + kRowAddress)) >= 0 ? f.U32(rows + i * kEntityRow + kRowAddress) : 0;
	return 0;
}

//the gun: the file's whole gun, or its entity of that hash. only the textures it uses are read
bool ReadGun(const std::vector<uint8_t>& d, Gun& gun, uint32_t entity = 0)
{
	const File f{ d };
	if (f.U32(0) != 0x47454F4D) return false;
	const uint32_t textureCount = std::min<uint32_t>(f.U16(kTextureList), 4096);
	const size_t textureRows = f.Rel(kTextureList + kListAt);
	gun.textures.assign(textureCount, Image{});
	std::vector<bool> textureRead(textureCount);
	const size_t mesh = entity ? EntityOf(f, entity) : PickMesh(f);
	if (!mesh) return false;
	const size_t points = f.Rel(mesh + kMeshPoints), coords = f.Rel(mesh + kMeshCoords), colours = f.Rel(mesh + kMeshColours);
	const uint32_t pointCount = f.U32(mesh + kMeshPointCount), strips = f.U32(mesh + kMeshStripCount);
	const float uvScale = 1.0f / float(1u << (16 - std::min(f.U32(mesh + kMeshFormat) >> 28, 15u)));
	auto corner = [&](size_t at, Corner& c)
	{
		const uint16_t point = f.U16(at), colour = f.U16(at + 4), coord = f.U16(at + 6);
		if (point >= pointCount) return false;
		const size_t p = points + point * kPointSize;
		for (int k = 0; k < 3; ++k)
		{
			c.pos[k] = f.F32(p + k * 4);
			c.normal[k] = f.In(p + 12 + k, 1) ? (int8_t)d[p + 12 + k] / 64.0f : 0.0f;
		}
		c.uv[0] = (int16_t)f.U16(coords + coord * 4) * uvScale;
		c.uv[1] = (int16_t)f.U16(coords + coord * 4 + 2) * uvScale;
		for (int k = 0; k < 4; ++k) c.colour[k] = f.In(colours + colour * 4 + k, 1) ? d[colours + colour * 4 + k] / 128.0f : 1.0f;
		return true;
	};
	size_t strip = f.Rel(mesh + kMeshStrips);
	for (uint32_t s = 0; s < strips && s < kMostStrips; ++s)
	{
		const uint32_t bytes = f.U32(strip + kStripBytes);
		const int texture = f.U16(strip + kStripTexture);
		if (texture < (int)textureCount && !textureRead[texture])
		{
			textureRead[texture] = true;
			gun.textures[texture] = ReadTexture(f, f.U32(textureRows + texture * kTextureRow + kRowAddress));
		}
		const bool lightmap = (f.U16(strip + kStripFlags) & kStripLightmap) == kStripLightmap;
		size_t at = strip + kStripHead;
		const size_t end = at + bytes;
		if (!f.In(at, bytes)) break;
		while (at + 3 <= end)
		{
			const uint8_t op = d[at];
			if (op == 0) { ++at; continue; }
			const uint16_t count = f.U16(at + 1);
			at += 3;
			if ((op & 0xF8) != 0x98 && (op & 0xF8) != 0x90 && (op & 0xF8) != 0xA0) break; //not a strip, a list or a fan: the rest can't be read
			std::vector<Corner> corners(count);
			bool ok = at + count * 8u <= end;
			for (uint16_t i = 0; ok && i < count; ++i) ok = corner(at + i * 8u, corners[i]);
			at += count * 8u;
			if (!ok || lightmap) continue;
			for (uint16_t i = 2; i < count; ++i)
			{
				Triangle t;
				t.texture = texture < (int)gun.textures.size() && !gun.textures[texture].rgba.empty() ? texture : -1;
				if ((op & 0xF8) == 0x98) { t.c[0] = corners[i - 2]; t.c[1] = corners[i - 1]; t.c[2] = corners[i]; }
				else if ((op & 0xF8) == 0xA0) { t.c[0] = corners[0]; t.c[1] = corners[i - 1]; t.c[2] = corners[i]; }
				else if (i % 3 == 2) { t.c[0] = corners[i - 2]; t.c[1] = corners[i - 1]; t.c[2] = corners[i]; }
				else continue;
				gun.triangles.push_back(t);
			}
		}
		strip = end;
	}
	if (gun.triangles.empty()) return false;
	float low[3]{ 1e9f, 1e9f, 1e9f }, high[3]{ -1e9f, -1e9f, -1e9f };
	for (const Triangle& t : gun.triangles)
		for (const Corner& c : t.c)
			for (int k = 0; k < 3; ++k) { low[k] = std::min(low[k], c.pos[k]); high[k] = std::max(high[k], c.pos[k]); }
	for (int k = 0; k < 3; ++k) gun.centre[k] = (low[k] + high[k]) / 2;
	for (const Triangle& t : gun.triangles)
		for (const Corner& c : t.c)
			gun.flat = std::max(gun.flat, std::hypot(c.pos[0] - gun.centre[0], c.pos[2] - gun.centre[2]));
	gun.high = (high[1] - low[1]) / 2;
	return gun.flat > 0;
}

const std::vector<uint8_t>& GunsFile()
{
	if (!g_gunsRead)
	{
		g_gunsRead = true;
		if (!disc_game_file(kGuns, g_gunsFile)) rt_log("goldeneye: weapons: o_guns.edb couldn't be read off the disc, the loadout menu shows the guns' pictures");
	}
	return g_gunsFile;
}

Gun& GunOf(uint64_t which)
{
	std::lock_guard<std::mutex> hold(g_gunsLock);
	Gun& gun = g_guns[which];
	if (gun.tried) return gun;
	gun.tried = true;
	if (which < kCustom)
	{
		gun.ok = ReadGun(GunsFile(), gun, uint32_t(which));
		if (!gun.ok) rt_log("goldeneye: weapons: o_guns has no gun %08X to draw for the loadout menu, it shows the picture", uint32_t(which));
		else if (g_trace) rt_log("goldeneye: weapons: gun %08X's picture, %zu triangles", uint32_t(which), gun.triangles.size());
		return gun;
	}
	const Weapon& w = g_weapons[size_t(which - kCustom)];
	std::ifstream in(std::filesystem::path(modpack_folder(w.pack)) / "weapons" / w.model, std::ios::binary);
	const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	gun.ok = ReadGun(d, gun);
	if (!gun.ok) rt_log("goldeneye: weapons: %s's gun can't be drawn for the loadout menu, it shows its base's picture", w.name.c_str());
	else if (g_trace) rt_log("goldeneye: weapons: %s's picture is %s, %zu triangles", w.name.c_str(), w.model.c_str(), gun.triangles.size());
	return gun;
}

//the inventory id of a weapon's key in the weapons table, 0 when it isn't there
uint32_t IdOfKey(uint32_t key)
{
	const uint32_t count = ld32(kWeapons + 8), rows = ld32(kWeapons + 0xC);
	for (uint32_t i = 0; guest_ptr(rows) && i < count && i < 512; ++i)
		if (ld32(rows + i * kRowSize + kRowKey) == key) return ld32(rows + i * kRowSize + kRowInventory);
	return 0;
}

//the gun a row's key shows
int64_t GunOfKey(uint32_t key)
{
	for (size_t i = 0; i < g_weapons.size(); ++i)
		if (g_weapons[i].key == key && Installed(g_weapons[i]))
			return g_weapons[i].modelFile ? int64_t(kCustom + i) : g_weapons[i].baseId ? int64_t(g_weapons[i].baseId) : kNone;
	const uint32_t id = IdOfKey(key);
	return id ? int64_t(id) : kNone;
}

void Sample(const Image& image, float u, float v, float out[4])
{
	u -= std::floor(u);
	v -= std::floor(v);
	const int x = std::min(int(u * image.w), image.w - 1), y = std::min(int(v * image.h), image.h - 1);
	const uint8_t* p = &image.rgba[(size_t(y) * image.w + x) * 4];
	for (int k = 0; k < 4; ++k) out[k] = p[k] / 255.0f;
}

//the gun turned by angle round the upright axis, seen from the side, w x h, RGBA
void Draw(const Gun& gun, float angle, int w, int h, std::vector<uint8_t>& out)
{
	const int bw = w * kSamples, bh = h * kSamples;
	std::vector<float> colour(size_t(bw) * bh * 4, 0.0f), depth(size_t(bw) * bh, 1e30f);
	const float ca = std::cos(angle), sa = std::sin(angle);
	//fitted to fit whichever way round it is, a little room at the sides
	const float scale = std::min(bw * 0.44f / gun.flat, gun.high > 0 ? bh * 0.38f / gun.high : 1e9f);
	const float light[3]{ -0.35f, 0.55f, 0.76f }; //towards the light: up, left and in front
	struct Screen { float x, y, z, n[3]; };
	for (const Triangle& t : gun.triangles)
	{
		Screen s[3];
		for (int i = 0; i < 3; ++i)
		{
			const float x = t.c[i].pos[0] - gun.centre[0], y = t.c[i].pos[1] - gun.centre[1], z = t.c[i].pos[2] - gun.centre[2];
			const float rx = x * ca - z * sa, rz = x * sa + z * ca; //turned
			s[i].x = bw * 0.5f + rz * scale; //the gun's forward (z) to the right
			s[i].y = bh * 0.5f - y * scale;
			s[i].z = rx;
			const float* n = t.c[i].normal;
			s[i].n[0] = n[0] * sa + n[2] * ca; //in the picture's axes: right, up, towards us
			s[i].n[1] = n[1];
			s[i].n[2] = n[2] * sa - n[0] * ca;
		}
		const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y);
		if (std::fabs(area) < 1e-6f) continue;
		const int x0 = std::max(0, (int)std::floor(std::min({ s[0].x, s[1].x, s[2].x }))), x1 = std::min(bw - 1, (int)std::ceil(std::max({ s[0].x, s[1].x, s[2].x })));
		const int y0 = std::max(0, (int)std::floor(std::min({ s[0].y, s[1].y, s[2].y }))), y1 = std::min(bh - 1, (int)std::ceil(std::max({ s[0].y, s[1].y, s[2].y })));
		const Image* image = t.texture >= 0 ? &gun.textures[t.texture] : nullptr;
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
			{
				const float px = x + 0.5f, py = y + 0.5f;
				const float b0 = ((s[1].x - px) * (s[2].y - py) - (s[2].x - px) * (s[1].y - py)) / area;
				const float b1 = ((s[2].x - px) * (s[0].y - py) - (s[0].x - px) * (s[2].y - py)) / area;
				const float b2 = 1.0f - b0 - b1;
				if (b0 < 0 || b1 < 0 || b2 < 0) continue;
				const float z = b0 * s[0].z + b1 * s[1].z + b2 * s[2].z; //smaller is nearer: we look along +x turned
				float& at = depth[size_t(y) * bw + x];
				if (z >= at) continue;
				at = z;
				float n[3], len = 0;
				for (int k = 0; k < 3; ++k) { n[k] = b0 * s[0].n[k] + b1 * s[1].n[k] + b2 * s[2].n[k]; len += n[k] * n[k]; }
				len = len > 1e-12f ? 1.0f / std::sqrt(len) : 0.0f;
				const float lit = 0.45f + 0.75f * std::fabs(n[0] * len * light[0] + n[1] * len * light[1] + n[2] * len * light[2]);
				float texel[4]{ 1, 1, 1, 1 };
				if (image)
					Sample(*image, b0 * t.c[0].uv[0] + b1 * t.c[1].uv[0] + b2 * t.c[2].uv[0], b0 * t.c[0].uv[1] + b1 * t.c[1].uv[1] + b2 * t.c[2].uv[1], texel);
				float* c = &colour[(size_t(y) * bw + x) * 4];
				for (int k = 0; k < 3; ++k)
					c[k] = std::min(1.0f, texel[k] * (b0 * t.c[0].colour[k] + b1 * t.c[1].colour[k] + b2 * t.c[2].colour[k]) * lit);
				c[3] = 1.0f;
			}
	}
	out.assign(size_t(w) * h * 4, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			float sum[4]{};
			for (int sy = 0; sy < kSamples; ++sy)
				for (int sx = 0; sx < kSamples; ++sx)
				{
					const float* c = &colour[(size_t(y * kSamples + sy) * bw + x * kSamples + sx) * 4];
					for (int k = 0; k < 3; ++k) sum[k] += c[k] * c[3];
					sum[3] += c[3];
				}
			uint8_t* p = &out[(size_t(y) * w + x) * 4];
			for (int k = 0; k < 3; ++k) p[k] = sum[3] > 0 ? uint8_t(std::lround(sum[k] / sum[3] * 255)) : 0;
			p[3] = uint8_t(std::lround(sum[3] / (kSamples * kSamples) * 255));
		}
}

//RGB5A3 in 4 x 4 tiles: opaque 1rrrrrgggggbbbbb, else 0aaarrrrggggbbbb
void Rgb5a3(const std::vector<uint8_t>& rgba, int w, int h, uint32_t to)
{
	uint32_t at = to;
	for (int ty = 0; ty < h; ty += 4)
		for (int tx = 0; tx < w; tx += 4)
			for (int y = ty; y < ty + 4; ++y)
				for (int x = tx; x < tx + 4; ++x, at += 2)
				{
					const uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
					const uint16_t v = p[3] >= 0xF0 ? uint16_t(0x8000 | (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | p[2] >> 3)
						: uint16_t((p[3] >> 5) << 12 | (p[0] >> 4) << 8 | (p[1] >> 4) << 4 | p[2] >> 4);
					st16(at, v);
				}
}

uint32_t Alloc(const PPCContext& c, uint32_t bytes)
{
	PPCContext call = c;
	const uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	call.r[3] = bytes;
	call.r[4] = 0;
	game_alloc(call);
	return guest_ptr(call.r[3]) ? call.r[3] : 0;
}

bool IsGunPicture(uint32_t pixels)
{
	const uint32_t front = front_end_file();
	if (front != g_front)
	{
		g_front = front;
		for (size_t i = 0; i < g_pictures.size(); ++i)
		{
			uint32_t sides = 0;
			const uint32_t texture = front_end_texture(kGunPictures[i], sides);
			g_pictures[i] = texture ? texture + kTexPixels : 0;
		}
	}
	return pixels && std::find(g_pictures.begin(), g_pictures.end(), pixels) != g_pictures.end();
}

void Info(PPCContext& c)
{
	const uint32_t key = c.r[5];
	const int64_t was = g_lit;
	g_lit = GunOfKey(key);
	if (g_trace && g_lit != was) rt_log("goldeneye: weapons: the loadout menu's lit row is %08X, its gun %llX", key, (long long)g_lit);
	g_menu = c.r[4];
	g_page = guest_ptr(g_menu) ? ld32(g_menu + kMenuPage) : 0;
	if (g_lit >= 0 && !g_blocks[0])
		for (uint32_t& block : g_blocks)
			if (const uint32_t at = Alloc(c, kMostSide * kMostLines * 2 + 32)) block = (at + 31) & ~31u;
	const weapons::ListLend lend(key);
	orig_info(c);
}

void Bind(PPCContext& c)
{
	const uint32_t now = ld32(kFrameCount), w = c.r[5], h = c.r[6];
	if (g_lit >= 0 && guest_ptr(g_menu) && ld32(g_menu + kMenuPage) == g_page && !guest_ptr(ld32(kPlayerPtr)) && g_blocks[0] && g_blocks[1] && c.r[7] == kRgb5a3 && w <= kMostSide && h <= kMostLines && w % 4 == 0 && h % 4 == 0 && IsGunPicture(c.r[4]))
	{
		Gun& gun = GunOf(uint64_t(g_lit));
		if (gun.ok)
		{
			if (now != g_drawnFrame || g_lit != g_drawnGun || (w << 16 | h) != g_drawnSize)
			{
				const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_start).count();
				std::vector<uint8_t> rgba;
				Draw(gun, seconds / kTurnSeconds * 6.2831853f, (int)w, (int)h, rgba);
				g_block ^= 1;
				Rgb5a3(rgba, (int)w, (int)h, g_blocks[g_block]);
				g_drawnFrame = now;
				g_drawnGun = g_lit;
				g_drawnSize = w << 16 | h;
			}
			//the texture's own texobj (the 9th argument, the caller's +8) is made again for ours
			const uint32_t stack = c.r[1], frame = stack - 0x20;
			st32(frame, stack);
			st32(frame + kArgTexObj, 0);
			c.r[1] = frame;
			c.r[4] = g_blocks[g_block];
			orig_bind(c);
			c.r[1] = stack;
			return;
		}
	}
	orig_bind(c);
}

} //namespace

int64_t weapon_gun_of_key(uint32_t key) { return GunOfKey(key); }

bool weapon_picture(int64_t gun, float angle, int w, int h, std::vector<uint8_t>& rgba)
{
	if (gun < 0) return false;
	const Gun& drawn = GunOf(uint64_t(gun));
	if (!drawn.ok) return false;
	Draw(drawn, angle, w, h, rgba);
	return true;
}

void install_weapon_previews()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	game_alloc = ppc_lookup(kAlloc);
	orig_info = ppc_hook("ge_loadout_info", Info);
	orig_bind = ppc_hook("ge_texture_bind", Bind);
	if (const char* dir = std::getenv("GE_PREVIEWS"))
	{
		auto dump = [&](uint64_t which, const std::string& name)
		{
			if (!GunOf(which).ok) return;
			for (int turn = 0; turn < 4; ++turn)
			{
				std::vector<uint8_t> rgba;
				Draw(GunOf(which), turn * 6.2831853f / 8, 256, 64, rgba);
				write_png((std::filesystem::path(dir) / (name + "_" + std::to_string(turn) + ".png")).string(), 256, 64, rgba.data());
			}
		};
		for (size_t n = 0; n < g_weapons.size(); ++n)
			if (g_weapons[n].modelFile) dump(kCustom + n, std::filesystem::path(g_weapons[n].file).stem().string() + "_" + std::to_string(g_weapons[n].pack));
		const File f{ GunsFile() };
		const uint32_t count = f.U16(kEntityList);
		const size_t rows = f.Rel(kEntityList + kListAt);
		for (uint32_t i = 0; i < count && i < 4096; ++i)
			if (const uint32_t hash = f.U32(rows + i * kEntityRow); hash >> 16 == 0x0200)
			{
				char name[16];
				std::snprintf(name, sizeof name, "%08X", hash);
				dump(hash, name);
			}
	}
	if (!game_alloc || !orig_info || !orig_bind) rt_log("goldeneye: ge_loadout_info or ge_texture_bind is not hooked in this build, the loadout menu shows the guns' pictures");
}

} //namespace ge
