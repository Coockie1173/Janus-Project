#!/bin/sh
# tools/netlab/run.sh NAME "PAD SCRIPT" SECONDS [EVERY]: a hidden instance against the local server
# NAME's NAND is build/netlab/NAME/nand, made from build/nand the first time (b* gets a console of its own)
# every line of run.log starts with the seconds since the start
# extra environment (GE_FPS, GE_MOUSE_MODE, ...) is passed through; AUDIO=1 keeps the sound on (with SDL_AUDIODRIVER=dummy nothing is heard)
cd "$(dirname "$0")/../.." || exit 1
NAME=$1; SCRIPT=$2; SECS=${3:-120}; EVERY=${4:-600}
OUT=build/netlab/$NAME; mkdir -p "$OUT/frames"
if [ ! -d "$OUT/nand" ]; then
    cp -a build/nand "$OUT/nand"
    case "$NAME" in b*) rm -f "$OUT/nand/mac.bin" "$OUT/nand/shared2/DWC_AUTHDATA";; esac
fi
FONTS=/usr/share/dolphin-emu/Sys/GC
# the window stays hidden, SHOW=1 shows it
[ -n "$SHOW" ] || { WIIKIT_HIDDEN=1; export WIIKIT_HIDDEN; }
GE_HOSTS="$PWD/tools/netlab/hosts.txt" GE_GECKO_EXTRA="$PWD/config/gecko/faststart.txt" GE_AUTO_START=1 \
GE_SKIP_MOVIES=1 GE_TRACE=1 WIIKIT_PAD="$SCRIPT" timeout $((SECS + 40)) build/out/wiiboot game --nand "$OUT/nand" \
    --keys build/keys.txt --symbols build/recomp/symbols.tsv $( [ -d $FONTS ] && echo --fonts $FONTS ) $( [ -n "$AUDIO" ] || echo --no-audio ) \
    --input keyboard --watch 10 --dump "$OUT/frames" --dump-every "$EVERY" --quit-after "$SECS" 2>&1 | awk 'BEGIN { t0 = systime() } { printf "%4d %s\n", systime() - t0, $0; fflush() }' > "$OUT/run.log"
