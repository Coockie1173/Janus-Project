//the game's Extras menu gets an entry "Mods": a page that lists the levels among the mods, one
//of them is picked and loads
//
//the menu's pages are an array object at 805E0590: how many there is room for at +0, how many
//there are at +8, the entries at +0xC, 16 bytes each: the page's hash (7D0000xx), its rows, how
//many rows, a class (7D03xxxx). looked up by hash, in no order
//a row is 20 bytes: a text's hash, a hash, a word, a kind (the top short of the fourth word)
//and a word. the kinds seen here: A9 the page's layout (a file and a script), A7, A8 and A5
//its dressing, A6 the title (the text, the widget 150000B1), A3 a slot of the list (widgets
//1500011B up), AA the footer and after it an AB row for each item with the line that says what
//the item is, and the items themselves:
//1 goes to another page (the second hash), 0xB loads a level (the second hash, then the entrance)
//Extras is page 7D0000D2 (rows 805808B4, 11 of them: Credits and Cheat Codes in two slots).
//the rows the code "Unused extras menu" points it at (80580990) have six slots and four more
//items. 7D000008 is a debug Mission Select nothing leads to without that code, with a level a
//row: where kind 0xB was found
//
//the Mods page is one more entry in the array (a new block from the game's own allocator, so
//the game can grow or free it as it would its own): a level select (class 7D030007, the lit
//level's picture beside the list, port/pictures.cpp) with the rows Conflict's has before its
//maps and a kind 0xB row for each level. Extras gets rows made here, its items and "Mods". a
//layout draws all of its slots, filled or not, so each list is given the layout of one of the
//game's pages with as many slots as it has items (kLayouts). the texts are answered from here (prompts.cpp asks for every text the game looks
//up)
//the unused extras menu's items are in the same rows when that option is on. it used to be two
//writes to where the array was on the heap, which is somewhere else when mods add files
//
//the maps of multiplayer's Conflict are a page of the same kind (7D000045, rows 805830C8: six
//of dressing with one list widget, then a kind 0xB row for each of its ten maps), and each of the seven other
//online gamemodes has one like it (kModes). a gamemode's page can get rows made here, a map
//for every level in its folder of the mods folder (mods/conflict, mods/team_conflict, ...
//port/mods.cpp), as many as there are. the code this replaces wrote other hashes into the
//game's ten rows
//which of the two it has is picked in the Multiplayer menu (7D000061, rows 8057E5A0: Split
//Screen and Nintendo Wi-Fi Connection, which goes to page 7D000049). with a level in any of
//those folders that page gets rows made here too: Split Screen, "Connect to Nintendo WFC" and "Connect to WFC
//Modded", both to 7D000049. the line under the list that says what the lit item is gets
//looked up as the item is lit, and each of the two has a text of its own for it: the one
//asked for last says which maps the gamemodes have
//
//the gamemode is picked in Choose Game Mode (rows 8057FBE0: nine slots, the eight gamemodes and
//Classic Conflict, each to its page of maps; and a second one at 8057FE60, to the search). which one was picked goes by the row's text
//(menu_thread_80232df0), not by its place, so after Connect to WFC Modded the page gets rows
//made here that leave out the gamemodes with no level in their folder. Classic Conflict has
//Conflict's page, and with it Conflict's folder
//
//a console doesn't have those maps and stops on one, and so does someone with other files in
//their mods folder. a network game has a version, 0x4F: the host's session is advertised with
//it and a search asks the server for it (the queries are called ..._VERSIONPROTOCOL...). after
//Connect to WFC Modded both get a number made here from each gamemode's maps and the mods'
//bytes, so only those with the same maps find each other
//
//Extras also gets "Level Select" and "Multiplayer Maps", with mods or without: two more pages
//of the same kind with every level the disc's file list has (port/mods.cpp), the ones no menu
//of the game leads to as well. Level Select has the mt_ files, the missions and the test
//levels by their file's name (but for two that don't load, kBrokenLevels); Multiplayer Maps the mpt_ files, a map once for each gamemode
//it was made for ("Frigate - Conflict"). a row loads its level as a mission is loaded
//
//the same two lists are grids in the settings menu (F1 > Level Select, F1 > Multiplayer Maps,
//port/settings.cpp), which is where 150 levels are easier to pick from
//
//Select Difficulty (rows 80582248 with Time Trial, 805823D8 without: slots, then an item of
//kind 0x13 to 0x17 each, which is what starts the mission) gets rows made here with one more
//item under the last, "007 Custom", to a page made here: four of the game's sliders (kind 5,
//the row's hash a setting's; port/custom.cpp answers for these four) and "Start Mission", a
//row of 007 Classic's kind. a slider's bar has no number, so its label is rewritten with it
//
//GE_MODS_PAGE=7D000045 Extras > Mods opens that page instead (to look at a page without going
//through the menus that lead to it)
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kPages = 0x805E0590; //the array object
const uint32_t kPagesRoom = 0x0, kPagesCount = 0x8, kPagesData = 0xC;
const uint32_t kPageSize = 0x10, kPageRows = 0x4, kPageRowCount = 0x8, kPageClass = 0xC;
const uint32_t kRowSize = 20, kRowHash = 0x4, kRowKind = 0xC;
const uint32_t kExtrasPage = 0x7D0000D2;
const uint32_t kModsPage = 0x7D000DFF; //the game's own stop at 7D00010A
const uint32_t kLevelsPage = 0x7D000DFE, kMapsPage = 0x7D000DFD; //Level Select, Multiplayer Maps
const uint32_t kExtrasRows = 0x805808B4, kExtrasRowCount = 11; //the game's
const uint32_t kUnusedRows = 0x80580990, kUnusedRowCount = 0x19; //the code's, as it set them
//a layout draws all its slots, with an item in them or not, so a list takes the layout of one
//of the game's pages that has as many slots as it has items: the rows of that page up to its
//last slot (layout, two of dressing, title, the slots) and its footer row. past eight the list
//scrolls
struct Layout { uint32_t rows, foot; }; //a page's rows, and which of them is the footer (kind AA)
const Layout kLayouts[] = {
	{0x8057F5D8, 6}, //one slot: 7D000016
	{0x805808B4, 8}, //two: Extras as the game has it
	{0x8057E798, 10}, //three: Options
	{0x80580CF0, 12}, //four: 7D00009E
	{0x80580B60, 14}, //five: 7D00009D
	{0x80580990, 16}, //six: the unused extras menu
	{0x8057EDA0, 18}, //seven: 7D000082
	{0x80580EF8, 25}}; //eight: 7D0000A2, whose own list is longer and scrolls
const uint32_t kLayoutTop = 4, kMostSlots = 8;
const uint32_t kTitleRow = 3;
const uint32_t kLevelSelectClass = 0x7D030007; //the class of a page that is a list of levels with the lit one's picture
const uint32_t kUnusedItems = 10, kUnusedItemCount = 6; //Credits, Cheat Codes, then the four unused ones
const uint32_t kGameItemCount = 2;
const uint32_t kUnusedFoot = 16; //the footer's row there, then a row a item: the line that says what the item is
const uint32_t kKindAbout = 0x00AB0000;
const uint32_t kKindPage = 0x00010000, kKindLevel = 0x000B0000;
const uint32_t kMissionStart = 1; //the entrance a level begins at
const uint32_t kTextMods = 0x4300F0FF, kTextAbout = 0x4300F0FE, kTextLevel = 0x4300F000; //the game's own texts stop at 430017xx
const uint32_t kConflictRows = 0x805830C8; //the game's
const uint32_t kConflictHead = 6; //the rows before the maps
const uint32_t kTextMap = 0x4300F100; //a text for each level of the mods in a gamemode's maps
const uint32_t kTextLevels = 0x4300F0F4, kTextAboutLevels = 0x4300F0F5, kTextMaps = 0x4300F0F6, kTextAboutMaps = 0x4300F0F7;
const uint32_t kTextDisc = 0x4300F200; //a text for each level of the disc
const uint32_t kLevelFiles = 0x01810000; //mt_, the others of the disc's levels are multiplayer's (mpt_)
//two of the disc's test levels end the game with a read through 0 as they load (mt_ai in
//fn_800C7D20, mt_archive_io the same way), so they aren't listed. the other 31 mt_ files get into play
const uint32_t kBrokenLevels[] = { 0x018100ED, 0x01810123 };
//the end of a multiplayer level's name says which gamemode it is for
struct MapKind { const char* ending; const char* mode; };
const MapKind kMapKinds[kMapModeCount] = {{"fdm", "Conflict"}, {"tdm", "Team Conflict"}, {"gdm", "Golden Gun"}, {"bbh", "Black Box"},
	{"gec", "GoldenEye"}, {"htd", "Heroes"}, {"lfd", "License to Kill"}, {"ltd", "Team License to Kill"}};
//an online gamemode: its page of maps, its item in Choose Game Mode by the text. in the order of kModeFolders
struct Mode { uint32_t page, text; };
const Mode kModes[kModeCount] = {
	{0x7D000045, 0x43000E80}, //Conflict
	{0x7D000046, 0x43000E84}, //Team Conflict
	{0x7D000054, 0x43000E81}, //Golden Gun
	{0x7D000056, 0x43000E83}, //Black Box
	{0x7D000057, 0x43000E85}, //GoldenEye
	{0x7D000058, 0x43000E86}, //Heroes
	{0x7D000055, 0x43000E82}, //License to Kill
	{0x7D000059, 0x43000E87}}; //Team License to Kill
const uint32_t kMapRowCount = 16; //each of those pages as the game has it: the six before the maps and ten maps
//Choose Game Mode, the game's. there are two, alike row for row: one whose items go to the gamemode's
//page of maps (7D000042), and the one a search for games comes through, all to 7D000048
const uint32_t kChooseRows[] = { 0x8057FBE0, 0x8057FE60 }, kChooseRowCount = 32;
const uint32_t kChooseItems = 13, kChooseItemCount = 9, kChooseAbout = 23; //its items, and the line of each
const uint32_t kTextChoose = 0x43000E14; //its title
const uint32_t kTextClassic = 0x43001566; //Classic Conflict, the ninth item
const uint32_t kMultiplayerPage = 0x7D000061;
const uint32_t kMultiplayerRows = 0x8057E5A0, kMultiplayerRowCount = 11; //the game's
const uint32_t kSplitScreenRow = 6, kSplitScreenAboutRow = 9;
const uint32_t kTextMultiplayer = 0x43000C88; //the page's title, the game's
const uint32_t kOnlinePage = 0x7D000049; //Nintendo Wi-Fi Connection
const uint32_t kTextWfc = 0x4300F0F0, kTextWfcModded = 0x4300F0F1, kTextAboutWfc = 0x4300F0F2, kTextAboutModded = 0x4300F0F3;
const uint32_t kDifficultyRows[] = { 0x80582248, 0x805823D8 }, kDifficultyItems[] = { 5, 4 }; //Select Difficulty, the game's: with Time Trial and without
const uint32_t kTextDifficulty = 0x430004E1; //its title
const uint32_t kCustomPage = 0x7D000DFC;
const uint32_t kTextCustom = 0x4300F0E0, kTextAboutCustom = 0x4300F0E1, kTextStart = 0x4300F0E2, kTextAboutStart = 0x4300F0E3;
const uint32_t kTextSlider = 0x4300F0E8, kTextAboutSlider = 0x4300F0EC; //one a slider from each
const uint32_t kTextHeadshots = 0x4300F0E6, kTextAboutHeadshots = 0x4300F0E7;
const uint32_t kKindSlider = 0x00050000, kKindChoice = 0x00030000, kKindClassic = 0x00160000; //a slider of a setting, a setting with values to pick from, 007 Classic
const uint32_t kLabelRoom = 48; //characters a slider's label has room for
const uint32_t kAlloc = 0x803428C0; //(bytes, flags) -> a block of the game's heap
const uint32_t kMostPages = 4096;

PPCFunc game_alloc = nullptr;
std::vector<ModLevel> g_levels;
std::vector<const ModLevel*> g_listed; //the ones Extras > Mods lists: a name once
std::map<uint32_t, uint32_t> g_texts; //a text's hash, its characters in the game's memory
uint32_t g_mods_rows = 0, g_mods_row_count = 0;
struct DiscLevel { uint32_t hash; std::string shown; };
std::vector<DiscLevel> g_disc[2]; //the disc's levels: the missions and tests, multiplayer's maps
uint32_t g_disc_rows[2] = {}, g_disc_row_count[2] = {};
bool g_built = false;
uint32_t g_extras_rows[2] = {}, g_extras_row_count[2] = {}; //without and with the unused items
bool g_failed = false;

std::vector<const ModLevel*> g_pool[kModeCount]; //the levels in each gamemode's folder
bool g_any_pool = false;
std::atomic<bool> g_map_pool{false}; //Connect to WFC Modded was the item lit last
uint32_t g_pool_rows[kModeCount] = {}, g_pool_row_count[kModeCount] = {};
uint32_t g_game_rows[kModeCount] = {}; //the game's own rows of each gamemode's page, as found
uint32_t g_choose_rows[2] = {}, g_choose_row_count[2] = {}; //Choose Game Mode without the gamemodes that have no maps
uint32_t g_multiplayer_rows = 0, g_multiplayer_row_count = 0;
bool g_pool_failed = false;
uint32_t g_pool_version = 0; //a network game's version with those maps
bool g_trace = false;
uint32_t g_mods_target = kModsPage; //GE_MODS_PAGE

uint32_t g_difficulty_rows[2] = {}, g_difficulty_row_count[2] = {}; //Select Difficulty with 007 Custom
uint32_t g_custom_rows = 0, g_custom_row_count = 0, g_custom_start = 0; //its page, and the row that starts the mission
uint32_t g_custom_labels[kCustomSliders] = {};
uint32_t g_custom_class = 0;
bool g_custom_failed = false;

uint32_t Alloc(const PPCContext& c, uint32_t bytes)
{
	PPCContext call = c;
	uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	call.r[3] = bytes;
	call.r[4] = 0;
	game_alloc(call);
	return guest_ptr(call.r[3]) ? call.r[3] : 0;
}

uint32_t Text(const PPCContext& c, const std::string& text)
{
	uint32_t at = Alloc(c, uint32_t(text.size() + 1) * 2);
	if (!at) return 0;
	for (size_t i = 0; i <= text.size(); ++i) st16(at + uint32_t(i) * 2, i < text.size() ? (uint8_t)text[i] : 0);
	return at;
}

void CopyRows(uint32_t to, uint32_t from, uint32_t count)
{
	for (uint32_t i = 0; i < count * kRowSize; i += 4) st32(to + i, ld32(from + i));
}

void Row(uint32_t at, uint32_t text, uint32_t hash, uint32_t word, uint32_t kind)
{
	st32(at, text); st32(at + 4, hash); st32(at + 8, word); st32(at + 0xC, kind); st32(at + 0x10, 0);
}

uint32_t FindPage(uint32_t data, uint32_t count, uint32_t hash)
{
	for (uint32_t i = 0; i < count; ++i)
		if (ld32(data + i * kPageSize) == hash) return data + i * kPageSize;
	return 0;
}

//a level's name in the list: its file's name without mt_ and .edb
std::string Shown(const std::string& file)
{
	std::string name = file;
	if (name.size() > 4 && name.compare(name.size() - 4, 4, ".edb") == 0) name.resize(name.size() - 4);
	if (name.compare(0, 3, "mt_") == 0) name.erase(0, 3);
	std::replace(name.begin(), name.end(), '_', ' ');
	if (!name.empty()) name[0] = (char)std::toupper((unsigned char)name[0]);
	return name.empty() ? file : name;
}

//room for a list of items in the layout that fits it: the rows before the items are filled in,
//the items, the footer and a line an item are the caller's. where the items go
uint32_t ListRows(const PPCContext& c, uint32_t items, uint32_t title, uint32_t& rows, uint32_t& count)
{
	const uint32_t slots = std::min(std::max(items, 1u), kMostSlots);
	const Layout& layout = kLayouts[slots - 1];
	const uint32_t head = kLayoutTop + slots;
	count = head + items * 2 + 1;
	rows = Alloc(c, count * kRowSize);
	if (!rows) return 0;
	CopyRows(rows, layout.rows, head);
	st32(rows + kTitleRow * kRowSize, title);
	CopyRows(rows + (head + items) * kRowSize, layout.rows + layout.foot * kRowSize, 1);
	return rows + head * kRowSize;
}

//a map that has a level of its own triggers in each gamemode's folder is in Extras > Mods once:
//the level in the mods folder itself when there is one, the first of the others when not
void ListLevels()
{
	for (const ModLevel& level : g_levels)
	{
		auto same = std::find_if(g_listed.begin(), g_listed.end(), [&](const ModLevel* p) { return p->name == level.name; });
		if (same == g_listed.end()) g_listed.push_back(&level);
		else if ((*same)->modes != 0 && level.modes == 0) *same = &level;
	}
}

//a multiplayer level's map and gamemode: mpt_frigate_fdm.edb is "Frigate" and Conflict's number
std::string MapOf(const std::string& file, int& mode)
{
	std::string name = file;
	if (name.size() > 4 && name.compare(name.size() - 4, 4, ".edb") == 0) name.resize(name.size() - 4);
	if (name.compare(0, 4, "mpt_") == 0) name.erase(0, 4);
	mode = -1;
	const size_t last = name.find_last_of('_');
	for (int i = 0; i < kMapModeCount; ++i)
	{
		if (last != std::string::npos && name.compare(last + 1, std::string::npos, kMapKinds[i].ending) == 0) mode = i;
	}
	if (mode >= 0) name.resize(last);
	std::replace(name.begin(), name.end(), '_', ' ');
	if (!name.empty()) name[0] = (char)std::toupper((unsigned char)name[0]);
	return name;
}

//its name in the game's list: "Frigate - Conflict"
std::string ShownMap(const std::string& file)
{
	int mode = -1;
	const std::string name = MapOf(file, mode);
	return mode < 0 ? name : name + " - " + kMapKinds[mode].mode;
}

//the disc's levels in the two lists, each by name
void ListDisc()
{
	for (const ModLevel& level : disc_levels())
	{
		const bool map = (level.hash & 0xFFFF0000u) != kLevelFiles;
		if (std::find(std::begin(kBrokenLevels), std::end(kBrokenLevels), level.hash) != std::end(kBrokenLevels)) continue;
		g_disc[map].push_back({ level.hash, map ? ShownMap(level.name) : Shown(level.name) });
	}
	for (auto& list : g_disc) std::sort(list.begin(), list.end(), [](const DiscLevel& a, const DiscLevel& b) { return a.shown < b.shown; });
}

//a level select like Conflict's: its rows before the maps, then a level a row
uint32_t LevelRows(const PPCContext& c, uint32_t title, uint32_t levels, uint32_t& count)
{
	count = kConflictHead + levels;
	const uint32_t rows = Alloc(c, count * kRowSize);
	if (!rows) return 0;
	CopyRows(rows, kConflictRows, kConflictHead);
	st32(rows + kTitleRow * kRowSize, title);
	return rows;
}

//the rows and the texts, made once
bool Build(const PPCContext& c)
{
	struct Item { uint32_t text, page, about; };
	std::vector<Item> items; //what Extras gets after the game's own
	const uint32_t discPages[2] = { kLevelsPage, kMapsPage }, discTitles[2] = { kTextLevels, kTextMaps }, discAbouts[2] = { kTextAboutLevels, kTextAboutMaps };
	const char* const discNames[2] = { "Level Select", "Multiplayer Maps" };
	const char* const discSays[2] = { "Play any level of the disc, the unused ones too.", "Walk around any multiplayer map of the disc, the unused ones too." };
	uint32_t text = kTextDisc;
	for (int list = 0; list < 2; ++list)
	{
		if (g_disc[list].empty()) continue;
		g_texts[discTitles[list]] = Text(c, discNames[list]);
		g_texts[discAbouts[list]] = Text(c, discSays[list]);
		g_disc_rows[list] = LevelRows(c, discTitles[list], (uint32_t)g_disc[list].size(), g_disc_row_count[list]);
		if (!g_disc_rows[list]) return false;
		for (size_t i = 0; i < g_disc[list].size(); ++i, ++text)
		{
			g_texts[text] = Text(c, g_disc[list][i].shown);
			Row(g_disc_rows[list] + (kConflictHead + (uint32_t)i) * kRowSize, text, g_disc[list][i].hash, kMissionStart, kKindLevel);
		}
		items.push_back({ discTitles[list], discPages[list], discAbouts[list] });
	}
	if (!g_listed.empty())
	{
		g_texts[kTextMods] = Text(c, "Mods");
		g_texts[kTextAbout] = Text(c, "Play the levels in the mods folder.");
		for (size_t i = 0; i < g_listed.size(); ++i) g_texts[kTextLevel + (uint32_t)i] = Text(c, Shown(g_listed[i]->name));
		g_mods_rows = LevelRows(c, kTextMods, (uint32_t)g_listed.size(), g_mods_row_count);
		if (!g_mods_rows) return false;
		for (uint32_t i = 0; i < (uint32_t)g_listed.size(); ++i) Row(g_mods_rows + (kConflictHead + i) * kRowSize, kTextLevel + i, g_listed[i]->hash, kMissionStart, kKindLevel);
		items.push_back({ kTextMods, kModsPage, kTextAbout });
	}
	for (const auto& made : g_texts)
		if (!made.second) return false;
	if (g_mods_target != kModsPage && !items.empty()) items.back().page = g_mods_target;

	for (int unused = 0; unused < 2; ++unused)
	{
		const uint32_t game = unused ? kUnusedItemCount : kGameItemCount, count = game + (uint32_t)items.size();
		const uint32_t at = ListRows(c, count, ld32(kUnusedRows + kTitleRow * kRowSize), g_extras_rows[unused], g_extras_row_count[unused]);
		if (!at) return false;
		CopyRows(at, kUnusedRows + kUnusedItems * kRowSize, game);
		const uint32_t about = at + (count + 1) * kRowSize;
		CopyRows(about, kUnusedRows + (kUnusedFoot + 1) * kRowSize, game);
		for (uint32_t i = 0; i < (uint32_t)items.size(); ++i)
		{
			Row(at + (game + i) * kRowSize, items[i].text, items[i].page, 0, kKindPage);
			Row(about + (game + i) * kRowSize, items[i].about, 0, 0, kKindAbout);
		}
	}
	return true;
}

//FNV-1a
const uint32_t kFnvStart = 0x811C9DC5, kFnvPrime = 0x01000193;
void Mix(uint32_t& sum, const uint8_t* bytes, size_t count)
{
	for (size_t i = 0; i < count; ++i) sum = (sum ^ bytes[i]) * kFnvPrime;
}

//a number for the pools: for each gamemode its number, each map's hash and every byte of its
//file, in the order of the hashes (the order in the folder doesn't count), then the same of
//every mod that isn't a level (a map's geometry is a file of its own). the hash a mod has
//here goes in too, two with the same one get theirs by what else is in the folders
const uint32_t kGameVersion = 0x4F;
const uint32_t kModdedVersions = 0x40000000; //where the numbers made here are, the game's own never is
//what this build sends differently from the game in such a game goes in as well, so builds
//that don't read each other's packets don't find each other: 1 the pitch's range (net.cpp),
//2 a pool for every gamemode, 3 a command's yaw and lean are the mouse's turn (input.cpp),
//4 a player's state has its ping behind it (net.cpp), 5 a command has the jump key behind it and a
//player's state the host's bunnyhopping (bunnyhop.cpp)
const uint8_t kPortProtocol = 5;

uint32_t FileSum(const std::string& path)
{
	uint32_t sum = kFnvStart;
	std::ifstream file(path, std::ios::binary);
	if (!file) { rt_log("goldeneye: %s couldn't be read for the network game's version", path.c_str()); return sum; }
	std::vector<uint8_t> bytes(1 << 16);
	while (file.read((char*)bytes.data(), (std::streamsize)bytes.size()) || file.gcount() > 0)
		Mix(sum, bytes.data(), (size_t)file.gcount());
	return sum;
}

void Mix32(uint32_t& sum, uint32_t value)
{
	const uint8_t bytes[4] = { uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value) };
	Mix(sum, bytes, sizeof bytes);
}

uint32_t PoolVersion()
{
	std::map<std::string, uint32_t> sums; //a file in several gamemodes is read once
	uint32_t sum = kFnvStart;
	Mix(sum, &kPortProtocol, 1);
	for (int mode = 0; mode < kModeCount; ++mode)
	{
		std::vector<const ModLevel*> maps = g_pool[mode];
		std::sort(maps.begin(), maps.end(), [](const ModLevel* a, const ModLevel* b) { return a->hash < b->hash; });
		Mix32(sum, (uint32_t)mode);
		for (const ModLevel* pMap : maps)
		{
			auto found = sums.find(pMap->path);
			if (found == sums.end()) found = sums.emplace(pMap->path, FileSum(pMap->path)).first;
			Mix32(sum, pMap->hash);
			Mix32(sum, found->second);
			//who the players are in it
			const std::string cast = mod_side_file(*pMap, ".txt");
			std::error_code error;
			if (std::filesystem::exists(cast, error)) Mix32(sum, FileSum(cast));
		}
	}
	//what isn't a level goes in too: the geometry a map's levels load, a file in place of the disc's
	std::vector<const ModLevel*> others;
	for (const ModLevel& file : mod_files())
		if (std::none_of(g_levels.begin(), g_levels.end(), [&](const ModLevel& level) { return level.hash == file.hash; })) others.push_back(&file);
	std::sort(others.begin(), others.end(), [](const ModLevel* a, const ModLevel* b) { return a->hash < b->hash; });
	for (const ModLevel* pFile : others)
	{
		Mix32(sum, pFile->hash);
		Mix32(sum, FileSum(pFile->path));
	}
	return kModdedVersions | (sum & (kModdedVersions - 1));
}

//each gamemode's maps: the levels in its folder
void ReadPool()
{
	for (const ModLevel& level : g_levels)
		for (int mode = 0; mode < kModeCount; ++mode)
			if (level.modes & (1u << mode)) g_pool[mode].push_back(&level);
	for (int mode = 0; mode < kModeCount; ++mode)
	{
		if (g_pool[mode].empty()) continue;
		g_any_pool = true;
		rt_log("goldeneye: %zu map(s) in %s/%s", g_pool[mode].size(), mods_folder().c_str(), kModeFolders[mode]);
	}
	if (!g_any_pool) return;
	g_pool_version = PoolVersion();
	rt_log("goldeneye: a network game with them is version %08X (the game's own: %X)", g_pool_version, kGameVersion);
}

//the Multiplayer menu with the two ways online, when there is a pool to pick
void MultiplayerFrame(const PPCContext& c, uint32_t data, uint32_t count)
{
	const uint32_t page = FindPage(data, count, kMultiplayerPage);
	if (!page || !g_any_pool || g_pool_failed || !game_alloc) return;
	if (!g_multiplayer_rows)
	{
		g_texts[kTextWfc] = Text(c, "Connect to Nintendo WFC");
		g_texts[kTextWfcModded] = Text(c, "Connect to WFC Modded");
		g_texts[kTextAboutWfc] = Text(c, "Connect to Nintendo WFC.");
		g_texts[kTextAboutModded] = Text(c, "Connect to Nintendo WFC with the gamemodes and maps of the mods folder.");
		const uint32_t items = 3;
		uint32_t rows = 0;
		const uint32_t at = ListRows(c, items, kTextMultiplayer, rows, g_multiplayer_row_count);
		if (!at || !g_texts[kTextWfc] || !g_texts[kTextWfcModded] || !g_texts[kTextAboutWfc] || !g_texts[kTextAboutModded]) { g_pool_failed = true; return; }
		CopyRows(at, kMultiplayerRows + kSplitScreenRow * kRowSize, 1);
		Row(at + kRowSize, kTextWfc, kOnlinePage, 0, kKindPage);
		Row(at + 2 * kRowSize, kTextWfcModded, kOnlinePage, 0, kKindPage);
		const uint32_t about = at + (items + 1) * kRowSize;
		CopyRows(about, kMultiplayerRows + kSplitScreenAboutRow * kRowSize, 1);
		Row(about + kRowSize, kTextAboutWfc, 0, 0, kKindAbout);
		Row(about + 2 * kRowSize, kTextAboutModded, 0, 0, kKindAbout);
		g_multiplayer_rows = rows;
	}
	if (ld32(page + kPageRows) != kMultiplayerRows) return;
	st32(page + kPageRows, g_multiplayer_rows);
	st32(page + kPageRowCount, g_multiplayer_row_count);
}

void Write(uint32_t at, const std::string& text, uint32_t room)
{
	const size_t count = std::min(text.size(), size_t(room - 1));
	for (size_t i = 0; i <= count; ++i) st16(at + uint32_t(i) * 2, i < count ? (uint8_t)text[i] : 0);
}

//007 Custom's page and Select Difficulty's rows with the item for it, made once
bool BuildCustom(const PPCContext& c)
{
	g_texts[kTextCustom] = Text(c, "007 Custom");
	g_texts[kTextAboutCustom] = Text(c, "007 Classic with the enemies' health, damage and reactions set by you.");
	g_texts[kTextStart] = Text(c, "Start Mission");
	g_texts[kTextAboutStart] = Text(c, "Play the mission as 007 Classic with these settings.");
	g_texts[kTextHeadshots] = Text(c, "Instant Kill Headshots");
	g_texts[kTextAboutHeadshots] = Text(c, "Off: a shot to an enemy's head does what a shot to the body does.");
	g_texts[kCustomTextOn] = Text(c, "On");
	g_texts[kCustomTextOff] = Text(c, "Off");
	bool made = g_texts[kTextCustom] && g_texts[kTextAboutCustom] && g_texts[kTextStart] && g_texts[kTextAboutStart]
		&& g_texts[kTextHeadshots] && g_texts[kTextAboutHeadshots] && g_texts[kCustomTextOn] && g_texts[kCustomTextOff];
	for (int i = 0; i < kCustomSliders; ++i)
	{
		g_custom_labels[i] = Alloc(c, kLabelRoom * 2);
		if (g_custom_labels[i]) Write(g_custom_labels[i], custom_label(i), kLabelRoom);
		g_texts[kTextSlider + i] = g_custom_labels[i];
		g_texts[kTextAboutSlider + i] = Text(c, custom_about(i));
		made = made && g_custom_labels[i] && g_texts[kTextAboutSlider + i];
	}
	if (!made) return false;

	const uint32_t items = kCustomRows, start = kCustomSliders + 1;
	const uint32_t at = ListRows(c, items, kTextCustom, g_custom_rows, g_custom_row_count);
	if (!at) return false;
	const uint32_t about = at + (items + 1) * kRowSize;
	for (uint32_t i = 0; i < kCustomSliders; ++i)
	{
		Row(at + i * kRowSize, kTextSlider + i, kCustomSetting + i, 0, kKindSlider);
		Row(about + i * kRowSize, kTextAboutSlider + i, 0, 0, kKindAbout);
	}
	Row(at + kCustomSliders * kRowSize, kTextHeadshots, kCustomHeadshots, 0, kKindChoice);
	Row(about + kCustomSliders * kRowSize, kTextAboutHeadshots, 0, 0, kKindAbout);
	g_custom_start = at + start * kRowSize;
	Row(g_custom_start, kTextStart, 0, 0, kKindClassic);
	Row(about + start * kRowSize, kTextAboutStart, 0, 0, kKindAbout);

	for (int which = 0; which < 2; ++which)
	{
		const uint32_t game = kDifficultyRows[which], had = kDifficultyItems[which], count = had + 1;
		const uint32_t to = ListRows(c, count, kTextDifficulty, g_difficulty_rows[which], g_difficulty_row_count[which]);
		if (!to) return false;
		CopyRows(to, game + (kLayoutTop + had) * kRowSize, had);
		Row(to + had * kRowSize, kTextCustom, kCustomPage, 0, kKindPage);
		const uint32_t line = to + (count + 1) * kRowSize;
		CopyRows(line, game + (kLayoutTop + had + had + 1) * kRowSize, had);
		Row(line + had * kRowSize, kTextAboutCustom, 0, 0, kKindAbout);
	}
	return true;
}

//once a frame: Select Difficulty has 007 Custom, whose page is one more of the array
void CustomFrame(const PPCContext& c, uint32_t& data, uint32_t& count)
{
	if (g_custom_failed || !game_alloc) return;
	if (!g_custom_rows && !BuildCustom(c))
	{
		g_custom_failed = true;
		rt_log("goldeneye: 007 Custom's page couldn't be made");
		return;
	}
	for (int i = 0; i < kCustomSliders; ++i) Write(g_custom_labels[i], custom_label(i), kLabelRoom);
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t page = data + i * kPageSize, rows = ld32(page + kPageRows);
		for (int which = 0; which < 2; ++which)
		{
			if (rows != kDifficultyRows[which]) continue;
			if (!g_custom_class)
			{
				g_custom_class = ld32(page + kPageClass);
				rt_log("goldeneye: Select Difficulty (page %08X, class %08X) has 007 Custom", ld32(page), g_custom_class);
			}
			st32(page + kPageRows, g_difficulty_rows[which]);
			st32(page + kPageRowCount, g_difficulty_row_count[which]);
		}
	}
	if (!g_custom_class || FindPage(data, count, kCustomPage)) return;
	//the array in a new block with one more page, the game's old one is left as it is
	const uint32_t fresh = Alloc(c, (count + 1) * kPageSize);
	if (!fresh) return;
	for (uint32_t i = 0; i < count * kPageSize; i += 4) st32(fresh + i, ld32(data + i));
	const uint32_t page = fresh + count * kPageSize;
	st32(page, kCustomPage);
	st32(page + kPageRows, g_custom_rows);
	st32(page + kPageRowCount, g_custom_row_count);
	st32(page + kPageClass, g_custom_class);
	data = fresh;
	count += 1;
	st32(kPages + kPagesData, data);
	st32(kPages + kPagesCount, count);
	st32(kPages + kPagesRoom, count);
}

bool Modded() { return g_map_pool && g_any_pool && !g_pool_failed; }

//a gamemode's page gets its pool's rows after Connect to WFC Modded, the game's own otherwise
void PoolFrame(const PPCContext& c, uint32_t data, uint32_t count, int mode)
{
	const uint32_t page = FindPage(data, count, kModes[mode].page);
	if (!page) return;
	if (!g_game_rows[mode]) g_game_rows[mode] = ld32(page + kPageRows);
	const uint32_t game = g_game_rows[mode];
	const auto& pool = g_pool[mode];
	const bool on = Modded() && !pool.empty() && game_alloc;
	if (on && !g_pool_rows[mode])
	{
		g_pool_row_count[mode] = kConflictHead + (uint32_t)pool.size();
		const uint32_t rows = Alloc(c, g_pool_row_count[mode] * kRowSize);
		if (!rows) { g_pool_failed = true; return; }
		CopyRows(rows, game, kConflictHead);
		for (size_t i = 0; i < pool.size(); ++i)
		{
			const uint32_t text = kTextMap + uint32_t(pool[i] - g_levels.data());
			if (!g_texts[text]) g_texts[text] = Text(c, Shown(pool[i]->name));
			if (!g_texts[text]) { g_pool_failed = true; return; }
			Row(rows + (kConflictHead + (uint32_t)i) * kRowSize, text, pool[i]->hash, kMissionStart, kKindLevel);
		}
		g_pool_rows[mode] = rows;
		rt_log("goldeneye: the maps of %s are the %zu of its folder", kModeFolders[mode], pool.size());
	}
	const uint32_t rows = on ? g_pool_rows[mode] : game, have = ld32(page + kPageRows);
	if (have == rows || (have != game && have != g_pool_rows[mode])) return;
	st32(page + kPageRows, rows);
	st32(page + kPageRowCount, on ? g_pool_row_count[mode] : kMapRowCount);
}

//a Choose Game Mode after Connect to WFC Modded: only the gamemodes that have maps
void ChooseFrame(const PPCContext& c, uint32_t data, uint32_t count, int which)
{
	const uint32_t game = kChooseRows[which];
	uint32_t& made = g_choose_rows[which];
	uint32_t page = 0;
	for (uint32_t i = 0; i < count && !page; ++i)
	{
		const uint32_t rows = ld32(data + i * kPageSize + kPageRows);
		if (rows == game || (made && rows == made)) page = data + i * kPageSize;
	}
	if (!page || !g_any_pool || g_pool_failed || !game_alloc) return;
	if (!made)
	{
		std::vector<uint32_t> items; //which of the game's nine stay
		for (uint32_t i = 0; i < kChooseItemCount; ++i)
		{
			const uint32_t text = ld32(game + (kChooseItems + i) * kRowSize);
			int mode = 0;
			while (mode < kModeCount && kModes[mode].text != text) ++mode;
			if (text == kTextClassic) mode = 0;
			if (mode == kModeCount || !g_pool[mode].empty()) items.push_back(i);
		}
		if (items.size() == kChooseItemCount) { made = game; g_choose_row_count[which] = kChooseRowCount; }
		else
		{
			uint32_t rows = 0;
			const uint32_t at = ListRows(c, (uint32_t)items.size(), kTextChoose, rows, g_choose_row_count[which]);
			if (!at) { g_pool_failed = true; return; }
			const uint32_t about = at + uint32_t(items.size() + 1) * kRowSize;
			for (size_t i = 0; i < items.size(); ++i)
			{
				CopyRows(at + uint32_t(i) * kRowSize, game + (kChooseItems + items[i]) * kRowSize, 1);
				CopyRows(about + uint32_t(i) * kRowSize, game + (kChooseAbout + items[i]) * kRowSize, 1);
			}
			made = rows;
			rt_log("goldeneye: Choose Game Mode (page %08X) has %zu of the %u gamemodes after Connect to WFC Modded", ld32(page), items.size(), kChooseItemCount);
		}
	}
	const bool on = Modded();
	const uint32_t rows = on ? made : game;
	if (ld32(page + kPageRows) == rows) return;
	st32(page + kPageRows, rows);
	st32(page + kPageRowCount, on ? g_choose_row_count[which] : kChooseRowCount);
}

//ge_pick_next_maps (801E5860): a network game's next two maps, each drawn at random from the
//mode's page until the first is neither of the two before and the second neither of those nor
//the first. the game's lists have ten maps, with a list of two or three that never ends
//the same here, but after a number of draws a map that was there before is taken, and with
//one map both are that one
const uint32_t kMatchMode = 0x2C, kMatchFirst = 0x48, kMatchSecond = 0x4C;
const uint32_t kRandomMap = 0x8023CD50; //(the pages, a mode) -> a map of that mode's page
const int kDraws = 64;
PPCFunc random_map = nullptr;

uint32_t RandomMap(const PPCContext& c, uint32_t mode)
{
	PPCContext call = c;
	uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	call.r[3] = kPages;
	call.r[4] = mode;
	random_map(call);
	return call.r[3];
}

void PickNextMaps(PPCContext& c)
{
	const uint32_t match = c.r[3], mode = ld32(match + kMatchMode);
	const uint32_t before[2] = { ld32(match + kMatchFirst), ld32(match + kMatchSecond) };
	uint32_t first = before[0];
	for (int i = 0; i < kDraws && (first == before[0] || first == before[1]); ++i) first = RandomMap(c, mode);
	uint32_t second = first;
	for (int i = 0; i < kDraws && (second == first || second == before[0] || second == before[1]); ++i) second = RandomMap(c, mode);
	for (int i = 0; i < kDraws && second == first; ++i) second = RandomMap(c, mode); //not a new one then, but another
	st32(match + kMatchFirst, first);
	st32(match + kMatchSecond, second);
}

//the version of a network game, where it goes to the server
//ge_session_export (801F6A50): the host's session into the record it is advertised with (the
//object's +4): the version at +0x154, always 0x4F
//ge_find_query (8021E2D0): a search's task into the query the server gets: the version from
//the task's +0x44, which the function before it (fn_8021DC00) has just set to 0x4F
//the session's own version (+0x20, fn_801E5610) stays: a LAN game compares it with 0x4F itself
const uint32_t kExportRecord = 0x4, kRecordVersion = 0x154, kTaskVersion = 0x44;
PPCFunc orig_session_export = nullptr, orig_find_query = nullptr;

void SessionExport(PPCContext& c)
{
	const uint32_t record = ld32(c.r[3] + kExportRecord);
	orig_session_export(c);
	if (!Modded() || !guest_ptr(record)) return;
	st32(record + kRecordVersion, g_pool_version);
	if (g_trace) rt_log("goldeneye: online: the session is advertised as version %08X", g_pool_version);
}

void FindQuery(PPCContext& c)
{
	if (Modded())
	{
		st32(c.r[3] + kTaskVersion, g_pool_version);
		if (g_trace) rt_log("goldeneye: online: searching for games of version %08X", g_pool_version);
	}
	orig_find_query(c);
}

} //namespace

std::string mod_shown_name(const std::string& file) { return Shown(file); }
std::string map_shown_name(const std::string& file, int& mode) { return MapOf(file, mode); }
const char* map_mode_name(int mode) { return kMapKinds[mode].mode; }
bool level_loads(uint32_t hash) { return std::find(std::begin(kBrokenLevels), std::end(kBrokenLevels), hash) == std::end(kBrokenLevels); }
bool modded_lobby() { return Modded(); }
bool custom_start_row(uint32_t row) { return row != 0 && row == g_custom_start; }

uint32_t mods_menu_text(uint32_t hash)
{
	//the line under the Multiplayer menu's list: which way online is lit
	if (hash == kTextAboutModded || hash == kTextAboutWfc)
	{
		const bool modded = hash == kTextAboutModded;
		if (g_map_pool.exchange(modded) != modded && g_trace) rt_log("goldeneye: Multiplayer: %s is lit", modded ? "Connect to WFC Modded" : "Connect to Nintendo WFC");
	}
	auto found = g_texts.find(hash);
	return found == g_texts.end() ? 0 : found->second;
}

//once a frame, on the game's thread (the limiter's hook)
void mods_menu_frame(PPCContext& c)
{
	uint32_t count = ld32(kPages + kPagesCount);
	uint32_t data = ld32(kPages + kPagesData);
	if (!guest_ptr(data) || count == 0 || count > kMostPages) return;
	CustomFrame(c, data, count);
	MultiplayerFrame(c, data, count);
	for (int mode = 0; mode < kModeCount; ++mode) PoolFrame(c, data, count, mode);
	for (int which = 0; which < 2; ++which) ChooseFrame(c, data, count, which);
	const uint32_t extras = FindPage(data, count, kExtrasPage);
	if (!extras) return;

	if ((g_listed.empty() && g_disc[0].empty() && g_disc[1].empty()) || g_failed)
	{
		//nothing to add: the game's rows, or the code's
		const bool unused = extras_menu();
		const uint32_t rows = unused ? kUnusedRows : kExtrasRows;
		const uint32_t have = ld32(extras + kPageRows);
		if (have == rows || (have != kExtrasRows && have != kUnusedRows)) return;
		st32(extras + kPageRows, rows);
		st32(extras + kPageRowCount, unused ? kUnusedRowCount : kExtrasRowCount);
		return;
	}

	if (!g_built && (!game_alloc || !Build(c)))
	{
		g_failed = true;
		rt_log("goldeneye: the Extras menu's own pages couldn't be made");
		return;
	}
	g_built = true;

	struct Own { uint32_t page, rows, count; };
	const Own all[] = { { kLevelsPage, g_disc_rows[0], g_disc_row_count[0] }, { kMapsPage, g_disc_rows[1], g_disc_row_count[1] }, { kModsPage, g_mods_rows, g_mods_row_count } };
	std::vector<Own> missing;
	for (const Own& own : all)
		if (own.rows && !FindPage(data, count, own.page)) missing.push_back(own);
	uint32_t pages = count;
	if (!missing.empty())
	{
		//more pages: the array in a new block, the game's old one is left as it is
		pages = count + (uint32_t)missing.size();
		const uint32_t fresh = Alloc(c, pages * kPageSize);
		if (!fresh) return;
		for (uint32_t i = 0; i < count * kPageSize; i += 4) st32(fresh + i, ld32(data + i));
		for (size_t i = 0; i < missing.size(); ++i)
		{
			const uint32_t page = fresh + (count + (uint32_t)i) * kPageSize;
			st32(page, missing[i].page);
			st32(page + kPageRows, missing[i].rows);
			st32(page + kPageRowCount, missing[i].count);
			st32(page + kPageClass, kLevelSelectClass);
		}
		st32(kPages + kPagesData, fresh);
		st32(kPages + kPagesCount, pages);
		st32(kPages + kPagesRoom, pages);
		data = fresh;
		rt_log("goldeneye: Extras has Level Select: %zu level(s), Multiplayer Maps: %zu, Mods: %zu", g_disc[0].size(), g_disc[1].size(), g_listed.size());
	}

	const int unused = extras_menu() ? 1 : 0;
	const uint32_t entry = FindPage(data, pages, kExtrasPage);
	if (entry && ld32(entry + kPageRows) != g_extras_rows[unused])
	{
		st32(entry + kPageRows, g_extras_rows[unused]);
		st32(entry + kPageRowCount, g_extras_row_count[unused]);
	}
}

void install_mods_menu()
{
	g_levels = mod_levels();
	ListLevels();
	ListDisc();
	game_alloc = ppc_lookup(kAlloc);
	g_trace = std::getenv("GE_TRACE") != nullptr;
	if (const char* e = std::getenv("GE_MODS_PAGE")) g_mods_target = (uint32_t)std::strtoul(e, nullptr, 16);
	ReadPool();
	random_map = ppc_lookup(kRandomMap);
	if (random_map) ppc_hook("ge_pick_next_maps", PickNextMaps);
	if (g_any_pool)
	{
		orig_session_export = ppc_hook("ge_session_export", SessionExport);
		orig_find_query = ppc_hook("ge_find_query", FindQuery);
	}
	if (!game_alloc) rt_log("goldeneye: the game's allocator isn't in this build, no Level Select and no Mods menu");
}

} //namespace ge
