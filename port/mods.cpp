//mods: loose .edb files take the place of the disc's, or come on top of them
//
//the game finds a file by its hash in Filelist.bin (version 7): a row for each file with the size
//of the part that stays loaded (the .edb header's +0x18), its hash (+4), version (+8), flags (+0xC)
//and where it starts in Filelist.000, then the names, scrambled by row and place
//a mod goes behind the end of Filelist.000 and gets the row of the file with its hash, or a new
//row at the end. two mods with the same hash: the second gets another one. a level is its mt_ file's hash: GE_LEVEL=hash starts a new one
//
//the levels among them can be picked in the game's menu: Extras > Mods (port/modmenu.cpp)
//online.txt in the folder is a list of maps for multiplayer's Conflict (the same file)
//a .png with a level's name is its picture in the menus (port/pictures.cpp)
//
//GE_MODS=DIR the folder, every .edb in it (mods in the game's folder by default)
//GE_TRACEFILES=1 log every file the game gives memory to, with its size
#include "ge.h"
#include "disc.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
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

std::vector<ModLevel> g_levels, g_files;
std::string g_folder;

//a level of the mods folder isn't in the game's level table. a single player game does without
//the row, a multiplayer game reads the players' characters out of it without asking (fn_8000F8E0,
//a crash at 000000A4). there the row of one of the game's own multiplayer maps stands in
PPCFunc orig_level_set = nullptr, is_multiplayer = nullptr;
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

void LevelSet(PPCContext& c)
{
	const uint32_t levels = c.r[3], hash = c.r[4];
	orig_level_set(c);
	if (!guest_ptr(levels) || ld32(levels + kLevelRow) != 0) return;
	if (std::none_of(g_files.begin(), g_files.end(), [&](const ModLevel& m) { return m.hash == hash; })) return;
	PPCContext call = c;
	uint32_t stack = c.r[1], frame = stack - 0x40;
	st32(frame, stack);
	call.r[1] = frame;
	if (!is_multiplayer) return;
	is_multiplayer(call);
	if ((call.r[3] & 0xFF) == 0) return;
	const uint32_t row = RowOf(ld32(levels + kLevelRows), ld32(levels + kLevelRowCount), kLevelRowSize, kStandIn);
	if (!row) return;
	st32(levels + kLevelRow, row);
	st32(levels + kLevelRow2, RowOf(ld32(levels + kLevelRows2), ld32(levels + kLevelRow2Count), kLevelRow2Size, kStandIn));
	rt_log("goldeneye: mod level %08X in a multiplayer game: it has no row in the game's level table, %08X's stands in", hash, kStandIn);
}

//a multiplayer spawn point has to have floor within a metre below it: ge_spawn_points_setup
//traces from 0.3 above each 1.3 down and, when one hits nothing, stops the game in an endless
//loop at its end ("SpawnPoint @ ... is not in the map", which the release build doesn't print).
//before it runs, a spawn point that hangs higher is put on the floor below it, and one with no
//floor at all is logged, so the level's maker knows why the game stands still
PPCFunc orig_spawn_setup = nullptr, make_ray = nullptr, trace_ray = nullptr;
const uint32_t kObjects = 0x805F4D94, kObjectNext = 0x4, kObjectPlace = 0x10, kObjectClass = 0x5C, kObjectKind = 0x60;
const uint32_t kSpawnClass = 0x1E000003, kMultiplayerSpawn = 0x1F000098;
const uint32_t kWorld = 0x805F4CCC, kMakeRay = 0x802D0250, kTraceRay = 0x8032E700;
const uint32_t kRayHit = 0xD4, kRayFraction = 0xA0; //of the ray: a byte, and how far along it
const float kTraceAbove = 0.3f, kTraceDown = 1.3f; //the game's own
const float kFarDown = 200.0f, kOnFloor = 0.05f;

//how far below (x, y, z) the world is hit going straight down by reach, negative when it isn't
float FloorBelow(const PPCContext& c, float x, float y, float z, float reach)
{
	PPCContext call = c;
	const uint32_t stack = c.r[1], frame = stack - 0x200, ray = frame + 0x40, start = frame + 0x10, way = frame + 0x20;
	st32(frame, stack);
	stf(start, x); stf(start + 4, y); stf(start + 8, z); stf(start + 12, 0.0f);
	stf(way, 0.0f); stf(way + 4, -reach); stf(way + 8, 0.0f); stf(way + 12, 0.0f);
	call.r[1] = frame;
	call.r[3] = ray; call.r[4] = start; call.r[5] = way;
	call.r[6] = call.r[7] = call.r[8] = call.r[9] = 0;
	make_ray(call);
	call.r[1] = frame;
	call.r[3] = kWorld; call.r[4] = ray; call.r[5] = 0;
	trace_ray(call);
	return ld8(ray + kRayHit) ? ldf(ray + kRayFraction) * reach : -1.0f;
}

void SpawnPointsSetup(PPCContext& c)
{
	if (make_ray && trace_ray)
	{
		int guard = 0;
		for (uint32_t object = ld32(kObjects); guest_ptr(object) && guard < 100000; object = ld32(object + kObjectNext), ++guard)
		{
			if (ld32(object + kObjectClass) != kSpawnClass || ld32(object + kObjectKind) != kMultiplayerSpawn) continue;
			const float x = ldf(object + kObjectPlace), y = ldf(object + kObjectPlace + 4), z = ldf(object + kObjectPlace + 8);
			if (FloorBelow(c, x, y + kTraceAbove, z, kTraceDown) >= 0.0f) continue;
			const float far = FloorBelow(c, x, y + kTraceAbove, z, kFarDown);
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

} //namespace

const std::vector<ModLevel>& mod_levels() { return g_levels; }
const std::vector<ModLevel>& mod_files() { return g_files; }
const std::string& mods_folder() { return g_folder; }

void install_mods()
{
	orig_onload = ppc_hook("ge_geofile_onload", OnLoad);
	orig_level_set = ppc_hook("ge_level_set", LevelSet);
	is_multiplayer = ppc_lookup(kIsMultiplayer);
	orig_spawn_setup = ppc_hook("ge_spawn_points_setup", SpawnPointsSetup);
	make_ray = ppc_lookup(kMakeRay);
	trace_ray = ppc_lookup(kTraceRay);

	const char* env = std::getenv("GE_MODS");
	const std::string dir = env ? env : std::string(GE_ROOT) + "/mods";
	std::error_code error;
	if (dir.empty() || !std::filesystem::is_directory(dir, error)) return;
	g_folder = dir;
	std::vector<Mod> mods;
	for (const auto& entry : std::filesystem::directory_iterator(dir, error))
	{
		if (entry.path().extension() != ".edb") continue;
		Mod mod;
		if (ReadMod(entry.path(), mod)) mods.push_back(mod);
		else rt_log("goldeneye: mods: %s isn't an .edb of this game, left out", entry.path().string().c_str());
	}
	if (mods.empty()) return;
	std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.name < b.name; });

	uint64_t listAt = 0, listSize = 0, dataSize = 0;
	if (!disc_file("Filelist.bin", &listAt, &listSize) || !disc_file("Filelist.000", nullptr, &dataSize))
	{
		rt_log("goldeneye: mods: the disc has no Filelist.bin and Filelist.000");
		return;
	}
	std::vector<uint8_t> bin(listSize);
	disc_read(listAt, bin.data(), bin.size());
	std::vector<Row> rows;
	uint16_t buildType = 0, lists = 0;
	if (!ReadList(bin, rows, buildType, lists))
	{
		rt_log("goldeneye: mods: Filelist.bin isn't a version %u list", kListVersion);
		return;
	}

	//two mods with the same hash (two levels made with the same number): the second by name gets
	//the first free hash of its kind, counted up from where the made ones start. the file is
	//served with that hash in its header. one that has the hash of a file of the disc is meant
	//to take that file's place and keeps it
	std::set<uint32_t> taken;
	for (const Row& row : rows) taken.insert(row.hash);
	std::set<uint32_t> ofMods;
	for (const Mod& mod : mods) taken.insert(mod.hash);
	for (Mod& mod : mods)
	{
		if (ofMods.insert(mod.hash).second) continue;
		uint32_t fresh = (mod.hash & 0xFFFF0000u) | kFirstFree;
		while (taken.count(fresh) && (fresh & 0xFFFF) != 0xFFFF) ++fresh;
		if (taken.count(fresh)) { rt_log("goldeneye: mods: no hash left for %s", mod.name.c_str()); continue; }
		rt_log("goldeneye: mod %s has the hash of another mod (%08X): it is %08X here", mod.name.c_str(), mod.hash, fresh);
		mod.ownHash = mod.hash;
		mod.hash = fresh;
		taken.insert(fresh);
		ofMods.insert(fresh);
	}

	std::vector<DiscPart> parts;
	DiscPart original; original.original = true;
	parts.push_back(original);
	uint64_t end = dataSize;
	for (const Mod& mod : mods)
	{
		const uint64_t at = (end + kSector - 1) & ~(kSector - 1);
		if (at + mod.size > 0xFFFFFFFFull) { rt_log("goldeneye: mods: no room left for %s", mod.name.c_str()); continue; }
		DiscPart gap; gap.size = at - end;
		DiscPart file; file.path = mod.path; file.size = mod.size;
		parts.push_back(gap);
		if (mod.ownHash && mod.size > kHeadSize)
		{
			//the first eight bytes from here: GEOM and the new hash, the rest from the file
			DiscPart head;
			head.data = std::make_shared<std::vector<uint8_t>>();
			Put32(*head.data, kGeom); Put32(*head.data, mod.hash);
			parts.push_back(head);
			file.off = kHeadSize; file.size = mod.size - kHeadSize;
		}
		parts.push_back(file);
		end = at + mod.size;
		auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.hash == mod.hash; });
		const bool isNew = row == rows.end();
		if (isNew) { rows.push_back(Row{}); row = rows.end() - 1; row->name = kNamePrefix + mod.name; }
		row->size = mod.loaded; row->hash = mod.hash; row->version = mod.version; row->flags = mod.flags;
		row->places = { { uint32_t(at), 0 } };
		rt_log("goldeneye: mod %s: file %08X, %s", mod.name.c_str(), mod.hash, isNew ? "new" : "in place of the disc's");
		g_files.push_back({ mod.hash, mod.name });
		if ((mod.hash & 0xFFFF0000u) == kLevelFiles) g_levels.push_back({ mod.hash, mod.name });
	}
	if (!disc_replace("Filelist.000", parts) || !disc_replace("Filelist.bin", { DiscPart{ {}, 0, 0, WriteList(rows, buildType, lists) } }))
		rt_log("goldeneye: mods: the disc's file list couldn't be replaced");
}

} //namespace ge
