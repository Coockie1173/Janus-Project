//what the chosen loadout has, beside the menus that choose one: the "Game starting in..." page of a
//network game and its pause menu, both with a Loadout row whose left and right go through the ten
//loadouts. a panel in the menus' look lists its weapons, attachments and gadgets, the primary
//turning under them (preview.cpp draws it)
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

struct Row { std::string label, value; };
struct Panel
{
	float x{}, y{}; //its top left on the game's 1280 x 720 screen
	std::vector<Row> rows;
	int64_t gun{ -1 }; //the primary's, turning under the rows
	uint32_t frame{}; //the game's frame it was made in
};

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
	panel.rows.clear();
	auto add = [&](const char* label, Item item, bool always)
	{
		std::string name = NameOf(c, items[item]);
		if (name.empty() && !always) return;
		panel.rows.push_back({ label, name.empty() ? "None" : name });
	};
	add("Primary", kPrimary, true);
	add("Primary Attachment", kPrimaryAttachment, false);
	add("Secondary", kSecondary, true);
	add("Secondary Attachment", kSecondaryAttachment, false);
	add("Gadget 1", kGadget1, false);
	add("Gadget 2", kGadget2, false);
	add("Gadget 3", kGadget3, false);
	panel.gun = weapon_gun_of_key(items[kPrimary]);
	if (panel.gun >= 0)
	{
		std::vector<uint8_t> warm; //the gun read here, not on the renderer's thread
		weapon_picture(panel.gun, 0, 4, 4, warm);
	}
}

} //namespace

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
	//the game starting: right of the Loadout row. paused: right of the menu's box
	g_panel.x = 868;
	g_panel.y = page == kGameStarting ? 122 : 236;
	g_panel.frame = now;
}

#ifdef GE_IMGUI
namespace
{

//no box: the gun, then the items as lines of text with hairlines between, over a shadow that fades
//out to the sides so it reads on any background
const float kWide = 360, kPitch = 27, kLabel = 18, kText = 21, kPictureHigh = kWide / 4, kSide = 24;
const int kPictureW = 256, kPictureH = 64;
const float kTurnSeconds = 6.0f;
const ImU32 kCream = IM_COL32(243, 232, 208, 255), kLabelColour = IM_COL32(243, 232, 208, 175), kWhite = IM_COL32(255, 255, 255, 255),
            kHair = IM_COL32(243, 232, 208, 45), kShade = IM_COL32(0, 0, 0, 120), kClear = IM_COL32(0, 0, 0, 0);

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
GLuint g_picture = 0;
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

//the gun turned as it is now into the panel's texture
bool Picture(int64_t gun)
{
	std::vector<uint8_t> rgba;
	const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_start).count();
	if (!LoadGl() || !weapon_picture(gun, seconds / kTurnSeconds * 6.2831853f, kPictureW, kPictureH, rgba)) return false;
	GLint texture = 0, unpack = 0;
	gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
	if (!g_picture) gl.GenTextures(1, &g_picture);
	gl.BindTexture(GL_TEXTURE_2D, g_picture);
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
	const float u = rect[3] / 720, x = rect[0] + panel.x * u, top = rect[1] + panel.y * u, w = kWide * u;
	const bool picture = panel.gun >= 0 && Picture(panel.gun);
	const float gunHigh = picture ? kPictureHigh * u : 0;
	const float bottom = top + gunHigh + kPitch * u * (float)panel.rows.size();
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	//the shadow: clear at the edges, darkest behind the middle
	const float side = kSide * u, mid = x + w / 2, fade = 14 * u;
	dl->AddRectFilledMultiColor(ImVec2(x - side, top), ImVec2(mid, bottom), kClear, kShade, kShade, kClear);
	dl->AddRectFilledMultiColor(ImVec2(mid, top), ImVec2(x + w + side, bottom), kShade, kClear, kClear, kShade);
	dl->AddRectFilledMultiColor(ImVec2(x - side, top - fade), ImVec2(mid, top), kClear, kClear, kShade, kClear);
	dl->AddRectFilledMultiColor(ImVec2(mid, top - fade), ImVec2(x + w + side, top), kClear, kClear, kClear, kShade);
	dl->AddRectFilledMultiColor(ImVec2(x - side, bottom), ImVec2(mid, bottom + fade), kClear, kShade, kClear, kClear);
	dl->AddRectFilledMultiColor(ImVec2(mid, bottom), ImVec2(x + w + side, bottom + fade), kShade, kClear, kClear, kClear);
	if (picture) dl->AddImage((ImTextureID)(intptr_t)g_picture, ImVec2(x, top), ImVec2(x + w, top + gunHigh));
	float y = top + gunHigh;
	for (size_t i = 0; i < panel.rows.size(); ++i)
	{
		const Row& row = panel.rows[i];
		if (i || picture) dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), kHair, 1 * u);
		const float label = kLabel * u, value = kText * u;
		menu_text(dl, ImVec2(x, y + (kPitch - kLabel) * 0.5f * u), label, kLabelColour, row.label, true);
		const float room = w - menu_text_width(row.label, label) - 14 * u;
		const float size = std::min(value, value * room / std::max(1.0f, menu_text_width(row.value, value)));
		menu_text(dl, ImVec2(x + w - menu_text_width(row.value, size), y + (kPitch * u - size) * 0.5f), size, row.label == "Primary" ? kCream : kWhite, row.value, true);
		y += kPitch * u;
	}
}
#else
bool loadout_panel_showing() { return false; }
void loadout_panel_draw() {}
#endif

} //namespace ge
