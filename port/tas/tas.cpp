//TASLib, the private library: TASLib.dll (Windows) or libTASLib.so (Linux) beside the executable
//or in its lib/, GE_TASLIB=F loads that file instead, GE_TASLIB=0 none at all
//the library only sees taslib_api.h, so it builds and ships apart from the game
#include "tas/tas.h"
#include "tas/taslib_api.h"
#include "ge_version.h"
#include "video.h"
#include <cstdlib>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace ge
{

namespace
{

#ifdef _WIN32
const char* kLibName = "TASLib.dll";
#else
const char* kLibName = "libTASLib.so";
#endif

const GeTasLib* g_pLib{};

std::string ExeDir()
{
#ifdef _WIN32
	char path[MAX_PATH]{};
	DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string s(path, n);
	size_t slash = s.find_last_of("\\/");
#else
	char path[4096]{};
	ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
	if (n <= 0) return ".";
	std::string s(path, n);
	size_t slash = s.rfind('/');
#endif
	return slash == std::string::npos ? std::string(".") : s.substr(0, slash);
}

void* OpenLib(const std::string& path)
{
#ifdef _WIN32
	return (void*)LoadLibraryA(path.c_str());
#else
	return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* FindSym(void* pLib, const char* name)
{
#ifdef _WIN32
	return (void*)GetProcAddress((HMODULE)pLib, name);
#else
	return dlsym(pLib, name);
#endif
}

void HostLog(const char* text) { rt_log("taslib: %s", text); }
uint8_t HostRead8(uint32_t a) { return guest_ptr(a) ? ld8(a) : 0; }
uint16_t HostRead16(uint32_t a) { return guest_ptr(a) ? ld16(a) : 0; }
uint32_t HostRead32(uint32_t a) { return guest_ptr(a) ? ld32(a) : 0; }
void HostWrite8(uint32_t a, uint8_t v) { if (guest_ptr(a)) st8(a, v); }
void HostWrite16(uint32_t a, uint16_t v) { if (guest_ptr(a)) st16(a, v); }
void HostWrite32(uint32_t a, uint32_t v) { if (guest_ptr(a)) st32(a, v); }
int HostValid(uint32_t a) { return guest_ptr(a) ? 1 : 0; }
uint32_t HostFrames() { return ld32(kFrameCount); }
int HostInPlay() { return in_play() ? 1 : 0; }

const GeTasHost kHost{
	GE_TAS_API_VERSION, sizeof(GeTasHost), GE_VERSION, HostLog,
	HostRead8, HostRead16, HostRead32, HostWrite8, HostWrite16, HostWrite32,
	HostValid, HostFrames, HostInPlay,
};

void Shutdown()
{
	if (g_pLib && g_pLib->Shutdown) g_pLib->Shutdown();
	g_pLib = nullptr;
}

} //namespace

void install_tas()
{
	const char* env = std::getenv("GE_TASLIB");
	if (env && std::string(env) == "0") return;
	std::vector<std::string> paths;
	if (env && *env) paths.push_back(env);
	else
	{
		std::string dir = ExeDir();
		paths.push_back(dir + "/" + kLibName);
		paths.push_back(dir + "/lib/" + kLibName);
	}
	void* pHandle{};
	std::string found;
	for (const std::string& p : paths)
	{
		if ((pHandle = OpenLib(p))) { found = p; break; }
	}
	//not having it is the normal case, only a path someone asked for is worth a line
	if (!pHandle)
	{
		if (env && *env) rt_log("taslib: couldn't load %s", env);
		return;
	}
	auto init = (GeTasInitFn)FindSym(pHandle, GE_TAS_INIT_NAME);
	if (!init)
	{
		rt_log("taslib: %s has no %s", found.c_str(), GE_TAS_INIT_NAME);
		return;
	}
	const GeTasLib* pLib = init(&kHost);
	if (!pLib) return;
	if (pLib->apiVersion != GE_TAS_API_VERSION)
	{
		rt_log("taslib: %s wants api %u, the game has %u", found.c_str(), pLib->apiVersion, GE_TAS_API_VERSION);
		return;
	}
	g_pLib = pLib;
	std::atexit(Shutdown);
	rt_log("taslib: %s loaded from %s", pLib->name ? pLib->name : "TASLib", found.c_str());
}

void tas_frame(uint32_t frame)
{
	if (g_pLib && g_pLib->Frame) g_pLib->Frame(frame);
}

void tas_buttons(int chan, ClassicState& s)
{
	if (!g_pLib || !g_pLib->Pad) return;
	GeTasPad pad{s.buttons, s.lx, s.ly, s.rx, s.ry, s.lt, s.rt};
	g_pLib->Pad(chan, &pad);
	s.buttons = pad.buttons;
	s.lx = pad.lx; s.ly = pad.ly; s.rx = pad.rx; s.ry = pad.ry;
	s.lt = pad.lt; s.rt = pad.rt;
}

bool tas_loaded() { return g_pLib != nullptr; }

} //namespace ge
