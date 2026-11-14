//every setting the pages change, a hotkey can change it too (hotkeys.h)

#include "ui/settings/menu.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ge::menu
{

#ifdef GE_IMGUI
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
		g_listenKey = -1;
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

//only does something: Enter on its row, or a hotkey
void AddAction(int page, const char* id, const char* label, const char* hint, std::function<void()> does)
{
	Setting& s = AddSetting(page, Setting::kAction, id, label, hint);
	s.get = [] { return 0.0f; };
	s.set = [does](float) { does(); };
	s.step = [](float v, int) { return v; };
	s.show = [](float) { return std::string(); };
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
	AddToggle(kScreen, "start_fullscreen", "Start Fullscreen", "On: the game starts in fullscreen, from the next start on. F11 or Alt+Enter switches while you play.",
		[] { return next_start().fullscreen; }, [](bool v) { next_start().fullscreen = v; });
	AddToggle(kScreen, "vsync", "VSync", "On: frames wait for the screen's refresh, so the picture doesn't tear. The game runs at the screen's rate at most.", vsync, set_vsync);
	AddToggle(kScreen, "gx_thread", "Graphics Thread", "On: the game's drawing commands are read on a thread of their own while the game goes on, as the console's graphics chip does: a higher frame rate. Off: on the game's thread. From the next start on.",
		[] { return next_start().gx_thread; }, [](bool v) { next_start().gx_thread = v; });
	AddChoice(kScreen, "anisotropy", "Anisotropic Filtering", "Keeps textures sharp on floors and walls seen at a slant. Off is the console's picture.",
		{"Off", "2x", "4x", "8x", "16x"}, [] { return NearestOf(kAnisotropies, anisotropy()); },
		[](int v) { set_anisotropy(kAnisotropies[v]); g_dirty = true; });
	AddChoice(kScreen, "anti_aliasing", "Anti-Aliasing", "Smooths the edges of everything drawn, with that many samples a pixel. Costs the graphics card more the higher it is.",
		{"Off", "2x MSAA", "4x MSAA", "8x MSAA"}, [] { return NearestOf(kSampleCounts, anti_aliasing()); },
		[](int v) { set_anti_aliasing(kSampleCounts[v]); g_dirty = true; });
	Setting& shake = AddRange(kScreen, "screen_shake", "Screen Shake", "How much the view shakes: from explosions, on Nightclub's dance floor and the like. 1.0 is the game's own.",
		shake_scale, [](float v) { set_shake_scale(v); g_dirty = true; }, 0, kShakeMax, 0.1f, 1);
	shake.show = [show = shake.show](float v) { return v > 0 ? show(v) + "x" : std::string("Off"); };
	Setting& drawDist = AddRange(kScreen, "draw_distance", "Draw Distance", "How far away details (Outpost's rocks) are drawn before they pop in. 1.0x is the game's own.",
		draw_distance, [](float v) { set_draw_distance(v); g_dirty = true; }, 1, kDrawDistanceMax, 0.5f, 1);
	drawDist.show = [show = drawDist.show](float v) { return v >= kDrawDistanceMax ? std::string("Unlimited") : show(v) + "x"; };
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

	AddToggle(kGameplay, "skip_movies", "Skip Movies", "On: briefings, the movies between missions and the logos are skipped.", skip_movies, set_skip_movies);
	AddToggle(kGameplay, "level_timer", "Level Timer", "On: the hud shows the time played in the level.", level_timer, set_level_timer);
	AddToggle(kGameplay, "auto_qte", "Auto Quick Time Events", "On: quick time events press their own buttons.", auto_qte, set_auto_qte);
	AddToggle(kGameplay, "auto_start", "Auto Start Mission", "On: a loaded mission starts by itself, without Continue being pressed.", auto_start, set_auto_start);
	AddToggle(kGameplay, "autosave", "Save At Checkpoints", "On: reaching a checkpoint saves the game, as Save and Quit does. After a crash Continue Game starts there.", autosave, set_autosave);
	AddToggle(kGameplay, "no_tutorials", "Disable Tutorials", "On: the pop-up boxes that explain the controls are never shown, in any mission, training included.", no_tutorials, set_no_tutorials);
	AddToggle(kVisuals, "no_colour_filter", "Disable Colour Filter", "On: the tint most levels lay over the picture (Dam's grey-blue, Facility's blue-green...) isn't drawn.", no_colour_filter, set_no_colour_filter);
	AddToggle(kVisuals, "no_bloom", "Disable Bloom", "On: the glow round lamps, screens and bright lights isn't drawn.", no_bloom, set_no_bloom);
	AddChoice(kVisuals, "depth_of_field", "Depth of Field", "How levels that blur what's far away (Jungle) draw it. Console is the game's own, blocky round anything near. High Quality blurs at full resolution.",
		{"Off", "Console", "High Quality"}, depth_of_field, [](int v) { set_depth_of_field(v); g_dirty = true; });
	AddToggle(kVisuals, "invert_flash", "Inverted Flashes", "On: a flash grenade freezes the picture of the moment it went off, with white edges, instead of whiting out the screen. It fades back as fast as the white would.", invert_flash, set_invert_flash);
	Setting& grain = AddRange(kVisuals, "film_grain", "Film Grain", "How much grain lies over the picture. 100% is the game's own.",
		film_grain, [](float v) { set_film_grain(v); g_dirty = true; }, 0, 1, 0.05f, 2);
	grain.show = [](float v) { return v > 0 ? std::to_string((int)std::lround(v * 100)) + "%" : std::string("Off"); };
	AddToggle(kUnlocks, "unlock_all", "Unlock Everything", "On: every mission and every difficulty can be picked. A save made with it on may keep them.", unlock_all, set_unlock_all);
	AddToggle(kUnlocks, "online_unlocks", "Online Unlocks", "On: online, every weapon, attachment, gadget and game mode is open at any level, but for the Proximity Mine.", online_unlocks, set_online_unlocks);
	AddToggle(kAudio, "occlusion", "Sound Occlusion", "On: in multiplayer a sound behind a wall is quieter, one behind a floor more so, so someone a floor up doesn't sound next to you. Off: the game's own, distance only.", occlusion, set_occlusion);
	AddToggle(kAudio, "sound_height", "Sound Height", "On: in multiplayer a sound from the floor above or below you is muffled, so you can hear that someone isn't on your floor. Off: the game's own, left and right only.", sound_height, set_sound_height);
	AddToggle(kStartup, "online", "Online", "On: the game finds the community server that stands in for Nintendo's (config/hosts.txt). Off: it stays offline. From the next start on.",
		[] { return next_start().online; }, [](bool v) { next_start().online = v; });
	AddToggle(kStartup, "fast_start", "Quicker Start", "On: the screens before the main menu are hurried through. From the next start on.",
		[] { return next_start().fast_start; }, [](bool v) { next_start().fast_start = v; });
#ifndef _WIN32
	AddToggle(kStartup, "crash_dumps", "Crash Dumps", "On: a crash or a stall writes a report into crashes/, to send along with a bug report. From the next start on.",
		[] { return next_start().crash_dumps; }, [](bool v) { next_start().crash_dumps = v; });
#endif
	AddToggle(kUnlocks, "extras_menu", "Unused Extras Menu", "On: Extras also lists Mission Select, Character Viewer, Movies and the Calibration Menu.", extras_menu, set_extras_menu);

	AddToggle(kCheats, "mouse_everywhere", "Always Enable Mouse", "On: the mouse turns the view where the game locks it: cutscenes, melee, climbing, death.", mouse_everywhere, set_mouse_everywhere);
	AddToggle(kCheats, "god_mode", "God Mode", "On: nothing hurts you in the campaign. A fall out of the level or a failed mission still ends it.", god_mode, set_god_mode);
	AddToggle(kCheats, "infinite_ammo", "Infinite Ammo", "On: your ammo never runs out in the campaign.", infinite_ammo, set_infinite_ammo);
	AddToggle(kCheats, "infinite_sprint", "Infinite Sprint", "On: the sprint never runs out in the campaign.", infinite_sprint, set_infinite_sprint);
	Setting& turboRow = AddRange(kCheats, "turbo", "Turbo Mode", "How fast you move in the campaign, times the game's own: walking, sprinting and crouched alike.",
		turbo, [](float v) { set_turbo(v); g_dirty = true; }, 1, kTurboMax, 0.1f, 1);
	turboRow.show = [show = turboRow.show](float v) { return v > 1 ? show(v) + "x" : std::string("Off"); };
	AddToggle(kCheats, "grenades", "Grenades", "On: you carry four frag grenades in the campaign, thrown with Throw Grenade. Every level starts you with four.", grenades, set_grenades);
	AddToggle(kCheats, "noclip", "Noclip", "On: you fly where you look, through walls, and don't fall. The sprint key flies faster.", noclip, set_noclip);
	AddToggle(kCheats, "stamina_meter", "Stamina Meter", "On: a bar shows the sprint that's left. Cancel a sprint right under the line and it's back at a second the frame after.", stamina_meter, set_stamina_meter);
	AddToggle(kCheats, "collision_view", "Collision Viewer", "On: the level's collision is drawn in place of the world. Floors are green, walls grey, ceilings red, props orange.", collision_view, set_collision_view);
	AddToggle(kTriggers, "trigger_view", "Trigger Viewer", "On: the triggers that go off when you walk into them are drawn over the world in the shape the game tests, a colour for each type. Yellow while someone is in one, grey once the game stopped checking it. The near ones have their number over them. The one you are in, or look at, is explained top left: what sets it off and what it does, with a line to each trigger it acts on (green enable, red disable).", trigger_view, set_trigger_view);
	AddToggle(kTriggers, "trigger_xray", "Through Walls", "On: the parts of a trigger behind the level's walls and floors are drawn too, fainter, so every trigger in the level shows. Off: only what is in sight.", trigger_xray, set_trigger_xray);
	AddRange(kTriggers, "trigger_opacity", "Opacity", "How solid the triggers' shapes and lines are, in percent. Lower lets the level show through.",
		trigger_opacity, [](float v) { set_trigger_opacity(v); g_dirty = true; }, 10, 100, 5, 0);
	AddToggle(kTriggers, "trigger_loads", "Show Loading", "On: the triggers that load and unload parts of the level are drawn too, the files they load named over them and in the trigger's explanation. Off: only those with a shape, without the files.", trigger_loads, set_trigger_loads);

	AddToggle(kSpeedrun, "speedrun_timer", "Show Timer", "On: the splits are on screen. Drag them with the mouse while the cursor is free (menus, a pause, this menu); let go outside the game's window and they get their own.", speedrun_timer, set_speedrun_timer);
	AddRange(kSpeedrun, "speedrun_opacity", "Timer Opacity", "How solid the splits are over the game, in percent. Lower lets the game show through. Their own window is always solid.",
		speedrun_opacity, [](float v) { set_speedrun_opacity(v); g_dirty = true; }, 10, 100, 5, 0);
	AddToggle(kSpeedrun, "speedrun_window", "Own Window", "On: the timer is in a window of its own, to put next to the game or capture on its own. Closing that window puts it back in the game's.", speedrun_window, set_speedrun_window);
	AddAction(kSpeedrun, "speedrun_reset", "Reset Run", "Drops the run so far. Your personal best and best segments stay. Can be a hotkey (Hotkeys > Add Hotkey).", []
	{
		speedrun_reset();
		g_resetUntil = Clock::now() + std::chrono::seconds(2);
	});
	g_settings.back().show = [](float) { return std::string(Clock::now() < g_resetUntil ? "Done" : ""); };
	AddToggle(kSpeedrun, "speedrun_quit_reset", "Reset On Save And Quit", "On: choosing Save and Quit in the pause menu resets the run.", speedrun_quit_reset, set_speedrun_quit_reset);

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
#ifdef GE_HL_CROUCH
	AddToggle(kBhop, "bhop_crouch", "Half-Life Crouch", "On: you crouch while the key is held and stand when it's let go. In the air it's instant and pulls your legs up, and landing crouched slides instead of stopping dead. It's never faster than the game's own crouch.",
		bhop_crouch_now, [](bool on) { if (!bhop_locked()) set_bhop_crouch(on); });
#endif
	AddToggle(kBhop, "bhop_hover", "Hovering", "On: as in the game, crouching in the air and the start of a long fall stop your fall for a moment. Off: you keep falling through them.",
		bhop_hover_now, [](bool on) { if (!bhop_locked()) set_bhop_hover(on); });
	AddToggle(kBhop, "bhop_ledge", "Ledge Hops", "On: landing on the edge of a small ledge with Jump held hops again, as Half-Life's square feet would, instead of sliding off it. Surf ramps still surf.",
		bhop_ledge_now, [](bool on) { if (!bhop_locked()) set_bhop_ledge(on); });
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
	for (const auto& add : g_addedToggles) add();
}

Row SettingRow(int i)
{
	const Setting& s = g_settings[i];
	Row r;
	r.label = s.label;
	r.hint = s.hint;
	if (s.kind == Setting::kAction)
	{
		r.value = [i] { return g_settings[i].show(0); };
		r.select = [i] { g_settings[i].set(1); };
		r.wide = true;
		return r;
	}
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
#endif

} //namespace ge::menu

namespace ge
{

using namespace menu;

void settings_toggle(int page, const char* id, const char* label, const char* hint, bool (*get)(), void (*set)(bool)) {
#ifdef GE_IMGUI
    g_addedToggles.push_back([=] { AddToggle(page, id, label, hint, get, set); });
#else
    (void)page, (void)id, (void)label, (void)hint, (void)get, (void)set;
#endif
}

#ifdef GE_IMGUI
const std::vector<Setting>& Settings() { return g_settings; }
//anything on the Cheats page is on, or a code of the launcher's Cheats tab runs
bool cheats_active()
{
	if (gecko_cheats()) return true;
	for (const Setting& s : g_settings)
	{
		if (s.id == "weapon_randomizer" || s.id == "no_attachment_limit") continue; //random guns are a challenge, not an aid
		const float off = s.id == "turbo" ? 1.0f : 0.0f; //turbo's off is the game's speed, 1x
		if ((s.page == kCheats || s.page == kForFun || (s.page == kTriggers && s.id == "trigger_view")) && s.get() > off + 0.05f) return true;
	}
	return false; //bunnyhopping is on its own page, so it doesn't count, nor does the randomizer
}
#else
bool cheats_active() { return gecko_cheats() || god_mode() || infinite_ammo() || infinite_sprint() || turbo() > 1 || grenades() || noclip(); }
#endif

} //namespace ge
