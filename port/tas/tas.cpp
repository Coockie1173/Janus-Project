//TASLib, the private library: TASLib.dll (Windows) or libTASLib.so (Linux) beside the executable
//or in its lib/, GE_TASLIB=F loads that file instead, GE_TASLIB=0 none at all
//the library only sees taslib_api.h, so it builds and ships apart from the game
//
//the library is opened before anything of the runtime starts, and its optional early export says
//what it needs from the start (the runtime's deterministic mode, which has to be set before its
//clock and devices are). its init comes with the port's install, like the rest
//
//what the game offers it is hooks (between two frames, the raw pad, the local controller's update,
//the window's keys) and primitives (memory, wiikit's saved states, a held frame rate, the mouse,
//text over the picture, the settings): the library's own behaviour stays the library's
#include "tas/tas.h"
#include "tas/taslib_api.h"
#include "input/mouse.h"
#include "ge.h"
#include "ge_version.h"
#include "video.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef GE_IMGUI
#include "imgui.h"
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace ge
{

std::string settings_values(); //ui/settings/menu.cpp
bool settings_apply(const std::string& id, const std::string& value);

namespace
{

#ifdef _WIN32
const char* kLibName = "TASLib.dll";
#else
const char* kLibName = "libTASLib.so";
#endif

std::string ExeDirOf()
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

struct Opened
{
	void* handle = nullptr;
	std::string path;
	bool asked = false; //GE_TASLIB named a file
};

//before anything starts: found, opened, and its early export asked
Opened OpenEarly()
{
	Opened o;
	const char* env = std::getenv("GE_TASLIB");
	if (env && std::string(env) == "0") return o;
	std::vector<std::string> paths;
	if (env && *env) { paths.push_back(env); o.asked = true; }
	else
	{
		const std::string dir = ExeDirOf();
		paths.push_back(dir + "/" + kLibName);
		paths.push_back(dir + "/lib/" + kLibName);
	}
	for (const std::string& p : paths)
		if ((o.handle = OpenLib(p))) { o.path = p; break; }
	if (!o.handle) return o;
	if (auto early = (GeTasEarlyFn)FindSym(o.handle, GE_TAS_EARLY_NAME))
		if (early() & GE_TAS_EARLY_DETERMINISTIC) rt_set_deterministic(true);
	return o;
}
const Opened g_opened = OpenEarly();

const GeTasLib* g_pLib{};
PPCContext* g_pBoundary{}; //the game thread's registers while Boundary runs
std::atomic<uint32_t> g_fixedRate{0};
std::atomic<bool> g_asDrawn{false}, g_mouseTaken{false};
std::atomic<uint32_t> g_presence{~0u};

std::mutex g_textMx;
struct Line
{
	float x, y, scale;
	uint32_t rgba;
	std::string text;
};
std::vector<Line> g_text;

//---- what the library may call --------------------------------------------------------------
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
int HostDeterministic() { return rt_deterministic() ? 1 : 0; }
const uint8_t* HostMemory(uint32_t a, uint32_t size)
{
	const uint64_t end = (uint64_t)a + size;
	const bool mem1 = a >= 0x80000000u && end <= 0x81800000u, mem2 = a >= 0x90000000u && end <= 0x90000000ull + g_mem2_size;
	if (!size || !(mem1 || mem2)) return nullptr;
	return host(a);
}
uint32_t HostMem2Size() { return g_mem2_size; }

int HostSaveState(void** data, uint32_t* size)
{
	*data = nullptr;
	*size = 0;
	if (!g_pBoundary) return 0;
	std::vector<uint8_t> out;
	std::string err;
	if (!rt_state_save(*g_pBoundary, out, err))
	{
		rt_log("taslib: no state: %s", err.c_str());
		return 0;
	}
	void* p = std::malloc(out.size());
	if (!p) return 0;
	std::memcpy(p, out.data(), out.size());
	*data = p;
	*size = (uint32_t)out.size();
	return 1;
}
void HostFreeState(void* data) { std::free(data); }
int HostLoadState(const void* data, uint32_t size, char* err, uint32_t errSize)
{
	std::string why;
	if (!g_pBoundary) why = "only between two frames";
	else if (rt_state_load(*g_pBoundary, (const uint8_t*)data, size, why)) return 1;
	if (err && errSize) std::snprintf(err, errSize, "%s", why.c_str());
	return 0;
}
void HostSleep(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void HostSetUnpaced(int on) { rt_set_unpaced(on != 0); }
void HostSetFixedRate(uint32_t fps) { g_fixedRate = fps == 0 ? 0 : std::clamp(fps, 30u, 1000u); }
void HostSetFramesAsDrawn(int on) { g_asDrawn = on != 0; }
void HostSetPresence(uint32_t mask) { g_presence = mask; }
void HostSetMouseTaken(int on) { g_mouseTaken = on != 0; }
void HostTakeMouse(float* dx, float* dy)
{
	float x = 0, y = 0;
	video_take_mouse_motion(x, y);
	if (mouse_invert()) y = -y;
	*dx = x;
	*dy = y;
}
int HostMouseCaptured() { return in_play() && !overlay_open() ? 1 : 0; }

int HostLook(GeLookInfo* info)
{
	*info = GeLookInfo{};
	const uint32_t ctrl = local_controller();
	if (!ctrl || ld8(kPaused) || is_tank(ctrl)) return 0;
	const uint32_t player = ld32(ctrl + 4);
	if (!guest_ptr(player)) return 0;
	const float sens = mouse_sens() > 0 ? mouse_sens() : 1.0f;
	info->controller = ctrl;
	info->radPerPixel = kRadPerPixel * sens * aim_sens_scale(ctrl);
	const Look look = look_mode(ctrl);
	if (look == kLookView || look == kLookForced)
	{
		const uint32_t view = ld32(player + 0x16C);
		if (!guest_ptr(view)) return 0;
		info->mode = GE_LOOK_VIEW;
		info->yawAddr = player + 0xDC;
		info->pitchAddr = view + 0x768;
		info->pitchMin = -pitch_limit();
		info->pitchMax = pitch_limit();
		info->yawMin = -INFINITY;
		info->yawMax = INFINITY;
		return 1;
	}
	if (look != kLookAim) return 0;
	const bool narrow = (ld32(ctrl + kStateFlags) & 0x80) != 0, cutscene = ld8(ctrl + kInCutscene) != 0;
	float lim[4];
	for (int i = 0; i < 4; ++i)
		lim[i] = kRadPerDegree * (narrow ? ldf(kAimLimitsNarrow + 4 * i) : cutscene ? ldf(ctrl + kAimLimits + 4 * i) : ldf(kAimLimitsPlay + 4 * i));
	if (!(lim[0] <= lim[1]) || !(lim[2] <= lim[3])) return 0;
	info->mode = GE_LOOK_AIM;
	info->yawAddr = ctrl + kAimYaw;
	info->pitchAddr = ctrl + kAimPitch;
	info->pitchMin = lim[0];
	info->pitchMax = lim[1];
	info->yawMin = lim[2];
	info->yawMax = lim[3];
	return 1;
}
void HostViewTurned(float dx) { mouse_moved(dx); }
void HostShowText(const GeTasText* lines, uint32_t count)
{
	std::lock_guard<std::mutex> lk(g_textMx);
	g_text.clear();
	for (uint32_t i = 0; i < count; ++i)
		g_text.push_back({lines[i].x, lines[i].y, lines[i].scale > 0 ? lines[i].scale : 1.0f, lines[i].rgba, lines[i].text ? lines[i].text : ""});
}
void HostSay(const char* text) { settings_say(text ? text : ""); }
uint32_t HostSettings(char* out, uint32_t size)
{
#ifdef GE_IMGUI
	const std::string all = settings_values();
#else
	const std::string all;
#endif
	if (out && size)
	{
		const size_t n = std::min<size_t>(all.size(), size - 1);
		std::memcpy(out, all.data(), n);
		out[n] = 0;
	}
	return (uint32_t)all.size();
}
int HostApplySetting(const char* id, const char* value)
{
#ifdef GE_IMGUI
	if (!id || !value) return 0;
	return settings_apply(id, value) ? 1 : 0;
#else
	(void)id;
	(void)value;
	return 0;
#endif
}
const char* HostExeDir()
{
	static const std::string dir = ExeDirOf();
	return dir.c_str();
}
int HostKeyDown(const char* name)
{
	const SDL_Scancode sc = SDL_GetScancodeFromName(name);
	if (sc == SDL_SCANCODE_UNKNOWN) return 0;
	int n = 0;
	const bool* keys = SDL_GetKeyboardState(&n);
	return keys && (int)sc < n && keys[sc] ? 1 : 0;
}

int HostMenuPage(const char* title, const char* about) { return settings_page(title, about); }
void HostMenuRow(int page, const char* label, const char* hint, const char* (*value)(void*), void (*adjust)(void*, int), void (*select)(void*), void* user)
{
	settings_row(page, label, hint,
		value ? std::function<std::string()>([value, user] { const char* v = value(user); return std::string(v ? v : ""); }) : nullptr,
		adjust ? std::function<void(int)>([adjust, user](int d) { adjust(user, d); }) : nullptr,
		select ? std::function<void()>([select, user] { select(user); }) : nullptr);
}
void HostMenuKey(int page, const char* id, const char* label, const char* hint, const char* otherwise)
{
	settings_key(page, id, label, hint, nullptr, otherwise ? otherwise : "");
}
uint32_t HostMenuKeyName(const char* id, char* out, uint32_t size)
{
	const std::string k = settings_key_name(id);
	if (out && size)
	{
		const size_t n = std::min<size_t>(k.size(), size - 1);
		std::memcpy(out, k.data(), n);
		out[n] = 0;
	}
	return (uint32_t)k.size();
}
int HostMenuOpen() { return overlay_open() ? 1 : 0; }

const GeTasHost kHost{
	GE_TAS_API_VERSION, sizeof(GeTasHost), GE_VERSION, HostLog,
	HostRead8, HostRead16, HostRead32, HostWrite8, HostWrite16, HostWrite32,
	HostValid, HostFrames, HostInPlay,
	HostDeterministic, HostMemory, HostMem2Size, HostSaveState, HostFreeState, HostLoadState, HostSleep, HostSetUnpaced,
	HostSetFixedRate, HostSetFramesAsDrawn, HostSetPresence, HostSetMouseTaken, HostTakeMouse, HostMouseCaptured,
	HostLook, HostViewTurned, HostShowText, HostSay, HostSettings, HostApplySetting, HostExeDir, HostKeyDown,
	HostMenuPage, HostMenuRow, HostMenuKey, HostMenuKeyName, HostMenuOpen,
};

void Shutdown()
{
	if (g_pLib && g_pLib->Shutdown) g_pLib->Shutdown();
	g_pLib = nullptr;
}

bool Present(uint32_t chan) { return chan < 32 && (g_presence.load() >> chan & 1); }

} //namespace

void install_tas()
{
	wpad_set_presence(nullptr);
	if (!g_opened.handle)
	{
		//not having it is the normal case, only a path someone asked for is worth a line
		if (g_opened.asked) rt_log("taslib: couldn't load %s", std::getenv("GE_TASLIB"));
		return;
	}
	auto init = (GeTasInitFn)FindSym(g_opened.handle, GE_TAS_INIT_NAME);
	if (!init)
	{
		rt_log("taslib: %s has no %s", g_opened.path.c_str(), GE_TAS_INIT_NAME);
		return;
	}
	const GeTasLib* pLib = init(&kHost);
	if (!pLib) return;
	if (pLib->apiVersion != GE_TAS_API_VERSION)
	{
		rt_log("taslib: %s wants api %u, the game has %u", g_opened.path.c_str(), pLib->apiVersion, GE_TAS_API_VERSION);
		return;
	}
	g_pLib = pLib;
	std::atexit(Shutdown);
	if (g_presence.load() != ~0u) wpad_set_presence(Present);
	rt_log("taslib: %s loaded from %s", pLib->name ? pLib->name : "TASLib", g_opened.path.c_str());
}

bool tas_loaded() { return g_pLib != nullptr; }
uint32_t lib_fixed_rate() { return g_pLib ? g_fixedRate.load() : 0; }
bool lib_frames_as_drawn() { return g_pLib && g_asDrawn; }
bool lib_mouse_taken() { return g_pLib && g_mouseTaken; }

void tas_frame(uint32_t frame)
{
	if (g_pLib && g_pLib->Frame) g_pLib->Frame(frame);
}

void tas_boundary(PPCContext& c)
{
	if (!g_pLib || !g_pLib->Boundary) return;
	const uint32_t presence = g_presence;
	g_pBoundary = &c;
	g_pLib->Boundary();
	g_pBoundary = nullptr;
	if (g_presence != presence) wpad_set_presence(g_presence == ~0u ? nullptr : Present);
}

void tas_controller(uint32_t ctrl)
{
	if (g_pLib && g_pLib->ControllerUpdate) g_pLib->ControllerUpdate(ctrl);
}

bool tas_input(int chan, ClassicState& s)
{
	if (!g_pLib || !g_pLib->Input) return false;
	GeTasPad pad{s.buttons, s.lx, s.ly, s.rx, s.ry, s.lt, s.rt, s.extra};
	if (!g_pLib->Input(chan, &pad)) return false;
	s.buttons = pad.buttons;
	s.lx = pad.lx; s.ly = pad.ly; s.rx = pad.rx; s.ry = pad.ry;
	s.lt = pad.lt; s.rt = pad.rt;
	s.extra = pad.extra;
	return true;
}

void tas_buttons(int chan, ClassicState& s)
{
	if (!g_pLib || !g_pLib->Pad) return;
	GeTasPad pad{s.buttons, s.lx, s.ly, s.rx, s.ry, s.lt, s.rt, s.extra};
	g_pLib->Pad(chan, &pad);
	s.buttons = pad.buttons;
	s.lx = pad.lx; s.ly = pad.ly; s.rx = pad.rx; s.ry = pad.ry;
	s.lt = pad.lt; s.rt = pad.rt;
	s.extra = pad.extra;
}

bool tas_event(const SDL_Event* e)
{
	if (!g_pLib || !g_pLib->Key) return false;
	if (e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP) return false;
	if (overlay_open() && e->type == SDL_EVENT_KEY_DOWN) return false; //the menu's: choosing a key, moving about
	const char* name = SDL_GetScancodeName(e->key.scancode);
	if (!name || !*name) return false;
	const int mods = ((e->key.mod & SDL_KMOD_SHIFT) ? 1 : 0) | ((e->key.mod & SDL_KMOD_CTRL) ? 2 : 0) | ((e->key.mod & SDL_KMOD_ALT) ? 4 : 0);
	return g_pLib->Key(name, e->type == SDL_EVENT_KEY_DOWN, e->key.repeat, mods) != 0;
}

bool tas_showing()
{
	std::lock_guard<std::mutex> lk(g_textMx);
	return !g_text.empty();
}

void tas_draw()
{
#ifdef GE_IMGUI
	std::vector<Line> lines;
	{
		std::lock_guard<std::mutex> lk(g_textMx);
		lines = g_text;
	}
	if (lines.empty()) return;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	for (const Line& l : lines)
	{
		const float size = ImGui::GetFontSize() * l.scale;
		const ImU32 col = IM_COL32(l.rgba >> 24 & 0xFF, l.rgba >> 16 & 0xFF, l.rgba >> 8 & 0xFF, l.rgba & 0xFF);
		dl->AddText(font, size, ImVec2(l.x + 1, l.y + 1), IM_COL32(0, 0, 0, (l.rgba & 0xFF) * 3 / 4), l.text.c_str());
		dl->AddText(font, size, ImVec2(l.x, l.y), col, l.text.c_str());
	}
#endif
}

} //namespace ge
