#!/usr/bin/env bash
# POM68K — profile-guided optimization, build and train on THIS machine.
#
#   tools/pgo_train.sh [build-dir]
#
# Three steps sharing ONE build directory. GCC writes .gcda beside its
# objects; Clang/AppleClang writes .profraw files, which this helper merges
# with llvm-profdata before configuring the use pass. Bit-identical emulation
# is unaffected: PGO changes code layout, never semantics.
#
# The training load itself lives in tools/pgo_train_run.sh — the same one the
# Raspberry Pi recipe uses (packaging/raspberry/build_native_pi.sh --pgo), so
# a desktop profile and a Pi profile cover the same ground.
#
# For a Pi, prefer the Pi script: it also picks the right -mcpu and sizes the
# job count against the board's RAM.
set -euo pipefail
BUILD="${1:-build}"
cd "$(dirname "$0")/.."
ROOT="$PWD"
case "$BUILD" in
    /*) ;;
    *) BUILD="$ROOT/$BUILD" ;;
esac

mapfile -t GATES < <("$ROOT/tools/pgo_train_run.sh" --list)

echo "=== 1/3  generate ==="
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DPOM68K_NATIVE=ON -DPOM68K_PGO=generate >/dev/null
cmake --build "$BUILD" -j4 --target "${GATES[@]}"

# A fresh directory prevents an old run from biasing a new LLVM profile.
# GCC ignores LLVM_PROFILE_FILE and continues to update its object-local
# .gcda counters, so the same training loop serves both compiler families.
PROFILE_RUN="$(mktemp -d "$BUILD/pgo-raw.XXXXXX")"

echo "=== 2/3  train (interpreter + JIT) ==="
LLVM_PROFILE_FILE="$PROFILE_RUN/%m-%p.profraw" "$ROOT/tools/pgo_train_run.sh" "$BUILD"

echo "=== 3/3  use ==="
PGO_ARGS=(-DPOM68K_PGO=use)
if compgen -G "$PROFILE_RUN/*.profraw" >/dev/null; then
    if command -v llvm-profdata >/dev/null 2>&1; then
        LLVM_PROFDATA="$(command -v llvm-profdata)"
    elif command -v xcrun >/dev/null 2>&1; then
        LLVM_PROFDATA="$(xcrun --find llvm-profdata)"
    else
        echo "error: Clang profiles were produced but llvm-profdata was not found" >&2
        exit 1
    fi
    "$LLVM_PROFDATA" merge -output="$BUILD/pom68k.profdata" \
        "$PROFILE_RUN"/*.profraw
    PGO_ARGS+=(-DPOM68K_PGO_PROFILE="$BUILD/pom68k.profdata")
fi
cmake -S "$ROOT" -B "$BUILD" "${PGO_ARGS[@]}" >/dev/null
cmake --build "$BUILD" -j4 --target "${GATES[@]}"
echo "done — rebuild the rest of the tree with: cmake --build $BUILD -j4"
