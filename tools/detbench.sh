#!/bin/sh
# tools/detbench.sh [ROUNDS] [GUEST_SECS] OUT_DIR...: the same work for every build, timed.
# Deterministic mode, unpaced (WIIKIT_DETERMINISTIC, WIIKIT_UNPACED): the guest's clock is virtual
# and runs as fast as the host can; the menu is driven on that clock (GE_BUTTONS) into Dam, whose
# opening plays. The game quits at GUEST_SECS of guest time (WIIKIT_QUIT_GUEST) and says how long
# that took. Every build does the same frames (the count is printed: it must match), so the times
# compare to a percent, unlike build/bench.sh's fps (menus on the wall clock, scenes drift).
#     tools/detbench.sh 4 90 build/out-a build/out-b      (rounds alternate the builds)
# DUMP=1 dumps a frame every 300 into build/detbench/frames-<n>-<round>.
cd "$(dirname "$0")/.." || exit 1
ROUNDS=${1:-3}; SECS=${2:-90}; shift 2 2>/dev/null
[ $# -gt 0 ] || set -- build/out
D=build/detbench
mkdir -p $D
B="8-8.2:10 11-11.2:4000 12-12.2:4000 $(seq 14 2 200 | awk '{printf "%d-%d.2:10 ", $1, $1}')"
for i in $(seq "$ROUNDS"); do
    for out in "$@"; do
        n=$(basename "$out")
        rm -rf $D/nand; cp -a build/nand $D/nand; cp build/keys.txt $D/keys.txt
        dump=""
        if [ -n "$DUMP" ]; then rm -rf $D/frames-$n-$i; mkdir -p $D/frames-$n-$i; dump="--dump $D/frames-$n-$i --dump-every 300"; fi
        WIIKIT_HIDDEN=1 WIIKIT_DETERMINISTIC=1 WIIKIT_UNPACED=1 WIIKIT_QUIT_GUEST=$SECS GE_BUTTONS="$B" GE_HOSTS= \
        GE_SAVED_SETTINGS=0 GE_GECKO_EXTRA="$PWD/config/gecko/faststart.txt" timeout $((SECS * 4 + 60)) \
            "$out/wiiboot" game --nand $D/nand --keys $D/keys.txt --symbols build/recomp/symbols.tsv \
            --fonts /usr/share/dolphin-emu/Sys/GC --no-audio --input keyboard $dump > $D/run-$n.log 2>&1
        echo "$n: $(grep -o 'took [0-9.]* s' $D/run-$n.log || echo 'no time (see '$D/run-$n.log')'), $(grep -o '[0-9]* frames,' $D/run-$n.log | head -1)"
    done
done
