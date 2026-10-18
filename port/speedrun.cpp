//the speedrun timer: a splits window like LiveSplit's, timed by the game's own level timer
//
//a run is the campaign from Dam to Cradle, a split a mission. it starts when Dam is picked in
//the menu (a level change from the front end to Dam's entrance 1) and each split ends with the
//app's update loading what comes after a won mission (StartLevelChange from 80009334: the next
//mission's entrance 1, or the front end for "Save and Quit" and after Cradle's credits). a quit
//from the pause menu (802354D8), a restart (802356D8) or a checkpoint's reload don't split
//with Reset On Save And Quit a save the menu made (ge_save_write, not a checkpoint's) followed by
//a change to the front end that isn't a split resets the run
//
//what's counted is the level timer (805C963C) going up while there is a player and the game
//isn't paused: every attempt of a level, the deaths and the cutscenes, never the menus or the
//loads. a checkpoint's reload sets the timer back, that drop isn't counted either way
//
//like LiveSplit: the personal best's time at the end of each split, the best segment of each,
//the difference to the personal best as a split ends (green ahead, red behind, gold for a best
//segment), the sum of best. a finished run faster than the personal best becomes it
//picking Dam in the menu again starts a new attempt, the F1 menu's Speedrun Timer page resets
//(Reset Run can be a hotkey too). the splits go out and come in as LiveSplit's .lss, in game time
//
//the window is dragged with the mouse wherever the cursor is free (menus, a pause, F1). let go
//outside the game's window it gets a window of its own (SDL's software renderer and a second
//ImGui context, the game's GL context isn't touched), closing that one puts it back
//the splits, the window's place and whether it's shown are in speedrun.txt, next to the settings
//GE_SPEEDRUN=F that file instead
//GE_SPEEDRUN_SHOT=F.bmp the own window's picture, once, for looking at it in a hidden run
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
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
#include "imgui_impl_sdlrenderer3.h"
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
ImFont* menu_font_add(ImFontAtlas* atlas); //the game's font in another context's atlas
void menu_font_select(ImFont* font); //what menu_text draws with, nullptr the menu's own
#endif

namespace
{

using Clock = std::chrono::steady_clock;
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
float m_X{ -1 }, m_Y{ 0.06f }; //the box's top left corner in the game's window, a part of it. -1: against the right edge
bool m_Popped{ false }; //in a window of its own
int m_WinX{ INT_MIN }, m_WinY{ INT_MIN }, m_WinW{ 0 }, m_WinH{ 0 }; //that window, on the desktop
bool m_QuitReset{ false };
bool m_MenuSaved{ false }; //the menu saved the game in this level
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
std::string m_Said; //what an import or export came to
Clock::time_point m_SaidUntil{};

std::string FilePath()
{
	if (const char* e = std::getenv("GE_SPEEDRUN")) return e;
	return std::filesystem::path(GE_SETTINGS_FILE).replace_filename("speedrun.txt").string();
}

void Clear()
{
	for (int i = 0; i < kSplits; ++i) m_Pb[i] = m_Best[i] = m_PbBefore[i] = -1;
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

int SplitNamed(std::string name)
{
	for (char& c : name) c = (char)std::tolower((unsigned char)c);
	for (int i = 0; i < kSplits; ++i)
	{
		std::string mine = kMissions[i].name;
		for (char& c : mine) c = (char)std::tolower((unsigned char)c);
		if (mine == name) return i;
	}
	return -1;
}

//our own file, all of it or (splitsOnly, an import) only the splits and the attempts. how many splits it had
int ReadOwn(std::istream& f, bool splitsOnly)
{
	int read = 0;
	std::string line;
	while (std::getline(f, line))
	{
		std::istringstream in(line);
		std::string key;
		in >> key;
		if (key == "attempts") in >> m_Attempts;
		else if (key == "split")
		{
			std::string name;
			float pb = -1, best = -1;
			in >> name >> pb >> best;
			if (int i = SplitNamed(name); i >= 0)
			{
				m_Pb[i] = pb;
				m_Best[i] = best;
				++read;
			}
		}
		else if (splitsOnly) continue;
		else if (key == "shown") in >> m_Shown;
		else if (key == "at") in >> m_X >> m_Y;
		else if (key == "window") in >> m_Popped >> m_WinX >> m_WinY >> m_WinW >> m_WinH;
		else if (key == "quit_reset") in >> m_QuitReset;
	}
	return read;
}

void Load()
{
	Clear();
	m_Loaded = true;
	std::ifstream f(FilePath());
	ReadOwn(f, false);
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
	f << "window " << (m_Popped ? 1 : 0) << " " << m_WinX << " " << m_WinY << " " << m_WinW << " " << m_WinH << "\n";
	f << "quit_reset " << (m_QuitReset ? 1 : 0) << "\n";
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

void Say(const std::string& text)
{
	m_Said = text;
	m_SaidUntil = Clock::now() + std::chrono::seconds(4);
	rt_log("goldeneye: speedrun: %s", text.c_str());
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

//LiveSplit's times: [d.]hh:mm:ss.fffffff
std::string LssTime(float seconds)
{
	double t = seconds;
	int h = (int)(t / 3600), m = (int)(t / 60) % 60;
	char text[32];
	std::snprintf(text, sizeof text, "%02d:%02d:%010.7f", h, m, t - h * 3600.0 - m * 60.0);
	return text;
}

float ReadLssTime(const std::string& text)
{
	int days = 0, h = 0, m = 0;
	double sec = 0;
	std::string t = text;
	size_t colon = t.find(':');
	if (colon == std::string::npos) return -1;
	if (size_t dot = t.find('.'); dot < colon)
	{
		days = std::atoi(t.substr(0, dot).c_str());
		t = t.substr(dot + 1);
	}
	if (std::sscanf(t.c_str(), "%d:%d:%lf", &h, &m, &sec) != 3) return -1;
	return (float)(days * 86400.0 + h * 3600.0 + m * 60.0 + sec);
}

//what's between <tag> and </tag>, empty for none
std::string Tag(const std::string& s, const char* tag)
{
	std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
	size_t a = s.find(open);
	if (a == std::string::npos) return "";
	a += open.size();
	size_t b = s.find(close, a);
	return b == std::string::npos ? "" : s.substr(a, b - a);
}

//an element that starts at the text "start" (<BestSegmentTime, or the Personal Best's SplitTime):
//its game time, its real time without one, -1 for neither or when it's empty
float TimeOf(const std::string& s, const std::string& start, const char* close)
{
	size_t a = s.find(start);
	if (a == std::string::npos) return -1;
	size_t gt = s.find('>', a);
	if (gt == std::string::npos || s[gt - 1] == '/') return -1;
	size_t b = s.find(close, gt);
	std::string inside = s.substr(gt + 1, b == std::string::npos ? std::string::npos : b - gt - 1);
	std::string game = Tag(inside, "GameTime");
	return ReadLssTime(game.empty() ? Tag(inside, "RealTime") : game);
}

int ReadLss(const std::string& s)
{
	struct Seg
	{
		std::string name;
		float pb, best;
	};
	std::vector<Seg> segs;
	for (size_t at = s.find("<Segment>"); at != std::string::npos; at = s.find("<Segment>", at + 1))
	{
		size_t end = s.find("</Segment>", at);
		if (end == std::string::npos) break;
		std::string seg = s.substr(at, end - at);
		segs.push_back({ Tag(seg, "Name"), TimeOf(seg, "name=\"Personal Best\"", "</SplitTime>"), TimeOf(seg, "<BestSegmentTime", "</BestSegmentTime>") });
	}
	bool byName = false;
	for (const Seg& g : segs) byName |= SplitNamed(g.name) >= 0;
	if (!byName && (int)segs.size() != kSplits) return 0;
	Clear();
	int read = 0;
	for (size_t n = 0; n < segs.size(); ++n)
	{
		int i = byName ? SplitNamed(segs[n].name) : (int)n;
		if (i < 0) continue;
		m_Pb[i] = segs[n].pb;
		m_Best[i] = segs[n].best;
		++read;
	}
	if (std::string count = Tag(s, "AttemptCount"); !count.empty()) m_Attempts = std::atoi(count.c_str());
	return read;
}

std::string WriteLss()
{
	std::ostringstream f;
	f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Run version=\"1.7.0\">\n  <GameIcon />\n"
	  << "  <GameName>GoldenEye 007 (Wii)</GameName>\n  <CategoryName>Any% IGT</CategoryName>\n"
	  << "  <Metadata>\n    <Run id=\"\" />\n    <Platform usesEmulator=\"False\">Wii</Platform>\n    <Region>\n    </Region>\n    <Variables />\n  </Metadata>\n"
	  << "  <Offset>00:00:00</Offset>\n  <AttemptCount>" << m_Attempts << "</AttemptCount>\n  <AttemptHistory />\n  <Segments>\n";
	for (int i = 0; i < kSplits; ++i)
	{
		f << "    <Segment>\n      <Name>" << kMissions[i].name << "</Name>\n      <Icon />\n      <SplitTimes>\n";
		if (m_Pb[i] >= 0) f << "        <SplitTime name=\"Personal Best\">\n          <GameTime>" << LssTime(m_Pb[i]) << "</GameTime>\n        </SplitTime>\n";
		else f << "        <SplitTime name=\"Personal Best\" />\n";
		f << "      </SplitTimes>\n";
		if (m_Best[i] >= 0) f << "      <BestSegmentTime>\n        <GameTime>" << LssTime(m_Best[i]) << "</GameTime>\n      </BestSegmentTime>\n";
		else f << "      <BestSegmentTime />\n";
		f << "      <SegmentHistory />\n    </Segment>\n";
	}
	f << "  </Segments>\n  <AutoSplitterSettings />\n</Run>\n";
	return f.str();
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
	else if (m_Running && m_QuitReset && m_MenuSaved && to == kFrontEnd && (from & kLevelKind) == kCampaignLevel)
	{
		ResetRun();
		rt_log("goldeneye: speedrun: Save and Quit, the run is reset");
	}
	m_MenuSaved = false;
}

void speedrun_menu_save()
{
	std::lock_guard<std::mutex> hold(g_lock);
	m_MenuSaved = true;
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

bool speedrun_window()
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	return m_Popped;
}

void set_speedrun_window(bool on)
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (m_Popped == on) return;
	m_Popped = on;
	if (on) m_Shown = true;
	Save();
}

bool speedrun_quit_reset()
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	return m_QuitReset;
}

void set_speedrun_quit_reset(bool on)
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (m_QuitReset == on) return;
	m_QuitReset = on;
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

std::string speedrun_said()
{
	std::lock_guard<std::mutex> hold(g_lock);
	return Clock::now() < m_SaidUntil ? m_Said : std::string();
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
const SDL_DialogFileFilter kFilters[] = { { "LiveSplit splits", "lss" }, { "The timer's own file", "txt" } };

ImVec2 g_box0, g_box1; //where it was drawn in the game's window
std::atomic<bool> g_dragging{ false }; //pointer.cpp asks from the game thread
ImVec2 g_grab; //the cursor's place in the box as it was taken

//its own window
SDL_Window* g_win = nullptr;
SDL_Renderer* g_ren = nullptr;
ImGuiContext* g_ctx = nullptr;
ImFont* g_popFont = nullptr;
Clock::time_point g_popDrawn{};

ImU32 Fade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a) << IM_COL32_A_SHIFT); }

//1:23.45, 1:02:03.45 past an hour, 12.34 under a minute
std::string Hms(float s, int decimals = 2)
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
	return (d < 0 ? "-" : "+") + Hms(std::fabs(d), std::fabs(d) < 60 ? 1 : 0);
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

//the game's GL context stays the current one around anything done to the own window: the
//software renderer's window surface can be a GL renderer's of its own (Wayland's always is),
//and the game's draws went to that one's context and crashed in the driver
struct KeepGl
{
	SDL_Window* m_pWindow{ SDL_GL_GetCurrentWindow() };
	SDL_GLContext m_Context{ SDL_GL_GetCurrentContext() };
	~KeepGl()
	{
		if (m_pWindow && m_Context && SDL_GL_GetCurrentContext() != m_Context) SDL_GL_MakeCurrent(m_pWindow, m_Context);
	}
};

void OpenPop()
{
	KeepGl keep;
	int w = m_WinW > 0 ? m_WinW : (int)kWidth, h = m_WinH > 0 ? m_WinH : (int)BoxHeight(1);
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (std::getenv("WIIKIT_HIDDEN") ? SDL_WINDOW_HIDDEN : 0);
	g_win = SDL_CreateWindow("Speedrun Timer", w, h, flags);
	if (!g_win)
	{
		rt_log("goldeneye: speedrun: no window of its own: %s", SDL_GetError());
		m_Popped = false;
		return;
	}
	if (m_WinX != INT_MIN && m_WinY != INT_MIN) SDL_SetWindowPosition(g_win, m_WinX, m_WinY);
	window_icon(g_win);
	g_ren = SDL_CreateRenderer(g_win, "software");
	if (!g_ren)
	{
		rt_log("goldeneye: speedrun: its window has no renderer: %s", SDL_GetError());
		SDL_DestroyWindow(g_win);
		g_win = nullptr;
		m_Popped = false;
		return;
	}
	ImGuiContext* game = ImGui::GetCurrentContext();
	g_ctx = ImGui::CreateContext();
	ImGui::SetCurrentContext(g_ctx);
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().Fonts->AddFontDefault();
	g_popFont = menu_font_add(ImGui::GetIO().Fonts);
	ImGui_ImplSDLRenderer3_Init(g_ren);
	ImGui::SetCurrentContext(game);
}

void ClosePop()
{
	KeepGl keep;
	ImGuiContext* game = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(g_ctx);
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui::DestroyContext(g_ctx);
	ImGui::SetCurrentContext(game);
	SDL_DestroyRenderer(g_ren);
	SDL_DestroyWindow(g_win);
	g_ctx = nullptr;
	g_ren = nullptr;
	g_win = nullptr;
	g_popFont = nullptr;
}

void DrawPop()
{
	auto now = Clock::now();
	if (now - g_popDrawn < std::chrono::milliseconds(16)) return; //60 a second is plenty for a clock
	g_popDrawn = now;
	KeepGl keep;
	ImGuiContext* game = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(g_ctx);
	int pw = 0, ph = 0;
	SDL_GetCurrentRenderOutputSize(g_ren, &pw, &ph);
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)std::max(pw, 1), (float)std::max(ph, 1));
	io.DeltaTime = 1.0f / 60;
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui::NewFrame();
	float u = std::min(io.DisplaySize.x / kWidth, io.DisplaySize.y / BoxHeight(1));
	menu_font_select(g_popFont);
	DrawBox(ImGui::GetForegroundDrawList(), std::floor((io.DisplaySize.x - kWidth * u) / 2), std::floor((io.DisplaySize.y - BoxHeight(u)) / 2), u);
	menu_font_select(nullptr);
	ImGui::Render();
	SDL_SetRenderDrawColor(g_ren, 6, 14, 18, 255);
	SDL_RenderClear(g_ren);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_ren);
	static bool shot = false;
	if (const char* path = std::getenv("GE_SPEEDRUN_SHOT"); path && !shot)
	{
		if (SDL_Surface* s = SDL_RenderReadPixels(g_ren, nullptr))
		{
			shot = SDL_SaveBMP(s, path);
			SDL_DestroySurface(s);
		}
	}
	SDL_RenderPresent(g_ren);
	ImGui::SetCurrentContext(game);
}

//the window an event is for, 0 for one that isn't any window's
SDL_WindowID EventWindow(const SDL_Event* e)
{
	if (e->type >= SDL_EVENT_WINDOW_FIRST && e->type <= SDL_EVENT_WINDOW_LAST) return e->window.windowID;
	switch (e->type)
	{
	case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return e->key.windowID;
	case SDL_EVENT_TEXT_INPUT: return e->text.windowID;
	case SDL_EVENT_MOUSE_MOTION: return e->motion.windowID;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: return e->button.windowID;
	case SDL_EVENT_MOUSE_WHEEL: return e->wheel.windowID;
	default: return 0;
	}
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
	if (g_win && EventWindow(e) == SDL_GetWindowID(g_win)) return PopEvent(e);
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
		if (want && !g_win && menu_font_ready()) OpenPop();
	}
	if (!want && g_win) ClosePop();
	if (!g_win) return;
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
	DrawBox(ImGui::GetForegroundDrawList(), x, y, u);
}
#endif

} //namespace ge
