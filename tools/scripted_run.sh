#!/bin/sh
# A reproducible run driven by timed button presses, with frames dumped:
#     tools/scripted_run.sh "6:A 8:A 30:D 31:A" SECONDS [DUMP_EVERY_RETRACES] [extra wiiboot args]
# Script syntax (wiikit's WIIKIT_PAD): TIME:BUTTONS, buttons A B 1 2 + - H U D L R
# (1 is the Classic's X, 2 its Y). Frames land in build/frames, a contact sheet in
# build/sheet.png, the log in build/run-script.log. AUDIO=1 keeps the sound on
# (SDL_AUDIODRIVER=dummy with it: the audio path runs, nothing is heard).
cd "$(dirname "$0")/.." || exit 1
SCRIPT=$1; SECS=${2:-60}; EVERY=${3:-300}; shift 3 2>/dev/null
rm -rf build/frames; mkdir -p build/frames
FONTS=/usr/share/dolphin-emu/Sys/GC
WIIKIT_PAD="$SCRIPT" timeout $((SECS + 40)) build/out/wiiboot game --nand build/nand --keys build/keys.txt \
    --symbols build/recomp/symbols.tsv $( [ -d $FONTS ] && echo --fonts $FONTS ) $( [ -n "$AUDIO" ] || echo --no-audio ) --input keyboard \
    --watch 10 --dump build/frames --dump-every "$EVERY" --quit-after "$SECS" "$@" > build/run-script.log 2>&1
echo "exit $?" >> build/run-script.log
if command -v montage >/dev/null; then
    montage build/frames/frame_*.png -tile 6x -geometry 426x240+2+2 -label '%f' build/sheet.png 2>/dev/null
fi
tail -25 build/run-script.log
