#!/bin/sh
# The Windows package: the cross-built game (make win), what it needs beside it, the
# instructions and an empty mods folder, zipped. Nothing of the disc goes in: game/ is left for the
# player to fill.
#     sh tools/package_win.sh [FONTS_DIR]      -> build/GoldenEye007-win64.zip
set -e
cd "$(dirname "$0")/.."
FONTS=${1:-/usr/share/dolphin-emu/Sys/GC}
NAME=GoldenEye007-win64
S=build/win/$NAME
rm -rf "$S" build/$NAME.zip
mkdir -p "$S/config/gecko" "$S/fonts" "$S/game" "$S/licenses"
cp build/out-win/wiiboot.exe "$S/GoldenEye.exe"
build/deps/llvm-mingw/bin/x86_64-w64-mingw32-strip "$S/GoldenEye.exe"
cp build/deps/sdl3-win/bin/SDL3.dll "$S/"
cp config/keys.txt "$S/keys.txt"
cp config/keys.txt config/hosts.txt config/openings.txt config/cutscenes.txt build/recomp/symbols.tsv "$S/config/"
cp build/patched/gecko.txt "$S/config/gecko-codes.txt"
cp config/gecko/*.txt "$S/config/gecko/"
for f in font_western.bin font_japanese.bin dsp_coef.bin; do
    [ -f "$FONTS/$f" ] && cp "$FONTS/$f" "$S/fonts/" || echo "no $f in $FONTS: the package goes without it"
done
[ -f "$FONTS/font-licenses.txt" ] && cp "$FONTS/font-licenses.txt" "$S/licenses/fonts.txt"
cp build/deps/sdl3-win/share/licenses/SDL3/LICENSE.txt "$S/licenses/SDL3.txt"
cp wiikit/LICENSE "$S/licenses/wiikit.txt" 2>/dev/null || true
cp build/deps/llvm-mingw/LICENSE.TXT "$S/licenses/llvm-mingw-runtime.txt"
cp build/deps/openssl-src/LICENSE.txt "$S/licenses/OpenSSL.txt"
# the window before the game and the disc extraction: the launcher, wiikit's disc readers, a Python to run them
mkdir -p "$S/launcher/wiikit"
cp tools/launcher.py tools/extract_disc.py "$S/launcher/"
cp wiikit/disc.py wiikit/rvz.py wiikit/aes.py "$S/launcher/wiikit/"
: > "$S/launcher/wiikit/__init__.py"
cp -r build/deps/winpy/python "$S/python"
cp "$S/python/LICENSE.txt" "$S/licenses/python.txt"
# the mods folder is there and empty, with its folder for each online gamemode (README.md, Mods):
# the mods themselves are a download of their own (tools/package_mods.sh), unzipped into this one
mkdir -p "$S/mods"
for d in conflict team_conflict golden_gun black_box goldeneye heroes license_to_kill team_license_to_kill; do
    mkdir -p "$S/mods/$d"
done
# Windows line ends for what Notepad and cmd read
for f in Play.bat README.txt; do sed 's/$/\r/' "port/win/$f" > "$S/$f"; done
printf 'The game goes here: Play.bat, then "Extract disc..." and pick your GoldenEye 007 (SJBE52) image.\r\nWhen it is done this folder holds sys\\main.dol and files\\. See README.txt.\r\n' > "$S/game/PUT YOUR DISC HERE.txt"
(cd build/win && zip -q -r -9 "../$NAME.zip" "$NAME")
echo "build/$NAME.zip: $(du -h build/$NAME.zip | cut -f1)"
