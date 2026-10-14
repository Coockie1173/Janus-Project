#!/bin/sh
# Record a way into the game for the scripted runs to start from:
#     tools/record_run.sh NAME [launch.sh options]
# Plays as ./launch.sh does (your settings, keys and mouse) on a copy of the save, so nothing
# played here is kept. Play to where the test should start, stand still, close the window.
# No cutscene is skipped while recording (SKIP=1 skips the intros, the replay then needs
# GE_SKIP_CUTSCENES=1 as well), whatever the settings say: the times in a
# recording are the game's, so one made with the opening played replays with it skipped too.
# Hotkeys (noclip, the frame rate... F1 > Hotkeys) are recorded as the values they set, so the
# replay needs no hotkeys.txt; what is changed in the F1 menu itself is not recorded.
# The input lands in build/rec/NAME.rec, the save it started from in build/rec/NAME.nand, the
# launcher's settings it was played with in build/rec/NAME.env;
# REPLAY=NAME tools/scripted_run.sh "" SECONDS plays it back.
cd "$(dirname "$0")/.." || exit 1
NAME=${1:?a name for the recording}; shift
mkdir -p build/rec
rm -rf "build/rec/$NAME.nand" build/rec/run.nand
cp -a build/nand "build/rec/$NAME.nand"; cp -a build/nand build/rec/run.nand
[ -f build/settings.env ] && cp build/settings.env "build/rec/$NAME.env"
echo "REC_SKIP=${SKIP:-0}" >> "build/rec/$NAME.env" #the replay has to skip the openings as the recording did
SKIPPED=none; [ "${SKIP:-0}" = 1 ] && SKIPPED=intros
NAND=build/rec/run.nand GE_SKIP_LIST=$SKIPPED GE_RECORD=build/rec/$NAME.rec ./launch.sh --no-gui "$@"
echo "recorded: build/rec/$NAME.rec ($(grep -c . "build/rec/$NAME.rec") lines)"
