//hotkeys: keys that change one of the settings menu's settings while the game plays
//made and removed on the menu's Hotkeys page (settings.cpp), kept in hotkeys.txt beside the
//launcher's settings, a line each:
//	F5 = fps switch 30
//the key by SDL's name, the setting's id, then what the key does: next, previous, set or
//switch (set, and back to what it was on the next press), with the value for the last two
//more than one hotkey can be on a key, they all go
#include "hotkeys.h"
#include "debug.h"
#include "ge.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef GE_IMGUI
#include <SDL3/SDL.h>

#ifndef GE_SETTINGS_FILE
#define GE_SETTINGS_FILE "settings.env"
#endif

namespace ge
{
namespace
{

std::vector<Hotkey> g_hotkeys;
const char* const kActionIds[Hotkey::kActionCount] = {"next", "previous", "set", "switch"};

std::string FilePath()
{
	std::string path = GE_SETTINGS_FILE;
	size_t slash = path.find_last_of("/\\");
	return (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "hotkeys.txt";
}

std::string Trim(const std::string& t)
{
	size_t a = t.find_first_not_of(" \t\r"), b = t.find_last_not_of(" \t\r");
	return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
}

int FindSetting(const std::string& id)
{
	for (size_t i = 0; i < Settings().size(); ++i)
	{
		if (Settings()[i].id == id) return (int)i;
	}
	return -1;
}

bool Same(float a, float b) { return std::fabs(a - b) < 0.001f; }

//what the setting is after the press, for the line on screen
std::string Press(Hotkey& h)
{
	const Setting& s = Settings()[h.setting];
	float now = s.get();
	switch (h.action)
	{
	case Hotkey::kNext: s.set(s.step(now, 1)); break;
	case Hotkey::kPrevious: s.set(s.step(now, -1)); break;
	case Hotkey::kSet: s.set(h.value); break;
	case Hotkey::kSwitch:
		if (!Same(now, h.value))
		{
			h.back = now;
			s.set(h.value);
		}
		else if (!std::isnan(h.back)) s.set(h.back);
		break;
	default: break;
	}
	debug_setting(s.id.c_str(), s.get()); //a recording keeps the value, the replay needn't have the key
	return s.label + ": " + s.show(s.get());
}

} //namespace

std::vector<Hotkey>& Hotkeys() { return g_hotkeys; }

void LoadHotkeys()
{
	g_hotkeys.clear();
	std::ifstream f(FilePath());
	for (std::string line; std::getline(f, line);)
	{
		line = Trim(line);
		size_t eq = line.rfind('='); //the key itself can be =
		if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
		std::stringstream does(line.substr(eq + 1));
		std::string id, action;
		Hotkey h;
		does >> id >> action;
		h.setting = FindSetting(id);
		int a = 0;
		while (a < Hotkey::kActionCount && action != kActionIds[a]) ++a;
		if (h.setting < 0 || a == Hotkey::kActionCount)
		{
			rt_log("goldeneye: hotkeys: can't read \"%s\"", line.c_str());
			continue;
		}
		h.action = (Hotkey::Action)a;
		if (!(does >> h.value)) h.value = Settings()[h.setting].get();
		h.key = Trim(line.substr(0, eq));
		if (SDL_GetScancodeFromName(h.key.c_str()) == SDL_SCANCODE_UNKNOWN) h.key.clear();
		g_hotkeys.push_back(h);
	}
	if (!g_hotkeys.empty()) rt_log("goldeneye: %zu hotkeys from %s", g_hotkeys.size(), FilePath().c_str());
}

void SaveHotkeys()
{
	std::ofstream f(FilePath(), std::ios::trunc);
	f << "#keys that change a setting in play, made in the settings menu (F1 > Hotkeys)\n"
	  << "#key = setting next|previous|set|switch [value]\n";
	for (const Hotkey& h : g_hotkeys)
	{
		char value[32] = "";
		if (HasValue(h)) std::snprintf(value, sizeof value, " %g", h.value);
		f << (h.key.empty() ? "None" : h.key) << " = " << Settings()[h.setting].id << ' ' << kActionIds[h.action] << value << '\n';
	}
	if (!f.good()) rt_log("goldeneye: hotkeys not saved: cannot write %s", FilePath().c_str());
}

Hotkey NewHotkey(int setting)
{
	Hotkey h;
	h.setting = setting;
	h.value = Settings()[setting].get();
	return h;
}

bool HasValue(const Hotkey& h) { return h.action == Hotkey::kSet || h.action == Hotkey::kSwitch; }

std::string ActionName(const Hotkey& h)
{
	static const char* const kRange[] = {"Increase", "Decrease", "Set To", "Switch To"};
	static const char* const kChoice[] = {"Next", "Previous", "Set To", "Switch To"};
	switch (Settings()[h.setting].kind)
	{
	case Setting::kToggle: return h.action == Hotkey::kNext ? "Toggle" : h.value > 0.5f ? "Turn On" : "Turn Off";
	case Setting::kChoice: return kChoice[h.action];
	default: return kRange[h.action];
	}
}

std::string Describe(const Hotkey& h)
{
	const Setting& s = Settings()[h.setting];
	std::string text = s.label + ": " + ActionName(h);
	if (HasValue(h) && s.kind != Setting::kToggle) text += " " + s.show(h.value);
	return text;
}

//a toggle has three: toggle, on and off
void CycleAction(Hotkey& h, int d)
{
	h.back = NAN;
	if (Settings()[h.setting].kind != Setting::kToggle)
	{
		h.action = (Hotkey::Action)((h.action + d + Hotkey::kActionCount) % Hotkey::kActionCount);
		return;
	}
	int at = h.action == Hotkey::kNext ? 0 : h.value > 0.5f ? 1 : 2;
	at = (at + d + 3) % 3;
	h.action = at == 0 ? Hotkey::kNext : Hotkey::kSet;
	h.value = at == 1 ? 1.0f : 0.0f;
}

bool PressHotkeys(int scancode, std::string& said)
{
	bool any = false;
	for (Hotkey& h : g_hotkeys)
	{
		if (h.key.empty() || (int)SDL_GetScancodeFromName(h.key.c_str()) != scancode) continue;
		said += (any ? ", " : "") + Press(h);
		any = true;
	}
	return any;
}

} //namespace ge
#endif
