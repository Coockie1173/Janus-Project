#!/bin/sh
# Build if needed, then run the game. Extra arguments go to wiiboot:
#     ./launch.sh                      windowed
#     ./launch.sh --fullscreen
#     ./launch.sh --window 1920x1080 --input keyboard
#     ./launch.sh --fps 60             let the engine run up to 60 frames a second
#     ./launch.sh --fps 120            above 60: experimental (the console's video runs faster too)
# The log of the run is kept in build/run.log (send it along with a report).
cd "$(dirname "$0")" || exit 1
ARGS=
while [ $# -gt 0 ]; do
    case $1 in
        --fps) GE_FPS=$2; export GE_FPS; shift 2 ;;
        *) ARGS="$ARGS $1"; shift ;;
    esac
done
make build || exit 1
echo "keys: build/keys.txt   log: build/run.log"
make -s run RUN_ARGS="--watch 10$ARGS" 2>&1 | tee build/run.log
