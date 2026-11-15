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
//drawn by the overlay (overlay.cpp) on the renderer's thread, the rest is in this folder (menu.h)
//
//GE_SETTINGS=N start with the menu up on page N, handy for screenshots

#include "ui/settings/menu.h"
#include "debug/debug.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace ge::menu
{

#ifdef GE_IMGUI
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

//the Cheats row that opens the trigger page
int TriggerRow()
{
	const auto& cheats = g_pages[kCheats].rows;
	for (size_t i = 0; i < cheats.size(); ++i)
		if (cheats[i].label == "Trigger Viewer") return (int)i;
	return 0;
}

void back() {
    if (g_drop >= 0) g_drop = -1;
    else if (g_listening >= 0) g_listening = -1;
    else if (g_page == kRoot) open(false);
    else if (g_page == kHotkeyEdit) go(kHotkeys, std::max(0, g_edit));
    else if (g_page == kSkips) go(kGameplay, 0);
    else if (g_page >= kGameplay && g_page <= kStartup) go(kEnhancements, g_page - kGameplay);
    else if (g_page == kBhop) go(kForFun, BhopRow());
    else if (g_page == kTriggers) go(kCheats, TriggerRow());
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

//a key pressed with the menu closed: the added keys on it are pressed
bool AddedKeyDown(const SDL_KeyboardEvent& k)
{
	const char* name = SDL_GetScancodeName(k.scancode);
	if (k.repeat || overlay_open() || !name || !*name) return false;
	bool any = false;
	for (const AddedKey& a : g_addedKeys)
	{
		if (a.key.empty() || SDL_strcasecmp(a.key.c_str(), name) || !a.pressed) continue;
		a.pressed();
		any = true;
	}
	return any;
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
        if (k.scancode == SDL_SCANCODE_ESCAPE) g_listening = g_listenKey = -1;
        else if (!name || !*name || SDL_GetScancodeFromName(name) != k.scancode) return;
        else if (g_page == kHotkeyEdit) {
            ChangeHotkey([name](Hotkey& h) { h.key = name; });
            g_listening = -1;
        } else if (g_listenKey >= 0) {
            g_addedKeys[g_listenKey].key = name;
            g_listening = g_listenKey = -1;
            g_dirty = true;
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

} //namespace ge::menu

namespace ge
{

using namespace menu;

//a page of the menu's first list for another file's switches, before the menu is first shown
int settings_page(const char* title, const char* about) {
#ifdef GE_IMGUI
    g_addedPages.push_back({title, about});
    return kPageCount + (int)g_addedPages.size() - 1;
#else
    (void)title, (void)about;
    return -1;
#endif
}

namespace {
std::string LaunchKey(const char* id) {
    std::string launch = "LAUNCH_KEY_";
    for (const char* c = id; *c; ++c) launch += (char)std::toupper((unsigned char)*c);
    return launch;
}
}

void settings_key(int page, const char* id, const char* label, const char* hint, void (*pressed)(), const char* otherwise) {
#ifdef GE_IMGUI
    const std::string launch = LaunchKey(id);
    std::string key = saved_setting(launch.c_str(), launch.c_str());
    if (key.empty() && otherwise) key = otherwise;
    g_addedKeys.push_back({page, launch, label, hint, key, pressed});
#else
    (void)page, (void)id, (void)label, (void)hint, (void)pressed, (void)otherwise;
#endif
}

std::string settings_key_name(const char* id) {
#ifdef GE_IMGUI
    const std::string launch = LaunchKey(id);
    for (const AddedKey& k : g_addedKeys)
        if (k.launch == launch) return k.key;
#else
    (void)id;
#endif
    return {};
}

void settings_row(int page, const char* label, const char* hint, std::function<std::string()> value, std::function<void(int)> adjust, std::function<void()> select) {
#ifdef GE_IMGUI
    g_addedRows.push_back({page, label, hint, std::move(value), std::move(adjust), std::move(select)});
#else
    (void)page, (void)label, (void)hint, (void)value, (void)adjust, (void)select;
#endif
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
bool menu_event(const SDL_Event* e);

void settings_init() {
    g_font = menu_font_add(ImGui::GetIO().Fonts);
    if (g_settings.empty()) build_pages(); //once: settings_values may have built them already
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
    AddMeleeBinding();
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
    if (!g_open) return e->type == SDL_EVENT_KEY_DOWN && (AddedKeyDown(e->key) | HotkeyDown(e->key));
    bool taken = menu_event(e);
    RefreshHotkeyPages();
    RefreshTriggerPage();
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
                if (const char* name = mouse_name(e->button.button); name && g_page != kHotkeyEdit && g_listenKey < 0) bind(name);
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
//every setting of the menu as id=value lines (not the actions), for TASLib
//and to see changed. a value is the setting's number, as exact as a float goes
std::string settings_values() {
    if (g_settings.empty()) build_pages();
    std::string out;
    for (const Setting& s : g_settings) {
        if (s.kind == Setting::kAction || !s.get) continue;
        char v[32];
        std::snprintf(v, sizeof v, "%.9g", s.get());
        out += s.id + "=" + v + "\n";
    }
    //007 Custom's sliders (the game's own menu changes them, they start as the settings file has them)
    out += "custom=" + custom_values() + "\n";
    out += std::string("custom_always=") + (custom_always() ? "1" : "0") + "\n";
    //the Skip Cutscenes page's switches, a cell a line
    for (int m = 0; m < skip_missions(); ++m)
        for (int p = 0; p < kSkipParts; ++p)
            if (skip_exists(m, p)) out += "skip." + std::to_string(m) + "." + std::to_string(p) + "=" + (skip_on(m, p) ? "1" : "0") + "\n";
    return out;
}
//a setting of the Cheats page (and its Triggers page): a tool, not part of play. its off value, ""
//for one with none of its own (a trigger's opacity), null for any other setting
const char* settings_cheat_off(const std::string& id) {
    if (g_settings.empty()) build_pages();
    auto s = std::find_if(g_settings.begin(), g_settings.end(), [&id](const Setting& s) { return s.id == id; });
    if (s == g_settings.end() || (s->page != kCheats && s->page != kTriggers)) return nullptr;
    if (s->id == "turbo") return "1"; //the game's own speed
    return s->kind == Setting::kToggle ? "0" : "";
}
//one of them set by TASLib: not written to the launcher's file
bool settings_apply(const std::string& id, const std::string& text) {
    const float value = (float)std::atof(text.c_str());
    if (id == "custom") return set_custom_values(text, custom_always());
    if (id == "custom_always") return set_custom_values(custom_values(), value != 0);
    int m = 0, p = 0;
    if (std::sscanf(id.c_str(), "skip.%d.%d", &m, &p) == 2) {
        if (m < 0 || m >= skip_missions() || p < 0 || p >= kSkipParts || !skip_exists(m, p)) return false;
        set_skip_on(m, p, value != 0);
        return true;
    }
    if (g_settings.empty()) build_pages();
    auto s = std::find_if(g_settings.begin(), g_settings.end(), [&id](const Setting& s) { return s.id == id; });
    if (s == g_settings.end() || s->kind == Setting::kAction || !s->set) return false;
    if (s->get && s->get() == value) return true;
    bool dirty = g_dirty, fpsChanged = g_fps_changed;
    s->set(value);
    g_dirty = dirty;
    g_fps_changed = fpsChanged;
    return true;
}
//with --no-video the overlay never draws, so the game's frame sets a recording's hotkeys
void settings_replay_headless() {
    if (g_settings.empty()) build_pages();
    ReplayHotkeys();
}
#endif

} //namespace ge
