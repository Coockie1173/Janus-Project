#!/bin/sh
# Dear ImGui (MIT) for the debug overlay, into DIR/imgui: sources only, compiled with the port.
#     sh tools/get_imgui.sh build/deps
set -e
DIR=${1:-build/deps}
mkdir -p "$DIR"
[ -d "$DIR/imgui" ] || git clone -q --depth 1 --branch v1.91.9b https://github.com/ocornut/imgui "$DIR/imgui"
echo "Dear ImGui in $DIR/imgui"
