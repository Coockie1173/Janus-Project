#!/bin/sh
# llvm-mingw (clang for Windows targets, run on Linux) into DIR/llvm-mingw: what make win builds with.
#     sh tools/get_mingw.sh build/deps
set -e
DIR=${1:-build/deps}
VER=20260922
mkdir -p "$DIR/llvm-mingw"
[ -x "$DIR/llvm-mingw/bin/x86_64-w64-mingw32-clang++" ] || {
    curl -sL -o "$DIR/llvm-mingw.tar.xz" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/$VER/llvm-mingw-$VER-ucrt-ubuntu-22.04-x86_64.tar.xz"
    tar -xf "$DIR/llvm-mingw.tar.xz" -C "$DIR/llvm-mingw" --strip-components=1
    rm "$DIR/llvm-mingw.tar.xz"
}
echo "llvm-mingw in $DIR/llvm-mingw"
