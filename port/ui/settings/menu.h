//the settings menu's insides, shared by its files:
//files.cpp the launcher's settings file and the key file
//catalog.cpp every setting the pages change
//pages.cpp the pages and the level grids
//draw.cpp the picture
//menu.cpp opening, keys, the mouse and the pads, and what the rest of the port calls
#pragma once
#include "ge.h"
#include "ui/settings/hotkeys.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#ifdef GE_IMGUI
#include "imgui.h"
#include <SDL3/SDL.h>
#endif

#ifndef GE_ROOT
#define GE_ROOT "."
#endif
#ifndef GE_SETTINGS_FILE
#define GE_SETTINGS_FILE "settings.env"
#endif

namespace ge::menu
{

using Clock = std::chrono::steady_clock;
inline std::atomic<bool> g_open{false};
inline std::atomic<Clock::rep> g_pause_until{0}; //+ is held until then (settings_buttons)
inline std::atomic<bool> g_escape{false}; //Esc is down, pressed with the menu closed
const uint32_t kClPlus = 0x0400;

#ifdef GE_IMGUI
//files.cpp
std::string trim(const std::string& t);
std::vector<std::string> read_lines(const std::string& path);
bool write_lines(const std::string& path, const std::vector<std::string>& lines);
inline bool g_fps_changed = false; //the frame rate was changed in the menu

//read by the launcher (launcher/launch.cpp) as the game starts, so a change here waits for the next start
struct NextStart { bool online = true, fast_start = false, fullscreen = false, crash_dumps = true, gx_thread = true, update_check = true; };
NextStart& next_start();
//keys other files listen for (settings_key): pressed with the menu closed they call theirs, kept in the launcher's file
struct AddedKey { int page; std::string launch, label, hint, key; void (*pressed)(); };
inline std::vector<AddedKey> g_addedKeys;
//rows other files add as they are (settings_row): not settings of the game's, not in a hotkey's list
struct AddedRow { int page; std::string label, hint; std::function<std::string()> value; std::function<void(int)> adjust; std::function<void()> select; };
inline std::vector<AddedRow> g_addedRows;
void save_launch_settings();

//the key file's [Classic Controller] section, by name
struct KeyFile {
    std::vector<std::string> lines;

    void load(const std::string& path) { lines = read_lines(path); }
    int find(const std::string& name) const {
        bool inside = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string t = trim(lines[i]);
            if (t.empty() || t[0] == '#') continue;
            if (t[0] == '[') { inside = t == "[Classic Controller]" || t == "[Classic]"; continue; }
            size_t eq = t.find('=');
            if (inside && eq != std::string::npos && !SDL_strcasecmp(trim(t.substr(0, eq)).c_str(), name.c_str())) return (int)i;
        }
        return -1;
    }
    std::string get(const std::string& name, const std::string& otherwise = "") const {
        int i = find(name);
        return i < 0 ? otherwise : trim(lines[i].substr(lines[i].find('=') + 1));
    }
    void set(const std::string& name, const std::string& value) {
        int i = find(name);
        if (i >= 0) { //keep the file's own spacing
            lines[i] = trim(lines[i].substr(0, lines[i].find('=') + 1) + " " + value);
            return;
        }
        int end = -1; //a new line goes at the section's end
        bool inside = false;
        for (size_t k = 0; k < lines.size(); ++k) {
            std::string t = trim(lines[k]);
            if (!t.empty() && t[0] == '[') inside = t == "[Classic Controller]" || t == "[Classic]";
            if (inside && !t.empty()) end = (int)k;
        }
        if (end < 0) { lines.push_back("[Classic Controller]"); end = (int)lines.size() - 1; }
        lines.insert(lines.begin() + end + 1, name + " = " + value);
    }
    std::vector<std::string> keys(const std::string& name) const {
        std::vector<std::string> out;
        std::stringstream all(get(name));
        for (std::string k; std::getline(all, k, ',');)
            if (!trim(k).empty()) out.push_back(trim(k));
        return out;
    }
    void set_keys(const std::string& name, const std::vector<std::string>& keys) {
        std::string v;
        for (auto& k : keys) v += (v.empty() ? "" : ", ") + k;
        set(name, v);
    }
};

inline KeyFile g_keys;
void load_keys();
void save_keys();
KeyFile& Keys(); //a hotkey can get here before the menu was ever opened
const float kDeadZoneMax = 0.9f;
float dead_zone();
void set_dead_zone(float z);
void restore_bindings();
void bind(const std::string& key);
void add_sprint_binding();
void AddWeaponBindings();
void AddJumpBinding();
void AddMeleeBinding(); //once: F from the smartphone to the melee attack

//the pages
//what the game does with each classic controller button, and the key file's name for it
struct Action { const char* label; const char* name; bool forFun = false; }; //forFun: bound under its toggle, not on Key Bindings
inline const Action kActions[] = {
    {"Fire", "ZR"}, {"Aim", "ZL"}, {"Melee Attack", "L"}, {"Throw Grenade (doubles as R button)", "R"}, {"Reload", "Y"},
    {"Crouch", "A"}, {"Action", "B"}, {"Sprint", "Extra 1"}, {"Cycle Grenades", "X"}, {"Next Weapon", "Right"},
    {"Previous Weapon", "Left"}, {"Weapon 1", "Extra 3"}, {"Weapon 2", "Extra 4"}, {"Weapon 3", "Extra 5"},
    {"Firing Mode", "Up"}, {"Smartphone", "Down"}, {"Night Vision / Scores", "Minus"},
    {"Pause", "Plus"}, {"Move Forward", "Left Stick Up"}, {"Move Back", "Left Stick Down"},
    {"Move Left", "Left Stick Left"}, {"Move Right", "Left Stick Right"}, {"Jump", "Extra 6", true}};

struct Row {
    std::string label, hint;
    std::function<std::string()> value; //none for a row that's only selected
    std::function<void(int)> adjust; //left and right, -1 or +1
    std::function<void()> select;
    std::function<void()> clear; //Delete
    bool binding = false; //the value is a list of keys, not something to step through
    std::function<float()> slider; //for a slider, where its knob is in 0..1
    std::function<void(float)> slide; //and the knob dragged there
    bool wide = false; //the value goes against the right edge in the room the label leaves
    const char* does = nullptr; //what Enter and Delete are called at the foot, when it's not the usual
    const char* clears = nullptr;
    std::function<std::vector<std::string>()> options; //a dropdown: Enter opens the list of these
    std::function<int()> chosen; //the one it's on
    std::function<void(int)> choose;
};
struct Page { std::string title; std::vector<Row> rows; };
enum { kRoot, kScreen, kControls, kBindings, kEnhancements, kCheats, kForFun, kHotkeys, kHotkeyEdit, kLevels, kMaps, kSkips, kBhop, kTriggers, kSpeedrun, kFunPacks,
    kGameplay, kVisuals, kAudio, kUnlocks, kStartup, kPageCount }; //the last five: Enhancements' pages
inline std::vector<Page> g_pages;
//pages other files add (settings_page, settings_toggle), behind the ones here
struct AddedPage { std::string title, about; };
inline std::vector<AddedPage> g_addedPages;
inline std::vector<std::function<void()>> g_addedToggles;
inline int g_listenKey = -1; //the added key a row waits for
inline int g_page = kRoot, g_row = 0, g_scroll = 0;
inline int g_root_row = 0; //where the cursor was on the first page
inline int g_listening = -1; //the binding row waiting for a key
inline int g_listenAction = -1; //and the action it's for
inline int g_dragging = -1; //the slider row the mouse has by the knob
inline int g_drop = -1; //the row with its dropdown open
inline int g_dropAt = 0, g_dropScroll = 0; //the cursor in its list
inline std::vector<std::string> g_dropOptions;
inline bool g_dirty = false; //the launcher's file needs writing
inline std::vector<Setting> g_settings;
inline int g_edit = -1; //the hotkey on the edit page
inline bool g_hotkeysChanged = false; //the two hotkey pages need building again
inline size_t g_triggerRows = 0; //the trigger page's rows before its list of types
inline uint32_t g_triggerTypes = ~0u; //the version of the list the page was built for
inline std::string g_toast; //what a hotkey just did
inline Clock::time_point g_toastUntil{};
inline Clock::time_point g_clearUntil{}; //Clear Best Times was selected once, again before this clears them
inline Clock::time_point g_resetUntil{}, g_clearedUntil{}; //Reset Run and Clear Best Times say they're done until then

//catalog.cpp
void BuildSettings();
void AddToggle(int page, const char* id, const char* label, const char* hint, bool (*get)(), void (*set)(bool));
Row BindingRow(int action);
Row SettingRow(int i);

//pages.cpp
//the two level grids: every level the disc has. Level Select is the missions and the test
//levels by name, four to a row; Multiplayer Maps a row a map and a column a gamemode, a cell
//where the disc has that map's level for the gamemode. Enter or a click loads the level
//Skip Cutscenes is a grid too: a mission a row, its intro, middle and ending the columns, a cell
//where it has that cutscene, Enter or a click turns it on or off
struct Grid
{
	std::vector<std::string> columns, rowNames; //the headings, none for a grid of names
	int cols{ 1 };
	std::vector<uint32_t> hash; //a row after the other, 0 where there's no level (no cell)
	std::vector<std::string> label, about; //what a cell shows, and what the foot says for it
	std::function<void(int)> pick; //what Enter on a cell does, when it doesn't load a level
	std::function<int(int)> lit; //a toggle's cell: 1 on, 0 off, -1 not a toggle
	int Rows() const { return (int)hash.size() / cols; }
};
inline Grid g_grids[3];
inline int g_gridAt = 0, g_gridScroll = 0; //the cell under the cursor, the first row shown
const int kGridVisible = 9, kLevelColumns = 4;
const float kGridLeft = 150, kGridRight = 1130, kGridTop = 190, kGridPitch = 38, kGridNames = 190;
const uint32_t kMissionFiles = 0x0181; //the top of an mt_ level's hash

inline bool OnGrid() { return g_page == kLevels || g_page == kMaps || g_page == kSkips; }
inline Grid& CurrentGrid() { return g_grids[g_page == kSkips ? 2 : g_page == kMaps ? 1 : 0]; }
void OpenGrid();
void GridMove(int dx, int dy);
void GridLoad();
void go(int page, int row = 0);
void ChangeHotkey(const std::function<void(Hotkey&)>& change);
void RefreshHotkeyPages();
void RefreshTriggerPage();
void build_pages();

//menu.cpp
void open(bool on);
void back();
void select();

//draw.cpp
inline ImFont* g_font = nullptr;
inline ImFont* g_textFont = nullptr; //another context's copy of it, while the speedrun timer's own window draws
const float kFontSize = 48;
struct Layout { //the game's 1280 x 720 menu screen, centred in the window
    float u, cx, w, h;
    float x(float ref) const { return cx + (ref - 640) * u; }
    float y(float ref) const { return ref * u; }
};
const float kRowLeft = 340, kRowRight = 940, kRowHeight = 38, kRowPitch = 41, kValueWidth = 230;
const float kSliderLeft = kRowRight - 262, kSliderRight = kRowRight - 66; //a slider's track, its number after it
const int kVisible = 8;
const float kDropLeft = 560, kDropPitch = 34, kDropPad = 5, kDropFloor = 538; //a dropdown's list, it stays above the foot
const int kDropVisible = 8;
inline Layout g_layout;
int GridAt(float x, float y);
int DropAt(float x, float y);
int row_at(float x, float y);
#endif

} //namespace ge::menu

namespace ge
{

bool settings_toast(); //a hotkey's line is up
#ifdef GE_IMGUI
ImFont* menu_font_add(ImFontAtlas* atlas);
#endif

} //namespace ge
