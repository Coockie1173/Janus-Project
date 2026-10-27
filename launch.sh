#!/bin/sh
# Build if needed and run the game, the way the packages start it: the launcher in the executable
# (launcher/) reads build/settings.env and the options, the F1 menu has every setting.
#     ./launch.sh                      the game with the saved settings
#     ./launch.sh --help               every option (--fps 120, --fov 70, --offline, --fast-start, --skip ...)
#     ./launch.sh --fullscreen         (and any other option of wiiboot: --window 1920x1080, --input keyboard...)
#     ./launch.sh --extract GAME.rvz   take the game's files out of a disc image into ./game and stop
#                                      (python: there's no executable to do it before game/ is there)
# Settings are kept in build/settings.env, the keys in build/keys.txt, the saves in build/nand
# (NAND=dir for another), the log of the run in build/run.log. GE_BIN=path runs another build.
# A crash, the runtime giving up or 20 s of standing still (0 fps) leaves a dump in
# build/crashes/<date>-<time>-<kind>/ (send that folder along with a report).
cd "$(dirname "$0")" || exit 1
mkdir -p build

if [ "$1" = --extract ]; then
    [ -n "$2" ] || { echo "--extract wants the disc image: ./launch.sh --extract GoldenEye.rvz"; exit 2; }
    shift; exec python3 tools/extract_disc.py "$@"
fi

make build || exit 1
echo "keys: build/keys.txt   log: build/run.log"
BEFORE=$(ls build/crashes 2>/dev/null | wc -l)
GE_RUN_LOG= ${GE_BIN:-build/out/wiiboot} "$@" ${NAND:+--nand "$NAND"} 2>&1 | tee build/run.log
# a run that left a crash dump: finish it (names, disassembly) and say where it is
if [ "$(ls build/crashes 2>/dev/null | wc -l)" -gt "$BEFORE" ]; then
    DUMP=build/crashes/$(ls build/crashes | tail -1)
    python3 tools/crash_report.py "$DUMP" > /dev/null 2>&1
    echo
    echo "the game left a crash dump: $DUMP (report.txt, analysis.txt)"
fi
