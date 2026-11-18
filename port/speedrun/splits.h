//the speedrun timer's insides, shared by its files:
//splits.cpp the run, its splits and the timer's own file
//lss.cpp LiveSplit's .lss, in and out
//window.cpp the splits drawn, in the game's window or one of their own
#pragma once
#include "ge.h"
#include <chrono>
#include <climits>
#include <istream>
#include <iterator>
#include <mutex>
#include <string>

#ifdef GE_IMGUI
#include "imgui.h"
#endif

#ifndef GE_SETTINGS_FILE
#define GE_SETTINGS_FILE "settings.env"
#endif

namespace ge
{

#ifdef GE_IMGUI
void menu_text(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const std::string& s, bool shadow); //ui/settings/draw.cpp, the game's font
float menu_text_width(const std::string& s, float size);
bool menu_font_ready();
ImFont* menu_font_add(ImFontAtlas* atlas); //the game's font in another context's atlas
void menu_font_select(ImFont* font); //what menu_text draws with, nullptr the menu's own
#endif

} //namespace ge

namespace ge::speedrun
{


using Clock = std::chrono::steady_clock;
const uint32_t kCurrentLevel = 0x805D1804;
const uint32_t kLevelTimer = 0x805C963C; //a float, seconds played in the level
const uint32_t kLevelKind = 0xFFFF0000, kCampaignLevel = 0x01810000;
const uint32_t kFrontEnd = 0x01080004;
const uint32_t kDam = 0x0181013F;
const uint32_t kMissionStart = 1;
const float kFreshTimer = 5.0f; //a level timer under this is a level just begun
const float kMaxStep = 1.0f; //more than this in a frame is a jump (a checkpoint), not time played

struct Mission
{
	const char* name;
	uint32_t levels[2];
};
inline const Mission kMissions[] = {
	{"Dam", {0x0181013F}}, {"Facility", {0x0181014A}}, {"Airfield", {0x0181013C}}, {"Nightclub", {0x01810144}},
	{"Carrier", {0x01810147}}, {"Outpost", {0x01810150}}, {"Bunker", {0x0181014F}}, {"Archives", {0x01810146}},
	{"Tank", {0x01810145}}, {"Station", {0x01810151}}, {"Memorial", {0x0181014E}}, {"Jungle", {0x01810149}},
	{"Solar", {0x0181014D}}, {"Cradle", {0x0181014B, 0x01810169}}};
const int kSplits = (int)std::size(kMissions);
int MissionOf(uint32_t level);

//everything below is the game thread's and the window's, under the lock
inline std::mutex g_lock;
inline bool m_Shown{ false };
inline float m_X{ -1 }, m_Y{ 0.06f }; //the box's top left corner in the game's window, a part of it. -1: against the right edge
inline bool m_Popped{ false }; //in a window of its own
inline int m_WinX{ INT_MIN }, m_WinY{ INT_MIN }, m_WinW{ 0 }, m_WinH{ 0 }; //that window, on the desktop
inline bool m_QuitReset{ false };
const float kMinOpacity = 10;
inline float m_Opacity{ 100 }; //how see-through the box is over the game, in percent
inline bool m_MenuSaved{ false }; //the menu saved the game in this level
inline int m_Attempts{ 0 };
inline float m_Pb[kSplits]; //the time at the end of each split in the personal best, -1 none
inline float m_Best[kSplits]; //the best segment, -1 none
inline float m_Segment[kSplits]{}; //this run's
inline bool m_Gold[kSplits]{}; //the segment was a best one as it ended
inline float m_PbBefore[kSplits]; //the personal best this run is held against, kept when it becomes the new one
inline bool m_Running{ false }, m_Finished{ false }, m_NewPb{ false };
inline int m_Index{ 0 }; //the split being run
inline bool m_Between{ false }; //the mission was won, the clock counts until the next level has a player
inline uint32_t m_ChangeTo{ 0 }; //the level the last level change went to
inline uint32_t m_LastLevel{ 0 };
inline float m_LastTime{ 0 };
inline Clock::time_point m_LastClock{};
inline bool m_Loaded{ false };
inline std::string m_Said; //what an import or export came to
inline Clock::time_point m_SaidUntil{};

//splits.cpp
std::string FilePath();
void Clear();
void ResetRun();
int SplitNamed(std::string name);
int ReadOwn(std::istream& f, bool splitsOnly); //our own file, all of it or (splitsOnly, an import) only the splits and the attempts. how many splits it had
void Load();
void Save();
void EnsureLoaded();
void Say(const std::string& text);
void StartRun();
void Split();

//lss.cpp
int ReadLss(const std::string& s); //how many splits it had
std::string WriteLss();

} //namespace ge::speedrun
