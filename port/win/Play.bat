@echo off
rem GoldenEye 007 (Wii) - start the game. Change the settings below with Notepad, then double-click.
rem Anything after Play.bat on a command line goes to the game as it is (--window 1920x1080, --input keyboard...).
setlocal
cd /d "%~dp0"

rem ---- settings ---------------------------------------------------------------------------
rem frame rate, up to 240 (30 is the console's)
set FPS=60
rem field of view in degrees (the game's own is 49)
set FOV=49
rem mouse look sensitivity (0: mouse look off)
set MOUSE=1.0
rem auto, direct or stick (the mouse as the right stick)
set MOUSE_MODE=auto
rem 1: fullscreen (F11 and Alt+Enter switch while playing)
set FULLSCREEN=0
rem 1: hurry through the screens before the menu
set FAST_START=0
rem 1: every cheat in config\gecko\cheats.txt on (delete the ones you do not want from that file)
set CHEATS=0
rem -----------------------------------------------------------------------------------------

if not exist "game\sys\main.dol" (
    echo The game's files are missing: game\sys\main.dol was not found.
    echo Extract your own GoldenEye 007 disc ^(SJBE52^) into the "game" folder - see README.txt.
    pause
    exit /b 1
)

set GE_FPS=%FPS%
set GE_FOV=%FOV%
set GE_MOUSE=%MOUSE%
set GE_MOUSE_MODE=%MOUSE_MODE%
set GE_GECKO_EXTRA=
if "%FAST_START%"=="1" set GE_GECKO_EXTRA=config/gecko/faststart.txt
if "%CHEATS%"=="1" set GE_GECKO_EXTRA=config/gecko/cheats.txt:%GE_GECKO_EXTRA%
set ARGS=
if "%FULLSCREEN%"=="1" set ARGS=--fullscreen

echo GoldenEye 007 is running. Keys: keys.txt   Log: run.log
GoldenEye.exe game --symbols config\symbols.tsv --watch 10 %ARGS% %* > run.log 2>&1
if errorlevel 1 (
    echo.
    echo The game stopped with an error. The end of run.log says why:
    echo.
    powershell -NoProfile -Command "Get-Content run.log -Tail 15"
    pause
)
