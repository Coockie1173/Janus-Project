GoldenEye 007 (Wii, NTSC-U, SJBE52) - Linux build
==================================================

The game's own code, translated to run on a PC. The game's data is NOT in this
folder: you need your own copy of the disc.


What you need
-------------
- A 64-bit Linux from about 2024 or later (glibc 2.38: Ubuntu 24.04, Debian 13,
  Fedora 39, Arch, Void...), with OpenSSL 3 (libssl.so.3; it is there already
  on all of those).
- A processor from about 2015 or later (AVX2: Intel Haswell, AMD Ryzen, or newer).
- A graphics card with OpenGL 4.5 (Mesa or NVIDIA's driver).
- Python 3 with Tk for the settings window and the disc extraction
  (Debian/Ubuntu: python3-tk; Fedora: python3-tkinter; Arch: tk; Void: python3-tkinter).
  Without Tk the game still starts, with the saved settings.
- Your own GoldenEye 007 disc for the Wii, US version (game ID SJBE52), as a
  .iso / .wbfs / .rvz file.


Setting it up (once)
--------------------
1. Unzip this whole folder somewhere you can write to (unzip keeps Play.sh and
   GoldenEye executable; if your file manager did not: chmod +x Play.sh GoldenEye).

2. Run ./Play.sh. A small window opens.

3. Click "Extract disc..." and pick your disc image. The game's files are
   taken out of it into the "game" folder (about 4 GB; under a minute from
   an SSD). This is needed once. From a terminal: ./Play.sh --extract GoldenEye.rvz

4. Click Play.

The first start takes a little longer and may stutter for a moment while the
shaders are made; they are kept in the "shadercache" folder for next time.


Playing
-------
Keyboard and mouse:
    W A S D          move                 Mouse            look
    Left button      fire                 Right button     aim down the sight
    Mouse wheel      change weapon        R                reload
    Left Ctrl        crouch               Left Shift / E   sprint / action
    G                grenade              V                melee attack
    T                cycle grenades       B                firing mode
    F                smartphone           Tab              night vision / scoreboard
    P                pause                Esc              free the mouse
    F11, Alt+Enter   fullscreen           F1               settings menu
In menus the left mouse button selects and the right one goes back
(Return and Backspace do too).

A gamepad works as a Classic Controller: plug it in before starting.

F1 in the game opens its settings menu: the field of view, the frame rate, the mouse, the
gamepad and the key bindings, changed while you play (a mission is paused
first). It writes the same settings.env and keys.txt as the window.

keys.txt holds every binding, with the names to use explained at its top.
Delete it to get the defaults back (a copy is in config/keys.txt).


Settings
--------
The window Play.sh opens has them: frame rate, field of view, mouse
sensitivity and mode, online, fullscreen, a quicker start, crash dumps; the
Controls tab rebinds every action (Set, then press a key or a mouse button);
the Enhancements tab has Skip Intro Cutscenes and the Level Timer; the Cheats
tab Always Enable Mouse, the Unused Extras Menu and any code in
config/gecko/cheats.txt.

From a command line, any option skips the window:
    ./Play.sh --no-gui                  start with what was saved
    ./Play.sh --fps 120 --fov 70 --mouse 1.5 --fullscreen
    ./Play.sh --offline                 the game does not look for its servers
    ./Play.sh --extract ~/GoldenEye.rvz
    ./Play.sh --window 1920x1080        (options of the game itself pass through)

The window goes through X11 (XWayland) when there is one; for Wayland's own:
SDL_VIDEODRIVER=wayland ./Play.sh


Where things are
----------------
    nand/           your saves and profiles (made on the first start). Back this up.
    keys.txt        key bindings
    settings.env    what the settings window saved
    run.log         the log of the last run
    crashes/        a folder for each crash or stall, with what the game was doing
    shadercache/    compiled shaders; safe to delete
    config/         Gecko codes, cheats, the server list, function names for the log
    lib/            SDL3, used by the game
    launcher/       the settings window and the disc extraction


If something goes wrong
-----------------------
- "The game's files are missing": Extract disc... has not been done yet. If you
  extracted with Dolphin instead, game/sys/main.dol and game/files/ must exist
  (move what is inside a DATA folder up into "game").
- Extract disc... says the image is another game: only the US version (SJBE52)
  works with this build.
- "version `GLIBC_2.38' not found": the distribution is too old for this build.
- "libssl.so.3: cannot open shared object file": install OpenSSL 3.
- A window that closes at once, or a black one: the graphics driver has no
  OpenGL 4.5 (glxinfo | grep "OpenGL core" tells).
- "Illegal instruction": the processor has no AVX2. This build cannot run there.
- No settings window ("launcher: no window"): Python's Tk is not installed.
- Anything else: run.log ends with what happened, and crashes/ has a folder for
  the crash. Send both along with a report.


Credits
-------
Built on wiikit (MIT) and SDL3 (zlib license). Their licenses are in licenses/.
The fonts in "fonts" are Dolphin's free replacements for the console's
(licenses/ has the note).
GoldenEye 007 is the property of its owners; this package contains none of its
data files.
