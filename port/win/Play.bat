@echo off
rem GoldenEye 007 (Wii) - double-click: the settings window, then the game.
rem     Play.bat --no-gui               straight to the game, with the saved settings
rem     Play.bat --extract GAME.rvz     take the game's files out of a disc image (.rvz .iso .wbfs) into "game"
rem     Play.bat --fps 120 --fov 70 --mouse 1.5 --fullscreen --fast-start --cheats
rem     Play.bat --always-mouse         the cheat "Always Enable Mouse": mouse look in every player state
rem     Play.bat --skip-cutscenes       a mission's opening cutscene is skipped
rem     Play.bat --skip-zukovsky        the meeting with Zukovsky in Nightclub is skipped
rem     Play.bat --skip-briggs          the meeting with Sky Briggs in Carrier is skipped
rem     Play.bat --no-skip-fade         a skipped cutscene is fast forwarded in sight instead of behind a fade
rem     Play.bat --skip-movies          briefings, the movies between missions and the logos are skipped
rem     Play.bat --no-tutorials         the tutorial boxes in the missions aren't shown
rem     Play.bat --no-colour-filter     the tint some levels lay over the picture isn't drawn
rem     Play.bat --no-level-timer       the hud without the level's time (it is on by default)
rem     Play.bat --auto-qte             quick time events press their own buttons
rem     Play.bat --auto-start           a loaded mission starts without "Continue" being pressed
rem     Play.bat --extras-menu          the cheat "Unused Extras Menu"
rem     Play.bat --unlock-all           the cheat "Unlock Everything": every mission and difficulty
rem Any option skips the window; what the launcher does not know goes to the game (--window 1920x1080...).
setlocal
cd /d "%~dp0"
if not exist "python\python.exe" (
    echo The "python" folder is missing: unzip the whole package, not only Play.bat.
    pause
    exit /b 1
)
python\python.exe launcher\launcher.py %*
if errorlevel 1 pause
