//the menu's picture, drawn like the game's own menus: cream lines, blue bars and the white bar
//under the cursor, the game's 1280 x 720 screen centred in the window

#include "ui/settings/menu.h"
#include <algorithm>
#include <cmath>

#ifdef GE_IMGUI
#include "ge_font.h"
#include "ge_version.h"
#endif

namespace ge::menu
{

#ifdef GE_IMGUI
const float kNarrow = 0.80f; //the game's font is narrow, so the glyphs get drawn squeezed
const ImU32 kCream = IM_COL32(243, 232, 208, 255), kBlue = IM_COL32(88, 124, 170, 255),
            kBar = IM_COL32(70, 100, 146, 150), kSelected = IM_COL32(255, 255, 255, 245),
            kSelectedText = IM_COL32(84, 146, 212, 255), kShadow = IM_COL32(0, 0, 0, 150),
            kDim = IM_COL32(6, 14, 18, 215), kGrid = IM_COL32(90, 150, 150, 22), kDark = IM_COL32(26, 32, 40, 255);

float text_width(const std::string& s, float size) {
    ImFont* font = g_textFont ? g_textFont : g_font;
    float w = 0;
    for (unsigned char c : s) w += font->GetCharAdvance(c);
    return w * size / font->FontSize * kNarrow;
}

void text(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const std::string& s, bool shadow = true) {
    ImFont* font = g_textFont ? g_textFont : g_font;
    float k = size / font->FontSize;
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
        float x = at.x + (pass ? 0 : size * 0.05f), y = at.y + (pass ? 0 : size * 0.05f);
        for (unsigned char c : s) {
            const ImFontGlyph* g = font->FindGlyph(c);
            if (!g) continue;
            if (g->Visible) {
                dl->PrimReserve(6, 4);
                dl->PrimRectUV(ImVec2(x + g->X0 * k * kNarrow, y + g->Y0 * k), ImVec2(x + g->X1 * k * kNarrow, y + g->Y1 * k),
                               ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V1), pass ? colour : kShadow);
            }
            x += g->AdvanceX * k * kNarrow;
        }
    }
}

//as large as size, smaller if that's what fits in width
float fit(const std::string& s, float size, float width) {
    float w = text_width(s, size);
    return w > width ? size * width / w : size;
}

ImU32 fade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a) << IM_COL32_A_SHIFT); }

float rows_top() { return g_pages[g_page].rows.size() > 4 ? 178.0f : 276.0f; }
int DropShown() { return std::min(kDropVisible, (int)g_dropOptions.size()); }
float DropHeight() { return kDropPitch * (float)DropShown() + 2 * kDropPad; }
float DropTop() //under its row
{
	float under = rows_top() + kRowPitch * (float)(g_drop - g_scroll) + kRowHeight + 3;
	return std::min(under, kDropFloor - DropHeight());
}

//a key's cap and what it does, like the game's "(b) Back"
float key_hint(ImDrawList* dl, const Layout& L, float x, float y, const char* key, const char* what, bool right) {
    float size = 24 * L.u, cap = 17 * L.u, pad = 7 * L.u;
    float kw = text_width(key, cap) + 2 * pad, ww = text_width(what, size), gap = 12 * L.u;
    if (right) x -= kw + gap + ww;
    float kx = right ? x + ww + gap : x, wx = right ? x : x + kw + gap;
    dl->AddRectFilled(ImVec2(kx, y), ImVec2(kx + kw, y + 26 * L.u), kDark, 13 * L.u);
    dl->AddRect(ImVec2(kx, y), ImVec2(kx + kw, y + 26 * L.u), fade(kCream, 0.5f), 13 * L.u, 0, 1.5f * L.u);
    text(dl, ImVec2(kx + pad, y + 4 * L.u), cap, kCream, key, false);
    text(dl, ImVec2(wx, y), size, kCream, what);
    return kw + gap + ww;
}

void draw_frame(ImDrawList* dl, const Layout& L, const Page& page) {
    float u = L.u;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(L.w, L.h), kDim);
    for (float gx = std::fmod(L.cx, 64 * u); gx < L.w; gx += 64 * u) dl->AddLine(ImVec2(gx, 0), ImVec2(gx, L.h), kGrid, u);
    for (float gy = 0; gy < L.h; gy += 64 * u) dl->AddLine(ImVec2(0, gy), ImVec2(L.w, gy), kGrid, u);

    //the title with its node and the bar that runs off to the right
    ImVec2 node(L.x(260), L.y(128));
    dl->AddLine(ImVec2(L.x(150), L.y(18)), node, kCream, 5 * u);
    dl->AddRectFilled(ImVec2(node.x, L.y(79)), ImVec2(L.x(318), L.y(123)), kCream);
    dl->AddCircleFilled(node, 49 * u, kCream, 48);
    dl->AddRectFilled(ImVec2(node.x, L.y(87)), ImVec2(L.x(311), L.y(115)), kBlue);
    dl->AddCircleFilled(node, 41 * u, kBlue, 48);
    dl->AddCircleFilled(node, 11 * u, kCream, 24);
    dl->AddCircleFilled(node, 5 * u, kDark, 16);
    ImU32 none = fade(kCream, 0), blue0 = fade(kBlue, 0);
    dl->AddRectFilledMultiColor(ImVec2(L.x(600), L.y(86)), ImVec2(L.x(760), L.y(89)), none, kCream, kCream, none);
    dl->AddRectFilled(ImVec2(L.x(760), L.y(86)), ImVec2(L.w, L.y(89)), kCream);
    dl->AddRectFilledMultiColor(ImVec2(L.x(620), L.y(96)), ImVec2(L.x(820), L.y(118)), blue0, kBlue, kBlue, blue0);
    dl->AddRectFilled(ImVec2(L.x(820), L.y(96)), ImVec2(L.w, L.y(118)), kBlue);
    dl->AddRectFilled(ImVec2(L.x(322), L.y(123)), ImVec2(L.w, L.y(126)), kCream);
    text(dl, ImVec2(L.x(323), L.y(78)), 40 * u, kCream, g_page == kBhop && bhop_locked() ? page.title + (bhop_map_locked() ? " (Map's)" : " (Host's)") : page.title);

    //the foot, what the row under the cursor is for and the keys
    for (float y : {545.0f, 668.0f}) {
        dl->AddRectFilledMultiColor(ImVec2(L.x(200), L.y(y)), ImVec2(L.x(300), L.y(y + 3)), none, kCream, kCream, none);
        dl->AddRectFilled(ImVec2(L.x(300), L.y(y)), ImVec2(L.x(980), L.y(y + 3)), kCream);
        dl->AddRectFilledMultiColor(ImVec2(L.x(980), L.y(y)), ImVec2(L.x(1080), L.y(y + 3)), kCream, none, none, kCream);
    }
    const Row& row = page.rows[g_row];
    std::string hint = g_listening < 0 ? row.hint : g_page == kHotkeyEdit || g_listenKey >= 0 ? "Press a key. Esc leaves it as it is."
        : "Press a key or a mouse button. Esc leaves it as it is.";
    float hs = fit(hint, 24 * u, 840 * u);
    text(dl, ImVec2(L.cx - text_width(hint, hs) / 2, L.y(592)), hs, kCream, hint);
    float left = L.x(210);
    left += key_hint(dl, L, left, L.y(625), "Esc", g_page == kRoot ? "Close" : "Back", false) + 28 * u;
    if (row.clear && g_listening < 0) key_hint(dl, L, left, L.y(625), "Del", row.clears ? row.clears : "Clear", false);
    const char* does = row.does ? row.does : row.options ? "Choose" : row.binding ? "Add Key" : row.select || !row.adjust ? "Select" : "Change";
    if (g_listening < 0) key_hint(dl, L, L.x(1070), L.y(625), row.select || !row.adjust ? "Enter" : "< >", does, true);
    //the version under the foot, for bug reports
    if (g_page == kRoot) {
        const std::string version = "Janus-Project " GE_VERSION;
        text(dl, ImVec2(L.x(1080) - text_width(version, 18 * u), L.y(682)), 18 * u, fade(kCream, 0.5f), version);
    }
}

void draw_rows(ImDrawList* dl, const Layout& L, const Page& page) {
    float u = L.u;
    int n = (int)page.rows.size();
    g_scroll = std::clamp(g_scroll, std::max(0, g_row - kVisible + 1), g_row);
    g_scroll = std::clamp(g_scroll, 0, std::max(0, n - kVisible));
    float x0 = L.x(kRowLeft), x1 = L.x(kRowRight);
    for (int i = g_scroll; i < n && i < g_scroll + kVisible; ++i) {
        const Row& r = page.rows[i];
        bool sel = i == g_row;
        float y0 = L.y(rows_top() + kRowPitch * (float)(i - g_scroll)), y1 = y0 + kRowHeight * u;
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), sel ? kSelected : kBar);
        ImU32 colour = sel ? kSelectedText : kCream;
        float size = 31 * u, ty = y0 + 3 * u;
        float ls = r.wide ? fit(r.label, size, (x1 - x0) * 0.7f) : size; //a long one leaves room for its value
        text(dl, ImVec2(x0 + 14 * u, ty + (size - ls) * 0.55f), ls, colour, r.label, !sel);
        if (!r.value) continue;
        std::string v = r.value();
        if (r.options) { //what it's on and the arrow that says there's a list
            float a = 7 * u, ax = x1 - 14 * u - a, ay = (y0 + y1) / 2 - a * 0.5f;
            dl->AddTriangleFilled(ImVec2(ax - a, ay), ImVec2(ax + a, ay), ImVec2(ax, ay + a), colour);
            float room = ax - a - 12 * u - (x0 + 44 * u + text_width(r.label, ls)), vs = fit(v, 26 * u, room);
            text(dl, ImVec2(ax - a - 12 * u - text_width(v, vs), ty + (size - vs) * 0.55f), vs, colour, v, !sel);
            continue;
        }
        if (r.binding || r.wide) { //the keys, against the right edge
            if (g_listening == i) v = std::fmod(ImGui::GetTime(), 1.0) < 0.6 ? "Press a key..." : "";
            float room = x1 - x0 - 44 * u - text_width(r.label, ls), vs = fit(v, 26 * u, room);
            text(dl, ImVec2(x1 - 14 * u - text_width(v, vs), ty + (size - vs) * 0.55f), vs, colour, v, !sel);
            continue;
        }
        float mid = (y0 + y1) / 2;
        if (r.slider) { //the track, filled up to the knob, and the number
            float sx0 = L.x(kSliderLeft), sx1 = L.x(kSliderRight), kx = sx0 + (sx1 - sx0) * std::clamp(r.slider(), 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(sx0, mid - 2 * u), ImVec2(sx1, mid + 2 * u), fade(colour, 0.35f));
            dl->AddRectFilled(ImVec2(sx0, mid - 4 * u), ImVec2(kx, mid + 4 * u), colour);
            dl->AddRectFilled(ImVec2(kx - 4 * u, mid - 13 * u), ImVec2(kx + 4 * u, mid + 13 * u), colour);
            float vs = 28 * u;
            text(dl, ImVec2(x1 - 14 * u - text_width(v, vs), ty + (size - vs) * 0.55f), vs, colour, v, !sel);
            continue;
        }
        float vx1 = x1 - 12 * u, vx0 = vx1 - kValueWidth * u, a = 8 * u;
        float vs = fit(v, 28 * u, (kValueWidth - 50) * u);
        text(dl, ImVec2((vx0 + vx1 - text_width(v, vs)) / 2, ty + (size - vs) * 0.55f), vs, colour, v, !sel);
        if (sel) {
            dl->AddTriangleFilled(ImVec2(vx0, mid), ImVec2(vx0 + a, mid - a), ImVec2(vx0 + a, mid + a), colour);
            dl->AddTriangleFilled(ImVec2(vx1, mid), ImVec2(vx1 - a, mid - a), ImVec2(vx1 - a, mid + a), colour);
        }
    }
    //more above, more below
    float mx = (x0 + x1) / 2, a = 9 * u;
    if (g_scroll > 0) {
        float y = L.y(rows_top() - 10);
        dl->AddTriangleFilled(ImVec2(mx, y - a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
    }
    if (g_scroll + kVisible < n) {
        float y = L.y(rows_top() + kRowPitch * kVisible + 5);
        dl->AddTriangleFilled(ImVec2(mx, y + a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
    }
}

//the open dropdown's list, over the rows under it
void DrawDrop(ImDrawList* dl, const Layout& L)
{
	float u = L.u;
	int n = (int)g_dropOptions.size(), shown = DropShown();
	g_dropScroll = std::clamp(g_dropScroll, std::max(0, g_dropAt - shown + 1), g_dropAt);
	g_dropScroll = std::clamp(g_dropScroll, 0, n - shown);
	float x0 = L.x(kDropLeft), x1 = L.x(kRowRight), top = L.y(DropTop()), bottom = top + DropHeight() * u;
	dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, bottom), kDark);
	dl->AddRect(ImVec2(x0, top), ImVec2(x1, bottom), kCream, 0, 0, 2 * u);
	int chosen = g_pages[g_page].rows[g_drop].chosen();
	for (int i = g_dropScroll; i < g_dropScroll + shown; ++i)
	{
		bool sel = i == g_dropAt;
		float y0 = top + (kDropPad + kDropPitch * (float)(i - g_dropScroll)) * u, y1 = y0 + (kDropPitch - 2) * u;
		if (sel) dl->AddRectFilled(ImVec2(x0 + kDropPad * u, y0), ImVec2(x1 - kDropPad * u, y1), kSelected);
		ImU32 colour = sel ? kSelectedText : kCream;
		if (i == chosen) dl->AddCircleFilled(ImVec2(x0 + 20 * u, (y0 + y1) / 2), 4 * u, colour, 12);
		float size = fit(g_dropOptions[i], 26 * u, x1 - x0 - 50 * u);
		text(dl, ImVec2(x0 + 34 * u, y0 + 3 * u + (26 * u - size) * 0.55f), size, colour, g_dropOptions[i], !sel);
	}
	//more above, more below
	float ax = x1 - 18 * u, a = 6 * u;
	if (g_dropScroll > 0) dl->AddTriangleFilled(ImVec2(ax, top - a - 3 * u), ImVec2(ax - a, top - 3 * u), ImVec2(ax + a, top - 3 * u), kCream);
	if (g_dropScroll + shown < n) dl->AddTriangleFilled(ImVec2(ax, bottom + a + 3 * u), ImVec2(ax - a, bottom + 3 * u), ImVec2(ax + a, bottom + 3 * u), kCream);
}

//where a grid's cells start after its row names, and how wide one is
void GridColumns(const Layout& L, const Grid& grid, float& x0, float& width)
{
	x0 = L.x(kGridLeft + (grid.rowNames.empty() ? 0.0f : kGridNames));
	width = (L.x(kGridRight) - x0) / (float)grid.cols;
}

void DrawGrid(ImDrawList* dl, const Layout& L)
{
	const Grid& grid = CurrentGrid();
	float u = L.u, x0, cw;
	GridColumns(L, grid, x0, cw);
	int rows = grid.Rows(), at = g_gridAt / grid.cols;
	g_gridScroll = std::clamp(g_gridScroll, std::max(0, at - kGridVisible + 1), at);
	g_gridScroll = std::clamp(g_gridScroll, 0, std::max(0, rows - kGridVisible));
	for (int col = 0; col < (int)grid.columns.size(); ++col)
	{
		float size = fit(grid.columns[col], 21 * u, cw - 8 * u);
		text(dl, ImVec2(x0 + cw * ((float)col + 0.5f) - text_width(grid.columns[col], size) / 2, L.y(kGridTop - 30) + (21 * u - size) * 0.55f), size, kCream, grid.columns[col]);
	}
	for (int row = g_gridScroll; row < rows && row < g_gridScroll + kGridVisible; ++row)
	{
		float y0 = L.y(kGridTop + kGridPitch * (float)(row - g_gridScroll)), y1 = y0 + (kGridPitch - 4) * u;
		if (!grid.rowNames.empty())
		{
			float size = fit(grid.rowNames[row], 27 * u, (kGridNames - 12) * u);
			text(dl, ImVec2(L.x(kGridLeft), y0 + 3 * u + (27 * u - size) * 0.55f), size, row == at ? kSelected : kCream, grid.rowNames[row]);
		}
		for (int col = 0; col < grid.cols; ++col)
		{
			int i = row * grid.cols + col;
			ImVec2 a(x0 + cw * (float)col + 2 * u, y0), b(x0 + cw * (float)(col + 1) - 2 * u, y1);
			if (!grid.hash[i])
			{
				dl->AddRectFilled(a, b, fade(kBar, 0.25f));
				continue;
			}
			bool sel = i == g_gridAt;
			int lit = grid.lit ? grid.lit(i) : -1;
			const std::string label = lit < 0 ? grid.label[i] : lit ? "On" : "Off";
			dl->AddRectFilled(a, b, sel ? kSelected : lit == 0 ? fade(kBar, 0.45f) : kBar);
			float size = fit(label, 26 * u, cw - 18 * u);
			text(dl, ImVec2((a.x + b.x - text_width(label, size)) / 2, y0 + 3 * u + (26 * u - size) * 0.55f), size, sel ? kSelectedText : lit == 0 ? fade(kCream, 0.6f) : kCream, label, !sel);
		}
	}
	//more above, more below
	float mx = (L.x(kGridLeft) + L.x(kGridRight)) / 2, a = 9 * u;
	if (g_gridScroll > 0)
	{
		float y = L.y(kGridTop - 36);
		dl->AddTriangleFilled(ImVec2(mx, y - a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
	}
	if (g_gridScroll + kGridVisible < rows)
	{
		float y = L.y(kGridTop + kGridPitch * kGridVisible + 2);
		dl->AddTriangleFilled(ImVec2(mx, y + a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
	}
}

//the grid's cell with a level under a point of the window, -1 for none
int GridAt(float x, float y)
{
	const Layout& L = g_layout;
	const Grid& grid = CurrentGrid();
	if (L.u <= 0 || grid.hash.empty()) return -1;
	float x0, cw;
	GridColumns(L, grid, x0, cw);
	float r = (y - L.y(kGridTop)) / (kGridPitch * L.u), c = (x - x0) / cw;
	int row = (int)std::floor(r), col = (int)std::floor(c);
	if (r < 0 || row >= kGridVisible || c < 0 || col >= grid.cols) return -1;
	int i = (row + g_gridScroll) * grid.cols + col;
	return i < (int)grid.hash.size() && grid.hash[i] ? i : -1;
}

//the one in the list under a point of the window, -1 for none
int DropAt(float x, float y)
{
	const Layout& L = g_layout;
	if (L.u <= 0 || x < L.x(kDropLeft) || x > L.x(kRowRight)) return -1;
	float r = (y - L.y(DropTop() + kDropPad)) / (kDropPitch * L.u);
	int i = (int)std::floor(r);
	return r < 0 || i >= DropShown() ? -1 : i + g_dropScroll;
}

//the row under a point of the window, -1 for none
int row_at(float x, float y) {
    const Layout& L = g_layout;
    if (L.u <= 0 || x < L.x(kRowLeft) || x > L.x(kRowRight)) return -1;
    float r = (y - L.y(rows_top())) / (kRowPitch * L.u);
    int i = (int)std::floor(r);
    if (r < 0 || i >= kVisible || r - (float)i > kRowHeight / kRowPitch) return -1;
    i += g_scroll;
    return i < (int)g_pages[g_page].rows.size() ? i : -1;
}

void DrawToast(ImDrawList* dl, const Layout& L)
{
	float left = std::chrono::duration<float>(g_toastUntil - Clock::now()).count();
	if (left <= 0) return;
	float u = L.u, a = std::min(1.0f, left / 0.3f); //fades out at the end
	float size = fit(g_toast, 26 * u, L.w - 80 * u), w = text_width(g_toast, size), pad = 16 * u;
	ImVec2 at(L.cx - w / 2, L.y(34));
	dl->AddRectFilled(ImVec2(at.x - pad, at.y - 6 * u), ImVec2(at.x + w + pad, at.y + size + 8 * u), fade(kDim, a), 8 * u);
	text(dl, at, size, fade(kCream, a), g_toast, false);
}
#endif

} //namespace ge::menu

namespace ge
{

using namespace menu;

#ifdef GE_IMGUI
//the menu's look for the speedrun timer (speedrun/window.cpp)
void menu_text(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const std::string& s, bool shadow) { text(dl, at, size, colour, s, shadow); }
float menu_text_width(const std::string& s, float size) { return text_width(s, size); }
bool menu_font_ready() { return g_font != nullptr; }
ImFont* menu_font_add(ImFontAtlas* atlas)
{
	ImFontConfig cfg;
	cfg.FontDataOwnedByAtlas = false;
	return atlas->AddFontFromMemoryTTF((void*)kMenuFont, (int)sizeof kMenuFont, kFontSize, &cfg);
}
void menu_font_select(ImFont* font) { g_textFont = font; }

void settings_draw() {
    if (!g_open && !settings_toast()) return;
    ImVec2 size = ImGui::GetIO().DisplaySize;
    Layout& L = g_layout;
    L.w = size.x;
    L.h = size.y;
    L.u = std::min(size.y / 720, size.x / 960);
    L.cx = size.x / 2;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->PushTextureID(g_font->ContainerAtlas->TexID);
    if (g_open) {
        if (g_keys.lines.empty()) load_keys(); //opened before the window had read the key file
        RefreshHotkeyPages();
        RefreshTriggerPage();
        if (OnGrid()) { //the foot says which level the cursor is on
            const Grid& grid = CurrentGrid();
            bool cell = g_gridAt < (int)grid.about.size() && grid.hash[g_gridAt];
            int lit = cell && grid.lit ? grid.lit(g_gridAt) : -1;
            g_pages[g_page].rows[0].hint = g_page == kSkips ? (!cell ? "No missions are listed (config/openings.txt)."
                    : lit < 0 ? grid.about[g_gridAt] + "." : grid.about[g_gridAt] + (lit ? ": skipped." : ": played."))
                : !cell ? "The disc's levels couldn't be listed."
                : guest_ptr(ld32(kPlayerPtr)) ? grid.about[g_gridAt] + ". Levels load from the main menu: quit this one first." : "Load " + grid.about[g_gridAt] + ".";
        }
        const Page& page = g_pages[g_page];
        draw_frame(dl, L, page);
        if (OnGrid()) DrawGrid(dl, L);
        else draw_rows(dl, L, page);
        if (g_drop >= 0) DrawDrop(dl, L);
    } else DrawToast(dl, L);
    dl->PopTextureID();
}
#endif

} //namespace ge
