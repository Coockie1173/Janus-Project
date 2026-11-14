//what the game and TASLib (the private library, loaded at run time) hand each other
//plain C so the library can be built apart and swapped without rebuilding the game
//bump GE_TAS_API_VERSION when a struct changes, only add fields at the end
#ifndef GE_TASLIB_API_H
#define GE_TASLIB_API_H
#include <stdint.h>

#define GE_TAS_API_VERSION 1

#ifdef _WIN32
#define GE_TAS_EXPORT __declspec(dllexport)
#else
#define GE_TAS_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

//the classic controller, what the game reads this frame
typedef struct GeTasPad
{
	uint32_t buttons;
	float lx, ly, rx, ry, lt, rt;
} GeTasPad;

//what the game offers the library
typedef struct GeTasHost
{
	uint32_t apiVersion;
	uint32_t size; //sizeof(GeTasHost) on the game's side
	const char* gameVersion;
	void (*Log)(const char* text);
	uint8_t (*Read8)(uint32_t addr);
	uint16_t (*Read16)(uint32_t addr);
	uint32_t (*Read32)(uint32_t addr);
	void (*Write8)(uint32_t addr, uint8_t v);
	void (*Write16)(uint32_t addr, uint16_t v);
	void (*Write32)(uint32_t addr, uint32_t v);
	int (*ValidAddr)(uint32_t addr);
	uint32_t (*FrameCount)(void);
	int (*InPlay)(void);
} GeTasHost;

//what the library gives back, any callback can be null
typedef struct GeTasLib
{
	uint32_t apiVersion;
	uint32_t size; //sizeof(GeTasLib) on the library's side
	const char* name;
	void (*Frame)(uint32_t frame); //once a game frame, on the game's thread
	void (*Pad)(int chan, GeTasPad* pad); //last say over the pad, after every other filter
	void (*Shutdown)(void);
} GeTasLib;

//the one export, null when the library doesn't want to run
typedef const GeTasLib* (*GeTasInitFn)(const GeTasHost* host);
#define GE_TAS_INIT_NAME "ge_taslib_init"

#ifdef __cplusplus
}
#endif
#endif
