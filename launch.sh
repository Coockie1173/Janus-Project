#!/bin/sh
# Run the game. With no arguments a window comes first: the frame rate, the field of view,
# the mouse's sensitivity, online on or off, and the key bindings; then Play.
#     ./launch.sh                      the settings window, then the game
#     ./launch.sh --no-gui             straight to the game, with the saved settings
#     ./launch.sh --fps 60             the frame rate (auto: the screen's refresh rate; 30: the console's)
#     ./launch.sh --fov 70             the field of view in degrees (the game's is 49)
#     ./launch.sh --mouse 1.5          mouse look sensitivity (1 by default; 0: mouse look off)
#     ./launch.sh --offline            no hosts file: the game does not find its servers
#     ./launch.sh --fullscreen         (and any other option of wiiboot: --window 1920x1080, --input keyboard...)
# Any option skips the window. Settings are kept in build/settings.env, the keys in build/keys.txt.
# Keyboard and mouse by default: W A S D move, the mouse looks, left button fires, right button aims,
# the wheel changes weapon, R reloads, Ctrl crouches, Shift sprints, E acts, G grenade, V melee;
# in menus the left button selects and the right one goes back. Esc frees the mouse.
# The log of the run is kept in build/run.log (send it along with a report).
cd "$(dirname "$0")" || exit 1
mkdir -p build
[ -f build/keys.txt ] || cp config/keys.txt build/keys.txt

if [ $# -eq 0 ] && command -v python3 >/dev/null; then
    python3 tools/launcher.py
    [ $? -eq 3 ] && exit 0                           # the window was closed without Play
fi

# the saved settings, then the command line over them
LAUNCH_FPS=auto; LAUNCH_FOV=49; LAUNCH_MOUSE=1.0; LAUNCH_ONLINE=1; LAUNCH_FULLSCREEN=0
[ -f build/settings.env ] && . ./build/settings.env
ARGS=
while [ $# -gt 0 ]; do
    case $1 in
        --no-gui) shift ;;
        --fps) LAUNCH_FPS=$2; shift 2 ;;
        --fov) LAUNCH_FOV=$2; shift 2 ;;
        --mouse) LAUNCH_MOUSE=$2; shift 2 ;;
        --offline) LAUNCH_ONLINE=0; shift ;;
        --fullscreen) LAUNCH_FULLSCREEN=1; shift ;;
        *) ARGS="$ARGS $1"; shift ;;
    esac
done
GE_FPS=${GE_FPS:-$LAUNCH_FPS}; GE_FOV=${GE_FOV:-$LAUNCH_FOV}; GE_MOUSE=${GE_MOUSE:-$LAUNCH_MOUSE}
export GE_FPS GE_FOV GE_MOUSE
if [ "$LAUNCH_ONLINE" = 0 ]; then GE_HOSTS=; export GE_HOSTS; fi
[ "$LAUNCH_FULLSCREEN" = 1 ] && ARGS="$ARGS --fullscreen"

make build || exit 1
echo "keys: build/keys.txt   log: build/run.log"
make -s run RUN_ARGS="--watch 10$ARGS" 2>&1 | tee build/run.log
