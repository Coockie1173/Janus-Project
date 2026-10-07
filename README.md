# GEWII-PC
This project aims to get **GoldenEye 007** (Wii, Eurocom / Activision, 2010, NTSC-U `SJBE52`) running natively on PC. It's a static recompilation: the game's own PowerPC code gets translated to C++ and built for PC, where it runs on top of a replacement for the Wii's hardware and system software. Nothing is emulated at the instruction level, and nothing of the game itself is rewritten.

It's built on [wiikit](https://github.com/vs-sr-dev/wiikit) (MIT), a game-agnostic Wii recompiler and runtime, which lives in here as a submodule.

It consists out of five distinct parts:
- wiikit
- Port
- Config
- Tools
- Mods

Each part serves it's own distinct purpose.

# Bring your own disc
Before anything else: this repo holds tools, names and the port's own layer, and that's it. **No game data, no executables, no assets.** You'll need your own copy of the game.

Extract your disc into `./game` (in Dolphin: right-click the game → Properties → Filesystem → Extract Entire Disc, or use `dolphin-tool extract`). You can also just let the port do it for you from an image:
```sh
./launch.sh --extract GoldenEye.rvz    # .iso and .wbfs work too
```
Either way you should end up with `game/sys/main.dol` and `game/files/`.

# Quick overview
## wiikit
The recompiler and the hardware runtime (CPU model, OS threads, GX → OpenGL 4.5, IOS, audio, Wii remotes, the whole lot). This isn't mine, it's [Samuele Voltan's](https://github.com/vs-sr-dev/wiikit), and it's taken along as a submodule. The changes I did have to make to it live in `patches/wiikit.patch`, which `make` applies for you.

## Port
Everything in `port/` is the game's own layer on top of the runtime: the hooks, the input mapping, the settings menu, the mod loader, online, crash dumps, etc. `port/ge.h` lists which file does what, and [docs/CODE.md](docs/CODE.md) goes over all of it in a lot more detail (every hooked function, every address and struct offset it relies on).

## Config
The game's executable is stripped, so we have to figure out what everything is called ourselves.
- `config/symbols/` has three symbol sources for SJBE52, none of which are fully accurate (fun!)
- `config/names-manual.tsv` has the names found by hand, each with its evidence
- `config/hooks.txt` lists the functions the port is allowed to replace
- `config/hosts.txt` and `config/gecko.txt` are for online and Gecko codes, more on those later

## Tools
A bunch of python and shell scripts. The important ones:
- `tools/merge_symbols.py` mixes the symbol sources into `build/names.tsv`
- `tools/sigmatch.py` names SDK functions using Dolphin's signatures
- `tools/build_sdl3.sh` builds SDL3 from source if your system doesn't have it

## Mods
Loose `.edb` files in `mods/` (not in git) that replace the disc's files or come on top of them. This gets its own section further down.

Then there's `game/` and `build/`, neither of which are in git. That's your disc and everything derived from it.

# Building
You'll need Python 3.8+, CMake, Ninja, a C++20 compiler (clang), OpenGL 4.5 and SDL3 (which gets built from source into `build/deps` if your system has no SDL3 development files). Linux is what I develop this on.

```sh
git clone --recursive git@github.com:Coockie1173/GEWII-PC.git     # or: git submodule update --init
make            # mix the symbols, recompile main.dol, build build/out/wiiboot
./launch.sh     # build if needed and boot the game (log in build/run.log)
make smoke      # 30 s headless boot, log in build/run.log
```

The boot ROM fonts and the DSP's resampling table (`font_western.bin`, `font_japanese.bin`, `dsp_coef.bin`) are taken from Dolphin's free replacements in `Sys/GC` if you have Dolphin installed (pass `FONTS=dir` otherwise). If Dolphin's `Sys/totaldb.dsy` is around (`DSY=file`) it'll name some more SDK functions by signature.

## Windows
`make win` cross-compiles a 64-bit Windows build from Linux (llvm-mingw and SDL3 get fetched into `build/deps`) and packs it as `build/GoldenEye007-win64.zip`. In there you'll find the executable, `Play.bat` which opens the same settings window (a Python with Tk is in the package), and a `README.txt` (see `port/win/`). The disc's files are not in it! The player takes them out of their own image using the window's "Extract disc..." button. Online works the same as on Linux (Winsock instead of BSD sockets).

## Linux package
`make linux` builds the same portable layout for Linux and packs it as `build/GoldenEye007-linux64.zip`: the executable (C++ runtime linked in, SDL3 next to it in `lib/`), `Play.sh` which opens the settings window with the system's Python, and a `README.txt` (see `port/linux/`). It needs glibc 2.38, OpenSSL 3 and OpenGL 4.5 wherever it runs. Online and crash dumps work there too.

Both packages take the `mods/` folder along exactly as it is when they're made (the `.edb` files, their `.png` pictures and `online.txt`), so whatever is in there when you run `make win` or `make linux` ships in the zip.

# Playing
`./launch.sh` opens a small window first. In there you can set the frame rate (60 by default, 30 is what the console does), the field of view, the mouse sensitivity, online on or off, and the key bindings (click Set, press a key or a mouse button). Then hit Play.

Don't want the window? `./launch.sh --no-gui` starts the game with whatever was saved, and so does passing any option (`--fps 60`, `--fov 70`, `--mouse 1.5`, `--offline`, `--fullscreen`).

It's keyboard and mouse by default:

| Input | Action |
|-------|--------|
| W A S D | Move |
| Mouse | Look |
| Left button | Fire (select in menus) |
| Right button | Aim (back in menus) |
| Wheel | Change weapon |
| 1 2 3 | Take out the weapon in that slot |
| R | Reload |
| Ctrl | Crouch |
| Shift | Sprint |
| E | Act |
| G | Throw a grenade |
| V | Melee attack |

A gamepad just acts as a Classic Controller.

Then there's a couple of extra keys:
- **F1** opens the settings menu, drawn like the game's own. Field of view, frame rate (30 to 275), the mouse (sensitivity, invert, how it turns the view), the gamepad (face buttons, dead zone) and the key bindings. Changes count right away and are kept in `build/settings.env` and `build/keys.txt`, which are the launcher's files too.
- **F3** opens a debug overlay. It shows the memory watches of a Dolphin Memory Engine list (a `.dmw` file in the project's folder, or `GE_DMW=file`), read live from the running game, with a filter. It's drawn with Dear ImGui, which `make` fetches into `build/deps`.
- **F12** takes a mod level's picture, see Mods.

## Online
The game's online servers are long gone. `config/hosts.txt` points their names at a community server instead, the same way lines in `/etc/hosts` would. You'll need OpenSSL's development files for this.

Gecko codes go in `config/gecko.txt` (RAM writes only).

# Mods
Every `.edb` file in `mods/` (or `GE_MODS=folder`) gets loaded in place of the disc's file with the same hash. If no file on the disc has that hash, it just comes on top. Any levels among them show up in the game's menu under Extras > Mods.

If there's an `online.txt` in the folder (a mod's file name or a file's hash per line), the Multiplayer menu gets two ways to go online:
- Connect to Nintendo WFC, with the game's own maps for Conflict
- Connect to WFC Modded, with the listed ones

A `.png` with the level's name next to it (so `mt_yard.png` for `mt_yard.edb`) is its picture in the menus: beside the list in Extras > Mods, in Conflict's map list, and for the two maps a network game offers. Any size works, the middle gets cut to the panel's shape (0.85 times as wide as high) and shown at 256 x 256. Can't be bothered making one? Press F12 while in a mod's level and it'll make that `.png` from whatever is on screen: black and white, without the HUD, the crosshair and the gun.

A level is just the hash of its file, so
```sh
GE_LEVEL=01810201 ./launch.sh
```
starts that level no matter which mission you pick in the menu.

New levels are made from glTF scenes with [eurochef-GE](https://github.com/Coockie1173/eurochef-GE), my fork of [eurochef](https://github.com/eurotools/eurochef).

# How the names are mixed
As mentioned earlier, the executable is stripped. So for every function address we have to pick one name, and we pick it in this order:

    manual  >  symbols.txt  >  symbols-2.txt  >  main.dol.map  >  signatures

A source only counts if its name is a real one (so not `fn_…` or `FUN_…`). Every function address any of the three symbol files knows about is also used as a seed for function discovery. Want to know who gave what, and where the real names disagree? `build/names.report.txt` lists exactly that.

# Status
Work in progress! It boots, the campaign and online are playable, but it's not what I'd call stable just yet. [TODO.md](TODO.md) has the list of what's still broken.

# Credits
- [wiikit](https://github.com/vs-sr-dev/wiikit) and its ports ([pc-conduit2](https://github.com/vs-sr-dev/pc-conduit2), [pc-dragonquestswords](https://github.com/vs-sr-dev/pc-dragonquestswords)) by Samuele Voltan, MIT: the recompiler, the runtime, `tools/sigmatch.py`, and the method.
- [Dolphin](https://dolphin-emu.org) for the signature database and as the reference for how the console behaves.
- [SDL](https://libsdl.org).
- Special thanks to FitterSpace and AlpineEscape for testing.
