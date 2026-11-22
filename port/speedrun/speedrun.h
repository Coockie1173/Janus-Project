//the speedrun timer
#pragma once
#include "guest.h"

namespace ge
{

//splits.cpp, the run
void speedrun_frame(); //once a frame on the game's thread
void speedrun_level_change(uint32_t from, uint32_t to, uint32_t entrance, bool next); //next: the app's update loads what comes after a won mission
bool speedrun_timer(); //the splits window is shown
void set_speedrun_timer(bool on);
bool speedrun_window(); //it's in a window of its own
void set_speedrun_window(bool on);
bool speedrun_quit_reset(); //Save and Quit resets the run
void set_speedrun_quit_reset(bool on);
float speedrun_opacity(); //the box over the game, in percent
void set_speedrun_opacity(float percent);
void speedrun_menu_save(); //autosave.cpp: the game's menu saved the game, not a checkpoint
void speedrun_reset(); //the run so far is dropped, the best times stay
std::string speedrun_said(); //what the last import or export came to, for a few seconds
void speedrun_clear_best(); //the personal best, the best segments and the attempts are gone

//window.cpp
void speedrun_export(); //a file dialog, the splits as LiveSplit's .lss
void speedrun_import(); //a .lss, or the timer's own file
bool speedrun_in_game(); //drawn over the game's picture
bool speedrun_dragging(); //the window is held by the mouse

} //namespace ge
