//the splits drawn: a box in the game's window, dragged with the mouse wherever the cursor is free,
//or a window of its own (a second ImGui context, drawn by a thread with a GL context of its own)
#include "speedrun/splits.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef GE_IMGUI
#include "ui/popwindow.h"
#include <SDL3/SDL.h>
#endif

namespace ge
{

using namespace speedrun;

#ifdef GE_IMGUI
namespace
{

const ImU32 kCream = IM_COL32(243, 232, 208, 255), kBlue = IM_COL32(88, 124, 170, 255),
	kBar = IM_COL32(70, 100, 146, 150), kSelected = IM_COL32(255, 255, 255, 245),
	kSelectedText = IM_COL32(84, 146, 212, 255), kPanel = IM_COL32(6, 14, 18, 205), kGrid = IM_COL32(90, 150, 150, 22),
	kDark = IM_COL32(26, 32, 40, 255), kAhead = IM_COL32(92, 214, 112, 255), kBehind = IM_COL32(232, 76, 62, 255),
	kGoldColour = IM_COL32(255, 204, 64, 255);
const float kWidth = 330, kHead = 46, kPitch = 25, kRow = 23, kFoot = 92;
const SDL_DialogFileFilter kFilters[] = { { "LiveSplit splits", "lss" }, { "The timer's own file", "txt" } };

ImVec2 g_box0, g_box1; //where it was drawn in the game's window
std::atomic<bool> g_dragging{ false }; //pointer.cpp asks from the game thread
ImVec2 g_grab; //the cursor's place in the box as it was taken

//its own window
PopWindow g_pop;
ImFont* g_popFont = nullptr;
Clock::time_point g_popDrawn{};

ImU32 Fade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a) << IM_COL32_A_SHIFT); }

//1:23.45, 1:02:03.45 past an hour, 12.34 under a minute
std::string Hms(float s, int decimals = 2)
{
	//rounded first, so 59.996 shows as 1:00.00 and not 60.00
	double scale = std::pow(10.0, decimals);
	double t = std::round(std::max((double)s, 0.0) * scale) / scale;
	int whole = (int)t, h = whole / 3600, m = whole / 60 % 60;
	double sec = t - (double)(h * 3600 + m * 60);
	int width = decimals ? decimals + 3 : 2; //"05.4", or "05" with no decimals
	char text[32];
	if (h) std::snprintf(text, sizeof text, "%d:%02d:%0*.*f", h, m, width, decimals, sec);
	else if (m) std::snprintf(text, sizeof text, "%d:%0*.*f", m, width, decimals, sec);
	else std::snprintf(text, sizeof text, "%.*f", decimals, sec);
	return text;
}

std::string Delta(float d)
{
	return (d < 0 ? "-" : "+") + Hms(std::fabs(d), 1);
}

void RightText(ImDrawList* dl, float right, float y, float size, ImU32 colour, const std::string& s, bool shadow = true)
{
	menu_text(dl, ImVec2(right - menu_text_width(s, size), y), size, colour, s, shadow);
}

float BoxHeight(float u) { return (kHead + kPitch * (float)kSplits + kFoot) * u; }

float Scale()
{
	ImVec2 screen = ImGui::GetIO().DisplaySize;
	return std::max(0.75f, std::min(screen.y / 720, screen.x / 960));
}

//the box with its top left corner at x, y, under the lock
void DrawBox(ImDrawList* dl, float x, float y, float u)
{
	float w = kWidth * u, h = BoxHeight(u);
	ImVec2 b0(x, y), b1(x + w, y + h);

	//the menus' dark screen with its faint grid, and a cream edge
	dl->AddRectFilled(b0, b1, kPanel, 4 * u);
	dl->PushClipRect(b0, b1, true);
	for (float gx = x + 32 * u; gx < x + w; gx += 32 * u) dl->AddLine(ImVec2(gx, y), ImVec2(gx, y + h), kGrid, u);
	for (float gy = y + 32 * u; gy < y + h; gy += 32 * u) dl->AddLine(ImVec2(x, gy), ImVec2(x + w, gy), kGrid, u);
	dl->PopClipRect();
	dl->AddRect(b0, b1, Fade(kCream, g_dragging ? 0.9f : 0.35f), 4 * u, 0, 1.5f * u);

	//the title as the menus have it: a node and a blue bar under a cream one
	ImVec2 node(x + 26 * u, y + 23 * u);
	dl->AddRectFilled(ImVec2(x + 26 * u, y + 9 * u), ImVec2(x + w - 8 * u, y + 12 * u), kCream);
	dl->AddRectFilledMultiColor(ImVec2(x + 26 * u, y + 15 * u), ImVec2(x + w - 8 * u, y + 35 * u), kBlue, Fade(kBlue, 0.15f), Fade(kBlue, 0.15f), kBlue);
	dl->AddRectFilled(ImVec2(x + 26 * u, y + 37 * u), ImVec2(x + w - 8 * u, y + 39 * u), kCream);
	dl->AddCircleFilled(node, 17 * u, kCream, 32);
	dl->AddCircleFilled(node, 13 * u, kBlue, 32);
	dl->AddCircleFilled(node, 4 * u, kCream, 16);
	dl->AddCircleFilled(node, 2 * u, kDark, 12);
	menu_text(dl, ImVec2(x + 50 * u, y + 11 * u), 27 * u, kCream, "Speedrun", true);
	RightText(dl, x + w - 14 * u, y + 15 * u, 19 * u, kCream, "Attempt " + std::to_string(m_Attempts));

	//a row a mission: its name, the difference to the personal best, the time at its end
	float total = 0;
	for (int i = 0; i < kSplits; ++i) total += m_Segment[i];
	float at = 0, sumBest = 0;
	bool haveSum = true;
	for (int i = 0; i < kSplits; ++i)
	{
		at += m_Segment[i];
		if (m_Best[i] < 0) haveSum = false;
		else sumBest += m_Best[i];
		bool current = m_Running && i == m_Index, done = i < m_Index;
		float y0 = y + (kHead + kPitch * (float)i) * u, y1 = y0 + kRow * u;
		dl->AddRectFilled(ImVec2(x + 8 * u, y0), ImVec2(x + w - 8 * u, y1), current ? kSelected : Fade(kBar, done ? 1.0f : 0.7f));
		ImU32 colour = current ? kSelectedText : done ? kCream : Fade(kCream, 0.8f);
		float size = 21 * u, ty = y0 + 1 * u;
		menu_text(dl, ImVec2(x + 16 * u, ty), size, colour, kMissions[i].name, !current);
		std::string time = done || current ? Hms(at) : m_PbBefore[i] >= 0 ? Hms(m_PbBefore[i]) : "-";
		RightText(dl, x + w - 16 * u, ty, size, colour, time, !current);
		float pb = m_PbBefore[i];
		bool showDelta = pb >= 0 && (done || (current && at > pb));
		if (showDelta)
		{
			float d = at - pb;
			ImU32 c = done && m_Gold[i] ? kGoldColour : d < 0 ? kAhead : kBehind;
			RightText(dl, x + w - 112 * u, ty, size, c, Delta(d), !current);
		}
		else if (done && m_Gold[i]) RightText(dl, x + w - 112 * u, ty, size, kGoldColour, "Best", !current);
	}

	//the foot: a cream line, the run's time, the sum of best
	float fy = y + (kHead + kPitch * (float)kSplits + 6) * u;
	ImU32 none = Fade(kCream, 0);
	dl->AddRectFilledMultiColor(ImVec2(x + 8 * u, fy), ImVec2(x + 60 * u, fy + 2 * u), none, kCream, kCream, none);
	dl->AddRectFilled(ImVec2(x + 60 * u, fy), ImVec2(x + w - 60 * u, fy + 2 * u), kCream);
	dl->AddRectFilledMultiColor(ImVec2(x + w - 60 * u, fy), ImVec2(x + w - 8 * u, fy + 2 * u), kCream, none, none, kCream);
	ImU32 clock = kCream;
	if (m_Finished) clock = m_NewPb ? kGoldColour : kBehind;
	else if (m_Running)
	{
		float pb = m_PbBefore[std::min(m_Index, kSplits - 1)];
		clock = pb < 0 || total <= pb ? kAhead : kBehind;
	}
	else clock = Fade(kCream, 0.7f);
	RightText(dl, x + w - 14 * u, fy + 6 * u, 52 * u, clock, Hms(total));
	menu_text(dl, ImVec2(x + 16 * u, fy + 14 * u), 18 * u, Fade(kCream, 0.85f), "Sum of Best", true);
	menu_text(dl, ImVec2(x + 16 * u, fy + 34 * u), 20 * u, kCream, haveSum ? Hms(sumBest) : "-", true);
	const char* state = m_Finished ? (m_NewPb ? "New personal best!" : "Run finished") : m_Running ? "In-game time" : "Pick Dam to start";
	menu_text(dl, ImVec2(x + 16 * u, fy + 62 * u), 18 * u, Fade(kCream, 0.7f), state, true);
}


void OpenPop()
{
	int w = m_WinW > 0 ? m_WinW : (int)kWidth, h = m_WinH > 0 ? m_WinH : (int)BoxHeight(1);
	if (!g_pop.Open("Speedrun Timer", m_WinX, m_WinY, w, h, false, [](ImFontAtlas* atlas) { atlas->AddFontDefault(); g_popFont = menu_font_add(atlas); }))
		m_Popped = false;
}

void ClosePop()
{
	g_pop.Close();
	g_popFont = nullptr;
}

void DrawPop()
{
	g_pop.Present();
	auto now = Clock::now();
	if (now - g_popDrawn < std::chrono::milliseconds(16)) return; //60 a second is plenty for a clock
	g_popDrawn = now;
	g_pop.Begin();
	ImGuiIO& io = ImGui::GetIO();
	float u = std::min(io.DisplaySize.x / kWidth, io.DisplaySize.y / BoxHeight(1));
	menu_font_select(g_popFont);
	DrawBox(ImGui::GetForegroundDrawList(), std::floor((io.DisplaySize.x - kWidth * u) / 2), std::floor((io.DisplaySize.y - BoxHeight(u)) / 2), u);
	menu_font_select(nullptr);
	g_pop.End();
}

//its own window's: closed it goes back in the game's, moved or resized it's kept
bool PopEvent(const SDL_Event* e)
{
	std::lock_guard<std::mutex> hold(g_lock);
	switch (e->type)
	{
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		m_Popped = false;
		Save();
		break;
	case SDL_EVENT_WINDOW_MOVED:
		m_WinX = e->window.data1;
		m_WinY = e->window.data2;
		Save();
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		m_WinW = e->window.data1;
		m_WinH = e->window.data2;
		Save();
		break;
	default: break;
	}
	return e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP; //F1 and the hotkeys work from it too
}

void Exported(void*, const char* const* files, int)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (!files) return Say(std::string("Export failed: ") + SDL_GetError());
	if (!files[0]) return;
	std::filesystem::path path = files[0];
	if (!path.has_extension()) path += ".lss";
	std::ofstream f(path);
	f << WriteLss();
	Say(f.good() ? "Exported to " + path.filename().string() : "Couldn't write " + path.string());
}

void Imported(void*, const char* const* files, int)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (!files) return Say(std::string("Import failed: ") + SDL_GetError());
	if (!files[0]) return;
	std::ifstream f(files[0]);
	std::stringstream text;
	text << f.rdbuf();
	std::string s = text.str();
	std::filesystem::path path = files[0];
	int read = 0;
	if (s.find("<Run") != std::string::npos) read = ReadLss(s);
	else
	{
		float pb[kSplits], best[kSplits];
		std::copy(m_Pb, m_Pb + kSplits, pb);
		std::copy(m_Best, m_Best + kSplits, best);
		Clear();
		std::istringstream in(s);
		read = ReadOwn(in, true);
		if (!read) //not one of ours: what was there stays
		{
			std::copy(pb, pb + kSplits, m_Pb);
			std::copy(best, best + kSplits, m_Best);
		}
	}
	if (!read) return Say("No splits in " + path.filename().string());
	ResetRun();
	Save();
	Say("Imported " + std::to_string(read) + " splits");
}

} //namespace

void speedrun_export()
{
	SDL_ShowSaveFileDialog(Exported, nullptr, SDL_GL_GetCurrentWindow(), kFilters, 1, nullptr);
}

void speedrun_import()
{
	SDL_ShowOpenFileDialog(Imported, nullptr, SDL_GL_GetCurrentWindow(), kFilters, 2, nullptr, false);
}

bool speedrun_event(const SDL_Event* e)
{
	if (g_pop.Opened() && event_window(e) == SDL_GetWindowID(g_pop.Window())) return PopEvent(e);
	if (!speedrun_timer() || speedrun_window()) return false;
	bool free = !in_play() || overlay_open();
	switch (e->type)
	{
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (!free || e->button.button != SDL_BUTTON_LEFT) return false;
		if (e->button.x < g_box0.x || e->button.x > g_box1.x || e->button.y < g_box0.y || e->button.y > g_box1.y) return false;
		g_dragging = true;
		g_grab = ImVec2(e->button.x - g_box0.x, e->button.y - g_box0.y);
		return true;
	case SDL_EVENT_MOUSE_MOTION:
		if (!g_dragging) return false;
		if (!(e->motion.state & SDL_BUTTON_LMASK) || !free)
		{
			g_dragging = false;
			return false;
		}
		{
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			std::lock_guard<std::mutex> hold(g_lock);
			m_X = (e->motion.x - g_grab.x) / std::max(screen.x, 1.0f);
			m_Y = (e->motion.y - g_grab.y) / std::max(screen.y, 1.0f);
		}
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
	{
		if (!g_dragging || e->button.button != SDL_BUTTON_LEFT) return false;
		g_dragging = false;
		std::lock_guard<std::mutex> hold(g_lock);
		//let go outside the game's window: a window of its own where it was dropped
		SDL_Window* game = SDL_GetWindowFromID(e->button.windowID);
		int ww = 0, wh = 0, wx = 0, wy = 0;
		if (game) SDL_GetWindowSize(game, &ww, &wh);
		if (game && (e->button.x < 0 || e->button.y < 0 || e->button.x >= (float)ww || e->button.y >= (float)wh))
		{
			SDL_GetWindowPosition(game, &wx, &wy);
			m_WinX = wx + (int)(e->button.x - g_grab.x);
			m_WinY = wy + (int)(e->button.y - g_grab.y);
			m_WinW = (int)(g_box1.x - g_box0.x);
			m_WinH = (int)(g_box1.y - g_box0.y);
			m_Popped = true;
			m_X = -1; //back in the game's window it's against the right edge again
			m_Y = 0.06f;
		}
		Save();
		return true;
	}
	default:
		return false;
	}
}

bool speedrun_dragging() { return g_dragging; }

//before the game's window's overlay: its own window opened, closed and drawn
void speedrun_own_window()
{
	bool want;
	{
		std::lock_guard<std::mutex> hold(g_lock);
		EnsureLoaded();
		want = m_Shown && m_Popped;
		if (want && !g_pop.Opened() && menu_font_ready()) OpenPop();
	}
	if (!want && g_pop.Opened()) ClosePop();
	if (!g_pop.Opened()) return;
	std::lock_guard<std::mutex> hold(g_lock);
	DrawPop();
}

bool speedrun_in_game() //drawn over the game's picture
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	return m_Shown && !m_Popped;
}

void speedrun_draw()
{
	if (!menu_font_ready() || pictures_hiding()) return;
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (!m_Shown || m_Popped) return;
	float u = Scale();
	ImVec2 screen = ImGui::GetIO().DisplaySize;
	float w = kWidth * u, h = BoxHeight(u);
	float x = m_X < 0 ? screen.x - w - 16 * u : m_X * screen.x, y = m_Y * screen.y;
	if (!g_dragging) //held, it goes past the edges and out
	{
		x = std::clamp(x, 0.0f, std::max(0.0f, screen.x - w));
		y = std::clamp(y, 0.0f, std::max(0.0f, screen.y - h));
	}
	g_box0 = ImVec2(x, y);
	g_box1 = ImVec2(x + w, y + h);
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	int from = dl->VtxBuffer.Size;
	DrawBox(dl, x, y, u);
	float a = g_dragging ? 1.0f : m_Opacity / 100; //held it's all there, to see where it goes
	if (a < 1)
	{
		for (int i = from; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].col = Fade(dl->VtxBuffer[i].col, a);
	}
}
#endif

} //namespace ge
