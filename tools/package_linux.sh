#!/bin/sh
# The Linux package: the portable build (make linux), what it needs beside it, the
# instructions and an empty mods folder, zipped. Nothing of the disc goes in: game/ is left for the
# player to fill.
#     sh tools/package_linux.sh [FONTS_DIR]      -> build/Janus-Project-linux64.zip
set -e
cd "$(dirname "$0")/.."
FONTS=${1:-/usr/share/dolphin-emu/Sys/GC}
NAME=Janus-Project-linux64
S=build/linux/$NAME
SDL=$(ls build/deps/sdl3/lib*/libSDL3.so.0 | head -1)
rm -rf "$S" build/$NAME.zip
mkdir -p "$S/config/gecko" "$S/fonts" "$S/game" "$S/lib" "$S/licenses"
# the function names stay in: a crash dump's host backtrace reads them
cp build/out-linux/wiiboot "$S/Janus-Project"
strip --strip-debug "$S/Janus-Project"
cp -L "$SDL" "$S/lib/libSDL3.so.0"
strip --strip-unneeded "$S/lib/libSDL3.so.0"
cp config/keys.txt "$S/keys.txt"
cp config/keys.txt config/hosts.txt config/openings.txt config/cutscenes.txt build/recomp/symbols.tsv "$S/config/"
cp build/patched/gecko.txt "$S/config/gecko-codes.txt"
cp config/gecko/*.txt "$S/config/gecko/"
for f in font_western.bin font_japanese.bin dsp_coef.bin; do
    [ -f "$FONTS/$f" ] && cp "$FONTS/$f" "$S/fonts/" || echo "no $f in $FONTS: the package goes without it"
done
[ -f "$FONTS/font-licenses.txt" ] && cp "$FONTS/font-licenses.txt" "$S/licenses/fonts.txt"
cp build/deps/SDL3-src/LICENSE.txt "$S/licenses/SDL3.txt"
cp wiikit/LICENSE "$S/licenses/wiikit.txt" 2>/dev/null || true
cp LICENSE "$S/licenses/Janus-Project.txt"
cp build/deps/imgui/LICENSE.txt "$S/licenses/imgui.txt"
cp port/licenses/Roboto.txt "$S/licenses/Roboto.txt"
# the window before the game and the disc extraction: the launcher, wiikit's disc readers (the system's Python runs them)
mkdir -p "$S/launcher/wiikit"
cp tools/launcher.py tools/extract_disc.py "$S/launcher/"
cp wiikit/disc.py wiikit/rvz.py wiikit/aes.py "$S/launcher/wiikit/"
: > "$S/launcher/wiikit/__init__.py"
# the mods folder is there and empty, with its folder for each online gamemode (README.md, Mods):
# the mods themselves are a download of their own (tools/package_mods.sh), unzipped into this one
mkdir -p "$S/mods"
for d in conflict team_conflict golden_gun black_box goldeneye heroes license_to_kill team_license_to_kill; do
    mkdir -p "$S/mods/$d"
done
cp port/linux/Play.sh port/linux/README.txt "$S/"
chmod 755 "$S/Play.sh" "$S/Janus-Project"
printf 'The game goes here: ./Play.sh, then "Extract disc..." and pick your GoldenEye 007 (SJBE52) image.\nWhen it is done this folder holds sys/main.dol and files/. See README.txt.\n' > "$S/game/PUT YOUR DISC HERE.txt"
(cd build/linux && zip -q -r -9 "../$NAME.zip" "$NAME")
echo "build/$NAME.zip: $(du -h build/$NAME.zip | cut -f1)"
