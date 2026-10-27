//the game's files out of a disc image, into the game folder
#ifndef LAUNCHER_EXTRACT_H
#define LAUNCHER_EXTRACT_H

#include <cstdint>
#include <functional>
#include <string>

namespace launcher
{
	//the folder has sys/main.dol and files/
	bool HaveGame(const std::string& Folder);

	//"RVZ  SJBE52  'GoldenEye 007'", false and Error when it can't be read or isn't a wii disc
	bool InspectImage(const std::string& Image, std::string& About, std::string& Error);

	//Progress gets the bytes done, the total and the file being written, returning false stops
	//it (nothing is kept). the files go into Out.extracting first and only take Out's place once
	//they're all there, a filled Out is kept unless Force, and then it's moved aside, not deleted
	using ExtractProgress = std::function<bool(uint64_t, uint64_t, const std::string&)>;
	bool ExtractGame(const std::string& Image, const std::string& Out, bool Force, const ExtractProgress& Progress, std::string& Error);
}

#endif
