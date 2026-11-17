//what the game and TASLib (the private library, loaded at run time) hand each other
//plain C so the library can be built apart and swapped without rebuilding the game
//bump GE_TAS_API_VERSION when a struct changes, only add fields at the end
//
//the game only offers hooks and primitives here, what the library does with them is its own
#ifndef GE_TASLIB_API_H
#define GE_TASLIB_API_H
#include <stdint.h>

#define GE_TAS_API_VERSION 9

#ifdef _WIN32
#define GE_TAS_EXPORT __declspec(dllexport)
#else
#define GE_TAS_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

//the classic controller as it is held: KPAD's classic bits, sticks -1..1, triggers 0..1, and
//the port's own keys in extra (bit 0 sprint, bits 2..4 weapon slots 1..3, bit 5 jump)
typedef struct GeTasPad
{
	uint32_t buttons;
	float lx, ly, rx, ry, lt, rt;
	uint32_t extra; //api 2
} GeTasPad;

//the local player's look as the mouse turns it: the mode, where its two floats are and their limits
//view: the player's yaw (radians, growing right) and the view's pitch (radians, down positive)
//aim: inside a cutscene's or a vehicle's view, the aim's yaw and pitch
enum { GE_LOOK_NONE = 0, GE_LOOK_VIEW = 1, GE_LOOK_AIM = 2 };
typedef struct GeLookInfo
{
	int32_t mode;
	uint32_t controller; //the local controller
	uint32_t yawAddr, pitchAddr; //floats in guest memory
	float pitchMin, pitchMax, yawMin, yawMax; //radians, the yaw's unbounded (+-inf) for the view
	float radPerPixel; //what a pixel of mouse turns here with the mouse settings (sensitivity, aim)
} GeLookInfo;

//the local player's bunnyhop (For Fun) as its last step left it: the speed is the hop's while active,
//in the air the stick adds speed along itself, quake's air accelerate: the speed along the stick's
//direction up to min(wishSpeed * stick, airCap), at most airAccel * wishSpeed * stick * dt a step
typedef struct GeMoveInfo
{
	int32_t active, inAir;
	float velX, velZ; //metres a second, x and z (a yaw's forward is (sin yaw, cos yaw))
	float airCap, airAccel, wishSpeed, maxSpeed;
	float deadZone; //a stick under this adds nothing
	float dt; //the last step, seconds
} GeMoveInfo;

//a line of text over the picture. x, y are the window's pixels from the corner the anchor names, so a
//line can sit against any edge whatever the window's size (a watermark in a corner)
//GE_ANCHOR_TIMER sits the line above the game's mission timer (right-aligned to it); x and y are the
//pixels left of the timer's right edge and above its top. off a mission (no timer) it falls back to top centre
//GE_ANCHOR_VIEW (api 8): x and y are a place on the game's picture, -1 to 1 across and up (a point the view's clip
//matrix put there), the text centred on it
enum { GE_ANCHOR_TL = 0, GE_ANCHOR_TR = 1, GE_ANCHOR_BL = 2, GE_ANCHOR_BR = 3, GE_ANCHOR_TC = 4, GE_ANCHOR_TIMER = 5, GE_ANCHOR_VIEW = 6 }; //api 7
typedef struct GeTasText
{
	float x, y;
	uint32_t rgba; //0xRRGGBBAA
	float scale; //1 the overlay's font
	const char* text;
	uint32_t anchor; //api 7: GE_ANCHOR_*, the corner x and y count from (0 top left, as before)
} GeTasText;

//api 8: the local player's bullets as they happen, on the game's thread
//fired: it leaves the gun (x y z where, v its velocity in metres a second), a shotgun's pellets each a bullet after
//impact: it lands on the world (x y z where); contact: on a thing (x y z where, thing the entity)
//hurt: a guard takes it (thing the guard, before and after its health, flags the hit's: bit 0 the head)
enum { GE_SHOT_FIRED = 0, GE_SHOT_IMPACT = 1, GE_SHOT_CONTACT = 2, GE_SHOT_HURT = 3 };
typedef struct GeTasShot
{
	int32_t kind;
	float x, y, z;
	float vx, vy, vz;
	uint32_t thing;
	float before, after;
	uint32_t flags;
} GeTasShot;

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

	//api 2
	int (*Deterministic)(void); //the runtime's deterministic mode is on (asked for by the early export)
	const uint8_t* (*Memory)(uint32_t addr, uint32_t size); //guest memory to read, null outside MEM1/MEM2
	uint32_t (*Mem2Size)(void);
	//saved states, only from inside Boundary. Save hands back a buffer of the host's (FreeState it),
	//Load puts one back and says why not in err when it can't (0)
	int (*SaveState)(void** data, uint32_t* size);
	void (*FreeState)(void* data);
	int (*LoadState)(const void* data, uint32_t size, char* err, uint32_t errSize);
	void (*Sleep)(uint32_t ms); //the window keeps drawing meanwhile
	void (*SetUnpaced)(int on); //deterministic mode: the guest's clock runs as fast as the host can
	void (*SetFixedRate)(uint32_t fps); //the engine's rate held at fps, 30..1000; 0 the game's limiter
	void (*SetFramesAsDrawn)(int on); //each frame shown as it's drawn, not at VI's retrace
	void (*SetPresence)(uint32_t mask); //the channels holding a controller (bit a channel), ~0 the host's pads
	void (*SetMouseTaken)(int on); //the mouse turns nothing and works no menu by itself: the library takes it
	void (*TakeMouse)(float* dx, float* dy); //the mouse's motion since last taken, pixels (inverted as the settings say)
	int (*MouseCaptured)(void); //the cursor is the game's (in play, no menu over it)
	int (*Look)(GeLookInfo* info); //the local player's look now, 0 (mode NONE) when there's none
	void (*ViewTurned)(float dx); //the view was turned by dx radians (the aim snap lets go of a turning view)
	void (*ShowText)(const GeTasText* lines, uint32_t count); //replaces what was shown, from any thread
	void (*Say)(const char* text); //a line at the top for a moment (the settings menu's)
	uint32_t (*Settings)(char* out, uint32_t size); //"id=value\n" for every setting of the menu, the length (out may be short)
	int (*ApplySetting)(const char* id, const char* value);
	const char* (*ExeDir)(void);
	int (*KeyDown)(const char* name); //SDL's name of a key: held now

	//api 3: a page of the settings menu (F1) of the library's own, made in init (before the menu is first shown)
	int (*MenuPage)(const char* title, const char* about); //its number for the rows, -1 without a menu
	//a row: value (null for a row only selected, its text the library's until the next call), adjust (left -1, right +1), select (Enter); any may be null.
	//they're called on the renderer's thread
	void (*MenuRow)(int page, const char* label, const char* hint, const char* (*value)(void* user), void (*adjust)(void* user, int dir),
		void (*select)(void* user), void* user);
	//a key chosen on the page (kept in the launcher's file as LAUNCH_KEY_<ID>), otherwise until one is chosen
	void (*MenuKey)(int page, const char* id, const char* label, const char* hint, const char* otherwise);
	uint32_t (*MenuKeyName)(const char* id, char* out, uint32_t size); //the key chosen on that row ("" none), its length
	int (*MenuOpen)(void); //the settings menu or the overlay is up (keys are its then, not the library's)

	//api 4: the guest's clock against the host's, 1 real time, 0.5 half as fast (1/64..64); the sound follows
	void (*SetSpeed)(double speed);

	//api 5: 0 with bunnyhopping off or no local player hopping
	int (*Movement)(GeMoveInfo* info);

	//api 6: a setting of the Cheats page (a tool, not part of play): the value that turns it off ("" one
	//with none of its own), null for a setting that isn't one
	const char* (*CheatOff)(const char* id);
	//the window as shown (the game's overlays, and the library's lines unless it hides them) and its sound into a video file (ffmpeg),
	//w x h at fps, timed by the game's time less what a skip jumped. needs a window; 0 and why in err when it can't.
	//both from the game's thread, Stop finishes the file before it returns
	int (*StartVideo)(const char* path, uint32_t w, uint32_t h, uint32_t fps, char* err, uint32_t errSize);
	void (*StopVideo)(void);

	//api 8: the clip matrix of the local player's view as last drawn (16 floats, a row vector (x y z 1) times it), 0 none yet
	int (*ViewClip)(float* m);
	//quiet: the frames run unseen and unheard, the window keeps the picture it shows and the sound is silent.
	//let go, the pictures the game shows are put back as they were when it was taken (none of the quiet ones):
	//let go after loading back a state saved before it, then the frames drawn after
	void (*SetQuiet)(int on);

	//api 9: a window of the library's own, drawn with the game's Dear ImGui (the library builds the same
	//sources in and draws into this context, with these allocators, from DrawUi). 0/null without ImGui
	uint32_t (*UiVersion)(void); //IMGUI_VERSION_NUM the game was built with, 0 none
	void* (*UiContext)(void); //the ImGuiContext
	void (*UiAllocators)(void** alloc, void** free, void** user); //ImGui's, to share its heap
	//the library's window is up: the mouse is free and works it (not the view), ImGui gets the window's
	//events, and the library's keys still come to Key unless a text field is being typed in
	void (*SetUiOpen)(int on);
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

	//api 2
	void (*Boundary)(void); //between two frames on the game's thread, before anything of the next: states, Sleep
	int (*Input)(int chan, GeTasPad* pad); //the raw pad (host keys, wheel, a gamepad) before the port's rules: 1 when the library's replaces it
	void (*ControllerUpdate)(uint32_t controller); //the local controller's update starts, on the game's thread
	int (*Key)(const char* name, int down, int repeat, int mods); //a window's key (renderer's thread): 1 when it's the library's; mods bit 0 shift, 1 ctrl, 2 alt

	//api 8
	void (*Shot)(const GeTasShot* shot); //the local player's bullet: fired, landed, a guard hurt (game's thread)

	//api 9
	void (*DrawUi)(void); //inside the overlay's ImGui frame (renderer's thread), every frame while SetUiOpen(1)
} GeTasLib;

//the one export, null when the library doesn't want to run
typedef const GeTasLib* (*GeTasInitFn)(const GeTasHost* host);
#define GE_TAS_INIT_NAME "ge_taslib_init"

//optional, asked before the runtime starts (nothing else of the game is up yet): what it needs from
//the start. bit 0: the runtime's deterministic mode
typedef uint32_t (*GeTasEarlyFn)(void);
#define GE_TAS_EARLY_NAME "ge_taslib_early"
#define GE_TAS_EARLY_DETERMINISTIC 1u

#ifdef __cplusplus
}
#endif
#endif
