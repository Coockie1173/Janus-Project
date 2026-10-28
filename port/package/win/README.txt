GoldenEye 007 (Wii, NTSC-U, SJBE52) - Windows build
====================================================

The game's own code, translated to run on a PC. The game's data is NOT in this
folder: you need your own copy of the disc.


What you need
-------------
- Windows 10 or 11, 64-bit.
- A processor from about 2015 or later (AVX2: Intel Haswell, AMD Ryzen, or newer).
- A graphics card with OpenGL 4.5 and up-to-date drivers (NVIDIA, AMD, or Intel
  from the last ten years).
- Your own GoldenEye 007 disc for the Wii, US version (game ID SJBE52), as a
  .iso / .wbfs / .rvz file.


Setting it up (once)
--------------------
1. Unzip this whole folder somewhere you can write to (not Program Files),
   for instance C:\Games\Janus-Project.

2. Double-click Janus-Project.exe. The first time, a window asks for your
   disc image.

3. Click "Choose Disc Image..." (or drop the file on the window) and pick it.
   The game's files are taken out of it into the "game" folder (about 4 GB;
   under a minute from an .rvz or .iso, a few minutes from a .wia). The game
   starts when it's done. This is needed once.

The first start takes a little longer and may stutter for a moment while the
shaders are made; they are kept in the "shadercache" folder for next time.


Playing
-------
Keyboard and mouse:
    W A S D          move                 Mouse            look
    Left button      fire                 Right button     aim down the sight
    Mouse wheel      change weapon        R                reload
    Left Ctrl        crouch               Left Shift       sprint
    G                grenade              V                melee attack
    T                cycle grenades       B                firing mode
    F                smartphone           Tab              night vision / scoreboard
    1, 2, 3          the weapon in that slot
    Esc, P           pause                E                action
    F11, Alt+Enter   fullscreen           F1               settings menu
In menus the left mouse button selects and the right one goes back
(Return and Backspace do too).

A gamepad works as a Classic Controller: plug it in before starting.

F1 in the game opens its settings menu: the field of view, the frame rate, the mouse, the
gamepad and the key bindings, changed while you play (a mission is paused
first). It keeps them in settings.env and keys.txt.

Its Hotkeys page puts settings on keys of your own: a key for 30 frames a second and back,
one that turns the level timer on and off, one for a wider view... They work while you play,
with the menu closed, and are kept in hotkeys.txt.

keys.txt holds every binding, with the names to use explained at its top.
Delete it to get the defaults back (a copy is in config\keys.txt).


Settings
--------
All of them are in F1. Start Fullscreen (Screen Settings), Online and Quicker
Start (Enhancements) apply from the next start on.

From a command prompt in this folder, options go over what's saved, for that start:
    Janus-Project.exe --fps 120 --fov 70 --mouse 1.5 --fullscreen
    Janus-Project.exe --offline                 the game doesn't look for its servers
    Janus-Project.exe --extract D:\GoldenEye.rvz (--force replaces a game folder that's there)
    Janus-Project.exe --window 1920x1080        (options of the game itself pass through)
    Janus-Project.exe --help                    every option


Where things are
----------------
    nand\           your saves and profiles (made on the first start). Back this up.
    keys.txt        key bindings
    settings.env    your settings (F1 writes them)
    modpacks/       levels that aren't the game's own (.edb files), a folder for each
                    modpack (modpacks/default, ...). Extras > Mods in the game's menu
                    lists the packs and their levels; the pack you highlight is the
                    one that's loaded. A pack has a folder for each online game mode
                    (conflict, team_conflict, golden_gun, black_box, goldeneye, heroes,
                    license_to_kill, team_license_to_kill): the levels in one are that
                    mode's maps after Multiplayer > Connect to WFC Modded, where you
                    pick the pack first, and a mode with an empty folder isn't offered
                    there
                    It comes with an empty pack "default": Janus-Project-mods.zip, a
                    download of its own next to this one, has levels for it. Unzip it
                    into this folder.
                    Without any level in those folders Multiplayer is the game's own
                    menu, with Nintendo Wi-Fi Connection alone.
    run.log         the log of the last run
    shadercache\    compiled shaders; safe to delete
    config\         Gecko codes, cheats, the server list, function names for the log


If something goes wrong
-----------------------
- The window asks for the disc image again: the "game" folder has no
  game\sys\main.dol and game\files\. If you extracted with Dolphin instead,
  move what is inside its DATA folder up into "game".
- The window says the image is another game: only the US version (SJBE52)
  works with this build.
- A window that closes at once, or a black one: update the graphics driver. On
  a laptop, make Janus-Project.exe use the dedicated graphics card (Windows
  Settings -> Display -> Graphics).
- "Illegal instruction", or it never opens a window on an old PC: the processor
  has no AVX2. This build cannot run there.
- Windows SmartScreen says the program is unknown: More info -> Run anyway.
- Online does not connect: allow Janus-Project.exe through the Windows firewall
  when it asks, and leave F1 > Enhancements > Online on.
- Anything else: run.log ends with what happened. Send it along with a report,
  and the version: its first lines have it, and so does the foot of the F1 menu.


Not in this build
-----------------
- The crash-dump folders of the Linux version.


Credits
-------
Janus-Project itself is under the Mozilla Public License 2.0. It is built on
wiikit (MIT), SDL3 (zlib license), Dear ImGui (MIT, with the Roboto typeface,
Apache 2.0) and OpenSSL; the disc images are read with zstd (BSD), bzip2 and the
LZMA SDK (public domain). Their licenses are in licenses\. The fonts in
"fonts" are Dolphin's free replacements for the console's (licenses\ has the note).
GoldenEye 007 is the property of its owners; this package contains none of its
data files. This is an unofficial fan project, not made or endorsed by Activision,
Eurocom, Nintendo, MGM or Danjaq.
