#!/bin/sh
# Build if needed, then run the game. Extra arguments go to wiiboot:
#     ./launch.sh                      windowed
#     ./launch.sh --fullscreen
#     ./launch.sh --window 1920x1080 --input keyboard
# The log of the run is kept in build/run.log (send it along with a report).
cd "$(dirname "$0")" || exit 1
make build || exit 1
echo "keys: build/keys.txt   log: build/run.log"
make -s run RUN_ARGS="--watch 10 $*" 2>&1 | tee build/run.log
