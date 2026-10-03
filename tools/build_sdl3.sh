#!/bin/sh
# Build SDL3 from source into DIR/sdl3 (for systems without SDL3 dev files).
#     sh tools/build_sdl3.sh build/deps
#     sh tools/build_sdl3.sh build/deps win     for Windows, into DIR/sdl3-win (tools/mingw.cmake)
set -e
DIR=${1:-build/deps}
TOOLS=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$DIR"
cd "$DIR"
[ -d SDL3-src ] || git clone -q --depth 1 --branch release-3.4.x https://github.com/libsdl-org/SDL SDL3-src \
                || git clone -q --depth 1 https://github.com/libsdl-org/SDL SDL3-src
if [ "$2" = win ]; then
    cmake -S SDL3-src -B SDL3-build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLS/mingw.cmake" -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$PWD/sdl3-win" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF > sdl-win-config.log 2>&1
    ninja -C SDL3-build-win install > sdl-win-build.log 2>&1
    echo "SDL3 for Windows installed in $PWD/sdl3-win"
    exit 0
fi
cmake -S SDL3-src -B SDL3-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$PWD/sdl3" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF > sdl-config.log 2>&1
ninja -C SDL3-build install > sdl-build.log 2>&1
echo "SDL3 installed in $PWD/sdl3"
