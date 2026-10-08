#!/bin/sh
# The mods as a download of their own: the mods folder as it is (the .edb files, their .png
# pictures and .txt files, in it and in its folder for each online gamemode), zipped. It is
# unzipped into the game's folder, next to Play, where the packages have an empty mods folder.
#     sh tools/package_mods.sh      -> build/GoldenEye007-mods.zip, none without a level in mods/
set -e
cd "$(dirname "$0")/.."
NAME=GoldenEye007-mods
S=build/mods-package
rm -rf "$S" build/$NAME.zip
mkdir -p "$S/mods"
for f in mods/*.edb mods/*.png mods/*.txt; do [ -f "$f" ] && cp "$f" "$S/mods/"; done
for d in conflict team_conflict golden_gun black_box goldeneye heroes license_to_kill team_license_to_kill; do
    mkdir -p "$S/mods/$d"
    for f in mods/$d/*.edb mods/$d/*.png mods/$d/*.txt; do [ -f "$f" ] && cp "$f" "$S/mods/$d/"; done
done
if [ -z "$(find "$S/mods" -name '*.edb' | head -1)" ]; then
    echo "mods: no .edb in mods/, no $NAME.zip"
    exit 0
fi
echo "mods: $(cd "$S/mods" && find . -type f | sed 's|^\./||' | sort | tr '\n' ' ')"
(cd "$S" && zip -q -r -9 "../$NAME.zip" mods)
echo "build/$NAME.zip: $(du -h build/$NAME.zip | cut -f1)"
