//the launcher: what Play.bat, Play.sh and tools/launcher.py did, now in the game's executable
//
//started without a game folder (a double click, launch.sh, or options only) it reads settings.env
//and the options, puts them where the port looks for them (the environment), takes the game's
//files out of the disc first when they aren't there yet (window.cpp), sends the log to run.log
//and hands wiiboot its command line. started the old way (wiiboot game --nand ...) it stays out
//of it. a package keeps its files next to the executable, a checkout in build/ (launcher.cmake)
//
//GE_RUN_LOG=  (empty) leaves the output where it was going, launch.sh tees it itself
#include "extract.h"
#include "files.h"
#include "rt.h"
#include "update.h"
#include "window.h"
#include <SDL3/SDL_filesystem.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace launcher
{
namespace
{
	const char* kHelp{
		"Janus-Project [options]: GoldenEye 007 (Wii, SJBE52) on PC\n"
		"\n"
		"  --extract IMAGE [--force]  take the game's files out of a disc image (.rvz .iso .wbfs .wia) into game\n"
		"  --fps N --fov N --mouse N --mouse-mode auto|direct|stick\n"
		"  --fullscreen --vsync --no-ping --no-occlusion --no-sound-height --fast-start --cheats --offline\n"
		"  --no-gx-thread             the graphics commands on the game's own thread\n"
		"  --always-mouse             mouse look in every player state\n"
		"  --skip dam-intro,...       those cutscenes are skipped as well: a mission's name and\n"
		"                             intro, middle or ending; all, intros, middles, endings\n"
		"  --skip-cutscenes --skip-zukovsky --skip-briggs --skip-endings --skip-movies\n"
		"  --no-tutorials --no-colour-filter --no-level-timer --auto-qte --auto-start\n"
		"  --extras-menu --unlock-all --stamina-meter\n"
		"  --crash-dumps --no-crash-dumps --crash-mem --trace\n"
		"  --no-update-check          don't ask GitHub for a newer build this time\n"
		"anything else goes to the game itself (--window 1920x1080, --language fr...).\n"
		"The settings are kept in settings.env, the key bindings in keys.txt, the log in run.log.\n"
	};

	const int kFpsMax{ 275 };

	//what a setting is when settings.env doesn't say
	const std::map<std::string, std::string> kDefaults{
		{ "fps", "auto" }, { "fov", "49" }, { "mouse", "1.0" }, { "online", "1" }, { "fullscreen", "0" }, { "faststart", "0" },
		{ "mousemode", "auto" }, { "mouseinvert", "0" }, { "adsfov", "22.5" }, { "adssens", "0.5" }, { "aimlock", "0" },
		{ "mouseeverywhere", "0" }, { "skiplist", "" }, { "skipmovies", "0" }, { "notutorials", "0" }, { "nocolourfilter", "0" },
		{ "autosave", "1" }, { "leveltimer", "1" }, { "autoqte", "0" }, { "autostart", "0" }, { "extrasmenu", "0" },
		{ "onlineunlocks", "1" }, { "unlockall", "0" }, { "staminameter", "0" }, { "vsync", "0" }, { "ping", "1" },
		{ "occlusion", "1" }, { "soundheight", "1" }, { "crashdump", "1" }, { "crashmem", "0" }, { "gxthread", "1" },
		{ "updatecheck", "1" }, { "skipupdate", "" },
	};

	//the environment variable the port reads for each
	const std::pair<const char*, const char*> kVariables[]{
		{ "GE_FPS", "fps" }, { "GE_FOV", "fov" }, { "GE_MOUSE", "mouse" }, { "GE_MOUSE_MODE", "mousemode" },
		{ "GE_MOUSE_INVERT", "mouseinvert" }, { "GE_ADS_FOV", "adsfov" }, { "GE_ADS_SENS", "adssens" }, { "GE_AIM_LOCK", "aimlock" },
		{ "GE_MOUSE_EVERYWHERE", "mouseeverywhere" }, { "GE_SKIP_LIST", "skiplist" }, { "GE_LEVEL_TIMER", "leveltimer" },
		{ "GE_AUTO_QTE", "autoqte" }, { "GE_AUTO_START", "autostart" }, { "GE_SKIP_MOVIES", "skipmovies" },
		{ "GE_NO_TUTORIALS", "notutorials" }, { "GE_NO_COLOUR_FILTER", "nocolourfilter" }, { "GE_AUTOSAVE", "autosave" },
		{ "GE_EXTRAS_MENU", "extrasmenu" }, { "GE_UNLOCK_ALL", "unlockall" }, { "GE_ONLINE_UNLOCKS", "onlineunlocks" },
		{ "GE_STAMINA", "staminameter" }, { "GE_VSYNC", "vsync" }, { "GE_PING", "ping" },
		{ "GE_OCCLUSION", "occlusion" }, { "GE_SOUND_HEIGHT", "soundheight" }, { "WIIKIT_GX_THREAD", "gxthread" },
	};

	//the flags that just turn a setting on or off
	const std::pair<const char*, std::pair<const char*, const char*>> kFlags[]{
		{ "--fullscreen", { "fullscreen", "1" } }, { "--vsync", { "vsync", "1" } }, { "--no-ping", { "ping", "0" } },
		{ "--always-mouse", { "mouseeverywhere", "1" } }, { "--no-level-timer", { "leveltimer", "0" } },
		{ "--auto-qte", { "autoqte", "1" } }, { "--auto-start", { "autostart", "1" } }, { "--skip-movies", { "skipmovies", "1" } },
		{ "--no-tutorials", { "notutorials", "1" } }, { "--no-colour-filter", { "nocolourfilter", "1" } },
		{ "--extras-menu", { "extrasmenu", "1" } }, { "--unlock-all", { "unlockall", "1" } }, { "--stamina-meter", { "staminameter", "1" } },
		{ "--fast-start", { "faststart", "1" } }, { "--offline", { "online", "0" } }, { "--crash-dumps", { "crashdump", "1" } },
		{ "--no-crash-dumps", { "crashdump", "0" } }, { "--no-occlusion", { "occlusion", "0" } },
		{ "--no-sound-height", { "soundheight", "0" } }, { "--no-gx-thread", { "gxthread", "0" } },
		{ "--no-update-check", { "updatecheck", "0" } },
	};

	//where the player's files are: "." in a package, "build" in a checkout
	std::string State(const char* pName)
	{
		const std::string Dir{ GE_LAUNCH_STATE };
		return Dir == "." ? pName : Dir + "/" + pName;
	}

	void SetVariable(const char* pName, const std::string& Value)
	{
#ifdef _WIN32
		_putenv_s(pName, Value.c_str());
#else
		setenv(pName, Value.c_str(), 1);
#endif
	}

	//the environment wins over the settings, as it did with the python launcher
	void SetDefault(const char* pName, const std::string& Value)
	{
		if (std::getenv(pName) == nullptr && !Value.empty())
		{
			SetVariable(pName, Value);
		}
	}

	std::map<std::string, std::string> LoadSettings()
	{
		std::map<std::string, std::string> Settings{ kDefaults };
		bool HasSkipList{};
		std::ifstream File{ GE_SETTINGS_FILE };
		std::string Line;
		while (std::getline(File, Line))
		{
			const size_t Equals{ Line.find('=') };
			if (Line.rfind("LAUNCH_", 0) != 0 || Equals == std::string::npos)
			{
				continue;
			}
			std::string Key{ Line.substr(7, Equals - 7) }, Value{ Line.substr(Equals + 1) };
			for (char& c : Key)
			{
				c = (char)std::tolower((unsigned char)c);
			}
			while (!Value.empty() && (Value.back() == '\r' || Value.back() == '"'))
			{
				Value.pop_back();
			}
			if (!Value.empty() && Value.front() == '"')
			{
				Value.erase(0, 1);
			}
			Settings[Key] = Value;
			HasSkipList = HasSkipList || Key == "skiplist";
		}
		if (!HasSkipList)
		{
			//a file from before the list: the four options it replaced
			const std::pair<const char*, const char*> Old[]{ { "skipcutscenes", "intros" }, { "skipzukovsky", "nightclub-middle" },
				{ "skipbriggs", "carrier-middle" }, { "skipendings", "endings" } };
			for (const auto& [pKey, pWord] : Old)
			{
				if (Settings[pKey] == "1")
				{
					Settings["skiplist"] += std::string(Settings["skiplist"].empty() ? "" : ",") + pWord;
				}
			}
		}
		return Settings;
	}

	//a gecko file with at least one code in it
	bool HasCodes(const std::string& Path)
	{
		std::ifstream File{ FsPath(Path) };
		std::string Line;
		while (std::getline(File, Line))
		{
			if (!Line.empty() && Line[0] == '$')
			{
				return true;
			}
		}
		return false;
	}

	void CopyFile(const std::string& From, const std::string& To)
	{
		std::error_code Ignored;
		std::filesystem::copy_file(FsPath(From), FsPath(To), std::filesystem::copy_options::overwrite_existing, Ignored);
	}

	//the command line's own output, for --extract and --help: a windows build has no console
	//unless it was started from one
	void UseConsole()
	{
#ifdef _WIN32
		if (AttachConsole(ATTACH_PARENT_PROCESS))
		{
			std::freopen("CONOUT$", "w", stdout);
			std::freopen("CONOUT$", "w", stderr);
		}
#endif
	}

	int ExtractFromCommandLine(const std::string& Image, bool Force)
	{
		UseConsole();
		std::string About, Error;
		if (!InspectImage(Image, About, Error))
		{
			std::fprintf(stderr, "extract: %s\n", Error.c_str());
			return 1;
		}
		std::printf("%s\n", About.c_str());
		int Last{ -1 };
		const bool Ok{ ExtractGame(Image, "game", Force, [&Last](uint64_t Done, uint64_t Total, const std::string&)
			{
				const int Percent{ (int)(Done * 100 / (Total ? Total : 1)) };
				if (Percent != Last)
				{
					Last = Percent;
					std::printf("\r%3d%%  %.2f / %.2f GB", Percent, Done / 1e9, Total / 1e9);
					std::fflush(stdout);
				}
				return true;
			}, Error) };
		if (!Ok)
		{
			std::fprintf(stderr, "\nextract: %s\n", Error.c_str());
			return 1;
		}
		std::printf("\nthe game's files are in game\n");
		return 0;
	}

	std::vector<std::string> Launch(int argc, char** argv)
	{
		std::vector<std::string> Args(argv, argv + argc);
		if (argc >= 2 && argv[1][0] != '-')
		{
			return Args;
		}
		//everything is next to the executable (or in the checkout), wherever it was started from
		//(an image given on the command line is still found from where that was)
		std::error_code Ec;
		const std::filesystem::path StartedIn{ std::filesystem::current_path(Ec) };
#ifdef GE_LAUNCH_PORTABLE
		const char* pHome{ SDL_GetBasePath() };
#else
		const char* pHome{ GE_ROOT };
#endif
		if (pHome != nullptr)
		{
			std::filesystem::current_path(FsPath(pHome), Ec);
		}
#ifndef _WIN32
		//through x11 when there is one: a present on wayland costs the renderer 2 ms, 0.3 there
		if (std::getenv("DISPLAY") != nullptr)
		{
			SetDefault("SDL_VIDEODRIVER", "x11,wayland");
		}
#endif

		std::map<std::string, std::string> Settings{ LoadSettings() };
		std::vector<std::string> Rest;
		bool Trace{};
		for (size_t i{ 1 }; i < Args.size(); ++i)
		{
			const std::string& Arg{ Args[i] };
			const bool HasValue{ i + 1 < Args.size() };
			bool Flag{};
			for (const auto& [pFlag, Setting] : kFlags)
			{
				if (Arg == pFlag)
				{
					Settings[Setting.first] = Setting.second;
					Flag = true;
				}
			}
			if (Flag)
			{
				continue;
			}
			if (Arg == "--no-gui")
			{
				//the python launcher's, there's no window to skip any more
			}
			else if (Arg == "--help" || Arg == "-h")
			{
				UseConsole();
				std::printf("%s", kHelp);
				std::exit(0);
			}
			else if (Arg == "--extract")
			{
				if (!HasValue)
				{
					UseConsole();
					std::fprintf(stderr, "--extract wants the disc image: --extract GoldenEye.rvz\n");
					std::exit(2);
				}
				bool Force{};
				for (const std::string& Other : Args)
				{
					Force = Force || Other == "--force";
				}
				std::exit(ExtractFromCommandLine(Utf8(StartedIn / FsPath(Args[i + 1])), Force));
			}
			else if (HasValue && (Arg == "--fps" || Arg == "--fov" || Arg == "--mouse" || Arg == "--mouse-mode"))
			{
				Settings[Arg == "--mouse-mode" ? "mousemode" : Arg.substr(2)] = Args[++i];
			}
			else if (HasValue && Arg == "--skip")
			{
				Settings["skiplist"] += "," + Args[++i];
			}
			else if (Arg == "--skip-cutscenes" || Arg == "--skip-zukovsky" || Arg == "--skip-briggs" || Arg == "--skip-endings")
			{
				Settings["skiplist"] += Arg == "--skip-cutscenes" ? ",intros" : Arg == "--skip-zukovsky" ? ",nightclub-middle"
					: Arg == "--skip-briggs" ? ",carrier-middle" : ",endings";
			}
			else if (Arg == "--cheats")
			{
				CopyFile("config/gecko/cheats.txt", State("cheats.txt"));
			}
			else if (Arg == "--crash-mem")
			{
				Settings["crashdump"] = Settings["crashmem"] = "1";
			}
			else if (Arg == "--trace")
			{
				Trace = true;
			}
			else
			{
				Rest.push_back(Arg);
			}
		}

		//a package asks for a newer build (update.cpp), a checkout never does
		CleanUpdate();
		if (Settings["updatecheck"] != "0")
		{
			Release Newer;
			if (CheckForUpdate(Settings["skipupdate"], Newer) && UpdateWindow(Newer))
			{
				Restart(argv);
			}
		}

		if (!HaveGame("game") && !ExtractWindow())
		{
			std::exit(0);
		}

		if (Settings["fps"] != "auto" && std::atoi(Settings["fps"].c_str()) > kFpsMax)
		{
			Settings["fps"] = std::to_string(kFpsMax);
		}
		for (const auto& [pVariable, pKey] : kVariables)
		{
			SetDefault(pVariable, Settings[pKey]);
		}
		if (Settings["online"] == "0")
		{
			SetVariable("GE_HOSTS", "off");
		}
		if (Trace)
		{
			SetVariable("GE_TRACE", "1");
			SetVariable("GE_TRACELOOK", "1");
		}
#ifndef _WIN32
		if (Settings["crashdump"] == "0")
		{
			SetVariable("GE_NO_CRASHDUMP", "1");
		}
		else
		{
			SetDefault("GE_CRASH_DIR", State("crashes"));
		}
		if (Settings["crashmem"] == "1")
		{
			SetVariable("GE_CRASH_MEM", "1");
		}
#endif
		std::string Gecko{ Settings["faststart"] == "1" ? "config/gecko/faststart.txt" : "" };
		if (HasCodes(State("cheats.txt")))
		{
			Gecko += std::string(Gecko.empty() ? "" : ":") + State("cheats.txt");
		}
		if (const char* pExtra{ std::getenv("GE_GECKO_EXTRA") })
		{
			Gecko += std::string(Gecko.empty() ? "" : ":") + pExtra;
		}
		if (!Gecko.empty())
		{
			SetVariable("GE_GECKO_EXTRA", Gecko);
		}
		if (!std::filesystem::exists(FsPath(State("keys.txt"))))
		{
			CopyFile("config/keys.txt", State("keys.txt"));
		}

		const char* pLog{ std::getenv("GE_RUN_LOG") };
		const std::string Log{ pLog != nullptr ? pLog : State("run.log") };
		if (!Log.empty())
		{
			//both appending to one emptied file, so neither writes over the other
			std::ofstream{ Log, std::ios::trunc };
			std::freopen(Log.c_str(), "a", stdout);
			std::freopen(Log.c_str(), "a", stderr);
			//freopen leaves it buffered, and the game leaves through _Exit
			std::setvbuf(stderr, nullptr, _IONBF, 0);
		}
		std::vector<std::string> Out{ Args[0], "game", "--nand", State("nand"), "--keys", State("keys.txt"),
			"--symbols", GE_LAUNCH_SYMBOLS, "--watch", "10" };
		if (std::string(GE_LAUNCH_FONTS) != "")
		{
			Out.insert(Out.end(), { "--fonts", GE_LAUNCH_FONTS });
		}
		if (Settings["fullscreen"] == "1")
		{
			Out.push_back("--fullscreen");
		}
		Out.insert(Out.end(), Rest.begin(), Rest.end());
		return Out;
	}

	RtGameLauncher g_Launcher{ Launch };
}
}
