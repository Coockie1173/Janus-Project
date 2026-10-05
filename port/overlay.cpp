//the debug overlay (F3): memory watches from a Dolphin Memory Engine list (.dmw), read live
//
//drawn with Dear ImGui by the renderer's thread through wiikit's overlay hooks
//while it's up the mouse is free and doesn't turn the view
//no watch list means no overlay, F3 does nothing then
//the settings menu (F1, settings.cpp) gets drawn in the same frame on top of it
//
//GE_DMW=F the watch list (the first .dmw in the project folder by default)
//GE_OVERLAY=1 start with the overlay up
//GE_STATE=1 start with the player state shown
//
//"Player state on screen" puts the local player's state (the controller's +0x704, named
//after the Ghidra project) in the top right corner, it stays there with the window closed
#include "dmw.h"
#include "ge.h"
#include <atomic>
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
void settings_init(); //settings.cpp
bool settings_event(const SDL_Event* e);
void settings_draw();
bool settings_toast(); //a hotkey's line is up
void settings_replay(); //a recording's hotkeys, every frame
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
    settings_replay();
    if (!overlay_open() && !g_show_state && !skipping_cutscene() && !settings_toast()) return;
    //with only the state shown the cursor stays hidden over the picture
    ImGuiIO& io = ImGui::GetIO();
    if (overlay_open()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (g_show_state) player_state();
    if (skipping_cutscene()) skipping_label();
    if (g_open) watches();
    settings_draw();
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
