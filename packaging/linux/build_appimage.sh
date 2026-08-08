#!/bin/bash
# Build a POM68K AppImage for Linux — x86_64 or aarch64.
#
# The architecture comes from `uname -m`: no cross-compilation here, this
# packages what the current machine just built. Layout:
#
#   usr/bin/POM68K                          stripped binary
#   usr/lib/                                glfw + libX* deployed by
#                                           linuxdeploy (rpath $ORIGIN/../lib)
#   usr/share/applications/POM68K.desktop
#   usr/share/icons/hicolor/512x512/apps/POM68K.png
#   AppRun                                  picks the data directory and
#                                           chdirs to it: beside the .AppImage
#                                           if roms/hdv/disks35 already sit
#                                           there, else the launch directory,
#                                           else ~/.local/share/POM68K, which
#                                           it seeds. ROMs are user-provided,
#                                           never shipped.
#
# Output: dist/POM68K-<VERSION>-<arch>.AppImage
#
# Env: POM68K_VERSION (else the VERSION file), POM68K_BUILD_DIR (else build),
#      POM68K_APPIMAGE_TOOLS_DIR (baked in the release image; a local run
#      fetches into build-appimage/tools).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${REPO_ROOT}"

HOST_ARCH="$(uname -m)"
case "${HOST_ARCH}" in
    x86_64|amd64)   APPIMAGE_ARCH="x86_64"  ;;
    aarch64|arm64)  APPIMAGE_ARCH="aarch64" ;;
    *) echo "[appimage] unsupported architecture: ${HOST_ARCH}" >&2; exit 1 ;;
esac
echo "[appimage] architecture: ${HOST_ARCH} -> ${APPIMAGE_ARCH}"

VERSION="${POM68K_VERSION:-$(head -1 "${REPO_ROOT}/VERSION")}"
BUILD_DIR="${POM68K_BUILD_DIR:-build}"
DIST="${REPO_ROOT}/dist"
WORK="${REPO_ROOT}/build-appimage"
APPDIR="${WORK}/AppDir"
TOOLS="${POM68K_APPIMAGE_TOOLS_DIR:-${WORK}/tools}"

[ -x "${BUILD_DIR}/POM68K" ] || {
    echo "[appimage] ${BUILD_DIR}/POM68K missing — build it first" >&2; exit 1; }

# --- AppImage tools (baked in the release image; fetched for a local run) ----
mkdir -p "${TOOLS}"
fetch_extract() {
    local url="$1" outname="$2" appdir="${TOOLS}/${2}.AppDir"
    if [ -d "${appdir}" ]; then return 0; fi
    echo "[appimage] fetching ${outname}…"
    wget -q "${url}" -O "${TOOLS}/${outname}.AppImage"
    chmod +x "${TOOLS}/${outname}.AppImage"
    (cd "${TOOLS}" && "./${outname}.AppImage" --appimage-extract >/dev/null \
        && mv squashfs-root "${outname}.AppDir")
}
fetch_extract "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage" linuxdeploy
# appimagetool from AppImageKit (the OLD repo): ET_EXEC runtime — see the
# Dockerfile note; the new AppImage/appimagetool runtime is rejected by
# AppImageLauncher as "type -1".
fetch_extract "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${APPIMAGE_ARCH}.AppImage" appimagetool

# AppImageKit's `continuous` aarch64 runtime is ET_DYN; release 12 is the
# last ET_EXEC aarch64 runtime, pinned via --runtime-file (POM1 lesson).
RUNTIME_ARG=()
if [ "${APPIMAGE_ARCH}" = "aarch64" ]; then
    RUNTIME_FILE="${TOOLS}/runtime-aarch64-et_exec"
    if [ ! -f "${RUNTIME_FILE}" ]; then
        echo "[appimage] fetching aarch64 ET_EXEC runtime (AppImageKit 12)…"
        wget -q "https://github.com/AppImage/AppImageKit/releases/download/12/runtime-aarch64" \
             -O "${RUNTIME_FILE}"
    fi
    RUNTIME_ARG=(--runtime-file "${RUNTIME_FILE}")
fi

# --- Stage the AppDir --------------------------------------------------------
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" \
         "${APPDIR}/usr/share/applications" \
         "${APPDIR}/usr/share/icons/hicolor/512x512/apps"

install -m 755 "${BUILD_DIR}/POM68K" "${APPDIR}/usr/bin/POM68K"
strip --strip-unneeded "${APPDIR}/usr/bin/POM68K" || true
install -m 644 packaging/linux/POM68K.desktop "${APPDIR}/usr/share/applications/"
install -m 644 packaging/POM68K.png \
        "${APPDIR}/usr/share/icons/hicolor/512x512/apps/POM68K.png"
install -m 755 packaging/linux/AppRun "${APPDIR}/AppRun"

# --- linuxdeploy: bundle non-blacklisted libs (glfw, libX*) ------------------
#     glibc and the GL/graphics-driver stack stay on the HOST by AppImage
#     rule — that is exactly what keeps the glibc floor at the build image.
"${TOOLS}/linuxdeploy.AppDir/AppRun" \
    --appdir "${APPDIR}" \
    -d "${APPDIR}/usr/share/applications/POM68K.desktop" \
    -i "${APPDIR}/usr/share/icons/hicolor/512x512/apps/POM68K.png"

# linuxdeploy installs its own generic AppRun symlink when one exists —
# make sure OURS survives (the chdir-to-data-dir behaviour is load-bearing).
install -m 755 packaging/linux/AppRun "${APPDIR}/AppRun"

# --- appimagetool: wrap ------------------------------------------------------
mkdir -p "${DIST}"
OUT="${DIST}/POM68K-${VERSION}-${APPIMAGE_ARCH}.AppImage"
ARCH="${APPIMAGE_ARCH}" "${TOOLS}/appimagetool.AppDir/AppRun" \
    "${RUNTIME_ARG[@]}" "${APPDIR}" "${OUT}"
echo "[appimage] wrote ${OUT}"
