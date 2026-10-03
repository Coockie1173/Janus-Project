@echo off
rem GoldenEye 007 (Wii) - double-click: the settings window, then the game.
rem     Play.bat --no-gui               straight to the game, with the saved settings
rem     Play.bat --extract GAME.rvz     take the game's files out of a disc image (.rvz .iso .wbfs) into "game"
rem     Play.bat --fps 120 --fov 70 --mouse 1.5 --fullscreen --fast-start --cheats
rem     Play.bat --always-mouse         the cheat "Always Enable Mouse": mouse look in every player state
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
