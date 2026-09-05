#!/bin/bash
# B.2 slice 4 — fingerprint / icache-triple table across the three engines at
# two budgets, slice binary against the unmodified-HEAD binary.
# Run from the main worktree so findAsset() resolves the same ROM/disk the
# recorded reference numbers were taken with.
set -u
cd /home/gistarcade/src/pom68k || exit 1
OUT=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6/scratchpad/2026-09-04/b2s4
HEAD_BIN=/home/gistarcade/src/pom68k/build/jit_bench_lcii
SLICE_BIN=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6/build/jit_bench_lcii

run() {   # $1 = arm label, $2 = binary, $3 = frames, then env assignments
  local label=$1 bin=$2 frames=$3; shift 3
  echo "### $label frames=$frames"
  env POM68K_BENCH_FRAMES="$frames" "$@" "$bin" 2>&1 |
    grep -E '^lcii |  backend=|  icache:|  SCSI='
}

for F in 2000 6000; do
  run "HEAD   interp  " "$HEAD_BIN"  "$F" POM68K_CPU_ENGINE=interp
  run "SLICE  interp  " "$SLICE_BIN" "$F" POM68K_CPU_ENGINE=interp
  run "HEAD   threaded" "$HEAD_BIN"  "$F" POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1
  run "SLICE  threaded" "$SLICE_BIN" "$F" POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1
  run "HEAD   x64     " "$HEAD_BIN"  "$F" POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1
  run "SLICE  x64     " "$SLICE_BIN" "$F" POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1
done | tee "$OUT/fingerprint-icache-table.txt"
