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
//items. 7D000008 is the debug Mission Select nothing leads to without that code: eight
//slots and a level a row, which is what the Mods page is made like
//
//the Mods page is one more entry in the array (a new block from the game's own allocator, so
//the game can grow or free it as it would its own) and Extras gets rows made here: the six
//slots, its items and "Mods". the texts are answered from here (prompts.cpp asks for every
//text the game looks up)
//the unused extras menu's items are in the same rows when that cheat is on. it used to be two
//writes to where the array was on the heap, which is somewhere else when mods add files
#include "ge.h"
#include <algorithm>
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
const uint32_t kExtrasPage = 0x7D0000D2, kDebugMissionsPage = 0x7D000008;
const uint32_t kModsPage = 0x7D000DFF; //the game's own stop at 7D00010A
const uint32_t kExtrasRows = 0x805808B4, kExtrasRowCount = 11; //the game's
const uint32_t kUnusedRows = 0x80580990, kUnusedRowCount = 0x19; //the code's, as it set them
const uint32_t kUnusedHead = 10; //the layout, the title and six slots
const uint32_t kUnusedItems = 10, kUnusedItemCount = 6; //Credits, Cheat Codes, then the four unused ones
const uint32_t kGameItemCount = 2;
const uint32_t kUnusedFoot = 16; //the footer's row, then a row a item: the line that says what the item is
const uint32_t kKindAbout = 0x00AB0000;
const uint32_t kDebugHead = 13; //the debug Mission Select's rows before its levels: layout, title, eight slots
const uint32_t kDebugTitleRow = 3;
const uint32_t kKindPage = 0x00010000, kKindLevel = 0x000B0000;
const uint32_t kMissionStart = 1; //the entrance a level begins at
const uint32_t kTextMods = 0x4300F0FF, kTextAbout = 0x4300F0FE, kTextLevel = 0x4300F000; //the game's own texts stop at 430017xx
const uint32_t kAlloc = 0x803428C0; //(bytes, flags) -> a block of the game's heap
const uint32_t kMostPages = 4096;

PPCFunc game_alloc = nullptr;
std::vector<ModLevel> g_levels;
std::map<uint32_t, uint32_t> g_texts; //a text's hash, its characters in the game's memory
uint32_t g_mods_rows = 0, g_mods_row_count = 0;
uint32_t g_extras_rows[2] = {}, g_extras_row_count[2] = {}; //without and with the unused items
bool g_failed = false;

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

//the rows and the texts, made once
bool Build(const PPCContext& c, uint32_t debugRows)
{
	g_texts[kTextMods] = Text(c, "Mods");
	g_texts[kTextAbout] = Text(c, "Play the levels in the mods folder.");
	for (size_t i = 0; i < g_levels.size(); ++i) g_texts[kTextLevel + (uint32_t)i] = Text(c, Shown(g_levels[i].name));
	for (const auto& text : g_texts)
		if (!text.second) return false;

	g_mods_row_count = kDebugHead + (uint32_t)g_levels.size();
	g_mods_rows = Alloc(c, g_mods_row_count * kRowSize);
	if (!g_mods_rows) return false;
	CopyRows(g_mods_rows, debugRows, kDebugHead);
	st32(g_mods_rows + kDebugTitleRow * kRowSize, kTextMods);
	for (size_t i = 0; i < g_levels.size(); ++i)
		Row(g_mods_rows + (kDebugHead + (uint32_t)i) * kRowSize, kTextLevel + (uint32_t)i, g_levels[i].hash, kMissionStart, kKindLevel);

	for (int unused = 0; unused < 2; ++unused)
	{
		const uint32_t items = unused ? kUnusedItemCount : kGameItemCount;
		g_extras_row_count[unused] = kUnusedHead + (items + 1) * 2 + 1;
		uint32_t rows = g_extras_rows[unused] = Alloc(c, g_extras_row_count[unused] * kRowSize);
		if (!rows) return false;
		CopyRows(rows, kUnusedRows, kUnusedHead);
		CopyRows(rows + kUnusedHead * kRowSize, kUnusedRows + kUnusedItems * kRowSize, items);
		Row(rows + (kUnusedHead + items) * kRowSize, kTextMods, kModsPage, 0, kKindPage);
		const uint32_t foot = rows + (kUnusedHead + items + 1) * kRowSize;
		CopyRows(foot, kUnusedRows + kUnusedFoot * kRowSize, 1 + items);
		Row(foot + (1 + items) * kRowSize, kTextAbout, 0, 0, kKindAbout);
	}
	return true;
}

} //namespace

uint32_t mods_menu_text(uint32_t hash)
{
	auto found = g_texts.find(hash);
	return found == g_texts.end() ? 0 : found->second;
}

//once a frame, on the game's thread (the limiter's hook)
void mods_menu_frame(PPCContext& c)
{
	const uint32_t count = ld32(kPages + kPagesCount);
	uint32_t data = ld32(kPages + kPagesData);
	if (!guest_ptr(data) || count == 0 || count > kMostPages) return;
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

	if (!g_mods_rows)
	{
		const uint32_t debug = FindPage(data, count, kDebugMissionsPage);
		if (!debug || ld32(debug + kPageRowCount) < kDebugHead || !game_alloc || !Build(c, ld32(debug + kPageRows)))
		{
			g_failed = true;
			rt_log("goldeneye: the Mods menu couldn't be made");
			return;
		}
	}

	if (!FindPage(data, count, kModsPage))
	{
		//one more page: the array in a new block, the game's old one is left as it is
		const uint32_t debug = FindPage(data, count, kDebugMissionsPage);
		const uint32_t fresh = Alloc(c, (count + 1) * kPageSize);
		if (!fresh || !debug) return;
		for (uint32_t i = 0; i < count * kPageSize; i += 4) st32(fresh + i, ld32(data + i));
		const uint32_t page = fresh + count * kPageSize;
		st32(page, kModsPage);
		st32(page + kPageRows, g_mods_rows);
		st32(page + kPageRowCount, g_mods_row_count);
		st32(page + kPageClass, ld32(debug + kPageClass));
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
	if (!g_levels.empty() && !game_alloc) rt_log("goldeneye: the game's allocator isn't in this build, no Mods menu");
}

} //namespace ge
