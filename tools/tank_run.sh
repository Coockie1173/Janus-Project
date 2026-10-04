#!/bin/sh
# Into the Tank mission without a recording: the menu script that starts Dam, with GE_LEVEL
# loading Tank in its place. The opening and the two tutorial boxes are over 56 s in.
#     tools/tank_run.sh SECONDS [DUMP_EVERY_RETRACES]     (GE_STICK, GE_MOUSEMOVE... as scripted_run.sh)
cd "$(dirname "$0")/.." || exit 1
GE_LEVEL=01810145 GE_AUTO_START=1 GE_SKIP_MOVIES=1 GE_TRACE=1 exec tools/scripted_run.sh \
    "8:A 11:D 12:D $(seq 14 2 56 | sed 's/$/:A/' | tr '\n' ' ')" "${1:-70}" "${2:-300}"
