#!/bin/sh
# Build if needed, then run the game. Extra arguments go to wiiboot:
#     ./launch.sh                      windowed
#     ./launch.sh --fullscreen
#     ./launch.sh --window 1920x1080 --input keyboard
#     ./launch.sh --offline            no hosts file: the game does not find its servers
#     ./launch.sh --fps 60             the frame rate (default: the screen's refresh rate; 30: the console's)
#     ./launch.sh --mouse 1.5          mouse look sensitivity (default 1; 0: mouse look off)
# Keyboard and mouse: W A S D move, the mouse looks, left button fires, right button aims,
# the wheel changes weapon, R reloads, C crouches, Shift sprints, E acts, G grenade, V melee;
# the rest is in build/keys.txt ([Classic Controller]). Esc frees the mouse.
# The log of the run is kept in build/run.log (send it along with a report).
cd "$(dirname "$0")" || exit 1
ARGS=
GE_FPS=${GE_FPS:-auto}; export GE_FPS                # the screen's refresh rate unless --fps says otherwise
while [ $# -gt 0 ]; do
    case $1 in
        --fps) GE_FPS=$2; export GE_FPS; shift 2 ;;
        --mouse) GE_MOUSE=$2; export GE_MOUSE; shift 2 ;;
        --offline) GE_HOSTS=; export GE_HOSTS; shift ;;
        *) ARGS="$ARGS $1"; shift ;;
    esac
done
make build || exit 1
echo "keys: build/keys.txt   log: build/run.log"
make -s run RUN_ARGS="--watch 10$ARGS" 2>&1 | tee build/run.log
