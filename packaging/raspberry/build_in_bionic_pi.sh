#!/usr/bin/env bash
# =============================================================================
#  build_in_bionic_pi.sh — the Raspberry Pi PACKAGE, built in CI.
#
#  Runs INSIDE the same pinned aarch64 builder image the release uses
#  (packaging/linux/Dockerfile.bionic, glibc 2.27, g++-11, appimagetool baked),
#  on GitHub's native arm64 runner. Called by .github/workflows/pi400.yml.
#
#  WHAT MAKES IT DIFFERENT from packaging/linux/build_in_bionic.sh, which
#  produces the generic `POM68K-<v>-aarch64.AppImage`:
#
#    generic release   -DPOM68K_TUNE=cortex-a72   ISA floor stays armv8-a.
#                      Scheduled for the A72, loads on every aarch64 machine
#                      from a Pi 3 to a server. One artifact for everyone.
#
#    this one          -mcpu=$POM68K_MCPU         ISA floor RISES to that core.
#                      GCC MAY use instructions the generic build cannot.
#                      Runs on that core and its supersets only.
#
#  "MAY" is doing real work in that sentence. Measured 2026-08-08: on
#  cortex-a72 it does NOT — the two binaries are byte-identical bar the
#  build-id and the version string, because `-mcpu=X` is `-march=<X's arch>
#  -mtune=X`, the release already carries `-mtune=cortex-a72`, and the only
#  ISA delta left (crc, crypto) is code GCC never writes by itself. The build
#  below therefore MEASURES the difference every run rather than assuming it,
#  and says so in the job summary. For a Pi 4/400 the honest recommendation is
#  the release AppImage; what this workflow still gives that the release does
#  not is the tarball.
#
#  Both get LTO. Neither gets PGO, and that is not an oversight: profile
#  training needs a machine that BOOTS, POM68K ships no ROMs (user-provided,
#  never committed), so a CI runner has nothing to boot. A profile collected
#  from nothing is the silent failure docs/RASPBERRY_PI.md § 5 is about — an
#  empty profile produces a plain -O3 binary with no warning. PGO on the Pi is
#  therefore the user's own `build_native_pi.sh --pgo`, run where the assets
#  are.
#
#  TWO PACKAGES OUT OF ONE BUILD, no recompilation:
#    · .AppImage — Pi OS with a desktop: one clickable file.
#    · .tar.gz   — Pi OS Lite / a kiosk: Raspberry Pi OS bookworm does not
#                  install libfuse2, which a type-2 AppImage needs to mount
#                  itself. Unpacked, there is nothing to mount.
#
#  Env: POM68K_MCPU (required, e.g. cortex-a72), POM68K_VERSION, IMGUI_TAG.
#
#  (c) 2026 VERHILLE Arnaud — POM68K, GPLv3.
# =============================================================================
set -euxo pipefail

MCPU="${POM68K_MCPU:?POM68K_MCPU is required (e.g. cortex-a72)}"

# The bind-mounted repo is owned by the host uid, not root — let git touch it.
git config --global --add safe.directory '*'

# --- Dear ImGui (pinned; matches setup_imgui.sh and every other job) ---------
rm -rf imgui
git clone --depth 1 --branch "${IMGUI_TAG:-v1.92.8-docking}" \
    https://github.com/ocornut/imgui.git imgui

# --- Refuse to degrade silently ----------------------------------------------
# build_native_pi.sh falls back to generic when the compiler rejects -mcpu,
# because there is a human at the keyboard who would rather have a binary. In
# CI the opposite is right: an artifact named "pi400" that is secretly generic
# is worse than no artifact. Probe first, fail here.
echo 'int main(){}' | g++ -x c++ -mcpu="${MCPU}" -o /dev/null - \
    || { echo "ERROR: -mcpu=${MCPU} rejected by $(g++ --version | head -1)"; exit 1; }

# --- Build --------------------------------------------------------------------
#   POM68K_NATIVE=OFF: -mcpu is passed explicitly below; letting NATIVE probe
#   `-mcpu=native` on top would target the RUNNER's core (Neoverse), not the Pi's.
#   The flags go through CMAKE_*_FLAGS rather than a cache option on purpose:
#   POM68K_TUNE only emits -mtune=, which by design does NOT raise the floor,
#   and that is the one thing this package exists to do.
rm -rf build-pi400
cmake -S . -B build-pi400 -DCMAKE_BUILD_TYPE=Release \
    -DPOM68K_NATIVE=OFF -DPOM68K_LTO=ON -DPOM68K_TESTS=OFF \
    -DCMAKE_C_FLAGS="-mcpu=${MCPU} -mtune=${MCPU}" \
    -DCMAKE_CXX_FLAGS="-mcpu=${MCPU} -mtune=${MCPU}" \
    ${POM68K_VERSION:+-DPOM68K_VERSION="${POM68K_VERSION}"} \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"

# Evidence for the workflow's verification step: what a compile line ACTUALLY
# carried, not what we asked for. CMake would have dropped an unknown flag.
grep -q -- "-mcpu=${MCPU}" build-pi400/CMakeFiles/pom68k_core.dir/flags.make \
    || { echo "ERROR: -mcpu=${MCPU} is absent from the compile flags"; exit 1; }

cmake --build build-pi400 -j"$(nproc)" --target POM68K

# --- Does the raised floor change ANYTHING? Measure, do not assert ------------
# Measured 2026-08-08 on cortex-a72: it does not. `-mcpu=X` is `-march=<X's
# arch> -mtune=X`, and the generic release build already carries
# `-mtune=cortex-a72`; the only ISA delta left is crc+crypto, which GCC never
# generates on its own for this code. The two binaries came out byte-identical
# apart from the build-id and the version string.
#
# So this step builds the RELEASE configuration too and diffs it. It is the
# difference between shipping a package and shipping a claim: for a core whose
# extra instructions nothing uses, the honest answer is "identical, use the
# release AppImage", and for a Pi 5's cortex-a76 (armv8.2-a: LSE atomics,
# fp16, dotprod) the answer may well differ. Whoever prepares a board gets
# told which, per run, instead of inheriting today's result forever.
rm -rf build-ref
cmake -S . -B build-ref -DCMAKE_BUILD_TYPE=Release \
    -DPOM68K_NATIVE=OFF -DPOM68K_LTO=ON -DPOM68K_TESTS=OFF \
    -DPOM68K_TUNE="${MCPU}" \
    ${POM68K_VERSION:+-DPOM68K_VERSION="${POM68K_VERSION}"} \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build-ref -j"$(nproc)" --target POM68K

# The build-id is a hash of the contents and differs between any two links;
# removing it is what makes the comparison mean "the CODE is the same". Both
# builds carry the same POM68K_VERSION, so the version string cannot diverge.
objcopy --remove-section=.note.gnu.build-id build-pi400/POM68K /tmp/cmp-mcpu
objcopy --remove-section=.note.gnu.build-id build-ref/POM68K   /tmp/cmp-tune
mkdir -p dist
if cmp -s /tmp/cmp-mcpu /tmp/cmp-tune; then
    echo "none" > dist/ISA-DELTA.txt
    set +x
    echo "###############################################################"
    echo "#  -mcpu=${MCPU} produced BYTE-IDENTICAL code to the release"
    echo "#  build (-mtune=${MCPU}, generic armv8-a floor)."
    echo "#  This package is therefore no faster than the release"
    echo "#  AppImage. Its remaining reason to exist is the .tar.gz,"
    echo "#  which needs no libfuse2 — see docs/RASPBERRY_PI.md."
    echo "###############################################################"
    set -x
else
    echo "differs" > dist/ISA-DELTA.txt
    echo "[pi400] -mcpu=${MCPU} changed the generated code vs -mtune=${MCPU}"
fi

# --- Package 1: the AppImage --------------------------------------------------
export POM68K_BUILD_DIR=build-pi400
export POM68K_PKG_TAG=pi400
export APPIMAGE_EXTRACT_AND_RUN=1        # no FUSE inside docker
packaging/linux/build_appimage.sh

# --- Package 2: the tarball (no FUSE needed) ----------------------------------
# Reuses the AppDir build_appimage.sh just staged — same binary, same bundled
# glfw/libX* from linuxdeploy, so the two packages cannot drift apart.
VERSION="${POM68K_VERSION:-$(head -1 VERSION)}"
STAGE="build-appimage/tarball/pom68k-pi400"
rm -rf "build-appimage/tarball"
mkdir -p "${STAGE}"
cp -a build-appimage/AppDir/usr "${STAGE}/usr"
# The SAME launcher the AppImage uses. Unpacked, $APPIMAGE is unset and its
# third candidate — its own directory — is the tarball root, i.e. exactly
# where roms/ goes. See packaging/linux/AppRun.
install -m 755 packaging/linux/AppRun "${STAGE}/POM68K"
mkdir -p "${STAGE}/roms" "${STAGE}/hdv" "${STAGE}/disks35"
cat > "${STAGE}/README.txt" <<EOF
POM68K ${VERSION} — Raspberry Pi package (-mcpu=${MCPU})

Built for the ${MCPU} core: this binary uses instructions a generic aarch64
build cannot, and will NOT run on a lesser core. For a Pi 3, or for anything
else aarch64, use the generic POM68K-<version>-aarch64.AppImage instead.

  ./POM68K                 launch (roms/, hdv/, disks35/ are read from HERE)

  roms/      your Macintosh ROM dumps — user-provided, never distributed.
             The machine profile is picked by ROM size + checksum.
  hdv/       hard-disk images (.vhd/.dsk/.img, raw HFS or DDM-partitioned)
  disks35/   3.5" floppy images (.dsk 400K/800K/1440K, DiskCopy 4.2)

Everything the guest writes — PRAM, save states, screenshots — stays in this
directory too. Move the whole folder and the installation moves with it.
POM68K_DATA_DIR=<path> overrides the choice; the directory actually used is
printed at every launch.

This is the FUSE-free package: Raspberry Pi OS bookworm does not install
libfuse2, which a .AppImage needs in order to mount itself.

Faster still, if you have the assets and an afternoon: build on the Pi with
profile-guided optimization — packaging/raspberry/build_native_pi.sh --pgo.
See docs/RASPBERRY_PI.md.
EOF
mkdir -p dist
tar -czf "dist/pom68k-${VERSION}-pi400-aarch64.tar.gz" \
    -C "build-appimage/tarball" pom68k-pi400
echo "[pi400] wrote dist/pom68k-${VERSION}-pi400-aarch64.tar.gz"
ls -la dist/
