#!/usr/bin/env bash
# Build POM68K's one third-party library — GLFW — as a UNIVERSAL 2 static
# archive (arm64 + x86_64), so the packaged .app contains no dylib to
# resolve at runtime at all.
#
# Why not `brew install glfw` (the POM1 lesson, packaging/macos/
# build_universal_deps.sh there): Homebrew is single-arch, and the linker
# bakes brew's ABSOLUTE prefix into the binary — /usr/local/opt/... on
# Intel, /opt/homebrew/opt/... on Apple Silicon — so a brew-linked .dmg
# dies at dyld on every Mac whose brew lives at the other prefix. A static
# universal GLFW removes the failure mode instead of patching it.
#
# Usage: packaging/macos/build_universal_deps.sh --out <dir>
# Result: <dir>/glfw  — a CMAKE_PREFIX_PATH root with lib/libglfw3.a
#         (both slices) + lib/cmake/glfw3 config files.

set -euo pipefail

GLFW_VER=3.3.10
GLFW_SHA256=4ff18a3377da465386374d8127e7b7349b685288cb8e17122f7e1179f73769d5

OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done
[ -n "${OUT}" ] || { echo "usage: $0 --out <dir>" >&2; exit 1; }
mkdir -p "${OUT}"
OUT="$(cd "${OUT}" && pwd)"

WORK="${OUT}/glfw-src"
mkdir -p "${WORK}"
cd "${WORK}"

if [ ! -d "glfw-${GLFW_VER}" ]; then
    curl -fsSL -o glfw.tar.gz \
        "https://github.com/glfw/glfw/archive/refs/tags/${GLFW_VER}.tar.gz"
    echo "${GLFW_SHA256}  glfw.tar.gz" | shasum -a 256 -c -
    tar -xzf glfw.tar.gz
fi

cmake -S "glfw-${GLFW_VER}" -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX="${OUT}/glfw"
cmake --build build -j"$(sysctl -n hw.ncpu)" --target install

lipo -info "${OUT}/glfw/lib/libglfw3.a"
for a in arm64 x86_64; do
    lipo -info "${OUT}/glfw/lib/libglfw3.a" | grep -qw "$a" \
        || { echo "ERROR: libglfw3.a is missing the $a slice" >&2; exit 1; }
done
echo "OK: universal static GLFW at ${OUT}/glfw"
