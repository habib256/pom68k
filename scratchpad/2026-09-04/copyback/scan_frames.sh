#!/bin/bash
# Report every built test/etalon binary whose largest stack-clash probe frame
# is >= 1 MB, worst first. A probe >= 8 MB is a guaranteed SIGSEGV at entry.
cd /home/gistarcade/src/pom68k/.claude/worktrees/agent-a360a5fb181d5ee26/build || exit 1
for f in *_test *_etalon; do
  [ -x "$f" ] || continue
  [ -f "$f" ] || continue
  m=$(objdump -d "$f" 2>/dev/null |
      grep -oE 'lea +-0x[0-9a-f]+\(%rsp\),%r11' |
      grep -oE '0x[0-9a-f]+' | sort -u |
      awk '{printf "%d\n", strtonum($1)}' | sort -n | tail -1)
  [ -z "$m" ] && continue
  [ "$m" -lt 1048576 ] && continue
  echo "$m $f"
done | sort -rn
