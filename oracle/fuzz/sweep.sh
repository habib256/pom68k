#!/usr/bin/env bash
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# One fresh-seed sweep of both generators (oracle/README.md, TODO.md
# § Moteur › Socle): every family of fuzz040.py and fuzz030.py over every
# MMU setup it accepts, against the WinUAE oracle (strict 6888x on the
# 68030), then replayed by the interpreter (--strict-ram) and by the JIT
# lockstep harness. A pinned corpus is a floor, not a coverage: run this
# with seeds no corpus uses, and read every divergence against
# oracle/fuzz/disputes/NOTES.md before calling it new.
#
# Usage: oracle/fuzz/sweep.sh SEED040 SEED030 [N-per-cell] [OUT] [BUILD]
#   SEED040/SEED030  first seed of each CPU; each cell takes the next one
#   N                vectors per cell (default 1000)
#   OUT              output directory, gigabytes at the default N (default
#                    BUILD/sweep-SEED040: git-ignored, and not a RAM tmpfs)
#   BUILD            build tree with sst68030/sst68040[_jit] (default build)
# The oracle is build-oracle/liboracle_uae.{so,dylib}, or $POM68K_ORACLE_SO.
# Exit status: 0 when nothing diverged, 1 otherwise (rulings included —
# the summary is the work list, not a verdict).

set -euo pipefail
cd "$(dirname "$0")/../.."             # project root
[ $# -ge 2 ] || { sed -n 13,21p "$0"; exit 2; }
S040=$1; S030=$2; N=${3:-1000}
# Python is already required by the generators; avoid GNU-only realpath -m
# and nproc so the same entry point works on macOS.
abspath() { python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$1"; }
BUILD=$(abspath "${5:-build}")
OUT=$(abspath "${4:-$BUILD/sweep-$S040}")
oracle=build-oracle/liboracle_uae.so
if [ ! -f "$oracle" ] && [ -f build-oracle/liboracle_uae.dylib ]; then
    oracle=build-oracle/liboracle_uae.dylib
fi
A=$(abspath "${POM68K_ORACLE_SO:-$oracle}")
JOBS=${POM68K_SWEEP_JOBS:-$(python3 -c 'import os; print(max(1, (os.cpu_count() or 1) // 2))')}
case "$JOBS" in
    *[!0-9]*|0|'') echo "POM68K_SWEEP_JOBS must be a positive integer"; exit 2;;
esac
[ -f "$A" ] || { echo "no oracle at $A — see oracle/README.md"; exit 2; }
for t in sst68030 sst68040 sst68030_jit sst68040_jit; do
    [ -x "$BUILD/$t" ] || { echo "missing $BUILD/$t"; exit 2; }
done
mkdir -p "$OUT"
cmds="$OUT/cmds.txt"; : > "$cmds"

steps_of() { case $1 in fpu2) echo "--steps 2";; fpuh) echo "--steps 3";; esac; }
s=$S040
for f in core mmu random fault fpu fline fpuraw fpu2 sysraw rmw fpuh pagend earaw; do
    for m in off identity tt ttvar; do
        case $f in fault|rmw) [ $m = off ] && continue;; esac
        case $f in pagend) [ $m = identity ] || continue;; esac
        mkdir -p "$OUT/040/$f"
        echo "python3 fuzz040.py --a $A --family $f --mmu $m $(steps_of $f) --n $N --seed $s --out $OUT/040/$f/${f}_${m}_$s.json" >> "$cmds"
        s=$((s+1))
    done
done
s=$S030
for f in core mmu random fault fpu fpuraw fpu2 rmw fpuh pagend earaw; do
    for m in off identity tt; do
        case $f in fault|rmw) [ $m = off ] && continue;; esac
        case $f in pagend) [ $m = identity ] || continue;; esac
        mkdir -p "$OUT/030/$f"
        echo "python3 fuzz030.py --a $A --family $f --mmu $m --strict-fpu $(steps_of $f) --n $N --seed $s --out $OUT/030/$f/${f}_${m}_$s.json" >> "$cmds"
        s=$((s+1))
    done
done

# Append the second 030 geometry after legacy cells to preserve their seeds.
for f in core mmu random fault fpu fpuraw fpu2 rmw fpuh pagend earaw; do
    mkdir -p "$OUT/030/$f"
    echo "python3 fuzz030.py --a $A --family $f --mmu identity --geometry 4k-2level --strict-fpu $(steps_of $f) --n $N --seed $s --out $OUT/030/$f/${f}_4k_2level_$s.json" >> "$cmds"
    s=$((s+1))
done

# Long descriptors at both levels, appended without renumbering prior cells.
for f in core mmu random fault fpu fpuraw fpu2 rmw fpuh pagend earaw; do
    mkdir -p "$OUT/030/$f"
    echo "python3 fuzz030.py --a $A --family $f --mmu identity --geometry 4k-2level-long --strict-fpu $(steps_of $f) --n $N --seed $s --out $OUT/030/$f/${f}_4k_2level_long_$s.json" >> "$cmds"
    s=$((s+1))
done

# Upper/lower table limits, plus probes immediately around each boundary.
for f in core mmu random fault fpu fpuraw fpu2 rmw fpuh pagend earaw limits; do
    mkdir -p "$OUT/030/$f"
    echo "python3 fuzz030.py --a $A --family $f --mmu identity --geometry 4k-limits --strict-fpu $(steps_of $f) --n $N --seed $s --out $OUT/030/$f/${f}_4k_limits_$s.json" >> "$cmds"
    s=$((s+1))
done

echo "generating $(wc -l < "$cmds") cells into $OUT"
(cd oracle/fuzz && xargs -P "$JOBS" -I{} sh -c '{} > /dev/null' < "$cmds") \
    2> "$OUT/gen.err" || { echo "generation failed: $OUT/gen.err"; exit 2; }

status=0
summary="$OUT/summary.txt"; : > "$summary"
replay() {   # cpu family-dir harness-flags...
    local cpu=$1 dir=$2; shift 2
    local log="$OUT/replay_${cpu}_$(basename "$dir")"
    "$BUILD/sst$cpu" "$dir" --strict-ram --examples 200 "$@" > "$log.txt" 2>&1 || status=1
    # Short vectors must reach compilation even on A64/030 (default score 64).
    POM68K_JIT_PROFIT_SCORE=0 "$BUILD/sst${cpu}_jit" "$dir" --examples 50 "$@" > "$log.jit.txt" 2>&1 || status=1
    grep -hE '\] (OK|FAIL):' "$log.txt" | sed "s|^|$cpu $(basename "$dir") interp: |" >> "$summary"
    grep -hE '\] (OK|FAIL):' "$log.jit.txt" | sed "s|^|$cpu $(basename "$dir") jit:    |" >> "$summary"
}
for d in "$OUT"/040/*; do
    case $(basename "$d") in fpu|fpuraw|fpu2|fpuh) replay 68040 "$d" --fpu;;
                             *) replay 68040 "$d";; esac
done
for d in "$OUT"/030/*; do replay 68030 "$d"; done
cat "$summary"
echo "divergences, one per line: grep -h '^ *x' $OUT/replay_*.txt"
exit $status
