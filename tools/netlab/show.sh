#!/bin/sh
# tools/netlab/show.sh NAME [FIRST NUDGE]: a visible instance against the local server (pair.sh has how it is started), into
# Golden Gun of Connect to WFC Modded, then left to the keyboard and mouse for an hour. a first (the host), b 45 s later
# (show.sh a 150, show.sh b 120). a stick nudge of 0.2 s a minute keeps the one not played from being thrown out as idle.
# GE_ZONELOG is on: F9 marks build/netlab/NAME/zones.log when somebody is gone
cd "$(dirname "$0")/../.." || exit 1
NAME=$1; OUT=build/netlab/$NAME; mkdir -p "$OUT/frames"
[ -d "$OUT/nand" ] || exit 1
NUDGE=$(i=0; t=${2:-150}; while [ $t -lt 3600 ]; do [ $((i % 2)) = 0 ] && d=1 || d=-1; printf '%d-%d.2:l0,%d ' $t $t $d; t=$((t + 60)); i=$((i + 1)); done)
export GE_HOSTS="$PWD/tools/netlab/hosts.txt" GE_GECKO_EXTRA="$PWD/config/gecko/faststart.txt" GE_AUTO_START=1 GE_SKIP_MOVIES=1
export GE_FPS=144 GE_FOV=90 GE_MOUSE=1.00 GE_MOUSE_MODE=direct GE_ADS_FOV=40 GE_ADS_SENS=0.25 GE_PING=1
export GE_ZONELOG="$PWD/$OUT/zones.log" GE_STICK="$NUDGE" SDL_VIDEODRIVER=x11,wayland
WIIKIT_PAD="8:A 11:D 12:D 13:D 15:A 18:D 19:D 21:A 23:A 52:A 56:D 57:D 58:A" exec build/out/wiiboot game --nand "$OUT/nand" \
    --keys build/keys.txt --symbols build/recomp/symbols.tsv --fonts /usr/share/dolphin-emu/Sys/GC --no-audio \
    --input keyboard --watch 10 --dump "$OUT/frames" --dump-every 100000000 --quit-after 3600 > "$OUT/run.log" 2>&1
