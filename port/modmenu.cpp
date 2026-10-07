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
//the unused extras menu's items are in the same rows when that cheat is on. it used to be two
//writes to where the array was on the heap, which is somewhere else when mods add files
//
//the maps of multiplayer's Conflict are a page of the same kind (7D000045, rows 805830C8: six
//of dressing with one list widget, then a kind 0xB row for each of its ten maps). it can get
//rows made here, a map for every line of online.txt in the mods folder, as many as there
//are: a mod's file name (mt_yard.edb, or just mt_yard) or a file's hash in hex (01A00003, one
//of the disc's). the code this replaces wrote other hashes into the game's ten rows
//which of the two it has is picked in the Multiplayer menu (7D000061, rows 8057E5A0: Split
//Screen and Nintendo Wi-Fi Connection, which goes to page 7D000049). with an online.txt that
//page gets rows made here too: Split Screen, "Connect to Nintendo WFC" and "Connect to WFC
//Modded", both to 7D000049. the line under the list that says what the lit item is gets
//looked up as the item is lit, and each of the two has a text of its own for it: the one
//asked for last says which maps Conflict has
//
//a console doesn't have those maps and stops on one, and so does someone with other files in
//their mods folder. a network game has a version, 0x4F: the host's session is advertised with
//it and a search asks the server for it (the queries are called ..._VERSIONPROTOCOL...). after
//Connect to WFC Modded both get a number made here from the maps of online.txt and the mods'
//bytes, so only those with the same maps find each other
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
const uint32_t kConflictMapsPage = 0x7D000045;
const uint32_t kConflictRows = 0x805830C8, kConflictRowCount = 16; //the game's
const uint32_t kConflictHead = 6; //the rows before the maps
const uint32_t kTextMap = 0x4300F100; //a text for each map of the pool that isn't one of the game's ten
const uint32_t kMultiplayerPage = 0x7D000061;
const uint32_t kMultiplayerRows = 0x8057E5A0, kMultiplayerRowCount = 11; //the game's
const uint32_t kSplitScreenRow = 6, kSplitScreenAboutRow = 9;
const uint32_t kTextMultiplayer = 0x43000C88; //the page's title, the game's
const uint32_t kOnlinePage = 0x7D000049; //Nintendo Wi-Fi Connection
const uint32_t kTextWfc = 0x4300F0F0, kTextWfcModded = 0x4300F0F1, kTextAboutWfc = 0x4300F0F2, kTextAboutModded = 0x4300F0F3;
const uint32_t kAlloc = 0x803428C0; //(bytes, flags) -> a block of the game's heap
const uint32_t kMostPages = 4096;

PPCFunc game_alloc = nullptr;
std::vector<ModLevel> g_levels;
std::map<uint32_t, uint32_t> g_texts; //a text's hash, its characters in the game's memory
uint32_t g_mods_rows = 0, g_mods_row_count = 0;
uint32_t g_extras_rows[2] = {}, g_extras_row_count[2] = {}; //without and with the unused items
bool g_failed = false;

struct PoolMap { uint32_t hash; std::string name; std::string file; }; //the file's name for a mod, none for one of the disc's
std::vector<PoolMap> g_pool; //online.txt
std::atomic<bool> g_map_pool{false}; //Connect to WFC Modded was the item lit last
uint32_t g_pool_rows = 0, g_pool_row_count = 0;
uint32_t g_multiplayer_rows = 0, g_multiplayer_row_count = 0;
bool g_pool_failed = false;
uint32_t g_pool_version = 0; //a network game's version with those maps
bool g_trace = false;
uint32_t g_mods_target = kModsPage; //GE_MODS_PAGE

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

//the rows and the texts, made once
bool Build(const PPCContext& c)
{
	g_texts[kTextMods] = Text(c, "Mods");
	g_texts[kTextAbout] = Text(c, "Play the levels in the mods folder.");
	for (size_t i = 0; i < g_levels.size(); ++i) g_texts[kTextLevel + (uint32_t)i] = Text(c, Shown(g_levels[i].name));
	for (const auto& text : g_texts)
		if (!text.second) return false;

	//a level select like Conflict's: its rows before the maps, then a level a row
	const uint32_t levels = (uint32_t)g_levels.size();
	g_mods_row_count = kConflictHead + levels;
	g_mods_rows = Alloc(c, g_mods_row_count * kRowSize);
	if (!g_mods_rows) return false;
	CopyRows(g_mods_rows, kConflictRows, kConflictHead);
	st32(g_mods_rows + kTitleRow * kRowSize, kTextMods);
	for (uint32_t i = 0; i < levels; ++i) Row(g_mods_rows + (kConflictHead + i) * kRowSize, kTextLevel + i, g_levels[i].hash, kMissionStart, kKindLevel);
	uint32_t at = 0;

	for (int unused = 0; unused < 2; ++unused)
	{
		const uint32_t items = (unused ? kUnusedItemCount : kGameItemCount) + 1;
		at = ListRows(c, items, ld32(kUnusedRows + kTitleRow * kRowSize), g_extras_rows[unused], g_extras_row_count[unused]);
		if (!at) return false;
		CopyRows(at, kUnusedRows + kUnusedItems * kRowSize, items - 1);
		Row(at + (items - 1) * kRowSize, kTextMods, g_mods_target, 0, kKindPage);
		const uint32_t about = at + (items + 1) * kRowSize;
		CopyRows(about, kUnusedRows + (kUnusedFoot + 1) * kRowSize, items - 1);
		Row(about + (items - 1) * kRowSize, kTextAbout, 0, 0, kKindAbout);
	}
	return true;
}

std::string Lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
	return text;
}

//FNV-1a
const uint32_t kFnvStart = 0x811C9DC5, kFnvPrime = 0x01000193;
void Mix(uint32_t& sum, const uint8_t* bytes, size_t count)
{
	for (size_t i = 0; i < count; ++i) sum = (sum ^ bytes[i]) * kFnvPrime;
}

//a number for the pool: each map's hash and, for a mod, every byte of its file, in the order
//of the hashes (the order of the lines doesn't count). the hash a mod has here goes in too, two
//with the same one get theirs by what else is in the folder
const uint32_t kGameVersion = 0x4F;
const uint32_t kModdedVersions = 0x40000000; //where the numbers made here are, the game's own never is
uint32_t PoolVersion()
{
	std::vector<const PoolMap*> maps;
	for (const PoolMap& map : g_pool) maps.push_back(&map);
	std::sort(maps.begin(), maps.end(), [](const PoolMap* a, const PoolMap* b) { return a->hash < b->hash; });
	uint32_t sum = kFnvStart;
	std::vector<uint8_t> bytes(1 << 16);
	for (const PoolMap* map : maps)
	{
		const uint8_t hash[4] = { uint8_t(map->hash >> 24), uint8_t(map->hash >> 16), uint8_t(map->hash >> 8), uint8_t(map->hash) };
		Mix(sum, hash, sizeof hash);
		if (map->file.empty()) continue;
		//the mod's name is in lower case, the file's may not be
		std::error_code error;
		std::filesystem::path found;
		for (const auto& entry : std::filesystem::directory_iterator(mods_folder(), error))
			if (Lower(entry.path().filename().string()) == map->file) { found = entry.path(); break; }
		std::ifstream file(found, std::ios::binary);
		if (!file) { rt_log("goldeneye: %s couldn't be read for the network game's version", map->file.c_str()); continue; }
		while (file.read((char*)bytes.data(), (std::streamsize)bytes.size()) || file.gcount() > 0)
			Mix(sum, bytes.data(), (size_t)file.gcount());
	}
	return kModdedVersions | (sum & (kModdedVersions - 1));
}

//online.txt: a map a line, anything behind a # is a note
void ReadPool()
{
	if (mods_folder().empty()) return;
	const std::string path = mods_folder() + "/online.txt";
	std::ifstream file(path);
	if (!file) return;
	std::string line;
	while (std::getline(file, line))
	{
		line = line.substr(0, line.find('#'));
		const size_t first = line.find_first_not_of(" \t\r"), last = line.find_last_not_of(" \t\r");
		if (first == std::string::npos) continue;
		line = line.substr(first, last - first + 1);
		const std::string wanted = Lower(line);
		const auto& files = mod_files();
		auto mod = std::find_if(files.begin(), files.end(), [&](const ModLevel& m) { return m.name == wanted || m.name == wanted + ".edb"; });
		if (mod != files.end()) { g_pool.push_back({ mod->hash, Shown(mod->name), mod->name }); continue; }
		char* end = nullptr;
		const unsigned long hash = std::strtoul(line.c_str(), &end, 16);
		if (end != line.c_str() && *end == 0 && line.size() >= 7 && line.size() <= 10) { g_pool.push_back({ (uint32_t)hash, line, {} }); continue; }
		rt_log("goldeneye: %s: \"%s\" is neither a mod's file nor a file's hash, left out", path.c_str(), line.c_str());
	}
	rt_log("goldeneye: %zu map(s) for Conflict in %s", g_pool.size(), path.c_str());
	if (!g_pool.empty())
	{
		g_pool_version = PoolVersion();
		rt_log("goldeneye: a network game with them is version %08X (the game's own: %X)", g_pool_version, kGameVersion);
	}
}

//the Multiplayer menu with the two ways online, when there is a pool to pick
void MultiplayerFrame(const PPCContext& c, uint32_t data, uint32_t count)
{
	const uint32_t page = FindPage(data, count, kMultiplayerPage);
	if (!page || g_pool.empty() || g_pool_failed || !game_alloc) return;
	if (!g_multiplayer_rows)
	{
		g_texts[kTextWfc] = Text(c, "Connect to Nintendo WFC");
		g_texts[kTextWfcModded] = Text(c, "Connect to WFC Modded");
		g_texts[kTextAboutWfc] = Text(c, "Connect to Nintendo WFC.");
		g_texts[kTextAboutModded] = Text(c, "Connect to Nintendo WFC with the maps of the mods folder.");
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

//Conflict's page gets the pool's rows after Connect to WFC Modded, the game's own otherwise
void PoolFrame(const PPCContext& c, uint32_t data, uint32_t count)
{
	const uint32_t page = FindPage(data, count, kConflictMapsPage);
	if (!page) return;
	const bool on = g_map_pool && !g_pool.empty() && !g_pool_failed && game_alloc;
	if (on && !g_pool_rows)
	{
		g_pool_row_count = kConflictHead + (uint32_t)g_pool.size();
		const uint32_t rows = Alloc(c, g_pool_row_count * kRowSize);
		if (!rows) { g_pool_failed = true; return; }
		CopyRows(rows, kConflictRows, kConflictHead);
		for (size_t i = 0; i < g_pool.size(); ++i)
		{
			//one of the game's ten keeps the game's name for it
			uint32_t text = 0;
			for (uint32_t k = kConflictHead; k < kConflictRowCount && !text; ++k)
				if (ld32(kConflictRows + k * kRowSize + kRowHash) == g_pool[i].hash) text = ld32(kConflictRows + k * kRowSize);
			if (!text)
			{
				text = kTextMap + (uint32_t)i;
				g_texts[text] = Text(c, g_pool[i].name);
				if (!g_texts[text]) { g_pool_failed = true; return; }
			}
			Row(rows + (kConflictHead + (uint32_t)i) * kRowSize, text, g_pool[i].hash, kMissionStart, kKindLevel);
		}
		g_pool_rows = rows;
		rt_log("goldeneye: Conflict's maps are the %zu of online.txt", g_pool.size());
	}
	const uint32_t rows = on ? g_pool_rows : kConflictRows, have = ld32(page + kPageRows);
	if (have == rows || (have != kConflictRows && have != g_pool_rows)) return;
	st32(page + kPageRows, rows);
	st32(page + kPageRowCount, on ? g_pool_row_count : kConflictRowCount);
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

bool Modded() { return g_map_pool && !g_pool.empty() && !g_pool_failed; }

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
	const uint32_t count = ld32(kPages + kPagesCount);
	uint32_t data = ld32(kPages + kPagesData);
	if (!guest_ptr(data) || count == 0 || count > kMostPages) return;
	MultiplayerFrame(c, data, count);
	PoolFrame(c, data, count);
	const uint32_t extras = FindPage(data, count, kExtrasPage);
	if (!extras) return;

	if (g_levels.empty() || g_failed)
	{
		//no mods: the game's rows, or the code's
		const bool unused = extras_menu();
		const uint32_t rows = unused ? kUnusedRows : kExtrasRows;
		const uint32_t have = ld32(extras + kPageRows);
		if (have == rows || (have != kExtrasRows && have != kUnusedRows)) return;
		st32(extras + kPageRows, rows);
		st32(extras + kPageRowCount, unused ? kUnusedRowCount : kExtrasRowCount);
		return;
	}

	if (!g_mods_rows && (!game_alloc || !Build(c)))
	{
		g_failed = true;
		rt_log("goldeneye: the Mods menu couldn't be made");
		return;
	}

	if (!FindPage(data, count, kModsPage))
	{
		//one more page: the array in a new block, the game's old one is left as it is
		const uint32_t fresh = Alloc(c, (count + 1) * kPageSize);
		if (!fresh) return;
		for (uint32_t i = 0; i < count * kPageSize; i += 4) st32(fresh + i, ld32(data + i));
		const uint32_t page = fresh + count * kPageSize;
		st32(page, kModsPage);
		st32(page + kPageRows, g_mods_rows);
		st32(page + kPageRowCount, g_mods_row_count);
		st32(page + kPageClass, kLevelSelectClass);
		st32(kPages + kPagesData, fresh);
		st32(kPages + kPagesCount, count + 1);
		st32(kPages + kPagesRoom, count + 1);
		data = fresh;
		rt_log("goldeneye: Extras has Mods: %zu level(s)", g_levels.size());
	}

	const int unused = extras_menu() ? 1 : 0;
	const uint32_t entry = FindPage(data, count + 1, kExtrasPage);
	if (entry && ld32(entry + kPageRows) != g_extras_rows[unused])
	{
		st32(entry + kPageRows, g_extras_rows[unused]);
		st32(entry + kPageRowCount, g_extras_row_count[unused]);
	}
}

void install_mods_menu()
{
	g_levels = mod_levels();
	game_alloc = ppc_lookup(kAlloc);
	g_trace = std::getenv("GE_TRACE") != nullptr;
	if (const char* e = std::getenv("GE_MODS_PAGE")) g_mods_target = (uint32_t)std::strtoul(e, nullptr, 16);
	ReadPool();
	random_map = ppc_lookup(kRandomMap);
	if (random_map) ppc_hook("ge_pick_next_maps", PickNextMaps);
	if (!g_pool.empty())
	{
		orig_session_export = ppc_hook("ge_session_export", SessionExport);
		orig_find_query = ppc_hook("ge_find_query", FindQuery);
	}
	if (!g_levels.empty() && !game_alloc) rt_log("goldeneye: the game's allocator isn't in this build, no Mods menu");
}

} //namespace ge
