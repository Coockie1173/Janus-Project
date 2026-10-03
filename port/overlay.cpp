// GoldenEye 007 — the debug overlay (F3): the memory watches of a Dolphin
// Memory Engine list (.dmw), read live from the running game.
//
// Drawn with Dear ImGui over the picture, by the renderer's thread (wiikit's
// overlay hooks: after the context is made, on each event, before each
// swap). While it is up the mouse is free and does not turn the view.
// Without a watch list there is no overlay: F3 does nothing.
// The settings menu (F1, settings.cpp) is drawn in the same frame, over it.
//
//     GE_DMW=F       the watch list (default: the first .dmw in the project's folder)
//     GE_OVERLAY=1   start with the overlay up
#include "dmw.h"
#include "ge.h"
#include <atomic>
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
void settings_init();                                // settings.cpp
bool settings_event(const SDL_Event* e);
void settings_draw();
#endif

namespace {

std::atomic<bool> g_open{false};

#ifdef GE_IMGUI
WatchGroup g_watches;
std::string g_source, g_error;
char g_filter[64] = "";
bool g_have = false;                                 // a watch list was read: without one there is no overlay

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

void group(const WatchGroup& g) {                   // the list as the file groups it
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

void filtered(const WatchGroup& g, const std::string& path) {   // with a filter: every match, flat
    for (auto& w : g.watches)
        if (matches(path + "/" + w.label)) row(w);
    for (auto& sub : g.groups) filtered(sub, path + "/" + sub.name);
}

void init(SDL_Window* win, void* gl) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;            // no imgui.ini left in the folder
    ImGui::StyleColorsDark();
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
    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    if (ImGui::Begin("GoldenEye 007: memory watches (F3)")) {
        uint32_t player = ld32(kPlayerPtr);
        ImGui::Text("%s", in_play() ? "in play" : guest_ptr(player) ? "paused" : "no player (menus)");
        ImGui::SameLine();
        ImGui::TextDisabled("  frame %u   %zu watches", ld32(kFrameCount), g_watches.count());
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
    if (!overlay_open()) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (g_open) watches();
    settings_draw();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
#endif

}  // namespace

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

}  // namespace ge
