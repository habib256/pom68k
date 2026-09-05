#!/bin/bash
# B.2 slice 5 — the five 68030 lockstep gates, re-run with FRESH SEEDS.
#
# Registered cadence is BUDGET=8192 / FINE_AT=110000; every round below moves
# BOTH off it, because the point of a fresh seed is that the compare windows
# fall somewhere else in the guest's execution than the ones the gate was
# tuned on (the 6000-frame a64 gate exists because the 8192 cadence once hid
# a native two-memory MOVE corruption).
#
#   round A  knob ON,  seed 1 (6144 / 73331)
#   round B  knob ON,  seed 2 (12288 / 95003) + FULL_RAM_AT — the store side
#   round C  knob ON,  seed 1 + ICTRACE=1     — the fetch side
#   round D  knob OFF, seed 1                 — control
#
# vramDiff runs at every boundary unconditionally (the gate does that itself
# since B.2 slice 1), so the framebuffer store side is covered in all rounds.
set -u
W=/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915
OUT="$W/scratchpad/2026-09-04/b2s5"
LOGS="$OUT/logs"
BIN="$W/build/jit_lockstep_030_test"
cd "$W" || exit 1

# name<TAB>gate environment, transcribed from cmake/Pom68kJitGates.cmake
gates=(
  "jit_lockstep_030_test|POM68K_JIT_BACKEND=x64"
  "jit_lockstep_030_blocks_test|POM68K_JIT_BACKEND=threaded POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1"
  "jit_lockstep_030_x64_experimental_test|POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1"
  "jit_lockstep_030_x64_packed_ccr_test|POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 POM68K_JIT_PACKED_CCR=1"
  "jit_lockstep_030_x64_alignment_test|POM68K_JIT_BACKEND=x64 POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 POM68K_JIT_RESTART_BASE=1 POM68K_JIT_BSRW=1"
)

round() {                    # round <tag> <extra env...>
    local tag=$1; shift
    for g in "${gates[@]}"; do
        local name=${g%%|*} genv=${g#*|}
        local log="$LOGS/lockstep-${tag}-${name}.log"
        local t0=$SECONDS
        # shellcheck disable=SC2086
        env $genv "$@" "$BIN" 120000 >"$log" 2>&1
        local rc=$?
        local verdict; verdict=$(grep -E "^\[jit_lockstep_030\] (OK|FAIL)" "$log" | head -1)
        local dw; dw=$(grep -o 'data window: .*' "$log" | head -1)
        printf '%-14s %-42s rc=%d  %ss  %s | %s\n' \
               "$tag" "$name" "$rc" "$((SECONDS - t0))" "$verdict" "$dw"
    done
}

echo "=== round A — knob ON, fresh seed 1 (BUDGET=6144 FINE_AT=73331) ==="
round A-on-seed1 POM68K_DATA_WINDOW=1 \
      POM68K_JIT_LOCKSTEP_BUDGET=6144 POM68K_JIT_LOCKSTEP_FINE_AT=73331

echo "=== round B — knob ON, fresh seed 2 (BUDGET=12288 FINE_AT=95003) + FULL_RAM_AT=119000 ==="
round B-on-seed2 POM68K_DATA_WINDOW=1 \
      POM68K_JIT_LOCKSTEP_BUDGET=12288 POM68K_JIT_LOCKSTEP_FINE_AT=95003 \
      POM68K_JIT_LOCKSTEP_FULL_RAM_AT=119000

echo "=== round C — knob ON, seed 1, ICTRACE=1 (the fetch side) ==="
round C-on-ictrace POM68K_DATA_WINDOW=1 POM68K_JIT_LOCKSTEP_ICTRACE=1 \
      POM68K_JIT_LOCKSTEP_BUDGET=6144 POM68K_JIT_LOCKSTEP_FINE_AT=73331

echo "=== round D — knob OFF control, seed 1 ==="
round D-off-seed1 POM68K_DATA_WINDOW=0 \
      POM68K_JIT_LOCKSTEP_BUDGET=6144 POM68K_JIT_LOCKSTEP_FINE_AT=73331

echo "=== ictrace lines printed in round C (must be zero) ==="
grep -c '\[ictrace\]' "$LOGS"/lockstep-C-on-ictrace-*.log
