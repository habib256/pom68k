#!/bin/bash
# Point this worktree at the main tree's private ROM/disk inputs so the
# asset-required gates EXECUTE here instead of soft-skipping. Both paths are
# .gitignore'd (except the tracked roms/macplus.rom symlink, left alone).
set -u
W=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6
M=/home/gistarcade/src/pom68k
rm -f "$W/roms/roms"
ln -sfn "$M/roms/512KB ROMs" "$W/roms/512KB ROMs"
ln -sfn "$M/roms/128KB ROMs" "$W/roms/128KB ROMs"
ln -sfn "$M/roms/256KB ROMs" "$W/roms/256KB ROMs"
ln -sfn "$M/roms/1MB ROMs"   "$W/roms/1MB ROMs"
ln -sfn "$M/hdv" "$W/hdv"
ls -l "$W/roms/"
echo "--- git status ---"
git -C "$W" status --short
