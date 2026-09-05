#!/bin/bash
# B.2 slice 5 — the asset-free tier, twice: knob off then knob on.
# Sequential by construction: never two ctests in the same build dir.
set -u
W=/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915
LOGS="$W/scratchpad/2026-09-04/b2s5/logs"

echo "=== ctest -L asset-none, POM68K_DATA_WINDOW=0 ==="
POM68K_DATA_WINDOW=0 ctest --test-dir "$W/build" -L asset-none --output-on-failure \
    > "$LOGS/ctest-asset-none-off.log" 2>&1
echo "rc=$? — $(tail -3 "$LOGS/ctest-asset-none-off.log" | tr '\n' ' ')"

echo "=== ctest -L asset-none, POM68K_DATA_WINDOW=1 ==="
POM68K_DATA_WINDOW=1 ctest --test-dir "$W/build" -L asset-none --output-on-failure \
    > "$LOGS/ctest-asset-none-on.log" 2>&1
echo "rc=$? — $(tail -3 "$LOGS/ctest-asset-none-on.log" | tr '\n' ' ')"
