#!/usr/bin/env bash
# One-time: fetch Dear ImGui into ./imgui and create ./build.
# Mirrors POMIIGS/setup_imgui.sh. ImGui is not vendored in git (see .gitignore).
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
if [ ! -f imgui/imgui.cpp ]; then
    if [ -f ../POMIIGS/imgui/imgui.cpp ]; then
        echo "Copying Dear ImGui from ../POMIIGS/imgui..."
        cp -r ../POMIIGS/imgui imgui
    else
        echo "Cloning Dear ImGui..."
        git clone --depth 1 https://github.com/ocornut/imgui.git imgui
    fi
else
    echo "imgui/ already present."
fi
mkdir -p build
echo "Done. Next: cd build && cmake .. && make -j"
