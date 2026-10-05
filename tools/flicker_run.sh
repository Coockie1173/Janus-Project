#!/bin/sh
# Play with everything on that shows why the level's geometry went missing for a moment:
#     tools/flicker_run.sh NAME [launch.sh options]
# Plays as tools/record_run.sh does (your settings, a copy of the save, the input recorded) and
# keeps in build/flicker/NAME/: video.mkv (every picture shown, small) with video.index (picture,
# the game's frame, seconds), frames.log (+ .lists: what each frame drew), zones.log (the portal
# walk of every view) and run.log.
# A picture that is suddenly much darker than the ones before gets its frame's GX commands
# traced (gxtrace_N.txt, and again when it is bright again), 60 times at most.
# Press F9 right after a flash: it marks the moment in zones.log and keeps the walks before it.
# Close the window when done. All of it is on disk, a few hundred MB for a level.
cd "$(dirname "$0")/.." || exit 1
NAME=${1:?a name for the run}; shift
DIR=build/flicker/$NAME
rm -rf "$DIR"; mkdir -p "$DIR"; rm -f gxtrace_*.txt build/gxtrace_*.txt
SIZE=${THUMBS_SIZE:-480x270}
WIIKIT_THUMBS_SIZE=$SIZE WIIKIT_THUMBS_INDEX=$DIR/video.index \
WIIKIT_THUMBS="|ffmpeg -loglevel error -f rawvideo -pix_fmt rgba -s $SIZE -r 60 -i - -c:v libx264 -preset ultrafast -crf 27 -pix_fmt yuv420p $DIR/video.mkv" \
WIIKIT_DARKTRACE=1 WIIKIT_FRAMELOG=$DIR/frames.log GE_ZONELOG=$DIR/zones.log \
SKIP=${SKIP:-1} tools/record_run.sh "flicker-$NAME" "$@"
cp build/run.log "$DIR/run.log"
mv gxtrace_*.txt build/gxtrace_*.txt "$DIR/" 2>/dev/null
echo "kept: $DIR ($(du -sh "$DIR" | cut -f1)), $(grep -c '^mark .* at frame' "$DIR/zones.log") marks"
