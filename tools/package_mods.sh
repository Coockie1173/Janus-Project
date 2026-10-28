#!/bin/sh
# The modpacks as a download of their own: the modpacks folder as it is (each pack's .edb files,
# their .png pictures and .txt files, in it and in its folder for each online gamemode, and the
# custom weapons with their sounds), zipped. It is unzipped into the game's folder, next to the
# executable, where the packages have an empty modpacks/default.
#     sh tools/package_mods.sh      -> build/Janus-Project-mods.zip, none without a level in modpacks/
set -e
cd "$(dirname "$0")/.."
NAME=Janus-Project-mods
S=build/mods-package
rm -rf "$S" build/$NAME.zip
mkdir -p "$S/modpacks"
for pack in modpacks/*/; do
    [ -d "$pack" ] || continue
    P="$S/modpacks/$(basename "$pack")"
    mkdir -p "$P"
    for f in "$pack"*.edb "$pack"*.png "$pack"*.txt; do [ -f "$f" ] && cp "$f" "$P/"; done
    for d in conflict team_conflict golden_gun black_box goldeneye heroes license_to_kill team_license_to_kill; do
        mkdir -p "$P/$d"
        for f in "$pack$d"/*.edb "$pack$d"/*.png "$pack$d"/*.txt; do [ -f "$f" ] && cp "$f" "$P/$d/"; done
    done
    mkdir -p "$P/weapons"
    for f in "$pack"weapons/*.edb "$pack"weapons/*.txt "$pack"weapons/*.wav; do [ -f "$f" ] && cp "$f" "$P/weapons/"; done
done
if [ -z "$(find "$S/modpacks" -name '*.edb' | head -1)" ]; then
    echo "mods: no .edb in modpacks/, no $NAME.zip"
    exit 0
fi
echo "mods: $(cd "$S/modpacks" && find . -type f | sed 's|^\./||' | sort | tr '\n' ' ')"
(cd "$S" && zip -q -r -9 "../$NAME.zip" modpacks)
echo "build/$NAME.zip: $(du -h build/$NAME.zip | cut -f1)"
