//what the chosen loadout has, beside the menus that choose one: the "Game starting in..." page of a
//network game and its pause menu, both with a Loadout row whose left and right go through the ten
//loadouts. a caption centred under the menu says what it has, in the menus' font: the primary and
//the secondary turning (preview.cpp draws them), the guns with their attachments, then the gadgets
//
//the game's own pictures of a loadout are left out: ge_loadout_scene (8023B090, the menu's update
//on pages 7D0000BD, 7D00004B and 7D000101) puts a scene of geometry 01090053 in panel 0 of the
//menu's +0x914, the script by the loadout's number from the table at 8057E108 (the presets' guns
//and grenades, nothing for a custom one). after it the panel is hidden (its byte +0x1129, a panel
//0x14 bytes, what fn_8023F860 sets)
//
//the Loadout row is a choice row (kind 3) of setting 7D01003D: ge_choice_step (8022D000) puts the
//loadout's number, 0 to 9, at the profile's +0x404 (profile 805DE6A0 + the pad * 0x590) and its
//hash at +0x40C, out of the table at 8057DA78 (eight bytes a loadout: its name's text, its hash),
//and ge_choice_text answers the name by the number. five presets, then Custom 1..5 (7D09001A..1E).
//a custom one's items are the profile's: eight lists of five words from +0x230, one word a custom
//loadout (primary, secondary, three gadgets, the primary's and the secondary's attachment, one
//more 7D08xxxx), which the step also copies to +0x410. a preset's are a row of the loadouts' sheet
//(805D18E0: +8 how many, +0xC the rows, 0x28 bytes each sorted by hash: the hash, its name's text,
//then the same eight words, FFFFFFFF for a gadget it hasn't). a key's name is in the loadout
//menu's list at 80585C80 (key, name's text, ...), attachments (7D07xxxx, 7D070001 none) and
//gadgets (7D06xxxx) too
//
//the menu's update (WIFI_FUN_80234a60, cutscenes.cpp hooks it) passes the menu: its page at +0x214,
//its pad at +0x1A4. a network game's custom weapon outside a modded game is its base, as
//ge_loadout_give will make it (weapons.cpp)
#include "mods/weapons/weapon.h"
#include "input/input.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#ifdef GE_IMGUI
#include "gl.h"
#include "imgui.h"
#include "speedrun/splits.h"
#include <SDL3/SDL.h>
#endif

namespace ge
{

using namespace weapons;

namespace
{

const uint32_t kProfiles = 0x805DE6A0, kProfileSize = 0x590;
const uint32_t kLoadoutNumber = 0x404, kLoadouts = 10;
const uint32_t kLoadoutTable = 0x8057DA78, kTableRow = 8, kTableHash = 4;
const uint32_t kCustomFirst = 0x7D09001A, kCustoms = 5, kCustomItems = 0x230, kCustomList = kCustoms * 4;
const uint32_t kSheet = 0x805D18E0, kSheetCount = 8, kSheetRows = 0xC, kSheetRow = 0x28, kSheetItems = 8;
const uint32_t kItemList = 0x80585C80, kItemRow = 0x10, kItemCount = 0x41, kItemName = 4;
const uint32_t kNoAttachment = 0x7D070001;
const uint32_t kMenuPage = 0x214, kMenuPad = 0x1A4;
const uint32_t kGameStarting = 0x7D00004B, kPausePage = 0x7D0000BD;
enum Item { kPrimary, kSecondary, kGadget1, kGadget2, kGadget3, kPrimaryAttachment, kSecondaryAttachment, kItems = 7 };

struct Panel
{
	float x{}, y{}; //the middle of its top on the game's 1280 x 720 screen
	float gunHigh{}; //how high the gun is drawn
	std::string guns, gadgets; //its two lines
	int64_t gun{ -1 }, second{ -1 }; //the primary's and the secondary's, turning over the lines
	uint32_t frame{}; //the game's frame it was made in
};
const char* const kDot = "  \xB7  "; //between two items

std::mutex g_lock;
Panel g_panel; //under g_lock
bool g_shown = false; //made, ever
uint32_t g_hash = 0; //the loadout the panel was made of, and when
uint32_t g_madeAt = 0;

uint32_t SheetRow(uint32_t hash)
{
	const uint32_t count = ld32(kSheet + kSheetCount), rows = ld32(kSheet + kSheetRows);
	for (uint32_t i = 0; guest_ptr(rows) && i < count && i < 256; ++i)
		if (ld32(rows + i * kSheetRow) == hash) return rows + i * kSheetRow;
	return 0;
}

std::string NameOf(const PPCContext& c, uint32_t key)
{
	if (!key || key == 0xFFFFFFFF || key == kNoAttachment) return {};
	for (const Weapon& w : g_weapons)
		if (w.key == key) return w.name;
	for (uint32_t i = 0; i < kItemCount; ++i)
		if (ld32(kItemList + i * kItemRow) == key) return game_text(c, ld32(kItemList + i * kItemRow + kItemName));
	return {};
}

//a custom weapon is its base outside a modded game, as it is given then
uint32_t AsGiven(uint32_t key)
{
	for (const Weapon& w : g_weapons)
		if (w.key == key && !(modded_lobby() && Installed(w))) return w.baseKey ? w.baseKey : key;
	return key;
}

void Make(const PPCContext& c, uint32_t hash, uint32_t pad, Panel& panel)
{
	uint32_t items[kItems]{};
	if (hash - kCustomFirst < kCustoms)
		for (uint32_t k = 0; k < kItems; ++k) items[k] = ld32(kProfiles + pad * kProfileSize + kCustomItems + k * kCustomList + (hash - kCustomFirst) * 4);
	else if (const uint32_t row = SheetRow(hash))
		for (uint32_t k = 0; k < kItems; ++k) items[k] = ld32(row + kSheetItems + k * 4);
	items[kPrimary] = AsGiven(items[kPrimary]);
	items[kSecondary] = AsGiven(items[kSecondary]);
	auto name = [&](Item item) { return NameOf(c, items[item]); };
	auto gun = [&](Item item, Item attachment)
	{
		const std::string with = name(attachment);
		return name(item) + (with.empty() ? "" : " + " + with);
	};
	panel.guns = gun(kPrimary, kPrimaryAttachment);
	if (const std::string secondary = gun(kSecondary, kSecondaryAttachment); !secondary.empty()) panel.guns += (panel.guns.empty() ? "" : kDot) + secondary;
	panel.gadgets.clear();
	for (Item item : { kGadget1, kGadget2, kGadget3 })
		if (const std::string gadget = name(item); !gadget.empty()) panel.gadgets += (panel.gadgets.empty() ? "" : kDot) + gadget;
	panel.gun = weapon_gun_of_key(items[kPrimary]);
	panel.second = weapon_gun_of_key(items[kSecondary]);
	for (int64_t gun : { panel.gun, panel.second })
		if (gun >= 0)
		{
			std::vector<uint8_t> warm; //the gun read here, not on the renderer's thread
			weapon_picture(gun, 0, 4, 4, warm);
		}
}

const uint32_t kMenuPanels = 0x914, kPanelShown = 0x1129;
PPCFunc orig_scene = nullptr;

void Scene(PPCContext& c)
{
	const uint32_t menu = c.r[3];
	orig_scene(c);
	if (!guest_ptr(menu)) return;
	//called on every page, panel 0 is the mission picture elsewhere: only the pages it fills
	const uint32_t page = ld32(menu + kMenuPage);
	if (page == kGameStarting || page == kPausePage || page == 0x7D000101) st8(menu + kMenuPanels + kPanelShown, 0);
}

} //namespace

void install_loadout_panel()
{
	orig_scene = ppc_hook("ge_loadout_scene", Scene);
	if (!orig_scene) rt_log("goldeneye: ge_loadout_scene is not hooked in this build, the game's loadout pictures show under the caption");
}

void loadout_menu_update(const PPCContext& c, uint32_t menu)
{
	if (!guest_ptr(menu)) return;
	const uint32_t page = ld32(menu + kMenuPage);
	if (page != kGameStarting && page != kPausePage) return;
	const uint32_t pad = std::min<uint32_t>(ld8(menu + kMenuPad), 3);
	const uint32_t number = ld32(kProfiles + pad * kProfileSize + kLoadoutNumber);
	if (number >= kLoadouts) return;
	const uint32_t hash = ld32(kLoadoutTable + number * kTableRow + kTableHash), now = ld32(kFrameCount);
	std::lock_guard<std::mutex> hold(g_lock);
	//made again when it changes, and now and then for an edit or a pack changed meanwhile
	if (hash != g_hash || !g_shown || now - g_madeAt > 60 || now < g_madeAt)
	{
		Make(c, hash, pad, g_panel);
		g_hash = hash;
		g_madeAt = now;
		g_shown = true;
	}
	//under the Loadout row (the players' list starts at 292), paused under the menu's box
	g_panel.x = page == kGameStarting ? 636 : 652;
	g_panel.y = page == kGameStarting ? 162 : 418;
	g_panel.gunHigh = page == kGameStarting ? 56 : 66;
	g_panel.frame = now;
}

#ifdef GE_IMGUI
namespace
{

const float kSecondSize = 0.75f; //of the primary's height
const float kOverlap = 0.3f; //of the secondary's frame, over the primary's
const float kGuns = 22, kGadgets = 19, kLineGap = 2;
const int kPictureW = 256, kPictureH = 64;
const float kTurnSeconds = 6.0f;
const ImU32 kCream = IM_COL32(243, 232, 208, 255), kFaint = IM_COL32(243, 232, 208, 190);

struct Gl
{
	PFNGLGENTEXTURESPROC GenTextures;
	PFNGLBINDTEXTUREPROC BindTexture;
	PFNGLTEXPARAMETERIPROC TexParameteri;
	PFNGLTEXIMAGE2DPROC TexImage2D;
	PFNGLGETINTEGERVPROC GetIntegerv;
	PFNGLPIXELSTOREIPROC PixelStorei;
};
Gl gl{};
bool g_glTried = false;
GLuint g_pictures[2]{}; //the primary's, the secondary's
const auto g_start = std::chrono::steady_clock::now();

bool LoadGl()
{
	if (g_glTried) return gl.GenTextures != nullptr;
	g_glTried = true;
	gl.GenTextures = (PFNGLGENTEXTURESPROC)SDL_GL_GetProcAddress("glGenTextures");
	gl.BindTexture = (PFNGLBINDTEXTUREPROC)SDL_GL_GetProcAddress("glBindTexture");
	gl.TexParameteri = (PFNGLTEXPARAMETERIPROC)SDL_GL_GetProcAddress("glTexParameteri");
	gl.TexImage2D = (PFNGLTEXIMAGE2DPROC)SDL_GL_GetProcAddress("glTexImage2D");
	gl.GetIntegerv = (PFNGLGETINTEGERVPROC)SDL_GL_GetProcAddress("glGetIntegerv");
	gl.PixelStorei = (PFNGLPIXELSTOREIPROC)SDL_GL_GetProcAddress("glPixelStorei");
	if (gl.GenTextures && gl.BindTexture && gl.TexParameteri && gl.TexImage2D && gl.GetIntegerv && gl.PixelStorei) return true;
	gl.GenTextures = nullptr;
	rt_log("goldeneye: weapons: the loadout panel draws no gun, OpenGL's texture functions weren't found");
	return false;
}

//the gun turned as it is now into one of the panel's textures
bool Picture(int64_t gun, GLuint& picture)
{
	std::vector<uint8_t> rgba;
	const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_start).count();
	if (!LoadGl() || !weapon_picture(gun, seconds / kTurnSeconds * 6.2831853f, kPictureW, kPictureH, rgba)) return false;
	GLint texture = 0, unpack = 0;
	gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
	if (!picture) gl.GenTextures(1, &picture);
	gl.BindTexture(GL_TEXTURE_2D, picture);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kPictureW, kPictureH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack);
	gl.BindTexture(GL_TEXTURE_2D, (GLuint)texture);
	return true;
}

} //namespace

bool loadout_panel_showing()
{
	if (!menu_font_ready()) return false;
	std::lock_guard<std::mutex> hold(g_lock);
	const uint32_t now = ld32(kFrameCount);
	return g_shown && now - g_panel.frame <= 2;
}

void loadout_panel_draw()
{
	if (!loadout_panel_showing()) return;
	Panel panel;
	{
		std::lock_guard<std::mutex> hold(g_lock);
		panel = g_panel;
	}
	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	float rect[4];
	video_picture_rect(screen.x, screen.y, rect);
	const float u = rect[3] / 720, mid = rect[0] + panel.x * u;
	float y = rect[1] + panel.y * u;
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	//side by side, the secondary smaller (each is drawn as big as its frame), their feet on one line
	const bool first = panel.gun >= 0 && Picture(panel.gun, g_pictures[0]);
	const bool second = panel.second >= 0 && Picture(panel.second, g_pictures[1]);
	if (first || second)
	{
		const float high = panel.gunHigh * u, small = high * kSecondSize, aspect = (float)kPictureW / kPictureH;
		const float wide = first ? high * aspect : 0, smallWide = second ? small * aspect : 0;
		const float overlap = first && second ? smallWide * kOverlap : 0; //a frame's sides are empty
		const float left = mid - (wide + smallWide - overlap) / 2, right = left + wide - overlap;
		if (first) dl->AddImage((ImTextureID)(intptr_t)g_pictures[0], ImVec2(left, y), ImVec2(left + wide, y + high));
		if (second) dl->AddImage((ImTextureID)(intptr_t)g_pictures[1], ImVec2(right, y + high - small), ImVec2(right + smallWide, y + high));
		y += high;
	}
	auto line = [&](const std::string& text, float size, ImU32 colour)
	{
		if (text.empty()) return;
		size *= u;
		menu_text(dl, ImVec2(std::round(mid - menu_text_width(text, size) / 2), std::round(y)), size, colour, text, true);
		y += size + kLineGap * u;
	};
	line(panel.guns, kGuns, kCream);
	line(panel.gadgets, kGadgets, kFaint);
}
#else
bool loadout_panel_showing() { return false; }
void loadout_panel_draw() {}
#endif

} //namespace ge
