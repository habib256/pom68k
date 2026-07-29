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
# All three steps must share one build directory or the .gcda paths do
# not line up. Bit-identical emulation is unaffected: PGO changes code
# layout, never semantics (the state fingerprints match a plain build).
set -euo pipefail
BUILD="${1:-build}"
cd "$(dirname "$0")/.."
ROOT="$PWD"

# One boot per CPU family. Missing assets soft-skip, which only costs
# coverage for that family, never correctness.
GATES=(q605_boot_etalon      # 68040 + MMU  (MEMCjr/PrimeTime)
       lcii_boot_etalon      # 68030, MMU off (V8)
       lc3_boot_etalon       # 68030, MMU on  (Sonora)
       lc_boot_etalon)       # 68020 + HMMU

echo "=== 1/3  generate ==="
cmake -S "$ROOT" -B "$BUILD" -DPOM68K_PGO=generate >/dev/null
make -C "$BUILD" -j4 "${GATES[@]}"

echo "=== 2/3  train (interpreter + JIT) ==="
for g in "${GATES[@]}"; do
    echo "--- $g (interpreter)"; (cd "$ROOT" && "$BUILD/$g" >/dev/null 2>&1 || true)
    echo "--- $g (jit)"
    (cd "$ROOT" && POM68K_CPU_ENGINE=jit "$BUILD/$g" >/dev/null 2>&1 || true)
done

echo "=== 3/3  use ==="
cmake -S "$ROOT" -B "$BUILD" -DPOM68K_PGO=use >/dev/null
make -C "$BUILD" -j4 "${GATES[@]}"
echo "done — rebuild the rest of the tree with: make -C $BUILD -j4"
