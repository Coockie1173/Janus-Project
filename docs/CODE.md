# The port's code

A reference for everything in `port/`: what each file does, the functions in it, the game
functions it hooks, and every address and struct offset of the game's memory it relies on.
The evidence for each named function is in `config/names-manual.tsv`, the reasoning behind each
fix in the comment at the top of its file.

Addresses are for **SJBE52** (NTSC-U) only. Offsets are hexadecimal, values are big-endian in
guest memory. The recompiler and the hardware runtime (`wiikit/`) are not covered here, see its
own README.

Contents:

1. [How the layer works](#1-how-the-layer-works)
2. [Memory map](#2-memory-map)
3. [The game's functions](#3-the-games-functions)
4. [The files](#4-the-files)
5. [Environment variables](#5-environment-variables)
6. [Files read and written](#6-files-read-and-written)
7. [The launcher](#7-the-launcher)

## 1. How the layer works

The port is linked into `wiiboot` through `port/goldeneye.cmake` (`-DWIIKIT_EXTRA`). It
registers itself with one static object, `RtGameLayer layer("GoldenEye 007", install)` in
`goldeneye.cpp`, and the runtime calls `install()` before the game's `__start`.

**Hooks.** `ppc_hook("name", fn)` replaces a recompiled function and returns the original,
`ppc_lookup(address)` returns a recompiled function to call without replacing it. A name can
only be hooked when it is listed in `config/hooks.txt` (the recompiler gives each listed
function a slot) and named in `build/names.tsv` (from `config/names-manual.tsv`). A hook is a
`void(PPCContext&)`: arguments in `c.r[3]`.., floats in `c.f[1]`.., the return address in `c.lr`,
the result in `c.r[3]`.

**Memory access.** `ld8/ld16/ld32/st8/st16/st32` read and write guest memory, `ge::ldf/stf`
(`ge.h`) do floats. The `r` variants (`ld32r`, `st8r`...) skip the hardware-register check.
`PPC_MEM(addr)` is the host pointer. `ge::guest_ptr(p)` is true for MEM1
(`80003000..817FFFFF`) and MEM2 (`90000000..93FFFFFF`); every pointer read from the game is
tested with it before it is followed.

**Patterns used all over:**

- *Set for one call, put back after.* A hook changes what the game reads, calls the original
  and restores it, so saves, options and other readers never see the changed value (the mouse's
  turn in `look_apply`, the sprint bit, the stick's sensitivity, the tank's drag, the hud's
  timer).
- *Only replace what is ours.* A value is written only when the game's own is there or the one
  the port wrote last (`fov_apply`, the frame rate's highest, `Keep` in `tweaks.cpp`).
- *Call the game on its own thread.* A game function is called from inside a hook with a copy of
  the context (`PPCContext saved = c; ...; c = saved;`), with room made under the guest stack
  when it needs a buffer (`collision.cpp`, `prompts.cpp`).
- *The local controller.* Everything per player goes by `local_controller()`, the controller
  the game's own test (`fn_80108D70`) said this console plays, found once a frame in
  `controller_update` and valid for 4 frames.

**Threads.** Hooks run on the game's (guest) thread. The settings menu and overlay are drawn on
the renderer's thread, the classic controller's filter runs wherever the pad is read, the debug
aids run threads of their own. What crosses threads is a `std::atomic`.

**The frame.** `frame_tick` (the hook on `ge_frame_tick`, `framerate.cpp`) is the port's
heartbeat. The engine spins in it between frames, so the per-frame work is done when the
game's frame count changes:

```
frame_tick
  apply_rate                 a changed frame rate or fast forward
  each_frame(frame)          goldeneye.cpp
    crash_test               GE_CRASH_TEST
    gecko_apply              gecko codes
    tweaks_apply             extras menu, unlock everything
    fov_apply                the field of view
    cutscenes_frame          skip cutscenes
    mouse_frame              unused mouse motion dropped
  autosave_frame(c)          a checkpoint's save, a step a frame
  collision_tick             the collision viewer goes off (every tick)
  orig_tick                  the game's own frame
```

**Install order** (`goldeneye.cpp`): hosts, fov, gecko, crash handler, profiler, debug, fps,
native, cutscenes, qte, tweaks, movies, files, records, zones, tank, body, noclip, stamina, collision,
autosave, weapons, prompts, then `wpad_set_classic(true)` (keys, mouse and gamepads all map to
the classic controller), mouse, overlay.

## 2. Memory map

`r13` is `806F0F80` (the small data base); `-0x50B0(r13)` is `806EBED0`.

### 2.1 Globals

| Address | Type | What | Used by |
|---|---|---|---|
| `80574AB8` | vtable | `XItemHandler_PlayerTank` | tank |
| `8057A8A0` | 12 ptrs | the text tags (`<ACT `, `<C0>`, `<menuSELECT>`...) | prompts |
| `805C94A8` | object | the app (`ge_reload_checkpoint`'s r3) | names |
| `805C963C` | float | `LevelTimer`, seconds played in the level | cutscenes, tweaks |
| `805D10B8` | object | the memcard (save) manager, see 2.9 | autosave |
| `805D1804` | u32 | hash of the level that is loaded (`01080004` is the front end) | autosave, cutscenes |
| `805D1844` / `805D1848` | u32 / table | tutorials: count, rows of 0x68 | names |
| `805D4020` | 4 × 0x1C | the actions on offer, see 2.4 | input |
| `805D43EC` | ptr | the player made last (not the local one online) | notes only |
| `805D5460` | struct | network state: `+0x10` non-zero on the host, `+0x30` 1 or 2 in a network game | input |
| `805DE6A0` | struct | the profile's settings and progress, see 2.10 | tweaks, input |
| `805E05AB` | u8 | `DisplayLevelTimer`, set in a time challenge | tweaks, autosave |
| `805E05AC` | float | `MaxTimeChallenge`, the hud shows this minus the time | tweaks |
| `805F4CCC` | object | the world, see 2.6 | collision |
| `8060B3F0` | OSMutex | the heap's lock | names |
| `80641F60` | 9 × 4x4 | the renderer's matrix block, see 2.8 | native |
| `806421E0` | 4x4 | world to clip matrix of the camera applied last (a row vector times it) | collision, zones |
| `80642220` | 4x4 | the second matrix the portal clip uses | zones |
| `80643040` | table | the heaps | names |
| `80643080` | object | the save file (`ge_save_write`'s r3) | autosave |
| `806473CC` | object | the file system, see 2.9 | files |
| `806E95F8` | 4 floats | aim limits in play, degrees: pitch from, to, yaw from, to | input |
| `806E9608` | 4 floats | the same with state flag 0x80 (narrow) | input |
| `806E96D8` | u8 | picks which throttle step the tank uses | tank |
| `806E96E8`, `806E96F8` | float | the tank's throttle step a call | tank |
| `806E9E40` | u32 | `-0x7140(r13)` the engine's lowest frame rate | framerate |
| `806E9E44` | u32 | `-0x713C(r13)` the engine's highest frame rate (30 in play) | framerate |
| `806E9E4C` | float | `-0x7134(r13)` the engine's current rate, every time step comes from it | framerate |
| `806E9E50` | float | `-0x7130(r13)` the frame's time in seconds | qte |
| `806E9E64` | float | `-0x711C(r13)` the time scale | qte |
| `806EB694` | ptr | the app/view settings object, see 2.2 | fov, cutscenes, tweaks |
| `806EB698` | ptr | a player (set while any exists: play, not menus) | `ge.h` |
| `806EB69C` | u8 | 1 while the pause menu is up | `ge.h` |
| `806EB9D8` / `806EB9DC` | struct | the restart point (0x37F8 bytes from `..DC`) | autosave |
| `806EBED0` | u32 | `-0x50B0(r13)` frames run | `ge.h` |
| `806EBEE8` | u64 | `-0x5098(r13)` the next frame's time in ticks | framerate |
| `806EC00C` | u32 | `ge_save_write`'s state, 11 once the file is written | autosave |
| `806EC298` | 2 × s32 | the screen's offset, x then y (`-0x4CE8(r13)`) | zones, native |
| `806EC2BC` | 6 × u8 | matrix dirty bytes, see 2.8 | native |
| `806EC2C8` | ptr | `-0x4CB8(r13)` the view: width at `+6`, height at `+8` | zones |
| `806EC2D8` | ptr | the pads' object, pressed buttons at `+0x80 + pad * 8` | names |
| `806ED29C` | float | 30, the button bash's rate | qte |
| `806ED2A0` | float | 5, a bash press at 30 fps (and the mark it is won under) | qte |
| `806ED2A4` | float | 0.2, the bash's smoothing | qte |
| `80774704` | u32 | Extras menu data, `80580990` shows the unused pages | tweaks |
| `8077470B` | u8 | Extras menu data, `0x19` with the above | tweaks |

### 2.2 The app object (`*806EB694`)

| Offset | Type | What |
|---|---|---|
| `+0x328` | float | the view's angle in degrees, 49 in play |
| `+0x338` | u32 | the entrance `StartLevelChange` loads the level at (1 is a mission's start) |
| `+0x354` | u32 | the step the level's load is at, `0x1A` waits for "Continue" |

### 2.3 The menu object (`ge_menu_pending`'s r3)

| Offset | Type | What |
|---|---|---|
| `+0x220` | u32 | the current page's index |
| `+0x2C8` | ptrs | the pages; a page's `+0xC` (u16) is the item picked, `0x13..0x16` the campaign's difficulties |
| `+0x7D4` | u32 | the pending level's hash |
| `+0x7DC` | u32 | the pending entrance |
| `+0x7E7` | u8 | split screen |
| `+0x7E9` | u8 | the pending action runs once this is over 2 |
| `+0x7EC` | u32 | the action: 1 tally continue, 2 quit, 3 restart level, 4 restart from checkpoint, 5 load the pending level |

### 2.4 The controller (`ExPlayer` in the Ghidra project)

One for every player, `ge_controller_update`'s r3. The tank's is another class with the same
front part.

| Offset | Type | What |
|---|---|---|
| `+0x000` | ptr | vtable (`80574AB8` for the tank) |
| `+0x004` | ptr | its entity (the player), see 2.5 |
| `+0x500` | 2 floats | the move stick, x then y |
| `+0x52C` | float | tank: where the view looks, radians |
| `+0x534` | u32 | actions pressed this frame |
| `+0x538` | u32 | actions held |
| `+0x578` | float | the frame's time step in seconds |
| `+0x57C`, `+0x57D` | u8 | look flags (traces only) |
| `+0x590` | 4 floats | the step `ge_look_apply` hands the body |
| `+0x5E4` | u8 | in a cutscene (set with state 0x22) |
| `+0x5ED` | s8 | holstered |
| `+0x704` | u32 | `PlayerState`, see below |
| `+0x708` | u32 | state flags, `0x80` narrow aim limits |
| `+0x71C`, `+0x720` | float | the angle the view is drawn with (aiming changes it) |
| `+0x968` | 4 floats | velocity |
| `+0x978` | float | look pitch rate, radians a second, negative up |
| `+0x97C` | float | look yaw rate, radians a second |
| `+0x99C`, `+0x9A0` | float | tank: the aim's pitch and yaw |
| `+0xA5C`, `+0xA64` | float | tank: the stick's throttle and the throttle |
| `+0xBAA`, `+0xF58` | u8 | set by the actions' update from bit 4 held while moving |
| `+0xBD9` | u8 | cutscene: the stick moves the aim inside the cutscene's view |
| `+0xC40` | struct | inventory: count at `+8` (`+0xC48`), items at `+0xC` (`+0xC4C`) |
| `+0xC96`, `+0xC97` | u8 | the slot's item in the hand, the one being changed to |
| `+0xD00` | u8 | aim is held |
| `+0xD10` | float | the aim blend, 0 to 1 in about eight frames (`+0xF48` its opposite) |
| `+0xE59` | u8 | sprinting |
| `+0xE5C` | float | the sprint left, seconds (0 to 3.1) |
| `+0xE64` | u8 | the sprint button is still down |
| `+0xE78` | 4 floats | the fall's velocity |
| `+0xE90` | float | the distance fallen |
| `+0xED4` | u8 | aim on |
| `+0xEDC`, `+0xEE0` | float | the aim's pitch and yaw inside a cutscene's view, radians |
| `+0xF88` | u8 | tank: 1 skips `ge_tank_drive` |
| `+0x1014` | 4 floats | the cutscene's aim limits, degrees |
| `+0x1024` | u32 | cutscene: non-zero when the game reads the look stick itself |
| `+0x1124` | float | cutscene: the yaw the script's limits are around |

**Player states (`+0x704`):** `0x00` Standing, `0x02` Moving, `0x03` Sprinting, `0x04` Landing
(in the air), `0x05` Death, `0x08` ClimbVault, `0x09` Melee, `0x22` FPSCutscene, `0x23`
StartSlide, `0x24` Sliding, `0x25` EndSlide. The player has the view in 0, 2, 3 and 4.

**Action bits (`+0x534`, `+0x538`, masks):** `0x4` action and sprint (the sources' "bit 4"), `0x200` next weapon,
`0x4000` previous weapon. A cutscene's button number n is action bit `1 << (n - 1)`.

**An action on offer** (`805D4020`, 0x1C each): `+0` its id (`46500001` none), `+0x10` the
button's bit, `+0x14` 0 press / 1 held / 2 let go, `+0x15` set when the stick has to be
forward.

**An inventory item** (0x10 each): `+0` id (`02000000` none), `+4` how many, `+0xE` the slot's
number (`0xFF` none).

### 2.5 The entity and its body

The entity (a player, a prop, a "world item"):

| Offset | Type | What |
|---|---|---|
| `+0x98` | 4x4 | its matrix in the world, as new as the last time the game asked |
| `+0xB8`, `+0xC0` | float | two of the matrix's floats, the tank hull's heading is their `atan2` |
| `+0xDC` | float | yaw, radians, growing to the right |
| `+0xF8` | 3 floats | position |
| `+0x14C` | ptr | its objects, see 2.7 |
| `+0x158` | ptr | its body |
| `+0x16C` | ptr | its view object; the view's `+0x768` is the pitch, radians, negative up |
| `+0x250` | | computed from the yaw |

The body:

| Offset | Type | What |
|---|---|---|
| `+0x00` | ptr | vtable: `+0x18` its step (`ge_body_update`), `+0x24` the shape getter (`800F24E0` for people), `+0x5C` `ge_body_move` |
| `+0x04` | ptr | its entity |
| `+0x08` | u16 | flags (masks): `0x1` no gravity, `0x2` no step through the collision, `0x8` not collided with |
| `+0x0A` | u16 | contact flags (masks): `0x1` on a floor, `0x2` at a wall, `0x8` a ceiling |
| `+0xBC` | 3 floats | the frame's step from the controller |
| `+0xEC` | vector | the push out of the collision |
| `+0xFC` | vector | the frame's move |
| `+0x15C` | float | fall speed |
| `+0x18C` | 16 floats | the shape in the item's space: kind last (`+0x3C`), 0 a sphere (centre, radius at `+0x10`), 1 a capsule (foot, axis at `+0x10`, radius at `+0x20`) |

### 2.6 The world, maps and collision geometry

The world (`805F4CCC`): `+0x1C` map count, `+0x20` the maps, `+0xA4` the item lists (`+0x0`
all items, `+0x8` what a body may touch; a node's `+4` is the next, `+8` the item).

A map: `+0x1C` its file, `+0x20` its hash, `+0x138` placement count, `+0x13C` placements,
`+0x348` its index.

A file: `+0x24` the `GEOM` header (`47454F4D`). The header's lists are two shorts and an
offset: `+0x48` entities (16 bytes an entry), `+0x50` placed entities (20 bytes), `+0x70` map
data (16 bytes). An entry: `+0` hash, `+4` top bit set while loaded, `+0xC` the pointer.
Offsets in the file are relative to where they are stored.

Map data: `+0x84` zone count, `+0x88` zones, 0x8C bytes each, the zone's entity hash first. A
zone's `+0x70` is its rectangle, `+0xA8` portal link count, `+0xAC` the links (8 bytes each).

A portal: 0x44 bytes, the quad's four corners at `+0x14` (12 bytes each).

A placement: 0x9C bytes, a 4x4 matrix first, the entity's hash at `+0x80`.

A geometry entity: `+0` vtable, `+4` flags (`0x100` collides), `+0x40` variants (the byte at
`+0x43` a bit per variant, the other 24 bits an offset from `+0x40`; bit 0 is the mesh a body
collides with), `+0x4C` its file, `+0x52` (u16) its part index. By vtable:

| Vtable | Kind | Layout |
|---|---|---|
| `80591008` | split | `+0x54` part count, offsets to the parts from `+0x5C` |
| `80590FB8` | list | `+0x54` (u8) count, hashes from `+0x58`, 0x14 apart |
| `8058B9D8` | mesh | `+0x58` strips, `+0x5C` vertices (16 bytes each), `+0x68` a u16 of flags a triangle, `+0xA4` strip count |

A strip: a 0x20 header (`+0` u16 triangles, `+4` u16 flags, `+8` size of what follows), then GX
triangle strips (`+2` u16 vertex count, then 8 bytes a vertex, the position's index first).
Strips with flags `0x101` and triangles with `0x200` are not collided with.

### 2.7 Objects (what moves)

An item's objects from `+0x14C`, each: `+0x4` next, `+0x10` its own list, `+0x18` vtable,
`+0x1C` file, `+0x20` hash, `+0x28` its entry in the file's placed list, `+0x88..0xD8` what
`fn_80301960` keeps (saved and put back around a call), `+0x98` matrix, `+0x114` four slots
of animated collision records, `+0x130` children. The vtable's `+0x50` is its scoop and tells
the kind: `8031D920` a group, `803144D0` an entity, `80308C20` something skinned.

A collision record (`EXItemAnimator_Entity`, 0x1C bytes for each part): top bit of the first
word set when a buffer of animated vertices is at `+8`. A slot is a pointer and a count.

### 2.8 The renderer's matrices

`80641F60`, 4x4 matrices 0x40 apart: `+0x000..0x080` the three outer ones A0..A2, `+0x0C0` the
model matrix M, `+0x100` N, `+0x180..0x200` the products P = A × M, `+0x240..0x2C0`
Q = P × N. Dirty bytes at `806EC2BC`: `+0` the Qs, `+1` everything, `+2` P0, `+3` P1, `+5` P2.

### 2.9 Files and saves

The file system (`806473CC`): `+0x24` which of the two reads is under way, `+0x38` (u8) a
piece is being read, `+0x39` set by the start, `+0x3C` the piece's size, `+0x44` the two
requests, `+0x58` the DVD file. Vtable `+0x14` opens, `+0x20` asks whether a piece is under
way (`fn_80292520`).

A request: `+0` its state (2 while a piece is read), `+0x120` its place on disc, `+0x128`
bytes still to read, `+0x12C` its place in memory. Pieces are up to 0x10000 bytes.

The memcard manager (`805D10B8`): `+0x8` state (0 or 10 idle, 0x12 writing, 0x13 written),
`+0x18` the slot of the loaded profile (3 slots).

A movie widget: `+0x8E` (u8) finished. Vtable `8057362C`, `+0xE0` is `ge_movie_start`.

### 2.10 The profile (`805DE6A0`)

| Offset | Address | What |
|---|---|---|
| `+0x38` | `805DE6D8` | a bit a mission, it can be picked |
| `+0x40..0x4C` | `805DE6E0..EC` | a mask a difficulty, a bit a mission done on it |
| `+0x104`, `+0x108` | `805DE7A4`, `805DE7A8` | look sensitivity across, up and down |
| `+0x12C`, `+0x130` | `805DE7CC`, `805DE7D0` | the same aiming down the sight |

### 2.11 A cutscene's input object (`ge_cutscene_input`'s r3)

| Offset | Type | What |
|---|---|---|
| `+0x00` | 4 × u32 | the buttons, action bit number plus one (`FFFFFFFF` none) |
| `+0x14` | u32 | the kind, `7B000000` plus 1, 3, 5 buttons in turn / 2, 4 all at once / 6 a bash |
| `+0x3C` | float | the bash's level, 0 to 100 |
| `+0x40` | float | the bash's time left |
| `+0x44` | float | the smoothed level |
| `+0x48` | float | the bash's climb |
| `+0x50` | u32 | which of the four is next |
| `+0x60`, `+0x62` | u8 | succeeded, done |

### 2.12 Vehicles, strings, text

A vehicle (`ge_vehicle_step`'s r3): `+0x58` flags (`0x100` damps by time), `+0x130` velocity
keep, `+0x1B0` spin, `+0x1E0` three spin keeps, `+0x1FC` wheels, `+0x200` wheel count. A wheel
is 0x90 bytes: `+0x54` spin, `+0x84` drag.

The hud: `+0x1708` the prompt's string, elements `0x1C` and `0x1D` its two button sprites.

A hud/menu string: `+0x14` its font (`07000033` is the one with only buttons).

Font characters: `0x2701..0x273A` buttons (`0x2708` and `0x271E` the left stick), `0x2800`
plain and `0x2801..0x2803` colours (`0x2801` the highlight), `0xA0` a space that is not wrapped
at. The classic's and the pro's button characters are the `kGlyphs` table in `prompts.cpp`.

Hash prefixes: `0181xxxx` levels, `0108xxxx` the front end, `7Dxxxxxx` menus, `7910xxxx`
buttons, `7C10xxxx` action names, `7Bxxxxxx` cutscene input kinds, `43xxxxxx` texts
(`43000204` "Select").

### 2.13 KPAD's classic controller bits

| Bit | Button | Bit | Button |
|---|---|---|---|
| `0x0002` | Left | `0x0080` | ZL (aim) |
| `0x0004` | ZR (fire) | `0x0200` | R |
| `0x0008` | x | `0x0400` | Plus (pause) |
| `0x0010` | a | `0x2000` | L |
| `0x0020` | y | `0x8000` | Right |
| `0x0040` | b | | |

`ClassicState::extra` is the port's own: bit 0 "Extra 1" the sprint key, bits 2..4 "Extra 3"
to "Extra 5" the weapon slots.

## 3. The game's functions

Hooked (replaced, the original called from inside) unless marked *called* (through
`ppc_lookup`) or *reference* (named for reading the code, not used by the port).

| Address | Name | Arguments and result | File |
|---|---|---|---|
| `80006310` | `__start` | entry | reference |
| `80292D50` | `main` | traced with `GE_TRACE` | goldeneye |
| `802929A0` | `ge_frame_tick` | one pass of the main loop, the frame limiter | framerate |
| `804C2FA0` | `VIWaitForRetrace` | a short sleep above 60 fps | framerate |
| `804BBE30` | `OSSleepTicks` | (64-bit ticks in r3:r4), *called* | framerate |
| `802BBE70` | `ge_frame_draw` | (r3 = 1 leaves the scene out) | cutscenes |
| `80108A60` | `ge_controller_update` | (controller, f1 time step) once a frame | input |
| `80108D70` | `fn_80108D70` | (controller) -> 0 if this console plays it, *called*; 0 for every controller on a network game's host, so `controller_is_local` calls it with `805D5470` cleared there | input |
| `80101DC0` | `ge_look_input` | (controller, pad) reads the look stick; traced with `GE_TRACELOOK` | debug |
| `80101EC0` | `ge_look_apply` | (controller) rates times the time step into rotation | input |
| `801CDC30` | `ge_look_sens` | reads the look sensitivities | input |
| `801CDE70` | `ge_look_sens_ads` | the same aiming down the sight | input |
| `8010D970` | `ge_sprint_update` | (controller) starts and ends a sprint | input |
| `8010DAF0` | `ge_sprint_end` | (controller) | input |
| `8010DB10` | `PlayerSprint` | (controller) refills it | input |
| `8010A260` | `ge_action_update` | (controller) the actions on offer, climb and vault | input |
| `80142E60` | `ge_tank_update` | (controller) the tank's update | reference |
| `80140600` | `ge_tank_look` | (controller) the tank's `ge_look_apply` | tank |
| `801478C0` | `ge_tank_drive` | (controller) the throttle | tank |
| `8036B310` | `ge_vehicle_step` | (vehicle, f1 time step) | tank |
| `800F0FC0` | `ge_body_update` | (body) move, then collide | body |
| `800EF380` | `ge_body_move` | (body) | reference |
| `800F12D0` | `ge_body_collide` | (body) | reference |
| `802CF760` | `ge_contacts_solve` | (scoop, body, out push, out flags, passes, callback) | reference |
| `8015A430` | `ge_weapon_input` | (controller) the weapon buttons, once a frame | weapons |
| `8014F490` | `ge_weapon_cycle` | (controller, 1 or -1); calls the search from `8014F5A0` and `8014F600` | reference |
| `8014B0A0` | `ge_weapon_next_slot` | (controller, item, direction, with ammo, weapons only) -> item or -1 | weapons |
| `8014AEC0` | `fn_8014AEC0` | (controller, item, weapons only, holstered, with ammo) -> usable, *called* | weapons |
| `8001C4F0` | `ge_cutscene_input` | (input) once a frame | qte |
| `802351F0` | `ge_menu_pending` | (menu) runs the pending action | cutscenes |
| `8000C370` | `StartLevelChange` | (app, level hash, ...) | cutscenes |
| `80008940` | `ge_reload_checkpoint` | (app) | reference |
| `800CB010` | `ge_record_restart` | (-, entrance id, entrance hash) fills the restart point | autosave |
| `800AF650` | `ge_save_game` | (manager, slot, delete, rename, checkpoint) -> 0 busy, 1 written, 5 failed, 6 no room, *called* | autosave |
| `802B1330` | `ge_save_write` | (save file, slot, buffer, flags); flag bit 0: no "Saved" box | autosave |
| `8018CD40` | `ge_hud_update` | (hud) | tweaks |
| `800C0B70` | `ge_enemy_bullet_scale` | (levels) -> f1 the mission's value by difficulty for a guard's bullet | custom |
| `800C0B90` | `ge_player_shot_scale` | (levels) -> f1 the same for the player's shot at a guard | custom |
| `800C0BB0` | `ge_enemy_fight_wait` | (levels) -> f1 seconds a guard that saw the player waits before it fights | custom |
| `800C0BD0` | `ge_hit_protection_time` | (levels) -> f1 seconds after a hit in which a weaker one doesn't hurt | custom |
| `800C0C70` | `ge_enemy_melee_damage` | (levels) -> f1 a guard's melee, read as the guard is made | custom |
| `800C0C90` | `ge_enemy_fire_wait` | (levels) -> f1 times the weapon's time a guard has its target before it fires | custom |
| `8022F4D0` | `ge_setting_get` | (profile, setting's hash, pad, -, as it is) -> f1 a slider's value, 0 to 1 | custom |
| `8022E2F0` | `ge_setting_step` | (profile, setting's hash, -1 or 1, pad) -> 1 when it moved | custom |
| `8022DAE0` | `ge_choice_text` | (profile, setting's hash, pad) -> the text of its value | custom |
| `8022D000` | `ge_choice_step` | (profile, setting's hash, -1 or 1, pad) | custom |
| `80239240` | `ge_slider_draw` | (f1 how full, menu, x, y, width, height, colour, 1) in a screen of 640 by 448 | custom |
| `800517E0` | `ge_npc_hit` | (guard, hit): the hit's `+0x30` attacker, `+0x3C` flags (bit 0 the head), `+0x44` damage; the guard's health `+0x3A8` of `+0x3AC` | custom |
| `8023D740` | `ge_menu_pointer_update` | (the menus' pointer, -, pad) its input, once a frame | pointer |
| `8023D200` | `ge_pad_confirmed` | (front end, pad) -> the pad pressed select | tweaks |
| `8019A080` | `ge_show_tutorial` | (hud, tutorial's hash) -> shown | tweaks |
| `800C2E00` | `ge_unlock_level` | (item's hash) -> the level before the one it opens at | tweaks |
| `80231320` | `ge_menu_locks` | (menu): sets the locked rows of its page | tweaks |
| `802AD300` | `ge_movie_start` | (widget, path, x, y, width, height, audio track, loops) | movies |
| `8037F0F0` | `ge_file_read_start` | (file system, request, size) | files |
| `8037E610` | `ge_file_read_done` | (file system) | files |
| `8029DA30` | `ge_records_new` | (slot, parts) | records |
| `8029DAA0` | `ge_records_release` | (slot) | records |
| `8029D900` | `ge_records_destroy` | (slot, delete it too) | records |
| `80342A50` | `ge_heap_free` | (block) | heap, records |
| `80351AD0` | `ge_sweep_update` | (shape) | sweep |
| `8032EE20` | `ge_zone_root` | (world, position) a view's portal walk | zones |
| `802F33C0` | `ge_zone_at` | (tree, position) -> the zone's number | zones |
| `8032FAA0` | `ge_zone_walk` | (world, map, zone, rectangle, depth) | zones |
| `8032F2C0` | `ge_portal_clip` | (world, portal, link, rectangle, out) -> 0 when nothing is left | zones |
| `80293E30` | `ge_camera_apply` | (camera); returns to `8017051C` for the player's view, `80007694` for the level's camera | collision |
| `8032D380` | `ge_world_draw` | (world) | collision |
| `80301960` | `fn_80301960` | (object, parent's matrix, flags word) -> the object's world matrix, *called* | collision |
| `800DDFC0` | `GetTextMsg_MENU_Text` | (text's hash, answer) -> 1 when there; also *called* | prompts |
| `801A6C20` | `ge_text_format` | (source, out) copies a text, writes out its tags | reference |
| `801A6FF0` | `ge_text_action` | (r4 out, r5 tag, r6 place, r7 1 for ACT2) -> characters | prompts |
| `801A7400` | `ge_text_tag` | (r4 tag index, r5 out) -> characters | prompts |
| `801909D0` | `ge_hud_prompt` | (f1 y, hud, two button hashes, text hash, bright) | prompts |
| `80190AF0` | `ge_hud_prompt_text` | the same with the text as a string | prompts |
| `800E8DC0` | `ge_string_set` | (string, wide characters) | prompts |
| `80199040` | `fn_80199040` | (button's hash) -> its font character, *called* | prompts |
| `803AB100` | `ge_box_clip_codes` | (4x4, box, out[2]) | native |
| `803A9FA0` | `ge_skin1` | (n, 3x4, in, out) -> end of out | native |
| `803AA2E4` | `ge_skin2` | (n, 3x4, in, out, -, second 3x4, weights) | native |
| `803AB2B4` | `ge_plane_ray` | (plane, f1 epsilon, out t, origin, direction) -> hit | native |
| `803AB324` | `ge_tri_contains` | (-, a, b, c, point, normal) -> inside | native |
| `800164B0` | `ge_vec4_transform` | (v, 4x4) v = M v | native |
| `80014CE0` | `ge_vec4_sub` | (out, a, b) | native |
| `8033C7B0` | `ge_segment_closest` | (segment, point, out) -> 1 origin, 2 between, 3 end | native |
| `80297E80` | `ge_matrix_flush` | () redoes the dirty matrix products | native |
| `802F2340` | `ge_box_in_rect` | (rect, box, 4x4) -> 0 out, -1 in | native |

Other game functions the comments refer to: `fn_8012A1D0` (the controller's look update),
`fn_80108350` (copies the frame's input into the controller), `fn_801222A0` / `fn_80122460` /
`fn_801227A0` (state 0x22 entered, left, updated), `fn_80124130` (the aim inside a cutscene),
`fn_8014BCD0` ("may aim" in a cutscene), `fn_800EECE0` (the body's gravity), `fn_8035D070` (a
vehicle body's step), `fn_800074A0` (the frame's update), `fn_801703C0` (draws the player's
view), `fn_80009CB0` > `fn_80379D20` > `fn_8032BCE0` (to `ge_world_draw`), `fn_80315C10` and
`fn_80296160` (the collision's scoop and triangles), `fn_802CD850` (the collision variant),
`fn_802E2A00` (lookup by hash), `fn_8027F410` and `fn_80302EA0` (the item and object lists),
`fn_8029E070` / `fn_8029DE10` (animated vertices), `fn_80292520` and `fn_802923B0` (a read's
status and its DVD callback), `fn_8037EA70` (the file pump), `fn_800AA260` ("Creating a real
save"), `fn_8024D520` (`MenuLoadSave::UpdateNewSaveGame`, menu `7D000035`), `fn_801CF680` (an
action bit's two button hashes), `fn_8023D140` (the menus' confirm character).

## 4. The files

### `ge.h`, `guest.h` and the folders' headers

The port is a folder a concern, and each folder has a header with what its files offer the
others: `engine/engine.h`, `sound/sound.h`, `input/input.h`, `gameplay/gameplay.h`,
`cheats/cheats.h`, `fun/fun.h`, `mods/mods.h`, `net/net.h`, `ui/ui.h`, `speedrun/speedrun.h`,
`debug/debug.h`. `ge.h` includes them all (every file includes `ge.h`) and lists the folders.
`guest.h` has the game's memory: the three globals (`kFrameCount`, `kPlayerPtr`, `kPaused`),
`guest_ptr`, `ldf`, `stf`, `in_play()` (a player exists and the pause menu is not up). Limits
and defaults sit with their file's declarations: `kGameFps` 30, `kGameFov` 49 (20..120),
`kGameAdsFov` 22.5 (from 10), `kRadPerPixel` 0.0022, `kMouseSensMax` 5, `kAdsSensMin/Max`
0.05..2. A folder whose files share more than that has an inner header of its own
(`input/mouse.h`, `ui/settings/menu.h`, `speedrun/splits.h`, `mods/weapons/weapon.h`).
Includes are written from `port/` (`"debug/debug.h"`), it is on the include path with
`-iquote`. `package/` is what the Windows and Linux packages ship.

### `goldeneye.cpp`

The entry point. `install()` calls every `install_*`, hooks `main` under `GE_TRACE`, and puts
the runtime on the classic controller. `each_frame(frame)` is the once-a-frame list in
section 1. The file's header is the list of the main environment variables.

### `engine/framerate.cpp`

The engine is variable-rate: `ge_frame_tick` runs a frame once `OSGetTime` passes the next
frame's time and moves its current rate one up when the frame was on time, one down when late,
between a lowest and highest rate. The game sets both to 30 in play; raising the highest is
the whole unlock.

| Function | What |
|---|---|
| `guest_sleep(c, ticks)` | `OSSleepTicks` on the guest thread, only with interrupts on |
| `apply_rate(fps, fast)` | sets `g_gx_early_done` (above 30) and `g_present_on_frame` (above 60 or fast forward: frames shown as drawn, not at VI's retrace) |
| `vi_wait` | hook: above 60 or in fast forward a quarter millisecond's sleep instead of the wait |
| `frame_tick` | hook: writes the highest rate, sleeps up to 0.2 ms before the next frame above 60, runs the per-frame list, fast forwards, calls the game, then mends the rate after a long frame |
| `wanted_rate` | the limit, or the screen's rate (`video_refresh_rate`) when vsync is on and that is lower |
| `vsync`, `set_vsync` | wiikit's `video_vsync` / `video_set_vsync` |
| `install_fps` | hooks, `GE_VSYNC`, `GE_FPS` (`auto` is 60 or the screen's rate if lower), `GE_SKIP_RATE` |
| `set_fast_forward(on, easeRate)` | a cutscene is skipped: the rate is held at the skip rate (3) and the guest's clock jumps to the next frame (`rt_advance`) after the audio caught up (`hw_ai_behind`); `easeRate` shows each frame instead |
| `skip_rate`, `engine_rate`, `engine_top_rate`, `fps_limit`, `set_fps_limit` | getters and the setter the menu uses |

The rate fix: a frame more than three frame times late makes the limiter restart from its
lowest rate and climb by 1 a frame. After the game's tick the rate is put back to what it was
minus 1, and a climbing rate climbs 4 a frame. `GE_NO_RATE_FIX` turns it off, `GE_HITCH`
makes 60 ms stalls to test it.

### input/: `players.cpp`, `look.cpp`, `buttons.cpp`, `input.cpp`, `mouse.h`

Keyboard and mouse. The mouse does not go through the stick: its motion is added to the
view's angles once a frame. `players.cpp` has whose controller is this console's and the
player's state (`controller_update`, `look_mode`, the aim blend), `look.cpp` the mouse look
(directly, through the command online, on the right stick, the sensitivities), `buttons.cpp`
the classic's filter (wheel, sprint, aim lock, menus), `input.cpp` the settings and
`install_mouse`; `mouse.h` is what they share.

| Function | What |
|---|---|
| `joined_game()` | a network game someone else hosts (`805D5460`) |
| `mouse_on_stick()` | mode 2, or auto in a joined game (the host turns the players) |
| `local_controller()`, `local_player_state()`, `local_cutscene_locked()`, `local_aim_blend()`, `local_look_name()` | the local player, for the other files |
| `look_mode(ctrl)` | locked, view (states 0, 2, 3, 4, or a cutscene that reads the stick), aim (a cutscene with the aim free), forced (the cheat) |
| `mouse_look(ctrl, player)` | the pitch is written to the view's `+0x768` (within ±1.35), the yaw is kept in `g_look_yaw` for `look_apply` |
| `mouse_aim(ctrl)` | the aim inside a cutscene's view (`+0xEDC`, `+0xEE0`) within the game's limits |
| `look_apply` | hook on `ge_look_apply`: `g_look_yaw / dt` added to the yaw rate (`+0x97C`) for the call |
| `aim_sens_scale(ctrl)` | the sensitivity goes to the aim sensitivity by the aim blend |
| `controller_update` | hook: finds the local controller with the game's test, applies the mouse, then `ads_fov_apply`, `noclip_apply` and `stamina_apply` after the game's update; the `GE_TRACE` and `GE_TRACELOOK` logs |
| `sprint_buttons`, `sprint_frame`, `sprint_read<I>` | sprint on its own key: for the three readers bit 4 is the sprint key, b only the action |
| `action_update` | hook on `ge_action_update`: the sprint key is b too for a climb or vault |
| `wheel_buttons` | the wheel as D-pad right/left presses, 80 ms held and 60 ms apart |
| `aim_lock_buttons` | a press of aim stays aimed until the next |
| `menu_buttons` | out of play ZR is a (select) and ZL is b (back); a trigger held when a menu closes stays out |
| `mouse_stick` | the mouse on the right stick: deflection that gives the mouse's speed at sensitivity 100 (rate 2.95 rad/s, dead zone 0.12, pitch 1.25 times the yaw) |
| `look_sens<I>` | hooks: the four sensitivities read as 100 for the call while the mouse is on the stick |
| `classic_filter(chan, s)` | channel 0 only, in this order: settings, mouse stick, wheel, debug, sprint, weapons, aim lock, menu, tank |
| `install_mouse` | environment, the filter, all the hooks |
| `mouse_frame`, `mouse_applied`, `mouse_capture` | unused motion dropped once a frame; the cursor captured and hidden only in play (`in_play()`, no overlay up), free and seen in a menu, a pause or a load (`video_keep_cursor`) |

### `gameplay/fov.cpp`

`fov_apply()` writes the view's angle (`*806EB694 + 0x328`) only over the game's 49 or what
it wrote last, so a cutscene keeps its own. `ads_fov_apply(ctrl)` runs after the controller's
update: the game computes `view + (weapon's aimed angle - view) * blend` into `+0x71C`; the
weapon's angle is worked back out of that, scaled by the option over 22.5, and the result
written to `+0x71C` and `+0x720`, never wider than the view.
`DrawDelayed` (hook on `draw_delayed?`, every mesh's draw): with the view wider than 49 a mesh
whose draw matrix (entry `+4`) has no rotation and no offset sits on the eye (a level's colour
filter; its own x and y scale can be anything, Tank and Solar size theirs there); its x and y are
stretched by `tan(view / 2) / tan(24.5)` for that draw. With Disable Colour Filter
(`no_colour_filter`) the filter is left out, at any angle: `IsColourFilter` knows it by the mesh
(entry `+0x10`), flags `0x1004` and a box a unit wide from 0.397 behind to 0.48 or more ahead.
`screen_draw` (screen.cpp) sees every entry first.

### `gameplay/screen.cpp`

`Bloom` (hook on `ge_bloom`) leaves the bloom pass out with Disable Bloom. `GrainPass` (hook on
`ge_grain_pass`) leaves the grain out at 0 and below 1 has `ScreenQuad` (hook on `ge_screen_quad`)
scale the white of the grain's quad (r3) by `film_grain()`: the grain is drawn added, that colour
times the texture times an alpha of 2. `screen_draw(c)`, from `DrawDelayed`: with Inverted Flashes the flash's white quad (box 1.17912 wide, 28.2 ahead) isn't drawn, its alpha and
the frame go to `g_flash`, `g_flash_frame`. On the renderer's thread `flash_showing()` (the quad
left out this frame or the last two, in play) and `flash_draw()`: the window copied once into a
texture at the flash's first frame (`glCopyTexImage2D` from the default framebuffer), drawn on
ImGui's background list at the quad's alpha with a white glow along the edges.

### `cheats/gecko.cpp`

Codes that patch code are applied to the dol before recompiling (`tools/gecko.py`); the rest
run here once a frame. `gecko_cheats()` is true when a file named `cheats.txt` (the launcher's
Cheats tab) had codes. `gecko_load(path)` reads Dolphin's notation (`$` starts a code, `*` and
`#` are comments), `gecko_apply()` runs them. Supported types: `00`, `02`, `04` writes, `08`
serial write, `20..2E` conditions (an odd address is an endif first), `CC` on/off switch,
`E0`, `E2`. Writes outside MEM1/MEM2 are dropped.

### `gameplay/cutscenes.cpp`

Skip Cutscenes: a cutscene plays through in very long steps (the engine held at 3 frames a
second of game time, nothing waits for a frame), muted, one frame in four drawn. Each one is
its own choice: a mission's intro, a cutscene in its middle and its ending (`g_on[mission][part]`,
the settings menu's grid, `GE_SKIP_LIST`).

State machine (`Skip::Idle`, `Waiting`, `Running`):

| Function | What |
|---|---|
| `MenuPending` | hook: action 5 with a campaign level (a difficulty or Time Trial picked) at entrance 1 arms the skip; `GE_LEVEL` and `GE_ENTRANCE` replace the level or entrance |
| `LevelChange` | hook on `StartLevelChange`: Restart Mission and Continue Game (action 6) arm it too, and so does the app's update loading the next mission after "Save and Continue" (the call returning to `80009334`); any other change stops it. `GE_TRACE` logs each one |
| `Arm(level)`, `Start()`, `Stop(why)` | `Stop` ends the fast forward, fades the picture and the sound back in over half a second and logs how long the opening was |
| `StartFade()`, `FadeOut()`, `Over()` | the picture and the sound go out over half a second at normal speed (`FadeTo`, `audio_mute`), then the long steps start; `Over` is what ends a skip in either state |
| `cutscene_fade()` | how black the picture is, for the overlay; 0 while the pause menu is up |
| `cutscenes_frame()` | waiting: the level's time has to count 3 frames in a row, then a locked cutscene starts the skip; 150 frames with the controls means no opening |
| `Run()` | stops when the option is turned off, after 600 s, when a button is wanted, the player is gone or has anything back; with the opening's end known it stops half a second before it, the last long steps before that at the engine's own rate |
| `FrameDraw` | hook: `r3 = 1` for the frames the skip does not show |
| `LoadOpenings` | `config/openings.txt`: level hash, the level time its opening ends at, the mission's name; the missions of the grid, in its order |
| `LoadCutscenes`, `ArmCutscene` | `config/cutscenes.txt`: level hash, timeline hash, seconds until the picture is back, part (`middle`, `ending`), the mission's level for another map of it; with its cell on a cutscene of it that takes the player is skipped like an opening; `g_option` is the cell the running skip belongs to, `g_part` its part. An ending that runs into another map of the mission keeps the black up (`Stop(why, hold)`) |
| `SetList`, `skip_list()` | `GE_SKIP_LIST`, `LAUNCH_SKIPLIST`: ids `mission-part` (`dam-intro`), or `all`, `none`, `intros`, `middles`, `endings`; without it `GE_SKIP_CUTSCENES`, `GE_SKIP_ZUKOVSKY`, `GE_SKIP_BRIGGS`, `GE_SKIP_ENDINGS` |
| `CutsceneMessage`, `PlayerTake` | hooks: the last timeline that sent the cutscene player a message (its hash at `+0x20`) names the cutscene the player is taken by; `GE_TRACECUT` logs every message |
| `TraceFrames` | `GE_TRACE`: frame times through the easing |

### `gameplay/qte.cpp`

`CutsceneInput` (hook on `ge_cutscene_input`): with Auto Quick Time Events on, the buttons
the input waits for (`Wanted`) are put into the controller's pressed actions for the call.
`BashStep` makes a button bash frame-rate independent whether auto is on or not: the drop a
press makes is `30 * 5 * frame time`, so above 30 the rest of the 5 is taken off the level
before the call and the smoothed level set up so its fifth lands where the step's share would
have. `cutscene_input_waiting()` is true for 4 frames after an input waited for a button
(the cutscene skip stops on it).

### `gameplay/movies.cpp`

`MovieStart` (hook on `ge_movie_start`): with Skip Movies on, a movie that does not loop is
not opened and its widget's "finished" byte (`+0x8E`) is set, which is all that anything
waiting for a movie reads.

### `gameplay/autosave.cpp`

A checkpoint saves the game. The game only writes a mission's progress from the pause menu's
"Save and Quit", whose screen calls `ge_save_game` each frame until it answers; the port makes
the same calls after every checkpoint.

| Function | What |
|---|---|
| `RecordRestart` | hook on `ge_record_restart`: a checkpoint was recorded, a save is pending (not in the front end) |
| `autosave_frame(c)` | once a frame: waits for play and an idle manager, then steps the save until it answers or 1800 frames went by, and logs the result |
| `Step(c)` | calls `ge_save_game(manager, the profile's slot, 0, 0, 1)` with the registers put back after |
| `ManagerIdle()`, `CanSave()` | the manager's state is 0 or 10; there is a profile, no time challenge, no network game (`network_game()`) and no multiplayer map (level `01A0xxxx`) |
| `SaveWrite` | hook on `ge_save_write`: flag bit 0 set for the port's own calls, so no "Saved" box waits for a button |

### `engine/files.cpp`

A fix for a race in the game. `ge_file_read_start` and `ge_file_read_done` ask the drive
whether a piece is over with interrupts on; when the read's interrupt lands inside the
question a piece is counted twice, the request's rest goes negative and the read runs on over
the heap. `Atomic(c, orig)` runs both with interrupts off (MSR bit `0x8000` cleared through
`ppc_mtmsr`). `GE_FILE_RACE=off` leaves the game's own, `=force` provokes the race every
100th piece; both log a piece counted twice.

### `engine/records.cpp`

A diagnostic, nothing is changed. Every array of collision records `ge_records_new` makes is
remembered with its slot; a free (`records_freed`, called by heap.cpp's hook) or a release
through another slot while the slot still holds the array is logged with the guest call chain
(`call_chain`, 14 frames deep), at most 16 reports.

### `engine/heap.cpp`

Keeps the game's heap whole and tells when it isn't. The heaps (pointers at `80643040`, four
at most; `+4` the first block, `+8` the last) are blocks with a `0x10` header in front of what
is handed out: `+0` the owner's pointer (free: the block before it in its free list), `+4` a
tag record (free: the next one), `+8` the size, low bits 1 free, 4 tagged, 8 kept, `+0xC` the
size of the block in front. The game's free (`fn_8033DD90`) trusts the header.

| Function | What |
|---|---|
| `Free` | hook on `ge_heap_free`: a block that `Wrong` finds fault with is logged (header, call chain, the free before it) and not passed on; the others are remembered, shown to records.cpp and freed |
| `Wrong(block)` | not a pointer, in none of the heaps, free already, longer than its heap, or the block behind it has another size for it |
| `Remember`, `Before` | the last free of a block (frame, 8 callers) in a table of 65 536 by address |
| `heap_frame()` | once a frame: `Walk`s every heap unless a thread holds the heap's mutex (`8060B3F0`, owner at `+8`); the first block that doesn't lead to the next is logged with the memory around it, once |
| `NetworkLines` | a network game's start and end logged with the walks and the frees left out since, without being asked |
| `call_chain(c)` | the guest's callers from the stack, by name |

### `engine/sweep.cpp`

`SweepUpdate`, a hook on `ge_sweep_update` (`80351AD0`): the count of steps a moving physics
shape is swept in (`+0x58`: the way its body went over 0.75 of the shape's size, unbounded in
the game) is kept to 64, and a shape that asked for more is logged with its body's places.

### `engine/zones.cpp`

The portal walk. The moment the camera is past a portal's plane the zone it came out of was
dropped and the level went black for a moment in vents.

| Function | What |
|---|---|
| `Root` | hook on `ge_zone_root`: counts a view's walk, writes its line to `GE_ZONELOG`, dumps both views when one walks fewer zones than the one before (first 600) |
| `At`, `Walk` | hooks: the zone the walk starts from, zones walked |
| `Eye(eye)` | the camera's position solved out of the clip matrix (the point its x, y and w rows give 0 for) |
| `NearPortal(portal)` | the eye within `g_near` (1.5, `GE_PORTAL_NEAR`) of the portal's plane and quad |
| `Clip` | hook on `ge_portal_clip`: a portal the game drops is kept with the rectangle as it came when the eye is near it |

Nothing is hooked when `GE_PORTAL_NEAR=0` and there is no log.

### `gameplay/tweaks.cpp`

| Option | How |
|---|---|
| Level timer | `HudUpdate`: for the call the flag `805E05AB` is set and the limit `805E05AC` is twice the level's time, so the hud shows limit - time = time |
| Unused extras menu | `Keep` holds `80774704 = 80580990` and `8077470B = 0x19` each frame |
| Unlock everything | `Keep` holds the five progress words (2.10) at -1 |
| Auto start | `PadConfirmed`: answers yes while the load's step is `0x1A` |
| No tutorials | `ShowTutorial`: answers 0 without asking the hud |
| Online unlocks | `UnlockLevel`: answers 0 for every item's hash but the Proximity Mine's (`7D060005`); `MenuLocks`: on Choose Game Mode (`7D000047`) the bytes of rows 6 to 8 at menu `+0x548` are cleared after the game's own update |

`Keep(writes, on, written)` remembers what the game had and puts it back when the option is
turned off. `tweaks_apply()` runs both once a frame.

### `gameplay/teleport.cpp`

Teleports, from eurochef's `teleport_<name>` volumes: the generic trigger asleep, with a shape, `TELE` in data 1, data 5 to 7 where it goes and data 8 the yaw.

| Function | What it does |
|---|---|
| `Find()` | the level's teleports, when the level or the number of triggers changes |
| `Inside(c, trigger, entity)` | the game's shape test (`fn_80024450`) on the entity's position |
| `Go(ctrl, entity, body, trigger)` | puts the player there and zeroes the step, the fall speed, the velocity and the fall |
| `teleport_apply(c, ctrl)` | after a controller's update, next to `bunnyhop_apply`, for a player in a state they move themselves in. It keeps no state, so online every console's stepping agrees |

### `gameplay/custom.cpp`

007 Custom: a mission as 007 Classic with the guards set by four sliders. A mission's row of
values by difficulty (levels `805D1804` `+0x34`, the difficulty the short at `805DE6CC`) is
read by a function a value; six are hooked and scale what the game read while the mode is on
(`Active()`: on and not multiplayer):

| Hook | Slider | How |
|---|---|---|
| `Bullet`, `Melee` | Enemy Damage, 0 to 2000 | times it |
| `Shot` | Enemy Health, 0 to 2000 | the player's shot divided by it, times a million at 0 |
| `FightWait`, `FireWait` | Enemy Reaction Speed, 0 to 100 | times `Slow()`, 2 at 0 and 1 at 100 |
| `ProtectionTime` | Hit Protection, 0 to 1000 | times it |

`SettingGet` and `SettingStep` (hooks on `ge_setting_get`, `ge_setting_step`) answer the
game's menu for the four sliders' hashes (`kCustomSetting` on): how full the bar is, and a
step every 40 ms. `set_custom_mode` is called by cutscenes.cpp's `MenuPending` for every
level the menu loads: on from the page's Start Mission row (`custom_start_row`), off from any
other. `custom_label`, `custom_about` and `custom_values` are for the menu and the settings
file. The page and Select Difficulty's item are modmenu.cpp's `BuildCustom` and `CustomFrame`.

`ChoiceText` and `ChoiceStep` are the switch for headshots that kill (`kCustomHeadshots`).
`NpcHit` (hook on `ge_npc_hit`): with the switch off a hit to the head is handed on without
its head bit and with the least of the attacker's damages; `GE_TRACEHIT` logs every hit.
`SliderDraw` (hook on `ge_slider_draw`) keeps where the four bars are and is where the mouse
works: a press on a bar (`menu_cursor`, pointer.cpp) takes hold of it, and a click on the
switch's row (`RowAt`) changes it. `custom_frame` lets go of a held slider when the page goes, `custom_buttons` keeps a
press that holds a slider from being a select.

### `input/pointer.cpp`

The mouse's cursor picks the menus' items. The menus have a pointer of their own
(MenuPointer, the menu's `+0x1A98`: `+0`, `+4` its place from 0 to 1, `+0x24` in use, `+0x38`
and `+0x3C` the arrow and the item under it, hotspots from `+0x44`), fed only from a Wii
Remote. `PointerUpdate` (hook on `ge_menu_pointer_update`) writes the cursor's place into it
after the game's input and puts it in use when the cursor moves; the lit item, select and the
crosshair are the game's. `pointer_frame` leaves the system's cursor out while the game draws
its own (`video_keep_cursor`). `menu_cursor` is the cursor in the menus' screen for custom.cpp;
`Cursor` reads wiikit's `video_mouse`, or `GE_POINTER` for a test.

### `gameplay/tank.cpp`

The tank's controller never reaches `ge_look_apply`, so `TankLook` (hook on `ge_tank_look`)
adds the mouse to both look rates for the call. `tank_buttons` moves the buttons round by one
in the tank (ZR does R's, ZL does ZR's, R does ZL's). Three things in a vehicle's physics
happen once a call with no time step in them; each is scaled for the call and put back:

- `VehicleStep`: every wheel's drag becomes `1 - (1 - drag)^frames`, and the body's four keep
  factors `keep^frames` (only for a body without flag `0x100`), `frames` being the time step
  times 30.
- `TankDrive`: the two throttle steps times `frames`.

`ShortStep` only lets a step shorter than a 30th of a second be scaled. `is_tank(ctrl)` tests
the vtable.

### `cheats/noclip.cpp`

`noclip_look(ctrl)` (before `ge_look_apply`) zeroes the controller's velocity and step.
`noclip_apply(ctrl)` (after the controller's update) keeps its own position, moved by the move
stick along the view at 6 units a second (20 with the sprint key), writes it to the entity,
keeps the body's flags `0x1` and `0x2` set and the fall speed, velocity and distance at 0. The game's
position is taken again when it jumps more than 4 units or the player is not in a free state;
`Land(body)` clears them again.

### `cheats/stamina.cpp`

The stamina meter (a cheat). `stamina_apply(ctrl)` (after the local controller's update) keeps
the sprint left (`+0xE5C`), full (`806E9490`, or `806E9494` with the byte at `806EBA7D` set,
3.1 both) and the line (full over `806EEC80`, 10) for the renderer's thread; `stamina(s)` gives
them to the overlay, false with the meter off, outside play or after 4 frames without an
update. Sprinting takes the frame's time off it (`ge_sprint_update`), `PlayerSprint` gives a
tenth of full back a second, and a refill that gets from under that tenth (0.31) to it makes
it 0.36 of full (`806EEC84`, 1.116) at once; a new sprint needs 1.0 (`806EEC20`). With
`GE_TRACE` a sprint's start and end and every such jump are logged.

### `input/weapons.cpp`

A key for each weapon slot. `weapon_buttons` (in the classic's filter) keeps the slot whose
key went down for 250 ms. `WeaponInput` (hook on `ge_weapon_input`) presses "next weapon" for
that call, and `NextSlot` (hook on `ge_weapon_next_slot`, only for the cycle's two calls)
answers with the item in the key's slot that `fn_8014AEC0` says is usable, so everything the
game does around a change still happens.

### `input/prompts.cpp`

The game's prompts show the bound key in place of a controller button.

| Function | What |
|---|---|
| `ReadKeys()` | the key file's `[Classic Controller]` lines, read again when the file changed (checked every 500 ms) |
| `KeyOf(button, menu)`, `KeyFor(ch, menu)` | the first key of a button, as `[E]` in the highlight colour; in a menu the key a menu is used with when the button has one |
| `ShowKeys(out, count, menu)` | the button characters a tag wrote become keys |
| `TextAction`, `TextTag` | hooks: `ShowKeys` on what `<ACT>` and the other tags wrote |
| `PromptKeys(c, first, second)` | a hud prompt's two buttons as keys; the stick with b is the sprint key and forward |
| `HudPrompt`, `HudPromptText` | hooks: called without the buttons, then `PutKeys` puts the keys in front of the text |
| `TextOf(c, hash)` | calls `GetTextMsg` with a frame of its own under the stack |
| `SetTo(c, text)` | calls `ge_string_set` with characters kept under the stack |
| `StringSet` | hook: a button in any string set; a menu's lone button string is emptied and its key goes with the label set next |
| `TextMsg` | `GE_TRACETEXT`: every text looked up, once |

### `cheats/collision.cpp`

The collision viewer (a cheat). The level's collision triangles are gathered from the game's
structures (2.6, 2.7) and sent to the runtime (`gx_debug_mesh`, `gx_debug_view`), which draws
them with the game's own clip matrix in place of what the game draws in 3D.

| Function | What |
|---|---|
| `Rel`, `Readable`, `Loaded`, `Find`, `Header`, `MapData` | reading the file's lists safely |
| `Emit(p, kind)` | a triangle coloured by slope and kind: floors green, walls grey, ceilings red, props orange, moving things blue, people purple |
| `Triangle`, `Mesh`, `Entity`, `Collider` | a mesh's strips, the three entity kinds, and only what the game collides with (flag `0x100`, variant bit 0) |
| `Gather()` | zones and placements of every loaded map into `g_level` |
| `WorldMatrix(c, object, parent, flags, level)` | calls `fn_80301960`, puts back what it keeps in the object |
| `Objects`, `ObjectEntity` | an item's object tree |
| `Person`, `Capsule`, `FindEye` | a person's body shape; the one the eye is inside of is left out |
| `GatherMoving(c)` | doors and people, every frame, into `g_moving` |
| `Signature()` | a hash of every entry's pointer, changes when a part of the level comes or goes |
| `Update(c)`, `Send()` | gather again 5 frames after a change and every 30 draws; send when the mesh changed |
| `CameraApply` | hook: the call returning to `8017051C` is the player's view, gather there |
| `WorldDraw` | hook: takes the clip matrix as the player's world draw begins |
| `collision_tick()` | the viewer goes off with the option or the level |

### Mission Complete on a mod level (`mods/mods.cpp`)

`MissionPage` (hook on `ge_mission_page`, `8025A8F0`): the page reads the last level's (`805D1808`) row
in the level table without a check. For that one call the Conflict stand-in's row gets the mod's
name (texts `43FFFF00`, `43FFFF01` that `TextFind` hands out) and `805D1808` is that row's level, both put
back after. Nothing else sees a row, single player still plays without one.

### `cheats/triggers.cpp`

The trigger viewer (a cheat). The triggers that go off when someone walks into them are drawn
over the world, see-through, in the shape the game tests (`gx_debug_overlay`; collision.cpp's
camera hooks gather it and tell the runtime which views are on).

| Function | What |
|---|---|
| `TestNear`, `TestPoint`, `TestBody` | hooks: the game's three "is someone in it" tests; a trigger they are asked about is remembered with what they answered (the near test only for shaped ones, the point test not from the AI points' wake checks) |
| `ShapeOf(trigger)` | the trigger's collision shape in its map's data |
| `TriggerMatrix`, `WorldShape(c, ...)` | the trigger's angles as the game turns them, then `fn_802D3120` and `fn_802A5360` make the shape's place in the world |
| `Box`, `Capsule`, `Sphere`, `Quad` | the shapes as triangles, the diagonals left out of the outline |
| `Gather(c)` | every remembered trigger still in the world's list: yellow while someone is in it, a colour a type while asked about, grey after; the lines of the ones the player is in |
| `triggers_update(c)` | gathers and sends when it changed |
| `trigger_labels()` | the lines for the overlay (overlay.cpp `trigger_list`) |
| `trigger_type_name(type)` | what a type does, from its class's update; nullptr when not known |
| `DeathColumn`, `Column` | a death barrier without a shape: in it as fn_800429C0 tests, drawn as a column |
| `trigger_types()`, `trigger_type_shown`, `set_trigger_types_shown`, `trigger_xray` | the menu's Trigger Viewer page (`ui/settings/pages.cpp` `RefreshTriggerPage`): the types asked about, which are drawn, and the faint pass behind walls |

### `engine/body.cpp`

Vents at high frame rates. How far the collision pushes a body out depends on how deep the
step went in, and the levels lean on a 30th of a second's step to get through a vent's frame.
`BodyUpdate` (hook on `ge_body_update`), for the local player on foot (state 2 or 3) with a
step under 0.9 of a 30th: when 8 frames of pushing got less than a tenth of their way the body
is *blocked* and moves as the console's does, waiting until a 30th of a second's moves have
added up and taking them as one step. Free again when nothing is pushed, it is clear of walls
and ceilings, or 15 steps in a row got half their way. `BodyUpdateTraced` is the
`GE_TRACEPOS` variant.

### `engine/native/`

Fifteen of Eurocom's hand-written paired-single routines written as plain float code in the same
order of operations, a file a kind: `clip.cpp` (box clip codes, box in a rectangle), `skin.cpp`,
`vector.cpp` (plane and ray, point in a triangle, vec4 transform and sub, segment closest),
`matrix.cpp` (the renderer's matrix flush, Euler angles, a node's matrix), `collision.cpp`
(a triangle's plane, ray against a mesh with its plane cache), `edges.cpp` (the edge query every
guard asks each frame; its own check, since its records come from the game's allocator).
`native.h` has what they share, `native.cpp` the table, the check and the hooks. Same
order of operations (`madd` fuses the multiply-adds the console fuses; `FP_CONTRACT OFF` for
the rest). `Quant` is a quantised element as `psq_l` reads and `psq_st` writes it through a
GQR. The table `g_routines` lists each with where it writes; `GE_NATIVE=check` runs the
recompiled and the native one on the same input (`checked<I>`) and counts the differences,
`GE_NATIVE=0` uses the recompiled ones.

Constants read from the game's small data: `r2 - 0x2538` (the Newton step's constant in
`plane_ray`), `r2 - 0x2E04` (zero, `segment_closest`), `r2 - 0x31FC` and `r2 - 0x3200` (one
and zero, `BoxInRect`), `r13 - 0x4CE8` and `r13 - 0x4CE4` (the screen's offset).

### `net/net.cpp`

`install_hosts()` reads a hosts file (`/etc/hosts` format) into the runtime
(`ios_net_add_host`) so the game's server names go to a community server, and turns on the
network log with `GE_NETLOG`.

`install_blend_time()` hooks `bd_interp_set_time`: the time a joined console takes another
player to the host's newest update over is 0.025 s more than the 0.1 s the updates are apart
(`GE_NET_BLEND`), so the player never gets there early and stops. `GE_NETTRACE=1` logs every
player's place and angles after each update (`net_trace_controller`) and every thrown thing
(`TraceThrown`); `tools/netlab/` runs two hidden instances in one game against a local server
and compares their logs.

In a network game of Connect to WFC Modded the mouse turns the view itself, joined or hosting
(input.cpp): `net_command_look` puts the frame's turn into the command's +0x2C and +0x28 after
`ge_command_fill`, `input_copy` (the hook of `ge_input_copy`) takes them out again on whatever
machine runs the command and `look_apply` turns the player.

`install_command_time()` hooks `bd_command_set_time` (bdUserCommand::setTimeInSeconds): a
player's command carries the frame's time as a byte of whole milliseconds, `SetCommandTime`
keeps what that cuts off for the next frame's command so the time adds up at any frame rate.

`UpdatePing` (from `net_frame`) keeps this console's ping: in a network game of Connect to WFC
Modded with Show Ping on, a joined console's is the client's float at +0x138 (the pointer at
`805D5474`; seconds, the connection's smoothed round trip of its reliable messages), eased a
little. The others' come from the host: `PlayerWrite` (the hook of `ge_player_write`) puts a
player's ping (`HostPingOf`: the server's player with that controller, its connection's round
trip) behind the player's state as one more ranged number, `PlayerRead` (`ge_player_read`)
takes it out again and keeps it by controller (`KeepPing`); both only in a modded game.
`ScoreboardDraw` (the hook of `ge_scoreboard_draw`, `install_ping`) puts each row's ping after
its name: the name's pointer (the scoreboard's +0xC + 4 * slot) is one to "name   9 ms" for
the call, the slot's player found by the controller's +0x998 (`PingOfSlot`).
`show_ping` / `set_show_ping` are the setting (`GE_PING`).

### `ui/overlay.cpp`, `ui/dmw.cpp`, `ui/dmw.h`

The debug overlay (F3), drawn with Dear ImGui on the renderer's thread through
`video_set_overlay` (only when ImGui's sources are in `build/deps/imgui`, `GE_IMGUI`). It
shows a Dolphin Memory Engine watch list live, grouped as the file groups it or flat under a
filter; the local player's state in the top right corner when asked; "Skipping cutscene..."
in the bottom right; "Cheats active" in the bottom left while anything on the menu's Cheats
page is on or a code of the launcher's Cheats tab runs (`cheats_active()`, `ui/settings/catalog.cpp`); the
stamina meter's bar at the bottom (`stamina_bar`: green under the line, dimmer while it
refills). The settings menu and the hotkeys' line are drawn in the same frame.
`overlay_open()` is true for either window, which frees the mouse.

`dmw.cpp`: a small JSON reader (`Json`, `Parser`), `dmw_load` (a `.dmw` is
`{"watchList": [...]}` of watches and groups), `watch_address` (follows the pointer chain,
false when anything is not readable) and `watch_value` (types 0 byte, 1 halfword, 2 word,
3 float, 4 double, 5 string, 6 bytes; bases decimal, hex, octal, binary).

### ui/settings/: `menu.cpp`, `catalog.cpp`, `pages.cpp`, `draw.cpp`, `files.cpp`, `menu.h`, `hotkeys.cpp`, `hotkeys.h`

The settings menu (F1), drawn like the game's own menus. `menu.cpp` opens it and takes the
keys, the mouse and the pads, `catalog.cpp` builds the list of settings, `pages.cpp` the pages
and the level grids, `draw.cpp` draws it, `files.cpp` writes the launcher's file and the key
file; `menu.h` has their shared state (namespace `ge::menu`). Pages: root, Screen, Controls, Key
Bindings, Enhancements, Cheats, Hotkeys (and the page editing one hotkey). While it is up the
game gets no input and a game in play is paused first (a press of +); Esc with the menu closed
is + as well.

Everything the pages change is one list of `Setting` (`hotkeys.h`: id, label, hint, kind,
`get`, `set`, `step`, `show`, a slider's `frac` and `at`), built in `BuildSettings()` with
`AddToggle`, `AddChoice` and `AddRange`:

| Page | Ids |
|---|---|
| Screen | `fov`, `ads_fov`, `fps` (stops 30 to 275), `vsync`, `show_ping` |
| Controls | `mouse_sens`, `ads_sens`, `aim_lock`, `mouse_invert`, `mouse_mode`, `input`, `face_buttons`, `dead_zone` |
| Enhancements | (a row to the Skip Cutscenes grid, `kSkips`), `skip_movies`, `level_timer`, `auto_qte`, `auto_start`, `autosave`, `no_tutorials`, `no_colour_filter`, `unlock_all`, `online_unlocks`, `extras_menu` |
| Cheats | `mouse_everywhere`, `noclip`, `stamina_meter`, `collision_view` |

Key bindings (`kActions`): Fire ZR, Aim ZL, Melee L, Throw Grenade R, Reload Y, Crouch A,
Action B, Sprint Extra 1, Cycle Grenades X, Next/Previous Weapon Right/Left, Weapon 1..3
Extra 3..5, Firing Mode Up, Smartphone Down, Night Vision / Scores Minus, Pause Plus, and the
four Left Stick directions. `KeyFile` rewrites only the `[Classic Controller]` lines it
changed. `add_sprint_binding` and `AddWeaponBindings` migrate an older key file.

Other parts: `save_launch_settings` (the launcher's `LAUNCH_*` lines), `build_pages`,
`BuildHotkeyPages` / `RefreshHotkeyPages`, `bind`, `open`/`back`/`move`/`adjust`/`select`,
the drawing (`Layout` is the game's 1280 × 720 menu screen centred in the window,
`draw_frame`, `draw_rows`, `key_hint`, `DrawToast`), the input (`key_down`, `pad_down`,
`click`, `drag`, `row_at`), the dropdowns (a `Row` with `options`/`chosen`/`choose`: `OpenDrop`,
`MoveDrop`, `ChooseDrop`, `DrawDrop`, `DropAt`; `g_drop` is the row whose list is open), and what the overlay calls: `settings_init`, `settings_event`,
`settings_draw`, `settings_toast`, `settings_replay`. `settings_buttons()` is the + the
classic's filter adds.

`hotkeys.cpp`: a hotkey is a key on one setting with an action (`next`, `previous`, `set`,
`switch`: set, and back on the next press). `LoadHotkeys`, `SaveHotkeys`, `NewHotkey`,
`HasValue`, `ActionNames` / `ActionIndex` / `SetAction` (a toggle has three: toggle, on, off),
`ActionName`, `Describe`, `PressHotkeys(scancode, said)`. A press
is handed to `debug_setting` so a recording keeps the value.

### `debug/crashdump.cpp`

Crash, die and stall dumps into `build/crashes/<date>-<time>-<kind>/report.txt` (POSIX only):
what happened, the host's faulting instruction and call stack (a stall: every host thread's), every guest thread with its
call chain, the crashing thread's stack from r1, the `GE_*` / `WIIKIT_*` settings, the last
log lines; `mem1.bin` and `mem2.bin` with `GE_CRASH_MEM`.

| Function | What |
|---|---|
| `Guarded(out, name, part)` | a part of the dump under `sigsetjmp`: a fault in it skips that part |
| `GuestFunctionAt(pc)`, `WriteHostFrame` | a host address named by the recompiled function that starts closest below it |
| `WriteGameState`, `WriteSettings`, `WriteGuestStack`, `WriteMemory`, `WriteDump` | the parts |
| `OnCrash` | SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT on its own stack; lets the runtime's watched pages through (`mem_watch_fault`); a 10 s alarm ends a stuck dump (`OnStuck`) |
| `OnDie` | `g_rt_on_die`: the runtime gave up (`rt_die`) |
| `WatchForStalls` | a thread: 20 s without a new game frame, or without a picture drawn (`video_frames`, the title's fps), writes a stall dump, once a stall |
| `Listed`, `OnWhere`, `WriteHostThreads` | the stall dump's host threads: each one with a handler stack is sent SIGUSR1 and notes its pc, call stack and guest r1 (no stdio, no malloc: it goes on) |
| `crash_test()` | `GE_CRASH_TEST`, once a frame |
| `crash_thread_stack()` | a signal stack for the calling thread (`g_rt_on_thread`) |

`tools/crash_report.py` names the host frames and disassembles around the guest addresses.

### `debug/diagnostics.cpp`

The sampling profiler (POSIX only). `GE_PROFILE=F`: SIGPROF every millisecond of CPU time;
`on_prof` stores the thread and the pc as an offset in the executable (`FFFFFFFF` outside it)
and 40 return addresses. Every 10 s `F`, `F.stacks` and `F.maps` are written;
`tools/profile_report.py` reads them. `install_pgo_writer` writes an instrumented build's
counts every 15 s.

### `debug/debug.cpp`, `debug/debug.h`

Aids for finding things in the running game, each a thread or a filter started by an
environment variable (section 5). Times are seconds of guest time from start, or from the end
of a replay.

| Function | Variable |
|---|---|
| `scan_main` | `GE_SCAN`, `GE_SCANINT`, `GE_SCANPAT`, `GE_SCANWATCH`: snapshots of MEM1 and MEM2 compared |
| `poke_main` | `GE_POKE` |
| `probe_main` | `GE_PROBE` |
| `obj_main` | `GE_OBJ` |
| `findptr_main` | `GE_FINDPTR` |
| `setall_main` | `GE_SETALL` |
| `watch_main`, `watch_segv`, `watch_trap` | `GE_WATCH`: the page goes read-only, a write is logged and single-stepped |
| `tracew_main`, `peek_main` | `GE_TRACEW`, `GE_PEEK` |
| `KeysMain` | `GE_KEYS` |
| `look_trace` | `GE_TRACELOOK`, a hook on `ge_look_input` |
| `debug_classic(s)` | `GE_BUTTONS`, `GE_STICK`, and the recording |
| `debug_mouse(dx, dy)` | `GE_MOUSEMOVE`, and the recording |
| `debug_setting`, `debug_setting_due` | what the hotkeys set, recorded and replayed |

Recorded input (`GE_RECORD`, `GE_REPLAY`) is lines of text: `sens S I`, `c T buttons lx ly rx
ry lt rt extra`, `m T dx dy`, `h T id value`, and `a` where the local player appeared or went
(a level loaded or was left). Times count from the last `a`; loads never take the same time
twice, so the replay waits for each `a` to happen.

## 5. Environment variables

Options (the launcher sets these from its settings):

| Variable | What |
|---|---|
| `GE_FPS=N` or `auto` | the frame rate the engine may reach, 31..1000 (auto is 60) |
| `GE_VSYNC=1` | frames wait for the screen's refresh, the rate is the screen's at most |
| `GE_PING=0` | no ping in the scoreboard of a modded network game |
| `GE_FOV=D`, `GE_ADS_FOV=D` | field of view, and aiming down the sight |
| `GE_MOUSE=S` | mouse sensitivity, 0 leaves the mouse free |
| `GE_MOUSE_MODE=auto\|direct\|stick` | how the mouse turns the view |
| `GE_MOUSE_INVERT=1`, `GE_ADS_SENS=S`, `GE_AIM_LOCK=1` | |
| `GE_SKIP_LIST=ids`, `GE_SKIP_MOVIES=1`, `GE_AUTO_QTE=1`, `GE_AUTO_START=1`, `GE_NO_TUTORIALS=1`, `GE_NO_COLOUR_FILTER=1`, `GE_NO_BLOOM=1`, `GE_INVERT_FLASH=1`, `GE_GRAIN=F`, `GE_DOT_CROSSHAIR=1`, `GE_UNLOCK_ALL=1`, `GE_ONLINE_UNLOCKS=0`, `GE_EXTRAS_MENU=1` | enhancements |
| `GE_LEVEL_TIMER=0`, `GE_AUTOSAVE=0` | on by default |
| `GE_CUSTOM=H,D,R,P[,K]`, `GE_CUSTOM_ON=1` | 007 Custom's sliders (enemy health, damage, reaction speed, hit protection; K 0: no headshots that kill), and every mission played with them |
| `GE_MOUSE_EVERYWHERE=1`, `GE_NOCLIP=1`, `GE_STAMINA=1`, `GE_COLLISION=1`, `GE_TRIGGERS=1`, `GE_TRIGGERS_XRAY=1`, `GE_TRIGGER_TYPES=off,+13` | cheats |
| `GE_HOSTS=F`, `GE_NETLOG=1` | the hosts file, the network log |
| `GE_GECKO=F`, `GE_GECKO_EXTRA=F[:F...]` | gecko code files |
| `GE_SETTINGS=N` | start with the settings menu open on page N |
| `GE_DMW=F`, `GE_OVERLAY=1`, `GE_STATE=1` | the debug overlay |

Tuning and testing:

| Variable | What |
|---|---|
| `GE_SKIP_RATE=N` | frames a second of game time while skipping (3, 1..30) |
| `GE_ENTRANCE=N`, `GE_LEVEL=hash` | start every mission at that entrance, or load that level whatever the menu starts |
| `GE_PORTAL_NEAR=N` | the portal fix's distance (1.5), 0 off |
| `GE_NATIVE=0\|check` | the recompiled math routines, or both compared |
| `GE_FILE_RACE=off\|force` | the file read fix off, or the race provoked |
| `GE_NO_RATE_FIX=1`, `GE_HITCH=t,t,...` | the long-frame fix off; 60 ms stalls at those times |
| `GE_CRASH_MEM=1`, `GE_CRASH_DIR=D`, `GE_NO_CRASHDUMP=1` | crash dumps |
| `GE_CRASH_TEST=crash\|die\|stall\|overflow\|badstack[,seconds]` | make one happen |
| `GE_PROFILE=F` | the sampling profiler |

Traces:

| Variable | What |
|---|---|
| `GE_TRACE=1` | `main`, the frame rate, player states, angles, checkpoints, qte, movies, tutorials, the body, noclip, the tank, weapon slots, the collision viewer, the trigger viewer, the skip |
| `GE_TRACETEXT=1` | texts looked up, tags written out, prompts |
| `GE_TRACEPOS=1` | the local body's position after every update |
| `GE_TRACELOOK=1` | the look input vectors |
| `GE_ZONELOG=F` | a line for every view's portal walk |
| `GE_SWEEPLOG=1` | the highest count of sweep steps so far |
| `GE_HEAPCHECK=0` | no walk of the heaps each frame; `=1` logs that it walks |
| `GE_HEAPTEST=1` | one block is freed twice in play, to see the second refused |
| `GE_HEAPNET=1` | the first 600 frames in play count as a network game, to see the heap check's lines |

Scripted input and memory tools (`debug.cpp`):

| Variable | Form |
|---|---|
| `GE_STICK` | `"42-43:r1,0 45-46:l0,1"` hold a stick |
| `GE_BUTTONS` | `"66-66.3:0004"` hold classic buttons (`10000` is Extra 1) |
| `GE_KEYS` | `"5:F1 6:Down"` press keys, SDL's names with `_` for a space |
| `GE_MOUSEMOVE` | `"66-67:300,0"` pixels a second |
| `GE_SCAN`, `GE_SCANINT`, `GE_SCANPAT`, `GE_SCANWATCH` | snapshot times and the rule; results in `build/scan.txt` |
| `GE_POKE` | `"50:80ABCDEF=f1.5 52:80ABCDEF+=x10"` |
| `GE_PROBE` | `"64:816F0270:build/cands.txt"` |
| `GE_OBJ` | `"806EB698:400:79,81.5"` into `build/obj.txt` |
| `GE_FINDPTR` | `"66:80848000:808491A4"` |
| `GE_SETALL` | `"62:49=80:80000000:94000000"` |
| `GE_WATCH` | `"64:807807AC"` or `"64:*806EB698+FC"` who writes that word |
| `GE_TRACEW`, `GE_PEEK` | addresses to log on change, or every half second |
| `GE_RECORD=F`, `GE_REPLAY=F`, `GE_REPLAY_FPS=N`, `GE_REPLAY_FROM=N` | recorded input |

## 6. Files read and written

| File | By | What |
|---|---|---|
| `config/hooks.txt` | the recompiler | the functions the port may hook |
| `config/names-manual.tsv` | `tools/merge_symbols.py` | address, name, evidence for every `ge_*` function |
| `config/hosts.txt` | net | the game's server names |
| `config/openings.txt` | cutscenes | level hash, the level time its opening ends at |
| `config/cutscenes.txt` | cutscenes | level hash, timeline hash, seconds, option: cutscenes inside a level that are skipped too |
| `build/patched/gecko.txt`, `config/gecko/*.txt` | gecko | codes run each frame |
| `build/settings.env` | settings | the launcher's `LAUNCH_*` lines (`settings.env` in a package) |
| `hotkeys.txt` beside it | hotkeys | `key = setting next\|previous\|set\|switch [value]` |
| the runtime's key file (`video_keys_path()`) | settings, prompts | the `[Classic Controller]` bindings |
| `*.dmw` in the project folder or `config/` | overlay | the watch list |
| `build/crashes/` | crashdump | the dumps |
| `build/scan.txt`, `build/scan-obj.txt`, `build/obj.txt` | debug | scan results |

## 7. The launcher

`launcher/` is compiled into every build (`launcher/launcher.cmake`): a package keeps the player's
files next to the executable (`GE_PORTABLE`), a checkout in `build/` with the symbols from
`build/recomp/` and the Makefile's `FONTS` (`launch.sh` starts it so, with `GE_RUN_LOG=` to tee
the log itself). It takes over wiiboot's command line through wiikit's `RtGameLauncher` (a patch in
`patches/wiikit.patch`) before anything else runs. Started with a game folder as its first
argument (`wiiboot game --nand ...`) it gives the arguments back untouched.

| File | What |
|---|---|
| `launch.cpp` | moves to the executable's folder, reads `settings.env`'s `LAUNCH_*` lines and the options (`--help` lists them), puts them in the `GE_*` variables the port reads (the environment wins), `--extract IMAGE [--force]` on the command line, `run.log` for stdout and stderr, then `game --symbols config/symbols.tsv --watch 10` and whatever it didn't know |
| `window.cpp` | the first start's window (SDL3, Dear ImGui, the F1 menu's colours): a disc image from the system's file dialog or a drop, the extraction on a thread with its progress, then the game. `GE_LAUNCHER_PICK=IMAGE` picks one, `GE_LAUNCHER_SHOT=file.bmp` saves the window every second, `WIIKIT_HIDDEN` hides it |
| `extract.cpp` | `ExtractGame`: checks the game id (SJBE52) and the free space, writes `game.extracting/` (`sys/`, `files/`, `disc/`, `ticket.bin`, `tmd.bin`) and only then renames it to `game/`, a filled `game/` is moved aside as `game.old` |
| `disc.cpp` | wiikit's `disc.py` and `rvz.py` in C++: .iso, .wbfs, .rvz and .wia (none, bzip2, LZMA, LZMA2, zstd; not WIA's purge), the data partition decrypted with OpenSSL, the FST |

The decompressors are compiled from source (`tools/get_codecs.sh` puts zstd, bzip2 and the LZMA
SDK's decoders in `build/deps/codecs`). Online, Quicker Start, Start Fullscreen and Crash Dumps
are F1 rows that only write `settings.env` (`next_start()` in `ui/settings/files.cpp`), the launcher reads
them on the next start; `--offline` sets `GE_HOSTS=off` (Windows drops an empty variable).
