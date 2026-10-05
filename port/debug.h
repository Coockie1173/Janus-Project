//debugging aids, all driven by environment variables (see debug.cpp)
#pragma once
#include <string>
struct ClassicState;
void debug_install();
void debug_classic(ClassicState& s); //GE_STICK, scripted stick deflections
void debug_mouse(float& dx, float& dy); //GE_MOUSEMOVE, scripted mouse motion added on top
void debug_setting(const char* id, float value); //GE_RECORD, a hotkey left that setting at that value
bool debug_setting_due(std::string& id, float& value); //GE_REPLAY, the next recorded one whose time has come
