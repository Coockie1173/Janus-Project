// GoldenEye 007 — the settings menu (F1), drawn the way the game draws its own
// menus: the cream lines, the blue bars, the white bar under the cursor.
//
// Five pages: the screen (the field of view and the frame rate, sliders), the controls (the mouse, the
// gamepad), the key bindings, the enhancements and the cheats. What is changed counts at once. The field
// of view and the mouse are kept in the launcher's settings file, the rest in
// the runtime's key file ([Classic Controller]: only the lines changed here
// are rewritten), so the launcher's window and this menu show the same.
//
// While it is up the game gets no input, and a game in play is paused first
// (a press of +). Keys: the arrows or W A S D, Return, Esc or Backspace to go
// back, Delete to clear a binding; the mouse; a gamepad's D-pad, A and B.
//
// Drawn by the overlay (overlay.cpp), on the renderer's thread.
//
//     GE_SETTINGS=N   start with the menu up, on page N (0 the first: for pictures of it)
#include "ge.h"
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
std::atomic<Clock::rep> g_pause_until{0};            // + is held until then (settings_buttons)
const uint32_t kClPlus = 0x0400;

#ifdef GE_IMGUI
// ---- the two files -----------------------------------------------------------------------------
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

bool g_fps_changed = false;                          // the frame rate was changed in the menu

// The launcher's file: LAUNCH_NAME="value" lines, the others kept.
void save_launch_settings() {
    char fov_text[16], mouse_text[16];
    std::snprintf(fov_text, sizeof fov_text, "%.0f", fov());
    std::snprintf(mouse_text, sizeof mouse_text, "%.2f", mouse_sens());
    static const char* modes[] = {"auto", "direct", "stick"};
    std::vector<std::pair<std::string, std::string>> values = {
        {"LAUNCH_FOV", fov_text}, {"LAUNCH_MOUSE", mouse_text},
        {"LAUNCH_MOUSEMODE", modes[mouse_mode()]}, {"LAUNCH_MOUSEINVERT", mouse_invert() ? "1" : "0"},
        {"LAUNCH_MOUSEEVERYWHERE", mouse_everywhere() ? "1" : "0"},
        {"LAUNCH_SKIPCUTSCENES", skip_cutscenes() ? "1" : "0"},
        {"LAUNCH_LEVELTIMER", level_timer() ? "1" : "0"}, {"LAUNCH_EXTRASMENU", extras_menu() ? "1" : "0"}};
    if (g_fps_changed) values.push_back({"LAUNCH_FPS", std::to_string(fps_limit())});   // "auto" kept until it is changed
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

// The key file's [Classic Controller] section, by name.
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
        if (i >= 0) {                                // the file's own spacing kept
            lines[i] = trim(lines[i].substr(0, lines[i].find('=') + 1) + " " + value);
            return;
        }
        int end = -1;                                // a new line: at the section's end
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

// ---- the pages ---------------------------------------------------------------------------------
// what the game does with each Classic Controller button (its default layout), and the key file's name for it
struct Action { const char* label; const char* name; };
const Action kActions[] = {
    {"Fire", "ZR"}, {"Aim", "ZL"}, {"Melee Attack", "L"}, {"Throw Grenade (doubles as R button)", "R"}, {"Reload", "Y"},
    {"Crouch", "A"}, {"Action / Sprint", "B"}, {"Cycle Grenades", "X"}, {"Next Weapon", "Right"},
    {"Previous Weapon", "Left"}, {"Firing Mode", "Up"}, {"Smartphone", "Down"}, {"Night Vision / Scores", "Minus"},
    {"Pause", "Plus"}, {"Move Forward", "Left Stick Up"}, {"Move Back", "Left Stick Down"},
    {"Move Left", "Left Stick Left"}, {"Move Right", "Left Stick Right"}};

struct Row {
    std::string label, hint;
    std::function<std::string()> value;              // none: a row that is only selected
    std::function<void(int)> adjust;                 // left and right: -1, +1
    std::function<void()> select;
    std::function<void()> clear;                     // Delete
    bool binding = false;                            // the value is a list of keys, not something to step through
    std::function<float()> slider;                   // a slider's row: where its knob is, 0..1
    std::function<void(float)> slide;                // and the knob dragged there
};
struct Page { std::string title; std::vector<Row> rows; };

enum { kRoot, kScreen, kControls, kBindings, kEnhancements, kCheats, kPageCount };
std::vector<Page> g_pages;
int g_page = kRoot, g_row = 0, g_scroll = 0;
int g_root_row = 0;                                  // where the cursor was on the first page
int g_listening = -1;                                // the binding row waiting for a key
int g_dragging = -1;                                 // the slider row the mouse has by the knob
bool g_dirty = false;                                // the launcher's file is to be written

template <size_t N> int index_of(const char* const (&names)[N], const std::string& v, int otherwise) {
    for (size_t i = 0; i < N; ++i)
        if (!SDL_strcasecmp(names[i], v.c_str())) return (int)i;
    return otherwise;
}
// The frame rates the slider stops at (the launcher's are the same): the
// console's 30, the usual screens' rates and their halves, up to 275.
const uint32_t kFpsStops[] = {30, 40, 50, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240, 275};
const int kFpsStopCount = (int)std::size(kFpsStops);
int fps_stop() {                                     // the stop nearest to the rate as it is
    int best = 0;
    for (int i = 1; i < kFpsStopCount; ++i)
        if (std::abs((int)kFpsStops[i] - (int)fps_limit()) < std::abs((int)kFpsStops[best] - (int)fps_limit())) best = i;
    return best;
}
void set_fps_stop(int i) {
    set_fps_limit(kFpsStops[std::clamp(i, 0, kFpsStopCount - 1)]);
    g_fps_changed = g_dirty = true;
}
const char* const kInputNames[] = {"Auto", "Pad", "Keyboard"};
const char* const kFaceNames[] = {"Position", "Label"};
const float kDeadZoneMax = 0.9f;
float dead_zone() { return std::clamp((float)std::atof(g_keys.get("Dead Zone", "0.15").c_str()), 0.0f, kDeadZoneMax); }
void set_dead_zone(float z) {                        // in hundredths; the file written only when it changes
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

void go(int page, int row = 0) {
    if (g_page == kRoot) g_root_row = g_row;
    g_page = page;
    g_row = row;
    g_scroll = 0;
    g_listening = g_dragging = -1;
}

void build_pages() {
    g_pages.resize(kPageCount);
    Page& root = g_pages[kRoot];
    root.title = "Settings";
    root.rows.push_back({"Screen Settings", "Adjust the field of view.", {}, {}, [] { go(kScreen); }});
    root.rows.push_back({"Control Settings", "Adjust the mouse and the gamepad.", {}, {}, [] { go(kControls); }});
    root.rows.push_back({"Key Bindings", "Choose the keys and mouse buttons for each action.", {}, {}, [] { go(kBindings); }});
    root.rows.push_back({"Enhancements", "Things the port adds to the game.", {}, {}, [] { go(kEnhancements); }});
    root.rows.push_back({"Cheats", "Things the game itself would not let you do.", {}, {}, [] { go(kCheats); }});

    Page& screen = g_pages[kScreen];
    screen.title = "Screen Settings";
    screen.rows.push_back({"Field of View", "How wide the view is in play, in degrees. The game's own is 49.",
        [] { return std::to_string((int)fov()); },
        [](int d) { set_fov(std::clamp(fov() + (float)d, kFovMin, kFovMax)); g_dirty = true; }, {}, {}, false,
        [] { return (fov() - kFovMin) / (kFovMax - kFovMin); },
        [](float f) { set_fov(std::round(kFovMin + f * (kFovMax - kFovMin))); g_dirty = true; }});
    screen.rows.push_back({"Max Frame Rate", "The most frames a second the game runs at. 30 is the console's; heavy levels may not hold the highest.",
        [] { return std::to_string(fps_limit()); },
        [](int d) { set_fps_stop(fps_stop() + d); }, {}, {}, false,
        [] { return (float)fps_stop() / (float)(kFpsStopCount - 1); },
        [](float f) { set_fps_stop((int)std::lround(f * (float)(kFpsStopCount - 1))); }});
    screen.rows.push_back({"Restore Defaults", "Back to the game's own field of view, and 60 frames a second.", {}, {}, [] {
        set_fov(kGameFov);
        set_fps_limit(60);
        g_fps_changed = g_dirty = true;
    }});

    Page& controls = g_pages[kControls];
    controls.title = "Control Settings";
    controls.rows.push_back({"Mouse Sensitivity", "How far the view turns for the mouse's motion. Off leaves the mouse free.",
        [] {
            char b[16];
            std::snprintf(b, sizeof b, "%.2f", mouse_sens());
            return mouse_sens() > 0 ? std::string(b) : std::string("Off");
        },
        [](int d) { set_mouse_sens(std::round((mouse_sens() + 0.05f * (float)d) * 20) / 20); g_dirty = true; }, {}, {}, false,
        [] { return mouse_sens() / kMouseSensMax; },
        [](float f) { set_mouse_sens(std::round(f * kMouseSensMax * 20) / 20); g_dirty = true; }});
    controls.rows.push_back({"Invert Mouse", "On: pushing the mouse forward looks down.",
        [] { return std::string(mouse_invert() ? "On" : "Off"); },
        [](int) { set_mouse_invert(!mouse_invert()); g_dirty = true; }});
    controls.rows.push_back({"Mouse Look", "Auto turns the view itself, and uses the right stick in a network game someone else hosts.",
        [] { static const char* n[] = {"Auto", "Direct", "Stick"}; return std::string(n[mouse_mode()]); },
        [](int d) { set_mouse_mode((mouse_mode() + d + 3) % 3); g_dirty = true; }});
    controls.rows.push_back({"Player 1 Input", "What plays as the first controller: keyboard, mouse and gamepad together, or only one.",
        [] {
            static const char* n[] = {"Keyboard + Gamepad", "Gamepad", "Keyboard"};
            return std::string(n[index_of(kInputNames, g_keys.get("Input"), 0)]);
        },
        [](int d) {
            int v = (index_of(kInputNames, g_keys.get("Input"), 0) + d + 3) % 3;
            g_keys.set("Input", kInputNames[v]);
            save_keys();
            video_set_input(v);
        }});
    controls.rows.push_back({"Gamepad Face Buttons", "Position: laid out as on the Classic Controller. Label: the button marked A is A.",
        [] { return std::string(kFaceNames[index_of(kFaceNames, g_keys.get("Face Buttons"), 0)]); },
        [](int) {
            g_keys.set("Face Buttons", kFaceNames[1 - index_of(kFaceNames, g_keys.get("Face Buttons"), 0)]);
            save_keys();
        }});
    controls.rows.push_back({"Stick Dead Zone", "How far a gamepad's sticks move before they count.",
        [] { return std::to_string((int)std::lround(dead_zone() * 100)) + "%"; },
        [](int d) { set_dead_zone(dead_zone() + 0.01f * (float)d); }, {}, {}, false,
        [] { return dead_zone() / kDeadZoneMax; },
        [](float f) { set_dead_zone(f * kDeadZoneMax); }});
    controls.rows.push_back({"Restore Defaults", "Back to the mouse and gamepad settings the port starts with.", {}, {}, [] {
        set_mouse_sens(1.0f);
        set_mouse_invert(false);
        set_mouse_mode(0);
        g_keys.set("Input", "Auto");
        g_keys.set("Face Buttons", "Position");
        g_keys.set("Dead Zone", "0.15");
        save_keys();
        video_set_input(0);
        g_dirty = true;
    }});

    Page& bindings = g_pages[kBindings];
    bindings.title = "Key Bindings";
    for (size_t i = 0; i < std::size(kActions); ++i) {
        const Action& a = kActions[i];
        Row r;
        r.label = a.label;
        r.hint = "Select to add a key or a mouse button. Delete clears the action.";
        r.binding = true;
        r.value = [name = std::string(a.name)] {
            std::string v;
            for (auto& k : g_keys.keys(name)) v += (v.empty() ? "" : ", ") + k;
            return v.empty() ? std::string("-") : v;
        };
        r.select = [i] { g_listening = (int)i; };
        r.clear = [name = std::string(a.name)] { g_keys.set_keys(name, {}); save_keys(); };
        bindings.rows.push_back(r);
    }
    bindings.rows.push_back({"Restore Defaults", "Back to the port's own keys for every action.", {}, {}, restore_bindings});

    Page& enhancements = g_pages[kEnhancements];
    enhancements.title = "Enhancements";
    enhancements.rows.push_back({"Skip Intro Cutscenes (Experimental)", "On: a mission starts at the checkpoint after its opening cutscene.",
        [] { return std::string(skip_cutscenes() ? "On" : "Off"); },
        [](int) { set_skip_cutscenes(!skip_cutscenes()); g_dirty = true; }});
    enhancements.rows.push_back({"Level Timer", "On: the hud shows the time played in the level.",
        [] { return std::string(level_timer() ? "On" : "Off"); },
        [](int) { set_level_timer(!level_timer()); g_dirty = true; }});

    Page& cheats = g_pages[kCheats];
    cheats.title = "Cheats";
    cheats.rows.push_back({"Always Enable Mouse", "On: the mouse turns the view where the game locks it: cutscenes, melee, climbing, death.",
        [] { return std::string(mouse_everywhere() ? "On" : "Off"); },
        [](int) { set_mouse_everywhere(!mouse_everywhere()); g_dirty = true; }});
    cheats.rows.push_back({"Unused Extras Menu", "On: Extras also lists Mission Select, Character Viewer, Movies and the Calibration Menu.",
        [] { return std::string(extras_menu() ? "On" : "Off"); },
        [](int) { set_extras_menu(!extras_menu()); g_dirty = true; }});
}

// The key or mouse button pressed for the action that is listening: on that
// action, and off every other one.
void bind(const std::string& key) {
    const char* name = kActions[g_listening].name;
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

// ---- opening, closing, moving ------------------------------------------------------------------
void open(bool on) {
    if (on == g_open) return;
    g_open = on;
    video_block_input(on);
    if (on) {
        load_keys();
        g_page = kRoot;
        g_row = g_scroll = 0;
        g_listening = g_dragging = -1;
        if (in_play())                               // the game's own pause, before the menu takes the input
            g_pause_until = (Clock::now() + std::chrono::milliseconds(150)).time_since_epoch().count();
    } else if (g_dirty) {
        save_launch_settings();
        g_dirty = false;
    }
    mouse_capture(!overlay_open());
}

void back() {
    if (g_listening >= 0) g_listening = -1;
    else if (g_page == kRoot) open(false);
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

void select() {
    Row& r = g_pages[g_page].rows[g_row];
    if (r.select) r.select();
    else if (r.adjust) r.adjust(1);
}

// ---- the picture -------------------------------------------------------------------------------
ImFont* g_font = nullptr;
const float kFontSize = 48, kNarrow = 0.80f;         // the game's face is a narrow one: the glyphs are drawn squeezed
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

// as large as size, smaller if that is what fits in width
float fit(const std::string& s, float size, float width) {
    float w = text_width(s, size);
    return w > width ? size * width / w : size;
}

ImU32 fade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a) << IM_COL32_A_SHIFT); }

struct Layout {                                      // the game's 1280 x 720 menu screen, centred in the window
    float u, cx, w, h;
    float x(float ref) const { return cx + (ref - 640) * u; }
    float y(float ref) const { return ref * u; }
};
const float kRowLeft = 340, kRowRight = 940, kRowHeight = 38, kRowPitch = 41, kValueWidth = 230;
const float kSliderLeft = kRowRight - 262, kSliderRight = kRowRight - 66;   // a slider's track, its number after it
const int kVisible = 8;
Layout g_layout;
float rows_top() { return g_pages[g_page].rows.size() > 4 ? 178.0f : 276.0f; }

// a key's cap, and what it does: the game's "(b) Back"
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

    // the title: its node, the bar that runs off to the right
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
    text(dl, ImVec2(L.x(323), L.y(78)), 40 * u, kCream, page.title);

    // the foot: what the row under the cursor is for, and the keys
    for (float y : {545.0f, 668.0f}) {
        dl->AddRectFilledMultiColor(ImVec2(L.x(200), L.y(y)), ImVec2(L.x(300), L.y(y + 3)), none, kCream, kCream, none);
        dl->AddRectFilled(ImVec2(L.x(300), L.y(y)), ImVec2(L.x(980), L.y(y + 3)), kCream);
        dl->AddRectFilledMultiColor(ImVec2(L.x(980), L.y(y)), ImVec2(L.x(1080), L.y(y + 3)), kCream, none, none, kCream);
    }
    const Row& row = page.rows[g_row];
    std::string hint = g_listening >= 0 ? "Press a key or a mouse button. Esc leaves it as it is." : row.hint;
    float hs = fit(hint, 24 * u, 840 * u);
    text(dl, ImVec2(L.cx - text_width(hint, hs) / 2, L.y(592)), hs, kCream, hint);
    float left = L.x(210);
    left += key_hint(dl, L, left, L.y(625), "Esc", g_page == kRoot ? "Close" : "Back", false) + 28 * u;
    if (row.binding && g_listening < 0) key_hint(dl, L, left, L.y(625), "Del", "Clear", false);
    const char* does = row.binding ? "Add Key" : row.select || !row.adjust ? "Select" : "Change";
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
        text(dl, ImVec2(x0 + 14 * u, ty), size, colour, r.label, !sel);
        if (!r.value) continue;
        std::string v = r.value();
        if (r.binding) {                             // the keys, against the right edge
            if (g_listening == i) v = std::fmod(ImGui::GetTime(), 1.0) < 0.6 ? "Press a key..." : "";
            float room = x1 - x0 - 44 * u - text_width(r.label, size), vs = fit(v, 26 * u, room);
            text(dl, ImVec2(x1 - 14 * u - text_width(v, vs), ty + (size - vs) * 0.55f), vs, colour, v, !sel);
            continue;
        }
        float mid = (y0 + y1) / 2;
        if (r.slider) {                              // the track, filled up to the knob, and the number
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
    // more above, more below
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

// the row under a point of the window, -1 for none
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
    if (r.select || !r.adjust) { select(); return; }
    if (r.slide) {                                   // on the track: the knob comes to the mouse, and follows it
        if (x >= g_layout.x(kSliderLeft) - 12 * g_layout.u && x <= g_layout.x(kSliderRight) + 12 * g_layout.u) {
            g_dragging = i;
            drag(x);
        }
        return;
    }
    float vx1 = g_layout.x(kRowRight) - 12 * g_layout.u, vx0 = vx1 - kValueWidth * g_layout.u;
    if (x >= vx0 - 10 * g_layout.u) r.adjust(x < (vx0 + vx1) / 2 ? -1 : 1);
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
        else if (name && *name && SDL_GetScancodeFromName(name) == k.scancode) bind(name);
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
    if (g_listening >= 0) {                          // a pad's buttons are not bound here: B leaves it
        if (button == SDL_GAMEPAD_BUTTON_EAST) g_listening = -1;
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

}  // namespace

bool settings_open() { return g_open; }

uint32_t settings_buttons() {
    return Clock::now().time_since_epoch().count() < g_pause_until ? kClPlus : 0;
}

#ifdef GE_IMGUI
void settings_init() {
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    g_font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)kMenuFont, (int)sizeof kMenuFont, kFontSize, &cfg);
    build_pages();
    rt_log("goldeneye: settings menu on F1");
    if (const char* e = std::getenv("GE_SETTINGS")) {
        open(true);
        g_page = std::clamp(std::atoi(e), 0, (int)g_pages.size() - 1);
    }
}

// True for the events the menu takes: while it is up, every key but the
// window's own (fullscreen, the GX trace), the mouse and the pads' buttons.
bool settings_event(const SDL_Event* e) {
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F1) {
        if (!e->key.repeat) open(!g_open);
        return true;
    }
    if (!g_open) return false;
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
            if (!(e->motion.state & SDL_BUTTON_LMASK)) g_dragging = -1;   // let go where the window did not see it
            if (g_dragging >= 0) drag(e->motion.x);
            else if (int i = row_at(e->motion.x, e->motion.y); i >= 0 && g_listening < 0) g_row = i;
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (g_listening >= 0) {
                if (const char* name = mouse_name(e->button.button)) bind(name);
            } else if (e->button.button == SDL_BUTTON_LEFT) click(e->button.x, e->button.y);
            else if (e->button.button == SDL_BUTTON_RIGHT) back();
            return true;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            g_dragging = -1;
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            if (g_listening < 0 && e->wheel.y != 0) {
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

// Inside the overlay's ImGui frame.
void settings_draw() {
    if (!g_open) return;
    if (g_keys.lines.empty()) load_keys();           // opened before the window had read the key file
    ImVec2 size = ImGui::GetIO().DisplaySize;
    Layout& L = g_layout;
    L.w = size.x;
    L.h = size.y;
    L.u = std::min(size.y / 720, size.x / 960);
    L.cx = size.x / 2;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const Page& page = g_pages[g_page];
    dl->PushTextureID(g_font->ContainerAtlas->TexID);
    draw_frame(dl, L, page);
    draw_rows(dl, L, page);
    dl->PopTextureID();
}
#endif

}  // namespace ge
