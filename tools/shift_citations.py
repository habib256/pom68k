#!/usr/bin/env python3
"""Move every `file:line[-line]` citation by the exact line delta git records.

`tools/refresh_citations.py` repairs a citation whose ANCHOR moved: it needs
an identifier the sentence names, and it relocates the range onto that
identifier's nearest occurrence. Two cases escape it. A citation whose
sentence names nothing checkable stays where it was and silently points at
other code after an edit above it; and an anchored one can be dragged to the
wrong occurrence (on 2026-11-06 it retargeted `jit_classic_boot_etalon`'s
citation onto `lcii_boot_etalon`, which the sentence also named).

This tool needs no anchor. For every source file changed between BASE and the
working tree it reads the hunks of `git diff -U0` and maps each cited line
through them, so a citation follows its code exactly — anchored or not. A
line inside a rewritten hunk keeps its offset within the hunk.

    tools/shift_citations.py <base>           # rewrite the docs in place
    tools/shift_citations.py <base> --check   # list what would move, exit 1 if any

BASE is the last commit at which the citations were right — normally the
commit before the edit. Run it, read the diff, then let `docs_test` § 10 and
`tools/refresh_citations.py` confirm that nothing is left drifted.
"""
import glob
import os
import re
import subprocess
import sys

DOCS = ["CLAUDE.md", "README.md", "DEV.md", "TODO.md", "src/jit/POM68K_JIT.md",
        "extern/moira/POM68K_VENDOR.md"]
ROOTS = ["", "src/", "src/jit/", "src/jit/backends/", "tests/", "tools/",
         "oracle/", "extern/moira/Moira/", ".github/workflows/"]
SOURCE = r"\.(?:h|cpp|cmake|py|sh|yml|txt|inl)$"
CITATION = re.compile(
    r"((?:[A-Za-z0-9_./-]+/)?)([A-Za-z0-9_]+\.(?:h|cpp|cmake|py|sh|yml|txt|inl))"
    r":(\d+)(-\d+)?")
HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", re.M)


def line_map(base, path):
    diff = subprocess.run(["git", "diff", "-U0", base, "--", path],
                          capture_output=True, text=True, check=True).stdout
    hunks = [(int(m[1]), int(m[2] or 1), int(m[3]), int(m[4] or 1))
             for m in HUNK.finditer(diff)]

    def moved(line):
        delta = 0
        for old, old_len, new, new_len in hunks:
            if old_len == 0:                    # pure insertion after `old`
                if line > old:
                    delta += new_len
            elif line > old + old_len - 1:
                delta += new_len - old_len
            elif line >= old:                   # inside a rewritten range
                return new + min(line - old, max(new_len - 1, 0))
        return line + delta
    return moved


def main():
    args = [a for a in sys.argv[1:] if a != "--check"]
    check = "--check" in sys.argv[1:]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    base = args[0]
    changed = subprocess.run(["git", "diff", "--name-only", base],
                             capture_output=True, text=True, check=True).stdout.split()
    maps = {}
    for path in changed:
        if re.search(SOURCE, path):
            maps.setdefault(os.path.basename(path), []).append(
                (path, line_map(base, path)))

    total = 0
    for doc in DOCS + sorted(glob.glob("docs/*.md")):
        if not os.path.exists(doc):
            continue
        text = open(doc, encoding="utf-8").read()
        moves = []

        def fix(match):
            prefix, name, first, last = match[1], match[2], int(match[3]), match[4]
            candidates = maps.get(name)
            if not candidates:
                return match[0]
            cited = prefix + name
            target = None
            for path, moved in candidates:      # resolve like docs_test § 10
                if any(path == root + cited for root in ROOTS):
                    target = moved
                    break
            if target is None:
                if len(candidates) != 1:
                    return match[0]
                target = candidates[0][1]
            new_first = target(first)
            new_last = ("-" + str(target(int(last[1:])))) if last else ""
            out = f"{cited}:{new_first}{new_last}"
            if out != match[0]:
                moves.append((match[0], out))
            return out

        new_text = CITATION.sub(fix, text)
        for old, new in moves:
            print(f"{doc}: `{old}` -> `{new}`")
        total += len(moves)
        if moves and not check:
            open(doc, "w", encoding="utf-8").write(new_text)
    print(f"citations moved: {total}" + ("" if not check else " (not written)"))
    return 1 if check and total else 0


if __name__ == "__main__":
    sys.exit(main())
