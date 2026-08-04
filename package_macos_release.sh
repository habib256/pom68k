#!/usr/bin/env bash
# POM68K — macOS release packager.
#
# Builds a Universal 2 (arm64 + x86_64) binary against the static universal
# GLFW from packaging/macos/build_universal_deps.sh, assembles POM68K.app by
# hand (the CMake target is a plain binary on every platform), and wraps it
# in a DMG with a drag-to-/Applications shortcut.
#
# The bundle's CFBundleExecutable is a small launcher script that provisions
# ~/Library/Application Support/POM68K/ (roms/, hdv/, disks35/ — ROMs are
# user-provided, never shipped) and chdirs there before exec'ing the real
# binary: POM68K resolves its assets relative to the CWD.
#
# Output: dist/POM68K-macOS-v<VERSION>.dmg + dist/POM68K.app (staging)
#
# Env: POM68K_VERSION (else the VERSION file), POM68K_MACOS_ARCHS
# ("arm64;x86_64" in the release workflow; unset = native-only dev build),
# CMAKE_PREFIX_PATH (points at the universal GLFW), IMGUI_TAG.

set -euo pipefail
cd "$(dirname "$0")"

VERSION="${POM68K_VERSION:-$(head -1 VERSION)}"
STAGING="dist/POM68K.app"
DMG_STAGE="dist/dmg-staging"
DMGPATH="dist/POM68K-macOS-v${VERSION}.dmg"

echo "============================================"
echo " POM68K — macOS distribution package (v${VERSION})"
echo "============================================"

# ---------- 1. Dear ImGui (pinned, same tag as setup_imgui.sh) ---------------
if [ ! -f imgui/imgui.cpp ]; then
    git clone --depth 1 --branch "${IMGUI_TAG:-v1.92.8-docking}" \
        https://github.com/ocornut/imgui.git imgui
fi

# ---------- 2. Build the binary ----------------------------------------------
BUILD_DIR="${POM68K_BUILD_DIR:-build-release}"
CMAKE_ARCH_ARG=()
[ -n "${POM68K_MACOS_ARCHS:-}" ] && \
    CMAKE_ARCH_ARG=(-DCMAKE_OSX_ARCHITECTURES="${POM68K_MACOS_ARCHS}")
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
    -DPOM68K_NATIVE=OFF -DPOM68K_TESTS=OFF \
    -DPOM68K_VERSION="${VERSION}" \
    "${CMAKE_ARCH_ARG[@]}"
cmake --build "${BUILD_DIR}" -j"$(sysctl -n hw.ncpu)" --target POM68K
[ -x "${BUILD_DIR}/POM68K" ] || { echo "ERROR: build produced no POM68K"; exit 1; }

# ---------- 3. Assemble POM68K.app -------------------------------------------
echo "==> Staging ${STAGING}"
rm -rf "${STAGING}"
mkdir -p "${STAGING}/Contents/MacOS" "${STAGING}/Contents/Resources"

cp "${BUILD_DIR}/POM68K" "${STAGING}/Contents/MacOS/POM68K-bin"

# Launcher: provision the user data dir, chdir, exec the real binary.
cat > "${STAGING}/Contents/MacOS/POM68K" <<'EOF'
#!/bin/bash
HERE="$(cd "$(dirname "$0")" && pwd)"
DATADIR="${HOME}/Library/Application Support/POM68K"
mkdir -p "${DATADIR}/roms" "${DATADIR}/hdv" "${DATADIR}/disks35"
if [ ! -f "${DATADIR}/README.txt" ]; then
    cat > "${DATADIR}/README.txt" <<'DOC'
POM68K data directory
  roms/     - your Macintosh ROM dumps (user-provided, never distributed)
  hdv/      - hard-disk images (.vhd/.dsk/.img)
  disks35/  - 3.5" floppy images (.dsk, DiskCopy 4.2)
DOC
fi
cd "${DATADIR}"
exec "${HERE}/POM68K-bin" "$@"
EOF
chmod +x "${STAGING}/Contents/MacOS/POM68K"

sed "s/@VERSION@/${VERSION}/g" packaging/macos/Info.plist.in \
    > "${STAGING}/Contents/Info.plist"

# Icon: .icns generated from the repo PNG (sips + iconutil ship with macOS).
ICONSET="dist/POM68K.iconset"
rm -rf "${ICONSET}"; mkdir -p "${ICONSET}"
for sz in 16 32 64 128 256 512; do
    sips -z ${sz} ${sz} packaging/POM68K.png \
         --out "${ICONSET}/icon_${sz}x${sz}.png" >/dev/null
done
cp "${ICONSET}/icon_512x512.png" "${ICONSET}/icon_256x256@2x.png"
iconutil -c icns "${ICONSET}" -o "${STAGING}/Contents/Resources/POM68K.icns"
rm -rf "${ICONSET}"

# ---------- 4. Verify: universal + self-contained ----------------------------
BIN="${STAGING}/Contents/MacOS/POM68K-bin"
otool -L "${BIN}"
if [ -n "${POM68K_MACOS_ARCHS:-}" ]; then
    lipo -info "${BIN}"
    for a in arm64 x86_64; do
        lipo -info "${BIN}" | grep -qw "$a" \
            || { echo "ERROR: POM68K is missing the $a slice"; exit 1; }
    done
fi
# No absolute Homebrew/MacPorts reference may survive (the POM1 dyld lesson).
LEAKED=$(otool -L "${BIN}" | tail -n +2 | awk '{print $1}' \
         | grep -E '^(/usr/local|/opt/homebrew|/opt/local)' || true)
if [ -n "${LEAKED}" ]; then
    echo "ERROR: .app references dylibs outside the bundle:"; echo "${LEAKED}"; exit 1
fi
echo "OK: bundle binary is self-contained"

# ---------- 5. DMG -----------------------------------------------------------
rm -rf "${DMG_STAGE}" "${DMGPATH}"
mkdir -p "${DMG_STAGE}"
cp -R "${STAGING}" "${DMG_STAGE}/POM68K.app"
ln -s /Applications "${DMG_STAGE}/Applications"
hdiutil create -volname "POM68K ${VERSION}" -srcfolder "${DMG_STAGE}" \
    -ov -format UDZO "${DMGPATH}"
rm -rf "${DMG_STAGE}"
echo "OK: ${DMGPATH}"
