// Debugging aids of the port (port/debug.cpp), driven by the environment.
#pragma once
struct ClassicState;
void debug_install();
void debug_classic(ClassicState& s);                 // GE_STICK: scripted stick deflections
void debug_mouse(float& dx, float& dy);              // GE_MOUSEMOVE: scripted mouse motion, added
