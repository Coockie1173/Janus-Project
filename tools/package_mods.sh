#!/bin/sh
# The modpacks as downloads of their own: a zip for each folder of modpacks/ that has a level or
# any other .edb in it, named after the pack (modpacks/default -> build/modpacks/default.zip) with
# the pack's files at its top: its .edb files, their .png pictures and .txt files, in it and in its
# folder for each online gamemode, and its custom weapons with their sounds. A zip goes into the
# game's modpacks folder as it is, the game reads it as the pack of its name (port/mods/unzip.cpp).
# A new folder in modpacks/ is a new zip, so a new pack is released as one of its own.
#     sh tools/package_mods.sh      -> build/modpacks/NAME.zip, none for a pack without an .edb
set -e
cd "$(dirname "$0")/.."
S=build/mods-package
OUT=build/modpacks
rm -rf "$S" "$OUT" build/Janus-Project-mods.zip
mkdir -p "$S" "$OUT"
for pack in modpacks/*/; do
    [ -d "$pack" ] || continue
    NAME=$(basename "$pack")
    P="$S/$NAME"
    mkdir -p "$P"
    for f in "$pack"*.edb "$pack"*.png "$pack"*.txt; do [ -f "$f" ] && cp "$f" "$P/"; done
    for d in conflict team_conflict golden_gun black_box goldeneye heroes license_to_kill team_license_to_kill; do
        for f in "$pack$d"/*.edb "$pack$d"/*.png "$pack$d"/*.txt; do [ -f "$f" ] && { mkdir -p "$P/$d"; cp "$f" "$P/$d/"; }; done
    done
    for f in "$pack"weapons/*.edb "$pack"weapons/*.txt "$pack"weapons/*.wav; do [ -f "$f" ] && { mkdir -p "$P/weapons"; cp "$f" "$P/weapons/"; }; done
    if [ -z "$(find "$P" -name '*.edb' | head -1)" ]; then
        echo "modpack $NAME: no .edb, no zip"
        continue
    fi
    (cd "$P" && zip -q -r -9 "$OLDPWD/$OUT/$NAME.zip" .)
    echo "$OUT/$NAME.zip: $(du -h "$OUT/$NAME.zip" | cut -f1), $(cd "$P" && find . -type f | wc -l) file(s)"
done
