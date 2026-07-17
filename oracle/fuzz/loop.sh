#!/usr/bin/env bash
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# The Phase-2 improve loop, one turn (oracle/README.md):
#   1. build the WinUAE oracle .so
#   2. fuzz the family×mmu grid WinUAE-solo (SST030 vectors)
#   3. replay against Moira M68030 via tests/sst68030
# Failures in step 3 are the work list for Moira's 68030 exec layer;
# arbitration is manual (MC68030UM / MC68881-882UM reading; the oracle
# wins over the spec — disputes/NOTES.md, Musashi retired 2026-07-15).
#
# Usage: oracle/fuzz/loop.sh [N-per-cell] [seed]

set -euo pipefail
cd "$(dirname "$0")/../.."             # project root
N=${1:-200}
SEED=${2:-1}
BUILD=build
CORPUS=tests/data/sst68030

cmake -S oracle/uae -B "$BUILD/oracle_uae" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD/oracle_uae" -j"$(nproc)" >/dev/null
A=$(find "$BUILD/oracle_uae" -name 'liboracle_uae.so' 2>/dev/null | head -1 || true)
[ -n "$A" ] || { echo "no oracle built — see oracle/README.md"; exit 1; }
echo "oracle: $A"

mkdir -p "$CORPUS"
cd oracle/fuzz
for family in core mmu random fpu; do
    for mmu in off identity tt; do
        # mmu-family vectors with the MMU off still exercise F-line traps
        python3 fuzz030.py --a "../../$A" --family "$family" --mmu "$mmu" \
            --n "$N" --seed "$SEED" \
            --out "../../$CORPUS/${family}_${mmu}.json" || true
    done
done
# fault family: memory ops aimed at invalid/WP/remapped pages — only
# meaningful with translation on
for mmu in identity tt; do
    python3 fuzz030.py --a "../../$A" --family fault --mmu "$mmu" \
        --n "$N" --seed "$SEED" \
        --out "../../$CORPUS/fault_${mmu}.json" || true
done
cd ../..

cmake --build "$BUILD" -j"$(nproc)" --target sst68030 >/dev/null
"./$BUILD/sst68030" "$CORPUS" --examples 5
