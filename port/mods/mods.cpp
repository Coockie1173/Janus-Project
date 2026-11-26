//mods: loose .edb files take the place of the disc's, or come on top of them
//
//they come in modpacks: a folder or a zip each in modpacks/ (modpacks/default, modpacks/NAME.zip), one of them
//installed at a time. Extras > Mods and Connect to WFC Modded first ask which (modmenu.cpp). every
//hash of every pack has a place behind the end of Filelist.000 with room for each pack's file of it
//and a row in the list from the start; a pack is installed by putting its files in those places
//(disc_rewrite) and its rows into the game's copy of the list. a hash the pack hasn't got is the
//disc's file again, or, when the disc has none, stays as it was (nothing of the pack leads to it)
//the game reads Filelist.bin once (the file system 806EC2D0: +8 how many lists, +0xC the lists;
//a list +0 its path, +0x24 how many files, +0x28 the files, 0x40 each by hash: +4 the size that
//stays loaded, +8 where it starts and which Filelist.00N, four times, +0x28 the file object once
//it has been asked for, +0x30 the hash, +0x34 the flags with the top bit the game's, +0x38 the
//version, +0x3C how many places). a file object keeps a copy of the places (its +0xC: +4 each 0xC
//bytes) and the size (+0x30)
//
//the game finds a file by its hash in Filelist.bin (version 7): a row for each file with the size
//of the part that stays loaded (the .edb header's +0x18), its hash (+4), version (+8), flags (+0xC)
//and where it starts in Filelist.000, then the names, scrambled by row and place
//a mod goes behind the end of Filelist.000 and gets the row of the file with its hash, or a new
//row at the end. two mods with the same hash: the second gets another one. a level is its mt_ file's hash: GE_LEVEL=hash starts a new one
//
//the levels among them can be picked in the game's menu: Extras > Mods (port/mods/modmenu.cpp)
//a folder in it for each online gamemode (kModeFolders): the levels in conflict/ are the maps
//Conflict has after Connect to WFC Modded, and so on. a file that is in more than one of them
//is the same mod once. what is in the mods folder itself is only in Extras > Mods
//a map for several gamemodes is made as the game's own are (eurochef's ge new-map): its
//geometry once, mg_yard.edb in the mods folder, and a small mt_yard.edb in each gamemode's
//folder with that gamemode's triggers, one of which loads the geometry
//a .png with a level's name is its picture in the menus (port/mods/pictures.cpp)
//a .txt with a level's name says who the players of a network game are in it (LevelSet)
//weapons/ has the custom weapons: their .txt files (port/mods/weapons/weapons.cpp) and the guns they are drawn with, mods too
//characters/ the custom characters: their .txt files (port/mods/characters.cpp) and their bodies, mods too
//
//GE_MODPACKS=DIR the packs' folder (modpacks in the game's folder by default). a mods folder of an
//older build, when there is no modpacks folder, is the pack "default"
//GE_MODS=DIR that folder is the only pack
//GE_MODPACK=NAME the pack installed at the start ("default", else the first)
//GE_TRACEFILES=1 log every file the game gives memory to, with its size
#include "ge.h"
#include "disc.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kListVersion = 7;
const uint32_t kNameKey = 0x16; //a name's byte is its letter - 0x16 + its row + its place
const uint64_t kSector = 0x800; //files start on one in Filelist.000
const char* kNamePrefix = "x:\\bondx\\binary\\_bin_wii\\";
const uint32_t kGeom = 0x47454F4D, kHeadSize = 8; //an .edb starts with GEOM and its hash
const uint32_t kFirstFree = 0x0200; //the low half of the hashes eurochef gives the files it makes, from here up
const uint32_t kLevelFiles = 0x01810000; //a level's own file (mt_), the hash a level is started by
const uint32_t kMultiplayerLevelFiles = 0x01A00000; //a multiplayer map's, one for each gamemode it has (mpt_)

struct Row
{
	uint32_t size{}, hash{}, version{}, flags{};
	std::vector<std::pair<uint32_t, uint32_t>> places; //address, which Filelist.00N
	std::string name;
};

struct Mod
{
	std::string path, name;
	uint32_t hash{}, version{}, flags{}, size{}, loaded{};
	uint32_t ownHash{}; //the hash in the file itself, when it had to get another one
	uint32_t modes{}; //the gamemode folders it is in, a bit each
};

uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
void Put32(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 8)); out.push_back(uint8_t(v));
}

bool ReadList(const std::vector<uint8_t>& bin, std::vector<Row>& rows, uint16_t& buildType, uint16_t& lists)
{
	if (bin.size() < 0x14 || Be32(&bin[0]) != kListVersion) return false;
	const uint32_t count = Be32(&bin[8]);
	buildType = uint16_t(bin[12] << 8 | bin[13]);
	lists = uint16_t(bin[14] << 8 | bin[15]);
	const size_t names = 0x10 + Be32(&bin[0x10]);
	size_t at = 0x14;
	for (uint32_t i = 0; i < count; ++i)
	{
		if (at + 20 > bin.size()) return false;
		Row row{ Be32(&bin[at]), Be32(&bin[at + 4]), Be32(&bin[at + 8]), Be32(&bin[at + 12]) };
		const uint32_t places = Be32(&bin[at + 16]);
		at += 20;
		if (at + places * 8ull > bin.size()) return false;
		for (uint32_t p = 0; p < places; ++p, at += 8) row.places.push_back({ Be32(&bin[at]), Be32(&bin[at + 4]) });
		const size_t entry = names + i * 4ull;
		if (entry + 4 > bin.size()) return false;
		for (size_t c = entry + Be32(&bin[entry]), n = 0; c < bin.size(); ++c, ++n)
		{
			const uint8_t letter = uint8_t(bin[c] + kNameKey - i - n);
			if (!letter) break;
			row.name.push_back(char(letter));
		}
		rows.push_back(row);
	}
	return true;
}

std::shared_ptr<std::vector<uint8_t>> WriteList(const std::vector<Row>& rows, uint16_t buildType, uint16_t lists)
{
	auto out = std::make_shared<std::vector<uint8_t>>();
	Put32(*out, kListVersion); Put32(*out, 0); Put32(*out, uint32_t(rows.size()));
	out->push_back(uint8_t(buildType >> 8)); out->push_back(uint8_t(buildType));
	out->push_back(uint8_t(lists >> 8)); out->push_back(uint8_t(lists));
	Put32(*out, 0);
	for (const Row& row : rows)
	{
		Put32(*out, row.size); Put32(*out, row.hash); Put32(*out, row.version); Put32(*out, row.flags);
		Put32(*out, uint32_t(row.places.size()));
		for (const auto& place : row.places) { Put32(*out, place.first); Put32(*out, place.second); }
	}
	const size_t names = out->size();
	size_t text = names + rows.size() * 4;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		Put32(*out, uint32_t(text - (names + i * 4)));
		text += rows[i].name.size() + 1;
	}
	for (size_t i = 0; i < rows.size(); ++i)
		for (size_t n = 0; n <= rows[i].name.size(); ++n) //the 0 at the end is scrambled too
			out->push_back(uint8_t((n < rows[i].name.size() ? rows[i].name[n] : 0) - kNameKey + i + n));
	while (out->size() % 32) out->push_back(0);
	const uint32_t size = uint32_t(out->size()), namesAt = uint32_t(names - 0x10);
	for (int b = 0; b < 4; ++b)
	{
		(*out)[4 + b] = uint8_t(size >> (24 - b * 8));
		(*out)[0x10 + b] = uint8_t(namesAt >> (24 - b * 8));
	}
	return out;
}

bool ReadMod(const std::filesystem::path& path, Mod& mod)
{
	FILE* f = std::fopen(path.string().c_str(), "rb");
	if (!f) return false;
	uint8_t head[0x1C];
	const bool ok = std::fread(head, 1, sizeof head, f) == sizeof head && Be32(head) == kGeom;
	std::fclose(f);
	if (!ok) return false;
	mod.path = path.string();
	mod.name = path.filename().string();
	std::transform(mod.name.begin(), mod.name.end(), mod.name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	mod.hash = Be32(head + 4); mod.version = Be32(head + 8); mod.flags = Be32(head + 12);
	mod.loaded = Be32(head + 0x18);
	mod.size = uint32_t(std::filesystem::file_size(path));
	return true;
}

//a file that gets no memory is never loaded and the game says nothing: the level then has no
//floor. ge_geofile_onload (802EBC10): the file's info at +0xC with the size at +0x30
PPCFunc orig_onload = nullptr;
const uint32_t kFileInfo = 0xC, kFileName = 0x10, kFileLoaded = 0x14, kFileHash = 0x20, kInfoSize = 0x30, kInfoMemory = 0x3C;

void OnLoad(PPCContext& c)
{
	const uint32_t file = c.r[3];
	const bool pending = guest_ptr(file) && ld32(file + kFileLoaded) == 0;
	const uint32_t info = guest_ptr(file) ? ld32(file + kFileInfo) : 0;
	const uint32_t size = pending && guest_ptr(info) ? ld32(info + kInfoSize) : 0;
	const uint32_t name = guest_ptr(file) ? ld32(file + kFileName) : 0;
	orig_onload(c);
	if (guest_ptr(file)) pictures_file(ld32(file + kFileHash), file);
	static const bool trace = std::getenv("GE_TRACEFILES") != nullptr;
	if (trace) rt_log("goldeneye: file %s: %u bytes, loaded %u, memory %08X, returns %u", guest_ptr(name) ? (const char*)PPC_MEM(name) : "?", guest_ptr(info) ? ld32(info + kInfoSize) : 0, guest_ptr(file) ? ld32(file + kFileLoaded) : 0, guest_ptr(info) ? ld32(info + kInfoMemory) : 0, c.r[3]);
	if (pending && size && c.r[3] == 0)
		rt_log("goldeneye: no memory for the file %s (%u bytes), it isn't loaded", guest_ptr(name) ? (const char*)PPC_MEM(name) : "?", size);
}

std::vector<ModLevel> g_disc_levels;
std::unordered_map<uint32_t, std::string> g_file_names; //the disc's files by hash, without their folders
std::unordered_map<uint32_t, uint32_t> g_file_places; //the disc's files by hash, where each starts in Filelist.000

struct Pack
{
	std::string name, folder;
	std::vector<Mod> mods;
	std::vector<ModLevel> levels, files; //what got a place
};
std::vector<Pack> g_packs;
int g_active = -1;
const char* const kDefaultPack = "default";
const char* const kUnzipped = ".unzipped"; //where the zips' packs are unpacked, in the packs' folder
const std::vector<ModLevel> kNone;
const std::string kNoFolder;

//a hash of any pack: its place in Filelist.000 and each pack's file of it
struct Slot
{
	uint32_t hash{};
	uint64_t at{}, room{};
	bool onDisc{};
	Row disc; //the disc's row of the hash
	std::vector<int> mods; //by pack, its mod of the hash, -1 for none
	int shown = -2; //the pack whose file is there now, -1 the disc's
};
std::vector<Slot> g_slots;

//a folder's name as it is looked for: lower case, a space or a dash is an underscore
std::string FolderName(std::string name)
{
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return c == ' ' || c == '-' ? '_' : char(std::tolower(c)); });
	return name;
}

bool SameBytes(const std::string& a, const std::string& b)
{
	std::ifstream first(a, std::ios::binary), second(b, std::ios::binary);
	if (!first || !second) return false;
	return std::equal(std::istreambuf_iterator<char>(first), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(second), std::istreambuf_iterator<char>());
}

//every .edb of a folder. one that is there already (the same name and bytes, in another
//gamemode's folder) is the same mod and only gets that gamemode too
void ReadFolder(const std::filesystem::path& dir, uint32_t modes, std::vector<Mod>& mods)
{
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(dir, error))
	{
		if (entry.path().extension() != ".edb" || entry.is_directory(error)) continue;
		Mod mod;
		if (!ReadMod(entry.path(), mod)) { rt_log("goldeneye: mods: %s isn't an .edb of this game, left out", entry.path().string().c_str()); continue; }
		mod.modes = modes;
		auto same = std::find_if(mods.begin(), mods.end(), [&](const Mod& m) { return m.name == mod.name && m.hash == mod.hash && m.size == mod.size && SameBytes(m.path, mod.path); });
		if (same != mods.end()) same->modes |= modes;
		else mods.push_back(mod);
	}
}

//a level of the mods folder isn't in the game's level table. a single player game does without
//the row, a multiplayer game reads the players' characters out of it without asking (fn_8000F8E0,
//a crash at 000000A4). there the row of one of the game's own multiplayer maps stands in
//
//the row also says who the players of a network game are: a set of four characters for each
//team (+0xA4, +0xAC: Facility has Red-one's and Pvt. Gurov's) and each team's hero for Heroes
//(+0xA8, +0xB0: Bond and Ourumov). fn_8000F8E0 looks each up in the character spreadsheet
//(805D1598: +8 how many, +0xC the rows, 0x44 bytes each: the character's hash, its set, its
//name's text) and takes a set's characters in turn. in split screen everyone picks their own
//a mod level can name them in mt_yard.txt (next to it, or in the mods folder for all of the
//map's gamemodes): a line each of team0, team1, hero0, hero1 with a character's name as the game
//shows it (a team: the set that character is in) or a hash. then it gets a copy of the row
//with those in it
//
//the teams' names aren't the row's. the first team is MI6 (text 43001487) and the other goes by
//a number, 0 Russians (43001488), 1 Janus (43001486), 2 Zukovsky (43001489), which
//ge_team_mode_reset gets from the level's hash (fn_8026F640: Archives 0, Railyard 1,
//Zukovsky's 2, any other level 0) into the gamemode's +0xF7C and 805E8D40. the hud, the score
//table and the lobby pick the text by it, and the other team's shouts go by it too. a mod level
//gets the number of the map that has its second team's set (Two-Two's and Five-One's are
//Janus, Vincent's Zukovsky), and `name0` and `name1` in the .txt are texts of its own, which
//ge_text_find hands out in place of those four
PPCFunc orig_level_set = nullptr, is_multiplayer = nullptr, level_alloc = nullptr, orig_team_mode_reset = nullptr, orig_text_find = nullptr;
const uint32_t kCurrentLevel = 0x805D1804, kTeamKind = 0x805E8D40, kModeTeamKind = 0xF7C;
const uint32_t kTextJanus = 0x43001486, kTextMi6 = 0x43001487, kTextZukovsky = 0x43001489;
const uint32_t kTextHead = 0x10, kTextChars = 0x4, kTextEnd = 0x8, kTextWord = 0xC, kNameLength = 31;
const uint32_t kKindSets[][2] = { { 0x4D000032, 1 }, { 0x4D000033, 1 }, { 0x4D00002F, 2 } };
uint32_t g_name_text[4] = {}; //a text row and its characters for each team's name and a level's two, the game's memory
bool g_named[2] = {}; //the loaded mod level names that team itself
uint32_t g_kind_level = 0, g_kind = 0; //the mod level a multiplayer game is in, and its second team's number
const char* const kCastKeys[4] = { "team0", "team1", "hero0", "hero1" };
const uint32_t kRowCast[4] = { 0xA4, 0xAC, 0xA8, 0xB0 };
const uint32_t kCharacters = 0x805D1598, kCharacterCount = 0x8, kCharacterRows = 0xC, kCharacterSize = 0x44, kCharacterSet = 0x4, kCharacterName = 0x8;
const uint32_t kNoSet = 0xFFFFFFFF, kGameAlloc = 0x803428C0;
uint32_t g_cast_row = 0; //the copy, the game's memory
const uint32_t kLevelRow = 0x1C, kLevelRowCount = 0x10, kLevelRows = 0x14, kLevelRowSize = 0xB4;
const uint32_t kLevelRow2 = 0x34, kLevelRow2Count = 0x28, kLevelRows2 = 0x2C, kLevelRow2Size = 0x74;
const uint32_t kStandIn = 0x01A00003; //Archives, Conflict
const uint32_t kIsMultiplayer = 0x800C0B20;

uint32_t RowOf(uint32_t rows, uint32_t count, uint32_t size, uint32_t hash)
{
	if (!guest_ptr(rows) || count > 4096) return 0;
	for (uint32_t i = 0; i < count; ++i)
		if (ld32(rows + i * size) == hash) return rows + i * size;
	return 0;
}

std::string Lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
	return text;
}

std::string Trimmed(const std::string& text)
{
	const size_t from = text.find_first_not_of(" \t\r\n="), to = text.find_last_not_of(" \t\r\n");
	return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
}

//a character's hash by the name the game shows, or its set's: the first with that name
uint32_t Character(const PPCContext& c, const std::string& name, bool set)
{
	if (name.size() == 8 && name.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) return (uint32_t)std::strtoul(name.c_str(), nullptr, 16);
	const uint32_t count = ld32(kCharacters + kCharacterCount), rows = ld32(kCharacters + kCharacterRows);
	if (!guest_ptr(rows) || count > 4096) return 0;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t row = rows + i * kCharacterSize;
		if (Lower(game_text(c, ld32(row + kCharacterName))) != Lower(name)) continue;
		return set && ld32(row + kCharacterSet) != kNoSet ? ld32(row + kCharacterSet) : ld32(row);
	}
	return 0;
}

//a team's name as the game's text: UTF-8 into the characters behind a text row of our own
void SetName(int team, const std::string& name)
{
	const uint32_t row = g_name_text[team], chars = row + kTextHead;
	uint32_t length = 0;
	for (size_t i = 0; i < name.size() && length < kNameLength;)
	{
		const uint8_t lead = (uint8_t)name[i];
		const int more = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
		uint32_t code = more == 0 ? lead : lead & (0x3F >> more);
		for (int k = 1; k <= more && i + k < name.size(); ++k) code = (code << 6) | ((uint8_t)name[i + k] & 0x3F);
		i += more + 1;
		st16(chars + length++ * 2, code < 0x10000 ? (uint16_t)code : '?');
	}
	st16(chars + length * 2, 0);
	st32(row + kTextChars, kTextHead - kTextChars);
	st32(row + kTextEnd, kTextHead - kTextEnd + length * 2);
	st32(row + kTextWord, 0xFFFFFFFF);
}

//what the level's .txt says of the four, 0 for what it leaves to the row that stands in
bool ReadCast(const PPCContext& c, const ModLevel& level, uint32_t cast[4], std::string names[2])
{
	std::ifstream file(mod_side_file(level, ".txt"));
	if (!file) return false;
	bool any = false;
	std::string line;
	while (std::getline(file, line))
	{
		line = Trimmed(line);
		for (int i = 0; i < 4; ++i)
		{
			const size_t length = std::strlen(kCastKeys[i]);
			if (Lower(line.substr(0, length)) != kCastKeys[i]) continue;
			const std::string name = Trimmed(line.substr(length));
			const uint32_t found = Character(c, name, i < 2);
			if (found) cast[i] = found;
			if (found) rt_log("goldeneye: mod level %s: %s is %s (%08X) in a network game", level.name.c_str(), kCastKeys[i], name.c_str(), cast[i]);
			else rt_log("goldeneye: mod level %s: the game has no character named %s for %s", level.name.c_str(), name.c_str(), kCastKeys[i]);
			any |= found != 0;
		}
		for (int i = 0; i < 2; ++i)
		{
			if (Lower(line.substr(0, 5)) != (i == 0 ? "name0" : "name1")) continue;
			names[i] = Trimmed(line.substr(5));
			rt_log("goldeneye: mod level %s: team %d is called %s in a network game", level.name.c_str(), i, names[i].c_str());
		}
	}
	return any;
}

//a mod level in a multiplayer game gets a level row to stand in, with its own cast and names
void StandIn(PPCContext& c, uint32_t levels, uint32_t hash)
{
	g_kind_level = 0;
	g_named[0] = g_named[1] = false;
	if (!guest_ptr(levels) || ld32(levels + kLevelRow) != 0) return;
	const auto& files = mod_files();
	const auto level = std::find_if(files.begin(), files.end(), [&](const ModLevel& m) { return m.hash == hash; });
	if (level == files.end()) return;
	uint32_t cast[4] = {};
	std::string names[2];
	const bool ownCast = ReadCast(c, *level, cast, names);
	PPCContext call = c;
	uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	if (!is_multiplayer) return;
	is_multiplayer(call);
	if ((call.r[3] & 0xFF) == 0) return;
	uint32_t row = RowOf(ld32(levels + kLevelRows), ld32(levels + kLevelRowCount), kLevelRowSize, kStandIn);
	if (!row) return;
	if (ownCast && !g_cast_row && level_alloc)
	{
		call.r[1] = frame;
		call.r[3] = kLevelRowSize; call.r[4] = 0;
		level_alloc(call);
		g_cast_row = guest_ptr(call.r[3]) ? call.r[3] : 0;
	}
	if (ownCast && g_cast_row)
	{
		std::memcpy(PPC_MEM(g_cast_row), PPC_MEM(row), kLevelRowSize);
		for (int i = 0; i < 4; ++i)
			if (cast[i]) st32(g_cast_row + kRowCast[i], cast[i]);
		row = g_cast_row;
	}
	st32(levels + kLevelRow, row);
	st32(levels + kLevelRow2, RowOf(ld32(levels + kLevelRows2), ld32(levels + kLevelRow2Count), kLevelRow2Size, kStandIn));
	//the second team's number, here for a gamemode that is already there and in TeamModeReset for one made later
	g_kind = 0;
	for (const auto& set : kKindSets)
		if (ld32(row + kRowCast[1]) == set[0]) g_kind = set[1];
	g_kind_level = hash;
	st32(kTeamKind, g_kind);
	for (int i = 0; i < 2; ++i)
	{
		if (names[i].empty() || !level_alloc) continue;
		if (!g_name_text[i])
		{
			call.r[1] = frame;
			call.r[3] = kTextHead + (kNameLength + 1) * 2; call.r[4] = 0;
			level_alloc(call);
			g_name_text[i] = guest_ptr(call.r[3]) ? call.r[3] : 0;
		}
		if (!g_name_text[i]) continue;
		SetName(i, names[i]);
		g_named[i] = true;
	}
	rt_log("goldeneye: mod level %08X in a multiplayer game: it has no row in the game's level table, %08X's stands in", hash, kStandIn);
}

void TeamModeReset(PPCContext& c)
{
	const uint32_t mode = c.r[3];
	orig_team_mode_reset(c);
	if (!g_kind_level || ld32(kCurrentLevel) != g_kind_level || !guest_ptr(mode)) return;
	st32(mode + kModeTeamKind, g_kind);
	st32(kTeamKind, g_kind);
}

//the Mission Complete page (ge_mission_page, every time it fills itself) looks the level that just
//ended up in the level table with no check: its name at +8 and +0xC and in Time Trial the time to
//beat at +0x1C and +0x20, a crash at 0000000C for a mod level. the row is wanted by nothing else
//there, so for that one call the stand-in's row has the mod's name and the page is told it's that
//level. the stand-in isn't a mission, so no best time of a real mission turns up on the page
const uint32_t kLastLevel = 0x805D1808, kRowName = 0x8, kRowPlace = 0xC, kRowTimeA = 0x1C, kRowTimeB = 0x20;
const uint32_t kTextLevel = 0x43FFFF00; //no texts of the game's, for TextFind: this one and the next
const char* const kPlaceName = "Custom Level";
PPCFunc orig_mission_page = nullptr;

void MissionPage(PPCContext& c)
{
	struct Drawing { Drawing() { custom_page_drawing(true); } ~Drawing() { custom_page_drawing(false); } } drawing; //007 Custom's settings can be up in it
	best_times_page(c.r[3]);
	const uint32_t levels = kCurrentLevel, last = ld32(kLastLevel);
	const uint32_t row = RowOf(ld32(levels + kLevelRows), ld32(levels + kLevelRowCount), kLevelRowSize, kStandIn);
	const auto& files = mod_files();
	const auto level = std::find_if(files.begin(), files.end(), [&](const ModLevel& m) { return m.hash == last; });
	if (!row || level == files.end() || RowOf(ld32(levels + kLevelRows), ld32(levels + kLevelRowCount), kLevelRowSize, last))
	{
		orig_mission_page(c);
		return;
	}
	for (int i = 2; i < 4; ++i)
	{
		if (g_name_text[i] || !level_alloc) continue;
		PPCContext call = c;
		call.r[1] = c.r[1] - 0x40;
		st32(call.r[1], c.r[1]);
		call.r[3] = kTextHead + (kNameLength + 1) * 2; call.r[4] = 0;
		level_alloc(call);
		g_name_text[i] = guest_ptr(call.r[3]) ? call.r[3] : 0;
	}
	const bool named = g_name_text[2] && g_name_text[3];
	if (named)
	{
		SetName(2, mod_shown_name(level->name));
		SetName(3, kPlaceName);
	}
	const uint32_t kept[4] = { ld32(row + kRowName), ld32(row + kRowPlace), ld32(row + kRowTimeA), ld32(row + kRowTimeB) };
	st32(row + kRowName, named ? kTextLevel : 0);
	st32(row + kRowPlace, named ? kTextLevel + 1 : 0);
	st32(row + kRowTimeA, 0);
	st32(row + kRowTimeB, 0);
	st32(kLastLevel, kStandIn);
	orig_mission_page(c);
	st32(kLastLevel, last);
	st32(row + kRowName, kept[0]);
	st32(row + kRowPlace, kept[1]);
	st32(row + kRowTimeA, kept[2]);
	st32(row + kRowTimeB, kept[3]);
}

void TextFind(PPCContext& c)
{
	const uint32_t hash = c.r[4];
	if ((hash == kTextLevel || hash == kTextLevel + 1) && g_name_text[2 + hash - kTextLevel])
	{
		const uint32_t text = g_name_text[2 + hash - kTextLevel];
		st32(text, hash);
		c.r[3] = text;
		return;
	}
	const int team = hash == kTextMi6 ? 0 : 1;
	if (hash < kTextJanus || hash > kTextZukovsky || !g_named[team] || ld32(kCurrentLevel) != g_kind_level) { orig_text_find(c); return; }
	st32(g_name_text[team], hash);
	c.r[3] = g_name_text[team];
}

//a multiplayer spawn point has to have floor within a metre below it: ge_spawn_points_setup
//traces from 0.3 above each 1.3 down and, when one hits nothing, stops the game in an endless
//loop at its end ("SpawnPoint @ ... is not in the map", which the release build doesn't print).
//before it runs, a spawn point that hangs higher is put on the floor below it, and one with no
//floor at all is logged, so the level's maker knows why the game stands still. a spawn point
//right over the line between two triangles of a floor falls through between them (a trace along
//a shared edge hits neither): it is moved a little to the side first
PPCFunc orig_spawn_setup = nullptr, make_ray = nullptr, trace_ray = nullptr;
const uint32_t kObjects = 0x805F4D94, kObjectNext = 0x4, kObjectPlace = 0x10, kObjectClass = 0x5C, kObjectKind = 0x60;
const uint32_t kSpawnClass = 0x1E000003, kMultiplayerSpawn = 0x1F000098;
const uint32_t kWorld = 0x805F4CCC, kMakeRay = 0x802D0250, kTraceRay = 0x8032E700;
const uint32_t kRayHit = 0xD4, kRayFraction = 0xA0; //of the ray: a byte, and how far along it
const float kTraceAbove = 0.3f, kTraceDown = 1.3f; //the game's own
const float kFarDown = 200.0f, kOnFloor = 0.05f;
const float kAside = 0.03f;
const float kAsides[][2] = { { 1.0f, 0.4f }, { -0.4f, 1.0f }, { -1.0f, -0.4f }, { 0.4f, -1.0f } };

//how far below (x, y, z) the world is hit going straight down by reach, negative when it isn't
float FloorBelow(const PPCContext& c, float x, float y, float z, float reach)
{
	const float start[3] = { x, y, z }, way[3] = { 0.0f, -reach, 0.0f };
	const float hit = world_trace(c, start, way);
	return hit >= 0.0f ? hit * reach : -1.0f;
}

void SpawnPointsSetup(PPCContext& c)
{
	if (make_ray && trace_ray)
	{
		int guard = 0;
		for (uint32_t object = ld32(kObjects); guest_ptr(object) && guard < 100000; object = ld32(object + kObjectNext), ++guard)
		{
			if (ld32(object + kObjectClass) != kSpawnClass || ld32(object + kObjectKind) != kMultiplayerSpawn) continue;
			float x = ldf(object + kObjectPlace), y = ldf(object + kObjectPlace + 4), z = ldf(object + kObjectPlace + 8);
			if (FloorBelow(c, x, y + kTraceAbove, z, kTraceDown) >= 0.0f) continue;
			float far = FloorBelow(c, x, y + kTraceAbove, z, kFarDown);
			for (const auto& aside : kAsides)
			{
				if (far >= 0.0f) break;
				const float nx = x + aside[0] * kAside, nz = z + aside[1] * kAside;
				far = FloorBelow(c, nx, y + kTraceAbove, nz, kFarDown);
				if (far < 0.0f) continue;
				rt_log("goldeneye: the multiplayer spawn point at %.2f %.2f %.2f is over a line between two triangles of the floor: moved %.2f aside", x, y, z, kAside);
				stf(object + kObjectPlace, nx); stf(object + kObjectPlace + 8, nz);
				x = nx; z = nz;
			}
			if (far >= 0.0f && far <= kTraceDown) continue;
			if (far < 0.0f)
			{
				rt_log("goldeneye: the multiplayer spawn point at %.2f %.2f %.2f has no floor below it: the game stops on such a level, on purpose", x, y, z);
				continue;
			}
			const float floor = y + kTraceAbove - far;
			stf(object + kObjectPlace + 4, floor + kOnFloor);
			rt_log("goldeneye: the multiplayer spawn point at %.2f %.2f %.2f is %.2f above the floor (the game wants less than 1): put on it", x, y, z, y - floor);
		}
	}
	orig_spawn_setup(c);
}

//a level's rows in the game's tables, picked as it loads: ge_level_set(levels, level) looks the
//level's hash up in two sorted tables and keeps the rows at +0x1C and +0x34. the second has a
//row for each of the 16 missions and nothing else: four values by difficulty, read without a
//look by seven small functions (fn_800C0B70 on), one of them when the player is hurt
//(fn_80135040 > fn_800C0BD0, which only in a network game answers 0.6 instead). a level of
//the mods has no row, so the first thing that hurts the player offline, their own grenade,
//read through 0 and crashed. it gets the first mission's row: all 16 are the same but for one
//value of Nightclub's
void LevelSet(PPCContext& c)
{
	const uint32_t levels = c.r[3], hash = c.r[4];
	st32(levels + kLevelRow2, 0); //the game leaves the last level's when the first table has no row either
	orig_level_set(c);
	StandIn(c, levels, hash);
	if (ld32(levels + kLevelRow2) || !ld32(levels + kLevelRow2Count) || !guest_ptr(ld32(levels + kLevelRows2))) return;
	st32(levels + kLevelRow2, ld32(levels + kLevelRows2));
	if (hash >> 16 == 0x0181) rt_log("goldeneye: level %08X has no difficulty values of its own, the first mission's are used", hash); //not for the menus' 01080004
}

} //namespace

//the game's own trace through the world's collision (the level, not the people in it): from
//start along way, how far along it the first hit is (0 to 1), negative when nothing is hit.
//on the game's thread, the ray is made on its stack below the caller's frame
float world_trace(const PPCContext& c, const float start[3], const float way[3])
{
	if (!make_ray || !trace_ray) return -1.0f;
	PPCContext call = c;
	const uint32_t stack = c.r[1], frame = stack - 0x200, ray = frame + 0x40, from = frame + 0x10, along = frame + 0x20;
	st32(frame, stack);
	stf(from, start[0]); stf(from + 4, start[1]); stf(from + 8, start[2]); stf(from + 12, 0.0f);
	stf(along, way[0]); stf(along + 4, way[1]); stf(along + 8, way[2]); stf(along + 12, 0.0f);
	call.r[1] = frame;
	call.r[3] = ray; call.r[4] = from; call.r[5] = along;
	call.r[6] = call.r[7] = call.r[8] = call.r[9] = 0;
	make_ray(call);
	call.r[1] = frame;
	call.r[3] = kWorld; call.r[4] = ray; call.r[5] = 0;
	trace_ray(call);
	return ld8(ray + kRayHit) ? ldf(ray + kRayFraction) : -1.0f;
}

//in the order of the game's Choose Game Mode. Classic Conflict has Conflict's maps, as in the game
const char* const kWeaponsFolder = "weapons", * const kCharactersFolder = "characters";
const char* const kModeFolders[kModeCount] = { "conflict", "team_conflict", "golden_gun", "black_box", "goldeneye", "heroes", "license_to_kill", "team_license_to_kill" };

const std::vector<ModLevel>& mod_levels() { return modpack_levels(g_active); }
const std::vector<ModLevel>& mod_files() { return modpack_files(g_active); }
const std::vector<ModLevel>& disc_levels() { return g_disc_levels; }
std::string file_name(uint32_t hash)
{
	auto it = g_file_names.find(hash);
	return it == g_file_names.end() ? std::string() : it->second;
}
bool disc_game_file(uint32_t hash, std::vector<uint8_t>& out)
{
	out.clear();
	uint64_t data = 0, size = 0;
	auto it = g_file_places.find(hash);
	if (it == g_file_places.end() || !disc_file("Filelist.000", &data, &size) || it->second + 0x20ull > size) return false;
	uint8_t head[0x20];
	disc_read(data + it->second, head, sizeof head);
	const uint32_t whole = Be32(&head[0x14]); //the .edb's size, its sections too
	if (Be32(head) != kGeom || Be32(&head[4]) != hash || whole < sizeof head || it->second + uint64_t(whole) > size) return false;
	out.resize(whole);
	disc_read(data + it->second, out.data(), out.size());
	return true;
}
const std::string& mods_folder() { return modpack_folder(g_active); }

int modpack_count() { return (int)g_packs.size(); }
int modpack_active() { return g_active; }
static bool ValidPack(int pack) { return pack >= 0 && pack < (int)g_packs.size(); }
const std::string& modpack_name(int pack) { return ValidPack(pack) ? g_packs[pack].name : kNoFolder; }
const std::string& modpack_folder(int pack) { return ValidPack(pack) ? g_packs[pack].folder : kNoFolder; }
const std::vector<ModLevel>& modpack_levels(int pack) { return ValidPack(pack) ? g_packs[pack].levels : kNone; }
const std::vector<ModLevel>& modpack_files(int pack) { return ValidPack(pack) ? g_packs[pack].files : kNone; }

//next to the mod's file, in whichever folder that is. a level in a gamemode's folder that has
//none there has the one in its pack's folder itself: one for the map, whichever gamemode
std::string mod_side_file(const ModLevel& mod, const char* extension)
{
	const std::filesystem::path next = std::filesystem::path(mod.path).replace_extension(extension);
	std::error_code error;
	if (mod.modes == 0 || std::filesystem::exists(next, error)) return next.string();
	return (next.parent_path().parent_path() / next.filename()).string();
}

namespace
{

//the pack whose file a slot has for a pack: its own, the disc's (-1), or the one already there
int Pick(const Slot& slot, int pack)
{
	if (ValidPack(pack) && slot.mods[pack] >= 0) return pack;
	if (slot.onDisc) return -1;
	if (slot.shown != -2) return slot.shown;
	for (size_t i = 0; i < slot.mods.size(); ++i)
		if (slot.mods[i] >= 0) return (int)i;
	return -1;
}

const Mod& ModOf(const Slot& slot, int pack) { return g_packs[pack].mods[slot.mods[pack]]; }

//a mod's bytes: the file, with GEOM and the hash it has here in front when that isn't its own
std::vector<DiscPart> PartsOf(const Mod& mod)
{
	DiscPart file; file.path = mod.path; file.size = mod.size;
	if (!mod.ownHash || mod.size <= kHeadSize) return { file };
	DiscPart head;
	head.data = std::make_shared<std::vector<uint8_t>>();
	Put32(*head.data, kGeom); Put32(*head.data, mod.hash);
	file.off = kHeadSize; file.size = mod.size - kHeadSize;
	return { head, file };
}

//a slot's row as the pack has it
void RowOf(const Slot& slot, int pack, Row& row)
{
	if (pack < 0) { row = slot.disc; return; }
	const Mod& mod = ModOf(slot, pack);
	row.size = mod.loaded; row.hash = mod.hash; row.version = mod.version; row.flags = mod.flags;
	row.places = { { uint32_t(slot.at), 0 } };
}

//the game's copy of Filelist.bin's rows
const uint32_t kFileSystem = 0x806EC2D0, kSystemCount = 0x8, kSystemLists = 0xC;
const uint32_t kListPath = 0x0, kListFileCount = 0x24, kListFiles = 0x28, kEntrySize = 0x40;
const uint32_t kEntryLoaded = 0x4, kEntryPlaces = 0x8, kEntryFile = 0x28, kEntryHash = 0x30, kEntryFlags = 0x34, kEntryVersion = 0x38, kEntryPlaceCount = 0x3C;
const uint32_t kPlaceSize = 0x8, kPlaceCount = 4, kFlagGame = 0x80000000;
const uint32_t kInfoPlaces = 0x4, kInfoPlaceSize = 0xC; //a file object's copy, its size at kInfoSize
const char* const kListFile = "filelist.bin";

uint32_t GameList()
{
	const uint32_t system = ld32(kFileSystem);
	if (!guest_ptr(system)) return 0;
	const uint32_t count = ld32(system + kSystemCount), lists = ld32(system + kSystemLists);
	for (uint32_t i = 0; guest_ptr(lists) && i < count && i < 16; ++i)
	{
		const uint32_t list = ld32(lists + i * 4), path = guest_ptr(list) ? ld32(list + kListPath) : 0;
		if (!guest_ptr(path)) continue;
		const std::string name = Lower((const char*)PPC_MEM(path));
		if (name.size() >= std::strlen(kListFile) && name.compare(name.size() - std::strlen(kListFile), std::string::npos, kListFile) == 0) return list;
	}
	return 0;
}

void SetEntry(uint32_t list, const Row& row)
{
	const uint32_t count = ld32(list + kListFileCount), files = ld32(list + kListFiles);
	uint32_t entry = 0;
	for (uint32_t i = 0; guest_ptr(files) && i < count && i < 65536 && !entry; ++i)
		if (ld32(files + i * kEntrySize + kEntryHash) == row.hash) entry = files + i * kEntrySize;
	if (!entry || row.places.empty())
	{
		rt_log("goldeneye: modpacks: the game's file list has no row for %08X", row.hash);
		return;
	}
	const uint32_t place = row.places[0].first, which = row.places[0].second;
	st32(entry + kEntryLoaded, row.size);
	for (uint32_t k = 0; k < kPlaceCount; ++k) { st32(entry + kEntryPlaces + k * kPlaceSize, place); st32(entry + kEntryPlaces + k * kPlaceSize + 4, which); }
	st32(entry + kEntryPlaceCount, 1);
	st32(entry + kEntryFlags, row.flags | (ld32(entry + kEntryFlags) & kFlagGame));
	st32(entry + kEntryVersion, row.version);
	const uint32_t file = ld32(entry + kEntryFile), info = guest_ptr(file) ? ld32(file + kFileInfo) : 0;
	if (!guest_ptr(info)) return;
	if (ld32(file + kFileLoaded)) rt_log("goldeneye: modpacks: %08X is loaded as its pack changes, it keeps the last one's until it loads again", row.hash);
	for (uint32_t k = 0; k < kPlaceCount; ++k) st32(info + kInfoPlaces + k * kInfoPlaceSize, place);
	st32(info + kInfoSize, row.size);
}

//every slot gets the pack's file. the game's rows too, once it has read them
void Install(int pack)
{
	const uint32_t list = GameList();
	int changed = 0;
	for (Slot& slot : g_slots)
	{
		const int want = Pick(slot, pack);
		if (want == slot.shown) continue;
		if (want >= 0 && !disc_rewrite("Filelist.000", slot.at, slot.room, PartsOf(ModOf(slot, want))))
		{
			rt_log("goldeneye: modpacks: %s's %s couldn't be put in place", g_packs[want].name.c_str(), ModOf(slot, want).name.c_str());
			continue;
		}
		slot.shown = want;
		++changed;
		if (!list) continue;
		Row row;
		RowOf(slot, want, row);
		SetEntry(list, row);
	}
	g_active = pack;
	rt_log("goldeneye: modpack %s installed: %zu level(s), %zu file(s), %d place(s) changed", modpack_name(pack).c_str(), modpack_levels(pack).size(), modpack_files(pack).size(), changed);
}

//a folder's name as it is looked for: lower case, a space or a dash is an underscore. the packs
//by their folders: "default" first, then by name
void FindPacks()
{
	std::error_code error;
	if (const char* one = std::getenv("GE_MODS"))
	{
		if (std::filesystem::is_directory(one, error)) g_packs.push_back({ kDefaultPack, one, {}, {}, {} });
		return;
	}
	const char* env = std::getenv("GE_MODPACKS");
	const std::filesystem::path root = env ? std::filesystem::path(env) : std::filesystem::path(GE_ROOT) / "modpacks";
	if (!std::filesystem::is_directory(root, error))
	{
		const std::filesystem::path old = std::filesystem::path(GE_ROOT) / "mods";
		if (env || !std::filesystem::is_directory(old, error)) return;
		rt_log("goldeneye: modpacks: no modpacks folder, %s is the pack \"%s\" (move it to modpacks/%s)", old.string().c_str(), kDefaultPack, kDefaultPack);
		g_packs.push_back({ kDefaultPack, old.string(), {}, {}, {} });
		return;
	}
	//a folder is a pack, and so is a zip (unzip.cpp: unpacked into .unzipped, which isn't one)
	std::vector<std::filesystem::path> zips;
	for (const auto& entry : std::filesystem::directory_iterator(root, error))
	{
		const std::string name = entry.path().filename().string();
		if (name.empty() || name[0] == '.') continue;
		if (entry.is_directory(error)) g_packs.push_back({ name, entry.path().string(), {}, {}, {} });
		else if (Lower(entry.path().extension().string()) == ".zip") zips.push_back(entry.path());
	}
	//a folder and a zip of the same name: the folder, unless it has no file at all (the packages'
	//empty default, its gamemode folders and nothing in them)
	auto hasFiles = [&](const std::string& folder)
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, error))
			if (entry.is_regular_file(error)) return true;
		return false;
	};
	for (const auto& zip : zips)
	{
		const std::string name = zip.stem().string();
		const auto same = std::find_if(g_packs.begin(), g_packs.end(), [&](const Pack& p) { return Lower(p.name) == Lower(name); });
		if (same != g_packs.end() && hasFiles(same->folder))
		{
			rt_log("goldeneye: modpacks: there is a folder %s, %s is left out", name.c_str(), zip.filename().string().c_str());
			continue;
		}
		if (same != g_packs.end()) g_packs.erase(same);
		const std::filesystem::path folder = root / kUnzipped / name;
		if (unzip_pack(zip.string(), folder.string())) g_packs.push_back({ name, folder.string(), {}, {}, {} });
	}
	std::sort(g_packs.begin(), g_packs.end(), [](const Pack& a, const Pack& b)
	{
		const bool aFirst = Lower(a.name) == kDefaultPack, bFirst = Lower(b.name) == kDefaultPack;
		return aFirst != bFirst ? aFirst : Lower(a.name) < Lower(b.name);
	});
}

//a pack's .edb files: in its folder, its gamemode folders and weapons/. two with the same hash
//(two levels made with the same number): the second by name gets the first free hash of its
//kind, counted up from where the made ones start, among the disc's and the pack's own (so a pack
//numbers its files the same whatever other packs there are). the file is served with that hash in
//its header. one that has the hash of a file of the disc is meant to take that file's place and keeps it
void ReadPack(Pack& pack, const std::set<uint32_t>& disc)
{
	std::error_code error;
	ReadFolder(pack.folder, 0, pack.mods);
	std::vector<std::filesystem::path> folders;
	for (const auto& entry : std::filesystem::directory_iterator(pack.folder, error))
		if (entry.is_directory(error)) folders.push_back(entry.path());
	std::sort(folders.begin(), folders.end());
	for (const auto& folder : folders)
	{
		const std::string name = FolderName(folder.filename().string());
		const auto mode = std::find(std::begin(kModeFolders), std::end(kModeFolders), name);
		if (mode != std::end(kModeFolders)) ReadFolder(folder, 1u << (mode - std::begin(kModeFolders)), pack.mods);
		else if (name == kWeaponsFolder) ReadFolder(folder, 0, pack.mods); //the custom weapons' guns (weapons/weapons.cpp)
		else if (name == kCharactersFolder) ReadFolder(folder, 0, pack.mods); //the custom characters' bodies (characters.cpp)
		else rt_log("goldeneye: modpack %s: the folder %s isn't a gamemode's, left out", pack.name.c_str(), folder.filename().string().c_str());
	}
	std::sort(pack.mods.begin(), pack.mods.end(), [](const Mod& a, const Mod& b) { return a.name != b.name ? a.name < b.name : a.path < b.path; });
	std::set<uint32_t> taken = disc, ofMods;
	for (const Mod& mod : pack.mods) taken.insert(mod.hash);
	for (auto mod = pack.mods.begin(); mod != pack.mods.end();)
	{
		if (ofMods.insert(mod->hash).second) { ++mod; continue; }
		uint32_t fresh = (mod->hash & 0xFFFF0000u) | kFirstFree;
		while (taken.count(fresh) && (fresh & 0xFFFF) != 0xFFFF) ++fresh;
		if (taken.count(fresh)) { rt_log("goldeneye: modpack %s: no hash left for %s", pack.name.c_str(), mod->name.c_str()); mod = pack.mods.erase(mod); continue; }
		rt_log("goldeneye: modpack %s: %s has the hash of another mod (%08X): it is %08X here", pack.name.c_str(), mod->name.c_str(), mod->hash, fresh);
		mod->ownHash = mod->hash;
		mod->hash = fresh;
		taken.insert(fresh);
		ofMods.insert(fresh);
		++mod;
	}
}

} //namespace

void modpack_select(int pack)
{
	if (!ValidPack(pack) || pack == g_active || g_slots.empty()) return;
	Install(pack);
}

//the file of the hash the game gets now is the pack's: its own, or another pack's with the same bytes
bool modpack_serves(int pack, uint32_t hash)
{
	const auto slot = std::find_if(g_slots.begin(), g_slots.end(), [&](const Slot& s) { return s.hash == hash; });
	if (!ValidPack(pack) || slot == g_slots.end() || slot->mods[pack] < 0 || slot->shown < 0) return false;
	if (slot->shown == pack) return true;
	static std::map<std::pair<std::string, std::string>, bool> same;
	const std::string a = ModOf(*slot, slot->shown).path, b = ModOf(*slot, pack).path;
	const auto known = same.find({ a, b });
	return known != same.end() ? known->second : same[{ a, b }] = SameBytes(a, b);
}

void install_mods()
{
	orig_onload = ppc_hook("ge_geofile_onload", OnLoad);
	orig_level_set = ppc_hook("ge_level_set", LevelSet);
	if (!orig_level_set) rt_die("goldeneye: ge_level_set is not hooked in this build");
	orig_team_mode_reset = ppc_hook("ge_team_mode_reset", TeamModeReset);
	orig_text_find = ppc_hook("ge_text_find", TextFind);
	orig_mission_page = ppc_hook("ge_mission_page", MissionPage);
	level_alloc = ppc_lookup(kGameAlloc);
	is_multiplayer = ppc_lookup(kIsMultiplayer);
	orig_spawn_setup = ppc_hook("ge_spawn_points_setup", SpawnPointsSetup);
	make_ray = ppc_lookup(kMakeRay);
	trace_ray = ppc_lookup(kTraceRay);

	//the disc's own list, read whether there are mods or not: its levels are Extras > Level Select's
	uint64_t listAt = 0, listSize = 0, dataSize = 0;
	std::vector<Row> rows;
	uint16_t buildType = 0, lists = 0;
	const bool listed = disc_file("Filelist.bin", &listAt, &listSize) && disc_file("Filelist.000", nullptr, &dataSize);
	if (listed)
	{
		std::vector<uint8_t> bin(listSize);
		disc_read(listAt, bin.data(), bin.size());
		if (!ReadList(bin, rows, buildType, lists)) rows.clear();
	}
	for (const Row& row : rows)
	{
		const size_t cut = row.name.find_last_of("\\/");
		g_file_names[row.hash] = cut == std::string::npos ? row.name : row.name.substr(cut + 1);
		for (const auto& place : row.places)
			if (place.second == 0) { g_file_places[row.hash] = place.first; break; }
	}
	for (const Row& row : rows)
	{
		const uint32_t kind = row.hash & 0xFFFF0000u;
		if (kind != kLevelFiles && kind != kMultiplayerLevelFiles) continue;
		const size_t slash = row.name.find_last_of("\\/");
		g_disc_levels.push_back({ row.hash, slash == std::string::npos ? row.name : row.name.substr(slash + 1), {}, 0 });
	}

	FindPacks();
	if (g_packs.empty()) return;
	if (!listed)
	{
		rt_log("goldeneye: mods: the disc has no Filelist.bin and Filelist.000");
		return;
	}
	if (rows.empty())
	{
		rt_log("goldeneye: mods: Filelist.bin isn't a version %u list", kListVersion);
		return;
	}
	std::set<uint32_t> disc;
	for (const Row& row : rows) disc.insert(row.hash);
	for (Pack& pack : g_packs) ReadPack(pack, disc);

	//a slot for every hash, as big as the biggest of its files
	std::map<uint32_t, size_t> slotOf;
	for (size_t p = 0; p < g_packs.size(); ++p)
		for (size_t m = 0; m < g_packs[p].mods.size(); ++m)
		{
			const Mod& mod = g_packs[p].mods[m];
			auto found = slotOf.find(mod.hash);
			if (found == slotOf.end())
			{
				found = slotOf.emplace(mod.hash, g_slots.size()).first;
				Slot slot;
				slot.hash = mod.hash;
				slot.mods.assign(g_packs.size(), -1);
				const auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.hash == mod.hash; });
				if (row != rows.end()) { slot.onDisc = true; slot.disc = *row; }
				g_slots.push_back(slot);
			}
			Slot& slot = g_slots[found->second];
			slot.mods[p] = (int)m;
			slot.room = std::max<uint64_t>(slot.room, mod.size);
		}
	std::vector<DiscPart> parts;
	DiscPart original; original.original = true;
	parts.push_back(original);
	uint64_t end = dataSize;
	for (auto slot = g_slots.begin(); slot != g_slots.end();)
	{
		const uint64_t at = (end + kSector - 1) & ~(kSector - 1);
		if (at + slot->room > 0xFFFFFFFFull)
		{
			rt_log("goldeneye: mods: no room left for %08X", slot->hash);
			for (size_t p = 0; p < g_packs.size(); ++p) slot->mods[p] = -1;
			slot = g_slots.erase(slot);
			continue;
		}
		DiscPart gap; gap.size = at - end;
		DiscPart room; room.size = slot->room; //zeros until a pack's file is put there
		parts.push_back(gap);
		parts.push_back(room);
		slot->at = at;
		end = at + slot->room;
		++slot;
	}
	for (Pack& pack : g_packs)
	{
		const size_t p = size_t(&pack - g_packs.data());
		for (const Slot& slot : g_slots)
		{
			if (slot.mods[p] < 0) continue;
			const Mod& mod = ModOf(slot, (int)p);
			pack.files.push_back({ mod.hash, mod.name, mod.path, mod.modes });
			if ((mod.hash & 0xFFFF0000u) == kLevelFiles) pack.levels.push_back({ mod.hash, mod.name, mod.path, mod.modes });
		}
		auto byName = [](const ModLevel& a, const ModLevel& b) { return a.name != b.name ? a.name < b.name : a.path < b.path; };
		std::sort(pack.files.begin(), pack.files.end(), byName);
		std::sort(pack.levels.begin(), pack.levels.end(), byName);
		rt_log("goldeneye: modpack %s (%s): %zu file(s), %zu level(s)", pack.name.c_str(), pack.folder.c_str(), pack.files.size(), pack.levels.size());
	}

	//the pack the game starts with
	int first = 0;
	const char* wanted = std::getenv("GE_MODPACK");
	for (size_t p = 0; p < g_packs.size(); ++p)
		if (Lower(g_packs[p].name) == Lower(wanted ? wanted : kDefaultPack)) first = (int)p;
	if (wanted && Lower(g_packs[first].name) != Lower(wanted)) rt_log("goldeneye: modpacks: there is no pack %s, %s is installed", wanted, g_packs[first].name.c_str());

	//the list gets a row for every slot: the first pack's file in it, or the disc's
	for (const Slot& slot : g_slots)
	{
		const int pick = Pick(slot, first);
		auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.hash == slot.hash; });
		const bool isNew = row == rows.end();
		if (isNew) { rows.push_back(Row{}); row = rows.end() - 1; row->name = kNamePrefix + ModOf(slot, pick).name; }
		RowOf(slot, pick, *row);
		if (pick >= 0) rt_log("goldeneye: mod %s: file %08X, %s", ModOf(slot, pick).name.c_str(), slot.hash, isNew ? "new" : "in place of the disc's");
	}
	if (!disc_replace("Filelist.000", parts) || !disc_replace("Filelist.bin", { DiscPart{ {}, 0, 0, WriteList(rows, buildType, lists) } }))
	{
		rt_log("goldeneye: mods: the disc's file list couldn't be replaced");
		g_slots.clear();
		return;
	}
	Install(first);
}

} //namespace ge
