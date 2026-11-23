//what's drawn over the game: the settings menu (F1), the debug overlay (F3), the window's icon
#pragma once
#include "guest.h"

namespace ge
{

//settings/: menu.cpp, catalog.cpp, files.cpp
bool cheats_active(); //something on the menu's Cheats page is on or a Cheats tab code runs
bool settings_open();
int settings_page(const char* title, const char* about); //a page of switches another file adds, before the menu is shown
void settings_toggle(int page, const char* id, const char* label, const char* hint, bool (*get)(), void (*set)(bool));
void settings_key(int page, const char* id, const char* label, const char* hint, void (*pressed)(), const char* otherwise = ""); //a key to choose on the page, pressed with the menu closed it calls pressed (may be null); otherwise: its key until one is chosen
std::string settings_key_name(const char* id); //the key chosen on an added key's row, empty for none
//a row of another file's, in the order added: value (none for a row only selected), adjust (left -1, right +1), select (Enter); any may be empty
void settings_row(int page, const char* label, const char* hint, std::function<std::string()> value, std::function<void(int)> adjust, std::function<void()> select);
void settings_say(const std::string& text); //a line at the top for a moment
void save_settings(); //the settings file is written
const char* settings_cheat_off(const std::string& id); //a setting of the Cheats page (or its Triggers page): its off value, "" for none of its own, null for any other
void settings_cheats_off(); //every one of them off, the launcher's file kept as it is
std::string saved_setting(const char* env, const char* name); //a setting as the game starts: the environment's, or what the menu saved under name in the launcher's file. empty for neither
uint32_t settings_buttons(); //classic buttons the menu presses itself (+ to pause as it opens)

//overlay.cpp
void install_overlay();
bool overlay_open(); //the watches, the settings menu or TASLib's window are up, so the mouse is free
bool menus_open(); //the watches or the settings menu (they take the keys)

//icon.cpp
void window_icon(SDL_Window* win); //port/package/icon.bmp on the window

} //namespace ge
