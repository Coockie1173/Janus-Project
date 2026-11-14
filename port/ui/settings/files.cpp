//the two files the menu writes: the launcher's settings (LAUNCH_NAME="value" lines) and the
//runtime's key file, only its [Classic Controller] lines

#include "ui/settings/menu.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ge::menu
{

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

//read by the launcher (launcher/launch.cpp) as the game starts, so a change here waits for the next start
NextStart& next_start() {
    static NextStart n;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        auto flag = [](const char* name, bool fallback) { std::string v = saved_setting("", name); return v.empty() ? fallback : v == "1"; };
        n.online = flag("LAUNCH_ONLINE", true);
        n.fast_start = flag("LAUNCH_FASTSTART", false);
        n.fullscreen = flag("LAUNCH_FULLSCREEN", false);
        n.crash_dumps = flag("LAUNCH_CRASHDUMP", true);
        n.gx_thread = flag("LAUNCH_GXTHREAD", true);
    }
    return n;
}

//the launcher's file, LAUNCH_NAME="value" lines. other lines are kept
void save_launch_settings() {
    char fov_text[16], mouse_text[16], ads_fov_text[16], ads_sens_text[16];
    std::snprintf(fov_text, sizeof fov_text, "%.0f", fov());
    std::snprintf(mouse_text, sizeof mouse_text, "%.2f", mouse_sens());
    std::snprintf(ads_fov_text, sizeof ads_fov_text, "%g", ads_fov());
    std::snprintf(ads_sens_text, sizeof ads_sens_text, "%.2f", ads_sens());
    char shake_text[16], tank_text[32];
    std::snprintf(shake_text, sizeof shake_text, "%g", shake_scale());
    char turbo_text[16], grain_text[16];
    std::snprintf(grain_text, sizeof grain_text, "%g", film_grain());
    char drawdist_text[16];
    std::snprintf(drawdist_text, sizeof drawdist_text, "%g", draw_distance());
    const char dof_text[2] = {(char)('0' + depth_of_field()), 0};
    std::snprintf(turbo_text, sizeof turbo_text, "%g", turbo());
    std::snprintf(tank_text, sizeof tank_text, "%d,%d,%d", tank_source(0), tank_source(1), tank_source(2));
    static const char* modes[] = {"auto", "direct", "stick"};
    std::vector<std::pair<std::string, std::string>> values = {
        {"LAUNCH_FOV", fov_text}, {"LAUNCH_MOUSE", mouse_text},
        {"LAUNCH_MOUSEMODE", modes[mouse_mode()]}, {"LAUNCH_MOUSEINVERT", mouse_invert() ? "1" : "0"},
        {"LAUNCH_ADSFOV", ads_fov_text}, {"LAUNCH_ADSSENS", ads_sens_text}, {"LAUNCH_AIMLOCK", aim_lock() ? "1" : "0"},
        {"LAUNCH_MOUSEEVERYWHERE", mouse_everywhere() ? "1" : "0"},
        {"LAUNCH_SKIPLIST", skip_list()}, {"LAUNCH_SKIPMOVIES", skip_movies() ? "1" : "0"}, {"LAUNCH_NOTUTORIALS", no_tutorials() ? "1" : "0"}, {"LAUNCH_NOCOLOURFILTER", no_colour_filter() ? "1" : "0"},
        {"LAUNCH_NOBLOOM", no_bloom() ? "1" : "0"}, {"LAUNCH_DOF", dof_text}, {"LAUNCH_INVERTFLASH", invert_flash() ? "1" : "0"}, {"LAUNCH_GRAIN", grain_text},
        {"LAUNCH_LEVELTIMER", level_timer() ? "1" : "0"}, {"LAUNCH_AUTOQTE", auto_qte() ? "1" : "0"}, {"LAUNCH_AUTOSTART", auto_start() ? "1" : "0"}, {"LAUNCH_AUTOSAVE", autosave() ? "1" : "0"}, {"LAUNCH_EXTRASMENU", extras_menu() ? "1" : "0"}, {"LAUNCH_UNLOCKALL", unlock_all() ? "1" : "0"}, {"LAUNCH_ONLINEUNLOCKS", online_unlocks() ? "1" : "0"}, {"LAUNCH_STAMINAMETER", stamina_meter() ? "1" : "0"},
        {"LAUNCH_VSYNC", vsync() ? "1" : "0"}, {"LAUNCH_PING", show_ping() ? "1" : "0"}, {"LAUNCH_OCCLUSION", occlusion() ? "1" : "0"}, {"LAUNCH_SOUNDHEIGHT", sound_height() ? "1" : "0"},
        {"LAUNCH_HUD", hud() ? "1" : "0"}, {"LAUNCH_SHAKE", shake_text}, {"LAUNCH_DRAWDIST", drawdist_text}, {"LAUNCH_TANKBUTTONS", tank_text},
        {"LAUNCH_ANISO", std::to_string(anisotropy())}, {"LAUNCH_MSAA", std::to_string(anti_aliasing())},
        {"LAUNCH_CUSTOM", custom_values()},
        {"LAUNCH_GOD", god_mode() ? "1" : "0"}, {"LAUNCH_INFINITEAMMO", infinite_ammo() ? "1" : "0"}, {"LAUNCH_INFINITESPRINT", infinite_sprint() ? "1" : "0"},
        {"LAUNCH_TURBO", turbo_text}, {"LAUNCH_GRENADES", grenades() ? "1" : "0"},
        {"LAUNCH_INVISIBLEENEMIES", invisible_enemies() ? "1" : "0"}, {"LAUNCH_SPEEDOMETER", speedometer_cheat() ? "1" : "0"},
        {"LAUNCH_WEAPONRANDOMIZER", weapon_randomizer() ? "1" : "0"},
        {"LAUNCH_NOATTACHMENTLIMIT", no_attachment_limit() ? "1" : "0"}, {"LAUNCH_BUNNYHOP", bunnyhop() ? "1" : "0"},
        {"LAUNCH_BHOP_PRESTRAFE", bhop_prestrafe() ? "1" : "0"},
#ifdef GE_HL_CROUCH
        {"LAUNCH_BHOP_CROUCH", bhop_crouch() ? "1" : "0"},
#endif
        {"LAUNCH_BHOP_HOVER", bhop_hover() ? "1" : "0"},
        {"LAUNCH_BHOP_LEDGE", bhop_ledge() ? "1" : "0"},
        {"LAUNCH_TRIGGERVIEW", trigger_view() ? "1" : "0"}, {"LAUNCH_TRIGGERXRAY", trigger_xray() ? "1" : "0"}, {"LAUNCH_TRIGGERLOADS", trigger_loads() ? "1" : "0"}, {"LAUNCH_TRIGGEROPACITY", std::to_string((int)trigger_opacity())}, {"LAUNCH_TRIGGERTYPES", trigger_types_saved()}};
    for (int i = 0; i < kBhopValueCount; ++i) {
        char text[24];
        std::snprintf(text, sizeof text, "%g", bhop_value(i).value);
        values.push_back({bhop_value(i).launch, text});
    }
    for (const AddedKey& k : g_addedKeys) values.push_back({k.launch, k.key});
    char forced_text[16];
    std::snprintf(forced_text, sizeof forced_text, "%08X", forced_weapon_id());
    values.push_back({"LAUNCH_FORCEDWEAPON", forced_text});
    const NextStart& next = next_start();
    values.push_back({"LAUNCH_ONLINE", next.online ? "1" : "0"});
    values.push_back({"LAUNCH_FASTSTART", next.fast_start ? "1" : "0"});
    values.push_back({"LAUNCH_FULLSCREEN", next.fullscreen ? "1" : "0"});
    values.push_back({"LAUNCH_CRASHDUMP", next.crash_dumps ? "1" : "0"});
    values.push_back({"LAUNCH_GXTHREAD", next.gx_thread ? "1" : "0"});
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

void load_keys() { g_keys.load(video_keys_path()); }
void save_keys() {
    if (video_keys_path().empty()) return;
    if (!write_lines(video_keys_path(), g_keys.lines)) rt_log("goldeneye: keys not saved: cannot write %s", video_keys_path().c_str());
    video_reload_keys();
}

KeyFile& Keys()
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

//a key file from before melee was on F: F goes from the smartphone to the melee attack, and the
//smartphone gets Q when nothing has it. once: the comment it leaves says it's done
void AddMeleeBinding()
{
	static bool done = false;
	if (done || video_keys_path().empty()) return;
	done = true;
	const char* const kDone = "# Melee Attack is on F, the smartphone on Q (moved once by the game)";
	if (g_keys.lines.empty() || std::find(g_keys.lines.begin(), g_keys.lines.end(), kDone) != g_keys.lines.end()) return;
	auto is = [](const char* key) { return [key](const std::string& k) { return !SDL_strcasecmp(k.c_str(), key); }; };
	auto phone = g_keys.keys("Down"), melee = g_keys.keys("L");
	if (std::erase_if(phone, is("F")) && std::none_of(melee.begin(), melee.end(), is("F")))
	{
		melee.insert(melee.begin(), "F");
		bool taken = false;
		for (auto& a : kActions)
		{
			auto keys = g_keys.keys(a.name);
			if (std::any_of(keys.begin(), keys.end(), is("Q"))) taken = true;
		}
		if (!taken) phone.push_back("Q");
		g_keys.set_keys("Down", phone);
		g_keys.set_keys("L", melee);
		rt_log("goldeneye: melee is on F now, the smartphone on %s (%s)", taken ? "its other keys" : "Q", video_keys_path().c_str());
	}
	g_keys.lines.push_back(kDone);
	save_keys();
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
#endif

} //namespace ge::menu

namespace ge
{

using namespace menu;

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

} //namespace ge
