//hotkeys.cpp: keys that change the settings menu's settings while the game plays
#pragma once
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace ge
{

//something the settings menu changes, as a number: 0 or 1 for a toggle, the index for a
//choice, the value itself for a range. an action (Reset Run) only does something, set does it
struct Setting
{
	enum Kind { kToggle, kChoice, kRange, kAction };
	std::string id; //its name in the hotkeys file
	std::string label, hint;
	int page{};
	Kind kind{};
	std::function<float()> get;
	std::function<void(float)> set;
	std::function<float(float, int)> step; //the value a step down (-1) or up from this one
	std::function<std::string(float)> show;
	std::function<float(float)> frac, at; //a range's slider: where a value is on the track (0..1) and the value there
	int count{}; //a choice: how many it has
	bool dropdown{}; //a choice that's a list to pick from, not stepped through
	std::function<bool(int)> offered; //a choice: the ones that can be picked now, none for all of them
};
const std::vector<Setting>& Settings(); //catalog.cpp, the menu's own list

struct Hotkey
{
	enum Action { kNext, kPrevious, kSet, kSwitch, kActionCount };
	std::string key; //SDL's name for it, empty while it has none
	int setting{}; //index in Settings()
	Action action{};
	float value{}; //what set and switch go to
	float back{NAN}; //switch: what it was before
};
std::vector<Hotkey>& Hotkeys();
void LoadHotkeys();
void SaveHotkeys();
Hotkey NewHotkey(int setting); //the first action, the value the setting has now
bool HasValue(const Hotkey& h); //the action goes to a value of the player's choice
std::vector<std::string> ActionNames(int setting); //what a key can do to that setting, a toggle has three
int ActionIndex(const Hotkey& h); //which of them the hotkey does
void SetAction(Hotkey& h, int index);
std::string ActionName(const Hotkey& h);
std::string Describe(const Hotkey& h); //"Max Frame Rate: Set To 120"
bool PressHotkeys(int scancode, std::string& said); //false when no hotkey is on that key

} //namespace ge
