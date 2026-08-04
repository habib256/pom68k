#!/usr/bin/env bash
# build_in_bionic.sh — the Linux release: COMPILE + PACKAGE only.
#
# Runs INSIDE the pinned builder image (packaging/linux/Dockerfile.bionic,
# glibc 2.27, g++-11, CMake, GLFW 3.3, appimagetool/linuxdeploy baked) with
# the repo bind-mounted at /work. Same image recipe serves x86_64 and
# aarch64 — the release workflow runs it on the matching native runner.
#
# WHY `docker run` AND NOT a `container:` KEY: GitHub's node24 actions need
# glibc >= 2.28 — one notch above bionic's 2.27 — so they cannot execute
# inside a bionic container. The actions run on the host; this script runs
# in the container.
#
# Honoured env: POM68K_VERSION, IMGUI_TAG.
set -euxo pipefail

# The bind-mounted repo is owned by the host uid, not root — let git touch it.
git config --global --add safe.directory '*'

# --- Dear ImGui (pinned; matches setup_imgui.sh and the macOS job) -----------
rm -rf imgui
git clone --depth 1 --branch "${IMGUI_TAG:-v1.92.8-docking}" \
    https://github.com/ocornut/imgui.git imgui

# --- Build POM68K ------------------------------------------------------------
#     POM68K_NATIVE=OFF: no -march=native in a distributable binary.
#     -static-libstdc++/-static-libgcc: g++-11's newer GLIBCXX/libgcc symbols
#     must not raise the floor above bionic's glibc 2.27 — with them static,
#     the ONLY libc-family floor the AppImage imposes is glibc itself.
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DPOM68K_NATIVE=OFF -DPOM68K_TESTS=OFF \
    ${POM68K_VERSION:+-DPOM68K_VERSION="${POM68K_VERSION}"} \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build-release -j"$(nproc)" --target POM68K

# --- Package the AppImage ----------------------------------------------------
#     Tools are baked in the image at POM68K_APPIMAGE_TOOLS_DIR, so nothing
#     is downloaded per release. APPIMAGE_EXTRACT_AND_RUN: no FUSE in docker.
export POM68K_BUILD_DIR=build-release
export APPIMAGE_EXTRACT_AND_RUN=1
packaging/linux/build_appimage.sh
