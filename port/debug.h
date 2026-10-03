//debugging aids, all driven by environment variables (see debug.cpp)
#pragma once
struct ClassicState;
void debug_install();
void debug_classic(ClassicState& s); //GE_STICK, scripted stick deflections
void debug_mouse(float& dx, float& dy); //GE_MOUSEMOVE, scripted mouse motion added on top
