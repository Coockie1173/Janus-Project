//paths are utf-8 strings everywhere in the launcher, this turns them into the system's
#ifndef LAUNCHER_FILES_H
#define LAUNCHER_FILES_H

#include <filesystem>
#include <string>

namespace launcher
{
	inline std::filesystem::path FsPath(const std::string& Path)
	{
		return std::filesystem::path(std::u8string(Path.begin(), Path.end()));
	}

	inline std::string Utf8(const std::filesystem::path& Path)
	{
		const std::u8string Text{ Path.u8string() };
		return std::string(Text.begin(), Text.end());
	}
}

#endif
