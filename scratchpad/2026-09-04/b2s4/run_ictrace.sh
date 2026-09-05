#!/bin/bash
# B.2 slice 4 — POM68K_JIT_LOCKSTEP_ICTRACE=1 over a full 120 000-comparison
# run. The trace prints ONLY when an i-cache counter delta changes, so a
# clean run prints no [ictrace] line at all.
set -u
cd /home/gistarcade/src/pom68k || exit 1
OUT=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6/scratchpad/2026-09-04/b2s4
BIN=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6/build/jit_lockstep_030_test
env POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 \
    POM68K_JIT_LOCKSTEP_BUDGET=8192 POM68K_JIT_LOCKSTEP_FINE_AT=110000 \
    POM68K_JIT_LOCKSTEP_ICTRACE=1 \
    "$BIN" 120000 > "$OUT/ictrace-x64-120k.log" 2>&1
echo "exit=$?"
echo "--- [ictrace] lines: $(grep -c '\[ictrace\]' "$OUT/ictrace-x64-120k.log") ---"
tail -12 "$OUT/ictrace-x64-120k.log"
