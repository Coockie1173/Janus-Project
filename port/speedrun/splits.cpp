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
//what's counted is the level timer (805C963C) going up while there is a player: every attempt of
//a level, the deaths and the cutscenes, never the menus or the loads. the pause menu counts too,
//by the clock, as the level timer stands still under it. a checkpoint's reload sets the timer
//back, that drop isn't counted either way. between missions it's the clock too: from the split
//through the Mission Complete page and the load until the next level has a player, all of it the
//next mission's. the game changes level before the page comes up, so the split is what starts it
//
//like LiveSplit: the personal best's time at the end of each split, the best segment of each,
//the difference to the personal best as a split ends (green ahead, red behind, gold for a best
//segment), the sum of best. a finished run faster than the personal best becomes it
//picking Dam in the menu again starts a new attempt, the F1 menu's Speedrun Timer page resets
//(Reset Run can be a hotkey too). the splits go out and come in as LiveSplit's .lss, in game time
//
//the window is dragged with the mouse wherever the cursor is free (menus, a pause, F1). let go
//outside the game's window it gets a window of its own (a second ImGui context, drawn by a thread
//with a GL context of its own), closing that one puts it back
//the splits, the window's place and whether it's shown are in speedrun.txt, next to the settings
//GE_SPEEDRUN=F that file instead
//GE_SPEEDRUN_SHOT=F.bmp the own window's picture, once, for looking at it in a hidden run
#include "speedrun/splits.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ge::speedrun
{

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
	m_Running = m_Finished = m_NewPb = m_Between = false;
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
		else if (key == "opacity") in >> m_Opacity;
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
	f << "opacity " << m_Opacity << "\n";
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

} //namespace ge::speedrun

namespace ge
{

using namespace speedrun;

void speedrun_frame()
{
	float t = ldf(kLevelTimer);
	uint32_t level = ld32(kCurrentLevel);
	auto now = Clock::now();
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	bool campaign = (level & kLevelKind) == kCampaignLevel;
	bool player = guest_ptr(ld32(kPlayerPtr));
	//the new level is in once it has a player and its own timer runs from the start. the old one
	//can still be the current level for a while after the change
	if (m_Between && level == m_ChangeTo && campaign && player && t > m_LastTime && t < kFreshTimer)
	{
		m_Between = false;
		rt_log("goldeneye: speedrun: %s begins, %.3f into its split", kMissions[m_Index].name, m_Segment[m_Index]);
	}
	if (m_Running && m_Between) m_Segment[m_Index] += std::chrono::duration<float>(now - m_LastClock).count();
	else if (m_Running && campaign && level == m_LastLevel && player)
	{
		//paused the level timer stands still, the clock goes on
		float step = ld8(kPaused) != 0 ? std::chrono::duration<float>(now - m_LastClock).count() : t >= m_LastTime ? t - m_LastTime : 0;
		if (step < kMaxStep) m_Segment[m_Index] += step;
	}
	m_LastLevel = level;
	m_LastTime = t;
	m_LastClock = now;
}

void speedrun_level_change(uint32_t from, uint32_t to, uint32_t entrance, bool next)
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (from == kFrontEnd && to == kDam && entrance == kMissionStart) StartRun();
	else if (m_Running && next && MissionOf(from) == m_Index && MissionOf(to) != m_Index && (entrance == kMissionStart || to == kFrontEnd))
	{
		Split();
		m_Between = m_Running; //the clock counts until the next mission begins
		m_LastClock = Clock::now();
	}
	else if (m_Running && m_QuitReset && m_MenuSaved && to == kFrontEnd && (from & kLevelKind) == kCampaignLevel)
	{
		ResetRun();
		rt_log("goldeneye: speedrun: Save and Quit, the run is reset");
	}
	m_ChangeTo = to;
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

float speedrun_opacity()
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	return m_Opacity;
}

void set_speedrun_opacity(float percent)
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	percent = std::clamp(percent, kMinOpacity, 100.0f);
	if (m_Opacity == percent) return;
	m_Opacity = percent;
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

} //namespace ge
