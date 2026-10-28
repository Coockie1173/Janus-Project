//a modpack as a .zip: modpacks/NAME.zip is unpacked into modpacks/.unzipped/NAME (mods.cpp), again
//only when the zip changes (its size and time are kept in .zip-stamp there). the pack's files may
//be at the zip's top or in one folder that holds all of them
//
//a zip ends with its directory (the end record 06054B50: how many entries, where the directory
//starts), an entry there (02014B50) says how it is packed, its sizes, its CRC and where its local
//header (04034B50) is, the data after that header's name and extra field. stored (0) and deflate
//(8, RFC 1951) are read; zip64 and encrypted entries aren't
#include "ge.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kEndRecord = 0x06054B50, kDirEntry = 0x02014B50, kLocalHeader = 0x04034B50;
const size_t kEndSize = 22, kDirSize = 46, kLocalSize = 30, kMostComment = 0xFFFF;
const uint16_t kStored = 0, kDeflate = 8, kEncrypted = 1;
const char* const kStamp = ".zip-stamp";

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24; }

uint32_t Crc32(const std::vector<uint8_t>& data)
{
	static uint32_t table[256];
	if (!table[1])
		for (uint32_t i = 0; i < 256; ++i)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32_t crc = 0xFFFFFFFFu;
	for (uint8_t b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

//deflate: the bits come lowest first, a Huffman code highest bit first
struct Bits
{
	const uint8_t* in; size_t size, at;
	uint32_t buf; int count;
	bool bad;
};

int Take(Bits& s, int n)
{
	if (n == 0) return 0;
	uint32_t v = s.buf;
	while (s.count < n)
	{
		if (s.at >= s.size) { s.bad = true; return 0; }
		v |= uint32_t(s.in[s.at++]) << s.count;
		s.count += 8;
	}
	s.buf = v >> n;
	s.count -= n;
	return int(v & ((1u << n) - 1));
}

//a canonical code: how many codes of each length, the symbols in code order
struct Huffman { short count[16]; short symbol[288]; };

bool Build(Huffman& h, const short* lengths, int n)
{
	std::memset(h.count, 0, sizeof h.count);
	for (int i = 0; i < n; ++i) ++h.count[lengths[i]];
	if (h.count[0] == n) return true; //no codes at all: a block without distances
	int left = 1;
	for (int len = 1; len < 16; ++len)
	{
		left = left * 2 - h.count[len];
		if (left < 0) return false; //more codes than there's room for
	}
	short offset[16] = {};
	for (int len = 1; len < 15; ++len) offset[len + 1] = short(offset[len] + h.count[len]);
	for (int i = 0; i < n; ++i)
		if (lengths[i]) h.symbol[offset[lengths[i]]++] = short(i);
	return true;
}

int Decode(Bits& s, const Huffman& h)
{
	int code = 0, first = 0, index = 0;
	for (int len = 1; len < 16; ++len)
	{
		code |= Take(s, 1);
		const int count = h.count[len];
		if (code - count < first) return h.symbol[index + (code - first)];
		index += count;
		first = (first + count) << 1;
		code <<= 1;
	}
	return -1;
}

const short kLengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const short kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const short kDistanceBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const short kDistanceExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
const uint8_t kCodeOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

bool Codes(Bits& s, std::vector<uint8_t>& out, const Huffman& lengths, const Huffman& distances)
{
	for (;;)
	{
		const int symbol = Decode(s, lengths);
		if (symbol < 0 || s.bad) return false;
		if (symbol < 256) { out.push_back(uint8_t(symbol)); continue; }
		if (symbol == 256) return true;
		const int l = symbol - 257;
		if (l >= 29) return false;
		const int length = kLengthBase[l] + Take(s, kLengthExtra[l]);
		const int d = Decode(s, distances);
		if (d < 0 || d >= 30) return false;
		const size_t distance = size_t(kDistanceBase[d] + Take(s, kDistanceExtra[d]));
		if (s.bad || distance > out.size()) return false;
		for (int i = 0; i < length; ++i) out.push_back(out[out.size() - distance]);
	}
}

bool Inflate(const uint8_t* in, size_t size, std::vector<uint8_t>& out)
{
	Bits s{ in, size, 0, 0, 0, false };
	int last = 0;
	while (!last)
	{
		last = Take(s, 1);
		const int type = Take(s, 2);
		if (s.bad) return false;
		if (type == 0)
		{
			//stored: from the next byte, its length and the length's complement
			s.buf = 0; s.count = 0;
			if (s.at + 4 > s.size) return false;
			const uint16_t length = Le16(in + s.at), check = Le16(in + s.at + 2);
			s.at += 4;
			if (uint16_t(~check) != length || s.at + length > s.size) return false;
			out.insert(out.end(), in + s.at, in + s.at + length);
			s.at += length;
		}
		else if (type == 1)
		{
			static Huffman fixedLengths, fixedDistances;
			static bool made = false;
			if (!made)
			{
				short l[288];
				for (int i = 0; i < 288; ++i) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
				Build(fixedLengths, l, 288);
				for (int i = 0; i < 30; ++i) l[i] = 5;
				Build(fixedDistances, l, 30);
				made = true;
			}
			if (!Codes(s, out, fixedLengths, fixedDistances)) return false;
		}
		else if (type == 2)
		{
			const int nLengths = Take(s, 5) + 257, nDistances = Take(s, 5) + 1, nCodes = Take(s, 4) + 4;
			if (nLengths > 286 || nDistances > 30) return false;
			short l[320] = {};
			for (int i = 0; i < nCodes; ++i) l[kCodeOrder[i]] = short(Take(s, 3));
			Huffman code, lengths, distances;
			if (!Build(code, l, 19)) return false;
			std::fill(std::begin(l), std::end(l), 0);
			for (int i = 0; i < nLengths + nDistances;)
			{
				const int symbol = Decode(s, code);
				if (symbol < 0 || s.bad) return false;
				if (symbol < 16) { l[i++] = short(symbol); continue; }
				short value = 0;
				int repeat = 0;
				if (symbol == 16) { if (i == 0) return false; value = l[i - 1]; repeat = 3 + Take(s, 2); }
				else if (symbol == 17) repeat = 3 + Take(s, 3);
				else repeat = 11 + Take(s, 7);
				if (i + repeat > nLengths + nDistances) return false;
				while (repeat--) l[i++] = value;
			}
			if (l[256] == 0 || !Build(lengths, l, nLengths) || !Build(distances, l + nLengths, nDistances)) return false;
			if (!Codes(s, out, lengths, distances)) return false;
		}
		else return false;
	}
	return true;
}

struct Entry { std::string name; uint16_t method, flags; uint32_t crc, packed, size, local; };

bool ReadDirectory(const std::vector<uint8_t>& zip, std::vector<Entry>& entries)
{
	if (zip.size() < kEndSize) return false;
	size_t end = std::string::npos;
	for (size_t at = zip.size() - kEndSize + 1, stop = zip.size() > kEndSize + kMostComment ? zip.size() - kEndSize - kMostComment : 0; at-- > stop;)
		if (Le32(&zip[at]) == kEndRecord) { end = at; break; }
	if (end == std::string::npos) return false;
	const uint16_t count = Le16(&zip[end + 10]);
	size_t at = Le32(&zip[end + 16]);
	for (uint16_t i = 0; i < count; ++i)
	{
		if (at + kDirSize > zip.size() || Le32(&zip[at]) != kDirEntry) return false;
		const uint16_t nameLength = Le16(&zip[at + 28]), extra = Le16(&zip[at + 30]), comment = Le16(&zip[at + 32]);
		if (at + kDirSize + nameLength > zip.size()) return false;
		Entry e{ std::string((const char*)&zip[at + kDirSize], nameLength), Le16(&zip[at + 10]), Le16(&zip[at + 8]),
			Le32(&zip[at + 16]), Le32(&zip[at + 20]), Le32(&zip[at + 24]), Le32(&zip[at + 42]) };
		std::replace(e.name.begin(), e.name.end(), '\\', '/');
		entries.push_back(e);
		at += kDirSize + nameLength + extra + comment;
	}
	return true;
}

//a name that stays inside the folder: no root, no drive, no ..
bool Safe(const std::string& name)
{
	if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos) return false;
	for (size_t from = 0; from <= name.size();)
	{
		const size_t to = std::min(name.find('/', from), name.size());
		if (name.compare(from, to - from, "..") == 0 && to - from == 2) return false;
		from = to + 1;
	}
	return true;
}

std::string StampOf(const std::filesystem::path& zip)
{
	std::error_code error;
	const auto size = std::filesystem::file_size(zip, error);
	const auto time = std::filesystem::last_write_time(zip, error).time_since_epoch().count();
	return std::to_string(size) + " " + std::to_string((long long)time);
}

bool Unpack(const std::filesystem::path& zipPath, const std::filesystem::path& to)
{
	std::ifstream file(zipPath, std::ios::binary);
	const std::vector<uint8_t> zip((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::vector<Entry> entries;
	if (!ReadDirectory(zip, entries))
	{
		rt_log("goldeneye: modpacks: %s isn't a zip that can be read (a zip64 one isn't)", zipPath.string().c_str());
		return false;
	}
	//one folder around everything is left out
	std::string top;
	for (const Entry& e : entries)
	{
		const size_t slash = e.name.find('/');
		const std::string first = slash == std::string::npos ? std::string() : e.name.substr(0, slash + 1);
		if (first.empty() || (!top.empty() && first != top)) { top.clear(); break; }
		top = first;
	}
	size_t files = 0;
	for (const Entry& e : entries)
	{
		const std::string name = e.name.substr(top.size());
		if (name.empty() || name.back() == '/') continue;
		if (!Safe(name)) { rt_log("goldeneye: modpacks: %s: %s would land outside the pack, left out", zipPath.filename().string().c_str(), e.name.c_str()); continue; }
		if (e.flags & kEncrypted || (e.method != kStored && e.method != kDeflate))
		{
			rt_log("goldeneye: modpacks: %s: %s is %s, left out (stored or deflate only)", zipPath.filename().string().c_str(), e.name.c_str(), e.flags & kEncrypted ? "encrypted" : "packed another way");
			continue;
		}
		if (size_t(e.local) + kLocalSize > zip.size() || Le32(&zip[e.local]) != kLocalHeader) return false;
		const size_t data = e.local + kLocalSize + Le16(&zip[e.local + 26]) + Le16(&zip[e.local + 28]);
		if (data + e.packed > zip.size()) return false;
		std::vector<uint8_t> bytes;
		bytes.reserve(e.size);
		if (e.method == kStored) bytes.assign(zip.begin() + (ptrdiff_t)data, zip.begin() + (ptrdiff_t)(data + e.packed));
		else if (!Inflate(&zip[data], e.packed, bytes)) { rt_log("goldeneye: modpacks: %s: %s couldn't be unpacked", zipPath.filename().string().c_str(), e.name.c_str()); return false; }
		if (bytes.size() != e.size || Crc32(bytes) != e.crc) { rt_log("goldeneye: modpacks: %s: %s doesn't match its CRC", zipPath.filename().string().c_str(), e.name.c_str()); return false; }
		const std::filesystem::path out = to / std::filesystem::path(name);
		std::error_code error;
		std::filesystem::create_directories(out.parent_path(), error);
		std::ofstream write(out, std::ios::binary);
		write.write((const char*)bytes.data(), (std::streamsize)bytes.size());
		if (!write) { rt_log("goldeneye: modpacks: %s couldn't be written", out.string().c_str()); return false; }
		++files;
	}
	rt_log("goldeneye: modpacks: %s unpacked, %zu file(s)", zipPath.filename().string().c_str(), files);
	return true;
}

} //namespace

//the zip's files in the folder, unless they are there from this very zip already
bool unzip_pack(const std::string& zip, const std::string& folder)
{
	const std::filesystem::path to = folder, stamp = to / kStamp;
	const std::string now = StampOf(zip);
	std::string had;
	std::getline(std::ifstream(stamp), had);
	if (had == now) return true;
	std::error_code error;
	std::filesystem::remove_all(to, error);
	std::filesystem::create_directories(to, error);
	if (!Unpack(zip, to)) { std::filesystem::remove_all(to, error); return false; }
	std::ofstream(stamp) << now << "\n";
	return true;
}

} //namespace ge
