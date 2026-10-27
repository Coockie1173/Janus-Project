#include "disc.h"
#include "files.h"
#include <Lzma2Dec.h>
#include <LzmaDec.h>
#include <bzlib.h>
#include <openssl/evp.h>
#include <zstd.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>

namespace launcher
{
namespace
{
	const uint32_t kWiiMagic{ 0x5D1C9EA3 };
	const uint64_t kSector{ 0x8000 };
	const uint64_t kPayload{ 0x7C00 };
	const uint64_t kHashArea{ 0x400 };
	const uint64_t kWiiSectorsDualLayer{ 143432 * 2 };

	//the public wii common keys: retail, korean, vwii
	const uint8_t kCommonKeys[3][16]{
		{ 0xEB, 0xE4, 0x2A, 0x22, 0x5E, 0x85, 0x93, 0xE4, 0x48, 0xD9, 0xC5, 0x45, 0x73, 0x81, 0xAA, 0xF7 },
		{ 0x63, 0xB8, 0x2B, 0xB4, 0xF4, 0x61, 0x4E, 0x2E, 0x13, 0xF2, 0xFE, 0xFB, 0xBA, 0x4C, 0x9B, 0x7E },
		{ 0x30, 0xBF, 0xC7, 0x6E, 0x7C, 0x19, 0xAF, 0xBB, 0x23, 0x16, 0x33, 0x30, 0xCE, 0xD7, 0xC2, 0x8D },
	};

	uint64_t Be64(const uint8_t* p)
	{
		return (uint64_t)Be32(p) << 32 | Be32(p + 4);
	}

	uint16_t Be16(const uint8_t* p)
	{
		return (uint16_t)(p[0] << 8 | p[1]);
	}

	bool CbcDecrypt(const uint8_t* pKey, const uint8_t* pIv, const uint8_t* pIn, uint8_t* pOut, size_t Size)
	{
		EVP_CIPHER_CTX* pCtx{ EVP_CIPHER_CTX_new() };
		int Len{};
		const bool Ok{ pCtx != nullptr && EVP_DecryptInit_ex(pCtx, EVP_aes_128_cbc(), nullptr, pKey, pIv) == 1
			&& EVP_CIPHER_CTX_set_padding(pCtx, 0) == 1 && EVP_DecryptUpdate(pCtx, pOut, &Len, pIn, (int)Size) == 1
			&& (size_t)Len == Size };
		EVP_CIPHER_CTX_free(pCtx);
		return Ok;
	}

	class FileImage : public Image
	{
	public:
		bool Open(const std::string& Path)
		{
			m_File.open(FsPath(Path), std::ios::binary);
			return m_File.is_open();
		}

		bool ReadFile(uint64_t Offset, uint8_t* pOut, size_t Size)
		{
			m_File.clear();
			m_File.seekg((std::streamoff)Offset);
			m_File.read((char*)pOut, (std::streamsize)Size);
			return (size_t)m_File.gcount() == Size;
		}

		bool Read(uint64_t Offset, uint8_t* pOut, size_t Size) override
		{
			return ReadFile(Offset, pOut, Size);
		}

		const char* Kind() const override { return "ISO"; }

	protected:
		std::ifstream m_File{};
	};

	//a usb loader's sparse container: the disc cut into wbfs sectors, a u16 table maps each to
	//one of the file's (0: not stored, reads as zeros)
	class WbfsImage : public FileImage
	{
	public:
		bool Init(std::string& Error)
		{
			uint8_t Head[0x100]{};
			if (!ReadFile(0, Head, sizeof Head) || std::memcmp(Head, "WBFS", 4) != 0)
			{
				Error = "not a WBFS file";
				return false;
			}
			m_HdSector = 1ull << Head[8];
			m_Sector = 1ull << Head[9];
			if (Head[0xC] == 0)
			{
				Error = "the WBFS file holds no disc";
				return false;
			}
			const uint64_t Count{ kWiiSectorsDualLayer / (m_Sector / kSector) };
			std::vector<uint8_t> Table(Count * 2);
			if (!ReadFile(m_HdSector + 0x100, Table.data(), Table.size()))
			{
				Error = "the WBFS file is cut short";
				return false;
			}
			m_Map.resize(Count);
			for (uint64_t i{}; i < Count; ++i)
			{
				m_Map[i] = Be16(&Table[i * 2]);
			}
			return true;
		}

		bool Read(uint64_t Offset, uint8_t* pOut, size_t Size) override
		{
			while (Size > 0)
			{
				const uint64_t Index{ Offset / m_Sector }, Within{ Offset % m_Sector };
				if (Index >= m_Map.size())
				{
					return false;
				}
				const size_t Now{ (size_t)std::min<uint64_t>(Size, m_Sector - Within) };
				if (m_Map[Index] == 0)
				{
					std::memset(pOut, 0, Now);
				}
				else if (!ReadFile(m_Map[Index] * m_Sector + Within, pOut, Now))
				{
					return false;
				}
				pOut += Now;
				Offset += Now;
				Size -= Now;
			}
			return true;
		}

		const char* Kind() const override { return "WBFS"; }

	private:
		uint64_t m_HdSector{};
		uint64_t m_Sector{};
		std::vector<uint16_t> m_Map{};
	};

	//the junk nintendo's mastering fills unused space with (dolphin's LaggedFibonacciGenerator),
	//rvz keeps the 68 byte seed instead of the bytes
	class LaggedFibonacci
	{
	public:
		explicit LaggedFibonacci(const uint8_t* pSeed)
		{
			for (int i{}; i < kSeed; ++i)
			{
				m_Buffer[i] = Be32(pSeed + i * 4);
			}
			for (int i{ kSeed }; i < kK; ++i)
			{
				m_Buffer[i] = (m_Buffer[i - 17] << 23) ^ (m_Buffer[i - 16] >> 9) ^ m_Buffer[i - 1];
			}
			for (uint32_t& Word : m_Buffer)
			{
				Word = (Word & 0xFF00FFFF) | ((Word >> 2) & 0x00FF0000);
			}
			for (int i{}; i < 4; ++i)
			{
				Forward();
			}
		}

		void Skip(uint64_t Count)
		{
			m_Position += Count;
			while (m_Position >= 4 * kK)
			{
				Forward();
				m_Position -= 4 * kK;
			}
		}

		void Read(uint8_t* pOut, size_t Count)
		{
			for (size_t i{}; i < Count; ++i)
			{
				pOut[i] = (uint8_t)(m_Buffer[m_Position / 4] >> (24 - 8 * (m_Position % 4)));
				if (++m_Position == 4 * kK)
				{
					Forward();
					m_Position = 0;
				}
			}
		}

	private:
		static const int kK{ 521 };
		static const int kJ{ 32 };
		static const int kSeed{ 17 };

		void Forward()
		{
			for (int i{}; i < kJ; ++i)
			{
				m_Buffer[i] ^= m_Buffer[i + kK - kJ];
			}
			for (int i{ kJ }; i < kK; ++i)
			{
				m_Buffer[i] ^= m_Buffer[i - kJ];
			}
		}

		uint32_t m_Buffer[kK]{};
		uint64_t m_Position{};
	};

	void* LzmaAlloc(ISzAllocPtr, size_t Size) { return std::malloc(Size); }
	void LzmaFree(ISzAllocPtr, void* p) { std::free(p); }
	const ISzAlloc kLzmaAlloc{ LzmaAlloc, LzmaFree };

	//dolphin's compressed formats: everything outside the partitions' data as compressed raw
	//groups, the partitions' payload decrypted in groups of its own
	class RvzImage : public FileImage
	{
	public:
		bool Init(std::string& Error)
		{
			uint8_t Head[0x48]{};
			if (!ReadFile(0, Head, sizeof Head) || (std::memcmp(Head, "WIA\x01", 4) != 0 && std::memcmp(Head, "RVZ\x01", 4) != 0))
			{
				Error = "not a WIA or RVZ file";
				return false;
			}
			m_Rvz = Head[0] == 'R';
			std::vector<uint8_t> D(Be32(Head + 0x0C));
			if (D.size() < 0xDC || !ReadFile(0x48, D.data(), D.size()))
			{
				Error = "the file is cut short";
				return false;
			}
			m_Compression = Be32(&D[4]);
			m_Chunk = Be32(&D[0xC]);
			std::memcpy(m_DiscHead, &D[0x10], sizeof m_DiscHead);
			m_Props.assign(&D[0xD5], &D[0xD5] + std::min<size_t>(D[0xD4], 7));
			if (m_Compression == 1)
			{
				Error = "WIA files with purge compression aren't supported: convert it to RVZ or ISO with Dolphin";
				return false;
			}
			if (m_Compression > 5 || m_Chunk < kSector)
			{
				Error = "an unknown kind of RVZ/WIA file";
				return false;
			}

			const uint32_t PartCount{ Be32(&D[0x90]) }, PartSize{ Be32(&D[0x94]) };
			std::vector<uint8_t> Parts((size_t)PartCount * PartSize);
			if (PartSize < 48 || !ReadFile(Be64(&D[0x98]), Parts.data(), Parts.size()))
			{
				Error = "the partition table can't be read";
				return false;
			}
			for (uint32_t i{}; i < PartCount; ++i)
			{
				const uint8_t* p{ &Parts[(size_t)i * PartSize + 16] };
				PartEntry Part{};
				for (int k{}; k < 2; ++k)
				{
					Part.Ranges[k] = { Be32(p + k * 16), Be32(p + k * 16 + 4), Be32(p + k * 16 + 8), Be32(p + k * 16 + 12) };
				}
				m_Parts.push_back(Part);
			}

			std::vector<uint8_t> Raw;
			if (!ReadTable(Be64(&D[0xB8]), Be32(&D[0xC0]), Raw))
			{
				Error = "the raw data table can't be read";
				return false;
			}
			for (uint32_t i{}; i < Be32(&D[0xB4]) && (i + 1) * 24 <= Raw.size(); ++i)
			{
				const uint64_t Offset{ Be64(&Raw[i * 24]) };
				m_Raw.push_back({ Offset - Offset % kSector, Offset + Be64(&Raw[i * 24 + 8]), Be32(&Raw[i * 24 + 16]) });
			}

			std::vector<uint8_t> Groups;
			if (!ReadTable(Be64(&D[0xC8]), Be32(&D[0xD0]), Groups))
			{
				Error = "the group table can't be read";
				return false;
			}
			const size_t Step{ m_Rvz ? 12u : 8u };
			for (size_t i{}; i < Be32(&D[0xC4]) && (i + 1) * Step <= Groups.size(); ++i)
			{
				const uint8_t* p{ &Groups[i * Step] };
				m_Groups.push_back({ (uint64_t)Be32(p) << 2, Be32(p + 4), m_Rvz ? Be32(p + 8) : 0 });
			}
			return true;
		}

		bool Read(uint64_t Offset, uint8_t* pOut, size_t Size) override
		{
			while (Size > 0)
			{
				const size_t Done{ ReadRaw(Offset, pOut, Size) };
				if (Done == 0)
				{
					return false;
				}
				pOut += Done;
				Offset += Done;
				Size -= Done;
			}
			return true;
		}

		std::function<bool(uint64_t, uint8_t*, size_t)> Payload(uint64_t DataStart) override
		{
			const uint64_t Start{ DataStart / kSector };
			for (const PartEntry& Part : m_Parts)
			{
				if (Part.Ranges[0].First == Start)
				{
					return [this, Start, Part](uint64_t Offset, uint8_t* pOut, size_t Size) { return ReadPayload(Start, Part, Offset, pOut, Size); };
				}
			}
			return {};
		}

		const char* Kind() const override { return m_Rvz ? "RVZ" : "WIA"; }

	private:
		struct Range
		{
			uint32_t First{}; //in disc sectors
			uint32_t Count{};
			uint32_t Group{};
			uint32_t Groups{};
		};
		struct PartEntry
		{
			Range Ranges[2]{};
		};
		struct RawEntry
		{
			uint64_t Start{};
			uint64_t End{};
			uint32_t Group{};
		};
		struct GroupEntry
		{
			uint64_t Offset{};
			uint32_t Size{}; //top bit on rvz: compressed
			uint32_t Packed{};
		};

		bool ReadTable(uint64_t Offset, uint32_t Size, std::vector<uint8_t>& Out)
		{
			std::vector<uint8_t> Data(Size);
			return ReadFile(Offset, Data.data(), Data.size()) && Decompress(Data, Out);
		}

		bool Decompress(const std::vector<uint8_t>& In, std::vector<uint8_t>& Out)
		{
			Out.clear();
			switch (m_Compression)
			{
			case 0:
				Out = In;
				return true;
			case 2:
				return Bzip2(In, Out);
			case 3:
			case 4:
				return Lzma(In, Out, m_Compression == 4);
			case 5:
				return Zstd(In, Out);
			}
			return false;
		}

		static bool Zstd(const std::vector<uint8_t>& In, std::vector<uint8_t>& Out)
		{
			ZSTD_DStream* pStream{ ZSTD_createDStream() };
			ZSTD_inBuffer Input{ In.data(), In.size(), 0 };
			size_t Result{ 1 };
			while (Result != 0 && !ZSTD_isError(Result))
			{
				const size_t Had{ Out.size() };
				Out.resize(Had + (1 << 18));
				ZSTD_outBuffer Output{ Out.data() + Had, 1 << 18, 0 };
				Result = ZSTD_decompressStream(pStream, &Output, &Input);
				Out.resize(Had + Output.pos);
				if (Input.pos == Input.size && Output.pos < Output.size)
				{
					break;
				}
			}
			ZSTD_freeDStream(pStream);
			return !ZSTD_isError(Result);
		}

		static bool Bzip2(const std::vector<uint8_t>& In, std::vector<uint8_t>& Out)
		{
			bz_stream Stream{};
			if (BZ2_bzDecompressInit(&Stream, 0, 0) != BZ_OK)
			{
				return false;
			}
			Stream.next_in = (char*)In.data();
			Stream.avail_in = (unsigned)In.size();
			int Result{ BZ_OK };
			while (Result == BZ_OK)
			{
				const size_t Had{ Out.size() };
				Out.resize(Had + (1 << 18));
				Stream.next_out = (char*)Out.data() + Had;
				Stream.avail_out = 1 << 18;
				Result = BZ2_bzDecompress(&Stream);
				Out.resize(Out.size() - Stream.avail_out);
				if (Result == BZ_OK && Stream.avail_in == 0 && Stream.avail_out != 0)
				{
					break;
				}
			}
			BZ2_bzDecompressEnd(&Stream);
			return Result == BZ_OK || Result == BZ_STREAM_END;
		}

		//raw lzma or lzma2 with the properties from the file's head, as wia writes them
		bool Lzma(const std::vector<uint8_t>& In, std::vector<uint8_t>& Out, bool Two)
		{
			CLzmaDec Lzma1;
			CLzma2Dec Lzma2;
			if (Two)
			{
				Lzma2Dec_CONSTRUCT(&Lzma2);
				if (m_Props.empty() || Lzma2Dec_Allocate(&Lzma2, m_Props[0], &kLzmaAlloc) != SZ_OK)
				{
					return false;
				}
				Lzma2Dec_Init(&Lzma2);
			}
			else
			{
				LzmaDec_CONSTRUCT(&Lzma1);
				if (m_Props.size() < LZMA_PROPS_SIZE || LzmaDec_Allocate(&Lzma1, m_Props.data(), LZMA_PROPS_SIZE, &kLzmaAlloc) != SZ_OK)
				{
					return false;
				}
				LzmaDec_Init(&Lzma1);
			}
			size_t At{};
			bool Ok{ true };
			while (Ok)
			{
				const size_t Had{ Out.size() };
				Out.resize(Had + (1 << 18));
				SizeT OutSize{ 1 << 18 }, InSize{ In.size() - At };
				ELzmaStatus Status{};
				const SRes Result{ Two ? Lzma2Dec_DecodeToBuf(&Lzma2, Out.data() + Had, &OutSize, In.data() + At, &InSize, LZMA_FINISH_ANY, &Status)
					: LzmaDec_DecodeToBuf(&Lzma1, Out.data() + Had, &OutSize, In.data() + At, &InSize, LZMA_FINISH_ANY, &Status) };
				At += InSize;
				Out.resize(Had + OutSize);
				Ok = Result == SZ_OK;
				if (Status == LZMA_STATUS_FINISHED_WITH_MARK || (OutSize == 0 && InSize == 0))
				{
					break;
				}
			}
			if (Two)
			{
				Lzma2Dec_Free(&Lzma2, &kLzmaAlloc);
			}
			else
			{
				LzmaDec_Free(&Lzma1, &kLzmaAlloc);
			}
			return Ok;
		}

		//one group: chunk bytes of raw disc (Sectors 0), or that many sectors of payload
		const std::vector<uint8_t>* Group(uint32_t Index, uint64_t DataOffset, uint64_t Sectors)
		{
			const auto Found{ m_Cache.find(Index) };
			if (Found != m_Cache.end())
			{
				return &Found->second;
			}
			if (Index >= m_Groups.size())
			{
				return nullptr;
			}
			const GroupEntry& Entry{ m_Groups[Index] };
			const size_t Want{ (size_t)(Sectors ? Sectors * kPayload : m_Chunk) };
			std::vector<uint8_t> Out;
			if ((Entry.Size & 0x7FFFFFFF) == 0)
			{
				Out.assign(Want, 0);
			}
			else
			{
				const bool Compressed{ m_Rvz ? (Entry.Size >> 31) != 0 : m_Compression > 1 };
				std::vector<uint8_t> Data(Entry.Size & 0x7FFFFFFF);
				if (!ReadFile(Entry.Offset, Data.data(), Data.size()))
				{
					return nullptr;
				}
				if (Compressed)
				{
					std::vector<uint8_t> Plain;
					if (!Decompress(Data, Plain))
					{
						return nullptr;
					}
					Data.swap(Plain);
				}
				size_t At{};
				if (Sectors != 0)
				{
					//the hash exceptions, a list for each 2 MiB of chunk
					for (uint64_t i{}; i < std::max<uint64_t>(1, m_Chunk / (kSector * 64)); ++i)
					{
						if (At + 2 > Data.size())
						{
							return nullptr;
						}
						At += 2 + 22 * (size_t)Be16(&Data[At]);
					}
					if (!Compressed)
					{
						At = (At + 3) & ~(size_t)3;
					}
				}
				if (At > Data.size())
				{
					return nullptr;
				}
				if (Entry.Packed != 0)
				{
					Unpack(Data, At, DataOffset, Out);
				}
				else
				{
					Out.assign(Data.begin() + (std::ptrdiff_t)At, Data.end());
				}
			}
			if (m_Cache.size() > 64)
			{
				m_Cache.clear();
			}
			return &(m_Cache[Index] = std::move(Out));
		}

		//rvz packing: runs of u32 size + bytes, a size with the top bit set is junk from a seed
		static void Unpack(const std::vector<uint8_t>& Data, size_t At, uint64_t DataOffset, std::vector<uint8_t>& Out)
		{
			while (At + 4 <= Data.size())
			{
				uint32_t Size{ Be32(&Data[At]) };
				At += 4;
				const size_t Had{ Out.size() };
				if (Size >> 31)
				{
					Size &= 0x7FFFFFFF;
					if (At + 68 > Data.size())
					{
						return;
					}
					LaggedFibonacci Junk{ &Data[At] };
					At += 68;
					Junk.Skip(DataOffset % kSector);
					Out.resize(Had + Size);
					Junk.Read(&Out[Had], Size);
				}
				else
				{
					Size = (uint32_t)std::min<size_t>(Size, Data.size() - At);
					Out.insert(Out.end(), Data.begin() + (std::ptrdiff_t)At, Data.begin() + (std::ptrdiff_t)(At + Size));
					At += Size;
				}
				DataOffset += Size;
			}
		}

		//some of a read from Offset, 0 when it can't be read
		size_t ReadRaw(uint64_t Offset, uint8_t* pOut, size_t Size)
		{
			if (Offset < sizeof m_DiscHead)
			{
				const size_t Now{ std::min<size_t>(Size, sizeof m_DiscHead - Offset) };
				std::memcpy(pOut, m_DiscHead + Offset, Now);
				return Now;
			}
			for (const RawEntry& Raw : m_Raw)
			{
				if (Raw.Start <= Offset && Offset < Raw.End)
				{
					const uint64_t Index{ (Offset - Raw.Start) / m_Chunk }, Within{ (Offset - Raw.Start) % m_Chunk };
					const std::vector<uint8_t>* pGroup{ Group(Raw.Group + (uint32_t)Index, Offset - Within, 0) };
					if (pGroup == nullptr)
					{
						return 0;
					}
					const size_t Now{ (size_t)std::min<uint64_t>({ (uint64_t)Size, Raw.End - Offset, m_Chunk - Within }) };
					const size_t Have{ Within < pGroup->size() ? std::min<size_t>(Now, pGroup->size() - Within) : 0 };
					std::memcpy(pOut, pGroup->data() + Within, Have);
					std::memset(pOut + Have, 0, Now - Have);
					return Now;
				}
			}
			for (const PartEntry& Part : m_Parts)
			{
				for (const Range& R : Part.Ranges)
				{
					if (R.First * kSector <= Offset && Offset < (uint64_t)(R.First + R.Count) * kSector)
					{
						return 0; //encrypted data that isn't kept: read it through Partition
					}
				}
			}
			//not covered by anything: zeros up to the next region
			uint64_t Next{ Offset + Size };
			for (const RawEntry& Raw : m_Raw)
			{
				if (Raw.Start > Offset)
				{
					Next = std::min(Next, Raw.Start);
				}
			}
			for (const PartEntry& Part : m_Parts)
			{
				if (Part.Ranges[0].First * kSector > Offset)
				{
					Next = std::min(Next, Part.Ranges[0].First * kSector);
				}
			}
			std::memset(pOut, 0, (size_t)(Next - Offset));
			return (size_t)(Next - Offset);
		}

		bool ReadPayload(uint64_t Start, const PartEntry& Part, uint64_t Offset, uint8_t* pOut, size_t Size)
		{
			const uint64_t PerGroup{ m_Chunk / kSector };
			while (Size > 0)
			{
				const uint64_t Sector{ Offset / kPayload + Start }, Within{ Offset % kPayload };
				size_t Now{};
				for (const Range& R : Part.Ranges)
				{
					if (R.First <= Sector && Sector < (uint64_t)R.First + R.Count)
					{
						const uint64_t Index{ (Sector - R.First) / PerGroup }, InGroup{ (Sector - R.First) % PerGroup };
						const uint64_t Sectors{ std::min<uint64_t>(PerGroup, R.Count - Index * PerGroup) };
						const uint64_t Base{ (R.First + Index * PerGroup - Start) * kPayload };
						const std::vector<uint8_t>* pGroup{ Group(R.Group + (uint32_t)Index, Base, Sectors) };
						const uint64_t At{ InGroup * kPayload + Within };
						if (pGroup == nullptr || At >= pGroup->size())
						{
							return false;
						}
						Now = (size_t)std::min<uint64_t>({ (uint64_t)Size, Sectors * kPayload - At, pGroup->size() - At });
						std::memcpy(pOut, pGroup->data() + At, Now);
						break;
					}
				}
				if (Now == 0)
				{
					return false; //past the data area
				}
				pOut += Now;
				Offset += Now;
				Size -= Now;
			}
			return true;
		}

		bool m_Rvz{};
		uint32_t m_Compression{};
		uint64_t m_Chunk{};
		uint8_t m_DiscHead[0x80]{};
		std::vector<uint8_t> m_Props{};
		std::vector<PartEntry> m_Parts{};
		std::vector<RawEntry> m_Raw{};
		std::vector<GroupEntry> m_Groups{};
		std::map<uint32_t, std::vector<uint8_t>> m_Cache{};
	};
}

uint32_t Be32(const uint8_t* p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

std::unique_ptr<Image> OpenImage(const std::string& Path, std::string& Error)
{
	char Magic[4]{};
	{
		std::ifstream File{ FsPath(Path), std::ios::binary };
		if (!File.is_open())
		{
			Error = "the file can't be opened";
			return nullptr;
		}
		File.read(Magic, 4);
	}
	if (std::memcmp(Magic, "WBFS", 4) == 0)
	{
		auto pImage{ std::make_unique<WbfsImage>() };
		if (pImage->Open(Path) && pImage->Init(Error))
		{
			return pImage;
		}
	}
	else if (std::memcmp(Magic, "RVZ\x01", 4) == 0 || std::memcmp(Magic, "WIA\x01", 4) == 0)
	{
		auto pImage{ std::make_unique<RvzImage>() };
		if (pImage->Open(Path) && pImage->Init(Error))
		{
			return pImage;
		}
	}
	else
	{
		auto pImage{ std::make_unique<FileImage>() };
		if (pImage->Open(Path))
		{
			return pImage;
		}
	}
	if (Error.empty())
	{
		Error = "the file can't be read";
	}
	return nullptr;
}

bool ReadHeader(Image& Disc, DiscHeader& Header)
{
	uint8_t Head[0x60]{};
	if (!Disc.Read(0, Head, sizeof Head))
	{
		return false;
	}
	Header.GameId.assign((const char*)Head, 6);
	Header.Title.assign((const char*)Head + 0x20, strnlen((const char*)Head + 0x20, 0x40));
	Header.Wii = Be32(Head + 0x18) == kWiiMagic;
	return true;
}

bool Partition::Open(Image& Disc, std::string& Error)
{
	m_pDisc = &Disc;
	//four partition groups at 0x40000, each a count and a table of (offset >> 2, type)
	uint8_t Groups[0x20]{};
	if (!Disc.Read(0x40000, Groups, sizeof Groups))
	{
		Error = "the partition table can't be read";
		return false;
	}
	bool Found{};
	for (int g{}; g < 4 && !Found; ++g)
	{
		const uint32_t Count{ Be32(Groups + g * 8) };
		std::vector<uint8_t> Table(Count * 8);
		if (Count == 0 || Count > 64 || !Disc.Read((uint64_t)Be32(Groups + g * 8 + 4) << 2, Table.data(), Table.size()))
		{
			continue;
		}
		for (uint32_t i{}; i < Count; ++i)
		{
			if (Be32(&Table[i * 8 + 4]) == 0) //DATA
			{
				m_Base = (uint64_t)Be32(&Table[i * 8]) << 2;
				Found = true;
				break;
			}
		}
	}
	if (!Found)
	{
		Error = "the disc has no data partition";
		return false;
	}
	uint8_t Ticket[0x2C0]{};
	if (!Disc.Read(m_Base, Ticket, sizeof Ticket))
	{
		Error = "the partition's ticket can't be read";
		return false;
	}
	uint8_t Iv[16]{};
	std::memcpy(Iv, Ticket + 0x1DC, 8);
	const uint8_t* pCommon{ kCommonKeys[Ticket[0x1F1] < 3 ? Ticket[0x1F1] : 0] };
	if (!CbcDecrypt(pCommon, Iv, Ticket + 0x1BF, m_TitleKey, 16))
	{
		Error = "the title key can't be decrypted";
		return false;
	}
	m_DataOffset = (uint64_t)Be32(Ticket + 0x2B8) << 2;
	m_Ticket.assign(Ticket, Ticket + 0x2A4);
	m_Tmd.resize(Be32(Ticket + 0x2A4));
	if (!Disc.Read(m_Base + ((uint64_t)Be32(Ticket + 0x2A8) << 2), m_Tmd.data(), m_Tmd.size()))
	{
		Error = "the partition's TMD can't be read";
		return false;
	}
	m_Direct = Disc.Payload(m_Base + m_DataOffset);
	return true;
}

bool Partition::Cluster(uint64_t Index)
{
	if (Index == m_ClusterIndex)
	{
		return true;
	}
	std::vector<uint8_t> Raw(kSector);
	m_ClusterData.resize(kSector - kHashArea);
	m_ClusterIndex = ~0ull;
	if (!m_pDisc->Read(m_Base + m_DataOffset + Index * kSector, Raw.data(), Raw.size())
		|| !CbcDecrypt(m_TitleKey, &Raw[0x3D0], &Raw[kHashArea], m_ClusterData.data(), m_ClusterData.size()))
	{
		return false;
	}
	m_ClusterIndex = Index;
	return true;
}

bool Partition::Read(uint64_t Offset, uint8_t* pOut, size_t Size)
{
	if (m_Direct)
	{
		return m_Direct(Offset, pOut, Size);
	}
	while (Size > 0)
	{
		const uint64_t Index{ Offset / kPayload }, Within{ Offset % kPayload };
		if (!Cluster(Index))
		{
			return false;
		}
		const size_t Now{ (size_t)std::min<uint64_t>(Size, kPayload - Within) };
		std::memcpy(pOut, m_ClusterData.data() + Within, Now);
		pOut += Now;
		Offset += Now;
		Size -= Now;
	}
	return true;
}

std::vector<uint8_t> Partition::Read(uint64_t Offset, size_t Size)
{
	std::vector<uint8_t> Out(Size);
	if (!Read(Offset, Out.data(), Size))
	{
		Out.clear();
	}
	return Out;
}

bool Partition::Files(std::vector<FstFile>& Out)
{
	//the fst: 12 byte entries (is-dir, u24 name offset, then offset >> 2 and size, or parent
	//and the index past the last child), entry 0's size is the count, the names follow
	const std::vector<uint8_t> Boot{ Read(0, 0x440) };
	if (Boot.empty())
	{
		return false;
	}
	const std::vector<uint8_t> Fst{ Read((uint64_t)Be32(&Boot[0x424]) << 2, (size_t)Be32(&Boot[0x428]) << 2) };
	if (Fst.size() < 12)
	{
		return false;
	}
	const uint32_t Total{ Be32(&Fst[8]) };
	const size_t Names{ (size_t)Total * 12 };
	if (Names > Fst.size())
	{
		return false;
	}
	std::vector<std::pair<uint32_t, std::string>> Stack{ { Total, "" } };
	for (uint32_t i{ 1 }; i < Total; ++i)
	{
		const uint8_t* e{ &Fst[i * 12] };
		while (Stack.size() > 1 && i >= Stack.back().first)
		{
			Stack.pop_back();
		}
		const size_t At{ Names + (Be32(e) & 0xFFFFFF) };
		if (At >= Fst.size())
		{
			return false;
		}
		const std::string Name{ (const char*)&Fst[At], strnlen((const char*)&Fst[At], Fst.size() - At) };
		if (e[0] != 0)
		{
			Stack.push_back({ Be32(e + 8), Stack.back().second + Name + "/" });
			Out.push_back({ Stack.back().second, 0, 0 });
		}
		else
		{
			Out.push_back({ Stack.back().second + Name, (uint64_t)Be32(e + 4) << 2, Be32(e + 8) });
		}
	}
	return true;
}
}
