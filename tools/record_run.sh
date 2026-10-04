#!/bin/sh
# Record a way into the game for the scripted runs to start from:
#     tools/record_run.sh NAME [launch.sh options]
# Plays as ./launch.sh does (your settings, keys and mouse) on a copy of the save, so nothing
# played here is kept. Play to where the test should start, stand still, close the window.
# Skip Intro Cutscenes is off while recording, whatever the settings say: the times in a
# recording are the game's, so one made with the opening played replays with it skipped too.
# The input lands in build/rec/NAME.rec, the save it started from in build/rec/NAME.nand;
# REPLAY=NAME tools/scripted_run.sh "" SECONDS plays it back.
cd "$(dirname "$0")/.." || exit 1
NAME=${1:?a name for the recording}; shift
mkdir -p build/rec
rm -rf "build/rec/$NAME.nand" build/rec/run.nand
cp -a build/nand "build/rec/$NAME.nand"; cp -a build/nand build/rec/run.nand
NAND=build/rec/run.nand GE_SKIP_CUTSCENES=0 GE_RECORD=build/rec/$NAME.rec ./launch.sh --no-gui "$@"
echo "recorded: build/rec/$NAME.rec ($(grep -c . "build/rec/$NAME.rec") lines)"
