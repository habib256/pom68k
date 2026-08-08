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
#     POM68K_NATIVE=OFF: no native ISA floor in a distributable binary.
#     POM68K_LTO=ON: LTO is NOT a portability hazard — it changes code layout,
#     not the instruction set. It used to ride on POM68K_NATIVE, which meant
#     every released artifact shipped without it; the two are separate knobs
#     since 2026-08-08. (NeoST measured LTO as roughly half of its PGO+LTO
#     gain on the same Moira loop — docs/PERFORMANCE_PI.md § 3.)
#     POM68K_TUNE on aarch64: schedule for the Cortex-A72, i.e. the Pi 4/400
#     this artifact exists for. `-mtune=` reorders, it does NOT emit A72-only
#     instructions — the AppImage still loads on a Pi 3 (A53) and a Pi 5 (A76),
#     and on every other aarch64 machine. Raising the floor would need
#     `-mcpu=`, which is what packaging/raspberry/build_native_pi.sh is for.
TUNE=""
case "$(uname -m)" in
    aarch64|arm64) TUNE="-DPOM68K_TUNE=${POM68K_TUNE:-cortex-a72}" ;;
esac
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DPOM68K_NATIVE=OFF -DPOM68K_LTO=ON -DPOM68K_TESTS=OFF ${TUNE} \
    ${POM68K_VERSION:+-DPOM68K_VERSION="${POM68K_VERSION}"} \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build-release -j"$(nproc)" --target POM68K

# --- Package the AppImage ----------------------------------------------------
#     Tools are baked in the image at POM68K_APPIMAGE_TOOLS_DIR, so nothing
#     is downloaded per release. APPIMAGE_EXTRACT_AND_RUN: no FUSE in docker.
export POM68K_BUILD_DIR=build-release
export APPIMAGE_EXTRACT_AND_RUN=1
packaging/linux/build_appimage.sh
