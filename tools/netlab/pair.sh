#!/bin/sh
# tools/netlab/pair.sh SECONDS: two hidden instances in one network game against a local server:
# a host (a) and, 45 s later, a joiner (b) with a console of its own, both through Connect to WFC Modded
# (find a game, none there, host one; the second finds the first's). everything lands in build/netlab
#
# the server is the Goldeneye-Wii-Server project, on this machine's loopback:
#   docker compose -f <server>/server/bootstrap/dwc-emulator/compose.yml -f tools/netlab/dwc-local.yml up --detach --wait
#   (cd <server> && .venv/bin/python -m server.run_local --host 127.0.0.1 --enable_stun_service \
#       --profile-store ... --leaderboard-store ... --friends-store ...)      # its data kept out of the server's own
#
# each one's environment comes from build/netlab/a.env and b.env (NAME=value lines, values may have
# spaces), times in them are each one's own clock: the host is in the level from 86 s and plays from
# 98, the joiner from 63 of its own (108 of the host's). useful: GE_NETTRACE=1 GE_FPS=144
# GE_STICK="100-106:l0,1" GE_MOUSEMOVE="100-108:300,-40" GE_BUTTONS="110-110.3:0200" (R, a grenade: two a life)
# a player without input for 90 s is thrown out (PLAYER IDLE TIMEOUT): a GE_STICK nudge now and then
# afterwards, in build/netlab: cmp.py (thrown things), walk.py b, steps.py b/run.log, turn.py b 98 111, yawsteps.py b own 98 111
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/../.." || exit 1
mkdir -p build/netlab && cd build/netlab || exit 1
[ -f a.env ] || echo GE_NETTRACE=1 > a.env
[ -f b.env ] || echo GE_NETTRACE=1 > b.env
SECS=${1:-185}; S=${S:-"8:A 11:D 12:D 13:D 15:A 18:D 19:D 21:A 52:A 58:A"} #S="... 52:A 57:D 58:A" is the second gamemode
( set -a; . ./a.env; set +a; "$HERE/run.sh" a "$S" "$SECS" ${EVERY:-100000} ) &
sleep 45
( set -a; . ./b.env; set +a; "$HERE/run.sh" b "$S" $((SECS - 45)) ${EVERY:-100000} )
wait
