#!/bin/sh
# The Ghidra project behind the names (a .gzf export in the project's folder), used headless.
#     tools/ghidra.sh import [FILE.gzf]     into build/ghidra (once, or again after a new export)
#     tools/ghidra.sh export                -> build/ghidra-export/: functions.tsv, symbols.tsv,
#                                              structs.txt, enums.txt, comments.tsv
#     tools/ghidra.sh decompile 80108A60 PlayerMove ...   the decompiler's C, by address or name
# Needs Ghidra (GHIDRA=dir; default ~/Applications/ghidra_11.4_PUBLIC) with the GameCube loader
# extension the project was made with, and a JDK.
cd "$(dirname "$0")/.." || exit 1
GHIDRA=${GHIDRA:-$HOME/Applications/ghidra_11.4_PUBLIC}
[ -x "$GHIDRA/support/analyzeHeadless" ] || { echo "no Ghidra at $GHIDRA (set GHIDRA=dir)"; exit 1; }
PROJ=$PWD/build/ghidra
run() { "$GHIDRA/support/analyzeHeadless" "$PROJ" GE "$@"; }
case $1 in
import)
    F=${2:-$(ls *.gzf 2>/dev/null | head -1)}
    [ -f "$F" ] || { echo "no .gzf file"; exit 1; }
    mkdir -p "$PROJ"
    run -import "$PWD/$F" -noanalysis -overwrite > "$PROJ/import.log" 2>&1 && grep 'REPORT: Import' "$PROJ/import.log" ;;
export)
    mkdir -p build/ghidra-export
    run -process -noanalysis -readOnly -scriptPath "$PWD/tools/ghidra" -postScript ExportAll.java "$PWD/build/ghidra-export" \
        > "$PROJ/export.log" 2>&1
    grep -E 'ExportAll.java> ' "$PROJ/export.log" | sed 's/.*ExportAll.java> //; s/ (GhidraScript).*//'
    wc -l build/ghidra-export/* ;;
decompile)
    shift
    run -process -noanalysis -readOnly -scriptPath "$PWD/tools/ghidra" -postScript Decompile.java "$@" 2> "$PROJ/decompile.log" \
        | sed -n '/^\/\/\/\/ /,$p' | grep -v '^INFO \|^WARN ' ;;
*) sed -n '2,9p' "$0" ;;
esac
