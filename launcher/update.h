//the updater: a package asks GitHub for the "latest" release as it starts, and when that was built
//from another commit than this one the window (window.cpp) offers it. the package's zip replaces
//the files that came in the package, the player's own (game/, nand/, keys.txt, settings.env...)
//stay, and the modpacks' zips go into modpacks/ when asked for. then the new executable is started
#ifndef LAUNCHER_UPDATE_H
#define LAUNCHER_UPDATE_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace launcher
{
	struct Asset
	{
		std::string Name{};
		std::string Url{};
		uint64_t Size{};
	};

	struct Release
	{
		std::string Commit{};
		std::string Title{};
		std::vector<std::string> Changes{}; //the commits since this build's, newest first
		Asset Package{};                    //this system's zip
		std::vector<Asset> Modpacks{};      //every other zip
	};

	struct UpdateProgress
	{
		std::mutex Lock{};
		std::string Step{};
		std::atomic<uint64_t> Done{};
		std::atomic<uint64_t> Total{};
		std::atomic<bool> Cancel{};
	};

	//the commit this build is from, empty in a checkout or a build with changes of its own
	std::string ThisCommit();
	//true with the release when it's another commit than this build and Skip, waiting
	//no more than a few seconds (offline, GitHub not answering: false)
	bool CheckForUpdate(const std::string& Skip, Release& Out);
	//the modpacks of the release that aren't in modpacks/ as they are there
	std::vector<Asset> ChangedModpacks(const Release& R);
	//everything downloaded, then put in place; false leaves the old files as they were
	bool ApplyUpdate(const Release& R, bool Modpacks, UpdateProgress& P, std::string& Error);
	//the old files the last update moved aside (a running executable can't be deleted on windows)
	void CleanUpdate();
	//"don't ask again" for that release: LAUNCH_SKIPUPDATE in settings.env
	void SkipRelease(const std::string& Commit);
	//the new executable in place of this one, with the same command line
	[[noreturn]] void Restart(char** argv);
}

#endif
