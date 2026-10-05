#!/bin/sh
# A reproducible run driven by timed button presses, with frames dumped:
#     tools/scripted_run.sh "6:A 8:A 30:D 31:A" SECONDS [DUMP_EVERY_RETRACES] [extra wiiboot args]
# Script syntax (wiikit's WIIKIT_PAD): TIME:BUTTONS, buttons A B 1 2 + - H U D L R
# (1 is the Classic's X, 2 its Y). Frames land in build/frames, a contact sheet in
# build/sheet.png, the log in build/run-script.log. AUDIO=1 keeps the sound on
# (SDL_AUDIODRIVER=dummy with it: the audio path runs, nothing is heard). GE_BIN=path runs another build.
# The screens before the title are hurried through (config/gecko/faststart.txt: the title is up
# 6 s in); INTRO=1 plays them. "dam" as the script is the way into Dam from a save with a
# profile: the title's button at 8, down twice to Mission Select, then A every 2 s. The level
# is drawn from about 27 s, its opening runs past 70.
# REPLAY=name plays build/rec/name.rec first (tools/record_run.sh) on a copy of the save it was
# recorded from and with the settings it was played with (build/rec/name.env: the frame rate, the
# skips, auto start... the menus only replay at the pace they were recorded at); GE_STICK, GE_BUTTONS, GE_MOUSEMOVE and the other aids then count from its end.
# "training" as the script goes New Game, Operative; with REPLAY the recording then takes over
# where the level starts (GE_REPLAY_FROM=1), its own menu part is left out.
cd "$(dirname "$0")/.." || exit 1
DAM="8:A 11:D 12:D $(seq 14 2 600 | sed 's/$/:A/' | tr '\n' ' ')"
SCRIPT=$1; [ "$SCRIPT" = dam ] && SCRIPT=$DAM
[ "$SCRIPT" = training ] && { SCRIPT="8:A 12:U 13:A 16:A"; [ -n "$REPLAY" ] && { GE_REPLAY_FROM=1; export GE_REPLAY_FROM; }; }
SECS=${2:-60}; EVERY=${3:-300}; shift 3 2>/dev/null
rm -rf build/frames; mkdir -p build/frames
NAND=${NAND:-build/nand}
if [ -n "$REPLAY" ]; then
    [ -f "build/rec/$REPLAY.rec" ] || { echo "no recording build/rec/$REPLAY.rec"; exit 1; }
    rm -rf build/rec/run.nand; cp -a "build/rec/$REPLAY.nand" build/rec/run.nand
    NAND=build/rec/run.nand; GE_REPLAY=build/rec/$REPLAY.rec; export GE_REPLAY
    if [ -f "build/rec/$REPLAY.env" ]; then #what launch.sh makes of the settings, the environment still overrides
        . "./build/rec/$REPLAY.env"
        GE_FPS=${GE_FPS:-$LAUNCH_FPS}; GE_FOV=${GE_FOV:-$LAUNCH_FOV}; GE_MOUSE=${GE_MOUSE:-$LAUNCH_MOUSE}
        GE_MOUSE_EVERYWHERE=${GE_MOUSE_EVERYWHERE:-$LAUNCH_MOUSEEVERYWHERE}; GE_SKIP_CUTSCENES=${GE_SKIP_CUTSCENES:-${REC_SKIP:-0}}; GE_SKIP_ZUKOVSKY=${GE_SKIP_ZUKOVSKY:-0}; GE_SKIP_BRIGGS=${GE_SKIP_BRIGGS:-0}
        GE_EXTRAS_MENU=${GE_EXTRAS_MENU:-$LAUNCH_EXTRASMENU}; GE_UNLOCK_ALL=${GE_UNLOCK_ALL:-$LAUNCH_UNLOCKALL}; GE_AUTO_QTE=${GE_AUTO_QTE:-$LAUNCH_AUTOQTE}
        GE_AUTO_START=${GE_AUTO_START:-$LAUNCH_AUTOSTART}; GE_SKIP_MOVIES=${GE_SKIP_MOVIES:-$LAUNCH_SKIPMOVIES}
        GE_NO_TUTORIALS=${GE_NO_TUTORIALS:-$LAUNCH_NOTUTORIALS}
        GE_ADS_FOV=${GE_ADS_FOV:-$LAUNCH_ADSFOV}; GE_ADS_SENS=${GE_ADS_SENS:-$LAUNCH_ADSSENS}
        export GE_FPS GE_FOV GE_MOUSE GE_MOUSE_EVERYWHERE GE_SKIP_CUTSCENES GE_SKIP_ZUKOVSKY GE_SKIP_BRIGGS GE_EXTRAS_MENU GE_UNLOCK_ALL GE_AUTO_QTE GE_AUTO_START GE_SKIP_MOVIES GE_NO_TUTORIALS GE_ADS_FOV GE_ADS_SENS
        [ "$LAUNCH_FASTSTART" = 1 ] || INTRO=1
    fi
fi
[ -n "$INTRO" ] || { GE_GECKO_EXTRA="$PWD/config/gecko/faststart.txt${GE_GECKO_EXTRA:+:$GE_GECKO_EXTRA}"; export GE_GECKO_EXTRA; }
FONTS=/usr/share/dolphin-emu/Sys/GC
WIIKIT_PAD="$SCRIPT" timeout $((SECS + 40)) ${GE_BIN:-build/out/wiiboot} game --nand $NAND --keys build/keys.txt \
    --symbols build/recomp/symbols.tsv $( [ -d $FONTS ] && echo --fonts $FONTS ) $( [ -n "$AUDIO" ] || echo --no-audio ) --input keyboard \
    --watch 10 --dump build/frames --dump-every "$EVERY" --quit-after "$SECS" "$@" > build/run-script.log 2>&1
echo "exit $?" >> build/run-script.log
if command -v montage >/dev/null; then
    montage build/frames/frame_*.png -alpha off -tile 6x -geometry 426x240+2+2 -label '%f' build/sheet.png 2>/dev/null
fi
tail -25 build/run-script.log
