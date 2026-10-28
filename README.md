# Janus-Project
This project aims to get **GoldenEye 007** (Wii, Eurocom / Activision, 2010, NTSC-U `SJBE52`) running natively on PC via static recompilation.

It's built on [wiikit](https://github.com/vs-sr-dev/wiikit) (MIT).

WiiKit was the first option I came across, so I went with it, and it worked well enough* for this recomp.

<sub>*after adding a decent amount of patches in ./patches<sub>

Nevertheless it works. This port comes with a lot of quality of life fixes, native mouse & keyboard support, a bunch of cheats, even speedrun timers and some bonus for fun thingies for the fans!

# No copyrighted data
This port does not hold any copyrighted material. This repo only contains tools, names and our own port layer. You'll need your own copy of the game to run this.
it's recommended you extract your disc via the launcher (just start the game, it asks for your disc the first time).

If you're stubborn however, you can also extract your files manually into `./game` (in Dolphin: right-click the game → Properties → Filesystem → Extract Entire Disc, or use `dolphin-tool extract`).

This is an unofficial fan project. Activision, Eurocom, Nintendo, MGM and Danjaq have nothing to do with it, so please don't bug them about it.

# Port
All the files in `port/` are our layer on top of the runtime. Things like hooks, input mapping, settings, the mod loader, our online redirect to the unofficial servers (you can play online on this port!), crash dumps to name a few. It's split into a folder per concern (`engine/`, `input/`, `mods/`, `net/`, `ui/`...), `port/ge.h` lists which folder does what and each folder's header lists its files, and [docs/CODE.md](docs/CODE.md) goes over all of it in a lot more detail.

# Launcher
`launcher/` is what runs before the game, in the packages and with `./launch.sh` alike. It reads your settings, and on a first start it asks for your disc and takes the files out of it (`.rvz`, `.iso`, `.wbfs` and `.wia`, no Python needed anymore!). Starting `wiiboot game ...` the old way skips it entirely, so `make run`, `make smoke` and the test scripts work like before.

# Config
Bunch of config files used by the recomp.
- `config/symbols/` has three symbol sources for SJBE52, none of which are fully accurate (fun!)
- `config/names-manual.tsv` has the names found by hand, each with its evidence
- `config/hooks.txt` lists the functions the port is allowed to replace
- `config/hosts.txt` and `config/gecko.txt` are for online and Gecko codes that I was too lazy to properly patch.

# Tools
A bunch of scripts to help me test rapidly. Feel free to use and or ignore them!

# Mods
You read that right! Custom levels! In my Eurocom game! We truly are blessed.

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


# Building
You'll need Python 3.8+, CMake, Ninja, a C++20 compiler (clang), OpenGL 4.5 and SDL3 (which gets built from source into `build/deps` if your system has no SDL3 development files). Linux is what I develop this on.

```sh
git clone --recursive git@github.com:Coockie1173/Janus-Project.git     # or: git submodule update --init
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
`make win` cross-compiles a 64-bit Windows build from Linux (llvm-mingw and SDL3 get fetched into `build/deps`) and packs it as `build/Janus-Project-win64.zip`. In there you'll find the executable and a `README.txt` (see `port/package/win/`). The disc's files are not in it! The player takes them out of their own image on the first start. Online works the same as on Linux (Winsock instead of BSD sockets).

## Linux package
`make linux` builds the same portable layout for Linux and packs it as `build/Janus-Project-linux64.zip`: the executable (C++ runtime linked in, SDL3 next to it in `lib/`), and a `README.txt` (see `port/package/linux/`). It needs glibc 2.38, OpenSSL 3 and OpenGL 4.5 wherever it runs. Online and crash dumps work there too.

For the launcher both packages also fetch zstd, bzip2 and the LZMA SDK into `build/deps/codecs` (that last one comes as a `.7z`, so you'll need `7z` around).

Both packages come with an empty `mods/` folder (the gamemode folders are in it). The mods are a download of their own: `make mods-zip` packs `mods/` exactly as it is (the `.edb` files, their `.png` pictures and `.txt` files, gamemode folders included) as `build/Janus-Project-mods.zip`, which gets unzipped into the game's folder, next to the executable. No level in `mods/`, no zip.

# Playing
Grab the zip for your OS from [Releases](https://github.com/Coockie1173/Janus-Project/releases), unzip it somewhere you can write to and run `Janus-Project.exe` (or `./Janus-Project` on Linux). The first time it asks for your disc image (`.rvz`, `.iso`, `.wbfs` or `.wia`, only the US version `SJBE52` works), takes the files out and you're good to go.

The small (and redundant) settings window is gone, everything lives in the much superior F1 menu in game now.

You'll need a 64-bit Windows or Linux, a CPU with AVX2 (roughly 2015 or newer) and a GPU with OpenGL 4.5. The `README.txt` in the zip has the controls and the rest.

## Something broke?
Open an issue with your `run.log`, and on Linux the `report.txt` from the folder in `crashes/` (just that file, the rest is a memory dump of the game). Please mention the version too, it's at the bottom of the F1 menu and near the top of `run.log`. A crash on a build that's a week old might already be fixed!

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
- [Dear ImGui](https://github.com/ocornut/imgui) for the F1 menu (with the Roboto font that comes with it).
- Special thanks to FitterSpace and AlpineEscape for testing.
