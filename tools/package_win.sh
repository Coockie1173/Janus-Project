#!/bin/sh
# The Windows package: the cross-built game (make win), what it needs beside it, the
# instructions and an empty modpacks/default, zipped. Nothing of the disc goes in: game/ is left for the
# player to fill.
#     sh tools/package_win.sh [FONTS_DIR]      -> build/Janus-Project-win64.zip
# WIN_OUT and WIN_NAME pick another build and zip (make win-old-cpu, for CPUs without AVX2)
set -e
cd "$(dirname "$0")/.."
FONTS=${1:-/usr/share/dolphin-emu/Sys/GC}
NAME=${WIN_NAME:-Janus-Project-win64}
WOUT=${WIN_OUT:-build/out-win}
S=build/win/$NAME
rm -rf "$S" build/$NAME.zip
mkdir -p "$S/config/gecko" "$S/fonts" "$S/game" "$S/licenses"
cp $WOUT/wiiboot.exe "$S/Janus-Project.exe"
build/deps/llvm-mingw/bin/x86_64-w64-mingw32-strip "$S/Janus-Project.exe"
cp build/deps/sdl3-win/bin/SDL3.dll "$S/"
# TASLib only as the built library, when the private submodule was there to build it
if [ -f $WOUT/TASLib.dll ]; then
    cp $WOUT/TASLib.dll "$S/"
    build/deps/llvm-mingw/bin/x86_64-w64-mingw32-strip "$S/TASLib.dll"
fi
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
cp LICENSE "$S/licenses/Janus-Project.txt"
cp build/deps/imgui/LICENSE.txt "$S/licenses/imgui.txt"
cp port/package/licenses/Roboto.txt "$S/licenses/Roboto.txt"
cp build/deps/llvm-mingw/LICENSE.TXT "$S/licenses/llvm-mingw-runtime.txt"
cp build/deps/openssl-src/LICENSE.txt "$S/licenses/OpenSSL.txt"
cp build/deps/codecs/zstd/LICENSE "$S/licenses/zstd.txt"
cp build/deps/codecs/bzip2/LICENSE "$S/licenses/bzip2.txt"
cp build/deps/codecs/lzma/DOC/lzma-sdk.txt "$S/licenses/lzma-sdk.txt"
# the modpacks folder is there with an empty pack "default", with its folder for each online
# gamemode (README.md, Mods): the packs themselves are a download of their own
# (tools/package_mods.sh), unzipped into this one
mkdir -p "$S/modpacks/default"
for d in conflict team_conflict golden_gun black_box goldeneye heroes license_to_kill team_license_to_kill; do
    mkdir -p "$S/modpacks/default/$d"
done
# Windows line ends for what Notepad and cmd read
sed 's/$/\r/' port/package/win/README.txt > "$S/README.txt"
printf 'The game goes here: start Janus-Project.exe and pick your GoldenEye 007 (SJBE52) disc image.\r\nWhen it is done this folder holds sys\\main.dol and files\\. See README.txt.\r\n' > "$S/game/PUT YOUR DISC HERE.txt"
(cd build/win && zip -q -r -9 "../$NAME.zip" "$NAME")
echo "build/$NAME.zip: $(du -h build/$NAME.zip | cut -f1)"
