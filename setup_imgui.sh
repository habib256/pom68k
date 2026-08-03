#!/usr/bin/env bash
# One-time: fetch Dear ImGui into ./imgui and create ./build.
# NOTE: the GUI needs the DOCKING branch (src/DockLayout.* uses
# ImGuiConfigFlags_DockingEnable + the DockBuilder API). master has
# neither, so a master checkout will not compile.
# Mirrors POMIIGS/setup_imgui.sh. ImGui is not vendored in git (see .gitignore).
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
if [ ! -f imgui/imgui.cpp ]; then
    if [ -f ../POMIIGS/imgui/imgui.cpp ]; then
        echo "Copying Dear ImGui from ../POMIIGS/imgui..."
        cp -r ../POMIIGS/imgui imgui
    else
        echo "Cloning Dear ImGui (docking branch)..."
        git clone --depth 1 -b docking https://github.com/ocornut/imgui.git imgui
    fi
else
    echo "imgui/ already present."
fi
# A tree copied from POMIIGS (or an older POM68K) sits on master; move it
# to docking in place rather than making the user guess at a build error.
if ! grep -q ImGuiConfigFlags_DockingEnable imgui/imgui.h; then
    echo "imgui/ is not the docking branch - switching..."
    git -C imgui fetch --depth 1 origin docking
    git -C imgui checkout -B docking FETCH_HEAD
fi
mkdir -p build
echo "Done. Next: cd build && cmake .. && make -j"
