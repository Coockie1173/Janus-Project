//the speedrun timer: a splits window like LiveSplit's, timed by the game's own level timer
//
//a run is the campaign from Dam to Cradle, a split a mission. it starts when Dam is picked in
//the menu (a level change from the front end to Dam's entrance 1) and each split ends with the
//app's update loading what comes after a won mission (StartLevelChange from 80009334: the next
//mission's entrance 1, or the front end for "Save and Quit" and after Cradle's credits). a quit
//from the pause menu (802354D8), a restart (802356D8) or a checkpoint's reload don't split
//
//what's counted is the level timer (805C963C) going up while there is a player and the game
//isn't paused: every attempt of a level, the deaths and the cutscenes, never the menus or the
//loads. a checkpoint's reload sets the timer back, that drop isn't counted either way
//
//like LiveSplit: the personal best's time at the end of each split, the best segment of each,
//the difference to the personal best as a split ends (green ahead, red behind, gold for a best
//segment), the sum of best. a finished run faster than the personal best becomes it
//picking Dam in the menu again starts a new attempt
//
//the window is dragged with the mouse wherever the cursor is free (menus, a pause, F1)
//the splits, the window's place and whether it's shown are in speedrun.txt, next to the settings
//GE_SPEEDRUN=F that file instead
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef GE_IMGUI
#include "imgui.h"
#include <SDL3/SDL.h>
#endif

#ifndef GE_SETTINGS_FILE
#define GE_SETTINGS_FILE "settings.env"
#endif

namespace ge
{

#ifdef GE_IMGUI
void menu_text(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const std::string& s, bool shadow); //settings.cpp, the game's font
float menu_text_width(const std::string& s, float size);
bool menu_font_ready();
#endif

namespace
{

const uint32_t kCurrentLevel = 0x805D1804;
const uint32_t kLevelTimer = 0x805C963C; //a float, seconds played in the level
const uint32_t kLevelKind = 0xFFFF0000, kCampaignLevel = 0x01810000;
const uint32_t kFrontEnd = 0x01080004;
const uint32_t kDam = 0x0181013F;
const uint32_t kMissionStart = 1;
const float kMaxStep = 1.0f; //more than this in a frame is a jump (a checkpoint), not time played

struct Mission
{
	const char* name;
	uint32_t levels[2];
};
const Mission kMissions[] = {
	{"Dam", {0x0181013F}}, {"Facility", {0x0181014A}}, {"Airfield", {0x0181013C}}, {"Nightclub", {0x01810144}},
	{"Carrier", {0x01810147}}, {"Outpost", {0x01810150}}, {"Bunker", {0x0181014F}}, {"Archives", {0x01810146}},
	{"Tank", {0x01810145}}, {"Station", {0x01810151}}, {"Memorial", {0x0181014E}}, {"Jungle", {0x01810149}},
	{"Solar", {0x0181014D}}, {"Cradle", {0x0181014B, 0x01810169}}};
const int kSplits = (int)std::size(kMissions);

int MissionOf(uint32_t level)
{
	for (int i = 0; i < kSplits; ++i)
	{
		for (uint32_t l : kMissions[i].levels)
		{
			if (l && l == level) return i;
		}
	}
	return -1;
}

//everything below is the game thread's and the window's, under the lock
std::mutex g_lock;
bool m_Shown{ false };
float m_X{ -1 }, m_Y{ 0.06f }; //the window's top left corner, a part of the screen. -1: against the right edge
int m_Attempts{ 0 };
float m_Pb[kSplits]; //the time at the end of each split in the personal best, -1 none
float m_Best[kSplits]; //the best segment, -1 none
float m_Segment[kSplits]{}; //this run's
bool m_Gold[kSplits]{}; //the segment was a best one as it ended
float m_PbBefore[kSplits]; //the personal best this run is held against, kept when it becomes the new one
bool m_Running{ false }, m_Finished{ false }, m_NewPb{ false };
int m_Index{ 0 }; //the split being run
uint32_t m_LastLevel{ 0 };
float m_LastTime{ 0 };
bool m_Loaded{ false };

std::string FilePath()
{
	if (const char* e = std::getenv("GE_SPEEDRUN")) return e;
	return std::filesystem::path(GE_SETTINGS_FILE).replace_filename("speedrun.txt").string();
}

void Clear()
{
	for (int i = 0; i < kSplits; ++i) m_Pb[i] = m_Best[i] = m_PbBefore[i] = -1;
}

void Load()
{
	Clear();
	m_Loaded = true;
	std::ifstream f(FilePath());
	std::string line;
	while (std::getline(f, line))
	{
		std::istringstream in(line);
		std::string key;
		in >> key;
		if (key == "shown") in >> m_Shown;
		else if (key == "at") in >> m_X >> m_Y;
		else if (key == "attempts") in >> m_Attempts;
		else if (key == "split")
		{
			std::string name;
			float pb = -1, best = -1;
			in >> name >> pb >> best;
			for (int i = 0; i < kSplits; ++i)
			{
				if (name == kMissions[i].name)
				{
					m_Pb[i] = pb;
					m_Best[i] = best;
				}
			}
		}
	}
	std::copy(m_Pb, m_Pb + kSplits, m_PbBefore);
}

void Save()
{
	std::ofstream f(FilePath());
	if (!f)
	{
		rt_log("goldeneye: speedrun timer not saved: cannot write %s", FilePath().c_str());
		return;
	}
	f << "# the speedrun timer (F1 > Speedrun Timer): a split's personal best (the time at its end) and its best segment, seconds, -1 for none\n";
	f << "shown " << (m_Shown ? 1 : 0) << "\n";
	f << "at " << m_X << " " << m_Y << "\n";
	f << "attempts " << m_Attempts << "\n";
	char text[96];
	for (int i = 0; i < kSplits; ++i)
	{
		std::snprintf(text, sizeof text, "split %s %.3f %.3f\n", kMissions[i].name, m_Pb[i], m_Best[i]);
		f << text;
	}
}

void EnsureLoaded()
{
	if (!m_Loaded) Load();
}

void ResetRun()
{
	m_Running = m_Finished = m_NewPb = false;
	m_Index = 0;
	for (int i = 0; i < kSplits; ++i)
	{
		m_Segment[i] = 0;
		m_Gold[i] = false;
	}
	std::copy(m_Pb, m_Pb + kSplits, m_PbBefore);
}

void StartRun()
{
	ResetRun();
	m_Running = true;
	++m_Attempts;
	rt_log("goldeneye: speedrun: attempt %d started", m_Attempts);
	Save();
}

void Split()
{
	int i = m_Index;
	float done = 0;
	for (int s = 0; s <= i; ++s) done += m_Segment[s];
	if (m_Best[i] < 0 || m_Segment[i] < m_Best[i])
	{
		m_Best[i] = m_Segment[i];
		m_Gold[i] = true;
	}
	rt_log("goldeneye: speedrun: %s split at %.3f (segment %.3f%s)", kMissions[i].name, done, m_Segment[i], m_Gold[i] ? ", best" : "");
	if (++m_Index == kSplits)
	{
		m_Running = false;
		m_Finished = true;
		if (m_Pb[kSplits - 1] < 0 || done < m_Pb[kSplits - 1])
		{
			m_NewPb = true;
			float at = 0;
			for (int s = 0; s < kSplits; ++s) m_Pb[s] = at += m_Segment[s];
			rt_log("goldeneye: speedrun: a new personal best, %.3f", done);
		}
	}
	Save();
}

} //namespace

void speedrun_frame()
{
	float t = ldf(kLevelTimer);
	uint32_t level = ld32(kCurrentLevel);
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	bool campaign = (level & kLevelKind) == kCampaignLevel;
	if (m_Running && campaign && level == m_LastLevel && guest_ptr(ld32(kPlayerPtr)) && ld8(kPaused) == 0)
	{
		float step = t >= m_LastTime ? t - m_LastTime : 0;
		if (step < kMaxStep) m_Segment[m_Index] += step;
	}
	m_LastLevel = level;
	m_LastTime = t;
}

void speedrun_level_change(uint32_t from, uint32_t to, uint32_t entrance, bool next)
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (from == kFrontEnd && to == kDam && entrance == kMissionStart) StartRun();
	else if (m_Running && next && MissionOf(from) == m_Index && MissionOf(to) != m_Index && (entrance == kMissionStart || to == kFrontEnd)) Split();
}

bool speedrun_timer()
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	return m_Shown;
}

void set_speedrun_timer(bool on)
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (m_Shown == on) return;
	m_Shown = on;
	Save();
}

void speedrun_reset()
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	ResetRun();
}

void speedrun_clear_best()
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	Clear();
	m_Attempts = 0;
	ResetRun();
	Save();
}

#ifdef GE_IMGUI
namespace
{

const ImU32 kCream = IM_COL32(243, 232, 208, 255), kBlue = IM_COL32(88, 124, 170, 255),
	kBar = IM_COL32(70, 100, 146, 150), kSelected = IM_COL32(255, 255, 255, 245),
	kSelectedText = IM_COL32(84, 146, 212, 255), kPanel = IM_COL32(6, 14, 18, 205), kGrid = IM_COL32(90, 150, 150, 22),
	kDark = IM_COL32(26, 32, 40, 255), kAhead = IM_COL32(92, 214, 112, 255), kBehind = IM_COL32(232, 76, 62, 255),
	kGoldColour = IM_COL32(255, 204, 64, 255);
const float kWidth = 330, kHead = 46, kPitch = 25, kRow = 23, kFoot = 92;

ImVec2 g_box0, g_box1; //where it was drawn, in the window's pixels
std::atomic<bool> g_dragging{ false }; //pointer.cpp asks from the game thread
ImVec2 g_grab; //the cursor's place in the box as it was taken

ImU32 Fade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a) << IM_COL32_A_SHIFT); }

//1:23.45, 1:02:03.45 past an hour, 12.34 under a minute
std::string Clock(float s, int decimals = 2)
{
	s = std::max(s, 0.0f);
	int whole = (int)s, h = whole / 3600, m = whole / 60 % 60;
	float sec = s - (float)(h * 3600 + m * 60);
	char text[32];
	if (h) std::snprintf(text, sizeof text, "%d:%02d:%0*.*f", h, m, decimals + 3, decimals, sec);
	else if (m) std::snprintf(text, sizeof text, "%d:%0*.*f", m, decimals + 3, decimals, sec);
	else std::snprintf(text, sizeof text, "%.*f", decimals, sec);
	return text;
}

std::string Delta(float d)
{
	return (d < 0 ? "-" : "+") + Clock(std::fabs(d), std::fabs(d) < 60 ? 1 : 0);
}

void RightText(ImDrawList* dl, float right, float y, float size, ImU32 colour, const std::string& s, bool shadow = true)
{
	menu_text(dl, ImVec2(right - menu_text_width(s, size), y), size, colour, s, shadow);
}

float Scale()
{
	ImVec2 screen = ImGui::GetIO().DisplaySize;
	return std::max(0.75f, std::min(screen.y / 720, screen.x / 960));
}

} //namespace

bool speedrun_event(const SDL_Event* e)
{
	if (!speedrun_timer()) return false;
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
		if (!g_dragging || e->button.button != SDL_BUTTON_LEFT) return false;
		g_dragging = false;
		{
			std::lock_guard<std::mutex> hold(g_lock);
			Save();
		}
		return true;
	default:
		return false;
	}
}

bool speedrun_dragging() { return g_dragging; }

void speedrun_draw()
{
	if (!menu_font_ready() || pictures_hiding()) return;
	std::unique_lock<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (!m_Shown) return;
	float u = Scale();
	ImVec2 screen = ImGui::GetIO().DisplaySize;
	float w = kWidth * u, h = (kHead + kPitch * (float)kSplits + kFoot) * u;
	float x = m_X < 0 ? screen.x - w - 16 * u : m_X * screen.x, y = m_Y * screen.y;
	x = std::clamp(x, 0.0f, std::max(0.0f, screen.x - w));
	y = std::clamp(y, 0.0f, std::max(0.0f, screen.y - h));
	g_box0 = ImVec2(x, y);
	g_box1 = ImVec2(x + w, y + h);
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	//the menus' dark screen with its faint grid, and a cream edge
	dl->AddRectFilled(g_box0, g_box1, kPanel, 4 * u);
	dl->PushClipRect(g_box0, g_box1, true);
	for (float gx = x + 32 * u; gx < x + w; gx += 32 * u) dl->AddLine(ImVec2(gx, y), ImVec2(gx, y + h), kGrid, u);
	for (float gy = y + 32 * u; gy < y + h; gy += 32 * u) dl->AddLine(ImVec2(x, gy), ImVec2(x + w, gy), kGrid, u);
	dl->PopClipRect();
	dl->AddRect(g_box0, g_box1, Fade(kCream, g_dragging ? 0.9f : 0.35f), 4 * u, 0, 1.5f * u);

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
		std::string time = done || current ? Clock(at) : m_PbBefore[i] >= 0 ? Clock(m_PbBefore[i]) : "-";
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
	RightText(dl, x + w - 14 * u, fy + 6 * u, 52 * u, clock, Clock(total));
	menu_text(dl, ImVec2(x + 16 * u, fy + 14 * u), 18 * u, Fade(kCream, 0.85f), "Sum of Best", true);
	menu_text(dl, ImVec2(x + 16 * u, fy + 34 * u), 20 * u, kCream, haveSum ? Clock(sumBest) : "-", true);
	const char* state = m_Finished ? (m_NewPb ? "New personal best!" : "Run finished") : m_Running ? "In-game time" : "Pick Dam to start";
	menu_text(dl, ImVec2(x + 16 * u, fy + 62 * u), 18 * u, Fade(kCream, 0.7f), state, true);
}
#endif

} //namespace ge
