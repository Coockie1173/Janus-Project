//the pages: the rows each shows and where they lead, the hotkey pages, the level grids

#include "ui/settings/menu.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ge::menu
{

#ifdef GE_IMGUI
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

void go(int page, int row) {
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

//the trigger page's types are what the game asked about in the level so far, built again when
//that changes
void RefreshTriggerPage()
{
	uint32_t version = trigger_types_version();
	if (version == g_triggerTypes) return;
	g_triggerTypes = version;
	auto& rows = g_pages[kTriggers].rows;
	rows.resize(g_triggerRows);
	std::vector<TriggerType> types = trigger_types();
	if (types.empty()) rows.push_back({"No Types Yet", "The types show up here once the game has checked a trigger of theirs in the level being played.", {}, {}, {}});
	for (const TriggerType& t : types)
	{
		char label[48], hint[160];
		const char* name = trigger_type_name(t.type);
		if (name) std::snprintf(label, sizeof label, "%s (0x%02X)", name, t.type);
		else std::snprintf(label, sizeof label, "Type 0x%02X", t.type);
		std::snprintf(hint, sizeof hint, "%u checked in this level so far, %u of them with a collision shape. On: drawn. Off: left out.", t.count, t.shaped);
		Row r;
		r.label = label;
		r.hint = hint;
		uint32_t type = t.type;
		r.value = [type] { return std::string(trigger_type_shown(type) ? "On" : "Off"); };
		r.adjust = [type](int) { set_trigger_type_shown(type, !trigger_type_shown(type)); g_dirty = true; };
		r.select = [type] { set_trigger_type_shown(type, !trigger_type_shown(type)); g_dirty = true; };
		rows.push_back(r);
	}
	if (g_page == kTriggers) g_row = std::clamp(g_row, 0, (int)rows.size() - 1);
}

void build_pages() {
    g_pages.resize(kPageCount + g_addedPages.size());
    Page& root = g_pages[kRoot];
    root.title = "Settings";
    root.rows.push_back({"Screen Settings", "Adjust the field of view, the frame rate and the picture.", {}, {}, [] { go(kScreen); }});
    root.rows.push_back({"Control Settings", "Adjust the mouse and the gamepad.", {}, {}, [] { go(kControls); }});
    root.rows.push_back({"Key Bindings", "Choose the keys and mouse buttons for each action.", {}, {}, [] { go(kBindings); }});
    root.rows.push_back({"Hotkeys", "Keys that change a setting while you play.", {}, {}, [] { go(kHotkeys); }});
    root.rows.push_back({"Enhancements", "Things the port adds to the game.", {}, {}, [] { go(kEnhancements); }});
    root.rows.push_back({"Cheats", "Things the game itself would not let you do.", {}, {}, [] { go(kCheats); }});
    root.rows.push_back({"For Fun", "Things that change how the game plays, just for fun.", {}, {}, [] { go(kForFun); }});
    root.rows.push_back({"Speedrun Timer", "A splits timer like LiveSplit's, of the in-game time from Dam to Cradle, against your best.",
        [] { return std::string(speedrun_timer() ? "On" : "Off"); }, {}, [] { go(kSpeedrun); }});
    for (size_t i = 0; i < g_addedPages.size(); ++i) {
        const int page = kPageCount + (int)i;
        g_pages[page].title = g_addedPages[i].title;
        root.rows.push_back({g_addedPages[i].title, g_addedPages[i].about, {}, {}, [page] { go(page); }});
    }
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

    //Enhancements is a page of pages
    const struct { int page; const char* title; const char* about; } kEnhancementPages[] = {
        {kGameplay, "Gameplay", "Skipping cutscenes and movies, quick time events, checkpoints, tutorials, the level timer."},
        {kVisuals, "Visuals", "The colour filter, the bloom, flash grenades and the film grain."},
        {kAudio, "Audio", "How sounds behind walls and on other floors are heard in multiplayer."},
        {kUnlocks, "Unlocks", "Missions, difficulties, online items and the unused extras menu."},
        {kStartup, "Startup", "What happens when the game starts: going online, the screens before the menu, crash reports."},
    };
    for (const auto& e : kEnhancementPages) {
        g_pages[kEnhancements].rows.push_back({e.title, e.about, {}, {}, [page = e.page] { go(page); }});
        g_pages[e.page].title = e.title;
    }
    g_pages[kGameplay].rows.push_back({"Skip Cutscenes (Experimental)", "Choose which cutscenes are skipped: each mission's intro, the one in its middle and its ending.", {}, {}, [] { go(kSkips); }});
    BuildSettings();
    for (size_t i = 0; i < g_settings.size(); ++i) g_pages[g_settings[i].page].rows.push_back(SettingRow((int)i));
	for (size_t i = 0; i < g_addedKeys.size(); ++i)
	{
		Row r;
		r.label = g_addedKeys[i].label;
		r.hint = g_addedKeys[i].hint;
		r.binding = true;
		r.does = "Set Key";
		r.value = [i] { return g_addedKeys[i].key.empty() ? std::string("-") : g_addedKeys[i].key; };
		r.select = [i]
		{
			g_listening = g_row;
			g_listenKey = (int)i;
		};
		r.clear = [i] { g_addedKeys[i].key.clear(); g_dirty = true; };
		g_pages[g_addedKeys[i].page].rows.push_back(r);
	}
	auto& hop = g_pages[kBhop].rows; //the jump right under its toggle
	hop.insert(hop.begin() + std::min<size_t>(1, hop.size()), BindingRow((int)std::size(kActions) - 1));
	hop.push_back({"Settings", "Online through Connect to WFC Modded the host's are everyone's: a host sends its own, and in a game someone else hosts they're shown here and can't be changed. Yours are back after the game.",
		[] { return std::string(bhop_map_locked() ? "The map's, locked" : bhop_locked() ? "The host's, locked" : "Yours"); }, {}, {}});
	hop.push_back({"Defaults", "Every value above back to its own. The switch and the jump key stay as they are.", {}, {}, []
	{
		if (bhop_locked()) return;
		bhop_defaults();
		g_dirty = true;
	}});
	auto& cheats = g_pages[kCheats].rows;
	const auto collision = std::find_if(cheats.begin(), cheats.end(), [](const Row& r) { return r.label == "Collision Viewer"; });
	cheats.insert(collision == cheats.end() ? cheats.end() : collision + 1, {"Trigger Viewer", "The triggers that go off when you walk into them, drawn over the world: its switch, whether they show through walls, and which types.",
		[] { return std::string(trigger_view() ? "On" : "Off"); }, {}, [] { go(kTriggers); }});
	Page& triggers = g_pages[kTriggers];
	triggers.rows.push_back({"Show All Types", "Every type on, the ones not checked yet too.", {}, {}, [] { set_trigger_types_shown(true); g_dirty = true; }});
	triggers.rows.push_back({"Hide All Types", "Every type off, the ones not checked yet too. Then turn on the ones you want.", {}, {}, [] { set_trigger_types_shown(false); g_dirty = true; }});
	g_triggerRows = triggers.rows.size();
	auto& fun = g_pages[kForFun].rows;
	const auto limit = std::find_if(fun.begin(), fun.end(), [](const Row& r) { return r.label == "No Attachment Limit (Experimental)"; });
	fun.insert(limit, {"Bunnyhopping", "Half-Life's bunnyhopping and air strafing: its switch, the jump key and how it feels.", []
	{
		if (bhop_locked()) return std::string(bunnyhop_now() ? (bhop_map_locked() ? "On (map)" : "On (host)") : (bhop_map_locked() ? "Off (map)" : "Off (host)"));
		return std::string(bunnyhop() ? "On" : "Off");
	}, {}, [] { go(kBhop); }});
	//every pack's custom weapons are the randomizer's and the forced weapon's, a switch a pack
	if (modpack_count() > 0)
	{
		const auto forced = std::find_if(fun.begin(), fun.end(), [](const Row& r) { return r.label == "Forced Weapon"; });
		fun.insert(forced == fun.end() ? fun.end() : forced + 1, {"Modpack Weapons", "Which modpacks' custom weapons the Weapon Randomizer and Forced Weapon use, whichever pack is installed.", []
		{
			int on = 0;
			for (int pack = 0; pack < modpack_count(); ++pack) on += randomizer_pack(pack);
			return std::to_string(on) + " of " + std::to_string(modpack_count());
		}, {}, [] { go(kFunPacks); }});
		Page& packs = g_pages[kFunPacks];
		for (int pack = 0; pack < modpack_count(); ++pack)
		{
			const std::string weapons = randomizer_pack_weapons(pack);
			auto flip = [pack] { set_randomizer_pack(pack, !randomizer_pack(pack)); g_dirty = true; };
			packs.rows.push_back({modpack_name(pack), weapons.empty() ? "No custom weapons in this pack." : "Its weapons: " + weapons + ".",
				[pack] { return std::string(randomizer_pack(pack) ? "On" : "Off"); }, [flip](int) { flip(); }, flip});
		}
	}

    Page& screen = g_pages[kScreen];
    screen.title = "Screen Settings";
    screen.rows.push_back({"Restore Defaults", "Back to the game's own fields of view and picture, and 60 frames a second.", {}, {}, [] {
        set_fov(kGameFov);
        set_ads_fov(kGameAdsFov);
        set_fps_limit(60);
        set_anisotropy(1);
        set_anti_aliasing(1);
        set_shake_scale(1.0f);
        set_draw_distance(1.0f);
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
    g_pages[kTriggers].title = "Trigger Viewer";
    g_pages[kFunPacks].title = "Modpack Weapons";
    Page& speedrun = g_pages[kSpeedrun];
    speedrun.title = "Speedrun Timer";
    speedrun.rows.push_back({"Export Splits", "Saves your personal best and best segments as a LiveSplit file (.lss), in game time.",
        [] { return speedrun_said(); }, {}, speedrun_export});
    speedrun.rows.push_back({"Import Splits", "Loads a LiveSplit file (.lss) or one the timer saved. Its game time is taken, its real time where it has none.",
        [] { return speedrun_said(); }, {}, speedrun_import});
    for (size_t i = speedrun.rows.size() - 2; i < speedrun.rows.size(); ++i) speedrun.rows[i].wide = true;
    speedrun.rows.push_back({"Clear Best Times", "Forgets the personal best, every best segment and the attempt count. Select twice to be sure.",
        [] { return std::string(Clock::now() < g_clearUntil ? "Select again" : Clock::now() < g_clearedUntil ? "Cleared" : ""); }, {}, [] {
            if (Clock::now() < g_clearUntil) {
                speedrun_clear_best();
                g_clearUntil = {};
                g_clearedUntil = Clock::now() + std::chrono::seconds(2);
            } else g_clearUntil = Clock::now() + std::chrono::seconds(3);
        }});
    speedrun.rows.back().wide = true;
    g_pages[kHotkeys].title = "Hotkeys";
    g_pages[kHotkeyEdit].title = "Hotkey";
    BuildHotkeyPages();
}
#endif

} //namespace ge::menu
