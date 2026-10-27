//wii disc images: .iso, .wbfs, .rvz and .wia, their data partition and its files
//
//the same reading as wiikit's disc.py and rvz.py (their docstrings have the layouts), without
//the gamecube side: the launcher only ever wants one wii game out of them
#ifndef LAUNCHER_DISC_H
#define LAUNCHER_DISC_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace launcher
{
	//a disc image seen as the disc's own bytes
	class Image
	{
	public:
		virtual ~Image() = default;
		//raw disc bytes, false past the end or on a read error
		virtual bool Read(uint64_t Offset, uint8_t* pOut, size_t Size) = 0;
		//rvz and wia keep a partition's payload decrypted: a reader for the partition whose data
		//starts at that disc offset, or an empty one when it has to be decrypted from Read
		virtual std::function<bool(uint64_t, uint8_t*, size_t)> Payload(uint64_t DataStart)
		{
			return {};
		}
		virtual const char* Kind() const = 0;
	};

	//nullptr and Error filled when it isn't an image this can read
	std::unique_ptr<Image> OpenImage(const std::string& Path, std::string& Error);

	struct DiscHeader
	{
		std::string GameId{};
		std::string Title{};
		bool Wii{};
	};
	bool ReadHeader(Image& Disc, DiscHeader& Header);

	struct FstFile
	{
		std::string Path{};
		uint64_t Offset{};
		uint64_t Size{};
	};

	//the decrypted payload of a disc's data partition
	class Partition
	{
	public:
		bool Open(Image& Disc, std::string& Error);
		bool Read(uint64_t Offset, uint8_t* pOut, size_t Size);
		std::vector<uint8_t> Read(uint64_t Offset, size_t Size);

		//the folders come first in their tree, with an empty Size and a / at the end
		bool Files(std::vector<FstFile>& Out);
		const std::vector<uint8_t>& Ticket() const { return m_Ticket; }
		const std::vector<uint8_t>& Tmd() const { return m_Tmd; }

	private:
		bool Cluster(uint64_t Index);

		Image* m_pDisc{};
		uint64_t m_Base{};
		uint64_t m_DataOffset{};
		uint8_t m_TitleKey[16]{};
		std::vector<uint8_t> m_Ticket{};
		std::vector<uint8_t> m_Tmd{};
		std::function<bool(uint64_t, uint8_t*, size_t)> m_Direct{};
		uint64_t m_ClusterIndex{ ~0ull };
		std::vector<uint8_t> m_ClusterData{};
	};

	uint32_t Be32(const uint8_t* p);
}

#endif
