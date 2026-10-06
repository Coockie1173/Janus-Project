# GoldenEye 007 (Wii) — static recompilation

Toward a native PC port of **GoldenEye 007** (Wii, Eurocom / Activision, 2010,
NTSC-U `SJBE52`). The game's own PowerPC code is translated to C++ and built
for the PC, running on a replacement for the Wii's hardware and system
software. Nothing is emulated at the instruction level, and nothing of the
game is rewritten.

Built on **[wiikit](https://github.com/vs-sr-dev/wiikit)** (MIT), the
game-agnostic Wii recompiler and runtime, taken as a submodule at `wiikit/`.

## Bring your own disc

This repository holds tools, names and the port's own layer only: **no game
data, no executables, no assets**. Extract your own disc into `./game`
(Dolphin: right-click the game → Properties → Filesystem → Extract Entire
Disc, or `dolphin-tool extract`), or let the port do it from an image
(`./launch.sh --extract GoldenEye.rvz`, .iso and .wbfs too), so that `game/sys/main.dol` and
`game/files/` exist.

## Status

Work in progress.

## Building

Needs Python 3.8+, CMake, Ninja, a C++20 compiler (clang), OpenGL 4.5 and
SDL3 (built from source into `build/deps` if the system has no SDL3
development files). Linux is the platform this port is developed on.

```sh
git clone --recursive <this repo>     # or: git submodule update --init
make            # mix the symbols, recompile main.dol, build build/out/wiiboot
./launch.sh     # build if needed and boot the game (log in build/run.log)
make smoke      # 30 s headless boot, log in build/run.log
```

### Windows

`make win` cross-compiles a 64-bit Windows build on Linux (llvm-mingw and
SDL3 are fetched into `build/deps`) and packs it as
`build/GoldenEye007-win64.zip`: the executable, `Play.bat` with the settings
that opens the same settings window (a Python with Tk is in the package),
and `README.txt` (`port/win/`). The disc's files are not in it; the player
takes them out of their own image with the window's "Extract disc...". Online play works
there as on Linux (Winsock in place of BSD sockets).

### Linux package

`make linux` builds the same portable layout for Linux and packs it as
`build/GoldenEye007-linux64.zip`: the executable (the C++ runtime linked in,
SDL3 beside it in `lib/`), `Play.sh` that opens the settings window with the
system's Python, and `README.txt` (`port/linux/`). It needs glibc 2.38,
OpenSSL 3 and OpenGL 4.5 where it runs; online and crash dumps work there.

Both packages take the `mods/` folder along as it is when they are made
(its `.edb` files, their `.png` pictures and `online.txt`, see Mods below), so what is in it when
`make win` or `make linux` runs ships in the zip.

The boot ROM fonts and the DSP's resampling table (`font_western.bin`,
`font_japanese.bin`, `dsp_coef.bin`) are taken from Dolphin's free
replacements in `Sys/GC` when Dolphin is installed (`FONTS=dir` otherwise).
Dolphin's `Sys/totaldb.dsy` (`DSY=file`), if present, names more SDK
functions by signature.

## Playing

`./launch.sh` opens a small window first: the frame rate (60 by default
and at most; 30 is the console's), the field of view, the mouse's sensitivity, online
on or off, and the key bindings (click Set, press a key or a mouse button).
Then Play. `./launch.sh --no-gui` starts the game with what was saved, and
any option does too (`--fps 60`, `--fov 70`, `--mouse 1.5`, `--offline`,
`--fullscreen`).

Keyboard and mouse by default: W A S D move, the mouse looks, the left
button fires, the right aims, the wheel changes weapon, 1 2 3 take out the
weapon in that slot, R reloads, Ctrl crouches, Shift sprints, E acts, G
throws a grenade, V is the melee attack;
in menus the left button selects and the right one goes back. A gamepad is
a Classic Controller.

The game's online servers are gone. `config/hosts.txt` points their names at
a community server, as lines of `/etc/hosts` would. Needs OpenSSL's
development files. Gecko codes go in `config/gecko.txt` (RAM writes only).

F1 opens the settings menu, drawn like the game's own: the field of view,
the frame rate (30 to 275),
the mouse (sensitivity, invert, how it turns the view), the gamepad (face
buttons, dead zone) and the key bindings. Changes count at once and are
kept in `build/settings.env` and `build/keys.txt`, the launcher's files.

F3 opens a debug overlay: the memory watches of a Dolphin Memory Engine
list (a `.dmw` file in the project's folder, or `GE_DMW=file`), read live
from the running game, with a filter. It is drawn with Dear ImGui, fetched
into `build/deps` by `make`.

## Mods

Every `.edb` file in `mods/` (or `GE_MODS=folder`) is loaded in place of the
disc's file with the same hash, or comes on top of the disc's files when no
file has its hash. The levels among them are listed in the game's menu
under Extras > Mods. With an `online.txt` in the folder (a mod's file name or
a file's hash a line) the Multiplayer menu has two ways online: Connect to
Nintendo WFC with the game's own maps for Conflict, Connect to WFC Modded
with the listed ones. A `.png` with a level's name next to it (`mt_yard.png`
for `mt_yard.edb`) is its picture in the menus: beside the list in Extras >
Mods and in Conflict's map list, and for the two maps a network game offers.
Any size: its middle is cut to the panel's shape (0.85 times as wide as
high) and shown at 256 x 256. A level is the hash of its file: `GE_LEVEL=01810201
./launch.sh` starts that level whichever mission is picked in the menu.
New levels are made from glTF scenes with
[eurochef-GE](https://github.com/Coockie1173/eurochef-GE), a fork of
[eurochef](https://github.com/eurotools/eurochef).

## Layout

    config/symbols/          three symbol sources for SJBE52, none fully accurate
    config/names-manual.tsv  names found by hand, each with its evidence
    config/hooks.txt         functions the port's layer may replace
    tools/merge_symbols.py   mixes the sources into build/names.tsv
    tools/sigmatch.py        names SDK functions by Dolphin's signatures
    port/                    the game's own layer on the runtime (port/ge.h lists its files)
    wiikit/                  recompiler and runtime (submodule)
    docs/                    status and findings
    mods/                    (not in git) loose .edb files, see Mods
    game/, build/            (not in git) your disc, everything derived from it

## How the names are mixed

The executable is stripped. For every function address, one name is chosen:

    manual  >  symbols.txt  >  symbols-2.txt  >  main.dol.map  >  signatures

where a source counts only if its name is a real one (not `fn_…`/`FUN_…`).
Every function address any of the three symbol files knows is also a seed
for function discovery. `build/names.report.txt` lists what each source
gave and where real names disagree.

## Credits

- [wiikit](https://github.com/vs-sr-dev/wiikit) and its ports
  ([pc-conduit2](https://github.com/vs-sr-dev/pc-conduit2),
  [pc-dragonquestswords](https://github.com/vs-sr-dev/pc-dragonquestswords))
  by Samuele Voltan, MIT: the recompiler, the runtime, `tools/sigmatch.py`,
  and the method.
- [Dolphin](https://dolphin-emu.org) for the signature database and as the
  reference for the console's behaviour.
- [SDL](https://libsdl.org).
- Special thanks to FitterSpace and AlpineEscape for testing.
