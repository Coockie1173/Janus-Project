//mods: loose .edb files take the place of the disc's, or come on top of them
//
//the game finds a file by its hash in Filelist.bin (version 7): a row for each file with the size
//of the part that stays loaded (the .edb header's +0x18), its hash (+4), version (+8), flags (+0xC)
//and where it starts in Filelist.000, then the names, scrambled by row and place
//a mod goes behind the end of Filelist.000 and gets the row of the file with its hash, or a new
//row at the end. a level is its mt_ file's hash: GE_LEVEL=hash starts a new one
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
#include <vector>

namespace ge
{
namespace
{

const uint32_t kListVersion = 7;
const uint32_t kNameKey = 0x16; //a name's byte is its letter - 0x16 + its row + its place
const uint64_t kSector = 0x800; //files start on one in Filelist.000
const char* kNamePrefix = "x:\\bondx\\binary\\_bin_wii\\";

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
	const bool ok = std::fread(head, 1, sizeof head, f) == sizeof head && Be32(head) == 0x47454F4D; //GEOM
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
const uint32_t kFileInfo = 0xC, kFileName = 0x10, kFileLoaded = 0x14, kInfoSize = 0x30, kInfoHash = 0x20;

void OnLoad(PPCContext& c)
{
	const uint32_t file = c.r[3];
	const bool pending = guest_ptr(file) && ld32(file + kFileLoaded) == 0;
	const uint32_t info = guest_ptr(file) ? ld32(file + kFileInfo) : 0;
	const uint32_t size = pending && guest_ptr(info) ? ld32(info + kInfoSize) : 0;
	const uint32_t name = guest_ptr(file) ? ld32(file + kFileName) : 0;
	orig_onload(c);
	static const bool trace = std::getenv("GE_TRACEFILES") != nullptr;
	if (trace) rt_log("goldeneye: file %s: %u bytes, loaded %u, memory %08X, returns %u", guest_ptr(name) ? (const char*)PPC_MEM(name) : "?", guest_ptr(info) ? ld32(info + kInfoSize) : 0, guest_ptr(file) ? ld32(file + kFileLoaded) : 0, guest_ptr(info) ? ld32(info + 0x3C) : 0, c.r[3]);
	if (pending && size && c.r[3] == 0)
		rt_log("goldeneye: no memory for the file %s (%u bytes), it isn't loaded", guest_ptr(name) ? (const char*)PPC_MEM(name) : "?", size);
}

} //namespace

void install_mods()
{
	orig_onload = ppc_hook("ge_geofile_onload", OnLoad);

	const char* env = std::getenv("GE_MODS");
	const std::string dir = env ? env : std::string(GE_ROOT) + "/mods";
	std::error_code error;
	if (dir.empty() || !std::filesystem::is_directory(dir, error)) return;
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
		parts.push_back(gap); parts.push_back(file);
		end = at + mod.size;
		auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.hash == mod.hash; });
		const bool isNew = row == rows.end();
		if (isNew) { rows.push_back(Row{}); row = rows.end() - 1; row->name = kNamePrefix + mod.name; }
		row->size = mod.loaded; row->hash = mod.hash; row->version = mod.version; row->flags = mod.flags;
		row->places = { { uint32_t(at), 0 } };
		rt_log("goldeneye: mod %s: file %08X, %s", mod.name.c_str(), mod.hash, isNew ? "new" : "in place of the disc's");
	}
	if (!disc_replace("Filelist.000", parts) || !disc_replace("Filelist.bin", { DiscPart{ {}, 0, 0, WriteList(rows, buildType, lists) } }))
		rt_log("goldeneye: mods: the disc's file list couldn't be replaced");
}

} //namespace ge
