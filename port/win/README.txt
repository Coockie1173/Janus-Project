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
   for instance C:\Games\GoldenEye007.

2. Double-click Play.bat. A small window opens.

3. Click "Extract disc..." and pick your disc image. The game's files are
   taken out of it into the "game" folder (about 4 GB; under a minute from
   an SSD). This is needed once.

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
Delete it to get the defaults back (a copy is in config\keys.txt).


Settings
--------
The window Play.bat opens has them: frame rate, field of view, mouse
sensitivity and mode, fullscreen, a quicker start; the Controls tab rebinds
every action (Set, then press a key or a mouse button); the Enhancements tab has
Skip Intro Cutscenes and the Level Timer; the Cheats tab Always Enable Mouse, the
Unused Extras Menu and any code in config\gecko\cheats.txt.

From a command line, any option skips the window:
    Play.bat --no-gui                  start with what was saved
    Play.bat --fps 120 --fov 70 --mouse 1.5 --fullscreen
    Play.bat --extract D:\GoldenEye.rvz
    Play.bat --window 1920x1080        (options of the game itself pass through)


Where things are
----------------
    nand\           your saves and profiles (made on the first start). Back this up.
    keys.txt        key bindings
    settings.env    what the settings window saved
    run.log         the log of the last run
    shadercache\    compiled shaders; safe to delete
    config\         Gecko codes, cheats, the server list, function names for the log
    launcher\, python\   the settings window and the disc extraction, and what runs them


If something goes wrong
-----------------------
- "The game's files are missing": Extract disc... has not been done yet. If you
  extracted with Dolphin instead, game\sys\main.dol and game\files\ must exist
  (move what is inside a DATA folder up into "game").
- Extract disc... says the image is another game: only the US version (SJBE52)
  works with this build.
- A window that closes at once, or a black one: update the graphics driver. On
  a laptop, make GoldenEye.exe use the dedicated graphics card (Windows
  Settings -> Display -> Graphics).
- "Illegal instruction", or it never opens a window on an old PC: the processor
  has no AVX2. This build cannot run there.
- Windows SmartScreen says the program is unknown: More info -> Run anyway.
- Anything else: run.log ends with what happened. Send it along with a report.


Not in this build
-----------------
- Online play: the network part is not ported to Windows yet. Everything
  offline (campaign, split-screen) is there.
- The crash-dump folders of the Linux version.


Credits
-------
Built on wiikit (MIT) and SDL3 (zlib license); the launcher runs on Python with
Tk and pycryptodome. Their licenses are in licenses\. The fonts in
"fonts" are Dolphin's free replacements for the console's (licenses\ has the note).
GoldenEye 007 is the property of its owners; this package contains none of its
data files.
