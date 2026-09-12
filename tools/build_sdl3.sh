#!/bin/sh
# Build SDL3 from source into DIR/sdl3 (for systems without SDL3 dev files).
#     sh tools/build_sdl3.sh build/deps
set -e
DIR=${1:-build/deps}
mkdir -p "$DIR"
cd "$DIR"
[ -d SDL3-src ] || git clone -q --depth 1 --branch release-3.4.x https://github.com/libsdl-org/SDL SDL3-src \
                || git clone -q --depth 1 https://github.com/libsdl-org/SDL SDL3-src
cmake -S SDL3-src -B SDL3-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$PWD/sdl3" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF > sdl-config.log 2>&1
ninja -C SDL3-build install > sdl-build.log 2>&1
echo "SDL3 installed in $PWD/sdl3"
