#!/bin/bash
# B.2 slice 4 — the 030 lockstep at cadences the registered gates do NOT use.
# The 8192/110000 pair is one seed; a fold that is right only at that seed is
# not right. The 260480 real-frame cadence is included because the 6000-frame
# a64 gate exists precisely to catch what the 8192 cadence once hid.
set -u
cd /home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6 || exit 1
OUT=scratchpad/2026-09-04/b2s4
BIN=./build/jit_lockstep_030_test

seed() {   # $1 = tag, $2 = steps, rest = env
  local tag=$1 steps=$2; shift 2
  env "$@" "$BIN" "$steps" > "$OUT/seed-$tag.log" 2>&1
  local rc=$?
  printf '%-28s exit=%d  %s | ictrace lines=%s\n' "$tag" "$rc" \
    "$(grep -E 'OK —|MISMATCH|DIVERG' "$OUT/seed-$tag.log" | head -1)" \
    "$(grep -c '\[ictrace\]' "$OUT/seed-$tag.log")"
}

seed x64-b4093-f57000 120000 POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 \
     POM68K_JIT_LOCKSTEP_BUDGET=4093 POM68K_JIT_LOCKSTEP_FINE_AT=57000 POM68K_JIT_LOCKSTEP_ICTRACE=1
seed x64-b1021-nofine 120000 POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 \
     POM68K_JIT_LOCKSTEP_BUDGET=1021 POM68K_JIT_LOCKSTEP_ICTRACE=1
seed x64-b260480-frame 6000 POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 \
     POM68K_JIT_LOCKSTEP_BUDGET=260480 POM68K_JIT_LOCKSTEP_ICTRACE=1
seed align-b260480-frame 6000 POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 \
     POM68K_JIT_RESTART_BASE=1 POM68K_JIT_BSRW=1 \
     POM68K_JIT_LOCKSTEP_BUDGET=260480 POM68K_JIT_LOCKSTEP_ICTRACE=1
seed threaded-b3079-f91000 120000 POM68K_JIT_BACKEND=threaded POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 \
     POM68K_JIT_LOCKSTEP_BUDGET=3079 POM68K_JIT_LOCKSTEP_FINE_AT=91000 POM68K_JIT_LOCKSTEP_ICTRACE=1
