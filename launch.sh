#!/bin/sh
# Run the game. With no arguments a window comes first: the frame rate, the field of view,
# the mouse's sensitivity, online on or off, and the key bindings; then Play.
#     ./launch.sh                      the settings window, then the game
#     ./launch.sh --no-gui             straight to the game, with the saved settings
#     ./launch.sh --fps 120            the frame rate, up to 275 (60 by default; 30 is the console's)
#     ./launch.sh --vsync              frames wait for the screen's refresh, no tearing (the F1 menu's Screen)
#     ./launch.sh --no-sound-height    sounds above and below you sound the same, as on the console (the F1 menu's Enhancements)
#     ./launch.sh --no-occlusion       sounds are heard through walls and floors, as on the console (the F1 menu's Enhancements)
#     ./launch.sh --no-ping            no pings next to the names in the scoreboard of a modded online game (the F1 menu's Screen)
#     ./launch.sh --fov 70             the field of view in degrees (the game's is 49)
#     ./launch.sh --mouse 1.5          mouse look sensitivity (1 by default; 0: mouse look off)
#     ./launch.sh --crash-dumps        write a dump into build/crashes when the game dies or stalls (on by
#                                      default; --no-crash-dumps turns it off, --crash-mem adds the game's
#                                      memory to each dump, 88 MB)
#     ./launch.sh --fast-start         hurry through the screens before the menu (config/gecko/faststart.txt)
#     ./launch.sh --cheats             every cheat in config/gecko/cheats.txt on (the window's Cheats tab picks them)
#     ./launch.sh --mouse-mode stick   the mouse as right-stick input (auto does this in a game someone
#                                      else hosts; direct never does)
#     ./launch.sh --always-mouse       the cheat "Always Enable Mouse": mouse look in every player state
#                                      (cutscenes, melee, climbing...), also in the F1 menu's Cheats
#     ./launch.sh --skip-cutscenes     a mission's opening cutscene is skipped (also in
#                                      the F1 menu's Enhancements)
#     ./launch.sh --skip-zukovsky      the meeting with Zukovsky in Nightclub is skipped (the F1 menu's Enhancements)
#     ./launch.sh --skip-briggs        the meeting with Sky Briggs in Carrier is skipped (the F1 menu's Enhancements)
#     ./launch.sh --skip-endings       a mission's ending cutscene is skipped (the F1 menu's Enhancements)
#     ./launch.sh --no-level-timer     the hud without the level's time (on by default, the F1 menu's Enhancements)
#     ./launch.sh --auto-qte           quick time events press their own buttons (the F1 menu's Enhancements)
#     ./launch.sh --skip-movies        briefings, the movies between missions and the logos are skipped
#     ./launch.sh --no-tutorials       the tutorial boxes in the missions aren't shown (the F1 menu's Enhancements)
#     ./launch.sh --no-colour-filter   the tint some levels lay over the picture isn't drawn (the F1 menu's Enhancements)
#     ./launch.sh --auto-start         a loaded mission starts without "Continue" being pressed (the F1 menu's Enhancements)
#     ./launch.sh --extras-menu        "Unused Extras Menu" (the F1 menu's Enhancements)
#     ./launch.sh --unlock-all         "Unlock Everything": every mission and difficulty (the F1 menu's Enhancements)
#     ./launch.sh --stamina-meter      the cheat "Stamina Meter": the sprint that's left as a bar
#     ./launch.sh --trace              log the mouse's player, its angles and the look state (for a report)
#     ./launch.sh --extract GAME.rvz   take the game's files out of a disc image (.rvz .iso .wbfs) into ./game
#                                      and stop; the window's "Extract disc..." does the same
#     ./launch.sh --offline            no hosts file: the game does not find its servers
#     ./launch.sh --fullscreen         (and any other option of wiiboot: --window 1920x1080, --input keyboard...)
# Any option skips the window. Settings are kept in build/settings.env, the keys in build/keys.txt.
# Keyboard and mouse by default: W A S D move, the mouse looks, left button fires, right button aims,
# the wheel changes weapon, R reloads, Ctrl crouches, Shift sprints, E acts, G grenade, V melee;
# in menus the left button selects and the right one goes back. Esc frees the mouse. F1: the settings menu.
# The window goes through X11 (XWayland) when DISPLAY is set: SDL_VIDEODRIVER=wayland for Wayland's own.
# The log of the run is kept in build/run.log. A crash, the runtime giving up or 20 s of standing still (0 fps) leaves
# a dump in build/crashes/<date>-<time>-<kind>/ (send that folder along with a report).
cd "$(dirname "$0")" || exit 1
mkdir -p build
[ -f build/keys.txt ] || cp config/keys.txt build/keys.txt

if [ "$1" = --extract ]; then
    [ -n "$2" ] || { echo "--extract wants the disc image: ./launch.sh --extract GoldenEye.rvz"; exit 2; }
    shift; exec python3 tools/extract_disc.py "$@"
fi

if [ $# -eq 0 ] && command -v python3 >/dev/null; then
    python3 tools/launcher.py
    [ $? -eq 3 ] && exit 0 #the window was closed without Play
fi

# the saved settings, then the command line over them
LAUNCH_FPS=auto; LAUNCH_FOV=49; LAUNCH_MOUSE=1.0; LAUNCH_ONLINE=1; LAUNCH_FULLSCREEN=0; LAUNCH_FASTSTART=0; LAUNCH_MOUSEMODE=auto
LAUNCH_CRASHDUMP=1; LAUNCH_CRASHMEM=0; LAUNCH_MOUSEINVERT=0; LAUNCH_MOUSEEVERYWHERE=0; LAUNCH_SKIPCUTSCENES=0; LAUNCH_SKIPZUKOVSKY=0; LAUNCH_SKIPBRIGGS=0; LAUNCH_SKIPENDINGS=0
LAUNCH_LEVELTIMER=1; LAUNCH_AUTOQTE=0; LAUNCH_AUTOSTART=0; LAUNCH_SKIPMOVIES=0; LAUNCH_NOTUTORIALS=0; LAUNCH_NOCOLOURFILTER=0; LAUNCH_AUTOSAVE=1; LAUNCH_EXTRASMENU=0; LAUNCH_UNLOCKALL=0; LAUNCH_ONLINEUNLOCKS=1; LAUNCH_STAMINAMETER=0; LAUNCH_ADSFOV=22.5; LAUNCH_ADSSENS=0.5; LAUNCH_AIMLOCK=0; LAUNCH_VSYNC=0; LAUNCH_PING=1; LAUNCH_OCCLUSION=1; LAUNCH_SOUNDHEIGHT=1
[ -f build/settings.env ] && . ./build/settings.env
ARGS=
while [ $# -gt 0 ]; do
    case $1 in
        --no-gui) shift ;;
        --fps) LAUNCH_FPS=$2; shift 2 ;;
        --vsync) LAUNCH_VSYNC=1; shift ;;
        --no-ping) LAUNCH_PING=0; shift ;;
        --no-occlusion) LAUNCH_OCCLUSION=0; shift ;;
        --no-sound-height) LAUNCH_SOUNDHEIGHT=0; shift ;;
        --fov) LAUNCH_FOV=$2; shift 2 ;;
        --mouse) LAUNCH_MOUSE=$2; shift 2 ;;
        --mouse-mode) LAUNCH_MOUSEMODE=$2; shift 2 ;;
        --always-mouse) LAUNCH_MOUSEEVERYWHERE=1; shift ;;
        --skip-cutscenes) LAUNCH_SKIPCUTSCENES=1; shift ;;
        --skip-zukovsky) LAUNCH_SKIPZUKOVSKY=1; shift ;;
        --skip-briggs) LAUNCH_SKIPBRIGGS=1; shift ;;
        --skip-endings) LAUNCH_SKIPENDINGS=1; shift ;;
        --no-level-timer) LAUNCH_LEVELTIMER=0; shift ;;
        --auto-qte) LAUNCH_AUTOQTE=1; shift ;;
        --auto-start) LAUNCH_AUTOSTART=1; shift ;;
        --skip-movies) LAUNCH_SKIPMOVIES=1; shift ;;
        --no-tutorials) LAUNCH_NOTUTORIALS=1; shift ;;
        --no-colour-filter) LAUNCH_NOCOLOURFILTER=1; shift ;;
        --extras-menu) LAUNCH_EXTRASMENU=1; shift ;;
        --unlock-all) LAUNCH_UNLOCKALL=1; shift ;;
        --stamina-meter) LAUNCH_STAMINAMETER=1; shift ;;
        --offline) LAUNCH_ONLINE=0; shift ;;
        --fullscreen) LAUNCH_FULLSCREEN=1; shift ;;
        --fast-start) LAUNCH_FASTSTART=1; shift ;;
        --crash-dumps) LAUNCH_CRASHDUMP=1; shift ;;
        --no-crash-dumps) LAUNCH_CRASHDUMP=0; shift ;;
        --crash-mem) LAUNCH_CRASHDUMP=1; LAUNCH_CRASHMEM=1; shift ;;
        --cheats) cp config/gecko/cheats.txt build/cheats.txt; shift ;;
        --trace) GE_TRACE=1; GE_TRACELOOK=1; export GE_TRACE GE_TRACELOOK; shift ;;
        *) ARGS="$ARGS $1"; shift ;;
    esac
done
# 275 at most, auto is 60 (GE_FPS in the environment still overrides)
case $LAUNCH_FPS in auto) ;; *) [ "$LAUNCH_FPS" -gt 275 ] 2>/dev/null && LAUNCH_FPS=275 ;; esac
GE_FPS=${GE_FPS:-$LAUNCH_FPS}; GE_FOV=${GE_FOV:-$LAUNCH_FOV}; GE_MOUSE=${GE_MOUSE:-$LAUNCH_MOUSE}
GE_MOUSE_MODE=${GE_MOUSE_MODE:-$LAUNCH_MOUSEMODE}; GE_MOUSE_INVERT=${GE_MOUSE_INVERT:-$LAUNCH_MOUSEINVERT}
GE_MOUSE_EVERYWHERE=${GE_MOUSE_EVERYWHERE:-$LAUNCH_MOUSEEVERYWHERE}; GE_SKIP_CUTSCENES=${GE_SKIP_CUTSCENES:-$LAUNCH_SKIPCUTSCENES}
GE_LEVEL_TIMER=${GE_LEVEL_TIMER:-$LAUNCH_LEVELTIMER}; GE_EXTRAS_MENU=${GE_EXTRAS_MENU:-$LAUNCH_EXTRASMENU}; GE_UNLOCK_ALL=${GE_UNLOCK_ALL:-$LAUNCH_UNLOCKALL}; GE_ONLINE_UNLOCKS=${GE_ONLINE_UNLOCKS:-$LAUNCH_ONLINEUNLOCKS}; GE_STAMINA=${GE_STAMINA:-$LAUNCH_STAMINAMETER}
GE_ADS_FOV=${GE_ADS_FOV:-$LAUNCH_ADSFOV}; GE_ADS_SENS=${GE_ADS_SENS:-$LAUNCH_ADSSENS}; GE_AIM_LOCK=${GE_AIM_LOCK:-$LAUNCH_AIMLOCK}
GE_AUTO_QTE=${GE_AUTO_QTE:-$LAUNCH_AUTOQTE}; GE_AUTO_START=${GE_AUTO_START:-$LAUNCH_AUTOSTART}
GE_SKIP_MOVIES=${GE_SKIP_MOVIES:-$LAUNCH_SKIPMOVIES}; GE_SKIP_ZUKOVSKY=${GE_SKIP_ZUKOVSKY:-$LAUNCH_SKIPZUKOVSKY}; GE_SKIP_BRIGGS=${GE_SKIP_BRIGGS:-$LAUNCH_SKIPBRIGGS}; GE_SKIP_ENDINGS=${GE_SKIP_ENDINGS:-$LAUNCH_SKIPENDINGS}
GE_NO_TUTORIALS=${GE_NO_TUTORIALS:-$LAUNCH_NOTUTORIALS}; GE_NO_COLOUR_FILTER=${GE_NO_COLOUR_FILTER:-$LAUNCH_NOCOLOURFILTER}; GE_AUTOSAVE=${GE_AUTOSAVE:-$LAUNCH_AUTOSAVE}
GE_VSYNC=${GE_VSYNC:-$LAUNCH_VSYNC}; GE_PING=${GE_PING:-$LAUNCH_PING}; GE_OCCLUSION=${GE_OCCLUSION:-$LAUNCH_OCCLUSION}; GE_SOUND_HEIGHT=${GE_SOUND_HEIGHT:-$LAUNCH_SOUNDHEIGHT}; export GE_VSYNC GE_PING GE_OCCLUSION GE_SOUND_HEIGHT
export GE_ADS_FOV GE_ADS_SENS GE_AIM_LOCK GE_AUTO_QTE GE_AUTO_START GE_SKIP_MOVIES GE_SKIP_ZUKOVSKY GE_SKIP_BRIGGS GE_SKIP_ENDINGS GE_NO_TUTORIALS GE_NO_COLOUR_FILTER GE_AUTOSAVE
export GE_FPS GE_FOV GE_MOUSE GE_MOUSE_MODE GE_MOUSE_INVERT GE_MOUSE_EVERYWHERE GE_SKIP_CUTSCENES GE_LEVEL_TIMER GE_EXTRAS_MENU GE_UNLOCK_ALL GE_ONLINE_UNLOCKS GE_STAMINA
# through X11 when there is one: a present on Wayland costs the renderer 2 ms, 0.3 there
# (SDL_VIDEODRIVER in the environment overrides)
if [ -z "$SDL_VIDEODRIVER" ] && [ -n "$DISPLAY" ]; then SDL_VIDEODRIVER=x11,wayland; export SDL_VIDEODRIVER; fi
if [ "$LAUNCH_ONLINE" = 0 ]; then GE_HOSTS=; export GE_HOSTS; fi
[ "$LAUNCH_FULLSCREEN" = 1 ] && ARGS="$ARGS --fullscreen"
if [ "$LAUNCH_FASTSTART" = 1 ]; then GE_GECKO_EXTRA="$PWD/config/gecko/faststart.txt${GE_GECKO_EXTRA:+:$GE_GECKO_EXTRA}"; export GE_GECKO_EXTRA; fi

# the crash handler: dumps unless switched off, the memory only when asked for
if [ "$LAUNCH_CRASHDUMP" = 0 ]; then GE_NO_CRASHDUMP=1; export GE_NO_CRASHDUMP; else unset GE_NO_CRASHDUMP; fi
if [ "$LAUNCH_CRASHMEM" = 1 ]; then GE_CRASH_MEM=1; export GE_CRASH_MEM; fi
# whatever the Cheats tab ticked lives in build/cheats.txt, nothing in there means no cheats
if grep -q '^\$' build/cheats.txt 2>/dev/null; then GE_GECKO_EXTRA="$PWD/build/cheats.txt${GE_GECKO_EXTRA:+:$GE_GECKO_EXTRA}"; export GE_GECKO_EXTRA; fi

make build || exit 1
echo "keys: build/keys.txt   log: build/run.log"
BEFORE=$(ls build/crashes 2>/dev/null | wc -l)
make -s run RUN_ARGS="--watch 10$ARGS" 2>&1 | tee build/run.log
# a run that left a crash dump: finish it (names, disassembly) and say where it is
if [ "$(ls build/crashes 2>/dev/null | wc -l)" -gt "$BEFORE" ]; then
    DUMP=build/crashes/$(ls build/crashes | tail -1)
    python3 tools/crash_report.py "$DUMP" > /dev/null 2>&1
    echo
    echo "the game left a crash dump: $DUMP (report.txt, analysis.txt)"
fi
