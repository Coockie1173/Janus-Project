//custom characters: a modpack's characters/NAME.txt is a character of its own, a body made by
//eurochef's `ge skin` on one of the game's (its base). the online Character page lists them after
//the game's (net/character.cpp)
//
//NAME.txt, `key = value` a line, # a comment:
//  name = Agent Smith       what the Character page calls it
//  base = Bond              the game's character it's made from, by name or hash (4D00xxxx): its
//                           arms, its size and the rest of its row
//  body = smith.edb         the body, next to it, made by `ge skin` on the base's body file
//  arms = smith_arms.edb    first person arms of its own, the base's when left out
//  width = 1.0              its size, the base's when left out
//  height = 1.15
//
//the character sheet 805D1598: +8 how many characters, +0xC their rows (0x44: hash, set, name's
//text, its animations file, +0x18 its body file, +0x1C the body's first animskin...: what a preview
//and a guard are drawn from), +0xD0 how many skins, +0xD4 theirs (0x18: hash, arms file, body file, its first
//animskin, width, height: a network game's players). ge_character_sheet_load (800B4300) fills it from spreadsheet 14000021
//of 01090013 whenever the characters were freed (each level's end, fn_800B4260; the skins stay),
//a row at a time through ge_character_insert and ge_skin_insert (sorted, a hash that's there is
//refused, the array may move). after that it makes the sets and split screen's roster out of
//pointers to its first N rows (N the spreadsheet's). ours (4D00F000 up, after all of the game's)
//go in before the game's first row so nothing points at a row that moved, and get their base's
//row once the load is done. a pack's characters are numbered from 4D00F000 by file name, so
//consoles with the same pack (the network game's version has the files) agree
//
//GE_TRACE=1 logs the rows made
#include "ge.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kSheet = 0x805D1598, kCharacterCount = 0x8, kCharacterRows = 0xC, kCharacterSize = 0x44;
const uint32_t kCharacterSet = 0x4, kCharacterName = 0x8, kCharacterBody = 0x18, kNoSet = 0xFFFFFFFF;
const uint32_t kSkinTable = 0xC8, kSkinCount = 0xD0, kSkinRows = 0xD4, kSkinSize = 0x18;
const uint32_t kSkinArms = 0x4, kSkinBody = 0x8, kSkinWidth = 0x10, kSkinHeight = 0x14;
const uint32_t kFirstHash = 0x4D00F000, kFirstText = 0x4300F600; //the game's stop at 4D0001xx and 430017xx
const uint32_t kStandIn = 0x4D0001B9; //Bond: the rows no character of the installed pack has
const uint32_t kAlloc = 0x803428C0;
const size_t kMost = 64; //a pack's
const char* const kFolder = "characters";

struct Character
{
	std::string file, name, base, body, arms;
	float width{ -1.0f }, height{ -1.0f };
	uint32_t hash{}, text{}, chars{}, bodyFile{}, armsFile{};
};

std::vector<std::vector<Character>> g_packs;
std::vector<std::vector<uint8_t>> g_sums;
size_t g_rows = 0; //how many rows of ours there are: the most any pack has
int g_filled = -2; //the pack the rows are filled for
uint32_t g_scratch = 0; //a row's worth of the game's memory to insert from
bool g_trace = false;
PPCFunc orig_sheet_load = nullptr, orig_character_insert = nullptr, orig_skin_insert = nullptr, game_alloc = nullptr;

std::string Trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

std::string Lower(std::string s)
{
	for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
	return s;
}

bool ReadCharacter(const std::filesystem::path& path, Character& ch, std::vector<uint8_t>& sum)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	sum.insert(sum.end(), text.begin(), text.end());
	ch.file = path.filename().string();
	size_t from = 0;
	while (from < text.size())
	{
		size_t end = text.find('\n', from);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(from, end - from);
		from = end + 1;
		if (const size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		const std::string key = Lower(Trim(line.substr(0, eq))), value = Trim(line.substr(eq + 1));
		if (key == "name") ch.name = value;
		else if (key == "base") ch.base = value;
		else if (key == "body") ch.body = value;
		else if (key == "arms") ch.arms = value;
		else if (key == "width") ch.width = std::strtof(value.c_str(), nullptr);
		else if (key == "height") ch.height = std::strtof(value.c_str(), nullptr);
		else rt_log("goldeneye: characters: %s: don't know \"%s\"", ch.file.c_str(), key.c_str());
	}
	if (ch.name.empty() || ch.base.empty() || ch.body.empty())
	{
		rt_log("goldeneye: characters: %s needs a name, a base and a body, left out", ch.file.c_str());
		return false;
	}
	return true;
}

//a file of the pack's characters/ folder: the hash the mods gave it, 0 when it isn't among them
uint32_t PackFile(int pack, const std::string& name)
{
	const std::filesystem::path path = std::filesystem::path(modpack_folder(pack)) / kFolder / name;
	std::error_code error;
	for (const ModLevel& file : modpack_files(pack))
		if (std::filesystem::equivalent(file.path, path, error)) return file.hash;
	return 0;
}

void ReadCharacters()
{
	g_packs.assign((size_t)modpack_count(), {});
	g_sums.assign((size_t)modpack_count(), {});
	for (int pack = 0; pack < modpack_count(); ++pack)
	{
		const std::filesystem::path folder = std::filesystem::path(modpack_folder(pack)) / kFolder;
		std::error_code error;
		if (!std::filesystem::is_directory(folder, error)) continue;
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(folder, error))
			if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".txt") files.push_back(entry.path());
		std::sort(files.begin(), files.end()); //the same numbers on every console with the same files
		for (const auto& path : files)
		{
			if (g_packs[pack].size() == kMost) break;
			Character ch;
			const std::string name = path.filename().string();
			g_sums[pack].insert(g_sums[pack].end(), name.begin(), name.end());
			if (!ReadCharacter(path, ch, g_sums[pack])) continue;
			ch.bodyFile = PackFile(pack, ch.body);
			if (!ch.bodyFile)
			{
				rt_log("goldeneye: characters: %s: %s isn't among the pack's files, left out", ch.file.c_str(), ch.body.c_str());
				continue;
			}
			if (!ch.arms.empty() && !(ch.armsFile = PackFile(pack, ch.arms))) rt_log("goldeneye: characters: %s: %s isn't among the pack's files, the base's arms are used", ch.file.c_str(), ch.arms.c_str());
			const uint32_t n = (uint32_t)g_packs[pack].size();
			ch.hash = kFirstHash + n;
			ch.text = kFirstText + n;
			g_packs[pack].push_back(ch);
		}
		g_rows = std::max(g_rows, g_packs[pack].size());
		if (!g_packs[pack].empty()) rt_log("goldeneye: %zu custom character(s) in %s", g_packs[pack].size(), folder.string().c_str());
	}
}

std::vector<Character>* Installed()
{
	const int pack = modpack_active();
	return pack >= 0 && pack < (int)g_packs.size() ? &g_packs[pack] : nullptr;
}

//a guest function with a frame of its own under the stack
uint32_t Call(const PPCContext& c, PPCFunc fn, uint32_t r3, uint32_t r4 = 0)
{
	PPCContext call = c;
	const uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	call.r[3] = r3;
	call.r[4] = r4;
	fn(call);
	return call.r[3];
}

//a row by its hash in a table sorted by it, 0 when it isn't there
uint32_t Find(uint32_t rows, uint32_t count, uint32_t size, uint32_t hash)
{
	int lo = 0, hi = (int)count - 1;
	if (!guest_ptr(rows) || count > 4096) return 0;
	while (lo <= hi)
	{
		const int mid = (lo + hi) / 2;
		const uint32_t at = ld32(rows + (uint32_t)mid * size);
		if (at == hash) return rows + (uint32_t)mid * size;
		if (at < hash) lo = mid + 1;
		else hi = mid - 1;
	}
	return 0;
}

uint32_t CharacterRow(uint32_t hash) { return Find(ld32(kSheet + kCharacterRows), ld32(kSheet + kCharacterCount), kCharacterSize, hash); }
uint32_t SkinRow(uint32_t hash) { return Find(ld32(kSheet + kSkinRows), ld32(kSheet + kSkinCount), kSkinSize, hash); }

//the base's row: a hash, or the name the game shows
uint32_t BaseRow(const PPCContext& c, const std::string& base)
{
	if (base.size() == 8 && base.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) return CharacterRow((uint32_t)std::strtoul(base.c_str(), nullptr, 16));
	const uint32_t count = ld32(kSheet + kCharacterCount), rows = ld32(kSheet + kCharacterRows);
	for (uint32_t i = 0; guest_ptr(rows) && i < count && i < 4096; ++i)
	{
		const uint32_t row = rows + i * kCharacterSize;
		if (ld32(row) < kFirstHash && Lower(game_text(c, ld32(row + kCharacterName))) == Lower(base)) return row;
	}
	return 0;
}

uint32_t Chars(const PPCContext& c, const std::string& text)
{
	const uint32_t at = Call(c, game_alloc, uint32_t(text.size() + 1) * 2, 0);
	if (!guest_ptr(at)) return 0;
	for (size_t i = 0; i <= text.size(); ++i) st16(at + uint32_t(i) * 2, i < text.size() ? (uint8_t)text[i] : 0);
	return at;
}

void Copy(uint32_t to, uint32_t from, uint32_t size)
{
	for (uint32_t k = 0; k < size; k += 4) st32(to + k, ld32(from + k));
}

//our rows get the installed pack's characters: their base's rows with their own hash, name, files
//and size. the rows no character has are Bond's
void Fill(const PPCContext& c)
{
	std::vector<Character>* pack = Installed();
	const uint32_t standIn = CharacterRow(kStandIn), standInSkin = SkinRow(kStandIn);
	if (!standIn || !standInSkin) return;
	g_filled = modpack_active();
	for (size_t k = 0; k < g_rows; ++k)
	{
		const uint32_t hash = kFirstHash + (uint32_t)k;
		const uint32_t row = CharacterRow(hash), skin = SkinRow(hash);
		if (!row || !skin) continue;
		Character* ch = pack && k < pack->size() ? &(*pack)[k] : nullptr;
		uint32_t base = ch ? BaseRow(c, ch->base) : 0;
		if (ch && !base) rt_log("goldeneye: characters: %s: the game has no character %s, Bond stands in", ch->file.c_str(), ch->base.c_str());
		if (!base) base = standIn;
		uint32_t baseSkin = SkinRow(ld32(base));
		if (!baseSkin) baseSkin = standInSkin;
		Copy(row, base, kCharacterSize);
		st32(row, hash);
		st32(row + kCharacterSet, kNoSet);
		Copy(skin, baseSkin, kSkinSize);
		st32(skin, hash);
		if (!ch) continue;
		if (!ch->chars) ch->chars = Chars(c, ch->name);
		st32(row + kCharacterName, ch->text);
		st32(row + kCharacterBody, ch->bodyFile);
		st32(skin + kSkinBody, ch->bodyFile);
		if (ch->armsFile) st32(skin + kSkinArms, ch->armsFile);
		if (ch->width > 0.0f) stf(skin + kSkinWidth, ch->width);
		if (ch->height > 0.0f) stf(skin + kSkinHeight, ch->height);
		if (g_trace) rt_log("goldeneye: characters: %s is %08X on %08X's row: arms %08X, body %08X, %.2f x %.2f", ch->name.c_str(), hash, ld32(base), ld32(skin + kSkinArms), ld32(skin + kSkinBody), ldf(skin + kSkinWidth), ldf(skin + kSkinHeight));
	}
}

//our rows, a hash each, through the game's own insert (table, row), before what it's asked to insert
void InsertOurs(const PPCContext& c, PPCFunc insert, uint32_t table, uint32_t size)
{
	if (!g_scratch) g_scratch = Call(c, game_alloc, kCharacterSize, 0);
	if (!guest_ptr(g_scratch)) return;
	for (uint32_t k = 0; k < kCharacterSize; k += 4) st32(g_scratch + k, 0);
	st32(g_scratch + kCharacterSet, kNoSet);
	for (size_t k = 0; k < g_rows; ++k)
	{
		const uint32_t hash = kFirstHash + (uint32_t)k;
		if (Find(ld32(table + 0xC), ld32(table + 8), size, hash)) continue;
		st32(g_scratch, hash);
		Call(c, insert, table, g_scratch);
	}
}

//(sheet, a row of the spreadsheet) the first of a load: ours go first
void CharacterInsert(PPCContext& c)
{
	if (c.r[3] == kSheet && ld32(kSheet + kCharacterCount) == 0) InsertOurs(c, orig_character_insert, kSheet, kCharacterSize);
	orig_character_insert(c);
}

//(the sheet's +0xC8, a row) the skins stay from load to load: ours go in once, before the game's
void SkinInsert(PPCContext& c)
{
	if (c.r[3] == kSheet + kSkinTable && !SkinRow(kFirstHash + (uint32_t)g_rows - 1)) InsertOurs(c, orig_skin_insert, kSheet + kSkinTable, kSkinSize);
	orig_skin_insert(c);
}

void SheetLoad(PPCContext& c)
{
	const bool loads = ld32(kSheet + kCharacterCount) == 0;
	orig_sheet_load(c);
	if (loads) Fill(c);
}

} //namespace

int custom_character_count()
{
	const std::vector<Character>* pack = Installed();
	if (!pack || g_filled != modpack_active()) return 0;
	int count = 0;
	for (const Character& ch : *pack)
	{
		if (!CharacterRow(ch.hash) || !modpack_serves(modpack_active(), ch.bodyFile)) break;
		++count;
	}
	return count;
}

uint32_t custom_character_hash(int n) { return kFirstHash + (uint32_t)n; }

uint32_t custom_character_row(uint32_t hash) { return hash >= kFirstHash && hash < kFirstHash + g_rows ? CharacterRow(hash) : 0; }

uint32_t custom_character_text(uint32_t hash)
{
	const std::vector<Character>* pack = Installed();
	if (!pack || hash < kFirstText || hash >= kFirstText + pack->size()) return 0;
	return (*pack)[hash - kFirstText].chars;
}

const std::vector<uint8_t>& custom_characters_sum()
{
	static const std::vector<uint8_t> none;
	const int pack = modpack_active();
	return pack >= 0 && pack < (int)g_sums.size() ? g_sums[pack] : none;
}

//once a frame on the game's thread: another pack installed, our rows get its characters
void custom_characters_frame(PPCContext& c)
{
	if (g_rows && g_filled != -2 && g_filled != modpack_active()) Fill(c);
}

void install_custom_characters()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	ReadCharacters();
	if (!g_rows) return;
	game_alloc = ppc_lookup(kAlloc);
	orig_sheet_load = ppc_hook("ge_character_sheet_load", SheetLoad);
	orig_character_insert = ppc_hook("ge_character_insert", CharacterInsert);
	orig_skin_insert = ppc_hook("ge_skin_insert", SkinInsert);
	if (!game_alloc || !orig_sheet_load || !orig_character_insert || !orig_skin_insert) rt_die("goldeneye: custom characters are not hooked in this build");
}

} //namespace ge
