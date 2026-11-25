//best times: the fastest won mission a level and difficulty (Operative, Agent, 007, 007 Classic, and
//007 Custom set as LTK or DLTK), with bunnyhopping or without, shown on the Mission Complete page right
//of the mission time
//
//the page (ge_mission_page) knows the level that ended (805D1808), whether it was won (page +0x18
//is 1), the difficulty (page +0x1A, 0 to 3) and shows the level timer (805C963C) as the mission time.
//in Time Trial (DisplayLevelTimer 805E05AB) that row is the game's own best. 007 Custom plays as 007
//Classic, it's only kept with the sliders at LTK's or DLTK's (kLtk, kDltk) the whole attempt
//
//a time only counts when no cheat was on at any point of the attempt (cheats_active, every frame),
//TAS mode wasn't on (lib_steering) and bunnyhopping kept its default rules (bhop_custom_now).
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
const uint32_t kDifficulties = 6; //Operative, Agent, 007, 007 Classic, LTK, DLTK
const uint32_t kClassic = 3, kLtkDifficulty = 4, kDltkDifficulty = 5;
const int kNotCustom = -1, kOtherCustom = -2;
//007 Custom's sliders and switch as custom_values() has them: health, damage, reactions, protection, headshots, accuracy
const char* const kLtk = "0,2000,100,0,0,200";
const char* const kDltk = "2000,2000,100,0,0,200";
enum class Spoiled { No, Cheats, Tas, BhopRules, Custom }; //what kept the attempt from being saved, the first
const uint32_t kMissionStart = 1;
const float kNewAttempt = 1.0f; //the timer under this after a mission's start: it's counting a new attempt

std::map<uint64_t, float> m_Best; //level << 32 | bhop << 8 | difficulty -> seconds
bool m_Loaded{};
Spoiled m_Spoiled{};
int m_Custom{kNotCustom}; //this attempt's 007 Custom, a difficulty or kNotCustom/kOtherCustom
bool m_Hopped{}; //bunnyhopping was on in this attempt
bool m_Pending{}; //a mission's start was asked for, the attempt begins when the timer is back at 0
bool m_Recorded{}; //this attempt's page was looked at already
bool m_Shown{}; //what the page shows: a best time, or what kept it from being saved
Spoiled m_ShownSpoiled{};
uint32_t m_ShownDifficulty{};
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
	f << "# the fastest won mission without cheats: level, difficulty (0 Operative, 1 Agent, 2 007, 3 007 Classic, 4 LTK, 5 DLTK), bhop (1 bunnyhopping), seconds\n";
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

//the 007 Custom the mission would be played as
int Custom()
{
	if (!custom_mode()) return kNotCustom;
	const std::string values = custom_values();
	if (values == kLtk) return kLtkDifficulty;
	if (values == kDltk) return kDltkDifficulty;
	return kOtherCustom;
}

void Spoil(Spoiled why)
{
	if (m_Spoiled == Spoiled::No) m_Spoiled = why;
}

} //namespace

void best_times_frame()
{
	if (m_Pending && ldf(kLevelTimer) < kNewAttempt)
	{
		m_Pending = false;
		m_Spoiled = Spoiled::No;
		m_Custom = Custom();
		m_Hopped = false;
		m_Recorded = false;
	}
	if (cheats_active()) Spoil(Spoiled::Cheats);
	if (lib_steering()) Spoil(Spoiled::Tas);
	if (bhop_custom_now()) Spoil(Spoiled::BhopRules);
	if (Custom() != m_Custom) Spoil(Spoiled::Custom);
	if (bunnyhop_now()) m_Hopped = true;
}

void best_times_level_change(uint32_t entrance)
{
	if (entrance <= kMissionStart) m_Pending = true;
}

void best_times_page(uint32_t page)
{
	m_Shown = false;
	if (!guest_ptr(page) || multiplayer_game() || ld8(kTimeTrial)) return;
	const uint32_t level = ld32(kLastLevel);
	uint32_t difficulty = ld16(page + kPageDifficulty);
	if (custom_mode())
	{
		//played as 007 Classic, only LTK and DLTK are kept
		const int custom = Custom();
		if (custom < 0 || difficulty != kClassic) return;
		difficulty = (uint32_t)custom;
	}
	else if (difficulty > kClassic) return;
	if (!m_Loaded) Load();
	const auto key = Key(level, difficulty, m_Hopped);
	if (ld8(page + kPageWon) == 1 && !m_Recorded && m_Spoiled == Spoiled::No)
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
	m_ShownSpoiled = won ? m_Spoiled : Spoiled::No;
	m_ShownHopped = m_Hopped;
	m_ShownDifficulty = difficulty;
	m_ShownTime = best == m_Best.end() ? 0.0f : best->second;
	m_Shown = m_ShownSpoiled != Spoiled::No || best != m_Best.end();
}

std::string best_time_text()
{
	if (!m_Shown) return {};
	switch (m_ShownSpoiled)
	{
	case Spoiled::Cheats: return "Cheats Used - Not Saved";
	case Spoiled::Tas: return "TAS Mode On - Not Saved";
	case Spoiled::BhopRules: return "Custom Bhop Rules - Not Saved";
	case Spoiled::Custom: return "Settings Changed - Not Saved";
	default: break;
	}
	const std::string kind = m_ShownDifficulty == kLtkDifficulty ? "LTK " : m_ShownDifficulty == kDltkDifficulty ? "DLTK " : "";
	return "Best " + kind + (m_ShownHopped ? "Bhop " : "") + "Time - " + Clock(m_ShownTime);
}

} //namespace ge
