#include "extract.h"
#include "disc.h"
#include "files.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace launcher
{
namespace
{
	//the build is made from that executable, nothing else runs
	const char* kGameId{ "SJBE52" };

	bool WriteFile(const std::filesystem::path& Path, const std::vector<uint8_t>& Data)
	{
		std::ofstream File{ Path, std::ios::binary };
		File.write((const char*)Data.data(), (std::streamsize)Data.size());
		return File.good();
	}

	std::vector<uint8_t> RawRead(Image& Disc, uint64_t Offset, size_t Size)
	{
		std::vector<uint8_t> Out(Size);
		if (!Disc.Read(Offset, Out.data(), Size))
		{
			Out.clear();
		}
		return Out;
	}

	//laid out as `wit extract` and dolphin do: sys/, files/, the disc's header and the ticket
	bool WriteGame(Image& Disc, Partition& Part, const std::vector<FstFile>& Files, uint64_t Total,
		const std::filesystem::path& Out, const ExtractProgress& Progress, std::string& Error)
	{
		const std::vector<uint8_t> Boot{ Part.Read(0, 0x440) };
		const std::vector<uint8_t> Dol{ Part.Read((uint64_t)Be32(&Boot[0x420]) << 2, 0x100) };
		const std::vector<uint8_t> App{ Part.Read(0x2440, 0x20) };
		if (Dol.empty() || App.empty())
		{
			Error = "the disc's boot files can't be read";
			return false;
		}
		//the dol's size from its own section table: 18 offsets, then 18 sizes at 0x90
		uint32_t DolSize{};
		for (int i{}; i < 18; ++i)
		{
			if (Be32(&Dol[0x90 + i * 4]) != 0)
			{
				DolSize = std::max(DolSize, Be32(&Dol[i * 4]) + Be32(&Dol[0x90 + i * 4]));
			}
		}
		const std::pair<const char*, std::vector<uint8_t>> Parts[]{
			{ "disc/header.bin", RawRead(Disc, 0, 0x100) },
			{ "disc/region.bin", RawRead(Disc, 0x4E000, 0x20) },
			{ "ticket.bin", Part.Ticket() },
			{ "tmd.bin", Part.Tmd() },
			{ "sys/boot.bin", Boot },
			{ "sys/bi2.bin", Part.Read(0x440, 0x2000) },
			{ "sys/fst.bin", Part.Read((uint64_t)Be32(&Boot[0x424]) << 2, (size_t)Be32(&Boot[0x428]) << 2) },
			{ "sys/main.dol", Part.Read((uint64_t)Be32(&Boot[0x420]) << 2, DolSize) },
			{ "sys/apploader.img", Part.Read(0x2440, 0x20 + Be32(&App[0x14]) + Be32(&App[0x18])) },
		};
		std::filesystem::create_directories(Out / "sys");
		std::filesystem::create_directories(Out / "disc");
		std::filesystem::create_directories(Out / "files");
		for (const auto& [pName, Data] : Parts)
		{
			if (Data.empty() || !WriteFile(Out / FsPath(pName), Data))
			{
				Error = std::string("can't write ") + pName;
				return false;
			}
		}

		uint64_t Done{};
		std::vector<uint8_t> Buffer(4 << 20);
		for (const FstFile& File : Files)
		{
			const std::filesystem::path Path{ Out / "files" / FsPath(File.Path) };
			if (File.Path.ends_with("/"))
			{
				std::filesystem::create_directories(Path);
				continue;
			}
			std::ofstream Stream{ Path, std::ios::binary };
			uint64_t Offset{ File.Offset }, Left{ File.Size };
			while (Left > 0)
			{
				const size_t Now{ (size_t)std::min<uint64_t>(Left, Buffer.size()) };
				if (!Part.Read(Offset, Buffer.data(), Now))
				{
					Error = "can't read " + File.Path + " from the disc: the image is damaged";
					return false;
				}
				Stream.write((const char*)Buffer.data(), (std::streamsize)Now);
				Offset += Now;
				Left -= Now;
				Done += Now;
				if (Progress && !Progress(Done, Total, File.Path))
				{
					Error = "stopped";
					return false;
				}
			}
			if (!Stream.good())
			{
				Error = "can't write " + File.Path + ": is the disc full?";
				return false;
			}
		}
		return true;
	}
}

bool HaveGame(const std::string& Folder)
{
	std::error_code Ignored;
	return std::filesystem::is_regular_file(FsPath(Folder) / "sys" / "main.dol", Ignored)
		&& std::filesystem::is_directory(FsPath(Folder) / "files", Ignored);
}

bool InspectImage(const std::string& Image, std::string& About, std::string& Error)
{
	std::unique_ptr<launcher::Image> pDisc{ OpenImage(Image, Error) };
	DiscHeader Header;
	if (pDisc == nullptr)
	{
		return false;
	}
	if (!ReadHeader(*pDisc, Header) || !Header.Wii)
	{
		Error = "not a Wii disc image";
		return false;
	}
	About = std::string(pDisc->Kind()) + "  " + Header.GameId + "  '" + Header.Title + "'";
	return true;
}

bool ExtractGame(const std::string& ImagePath, const std::string& OutPath, bool Force, const ExtractProgress& Progress, std::string& Error)
{
	std::error_code Ec;
	std::unique_ptr<Image> pDisc{ OpenImage(ImagePath, Error) };
	if (pDisc == nullptr)
	{
		return false;
	}
	DiscHeader Header;
	if (!ReadHeader(*pDisc, Header) || !Header.Wii)
	{
		Error = "not a Wii disc image";
		return false;
	}
	if (Header.GameId != kGameId)
	{
		Error = "this is " + Header.GameId + " '" + Header.Title + "', not GoldenEye 007 NTSC-U (" + kGameId + "): the port only runs that version";
		return false;
	}
	if (HaveGame(OutPath) && !Force)
	{
		Error = OutPath + " already holds the game (--force replaces it, the old one is kept as " + OutPath + ".old)";
		return false;
	}
	Partition Part;
	std::vector<FstFile> Files;
	if (!Part.Open(*pDisc, Error))
	{
		return false;
	}
	if (!Part.Files(Files))
	{
		Error = "the disc's file list can't be read: the image is damaged";
		return false;
	}
	uint64_t Total{};
	for (const FstFile& File : Files)
	{
		Total += File.Size;
	}

	const std::filesystem::path Out{ std::filesystem::absolute(FsPath(OutPath), Ec) };
	const std::filesystem::path Parent{ Out.parent_path() };
	std::filesystem::create_directories(Parent, Ec);
	const std::filesystem::space_info Space{ std::filesystem::space(Parent, Ec) };
	if (!Ec && Space.available < Total + (64ull << 20))
	{
		char Text[160];
		std::snprintf(Text, sizeof Text, "not enough room: the game is %.1f GB, %.1f GB are free there", Total / 1e9, Space.available / 1e9);
		Error = Text;
		return false;
	}

	std::filesystem::path Temp{ Out };
	Temp += ".extracting";
	std::filesystem::remove_all(Temp, Ec);
	if (!WriteGame(*pDisc, Part, Files, Total, Temp, Progress, Error) || !HaveGame(Utf8(Temp)))
	{
		if (Error.empty())
		{
			Error = "the image gave no sys/main.dol: it's damaged or not a game disc";
		}
		std::filesystem::remove_all(Temp, Ec);
		return false;
	}

	if (std::filesystem::is_directory(Out, Ec))
	{
		bool Filled{ HaveGame(Utf8(Out)) };
		for (const auto& Entry : std::filesystem::directory_iterator(Out, Ec))
		{
			Filled = Filled || Entry.is_directory();
		}
		if (Filled)
		{
			//moved aside, never deleted
			std::filesystem::path Old{ Out };
			Old += ".old";
			for (int n{ 2 }; std::filesystem::exists(Old, Ec); ++n)
			{
				Old = Out;
				Old += ".old" + std::to_string(n);
			}
			std::filesystem::rename(Out, Old, Ec);
		}
		else
		{
			//only the package's note in it
			std::filesystem::remove_all(Out, Ec);
		}
		if (Ec)
		{
			Error = "the game folder that's there can't be moved aside: " + Ec.message();
			return false;
		}
	}
	std::filesystem::rename(Temp, Out, Ec);
	if (Ec)
	{
		Error = "the extracted files can't be moved into place: " + Ec.message();
		return false;
	}
	return true;
}
}
