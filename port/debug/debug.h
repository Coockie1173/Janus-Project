//debug/: the debugging aids (debug.cpp, driven by environment variables), crash dumps, the profiler
#pragma once
#include "guest.h"
#include <string>
void debug_install();
void debug_classic(ClassicState& s); //GE_STICK, scripted stick deflections
void debug_mouse(float& dx, float& dy); //GE_MOUSEMOVE, scripted mouse motion added on top
void debug_setting(const char* id, float value); //GE_RECORD, a hotkey left that setting at that value
bool debug_setting_due(std::string& id, float& value); //GE_REPLAY, the next recorded one whose time has come

namespace ge
{

//crashdump.cpp
void install_crash_handler();
void crash_test();
void crash_thread_stack(); //the calling thread's stack for the crash handler

//diagnostics.cpp
void install_profiler();

} //namespace ge
