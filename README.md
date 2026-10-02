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
Disc, or `dolphin-tool extract`), so that `game/sys/main.dol` and
`game/files/` exist.

## Status

Work in progress — see [docs/STATUS.md](docs/STATUS.md).

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
button fires, the right aims, the wheel changes weapon, R reloads, Ctrl
crouches, Shift sprints, E acts, G throws a grenade, V is the melee attack;
in menus the left button selects and the right one goes back. A gamepad is
a Classic Controller.

The game's online servers are gone. `config/hosts.txt` points their names at
a community server, as lines of `/etc/hosts` would. Needs OpenSSL's
development files. Gecko codes go in `config/gecko.txt` (RAM writes only).

## Layout

    config/symbols/          three symbol sources for SJBE52, none fully accurate
    config/names-manual.tsv  names found by hand, each with its evidence
    config/hooks.txt         functions the port's layer may replace
    tools/merge_symbols.py   mixes the sources into build/names.tsv
    tools/sigmatch.py        names SDK functions by Dolphin's signatures
    port/                    the game's own layer on the runtime
    wiikit/                  recompiler and runtime (submodule)
    docs/                    status and findings
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
