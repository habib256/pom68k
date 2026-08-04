#!/usr/bin/env bash
# POM68K — profile-guided optimization training.
#
# The shipped recipe trained on the Quadra boot alone, so the profile
# optimized the 68040 paths and left the 030/020 machines cold: their
# hot code (mmuFetchWord, mmuTranslateAccess, the V8/Sonora decode
# cascades) never appeared in the profile. This trains on one machine
# per CPU family instead, on BOTH execution engines.
#
#   tools/pgo_train.sh [build-dir]
#
# All three steps share one build directory. GCC writes .gcda beside its
# objects. Clang/AppleClang writes .profraw files, which this helper merges
# with llvm-profdata before configuring the use pass. Bit-identical emulation
# is unaffected: PGO changes code layout, never semantics.
set -euo pipefail
BUILD="${1:-build}"
cd "$(dirname "$0")/.."
ROOT="$PWD"
case "$BUILD" in
    /*) ;;
    *) BUILD="$ROOT/$BUILD" ;;
esac

# One boot per CPU family. Missing assets soft-skip, which only costs
# coverage for that family, never correctness.
GATES=(q605_boot_etalon      # 68040 + MMU  (MEMCjr/PrimeTime)
       lcii_boot_etalon      # 68030, MMU off (V8)
       lc3_boot_etalon       # 68030, MMU on  (Sonora)
       lc_boot_etalon)       # 68020 + HMMU

echo "=== 1/3  generate ==="
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DPOM68K_NATIVE=ON -DPOM68K_PGO=generate >/dev/null
cmake --build "$BUILD" -j4 --target "${GATES[@]}"

# A fresh directory prevents an old run from biasing a new LLVM profile.
# GCC ignores LLVM_PROFILE_FILE and continues to update its object-local
# .gcda counters, so the same training loop serves both compiler families.
PROFILE_RUN="$(mktemp -d "$BUILD/pgo-raw.XXXXXX")"

echo "=== 2/3  train (interpreter + JIT) ==="
for g in "${GATES[@]}"; do
    echo "--- $g (interpreter)"
    (cd "$ROOT" && LLVM_PROFILE_FILE="$PROFILE_RUN/%m-%p.profraw" \
        "$BUILD/$g" >/dev/null 2>&1 || true)
    echo "--- $g (jit)"
    # `auto` trains the best end-to-end-validated backend for each host and
    # guest family. A native backend that is still explicit must first close
    # its boot gate before profile training can promote its hot paths.
    (cd "$ROOT" && LLVM_PROFILE_FILE="$PROFILE_RUN/%m-%p.profraw" \
        POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=auto \
        "$BUILD/$g" >/dev/null 2>&1 || true)
done

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
