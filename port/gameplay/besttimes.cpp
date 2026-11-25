//best times: the fastest won mission a level and difficulty (Operative, Agent, 007, 007 Classic), and
//with bunnyhopping or without, shown on the Mission Complete page right of the mission time
//
//the page (ge_mission_page) knows the level that ended (805D1808), whether it was won (page +0x18
//is 1), the difficulty (page +0x1A, 0 to 3) and shows the level timer (805C963C) as the mission time.
//in Time Trial (DisplayLevelTimer 805E05AB) that row is the game's own best, 007 Custom isn't kept
//
//a time only counts when no cheat was on at any point of the attempt (cheats_active, every frame).
//an attempt starts with a level change to a mission's start (a pick in the menu, a restart, Save and
//Continue's next mission) once the level timer has gone back to 0: the game changes level before the
//page comes up, with the won level's time still on the timer. a checkpoint's reload isn't a new attempt
//
//bunnyhopping on at any point of the attempt (bunnyhop_now) makes it a bhop time, kept apart
//
//kept in besttimes.txt next to the settings, a line each: level, difficulty, bhop, seconds
//GE_BESTTIMES=F that file instead
#include "ge.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace ge
{
namespace
{

const uint32_t kLastLevel = 0x805D1808, kLevelTimer = 0x805C963C, kTimeTrial = 0x805E05AB;
const uint32_t kPageWon = 0x18, kPageDifficulty = 0x1A;
const uint32_t kDifficulties = 4; //Operative, Agent, 007, 007 Classic
const uint32_t kMissionStart = 1;
const float kNewAttempt = 1.0f; //the timer under this after a mission's start: it's counting a new attempt

std::map<uint64_t, float> m_Best; //level << 32 | bhop << 8 | difficulty -> seconds
bool m_Loaded{};
bool m_Cheated{}; //a cheat was on in this attempt
bool m_Hopped{}; //bunnyhopping was on in this attempt
bool m_Pending{}; //a mission's start was asked for, the attempt begins when the timer is back at 0
bool m_Recorded{}; //this attempt's page was looked at already
bool m_Shown{}; //what the page shows: a best time, or that the cheats kept it from being saved
bool m_ShownCheated{};
bool m_ShownHopped{};
float m_ShownTime{};

uint64_t Key(uint32_t level, uint32_t difficulty, bool bhop) { return (uint64_t)level << 32 | (bhop ? 0x100u : 0u) | difficulty; }

std::string FilePath()
{
	if (const char* e = std::getenv("GE_BESTTIMES")) return e;
	return std::filesystem::path(GE_SETTINGS_FILE).replace_filename("besttimes.txt").string();
}

void Load()
{
	m_Loaded = true;
	std::ifstream f(FilePath());
	std::string line;
	while (std::getline(f, line))
	{
		unsigned level{}, difficulty{}, bhop{};
		float seconds{};
		if (line.empty() || line[0] == '#') continue;
		if (std::sscanf(line.c_str(), "%x %u %u %f", &level, &difficulty, &bhop, &seconds) == 4 && difficulty < kDifficulties && seconds > 0) m_Best[Key(level, difficulty, bhop != 0)] = seconds;
	}
}

void Save()
{
	std::ofstream f(FilePath());
	if (!f)
	{
		rt_log("goldeneye: best times not saved: cannot write %s", FilePath().c_str());
		return;
	}
	f << "# the fastest won mission without cheats: level, difficulty (0 Operative, 1 Agent, 2 007, 3 007 Classic), bhop (1 bunnyhopping), seconds\n";
	char text[64];
	for (const auto& [key, seconds] : m_Best)
	{
		std::snprintf(text, sizeof text, "%08X %u %u %.3f\n", (uint32_t)(key >> 32), (uint32_t)key & 0xFF, (uint32_t)key >> 8 & 1, seconds);
		f << text;
	}
}

//the game's way: hours, minutes, seconds, cut down
std::string Clock(float seconds)
{
	const int whole = (int)seconds;
	char text[32];
	std::snprintf(text, sizeof text, "%d:%02d:%02d", whole / 3600, whole / 60 % 60, whole % 60);
	return text;
}

} //namespace

void best_times_frame()
{
	if (m_Pending && ldf(kLevelTimer) < kNewAttempt)
	{
		m_Pending = false;
		m_Cheated = false;
		m_Hopped = false;
		m_Recorded = false;
	}
	if (cheats_active()) m_Cheated = true;
	if (bunnyhop_now()) m_Hopped = true;
}

void best_times_level_change(uint32_t entrance)
{
	if (entrance <= kMissionStart) m_Pending = true;
}

void best_times_page(uint32_t page)
{
	m_Shown = false;
	if (!guest_ptr(page) || multiplayer_game() || custom_mode() || ld8(kTimeTrial)) return;
	const uint32_t level = ld32(kLastLevel), difficulty = ld16(page + kPageDifficulty);
	if (difficulty >= kDifficulties) return;
	if (!m_Loaded) Load();
	const auto key = Key(level, difficulty, m_Hopped);
	if (ld8(page + kPageWon) == 1 && !m_Recorded && !m_Cheated)
	{
		const float time = ldf(kLevelTimer);
		const auto best = m_Best.find(key);
		const float before = best == m_Best.end() ? -1.0f : best->second;
		if (time > 0 && (before < 0 || time < before))
		{
			m_Best[key] = time;
			Save();
			rt_log("goldeneye: best time: level %08X difficulty %u%s %.3f s, was %.3f", level, difficulty, m_Hopped ? " bhop" : "", time, before);
		}
	}
	const bool won = ld8(page + kPageWon) == 1;
	if (won) m_Recorded = true;
	const auto best = m_Best.find(key);
	m_ShownCheated = won && m_Cheated;
	m_ShownHopped = m_Hopped;
	m_ShownTime = best == m_Best.end() ? 0.0f : best->second;
	m_Shown = m_ShownCheated || best != m_Best.end();
}

std::string best_time_text()
{
	if (!m_Shown) return {};
	if (m_ShownCheated) return "Cheats Used - Not Saved";
	return (m_ShownHopped ? "Best Bhop Time - " : "Best Time - ") + Clock(m_ShownTime);
}

} //namespace ge
