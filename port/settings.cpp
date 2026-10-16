//the settings menu (F1), drawn like the game's own menus: cream lines, blue bars and the
//white bar under the cursor
//
//six pages: screen (fov and frame rate), controls (mouse, gamepad), key bindings, hotkeys,
//enhancements and cheats. changes count right away
//what the pages change is one list of settings (hotkeys.h), a hotkey is a key on one of them.
//it works with the menu closed and says what it did at the top of the picture
//fov and mouse go in the launcher's settings file, the rest in the runtime's key file
//(only the [Classic Controller] lines changed here get rewritten) so the launcher and this
//menu show the same thing
//
//while it's up the game gets no input, and a game in play gets paused first (a press of +)
//Esc outside the menu is + as well (the game's own pause), it never gets to the runtime's pause box
//keys: arrows or WASD, Return, Esc or Backspace to go back, Delete to clear a binding
//a hotkey's setting, action and choice are dropdowns: Return opens the list, Return again picks
//the mouse and a gamepad's D-pad, A and B work too
//
//drawn by the overlay (overlay.cpp) on the renderer's thread
//
//GE_SETTINGS=N start with the menu up on page N, handy for screenshots
#include "debug.h"
#include "ge.h"
#include "hotkeys.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#ifdef GE_IMGUI
#include "ge_font.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#endif

#ifndef GE_ROOT
#define GE_ROOT "."
#endif
#ifndef GE_SETTINGS_FILE
#define GE_SETTINGS_FILE "settings.env"
#endif

namespace ge {
namespace {

using Clock = std::chrono::steady_clock;
std::atomic<bool> g_open{false};
std::atomic<Clock::rep> g_pause_until{0}; //+ is held until then (settings_buttons)
std::atomic<bool> g_escape{false}; //Esc is down, pressed with the menu closed
const uint32_t kClPlus = 0x0400;

#ifdef GE_IMGUI
//the two files
std::string trim(const std::string& t) {
    size_t a = t.find_first_not_of(" \t\r"), b = t.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
}

std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream f(path, std::ios::binary);
    for (std::string l; std::getline(f, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    return lines;
}

bool write_lines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    for (auto& l : lines) f << l << '\n';
    return f.good();
}

bool g_fps_changed = false; //the frame rate was changed in the menu

//the launcher's file, LAUNCH_NAME="value" lines. other lines are kept
void save_launch_settings() {
    char fov_text[16], mouse_text[16], ads_fov_text[16], ads_sens_text[16];
    std::snprintf(fov_text, sizeof fov_text, "%.0f", fov());
    std::snprintf(mouse_text, sizeof mouse_text, "%.2f", mouse_sens());
    std::snprintf(ads_fov_text, sizeof ads_fov_text, "%g", ads_fov());
    std::snprintf(ads_sens_text, sizeof ads_sens_text, "%.2f", ads_sens());
    char shake_text[16], tank_text[32];
    std::snprintf(shake_text, sizeof shake_text, "%g", shake_scale());
    std::snprintf(tank_text, sizeof tank_text, "%d,%d,%d", tank_source(0), tank_source(1), tank_source(2));
    static const char* modes[] = {"auto", "direct", "stick"};
    std::vector<std::pair<std::string, std::string>> values = {
        {"LAUNCH_FOV", fov_text}, {"LAUNCH_MOUSE", mouse_text},
        {"LAUNCH_MOUSEMODE", modes[mouse_mode()]}, {"LAUNCH_MOUSEINVERT", mouse_invert() ? "1" : "0"},
        {"LAUNCH_ADSFOV", ads_fov_text}, {"LAUNCH_ADSSENS", ads_sens_text}, {"LAUNCH_AIMLOCK", aim_lock() ? "1" : "0"},
        {"LAUNCH_MOUSEEVERYWHERE", mouse_everywhere() ? "1" : "0"},
        {"LAUNCH_SKIPLIST", skip_list()}, {"LAUNCH_SKIPMOVIES", skip_movies() ? "1" : "0"}, {"LAUNCH_NOTUTORIALS", no_tutorials() ? "1" : "0"}, {"LAUNCH_NOCOLOURFILTER", no_colour_filter() ? "1" : "0"},
        {"LAUNCH_LEVELTIMER", level_timer() ? "1" : "0"}, {"LAUNCH_AUTOQTE", auto_qte() ? "1" : "0"}, {"LAUNCH_AUTOSTART", auto_start() ? "1" : "0"}, {"LAUNCH_AUTOSAVE", autosave() ? "1" : "0"}, {"LAUNCH_EXTRASMENU", extras_menu() ? "1" : "0"}, {"LAUNCH_UNLOCKALL", unlock_all() ? "1" : "0"}, {"LAUNCH_ONLINEUNLOCKS", online_unlocks() ? "1" : "0"}, {"LAUNCH_STAMINAMETER", stamina_meter() ? "1" : "0"},
        {"LAUNCH_VSYNC", vsync() ? "1" : "0"}, {"LAUNCH_PING", show_ping() ? "1" : "0"}, {"LAUNCH_OCCLUSION", occlusion() ? "1" : "0"}, {"LAUNCH_SOUNDHEIGHT", sound_height() ? "1" : "0"},
        {"LAUNCH_HUD", hud() ? "1" : "0"}, {"LAUNCH_SHAKE", shake_text}, {"LAUNCH_TANKBUTTONS", tank_text},
        {"LAUNCH_ANISO", std::to_string(anisotropy())}, {"LAUNCH_MSAA", std::to_string(anti_aliasing())},
        {"LAUNCH_CUSTOM", custom_values()},
        {"LAUNCH_GOD", god_mode() ? "1" : "0"}, {"LAUNCH_INFINITEAMMO", infinite_ammo() ? "1" : "0"}, {"LAUNCH_INFINITESPRINT", infinite_sprint() ? "1" : "0"},
        {"LAUNCH_WEAPONRANDOMIZER", weapon_randomizer() ? "1" : "0"},
        {"LAUNCH_NOATTACHMENTLIMIT", no_attachment_limit() ? "1" : "0"}, {"LAUNCH_BUNNYHOP", bunnyhop() ? "1" : "0"},
        {"LAUNCH_BHOP_PRESTRAFE", bhop_prestrafe() ? "1" : "0"}};
    for (int i = 0; i < kBhopValueCount; ++i) {
        char text[24];
        std::snprintf(text, sizeof text, "%g", bhop_value(i).value);
        values.push_back({bhop_value(i).launch, text});
    }
    char forced_text[16];
    std::snprintf(forced_text, sizeof forced_text, "%08X", forced_weapon_id());
    values.push_back({"LAUNCH_FORCEDWEAPON", forced_text});
    if (g_fps_changed) values.push_back({"LAUNCH_FPS", std::to_string(fps_limit())}); //"auto" stays until it's changed
    auto lines = read_lines(GE_SETTINGS_FILE);
    if (lines.empty()) lines.push_back("# written by the launcher, read when the game is started");
    for (auto& [name, value] : values) {
        std::string line = name + "=\"" + value + "\"";
        bool found = false;
        for (auto& l : lines)
            if (l.compare(0, name.size() + 1, name + "=") == 0) { l = line; found = true; }
        if (!found) lines.push_back(line);
    }
    if (!write_lines(GE_SETTINGS_FILE, lines)) rt_log("goldeneye: settings not saved: cannot write %s", GE_SETTINGS_FILE);
}

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

KeyFile g_keys;

void load_keys() { g_keys.load(video_keys_path()); }
void save_keys() {
    if (video_keys_path().empty()) return;
    if (!write_lines(video_keys_path(), g_keys.lines)) rt_log("goldeneye: keys not saved: cannot write %s", video_keys_path().c_str());
    video_reload_keys();
}

//the pages
//what the game does with each classic controller button, and the key file's name for it
struct Action { const char* label; const char* name; bool forFun = false; }; //forFun: bound under its toggle, not on Key Bindings
const Action kActions[] = {
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

enum { kRoot, kScreen, kControls, kBindings, kEnhancements, kCheats, kForFun, kHotkeys, kHotkeyEdit, kLevels, kMaps, kSkips, kBhop, kPageCount };
std::vector<Page> g_pages;
int g_page = kRoot, g_row = 0, g_scroll = 0;
int g_root_row = 0; //where the cursor was on the first page
int g_listening = -1; //the binding row waiting for a key
int g_listenAction = -1; //and the action it's for
int g_dragging = -1; //the slider row the mouse has by the knob
int g_drop = -1; //the row with its dropdown open
int g_dropAt = 0, g_dropScroll = 0; //the cursor in its list
std::vector<std::string> g_dropOptions;
bool g_dirty = false; //the launcher's file needs writing
std::vector<Setting> g_settings;
int g_edit = -1; //the hotkey on the edit page
bool g_hotkeysChanged = false; //the two hotkey pages need building again
std::string g_toast; //what a hotkey just did
Clock::time_point g_toastUntil{};

template <size_t N> int index_of(const char* const (&names)[N], const std::string& v, int otherwise) {
    for (size_t i = 0; i < N; ++i)
        if (!SDL_strcasecmp(names[i], v.c_str())) return (int)i;
    return otherwise;
}
//the frame rates the slider stops at, same as the launcher's
const uint32_t kFpsStops[] = {30, 40, 50, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240, 275};
const int kFpsStopCount = (int)std::size(kFpsStops);
int FpsStop(float fps) //the stop nearest to a rate
{
	int best = 0;
	for (int i = 1; i < kFpsStopCount; ++i)
	{
		if (std::fabs((float)kFpsStops[i] - fps) < std::fabs((float)kFpsStops[best] - fps)) best = i;
	}
	return best;
}
const char* const kInputNames[] = {"Auto", "Pad", "Keyboard"};
const char* const kFaceNames[] = {"Position", "Label"};
const float kDeadZoneMax = 0.9f;
const int kAnisotropies[] = {1, 2, 4, 8, 16}, kSampleCounts[] = {1, 2, 4, 8};
template <class T, size_t N> int NearestOf(const T (&values)[N], T value) //which of them a value is, or is next to
{
	int best = 0;
	for (size_t i = 1; i < N; ++i)
	{
		if (std::fabs((float)(values[i] - value)) < std::fabs((float)(values[best] - value))) best = (int)i;
	}
	return best;
}
KeyFile& Keys() //a hotkey can get here before the menu was ever opened
{
	if (g_keys.lines.empty()) load_keys();
	return g_keys;
}
float dead_zone() { return std::clamp((float)std::atof(Keys().get("Dead Zone", "0.15").c_str()), 0.0f, kDeadZoneMax); }
void set_dead_zone(float z) { //in hundredths, only writes the file when it changes
    char b[16];
    std::snprintf(b, sizeof b, "%.2f", std::clamp(z, 0.0f, kDeadZoneMax));
    if (g_keys.get("Dead Zone") == b) return;
    g_keys.set("Dead Zone", b);
    save_keys();
}

void restore_bindings() {
    KeyFile defaults;
    defaults.load(std::string(GE_ROOT) + "/config/keys.txt");
    if (defaults.lines.empty()) { rt_log("goldeneye: no default keys (config/keys.txt)"); return; }
    for (auto& a : kActions) g_keys.set(a.name, defaults.get(a.name));
    save_keys();
}

Row BindingRow(int action)
{
	const Action& a = kActions[action];
	Row r;
	r.label = a.label;
	r.hint = "Select to add a key or a mouse button. Delete clears the action.";
	r.binding = true;
	r.value = [name = std::string(a.name)]
	{
		std::string v;
		for (auto& k : g_keys.keys(name)) v += (v.empty() ? "" : ", ") + k;
		return v.empty() ? std::string("-") : v;
	};
	r.select = [action]
	{
		g_listening = g_row;
		g_listenAction = action;
	};
	r.clear = [name = std::string(a.name)] { g_keys.set_keys(name, {}); save_keys(); };
	return r;
}

Setting& AddSetting(int page, Setting::Kind kind, const char* id, const char* label, const char* hint)
{
	Setting s;
	s.id = id;
	s.label = label;
	s.hint = hint;
	s.page = page;
	s.kind = kind;
	g_settings.push_back(s);
	return g_settings.back();
}

void AddToggle(int page, const char* id, const char* label, const char* hint, bool (*get)(), void (*set)(bool))
{
	Setting& s = AddSetting(page, Setting::kToggle, id, label, hint);
	s.get = [get] { return get() ? 1.0f : 0.0f; };
	s.set = [set](float v) { set(v > 0.5f); g_dirty = true; };
	s.step = [](float v, int) { return v > 0.5f ? 0.0f : 1.0f; };
	s.show = [](float v) { return std::string(v > 0.5f ? "On" : "Off"); };
}

Setting& AddChoice(int page, const char* id, const char* label, const char* hint, std::vector<const char*> names,
	std::function<int()> get, std::function<void(int)> set)
{
	Setting& s = AddSetting(page, Setting::kChoice, id, label, hint);
	int count = (int)names.size();
	s.get = [get] { return (float)get(); };
	s.set = [set, count](float v) { set(std::clamp((int)std::lround(v), 0, count - 1)); };
	s.step = [count](float v, int d) { return (float)(((int)std::lround(v) + d + count) % count); };
	s.show = [names, count](float v) { return std::string(names[std::clamp((int)std::lround(v), 0, count - 1)]); };
	s.count = count;
	return s;
}

//from lo to hi in steps of by, shown with that many decimals
Setting& AddRange(int page, const char* id, const char* label, const char* hint, std::function<float()> get,
	std::function<void(float)> set, float lo, float hi, float by, int decimals)
{
	Setting& s = AddSetting(page, Setting::kRange, id, label, hint);
	auto snap = [lo, hi, by](float v) { return std::clamp(std::round(v / by) * by, lo, hi); };
	s.get = get;
	s.set = set;
	s.step = [snap, by](float v, int d) { return snap(v + by * (float)d); };
	s.show = [decimals](float v)
	{
		char b[24];
		std::snprintf(b, sizeof b, "%.*f", decimals, v);
		return std::string(b);
	};
	s.frac = [lo, hi](float v) { return (v - lo) / (hi - lo); };
	s.at = [snap, lo, hi](float f) { return snap(lo + f * (hi - lo)); };
	return s;
}

//everything the pages change, in the order the pages list it
void BuildSettings()
{
	AddRange(kScreen, "fov", "Field of View", "How wide the view is in play, in degrees. The game's own is 49.",
		fov, [](float v) { set_fov(v); g_dirty = true; }, kFovMin, kFovMax, 1, 0);
	//whole degrees, and the game's 22.5 on the way past
	Setting& adsFov = AddRange(kScreen, "ads_fov", "Aim Field of View", "How wide the view is aiming down the sight. The game's own is 22.5 with the pistol.",
		ads_fov, [](float v) { set_ads_fov(v); g_dirty = true; }, kAdsFovMin, kFovMax, 1, 0);
	adsFov.step = [](float f, int d)
	{
		float next = d > 0 ? std::floor(f) + 1 : std::ceil(f) - 1;
		if ((f < kGameAdsFov) != (next < kGameAdsFov)) next = kGameAdsFov;
		return std::clamp(next, kAdsFovMin, kFovMax);
	};
	adsFov.show = [](float v)
	{
		char b[16];
		std::snprintf(b, sizeof b, "%g", v);
		return std::string(b);
	};
	Setting& fps = AddRange(kScreen, "fps", "Max Frame Rate", "The most frames a second the game runs at. 30 is the console's; heavy levels may not hold the highest.",
		[] { return (float)fps_limit(); },
		[](float v) { set_fps_limit((uint32_t)std::lround(v)); g_fps_changed = g_dirty = true; },
		(float)kFpsStops[0], (float)kFpsStops[kFpsStopCount - 1], 1, 0);
	fps.step = [](float v, int d) { return (float)kFpsStops[std::clamp(FpsStop(v) + d, 0, kFpsStopCount - 1)]; };
	fps.frac = [](float v) { return (float)FpsStop(v) / (float)(kFpsStopCount - 1); };
	fps.at = [](float f) { return (float)kFpsStops[std::clamp((int)std::lround(f * (float)(kFpsStopCount - 1)), 0, kFpsStopCount - 1)]; };
	AddToggle(kScreen, "vsync", "VSync", "On: frames wait for the screen's refresh, so the picture doesn't tear. The game runs at the screen's rate at most.", vsync, set_vsync);
	AddChoice(kScreen, "anisotropy", "Anisotropic Filtering", "Keeps textures sharp on floors and walls seen at a slant. Off is the console's picture.",
		{"Off", "2x", "4x", "8x", "16x"}, [] { return NearestOf(kAnisotropies, anisotropy()); },
		[](int v) { set_anisotropy(kAnisotropies[v]); g_dirty = true; });
	AddChoice(kScreen, "anti_aliasing", "Anti-Aliasing", "Smooths the edges of everything drawn, with that many samples a pixel. Costs the graphics card more the higher it is.",
		{"Off", "2x MSAA", "4x MSAA", "8x MSAA"}, [] { return NearestOf(kSampleCounts, anti_aliasing()); },
		[](int v) { set_anti_aliasing(kSampleCounts[v]); g_dirty = true; });
	Setting& shake = AddRange(kScreen, "screen_shake", "Screen Shake", "How much the view shakes: from explosions, on Nightclub's dance floor and the like. 1.0 is the game's own.",
		shake_scale, [](float v) { set_shake_scale(v); g_dirty = true; }, 0, kShakeMax, 0.1f, 1);
	shake.show = [show = shake.show](float v) { return v > 0 ? show(v) + "x" : std::string("Off"); };
	AddToggle(kScreen, "hud", "HUD", "Off: no crosshair, radar, health, ammunition or prompts, as in the game's cutscenes.", hud, set_hud);
	AddToggle(kScreen, "show_ping", "Show Ping", "On: in a game through Connect to WFC Modded the scoreboard has every player's ping next to their name.", show_ping, set_show_ping);

	Setting& sens = AddRange(kControls, "mouse_sens", "Mouse Sensitivity", "How far the view turns for the mouse's motion. Off leaves the mouse free.",
		mouse_sens, [](float v) { set_mouse_sens(v); g_dirty = true; }, 0, kMouseSensMax, 0.05f, 2);
	sens.show = [show = sens.show](float v) { return v > 0 ? show(v) : std::string("Off"); };
	AddRange(kControls, "ads_sens", "Aim Sensitivity", "The mouse's sensitivity aiming down the sight, times the one above. 1.00 turns as far as without aiming. A scope turns less by its zoom.",
		ads_sens, [](float v) { set_ads_sens(v); g_dirty = true; }, kAdsSensMin, kAdsSensMax, 0.05f, 2);
	AddToggle(kControls, "aim_lock", "Toggle ADS", "On: a press of Aim stays aimed down the sight until the next press.", aim_lock, set_aim_lock);
	AddToggle(kControls, "mouse_invert", "Invert Mouse", "On: pushing the mouse forward looks down.", mouse_invert, set_mouse_invert);
	AddChoice(kControls, "mouse_mode", "Mouse Look", "Auto turns the view itself, and uses the right stick in a network game someone else hosts.",
		{"Auto", "Direct", "Stick"}, mouse_mode, [](int m) { set_mouse_mode(m); g_dirty = true; });
	AddChoice(kControls, "input", "Player 1 Input", "What plays as the first controller: keyboard, mouse and gamepad together, or only one.",
		{"Keyboard + Gamepad", "Gamepad", "Keyboard"},
		[] { return index_of(kInputNames, Keys().get("Input"), 0); },
		[](int v)
		{
			Keys().set("Input", kInputNames[v]);
			save_keys();
			video_set_input(v);
		});
	AddChoice(kControls, "face_buttons", "Gamepad Face Buttons", "Position: laid out as on the Classic Controller. Label: the button marked A is A.",
		{kFaceNames[0], kFaceNames[1]},
		[] { return index_of(kFaceNames, Keys().get("Face Buttons"), 0); },
		[](int v)
		{
			Keys().set("Face Buttons", kFaceNames[v]);
			save_keys();
		});
	Setting& deadZone = AddRange(kControls, "dead_zone", "Stick Dead Zone", "How far a gamepad's sticks move before they count.",
		dead_zone, set_dead_zone, 0, kDeadZoneMax, 0.01f, 2);
	deadZone.show = [](float v) { return std::to_string((int)std::lround(v * 100)) + "%"; };
	std::vector<const char*> tankSources;
	for (int i = 0; i < kTankSourceCount; ++i) tankSources.push_back(tank_source_label(i));
	static const char* const kTankIds[kTankActionCount] = {"tank_machine_gun", "tank_cannon", "tank_lock_on"};
	for (int i = 0; i < kTankActionCount; ++i)
	{
		AddChoice(kControls, kTankIds[i], tank_action_label(i), (std::string(tank_action_hint(i)) + " Picked by what the button does on foot.").c_str(),
			tankSources, [i] { return tank_source(i); }, [i](int v) { set_tank_source(i, v); g_dirty = true; });
	}

	AddToggle(kEnhancements, "skip_movies", "Skip Movies", "On: briefings, the movies between missions and the logos are skipped.", skip_movies, set_skip_movies);
	AddToggle(kEnhancements, "level_timer", "Level Timer", "On: the hud shows the time played in the level.", level_timer, set_level_timer);
	AddToggle(kEnhancements, "auto_qte", "Auto Quick Time Events", "On: quick time events press their own buttons.", auto_qte, set_auto_qte);
	AddToggle(kEnhancements, "auto_start", "Auto Start Mission", "On: a loaded mission starts by itself, without Continue being pressed.", auto_start, set_auto_start);
	AddToggle(kEnhancements, "autosave", "Save At Checkpoints", "On: reaching a checkpoint saves the game, as Save and Quit does. After a crash Continue Game starts there.", autosave, set_autosave);
	AddToggle(kEnhancements, "no_tutorials", "Disable Tutorials", "On: the pop-up boxes that explain the controls are never shown, in any mission, training included.", no_tutorials, set_no_tutorials);
	AddToggle(kEnhancements, "no_colour_filter", "Disable Colour Filter", "On: the tint most levels lay over the picture (Dam's grey-blue, Facility's blue-green...) isn't drawn.", no_colour_filter, set_no_colour_filter);
	AddToggle(kEnhancements, "unlock_all", "Unlock Everything", "On: every mission and every difficulty can be picked. A save made with it on may keep them.", unlock_all, set_unlock_all);
	AddToggle(kEnhancements, "online_unlocks", "Online Unlocks", "On: online, every weapon, attachment, gadget and game mode is open at any level, but for the Proximity Mine.", online_unlocks, set_online_unlocks);
	AddToggle(kEnhancements, "occlusion", "Sound Occlusion", "On: in multiplayer a sound behind a wall is quieter, one behind a floor more so, so someone a floor up doesn't sound next to you. Off: the game's own, distance only.", occlusion, set_occlusion);
	AddToggle(kEnhancements, "sound_height", "Sound Height", "On: in multiplayer a sound from the floor above or below you is muffled, so you can hear that someone isn't on your floor. Off: the game's own, left and right only.", sound_height, set_sound_height);
	AddToggle(kEnhancements, "extras_menu", "Unused Extras Menu", "On: Extras also lists Mission Select, Character Viewer, Movies and the Calibration Menu.", extras_menu, set_extras_menu);

	AddToggle(kCheats, "mouse_everywhere", "Always Enable Mouse", "On: the mouse turns the view where the game locks it: cutscenes, melee, climbing, death.", mouse_everywhere, set_mouse_everywhere);
	AddToggle(kCheats, "god_mode", "God Mode", "On: nothing hurts you in the campaign. A fall out of the level or a failed mission still ends it.", god_mode, set_god_mode);
	AddToggle(kCheats, "infinite_ammo", "Infinite Ammo", "On: your ammo never runs out in the campaign.", infinite_ammo, set_infinite_ammo);
	AddToggle(kCheats, "infinite_sprint", "Infinite Sprint", "On: the sprint never runs out in the campaign.", infinite_sprint, set_infinite_sprint);
	AddToggle(kCheats, "noclip", "Noclip", "On: you fly where you look, through walls, and don't fall. The sprint key flies faster.", noclip, set_noclip);
	AddToggle(kCheats, "stamina_meter", "Stamina Meter", "On: a bar shows the sprint that's left. Cancel a sprint right under the line and it's back at a second the frame after.", stamina_meter, set_stamina_meter);
	AddToggle(kCheats, "collision_view", "Collision Viewer", "On: the level's collision is drawn in place of the world. Floors are green, walls grey, ceilings red, props orange.", collision_view, set_collision_view);

	AddToggle(kForFun, "weapon_randomizer", "Weapon Randomizer", "On: in the campaign every guard and you start with a random weapon, with one random attachment or none. From the next spawn on.", weapon_randomizer, set_weapon_randomizer);
	std::vector<const char*> weapons;
	for (int i = 0; i < forced_weapon_count(); ++i) weapons.push_back(forced_weapon_name(i));
	AddChoice(kForFun, "forced_weapon", "Forced Weapon", "In the campaign you and every guard start with this weapon, without attachments. Only while the Weapon Randomizer is off. From the next spawn on.",
		weapons, forced_weapon, [](int v) { set_forced_weapon(v); g_dirty = true; }).dropdown = true;
	AddToggle(kForFun, "no_attachment_limit", "No Attachment Limit (Experimental)", "On: the Weapon Randomizer's weapons may have several attachments at once, each one a chance of its own. Some may break the view.", no_attachment_limit, set_no_attachment_limit);

	//in a game someone else hosts through Connect to WFC Modded the page shows the host's, and nothing on it changes
	AddToggle(kBhop, "bunnyhop", "Bunnyhopping", "On: holding Jump hops whenever you touch the floor, and strafing in the air while turning adds speed, as in Half-Life. Steep ramps can be surfed. There is no sprint, you run at the base speed. Online through Connect to WFC Modded the host's settings are everyone's.",
		[] { return bhop_locked() ? bunnyhop_now() : bunnyhop(); }, [](bool on) { if (!bhop_locked()) set_bunnyhop(on); });
	AddToggle(kBhop, "bhop_prestrafe", "Pre-strafing", "On: the floor moves as in Half-Life too, so strafing while turning gains speed before the first hop, and a landing slides instead of stopping dead. Ground Accelerate and Friction set how much.",
		bhop_prestrafe_now, [](bool on) { if (!bhop_locked()) set_bhop_prestrafe(on); });
	for (int i = 0; i < kBhopValueCount; ++i)
	{
		const BhopValue& v = bhop_value(i);
		AddRange(kBhop, v.launch, v.label, v.hint, [i] { return bhop_value_now(i); }, [i](float f)
		{
			if (bhop_locked()) return;
			bhop_value(i).value = f;
			g_dirty = true;
		}, v.lo, v.hi, v.by, v.decimals);
	}
}

Row SettingRow(int i)
{
	const Setting& s = g_settings[i];
	Row r;
	r.label = s.label;
	r.hint = s.hint;
	r.value = [i] { return g_settings[i].show(g_settings[i].get()); };
	r.adjust = [i](int d) { g_settings[i].set(g_settings[i].step(g_settings[i].get(), d)); };
	if (s.frac)
	{
		r.slider = [i] { return g_settings[i].frac(g_settings[i].get()); };
		r.slide = [i](float f) { g_settings[i].set(g_settings[i].at(f)); };
	}
	if (s.dropdown)
	{
		r.options = [i]
		{
			std::vector<std::string> names;
			for (int v = 0; v < g_settings[i].count; ++v) names.push_back(g_settings[i].show((float)v));
			return names;
		};
		r.chosen = [i] { return (int)std::lround(g_settings[i].get()); };
		r.choose = [i](int v) { g_settings[i].set((float)v); };
	}
	return r;
}

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
Grid g_grids[3];
int g_gridAt = 0, g_gridScroll = 0; //the cell under the cursor, the first row shown
const int kGridVisible = 9, kLevelColumns = 4;
const float kGridLeft = 150, kGridRight = 1130, kGridTop = 190, kGridPitch = 38, kGridNames = 190;
const uint32_t kMissionFiles = 0x0181; //the top of an mt_ level's hash

bool OnGrid() { return g_page == kLevels || g_page == kMaps || g_page == kSkips; }
Grid& CurrentGrid() { return g_grids[g_page == kSkips ? 2 : g_page == kMaps ? 1 : 0]; }

//made when a grid is first opened: the disc's list is read as the game starts
void BuildGrids()
{
	if (!g_grids[0].hash.empty() || !g_grids[1].hash.empty()) return;
	std::vector<std::pair<std::string, uint32_t>> levels;
	std::vector<std::pair<std::string, std::vector<uint32_t>>> maps;
	for (const ModLevel& level : disc_levels())
	{
		if (!level_loads(level.hash)) continue;
		if (level.hash >> 16 == kMissionFiles)
		{
			levels.push_back({ mod_shown_name(level.name), level.hash });
			continue;
		}
		int mode = -1;
		const std::string map = map_shown_name(level.name, mode);
		if (mode < 0) continue;
		auto row = std::find_if(maps.begin(), maps.end(), [&map](const auto& m) { return m.first == map; });
		if (row == maps.end()) row = maps.insert(maps.end(), { map, std::vector<uint32_t>(kMapModeCount, 0) });
		row->second[mode] = level.hash;
	}
	std::sort(levels.begin(), levels.end());
	std::sort(maps.begin(), maps.end());

	Grid& names = g_grids[0];
	names.cols = kLevelColumns;
	for (const auto& [name, hash] : levels)
	{
		names.hash.push_back(hash);
		names.label.push_back(name);
		names.about.push_back(name);
	}
	while (names.hash.size() % kLevelColumns)
	{
		names.hash.push_back(0);
		names.label.emplace_back();
		names.about.emplace_back();
	}

	Grid& table = g_grids[1];
	table.cols = kMapModeCount;
	for (int mode = 0; mode < kMapModeCount; ++mode) table.columns.push_back(map_mode_name(mode));
	for (const auto& [map, hashes] : maps)
	{
		table.rowNames.push_back(map);
		for (int mode = 0; mode < kMapModeCount; ++mode)
		{
			table.hash.push_back(hashes[mode]);
			table.label.push_back(hashes[mode] ? "Play" : "");
			table.about.push_back(map + " - " + map_mode_name(mode));
		}
	}
}

//the cutscenes, from config/openings.txt and config/cutscenes.txt; the first row turns them all on or off
const char* const kSkipColumns[kSkipParts] = {"Intro", "Middle", "Ending"};
void BuildSkipGrid()
{
	Grid& grid = g_grids[2];
	grid = Grid();
	grid.cols = kSkipParts;
	grid.columns.assign(kSkipColumns, kSkipColumns + kSkipParts);
	grid.rowNames.push_back("All");
	grid.hash = {1, 1, 0};
	grid.label = {"Enable All", "Disable All", ""};
	grid.about = {"Skip every cutscene listed", "Skip none of them", ""};
	for (int m = 0; m < skip_missions(); ++m)
	{
		grid.rowNames.push_back(skip_mission_name(m));
		for (int p = 0; p < kSkipParts; ++p)
		{
			grid.hash.push_back(skip_exists(m, p) ? 1 : 0);
			grid.label.emplace_back();
			grid.about.push_back(std::string(skip_mission_name(m)) + ", " + kSkipColumns[p]);
		}
	}
	grid.pick = [](int i)
	{
		if (i < kSkipParts) set_skip_all(i == 0);
		else set_skip_on(i / kSkipParts - 1, i % kSkipParts, !skip_on(i / kSkipParts - 1, i % kSkipParts));
		g_dirty = true;
	};
	grid.lit = [](int i) { return i < kSkipParts ? -1 : skip_on(i / kSkipParts - 1, i % kSkipParts) ? 1 : 0; };
}

void OpenGrid()
{
	if (g_page == kSkips) BuildSkipGrid();
	else BuildGrids();
	const Grid& grid = CurrentGrid();
	g_gridAt = g_gridScroll = 0;
	while (g_gridAt + 1 < (int)grid.hash.size() && !grid.hash[g_gridAt]) ++g_gridAt;
}

//to the next cell with a level that way, the cursor stays when there's none
void GridMove(int dx, int dy)
{
	const Grid& grid = CurrentGrid();
	int at = g_gridAt;
	for (;;)
	{
		int row = at / grid.cols + dy, col = at % grid.cols + dx;
		if (row < 0 || row >= grid.Rows() || col < 0 || col >= grid.cols) return;
		at = row * grid.cols + col;
		if (grid.hash[at]) break;
	}
	g_gridAt = at;
}

void open(bool on);

void GridLoad()
{
	const Grid& grid = CurrentGrid();
	if (g_gridAt >= (int)grid.hash.size() || !grid.hash[g_gridAt]) return;
	if (grid.pick)
	{
		grid.pick(g_gridAt);
		return;
	}
	if (guest_ptr(ld32(kPlayerPtr))) //the menu's load ends the game when a level is running already
	{
		g_toast = "Quit to the main menu first, a level loads from there";
		g_toastUntil = Clock::now() + std::chrono::milliseconds(2500);
		open(false);
		return;
	}
	load_level(grid.hash[g_gridAt]);
	g_toast = "Loading " + grid.about[g_gridAt];
	g_toastUntil = Clock::now() + std::chrono::milliseconds(1800);
	open(false);
}

void go(int page, int row = 0) {
    if (g_page == kRoot) g_root_row = g_row;
    g_page = page;
    g_row = row;
    if (OnGrid()) OpenGrid();
    g_scroll = 0;
    g_listening = g_dragging = g_drop = -1;
}

Hotkey* EditedHotkey() { return g_edit >= 0 && g_edit < (int)Hotkeys().size() ? &Hotkeys()[g_edit] : nullptr; }

void ChangeHotkey(const std::function<void(Hotkey&)>& change)
{
	Hotkey* h = EditedHotkey();
	if (!h) return;
	change(*h);
	SaveHotkeys();
	g_hotkeysChanged = true;
}

void RemoveHotkey(int i)
{
	if (i < 0 || i >= (int)Hotkeys().size()) return;
	Hotkeys().erase(Hotkeys().begin() + i);
	SaveHotkeys();
	g_hotkeysChanged = true;
}

//the list, and the page for the one being edited. built again after every change, never from
//inside a row (RefreshHotkeyPages)
void BuildHotkeyPages()
{
	Page& list = g_pages[kHotkeys];
	list.rows.clear();
	for (size_t n = 0; n < Hotkeys().size(); ++n)
	{
		int i = (int)n;
		Row r;
		r.label = Describe(Hotkeys()[n]);
		r.hint = "Select to change it. Delete removes it.";
		r.binding = r.wide = true;
		r.does = "Edit";
		r.clears = "Remove";
		r.value = [i] { return i < (int)Hotkeys().size() && !Hotkeys()[i].key.empty() ? Hotkeys()[i].key : std::string("-"); };
		r.select = [i]
		{
			g_edit = i;
			g_hotkeysChanged = true;
			go(kHotkeyEdit);
		};
		r.clear = [i] { RemoveHotkey(i); };
		list.rows.push_back(r);
	}
	Row add;
	add.label = "Add Hotkey";
	add.hint = "A key that changes one of the settings while you play, with the menu closed.";
	add.select = []
	{
		Hotkeys().push_back(NewHotkey(0));
		SaveHotkeys();
		g_edit = (int)Hotkeys().size() - 1;
		g_hotkeysChanged = true;
		go(kHotkeyEdit);
	};
	list.rows.push_back(add);

	Page& edit = g_pages[kHotkeyEdit];
	edit.rows.clear();
	Hotkey* h = EditedHotkey();
	if (!h) //removed: only the way back is left
	{
		edit.rows.push_back({"Back", "The hotkey is gone.", {}, {}, [] { go(kHotkeys); }});
		return;
	}
	const Setting& s = g_settings[h->setting];

	Row setting;
	setting.label = "Setting";
	setting.hint = "What the key changes: " + s.hint;
	setting.value = [] { Hotkey* h = EditedHotkey(); return h ? g_settings[h->setting].label : std::string(); };
	setting.options = []
	{
		std::vector<std::string> labels;
		for (const Setting& s : g_settings) labels.push_back(s.label);
		return labels;
	};
	setting.chosen = [] { Hotkey* h = EditedHotkey(); return h ? h->setting : 0; };
	setting.choose = [](int i)
	{
		ChangeHotkey([i](Hotkey& h)
		{
			if (i == h.setting) return;
			std::string key = h.key;
			h = NewHotkey(i);
			h.key = key;
		});
	};
	edit.rows.push_back(setting);

	Row action;
	action.label = "Action";
	action.hint = h->action == Hotkey::kSwitch ? "Switch To: the key goes to the value, and back to what it was on the next press."
		: "What a press of the key does to it.";
	action.value = [] { Hotkey* h = EditedHotkey(); return h ? ActionName(*h) : std::string(); };
	action.options = [] { Hotkey* h = EditedHotkey(); return h ? ActionNames(h->setting) : std::vector<std::string>(); };
	action.chosen = [] { Hotkey* h = EditedHotkey(); return h ? ActionIndex(*h) : 0; };
	action.choose = [](int i) { ChangeHotkey([i](Hotkey& h) { SetAction(h, i); }); };
	edit.rows.push_back(action);

	if (HasValue(*h) && s.kind != Setting::kToggle)
	{
		Row value;
		value.label = "Value";
		value.hint = "What the key sets it to.";
		value.value = [] { Hotkey* h = EditedHotkey(); return h ? g_settings[h->setting].show(h->value) : std::string(); };
		if (s.kind == Setting::kChoice)
		{
			value.options = []
			{
				std::vector<std::string> names;
				if (Hotkey* h = EditedHotkey())
				{
					for (int v = 0; v < g_settings[h->setting].count; ++v) names.push_back(g_settings[h->setting].show((float)v));
				}
				return names;
			};
			value.chosen = [] { Hotkey* h = EditedHotkey(); return h ? (int)std::lround(h->value) : 0; };
			value.choose = [](int i) { ChangeHotkey([i](Hotkey& h) { h.value = (float)i; }); };
		}
		else value.adjust = [](int d) { ChangeHotkey([d](Hotkey& h) { h.value = g_settings[h.setting].step(h.value, d); }); };
		if (s.frac)
		{
			value.slider = [] { Hotkey* h = EditedHotkey(); return h ? g_settings[h->setting].frac(h->value) : 0.0f; };
			value.slide = [](float f) { ChangeHotkey([f](Hotkey& h) { h.value = g_settings[h.setting].at(f); }); };
		}
		edit.rows.push_back(value);
	}

	Row key;
	key.label = "Key";
	key.hint = "Select to choose the key. Delete leaves the hotkey without one.";
	key.binding = true;
	key.does = "Set Key";
	key.value = [] { Hotkey* h = EditedHotkey(); return h && !h->key.empty() ? h->key : std::string("-"); };
	key.select = [row = (int)edit.rows.size()] { g_listening = row; };
	key.clear = [] { ChangeHotkey([](Hotkey& h) { h.key.clear(); }); };
	edit.rows.push_back(key);

	edit.rows.push_back({"Remove Hotkey", "Takes the hotkey off its key and out of the list.", {}, {}, []
	{
		RemoveHotkey(g_edit);
		go(kHotkeys, std::max(0, g_edit - 1));
	}});
}

//after an event or before the picture, when no row is running
void RefreshHotkeyPages()
{
	if (!g_hotkeysChanged) return;
	g_hotkeysChanged = false;
	BuildHotkeyPages();
	g_row = std::clamp(g_row, 0, (int)g_pages[g_page].rows.size() - 1);
	if (g_listening >= (int)g_pages[g_page].rows.size()) g_listening = -1;
	g_drop = -1; //its row may be another one now
	if (g_dragging >= 0 && !g_pages[g_page].rows[std::min(g_dragging, (int)g_pages[g_page].rows.size() - 1)].slide) g_dragging = -1;
}

void build_pages() {
    g_pages.resize(kPageCount);
    Page& root = g_pages[kRoot];
    root.title = "Settings";
    root.rows.push_back({"Screen Settings", "Adjust the field of view, the frame rate and the picture.", {}, {}, [] { go(kScreen); }});
    root.rows.push_back({"Control Settings", "Adjust the mouse and the gamepad.", {}, {}, [] { go(kControls); }});
    root.rows.push_back({"Key Bindings", "Choose the keys and mouse buttons for each action.", {}, {}, [] { go(kBindings); }});
    root.rows.push_back({"Hotkeys", "Keys that change a setting while you play.", {}, {}, [] { go(kHotkeys); }});
    root.rows.push_back({"Enhancements", "Things the port adds to the game.", {}, {}, [] { go(kEnhancements); }});
    root.rows.push_back({"Cheats", "Things the game itself would not let you do.", {}, {}, [] { go(kCheats); }});
    root.rows.push_back({"For Fun", "Things that change how the game plays, just for fun.", {}, {}, [] { go(kForFun); }});
    root.rows.push_back({"Level Select", "Load any level of the disc, the unused ones too.", {}, {}, [] { go(kLevels); }});
    root.rows.push_back({"Multiplayer Maps", "Load any multiplayer map of the disc to walk around in, the unused ones too.", {}, {}, [] { go(kMaps); }});
    //a grid's one row is what the foot reads: the cell under the cursor (settings_draw)
    for (int page : {kLevels, kMaps, kSkips}) {
        Row cell;
        cell.select = [] { GridLoad(); };
        cell.does = page == kSkips ? "Toggle" : "Load";
        g_pages[page].rows.push_back(cell);
    }
    g_pages[kLevels].title = "Level Select";
    g_pages[kMaps].title = "Multiplayer Maps";
    g_pages[kSkips].title = "Skip Cutscenes";

    g_pages[kEnhancements].rows.push_back({"Skip Cutscenes (Experimental)", "Choose which cutscenes are skipped: each mission's intro, the one in its middle and its ending.", {}, {}, [] { go(kSkips); }});
    BuildSettings();
    for (size_t i = 0; i < g_settings.size(); ++i) g_pages[g_settings[i].page].rows.push_back(SettingRow((int)i));
	auto& hop = g_pages[kBhop].rows; //the jump right under its toggle
	hop.insert(hop.begin() + std::min<size_t>(1, hop.size()), BindingRow((int)std::size(kActions) - 1));
	hop.push_back({"Settings", "Online through Connect to WFC Modded the host's are everyone's: a host sends its own, and in a game someone else hosts they're shown here and can't be changed. Yours are back after the game.",
		[] { return std::string(bhop_locked() ? "The host's, locked" : "Yours"); }, {}, {}});
	hop.push_back({"Defaults", "Every value above back to its own. The switch and the jump key stay as they are.", {}, {}, []
	{
		if (bhop_locked()) return;
		bhop_defaults();
		g_dirty = true;
	}});
	auto& fun = g_pages[kForFun].rows;
	const auto limit = std::find_if(fun.begin(), fun.end(), [](const Row& r) { return r.label == "No Attachment Limit (Experimental)"; });
	fun.insert(limit, {"Bunnyhopping", "Half-Life's bunnyhopping and air strafing: its switch, the jump key and how it feels.", []
	{
		if (bhop_locked()) return std::string(bunnyhop_now() ? "On (host)" : "Off (host)");
		return std::string(bunnyhop() ? "On" : "Off");
	}, {}, [] { go(kBhop); }});

    Page& screen = g_pages[kScreen];
    screen.title = "Screen Settings";
    screen.rows.push_back({"Restore Defaults", "Back to the game's own fields of view and picture, and 60 frames a second.", {}, {}, [] {
        set_fov(kGameFov);
        set_ads_fov(kGameAdsFov);
        set_fps_limit(60);
        set_anisotropy(1);
        set_anti_aliasing(1);
        set_shake_scale(1.0f);
        set_hud(true);
        g_fps_changed = g_dirty = true;
    }});

    Page& controls = g_pages[kControls];
    controls.title = "Control Settings";
    controls.rows.push_back({"Restore Defaults", "Back to the mouse and gamepad settings the port starts with.", {}, {}, [] {
        set_mouse_sens(1.0f);
        set_ads_sens(0.5f);
        set_aim_lock(false);
        set_mouse_invert(false);
        set_mouse_mode(0);
        for (int i = 0; i < kTankActionCount; ++i) set_tank_source(i, i);
        Keys().set("Input", "Auto");
        g_keys.set("Face Buttons", "Position");
        g_keys.set("Dead Zone", "0.15");
        save_keys();
        video_set_input(0);
        g_dirty = true;
    }});

    Page& bindings = g_pages[kBindings];
    bindings.title = "Key Bindings";
    for (size_t i = 0; i < std::size(kActions); ++i)
        if (!kActions[i].forFun) bindings.rows.push_back(BindingRow((int)i));
    bindings.rows.push_back({"Restore Defaults", "Back to the port's own keys for every action.", {}, {}, restore_bindings});

    g_pages[kEnhancements].title = "Enhancements";
    g_pages[kCheats].title = "Cheats";
    g_pages[kForFun].title = "For Fun";
    g_pages[kBhop].title = "Bunnyhopping";
    g_pages[kHotkeys].title = "Hotkeys";
    g_pages[kHotkeyEdit].title = "Hotkey";
    BuildHotkeyPages();
}

//the key or mouse button pressed for the listening action goes on that one and off every other
void bind(const std::string& key) {
    if (g_listenAction < 0) return;
    const char* name = kActions[g_listenAction].name;
    for (auto& a : kActions) {
        auto keys = g_keys.keys(a.name);
        size_t before = keys.size();
        std::erase_if(keys, [&](const std::string& k) { return !SDL_strcasecmp(k.c_str(), key.c_str()); });
        if (a.name == name) keys.push_back(key);
        else if (keys.size() == before) continue;
        g_keys.set_keys(a.name, keys);
    }
    save_keys();
    g_listening = -1;
}

//a key file from before sprint had a key of its own: Left Shift goes from the action to the sprint
//once the runtime has read the file (the first event comes after)
void add_sprint_binding() {
    static bool done = false;
    if (done || video_keys_path().empty()) return;
    done = true;
    load_keys();
    if (g_keys.lines.empty() || g_keys.find("Extra 1") >= 0) return;
    auto keys = g_keys.keys("B");
    std::erase_if(keys, [](const std::string& k) { return !SDL_strcasecmp(k.c_str(), "Left Shift"); });
    g_keys.set_keys("B", keys);
    g_keys.set("Extra 1", "Left Shift");
    save_keys();
    rt_log("goldeneye: sprint is on Left Shift now, on its own (Extra 1 in %s)", video_keys_path().c_str());
}

//a key file from before the weapon slots had keys: 1, 2 and 3 go to them (Extra 3 to 5)
//1 and 2 were the previous and the next weapon, a number that's on anything else stays there
void AddWeaponBindings()
{
	static bool done = false;
	if (done || video_keys_path().empty()) return;
	done = true;
	if (g_keys.lines.empty() || g_keys.find("Extra 3") >= 0) return;
	const char* const was[] = {"Left", "Right", ""};
	for (int n = 0; n < 3; ++n)
	{
		const std::string key(1, (char)('1' + n)), slot = "Extra " + std::to_string(n + 3);
		auto is = [&](const std::string& k) { return !SDL_strcasecmp(k.c_str(), key.c_str()); };
		bool taken = false;
		for (auto& a : kActions)
		{
			auto keys = g_keys.keys(a.name);
			if (std::string(a.name) != was[n] && std::any_of(keys.begin(), keys.end(), is)) taken = true;
		}
		if (*was[n])
		{
			auto keys = g_keys.keys(was[n]);
			if (!taken && std::erase_if(keys, is)) g_keys.set_keys(was[n], keys);
		}
		g_keys.set(slot, taken ? "" : key);
	}
	save_keys();
	rt_log("goldeneye: the weapon slots are on 1, 2 and 3 now (Extra 3 to 5 in %s)", video_keys_path().c_str());
}

//a key file from before the jump: Space goes from the action to it, when the action keeps a key
void AddJumpBinding()
{
	static bool done = false;
	if (done || video_keys_path().empty()) return;
	done = true;
	if (g_keys.lines.empty() || g_keys.find("Extra 6") >= 0) return;
	auto is = [](const std::string& k) { return !SDL_strcasecmp(k.c_str(), "Space"); };
	bool taken = false;
	for (auto& a : kActions)
	{
		auto keys = g_keys.keys(a.name);
		if (std::string(a.name) != "B" && std::any_of(keys.begin(), keys.end(), is)) taken = true;
	}
	auto action = g_keys.keys("B");
	if (!taken && std::any_of(action.begin(), action.end(), is))
	{
		std::erase_if(action, is);
		if (action.empty()) taken = true;
		else g_keys.set_keys("B", action);
	}
	g_keys.set("Extra 6", taken ? "" : "Space");
	save_keys();
	rt_log("goldeneye: the jump is on %s (Extra 6 in %s)", taken ? "no key" : "Space", video_keys_path().c_str());
}

//opening, closing, moving
void open(bool on) {
    if (on == g_open) return;
    g_open = on;
    video_block_input(on);
    if (on) {
        load_keys();
        g_page = kRoot;
        g_row = g_scroll = 0;
        g_listening = g_dragging = g_drop = -1;
        if (in_play()) //the game's own pause, before the menu takes the input away
            g_pause_until = (Clock::now() + std::chrono::milliseconds(150)).time_since_epoch().count();
    } else if (g_dirty) {
        save_launch_settings();
        g_dirty = false;
    }
    mouse_capture(!overlay_open());
}

//the For Fun row that opens the bunnyhopping page
int BhopRow()
{
	const auto& fun = g_pages[kForFun].rows;
	for (size_t i = 0; i < fun.size(); ++i)
		if (fun[i].label == "Bunnyhopping") return (int)i;
	return 0;
}

void back() {
    if (g_drop >= 0) g_drop = -1;
    else if (g_listening >= 0) g_listening = -1;
    else if (g_page == kRoot) open(false);
    else if (g_page == kHotkeyEdit) go(kHotkeys, std::max(0, g_edit));
    else if (g_page == kSkips) go(kEnhancements, 0);
    else if (g_page == kBhop) go(kForFun, BhopRow());
    else go(kRoot, g_root_row);
}

void move(int d) {
    int n = (int)g_pages[g_page].rows.size();
    g_row = (g_row + d + n) % n;
}

void adjust(int d) {
    Row& r = g_pages[g_page].rows[g_row];
    if (r.adjust) r.adjust(d);
}

//the list opens on the one the row has now
void OpenDrop(int row)
{
	const Row& r = g_pages[g_page].rows[row];
	g_dropOptions = r.options();
	if (g_dropOptions.empty()) return;
	g_drop = row;
	g_dropAt = std::clamp(r.chosen(), 0, (int)g_dropOptions.size() - 1);
	g_dropScroll = g_dropAt;
}

void MoveDrop(int d, bool wrap)
{
	int n = (int)g_dropOptions.size();
	g_dropAt = wrap ? (g_dropAt + d + n) % n : std::clamp(g_dropAt + d, 0, n - 1);
}

void ChooseDrop()
{
	Row& r = g_pages[g_page].rows[g_drop];
	int at = g_dropAt;
	g_drop = -1;
	r.choose(at);
}

void select() {
    Row& r = g_pages[g_page].rows[g_row];
    if (r.options) OpenDrop(g_row);
    else if (r.select) r.select();
    else if (r.adjust) r.adjust(1);
}

//the picture
ImFont* g_font = nullptr;
const float kFontSize = 48, kNarrow = 0.80f; //the game's font is narrow, so the glyphs get drawn squeezed
const ImU32 kCream = IM_COL32(243, 232, 208, 255), kBlue = IM_COL32(88, 124, 170, 255),
            kBar = IM_COL32(70, 100, 146, 150), kSelected = IM_COL32(255, 255, 255, 245),
            kSelectedText = IM_COL32(84, 146, 212, 255), kShadow = IM_COL32(0, 0, 0, 150),
            kDim = IM_COL32(6, 14, 18, 215), kGrid = IM_COL32(90, 150, 150, 22), kDark = IM_COL32(26, 32, 40, 255);

float text_width(const std::string& s, float size) {
    float w = 0;
    for (unsigned char c : s) w += g_font->GetCharAdvance(c);
    return w * size / g_font->FontSize * kNarrow;
}

void text(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const std::string& s, bool shadow = true) {
    float k = size / g_font->FontSize;
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
        float x = at.x + (pass ? 0 : size * 0.05f), y = at.y + (pass ? 0 : size * 0.05f);
        for (unsigned char c : s) {
            const ImFontGlyph* g = g_font->FindGlyph(c);
            if (!g) continue;
            if (g->Visible) {
                dl->PrimReserve(6, 4);
                dl->PrimRectUV(ImVec2(x + g->X0 * k * kNarrow, y + g->Y0 * k), ImVec2(x + g->X1 * k * kNarrow, y + g->Y1 * k),
                               ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V1), pass ? colour : kShadow);
            }
            x += g->AdvanceX * k * kNarrow;
        }
    }
}

//as large as size, smaller if that's what fits in width
float fit(const std::string& s, float size, float width) {
    float w = text_width(s, size);
    return w > width ? size * width / w : size;
}

ImU32 fade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a) << IM_COL32_A_SHIFT); }

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
Layout g_layout;
float rows_top() { return g_pages[g_page].rows.size() > 4 ? 178.0f : 276.0f; }
int DropShown() { return std::min(kDropVisible, (int)g_dropOptions.size()); }
float DropHeight() { return kDropPitch * (float)DropShown() + 2 * kDropPad; }
float DropTop() //under its row
{
	float under = rows_top() + kRowPitch * (float)(g_drop - g_scroll) + kRowHeight + 3;
	return std::min(under, kDropFloor - DropHeight());
}

//a key's cap and what it does, like the game's "(b) Back"
float key_hint(ImDrawList* dl, const Layout& L, float x, float y, const char* key, const char* what, bool right) {
    float size = 24 * L.u, cap = 17 * L.u, pad = 7 * L.u;
    float kw = text_width(key, cap) + 2 * pad, ww = text_width(what, size), gap = 12 * L.u;
    if (right) x -= kw + gap + ww;
    float kx = right ? x + ww + gap : x, wx = right ? x : x + kw + gap;
    dl->AddRectFilled(ImVec2(kx, y), ImVec2(kx + kw, y + 26 * L.u), kDark, 13 * L.u);
    dl->AddRect(ImVec2(kx, y), ImVec2(kx + kw, y + 26 * L.u), fade(kCream, 0.5f), 13 * L.u, 0, 1.5f * L.u);
    text(dl, ImVec2(kx + pad, y + 4 * L.u), cap, kCream, key, false);
    text(dl, ImVec2(wx, y), size, kCream, what);
    return kw + gap + ww;
}

void draw_frame(ImDrawList* dl, const Layout& L, const Page& page) {
    float u = L.u;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(L.w, L.h), kDim);
    for (float gx = std::fmod(L.cx, 64 * u); gx < L.w; gx += 64 * u) dl->AddLine(ImVec2(gx, 0), ImVec2(gx, L.h), kGrid, u);
    for (float gy = 0; gy < L.h; gy += 64 * u) dl->AddLine(ImVec2(0, gy), ImVec2(L.w, gy), kGrid, u);

    //the title with its node and the bar that runs off to the right
    ImVec2 node(L.x(260), L.y(128));
    dl->AddLine(ImVec2(L.x(150), L.y(18)), node, kCream, 5 * u);
    dl->AddRectFilled(ImVec2(node.x, L.y(79)), ImVec2(L.x(318), L.y(123)), kCream);
    dl->AddCircleFilled(node, 49 * u, kCream, 48);
    dl->AddRectFilled(ImVec2(node.x, L.y(87)), ImVec2(L.x(311), L.y(115)), kBlue);
    dl->AddCircleFilled(node, 41 * u, kBlue, 48);
    dl->AddCircleFilled(node, 11 * u, kCream, 24);
    dl->AddCircleFilled(node, 5 * u, kDark, 16);
    ImU32 none = fade(kCream, 0), blue0 = fade(kBlue, 0);
    dl->AddRectFilledMultiColor(ImVec2(L.x(600), L.y(86)), ImVec2(L.x(760), L.y(89)), none, kCream, kCream, none);
    dl->AddRectFilled(ImVec2(L.x(760), L.y(86)), ImVec2(L.w, L.y(89)), kCream);
    dl->AddRectFilledMultiColor(ImVec2(L.x(620), L.y(96)), ImVec2(L.x(820), L.y(118)), blue0, kBlue, kBlue, blue0);
    dl->AddRectFilled(ImVec2(L.x(820), L.y(96)), ImVec2(L.w, L.y(118)), kBlue);
    dl->AddRectFilled(ImVec2(L.x(322), L.y(123)), ImVec2(L.w, L.y(126)), kCream);
    text(dl, ImVec2(L.x(323), L.y(78)), 40 * u, kCream, g_page == kBhop && bhop_locked() ? page.title + " (Host's)" : page.title);

    //the foot, what the row under the cursor is for and the keys
    for (float y : {545.0f, 668.0f}) {
        dl->AddRectFilledMultiColor(ImVec2(L.x(200), L.y(y)), ImVec2(L.x(300), L.y(y + 3)), none, kCream, kCream, none);
        dl->AddRectFilled(ImVec2(L.x(300), L.y(y)), ImVec2(L.x(980), L.y(y + 3)), kCream);
        dl->AddRectFilledMultiColor(ImVec2(L.x(980), L.y(y)), ImVec2(L.x(1080), L.y(y + 3)), kCream, none, none, kCream);
    }
    const Row& row = page.rows[g_row];
    std::string hint = g_listening < 0 ? row.hint : g_page == kHotkeyEdit ? "Press a key. Esc leaves it as it is."
        : "Press a key or a mouse button. Esc leaves it as it is.";
    float hs = fit(hint, 24 * u, 840 * u);
    text(dl, ImVec2(L.cx - text_width(hint, hs) / 2, L.y(592)), hs, kCream, hint);
    float left = L.x(210);
    left += key_hint(dl, L, left, L.y(625), "Esc", g_page == kRoot ? "Close" : "Back", false) + 28 * u;
    if (row.clear && g_listening < 0) key_hint(dl, L, left, L.y(625), "Del", row.clears ? row.clears : "Clear", false);
    const char* does = row.does ? row.does : row.options ? "Choose" : row.binding ? "Add Key" : row.select || !row.adjust ? "Select" : "Change";
    if (g_listening < 0) key_hint(dl, L, L.x(1070), L.y(625), row.select || !row.adjust ? "Enter" : "< >", does, true);
}

void draw_rows(ImDrawList* dl, const Layout& L, const Page& page) {
    float u = L.u;
    int n = (int)page.rows.size();
    g_scroll = std::clamp(g_scroll, std::max(0, g_row - kVisible + 1), g_row);
    g_scroll = std::clamp(g_scroll, 0, std::max(0, n - kVisible));
    float x0 = L.x(kRowLeft), x1 = L.x(kRowRight);
    for (int i = g_scroll; i < n && i < g_scroll + kVisible; ++i) {
        const Row& r = page.rows[i];
        bool sel = i == g_row;
        float y0 = L.y(rows_top() + kRowPitch * (float)(i - g_scroll)), y1 = y0 + kRowHeight * u;
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), sel ? kSelected : kBar);
        ImU32 colour = sel ? kSelectedText : kCream;
        float size = 31 * u, ty = y0 + 3 * u;
        float ls = r.wide ? fit(r.label, size, (x1 - x0) * 0.7f) : size; //a long one leaves room for its value
        text(dl, ImVec2(x0 + 14 * u, ty + (size - ls) * 0.55f), ls, colour, r.label, !sel);
        if (!r.value) continue;
        std::string v = r.value();
        if (r.options) { //what it's on and the arrow that says there's a list
            float a = 7 * u, ax = x1 - 14 * u - a, ay = (y0 + y1) / 2 - a * 0.5f;
            dl->AddTriangleFilled(ImVec2(ax - a, ay), ImVec2(ax + a, ay), ImVec2(ax, ay + a), colour);
            float room = ax - a - 12 * u - (x0 + 44 * u + text_width(r.label, ls)), vs = fit(v, 26 * u, room);
            text(dl, ImVec2(ax - a - 12 * u - text_width(v, vs), ty + (size - vs) * 0.55f), vs, colour, v, !sel);
            continue;
        }
        if (r.binding || r.wide) { //the keys, against the right edge
            if (g_listening == i) v = std::fmod(ImGui::GetTime(), 1.0) < 0.6 ? "Press a key..." : "";
            float room = x1 - x0 - 44 * u - text_width(r.label, ls), vs = fit(v, 26 * u, room);
            text(dl, ImVec2(x1 - 14 * u - text_width(v, vs), ty + (size - vs) * 0.55f), vs, colour, v, !sel);
            continue;
        }
        float mid = (y0 + y1) / 2;
        if (r.slider) { //the track, filled up to the knob, and the number
            float sx0 = L.x(kSliderLeft), sx1 = L.x(kSliderRight), kx = sx0 + (sx1 - sx0) * std::clamp(r.slider(), 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(sx0, mid - 2 * u), ImVec2(sx1, mid + 2 * u), fade(colour, 0.35f));
            dl->AddRectFilled(ImVec2(sx0, mid - 4 * u), ImVec2(kx, mid + 4 * u), colour);
            dl->AddRectFilled(ImVec2(kx - 4 * u, mid - 13 * u), ImVec2(kx + 4 * u, mid + 13 * u), colour);
            float vs = 28 * u;
            text(dl, ImVec2(x1 - 14 * u - text_width(v, vs), ty + (size - vs) * 0.55f), vs, colour, v, !sel);
            continue;
        }
        float vx1 = x1 - 12 * u, vx0 = vx1 - kValueWidth * u, a = 8 * u;
        float vs = fit(v, 28 * u, (kValueWidth - 50) * u);
        text(dl, ImVec2((vx0 + vx1 - text_width(v, vs)) / 2, ty + (size - vs) * 0.55f), vs, colour, v, !sel);
        if (sel) {
            dl->AddTriangleFilled(ImVec2(vx0, mid), ImVec2(vx0 + a, mid - a), ImVec2(vx0 + a, mid + a), colour);
            dl->AddTriangleFilled(ImVec2(vx1, mid), ImVec2(vx1 - a, mid - a), ImVec2(vx1 - a, mid + a), colour);
        }
    }
    //more above, more below
    float mx = (x0 + x1) / 2, a = 9 * u;
    if (g_scroll > 0) {
        float y = L.y(rows_top() - 10);
        dl->AddTriangleFilled(ImVec2(mx, y - a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
    }
    if (g_scroll + kVisible < n) {
        float y = L.y(rows_top() + kRowPitch * kVisible + 5);
        dl->AddTriangleFilled(ImVec2(mx, y + a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
    }
}

//the open dropdown's list, over the rows under it
void DrawDrop(ImDrawList* dl, const Layout& L)
{
	float u = L.u;
	int n = (int)g_dropOptions.size(), shown = DropShown();
	g_dropScroll = std::clamp(g_dropScroll, std::max(0, g_dropAt - shown + 1), g_dropAt);
	g_dropScroll = std::clamp(g_dropScroll, 0, n - shown);
	float x0 = L.x(kDropLeft), x1 = L.x(kRowRight), top = L.y(DropTop()), bottom = top + DropHeight() * u;
	dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, bottom), kDark);
	dl->AddRect(ImVec2(x0, top), ImVec2(x1, bottom), kCream, 0, 0, 2 * u);
	int chosen = g_pages[g_page].rows[g_drop].chosen();
	for (int i = g_dropScroll; i < g_dropScroll + shown; ++i)
	{
		bool sel = i == g_dropAt;
		float y0 = top + (kDropPad + kDropPitch * (float)(i - g_dropScroll)) * u, y1 = y0 + (kDropPitch - 2) * u;
		if (sel) dl->AddRectFilled(ImVec2(x0 + kDropPad * u, y0), ImVec2(x1 - kDropPad * u, y1), kSelected);
		ImU32 colour = sel ? kSelectedText : kCream;
		if (i == chosen) dl->AddCircleFilled(ImVec2(x0 + 20 * u, (y0 + y1) / 2), 4 * u, colour, 12);
		float size = fit(g_dropOptions[i], 26 * u, x1 - x0 - 50 * u);
		text(dl, ImVec2(x0 + 34 * u, y0 + 3 * u + (26 * u - size) * 0.55f), size, colour, g_dropOptions[i], !sel);
	}
	//more above, more below
	float ax = x1 - 18 * u, a = 6 * u;
	if (g_dropScroll > 0) dl->AddTriangleFilled(ImVec2(ax, top - a - 3 * u), ImVec2(ax - a, top - 3 * u), ImVec2(ax + a, top - 3 * u), kCream);
	if (g_dropScroll + shown < n) dl->AddTriangleFilled(ImVec2(ax, bottom + a + 3 * u), ImVec2(ax - a, bottom + 3 * u), ImVec2(ax + a, bottom + 3 * u), kCream);
}

//where a grid's cells start after its row names, and how wide one is
void GridColumns(const Layout& L, const Grid& grid, float& x0, float& width)
{
	x0 = L.x(kGridLeft + (grid.rowNames.empty() ? 0.0f : kGridNames));
	width = (L.x(kGridRight) - x0) / (float)grid.cols;
}

void DrawGrid(ImDrawList* dl, const Layout& L)
{
	const Grid& grid = CurrentGrid();
	float u = L.u, x0, cw;
	GridColumns(L, grid, x0, cw);
	int rows = grid.Rows(), at = g_gridAt / grid.cols;
	g_gridScroll = std::clamp(g_gridScroll, std::max(0, at - kGridVisible + 1), at);
	g_gridScroll = std::clamp(g_gridScroll, 0, std::max(0, rows - kGridVisible));
	for (int col = 0; col < (int)grid.columns.size(); ++col)
	{
		float size = fit(grid.columns[col], 21 * u, cw - 8 * u);
		text(dl, ImVec2(x0 + cw * ((float)col + 0.5f) - text_width(grid.columns[col], size) / 2, L.y(kGridTop - 30) + (21 * u - size) * 0.55f), size, kCream, grid.columns[col]);
	}
	for (int row = g_gridScroll; row < rows && row < g_gridScroll + kGridVisible; ++row)
	{
		float y0 = L.y(kGridTop + kGridPitch * (float)(row - g_gridScroll)), y1 = y0 + (kGridPitch - 4) * u;
		if (!grid.rowNames.empty())
		{
			float size = fit(grid.rowNames[row], 27 * u, (kGridNames - 12) * u);
			text(dl, ImVec2(L.x(kGridLeft), y0 + 3 * u + (27 * u - size) * 0.55f), size, row == at ? kSelected : kCream, grid.rowNames[row]);
		}
		for (int col = 0; col < grid.cols; ++col)
		{
			int i = row * grid.cols + col;
			ImVec2 a(x0 + cw * (float)col + 2 * u, y0), b(x0 + cw * (float)(col + 1) - 2 * u, y1);
			if (!grid.hash[i])
			{
				dl->AddRectFilled(a, b, fade(kBar, 0.25f));
				continue;
			}
			bool sel = i == g_gridAt;
			int lit = grid.lit ? grid.lit(i) : -1;
			const std::string label = lit < 0 ? grid.label[i] : lit ? "On" : "Off";
			dl->AddRectFilled(a, b, sel ? kSelected : lit == 0 ? fade(kBar, 0.45f) : kBar);
			float size = fit(label, 26 * u, cw - 18 * u);
			text(dl, ImVec2((a.x + b.x - text_width(label, size)) / 2, y0 + 3 * u + (26 * u - size) * 0.55f), size, sel ? kSelectedText : lit == 0 ? fade(kCream, 0.6f) : kCream, label, !sel);
		}
	}
	//more above, more below
	float mx = (L.x(kGridLeft) + L.x(kGridRight)) / 2, a = 9 * u;
	if (g_gridScroll > 0)
	{
		float y = L.y(kGridTop - 36);
		dl->AddTriangleFilled(ImVec2(mx, y - a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
	}
	if (g_gridScroll + kGridVisible < rows)
	{
		float y = L.y(kGridTop + kGridPitch * kGridVisible + 2);
		dl->AddTriangleFilled(ImVec2(mx, y + a), ImVec2(mx - a, y), ImVec2(mx + a, y), kCream);
	}
}

//the grid's cell with a level under a point of the window, -1 for none
int GridAt(float x, float y)
{
	const Layout& L = g_layout;
	const Grid& grid = CurrentGrid();
	if (L.u <= 0 || grid.hash.empty()) return -1;
	float x0, cw;
	GridColumns(L, grid, x0, cw);
	float r = (y - L.y(kGridTop)) / (kGridPitch * L.u), c = (x - x0) / cw;
	int row = (int)std::floor(r), col = (int)std::floor(c);
	if (r < 0 || row >= kGridVisible || c < 0 || col >= grid.cols) return -1;
	int i = (row + g_gridScroll) * grid.cols + col;
	return i < (int)grid.hash.size() && grid.hash[i] ? i : -1;
}

//the one in the list under a point of the window, -1 for none
int DropAt(float x, float y)
{
	const Layout& L = g_layout;
	if (L.u <= 0 || x < L.x(kDropLeft) || x > L.x(kRowRight)) return -1;
	float r = (y - L.y(DropTop() + kDropPad)) / (kDropPitch * L.u);
	int i = (int)std::floor(r);
	return r < 0 || i >= DropShown() ? -1 : i + g_dropScroll;
}

//the row under a point of the window, -1 for none
int row_at(float x, float y) {
    const Layout& L = g_layout;
    if (L.u <= 0 || x < L.x(kRowLeft) || x > L.x(kRowRight)) return -1;
    float r = (y - L.y(rows_top())) / (kRowPitch * L.u);
    int i = (int)std::floor(r);
    if (r < 0 || i >= kVisible || r - (float)i > kRowHeight / kRowPitch) return -1;
    i += g_scroll;
    return i < (int)g_pages[g_page].rows.size() ? i : -1;
}

void drag(float x) {
    const Layout& L = g_layout;
    float f = (x - L.x(kSliderLeft)) / (L.x(kSliderRight) - L.x(kSliderLeft));
    g_pages[g_page].rows[g_dragging].slide(std::clamp(f, 0.0f, 1.0f));
}

void click(float x, float y) {
    int i = row_at(x, y);
    if (i < 0) return;
    g_row = i;
    Row& r = g_pages[g_page].rows[i];
    if (r.options || r.select || !r.adjust) { select(); return; }
    if (r.slide) { //on the track the knob jumps to the mouse and follows it
        if (x >= g_layout.x(kSliderLeft) - 12 * g_layout.u && x <= g_layout.x(kSliderRight) + 12 * g_layout.u) {
            g_dragging = i;
            drag(x);
        }
        return;
    }
    float vx1 = g_layout.x(kRowRight) - 12 * g_layout.u, vx0 = vx1 - kValueWidth * g_layout.u;
    if (x >= vx0 - 10 * g_layout.u) r.adjust(x < (vx0 + vx1) / 2 ? -1 : 1);
}

//a key pressed with the menu closed: its hotkeys go, and the picture says what they did
bool HotkeyDown(const SDL_KeyboardEvent& k)
{
	std::string said;
	if (k.repeat || overlay_open() || !PressHotkeys(k.scancode, said)) return false;
	if (g_dirty)
	{
		save_launch_settings();
		g_dirty = false;
	}
	g_toast = said;
	g_toastUntil = Clock::now() + std::chrono::milliseconds(1800);
	rt_log("goldeneye: hotkey %s: %s", SDL_GetScancodeName(k.scancode), said.c_str());
	return true;
}

//what the hotkeys set in a recording (GE_REPLAY), said and logged like a press. the launcher's
//file isn't the replay's to change
void ReplayHotkeys()
{
	std::string id;
	float value{};
	while (debug_setting_due(id, value))
	{
		auto s = std::find_if(g_settings.begin(), g_settings.end(), [&id](const Setting& s) { return s.id == id; });
		if (s == g_settings.end())
		{
			rt_log("goldeneye: replay: no setting \"%s\"", id.c_str());
			continue;
		}
		bool dirty = g_dirty, fpsChanged = g_fps_changed;
		s->set(value);
		g_dirty = dirty;
		g_fps_changed = fpsChanged;
		g_toast = s->label + ": " + s->show(s->get());
		g_toastUntil = Clock::now() + std::chrono::milliseconds(1800);
		rt_log("goldeneye: replay: %s", g_toast.c_str());
	}
}

void DrawToast(ImDrawList* dl, const Layout& L)
{
	float left = std::chrono::duration<float>(g_toastUntil - Clock::now()).count();
	if (left <= 0) return;
	float u = L.u, a = std::min(1.0f, left / 0.3f); //fades out at the end
	float size = fit(g_toast, 26 * u, L.w - 80 * u), w = text_width(g_toast, size), pad = 16 * u;
	ImVec2 at(L.cx - w / 2, L.y(34));
	dl->AddRectFilled(ImVec2(at.x - pad, at.y - 6 * u), ImVec2(at.x + w + pad, at.y + size + 8 * u), fade(kDim, a), 8 * u);
	text(dl, at, size, fade(kCream, a), g_toast, false);
}

const char* mouse_name(int button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return "Mouse Left";
        case SDL_BUTTON_RIGHT: return "Mouse Right";
        case SDL_BUTTON_MIDDLE: return "Mouse Middle";
        case SDL_BUTTON_X1: return "Mouse X1";
        case SDL_BUTTON_X2: return "Mouse X2";
    }
    return nullptr;
}

void key_down(const SDL_KeyboardEvent& k) {
    if (g_listening >= 0) {
        if (k.repeat) return;
        const char* name = SDL_GetScancodeName(k.scancode);
        if (k.scancode == SDL_SCANCODE_ESCAPE) g_listening = -1;
        else if (!name || !*name || SDL_GetScancodeFromName(name) != k.scancode) return;
        else if (g_page == kHotkeyEdit) {
            ChangeHotkey([name](Hotkey& h) { h.key = name; });
            g_listening = -1;
        } else bind(name);
        return;
    }
    if (g_drop >= 0) {
        switch (k.scancode) {
            case SDL_SCANCODE_UP: case SDL_SCANCODE_W: MoveDrop(-1, true); break;
            case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: MoveDrop(1, true); break;
            case SDL_SCANCODE_PAGEUP: MoveDrop(-kDropVisible, false); break;
            case SDL_SCANCODE_PAGEDOWN: MoveDrop(kDropVisible, false); break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
                if (!k.repeat) ChooseDrop();
                break;
            case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE:
                if (!k.repeat) back();
                break;
            default: break;
        }
        return;
    }
    if (OnGrid()) {
        switch (k.scancode) {
            case SDL_SCANCODE_UP: case SDL_SCANCODE_W: GridMove(0, -1); break;
            case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: GridMove(0, 1); break;
            case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: GridMove(-1, 0); break;
            case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: GridMove(1, 0); break;
            case SDL_SCANCODE_PAGEUP: for (int i = 0; i < kGridVisible; ++i) GridMove(0, -1); break;
            case SDL_SCANCODE_PAGEDOWN: for (int i = 0; i < kGridVisible; ++i) GridMove(0, 1); break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
                if (!k.repeat) GridLoad();
                break;
            case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE:
                if (!k.repeat) back();
                break;
            default: break;
        }
        return;
    }
    switch (k.scancode) {
        case SDL_SCANCODE_UP: case SDL_SCANCODE_W: move(-1); break;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: move(1); break;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: adjust(-1); break;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: adjust(1); break;
        case SDL_SCANCODE_PAGEUP: move(-std::min(kVisible, g_row)); break;
        case SDL_SCANCODE_PAGEDOWN: move(std::min(kVisible, (int)g_pages[g_page].rows.size() - 1 - g_row)); break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
            if (!k.repeat) select();
            break;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE:
            if (!k.repeat) back();
            break;
        case SDL_SCANCODE_DELETE:
            if (auto& r = g_pages[g_page].rows[g_row]; r.clear && !k.repeat) r.clear();
            break;
        default: break;
    }
}

void pad_down(int button) {
    if (g_listening >= 0) { //pad buttons aren't bound here, B just leaves
        if (button == SDL_GAMEPAD_BUTTON_EAST) g_listening = -1;
        return;
    }
    if (g_drop >= 0) {
        switch (button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: MoveDrop(-1, true); break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: MoveDrop(1, true); break;
            case SDL_GAMEPAD_BUTTON_SOUTH: ChooseDrop(); break;
            case SDL_GAMEPAD_BUTTON_EAST: back(); break;
            case SDL_GAMEPAD_BUTTON_START: open(false); break;
            default: break;
        }
        return;
    }
    if (OnGrid()) {
        switch (button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: GridMove(0, -1); break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: GridMove(0, 1); break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: GridMove(-1, 0); break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: GridMove(1, 0); break;
            case SDL_GAMEPAD_BUTTON_SOUTH: GridLoad(); break;
            case SDL_GAMEPAD_BUTTON_EAST: back(); break;
            case SDL_GAMEPAD_BUTTON_START: open(false); break;
            default: break;
        }
        return;
    }
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: move(-1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: move(1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: adjust(-1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: adjust(1); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: select(); break;
        case SDL_GAMEPAD_BUTTON_EAST: back(); break;
        case SDL_GAMEPAD_BUTTON_START: open(false); break;
        default: break;
    }
}
#endif

} //namespace

void save_settings()
{
#ifdef GE_IMGUI
	save_launch_settings();
#endif
}

std::string saved_setting(const char* env, const char* name)
{
	if (const char* e = std::getenv(env)) return e;
	const char* use = std::getenv("GE_SAVED_SETTINGS"); //0: only the environment's, for a run that has to be the same every time
	if (use && std::atoi(use) == 0) return {};
	std::ifstream file(GE_SETTINGS_FILE);
	const std::string key = std::string(name) + "=";
	for (std::string line; std::getline(file, line);)
	{
		if (line.compare(0, key.size(), key) != 0) continue;
		std::string value = line.substr(key.size());
		while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
		return value;
	}
	return {};
}

void settings_say(const std::string& text)
{
	g_toast = text;
	g_toastUntil = Clock::now() + std::chrono::milliseconds(1800);
}

bool settings_open() { return g_open; }

uint32_t settings_buttons() {
    return g_escape || Clock::now().time_since_epoch().count() < g_pause_until ? kClPlus : 0;
}

#ifdef GE_IMGUI
const std::vector<Setting>& Settings() { return g_settings; }
//anything on the Cheats page is on, or a code of the launcher's Cheats tab runs
bool cheats_active()
{
	if (gecko_cheats()) return true;
	for (const Setting& s : g_settings)
	{
		if ((s.page == kCheats || s.page == kForFun) && s.get() > 0.5f) return true;
		if (s.page == kBhop && std::string(s.id) == "bunnyhop" && s.get() > 0.5f) return true;
	}
	return false;
}
bool menu_event(const SDL_Event* e);

void settings_init() {
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    g_font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)kMenuFont, (int)sizeof kMenuFont, kFontSize, &cfg);
    build_pages();
    std::atexit([] { if (g_dirty) save_launch_settings(); }); //a change made with the menu still up
    LoadHotkeys();
    g_hotkeysChanged = true;
    rt_log("goldeneye: settings menu on F1");
    if (const char* e = std::getenv("GE_SETTINGS")) {
        open(true);
        g_page = std::clamp(std::atoi(e), 0, (int)g_pages.size() - 1);
        if (OnGrid()) OpenGrid();
    }
}

//true for the events the menu takes: while it's up that's every key except the window's own
//(fullscreen, the GX trace), the mouse and the pads' buttons
bool settings_event(const SDL_Event* e) {
    add_sprint_binding();
    AddWeaponBindings();
    AddJumpBinding();
    if (e->type == SDL_EVENT_KEY_UP && e->key.scancode == SDL_SCANCODE_ESCAPE) g_escape = false;
    if (!g_open && e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) {
        g_escape = true;
        return true;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F1) {
        if (!e->key.repeat) open(!g_open);
        return true;
    }
    if (!g_open && e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F9 && !e->key.repeat && zones_mark()) {
        g_toast = "Marked";
        g_toastUntil = Clock::now() + std::chrono::milliseconds(700);
        return true;
    }
    if (!g_open) return e->type == SDL_EVENT_KEY_DOWN && HotkeyDown(e->key);
    bool taken = menu_event(e);
    RefreshHotkeyPages();
    return taken;
}

bool menu_event(const SDL_Event* e) {
    switch (e->type) {
        case SDL_EVENT_KEY_DOWN:
            if (e->key.scancode == SDL_SCANCODE_F3 || e->key.scancode == SDL_SCANCODE_F11 || e->key.scancode == SDL_SCANCODE_F12 ||
                (e->key.mod & SDL_KMOD_ALT))
                return false;
            key_down(e->key);
            return true;
        case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT:
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            if (!(e->motion.state & SDL_BUTTON_LMASK)) g_dragging = -1; //let go where the window didn't see it
            if (OnGrid()) {
                if (int i = GridAt(e->motion.x, e->motion.y); i >= 0) g_gridAt = i;
                return true;
            }
            if (g_drop >= 0) {
                if (int i = DropAt(e->motion.x, e->motion.y); i >= 0) g_dropAt = i;
            } else if (g_dragging >= 0) drag(e->motion.x);
            else if (int i = row_at(e->motion.x, e->motion.y); i >= 0 && g_listening < 0) g_row = i;
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (OnGrid()) {
                if (e->button.button == SDL_BUTTON_RIGHT) back();
                else if (int i = GridAt(e->button.x, e->button.y); i >= 0 && e->button.button == SDL_BUTTON_LEFT) {
                    g_gridAt = i;
                    GridLoad();
                }
            } else if (g_drop >= 0) { //a click outside the list shuts it
                if (int i = DropAt(e->button.x, e->button.y); i >= 0 && e->button.button == SDL_BUTTON_LEFT) {
                    g_dropAt = i;
                    ChooseDrop();
                } else g_drop = -1;
            } else if (g_listening >= 0) { //a hotkey is a key, the mouse's buttons stay the game's
                if (const char* name = mouse_name(e->button.button); name && g_page != kHotkeyEdit) bind(name);
            } else if (e->button.button == SDL_BUTTON_LEFT) click(e->button.x, e->button.y);
            else if (e->button.button == SDL_BUTTON_RIGHT) back();
            return true;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            g_dragging = -1;
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            if (OnGrid()) {
                if (e->wheel.y != 0) GridMove(0, e->wheel.y > 0 ? -1 : 1);
            } else if (g_drop >= 0) {
                if (e->wheel.y != 0) MoveDrop(e->wheel.y > 0 ? -1 : 1, false);
            } else if (g_listening < 0 && e->wheel.y != 0) {
                int n = (int)g_pages[g_page].rows.size();
                g_row = std::clamp(g_row + (e->wheel.y > 0 ? -1 : 1), 0, n - 1);
            }
            return true;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            pad_down(e->gbutton.button);
            return true;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            return true;
        default:
            return false;
    }
}

//runs inside the overlay's ImGui frame
bool settings_toast() { return Clock::now() < g_toastUntil; }
void settings_replay() { ReplayHotkeys(); }
//with --no-video the overlay never draws, so the game's frame sets a recording's hotkeys
void settings_replay_headless() {
    static bool built = false;
    if (!built) build_pages();
    built = true;
    ReplayHotkeys();
}

void settings_draw() {
    if (!g_open && !settings_toast()) return;
    ImVec2 size = ImGui::GetIO().DisplaySize;
    Layout& L = g_layout;
    L.w = size.x;
    L.h = size.y;
    L.u = std::min(size.y / 720, size.x / 960);
    L.cx = size.x / 2;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->PushTextureID(g_font->ContainerAtlas->TexID);
    if (g_open) {
        if (g_keys.lines.empty()) load_keys(); //opened before the window had read the key file
        RefreshHotkeyPages();
        if (OnGrid()) { //the foot says which level the cursor is on
            const Grid& grid = CurrentGrid();
            bool cell = g_gridAt < (int)grid.about.size() && grid.hash[g_gridAt];
            int lit = cell && grid.lit ? grid.lit(g_gridAt) : -1;
            g_pages[g_page].rows[0].hint = g_page == kSkips ? (!cell ? "No missions are listed (config/openings.txt)."
                    : lit < 0 ? grid.about[g_gridAt] + "." : grid.about[g_gridAt] + (lit ? ": skipped." : ": played."))
                : !cell ? "The disc's levels couldn't be listed."
                : guest_ptr(ld32(kPlayerPtr)) ? grid.about[g_gridAt] + ". Levels load from the main menu: quit this one first." : "Load " + grid.about[g_gridAt] + ".";
        }
        const Page& page = g_pages[g_page];
        draw_frame(dl, L, page);
        if (OnGrid()) DrawGrid(dl, L);
        else draw_rows(dl, L, page);
        if (g_drop >= 0) DrawDrop(dl, L);
    } else DrawToast(dl, L);
    dl->PopTextureID();
}
#endif

} //namespace ge
