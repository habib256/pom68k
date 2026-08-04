#!/usr/bin/env bash
# One-time: fetch the pinned Dear ImGui docking release into ./imgui and
# create ./build. Override IMGUI_REF only when deliberately upgrading.
# NOTE: the GUI needs a DOCKING release (src/DockLayout.* uses
# ImGuiConfigFlags_DockingEnable + the DockBuilder API). master has
# neither, so a master checkout will not compile.
# Mirrors POMIIGS/setup_imgui.sh. ImGui is not vendored in git (see .gitignore).
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMGUI_REF="${IMGUI_REF:-v1.92.8-docking}"
cd "$DIR"
if [ ! -f imgui/imgui.cpp ]; then
    echo "Cloning Dear ImGui ${IMGUI_REF}..."
    git clone --depth 1 --branch "$IMGUI_REF" \
        https://github.com/ocornut/imgui.git imgui
else
    echo "imgui/ already present."
fi
# Refuse an incompatible pre-existing checkout instead of mutating it behind
# the user's back. Removing imgui/ and rerunning installs the pinned release.
if ! grep -q ImGuiConfigFlags_DockingEnable imgui/imgui.h; then
    echo "error: imgui/ does not provide docking; remove it and rerun this script." >&2
    exit 1
fi
mkdir -p build
echo "Done. Next: cd build && cmake .. && make -j"
