//the debug overlay (F3): memory watches from a Dolphin Memory Engine list (.dmw), read live
//
//drawn with Dear ImGui by the renderer's thread through wiikit's overlay hooks
//while it's up the mouse is free and doesn't turn the view
//no watch list means no overlay, F3 does nothing then
//the settings menu (F1, settings/) gets drawn in the same frame on top of it
//
//GE_DMW=F the watch list (the first .dmw in the project folder by default)
//GE_OVERLAY=1 start with the overlay up
//GE_STATE=1 start with the player state shown
//
//"Player state on screen" puts the local player's state (the controller's +0x704, named
//after the Ghidra project) in the top right corner, it stays there with the window closed
#include "ui/dmw.h"
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifdef GE_IMGUI
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#endif

#ifndef GE_ROOT
#define GE_ROOT "."
#endif

namespace ge {

#ifdef GE_IMGUI
void settings_init(); //settings/menu.cpp
bool settings_event(const SDL_Event* e);
void settings_draw();
bool settings_toast(); //a hotkey's line is up
void settings_say(const std::string& text); //a line at the top for a moment
void settings_replay(); //a recording's hotkeys, every frame
bool speedrun_event(const SDL_Event* e); //speedrun/window.cpp
void speedrun_draw();
void speedrun_own_window();
void menu_text(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const std::string& s, bool shadow); //settings/draw.cpp, the game's font
float menu_text_width(const std::string& s, float size);
bool menu_font_ready();
#endif

namespace {

std::atomic<bool> g_open{false};

#ifdef GE_IMGUI
WatchGroup g_watches;
std::string g_source, g_error;
char g_filter[64] = "";
bool g_have = false; //a watch list was read
bool g_show_state = false; //the player state in the top right corner

const char* state_name(uint32_t st) { //the Ghidra project's PlayerStates
    switch (st) {
        case 0x00: return "Standing";
        case 0x02: return "Moving";
        case 0x03: return "Sprinting";
        case 0x04: return "Landing";
        case 0x05: return "Death";
        case 0x08: return "ClimbVault";
        case 0x09: return "Melee";
        case 0x22: return "FPSCutscene";
        case 0x23: return "StartSlide";
        case 0x24: return "Sliding";
        case 0x25: return "EndSlide";
    }
    return "Unknown";
}

//the black a cutscene is skipped behind
void cutscene_black(float fade) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, (int)(fade * 255 + 0.5f)));
}

//while a cutscene is fast-forwarded, in the bottom right corner
void skipping_label() {
    const char* text = "Skipping cutscene...";
    ImFont* font = ImGui::GetFont();
    float size = font->FontSize * 1.5f, pad = 6;
    ImVec2 box = font->CalcTextSizeA(size, FLT_MAX, 0, text), screen = ImGui::GetIO().DisplaySize;
    ImVec2 at(screen.x - box.x - 24 - pad, screen.y - box.y - 24 - pad);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + box.x + pad, at.y + box.y + pad), IM_COL32(0, 0, 0, 150), 3);
    dl->AddText(font, size, at, IM_COL32(255, 255, 255, 255), text);
}

void player_state() {
    char text[64];
    uint32_t st;
    if (local_player_state(st)) std::snprintf(text, sizeof text, "%s (0x%02X), mouse: %s", state_name(st), st, local_look_name());
    else std::snprintf(text, sizeof text, "no player");
    ImFont* font = ImGui::GetFont();
    float size = font->FontSize * 1.5f, pad = 6;
    ImVec2 box = font->CalcTextSizeA(size, FLT_MAX, 0, text), screen = ImGui::GetIO().DisplaySize;
    ImVec2 at(screen.x - box.x - 12 - pad, 12 + pad);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + box.x + pad, at.y + box.y + pad), IM_COL32(0, 0, 0, 150), 3);
    dl->AddText(font, size, at, IM_COL32(255, 255, 255, 255), text);
}

//in the bottom left corner while any cheat is on, menus included
void cheats_label() {
    const char* text = "Cheats active";
    ImFont* font = ImGui::GetFont();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float u = std::min(screen.y / 720, screen.x / 960);
    float size = font->FontSize * 1.5f * std::max(u, 1.0f), pad = 5 * u;
    ImVec2 box = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    ImVec2 at(14 * u + pad, screen.y - box.y - 14 * u - pad);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + box.x + pad, at.y + box.y + pad), IM_COL32(0, 0, 0, 150), 3);
    dl->AddText(font, size, at, IM_COL32(255, 196, 64, 255), text);
}

//the trigger viewer (triggers.cpp): the trigger the player is in or looks at and what it does,
//top left (a line starting with a tab is a detail, white under the yellow name), and the names
//over the triggers near by
void trigger_list(const std::vector<std::string>& lines, const std::vector<TriggerTag>& tags) {
    ImFont* font = ImGui::GetFont();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float u = std::min(screen.y / 720, screen.x / 960);
    float size = font->FontSize * 1.15f * std::max(u, 1.0f), pad = 4 * u;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float picture[4];
    video_picture_rect(screen.x, screen.y, picture);
    for (const TriggerTag& t : tags) {
        ImVec2 box = font->CalcTextSizeA(size, FLT_MAX, 0, t.text.c_str());
        ImVec2 at(std::floor(picture[0] + (t.x * 0.5f + 0.5f) * picture[2] - box.x / 2), std::floor(picture[1] + (0.5f - t.y * 0.5f) * picture[3] - box.y / 2));
        dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad / 2), ImVec2(at.x + box.x + pad, at.y + box.y + pad / 2), IM_COL32(0, 0, 0, 140), 3);
        dl->AddText(font, size, at, IM_COL32(t.rgb & 0xFF, t.rgb >> 8 & 0xFF, t.rgb >> 16 & 0xFF, 255), t.text.c_str());
    }
    float line_h = font->CalcTextSizeA(size, FLT_MAX, 0, "A").y;
    float wide = 0;
    for (const std::string& line : lines) wide = std::max(wide, font->CalcTextSizeA(size, FLT_MAX, 0, line.c_str() + (line[0] == '\t')).x);
    ImVec2 at(14 * u + pad, 14 * u + pad);
    if (!lines.empty())
        dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + wide + pad, at.y + lines.size() * (line_h + pad) + pad), IM_COL32(0, 0, 0, 160), 3);
    for (const std::string& line : lines) {
        bool detail = !line.empty() && line[0] == '\t';
        dl->AddText(font, size, at, detail ? IM_COL32(235, 235, 235, 255) : IM_COL32(255, 220, 60, 255), line.c_str() + detail);
        at.y += line_h + pad;
    }
}

//the stamina meter (stamina.cpp): the sprint that's left, and the line a sprint is cancelled
//right under. green under the line, dimmer while it refills, red while there's too little
//left to sprint again
void stamina_bar(const Stamina& s) {
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float u = std::min(screen.y / 720, screen.x / 960);
    float w = 280 * u, h = 10 * u, edge = 2 * u;
    float x = std::floor((screen.x - w) / 2), y = std::floor(screen.y - 58 * u);
    float left = std::clamp(s.left / s.full, 0.0f, 1.0f), line = std::clamp(s.cancel / s.full, 0.0f, 1.0f);
    ImU32 fill = !s.sprinting && s.left < s.needs ? IM_COL32(235, 60, 50, 255) : s.left < s.cancel ? IM_COL32(80, 230, 110, 255) : s.sprinting ? IM_COL32(245, 240, 225, 255) : IM_COL32(245, 240, 225, 170);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(x - edge, y - edge), ImVec2(x + w + edge, y + h + edge), IM_COL32(0, 0, 0, 150), 2);
    if (left > 0) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w * left, y + h), fill);
    float at = std::floor(x + w * line), wide = std::max(2.0f, std::floor(2 * u));
    dl->AddRectFilled(ImVec2(at - 1, y - 5 * u - 1), ImVec2(at + wide + 1, y + h + 5 * u + 1), IM_COL32(0, 0, 0, 200));
    dl->AddRectFilled(ImVec2(at, y - 5 * u), ImVec2(at + wide, y + h + 5 * u), IM_COL32(255, 255, 255, 255));
}

//the speedometer (bunnyhop.cpp: bunnyhopping, or the cheat), under the crosshair: across the floor, a second.
//on rules that aren't the defaults (bhop_custom_now) it flashes grey with the level timer (tweaks.cpp)
void speedometer(float speed) {
    char text[16];
    std::snprintf(text, sizeof text, "%.1f", speed);
    ImFont* font = ImGui::GetFont();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float u = std::min(screen.y / 720, screen.x / 960);
    float size = font->FontSize * 2.2f * std::max(u, 1.0f);
    ImVec2 box = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    ImVec2 at(std::floor((screen.x - box.x) / 2), std::floor(screen.y / 2 + 30 * u));
    float hot = std::clamp((speed - 4.2f) / 12.0f, 0.0f, 1.0f); //white at a run, orange as it builds
    ImU32 colour = IM_COL32(255, (int)(255 - 80 * hot), (int)(255 - 200 * hot), 255);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (ms % 1000 < 500 && bhop_custom_now()) colour = IM_COL32(192, 192, 192, 255); //the timer's 0x60 of the hud's 0x80
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddText(font, size, ImVec2(at.x + 2 * u, at.y + 2 * u), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(font, size, at, colour, text);
}

//007 Custom (custom.cpp): what the mission is played with, under the hud's timer and right-aligned to
//it, or where the timer is when it's off. in the game's font, the hud's sizes are of its 640 by 448
const float kTimerHudHigh = 22.0f, kCustomHudGap = 4.0f, kCustomHudSize = 13.0f, kCustomHudLine = 14.0f;
void custom_lines() {
    if (!menu_font_ready()) return;
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float picture[4];
    video_picture_rect(screen.x, screen.y, picture);
    float u = picture[3] / 448, right = 0, top = 0;
    if (mission_timer_anchor(screen.x, screen.y, right, top)) top += (kTimerHudHigh + kCustomHudGap) * u;
    else mission_timer_place(screen.x, screen.y, right, top);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float size = kCustomHudSize * u;
    auto line = [&](const std::string& text, ImU32 colour) {
        menu_text(dl, ImVec2(std::floor(right - menu_text_width(text, size)), std::floor(top)), size, colour, text, true);
        top += kCustomHudLine * u;
    };
    line("007 Custom", IM_COL32(231, 181, 30, 240));
    for (int i = 0; i < kCustomShown; ++i) line(custom_shown_name(i, true) + " " + custom_shown_value(i), IM_COL32(255, 255, 255, 220));
}

std::string find_dmw() {
    if (const char* e = std::getenv("GE_DMW")) return e;
    std::error_code ec;
    for (auto& f : std::filesystem::directory_iterator(GE_ROOT, ec))
        if (f.path().extension() == ".dmw") return f.path().string();
    for (auto& f : std::filesystem::directory_iterator(std::string(GE_ROOT) + "/config", ec))
        if (f.path().extension() == ".dmw") return f.path().string();
    return "";
}

bool matches(const std::string& text) {
    if (!g_filter[0]) return true;
    auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    return lower(text).find(lower(g_filter)) != std::string::npos;
}

void row(const Watch& w) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(w.label.c_str());
    uint32_t addr;
    bool ok = watch_address(w, addr);
    ImGui::TableNextColumn();
    if (w.offsets.empty()) ImGui::Text("%08X", w.address);
    else if (ok) ImGui::Text("%08X -> %08X", w.address, addr);
    else ImGui::Text("%08X -> ?", w.address);
    ImGui::TableNextColumn();
    if (ok) ImGui::TextUnformatted(watch_value(w, addr).c_str());
    else ImGui::TextDisabled("-");
}

bool begin_table(const char* id) {
    if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        return false;
    ImGui::TableSetupColumn("watch", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("address", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    return true;
}

void group(const WatchGroup& g) { //the list as the file groups it
    if (!g.watches.empty() && begin_table("w")) {
        for (auto& w : g.watches) row(w);
        ImGui::EndTable();
    }
    for (size_t i = 0; i < g.groups.size(); ++i) {
        const WatchGroup& sub = g.groups[i];
        ImGui::PushID((int)i);
        if (ImGui::TreeNode("g", "%s (%zu)", sub.name.c_str(), sub.count())) {
            group(sub);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void filtered(const WatchGroup& g, const std::string& path) { //with a filter it's every match, flat
    for (auto& w : g.watches)
        if (matches(path + "/" + w.label)) row(w);
    for (auto& sub : g.groups) filtered(sub, path + "/" + sub.name);
}

void init(SDL_Window* win, void* gl) {
    window_icon(win);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; //no imgui.ini left in the folder
    ImGui::StyleColorsDark();
    ImGui::GetIO().Fonts->AddFontDefault(); //the window's font, the settings menu adds its own after it
    g_show_state = std::getenv("GE_STATE") != nullptr;
    ImGui_ImplSDL3_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 450");
    g_source = find_dmw();
    if (g_source.empty()) g_error = "no .dmw file (put one in the project's folder, or set GE_DMW)";
    else if (dmw_load(g_source, g_watches, g_error))
        rt_log("goldeneye: overlay (F3): %zu watches from %s", g_watches.count(), g_source.c_str());
    g_have = g_error.empty() && g_watches.count() > 0;
    if (!g_have) rt_log("goldeneye: no overlay (F3 does nothing): %s", g_error.empty() ? "the list is empty" : g_error.c_str());
    if (g_have && std::getenv("GE_OVERLAY")) { g_open = true; mouse_capture(false); }
    settings_init();
}

bool event(const SDL_Event* e) {
    //F12: a mod level's picture for the menus (pictures.cpp), in place of the runtime's GX trace
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F12) {
        if (!e->key.repeat) pictures_shoot();
        return true;
    }
    if (speedrun_event(e)) return true;
    if (settings_event(e)) return true;
    if (g_have && e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F3 && !e->key.repeat) {
        g_open = !g_open;
        mouse_capture(!overlay_open());
    }
    if (g_open) ImGui_ImplSDL3_ProcessEvent(e);
    return false;
}

void watches() {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(470, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    if (ImGui::Begin("GoldenEye 007: memory watches (F3)")) {
        uint32_t player = ld32(kPlayerPtr);
        ImGui::Text("%s", in_play() ? "in play" : guest_ptr(player) ? "paused" : "no player (menus)");
        ImGui::SameLine();
        ImGui::TextDisabled("  frame %u   %zu watches", ld32(kFrameCount), g_watches.count());
        ImGui::Checkbox("player state on screen", &g_show_state);
        if (!g_error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", g_error.c_str());
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##filter", "filter (name or group)", g_filter, sizeof g_filter);
        ImGui::Separator();
        ImGui::BeginChild("list");
        if (g_filter[0]) {
            if (begin_table("f")) {
                filtered(g_watches, "");
                ImGui::EndTable();
            }
        } else group(g_watches);
        ImGui::EndChild();
    }
    ImGui::End();
}

void draw() {
    speedrun_own_window();
    settings_replay();
    if (std::string said = pictures_draw(); !said.empty()) settings_say(said);
    float fade = cutscene_fade();
    bool cheats = cheats_active();
    Stamina sprint;
    bool meter = stamina(sprint);
    float speed = 0;
    bool hop = bunnyhop_speed(speed);
    bool flash = flash_showing();
    bool custom = custom_showing() && fade <= 0;
    if (!flash && !custom && !overlay_open() && !g_show_state && !skipping_cutscene() && fade <= 0 && !settings_toast() && !cheats && !meter && !hop && !speedrun_in_game()) return;
    //with only the state shown the cursor stays hidden over the picture
    ImGuiIO& io = ImGui::GetIO();
    if (overlay_open()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (flash) flash_draw();
    if (fade > 0) cutscene_black(fade);
    if (meter) stamina_bar(sprint);
    if (hop && fade <= 0) speedometer(speed);
    if (cheats) cheats_label();
    if (custom) custom_lines();
    if (trigger_view() && fade <= 0) trigger_list(trigger_labels(), trigger_tags());
    if (g_show_state) player_state();
    if (skipping_cutscene()) skipping_label();
    if (g_open) watches();
    settings_draw();
    speedrun_draw();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
#endif

} //namespace

bool overlay_open() { return g_open || settings_open(); }

void install_overlay() {
#ifdef GE_IMGUI
    VideoOverlay o;
    o.init = init;
    o.event = event;
    o.draw = draw;
    video_set_overlay(o);
#endif
}

} //namespace ge
