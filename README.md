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

Cloning on its own gives you a clean wiikit, without my changes to it. You don't have to do anything about that: `make` (and so `./launch.sh`, `make win` and `make linux`) applies `patches/wiikit.patch` to the submodule before it builds, as long as the submodule is still untouched. Should you ever want to do it by hand:
```sh
git -C wiikit apply ../patches/wiikit.patch
```
And if you change wiikit yourself, `make wiikit-save` writes your changes back into the patch.

The boot ROM fonts and the DSP's resampling table (`font_western.bin`, `font_japanese.bin`, `dsp_coef.bin`) are taken from Dolphin's free replacements in `Sys/GC` if you have Dolphin installed (pass `FONTS=dir` otherwise). If Dolphin's `Sys/totaldb.dsy` is around (`DSY=file`) it'll name some more SDK functions by signature.

## Windows
`make win` cross-compiles a 64-bit Windows build from Linux (llvm-mingw and SDL3 get fetched into `build/deps`) and packs it as `build/GoldenEye007-win64.zip`. In there you'll find the executable, `Play.bat` which opens the same settings window (a Python with Tk is in the package), and a `README.txt` (see `port/win/`). The disc's files are not in it! The player takes them out of their own image using the window's "Extract disc..." button. Online works the same as on Linux (Winsock instead of BSD sockets).

## Linux package
`make linux` builds the same portable layout for Linux and packs it as `build/GoldenEye007-linux64.zip`: the executable (C++ runtime linked in, SDL3 next to it in `lib/`), `Play.sh` which opens the settings window with the system's Python, and a `README.txt` (see `port/linux/`). It needs glibc 2.38, OpenSSL 3 and OpenGL 4.5 wherever it runs. Online and crash dumps work there too.

Both packages come with an empty `mods/` folder (the gamemode folders are in it). The mods are a download of their own: `make mods-zip` packs `mods/` exactly as it is (the `.edb` files, their `.png` pictures and `.txt` files, gamemode folders included) as `build/GoldenEye007-mods.zip`, which gets unzipped into the game's folder, next to `Play`. No level in `mods/`, no zip.

## Releases
Every push to `main` builds both packages and the mods' zip and puts them up as the `latest` release, and pushing a tag like `v1.0` gives that one its own release. Since the build needs the disc, GitHub's own machines can't do it: `.github/workflows/release.yml` runs on a self-hosted runner, which is just my own PC. If it's off, the release simply waits.

Want the same on your fork? Add a self-hosted runner (Settings > Actions > Runners), and give it an `.env` file with `GE_GAME=/path/to/your/extracted/disc` (and `GE_MODS=/path/to/mods` if you want a `GoldenEye007-mods.zip` next to them).

# Playing
`./launch.sh` opens a small window first. In there you can set the frame rate (60 by default, 30 is what the console does), the field of view, the mouse sensitivity, online on or off, and the key bindings (click Set, press a key or a mouse button). Then hit Play.

Don't want the window? `./launch.sh --no-gui` starts the game with whatever was saved, and so does passing any option (`--fps 60`, `--vsync`, `--fov 70`, `--mouse 1.5`, `--offline`, `--fullscreen`).

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
- **F1** opens the settings menu, drawn like the game's own. Field of view, frame rate (30 to 275), VSync, the ping in modded online games, the mouse (sensitivity, invert, how it turns the view), the gamepad (face buttons, dead zone) and the key bindings. Changes count right away and are kept in `build/settings.env` and `build/keys.txt`, which are the launcher's files too.
- **F3** opens a debug overlay. It shows the memory watches of a Dolphin Memory Engine list (a `.dmw` file in the project's folder, or `GE_DMW=file`), read live from the running game, with a filter. It's drawn with Dear ImGui, which `make` fetches into `build/deps`.
- **F12** takes a mod level's picture, see Mods.

## Online
The game's online servers are long gone. `config/hosts.txt` points their names at a community server instead, the same way lines in `/etc/hosts` would. You'll need OpenSSL's development files for this.

Gecko codes go in `config/gecko.txt` (RAM writes only).

# Mods
Every `.edb` file in `mods/` (or `GE_MODS=folder`) gets loaded in place of the disc's file with the same hash. If no file on the disc has that hash, it just comes on top. Any levels among them show up in the game's menu under Extras > Mods.

Online maps go by folder. `mods/` has one for every online gamemode:

```
mods/
  mt_grid.edb              only in Extras > Mods
  conflict/                Conflict, and Classic Conflict (it has Conflict's maps in the game too)
    mt_yard.edb
    mt_yard.png
  team_conflict/
  golden_gun/
  black_box/
  goldeneye/
  heroes/
  license_to_kill/
  team_license_to_kill/
```

Every level in a gamemode's folder is in that gamemode's map pool. Want a map in more than one? Just copy it into each, it's loaded once.

A map with different things per gamemode (the golden gun only in Golden Gun, team spawns only in team games) is made the way the game's own are: its geometry once, as `mg_yard.edb` in `mods/` itself, and a tiny `mt_yard.edb` in each gamemode's folder with that gamemode's spawn points and objects, which loads the geometry. eurochef's `ge new-map` writes exactly that from one glTF scene, straight into `mods/`. Such a map is in Extras > Mods once. Anything in `mods/` itself isn't online at all, it only shows up under Extras > Mods (the ones in the folders are listed there as well, so you can walk around in them alone).

With a level in any of those folders the Multiplayer menu gets two ways to go online:
- Connect to Nintendo WFC, with the game's own gamemodes and maps
- Connect to WFC Modded, with yours: Choose Game Mode only lists the gamemodes whose folder has a level in it, an empty (or missing) folder means the gamemode is gone from the list

Modded games are kept apart from everyone else's: the game's version number for matchmaking is made from every gamemode's maps and their files (and every other mod that isn't a level, like a map's geometry), so you only find players who have the exact same folders (the log prints it at start, `a network game with them is version ...`). Got a friend who can't find your lobby? Compare that number.

A `.png` with the level's name next to it, in the same folder (so `mt_yard.png` for `mt_yard.edb`), is its picture in the menus (a level in a gamemode's folder without one takes `mods/mt_yard.png`, so one picture does for all of a map's gamemodes): beside the list in Extras > Mods, in Conflict's map list, and for the two maps a network game offers. Any size works, the middle gets cut to the panel's shape (0.85 times as wide as high) and shown at 256 x 256. Can't be bothered making one? Press F12 while in a mod's level and it'll make that `.png` from whatever is on screen: black and white, without the HUD, the crosshair and the gun.

Online the game dresses the two teams by the map, and a mod level gets Archives' characters. A `mt_yard.txt` next to the level (or in `mods/` itself, for all of the map's gamemodes) picks others:
```
team0 = Jones
team1 = Oddjob
hero0 = Bond
hero1 = Zukovsky
name0 = Agents
name1 = Villains
```
A team is one of the game's sets of four, named by any character in it as the game shows the name (`Jones` is Jones, Davis, Smyth and Adams; `Oddjob` is him, Jaws, Blofeld and Scaramanga; `Pvt. Gurov`, `Red-one`, `Two-Two`, `Five-One`, `Agent Zero`, `Sgt. Morozov`, `Sgt. Glebov`, `Vincent`, `Dr. No` are the others). `hero0` and `hero1` are each team's hero in Heroes, one character. `name0` and `name1` are what the two teams are called on the HUD, in the lobby and in the scores, any text up to 31 characters. Without them the first team is MI6, as it is on every map of the game, and the second is called after its characters the way the game's maps do it: Janus for Two-Two's and Five-One's, Zukovsky for Vincent's, Russians for the rest (its shouts follow that too). Leave a line out and that one stays Archives'. The log says what it made of each line when the level loads. Split screen isn't touched, everyone picks their own there.

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

# License
Copyright (c) 2026 Mars

This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/. The full text is in [LICENSE](LICENSE).

In short: do what you want with it, but if you change my files and hand them out, those changes stay open.

A couple of things in here aren't mine to license though:
- The game. None of it is in this repo, and this license says nothing about it. GoldenEye 007 belongs to its owners, you bring your own copy.
- `patches/wiikit.patch` and `tools/sigmatch.py` are MIT, same as the [wiikit](https://github.com/vs-sr-dev/wiikit) code they come from (Copyright (c) 2026 Samuele Voltan). That way the patch can go back upstream.
- `config/symbols/` are symbol lists other people made, kept as they were given to me.

# Credits
- [wiikit](https://github.com/vs-sr-dev/wiikit) and its ports ([pc-conduit2](https://github.com/vs-sr-dev/pc-conduit2), [pc-dragonquestswords](https://github.com/vs-sr-dev/pc-dragonquestswords)) by Samuele Voltan, MIT: the recompiler, the runtime, `tools/sigmatch.py`, and the method.
- [Dolphin](https://dolphin-emu.org) for the signature database and as the reference for how the console behaves.
- [SDL](https://libsdl.org).
- Special thanks to FitterSpace and AlpineEscape for testing.
