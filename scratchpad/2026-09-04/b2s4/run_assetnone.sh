#!/bin/bash
# B.2 slice 4 — the asset-free tier in this worktree's build.
# Output goes to the SHARED scratchpad so it survives worktree integration.
set -u
B=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6/build
S=/home/gistarcade/src/pom68k/scratchpad/2026-09-04/b2s4
ctest --test-dir "$B" -L asset-none --output-on-failure > "$S/ctest-asset-none.log" 2>&1
echo "ctest exit=$?" >> "$S/ctest-asset-none.log"
cp "$S/ctest-asset-none.log" \
   /home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6/scratchpad/2026-09-04/b2s4/
