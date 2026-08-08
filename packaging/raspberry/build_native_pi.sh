#!/usr/bin/env bash
# =============================================================================
#  build_native_pi.sh — build POM68K NATIVELY on a Raspberry Pi.
#
#  WHY, rather than the released `POM68K-<v>-aarch64.AppImage`: that artifact
#  is built for GENERIC aarch64 — it must load on a Pi 3, a Pi 5 and any other
#  arm64 machine, so its ISA floor is plain armv8-a. (Since 2026-08-08 it is at
#  least SCHEDULED for the Cortex-A72 via `-mtune=`, and built with LTO.) Here
#  the ISA floor itself rises to the exact core with `-mcpu=`, and the binary
#  is free to be unportable.
#
#  MORE THAN -mcpu, THOUGH: `--pgo`. POM68K's hot loop is the Moira interpreter
#  — an indirect branch on the opcode followed by a great many rarely-taken
#  conditional branches. Without a profile the compiler assumes both outcomes
#  of each are equally likely; with one it lays the frequent case out in
#  sequence. Fewer taken branches, and above all a far better used instruction
#  cache — which counts double on a Cortex-A72 (32 KB of L1i, a modest
#  predictor next to a desktop x86 core).
#
#  Usage, ON the Pi, in a checkout with roms/ and hdv/ populated:
#      ./setup_imgui.sh                                   # once
#      packaging/raspberry/build_native_pi.sh             # -mcpu only
#      packaging/raspberry/build_native_pi.sh --pgo       # + PGO + LTO (best)
#      sudo packaging/raspberry/build_native_pi.sh --pgo --install
#
#      POM68K_PGO_GATES="q605 lcii classic" …  --pgo      # shorter training
#      POM68K_JOBS=2  POM68K_LTO=1  POM68K_BUILD_DIR=build-pi   # overrides
#
#  --install lays out /opt/pom68k the way POM68K's own findPath() searches
#  (cwd, then the executable's directory, then its parent):
#      /opt/pom68k/bin/POM68K      ← execDir
#      /opt/pom68k/roms/…          ← found as execDir/../roms/…
#      /opt/pom68k/hdv/…
#
#  NUMBERS. The −20 % (PGO) / −34 % (PGO+LTO) figures this recipe is modelled
#  on were measured on NeoST's Cortex-A72, on the same Moira interpreter, not
#  on POM68K. POM68K's own PGO measurement is x86-64: −33 % interpreter,
#  −18 % JIT (CMakeLists.txt, 2026-07-28). Nobody has yet published a POM68K
#  before/after on a Pi. To produce one, use the FIXED-BUDGET harness, never a
#  boot etalon (an etalon stops the moment it recognises the Finder, so two
#  builds are timed over different amounts of guest work):
#      cmake --build build-pi -j2 --target jit_bench
#      POM68K_BENCH_FRAMES=600 ./build-pi/jit_bench
#  It prints wall-clock AND a state fingerprint — the fingerprint must be
#  identical between the two builds, which is what makes the comparison a
#  measurement rather than an anecdote.
#
#  (c) 2026 VERHILLE Arnaud — POM68K, GPLv3.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PREFIX="${POM68K_PREFIX:-/opt/pom68k}"
BUILD_DIR="${POM68K_BUILD_DIR:-build-pi}"
DO_INSTALL=0
DO_PGO=0
for a in "$@"; do
    case "$a" in
        --install) DO_INSTALL=1 ;;
        --pgo)     DO_PGO=1 ;;
        *) echo "unknown option: $a  (--pgo, --install)"; exit 1 ;;
    esac
done

[ -f imgui/imgui.cpp ] || {
    echo "error: imgui/ is missing — run ./setup_imgui.sh first (the GUI target"
    echo "       is simply not defined without it, and this script builds it)."
    exit 1; }

# --- 1. Identify the core ----------------------------------------------------
# The model string is in /proc/device-tree/model ("Raspberry Pi 400 Rev 1.0").
# `-mcpu=native` alone is NOT trusted: on some 64-bit kernels the MIDR GCC
# reads is incomplete and detection quietly degrades to generic — which is the
# whole thing this script exists to avoid.
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo unknown)"
case "$MODEL" in
    *"Raspberry Pi 5"*)            MCPU=cortex-a76 ;;
    *"Raspberry Pi 4"*|*"Pi 400"*) MCPU=cortex-a72 ;;
    *"Raspberry Pi 3"*)            MCPU=cortex-a53 ;;
    *)                             MCPU=native ;;
esac
# If the compiler refuses that -mcpu (GCC too old for the core), fall back to
# generic now rather than failing twenty minutes later on some random .cpp.
if ! echo 'int main(){}' | ${CXX:-g++} -x c++ -mcpu=$MCPU -o /dev/null - 2>/dev/null; then
    echo "[pi] WARNING: -mcpu=$MCPU refused by $(${CXX:-g++} --version | head -1) → generic"
    MCPU=""
fi
ARCH_FLAGS=""
[ -n "$MCPU" ] && ARCH_FLAGS="-mcpu=$MCPU -mtune=$MCPU"

# --- 2. Size the build against the board -------------------------------------
# A Pi 4 has four cores but often 2-4 GB. Moira.cpp alone is a very large
# template instantiation; -j4 on this tree OOM-kills on a 2 GB board, and the
# symptom reads like a compiler crash ("cc1plus: fatal error: Killed signal
# terminated program"). Budget ~1.2 GB per job.
MEM_KB=$(awk '/MemTotal/{print $2; exit}' /proc/meminfo 2>/dev/null || true)
MEM_MB=$(( ${MEM_KB:-2048000} / 1024 ))   # no /proc → assume 2 GB, the cautious read
JOBS="${POM68K_JOBS:-}"
if [ -z "$JOBS" ]; then
    JOBS=$(( MEM_MB / 1200 )); [ "$JOBS" -lt 1 ] && JOBS=1
    NPROC=$(nproc); [ "$JOBS" -gt "$NPROC" ] && JOBS=$NPROC
fi

# LTO is opt-in on its own (the link takes minutes on a Pi and wants ~2 GB),
# but --pgo turns it on automatically when the board has the RAM: the two
# together are worth roughly twice what PGO is worth alone.
LTO_ON=OFF
[ "${POM68K_LTO:-0}" = "1" ] && LTO_ON=ON

echo "[pi] model : $MODEL"
echo "[pi] ram   : ${MEM_MB} MB → -j${JOBS}"
echo "[pi] flags : ${ARCH_FLAGS:-<generic>}$( [ "$LTO_ON" = ON ] && echo ' +LTO')$( [ "$DO_PGO" = 1 ] && echo ' +PGO')"

# --- 3. Configure / build ----------------------------------------------------
# POM68K_NATIVE=OFF because the -mcpu above already IS the native tuning, and
# leaving it ON would add a second, probed `-mcpu=native` on top of the
# explicitly chosen core.
configure() {                 # configure <extra-flags> <LTO ON/OFF> [extra cmake args…]
    local flags="$1" lto="$2"; shift 2
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
          -DPOM68K_NATIVE=OFF -DPOM68K_LTO="$lto" \
          -DCMAKE_C_FLAGS="$ARCH_FLAGS $flags" \
          -DCMAKE_CXX_FLAGS="$ARCH_FLAGS $flags" \
          "$@" >/dev/null
}

if [ "$DO_PGO" = "1" ]; then
    # ⚠ BOTH PASSES SHARE ONE BUILD DIRECTORY. GCC names each .gcda after the
    # ABSOLUTE path of the object it belongs to; instrumenting in one directory
    # and reading back from another finds no profile — silently, because
    # -Wno-missing-profile is on for the untrained GUI objects. The training
    # runner audits that counters actually landed and fails if they did not.
    mapfile -t GATES < <("$ROOT/tools/pgo_train_run.sh" --list)

    # Budget the wait honestly: the instrumented pass is dominated by ONE
    # translation unit. Moira.cpp under -fprofile-generate took 13 of the
    # 14m42 a 16-core x86-64 desktop needed for this step, single-threaded,
    # with every other object waiting on it — `-j` cannot help there. Expect
    # the Pi to be several times that.
    echo "[pi] PGO 1/2: instrumented build (Moira.cpp alone is a long single-threaded wait)"
    configure "" OFF -DPOM68K_PGO=generate
    cmake --build "$BUILD_DIR" -j"$JOBS" --target "${GATES[@]}"

    echo "[pi] PGO: training run (long on a Pi — narrow it with POM68K_PGO_GATES)"
    "$ROOT/tools/pgo_train_run.sh" "$BUILD_DIR"

    PGO_LTO=ON
    [ "$MEM_MB" -lt 2000 ] && { PGO_LTO=OFF; echo "[pi] < 2 GB RAM → LTO off (PGO keeps most of the gain)"; }
    echo "[pi] PGO 2/2: final build (profile$( [ "$PGO_LTO" = ON ] && echo ' + LTO'))"
    configure "" "$PGO_LTO" -DPOM68K_PGO=use
    cmake --build "$BUILD_DIR" -j"$JOBS" --target POM68K
else
    configure "" "$LTO_ON" -DPOM68K_PGO=off
    cmake --build "$BUILD_DIR" -j"$JOBS" --target POM68K
fi

echo "[pi] built: $BUILD_DIR/POM68K"

# --- 4. Install (optional) ---------------------------------------------------
if [ "$DO_INSTALL" = "1" ]; then
    [ "$(id -u)" -eq 0 ] || { echo "error: --install needs root (sudo)"; exit 1; }
    install -d "$PREFIX/bin"
    install -m 755 "$BUILD_DIR/POM68K" "$PREFIX/bin/"
    # Data: never overwrite what is already there. A ROM set and a disk image
    # the operator dropped in must survive a binary upgrade — and POM68K's
    # disk images are WRITTEN to, so clobbering one destroys a guest volume.
    for d in roms hdv disks35 assets; do
        [ -d "$ROOT/$d" ] || continue
        install -d "$PREFIX/$d"
        cp -rn "$ROOT/$d/." "$PREFIX/$d/" 2>/dev/null || true
    done
    echo "[pi] installed in $PREFIX (binary + data, existing files kept)"
fi
