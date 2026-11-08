#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Evidence grades in the Moira core's comments (CLAUDE.md § Evidence grades).

Every comment block in extern/moira/Moira/ that cites WinUAE — the
differential oracle, grade D — must say so at its head and name the
revision it read: `[D] … @ Hatari e77819f7`, the tree pinned under
oracle/uae/upstream. A grade-D citation without its revision designates
nothing (upstream line numbers move on every commit), and a reader porting
the core to another host must be able to tell a reading of WinUAE from a
manual (`[B]`) before deciding whether a divergence is deliberate.

  --check   the gate: every WinUAE-citing block carries `[D]` and a
            revision; every `file.c:N[-M]` it cites exists in the pinned
            tree and the range lies inside the file.
  --report  for each line citation, whether an identifier named in the
            same comment occurs near the cited lines (±25) — the way a
            citation written against another revision shows (fpp.c:1678
            for `*adp = ad;`, at 1687 in e77819f7). Advisory.
  --apply   insert `// [D] WinUAE @ Hatari e77819f7 — oracle reading,
            cited below.` at the head of every ungraded WinUAE block, at
            the block's indentation; inline (`code; // WinUAE …`) comments
            get the bracket in place. Run --report first and fix the lines
            it names: --apply stamps a revision, it does not verify one.

Gate: core_grades_test (cmake/Pom68kCpuGates.cmake).
"""
import argparse
import glob
import os
import re
import sys

REV = "Hatari e77819f7"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "extern", "moira", "Moira")
UPSTREAM = os.path.join(ROOT, "oracle", "uae", "upstream")

UAE = re.compile(r"\bWinUAE\b|\bgencpu\b|\bnewcpu\b|\bfpp\.c\b|\bcpummu(?:030)?\b")
GRADE_D = re.compile(r"\[D\]")
REVISION = re.compile(r"@\s*(?:Hatari\s+)?[0-9a-f]{7,40}\b")
CITE = re.compile(r"\b((?:gencpu|newcpu|newcpu_common|fpp|fpp_softfloat|cpummu030|cpummu|readcpu)\.c)"
                  r":(\d+)(?:-(\d+))?")
IDENT = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]{3,})\b")


def core_files():
    return sorted(glob.glob(os.path.join(CORE, "*.h")) +
                  glob.glob(os.path.join(CORE, "*.cpp")))


def comment_units(lines):
    """Yield (first, last, inline) for each comment unit: a run of `//`-only
    lines, or a single code line with a trailing `//` comment."""
    i = 0
    while i < len(lines):
        s = lines[i].lstrip()
        if s.startswith("//"):
            j = i
            while j < len(lines) and lines[j].lstrip().startswith("//"):
                j += 1
            yield i, j - 1, False
            i = j
        else:
            if "//" in lines[i] and not s.startswith("#"):
                yield i, i, True
            i += 1


def comment_text(lines, a, b, inline):
    if inline:
        return lines[a][lines[a].index("//"):]
    return "\n".join(lines[a:b + 1])


_upstream_cache = {}


def upstream(name):
    if name not in _upstream_cache:
        p = os.path.join(UPSTREAM, name)
        _upstream_cache[name] = (open(p, errors="replace").read().split("\n")
                                 if os.path.exists(p) else None)
    return _upstream_cache[name]


def check():
    problems = []
    blocks = 0
    for f in core_files():
        lines = open(f).read().split("\n")
        rel = os.path.relpath(f, ROOT)
        for a, b, inline in comment_units(lines):
            text = comment_text(lines, a, b, inline)
            if not UAE.search(text):
                continue
            blocks += 1
            if not GRADE_D.search(text):
                problems.append(f"{rel}:{a + 1}: WinUAE cited without a [D] grade")
            if not REVISION.search(text):
                problems.append(f"{rel}:{a + 1}: WinUAE cited without a revision (@ {REV})")
            for m in CITE.finditer(text):
                up = upstream(m.group(1))
                if up is None:
                    problems.append(f"{rel}:{a + 1}: {m.group(0)} — no such file in oracle/uae/upstream")
                    continue
                lo, hi = int(m.group(2)), int(m.group(3) or m.group(2))
                if not (1 <= lo <= hi <= len(up)):
                    problems.append(f"{rel}:{a + 1}: {m.group(0)} — outside {m.group(1)} "
                                    f"({len(up)} lines)")
    for p in problems:
        print("FAIL", p)
    print(f"{'ok  ' if not problems else 'FAIL'} {blocks} WinUAE-citing comment blocks in the "
          f"core, {len(problems)} problem(s)")
    return 1 if problems else 0


def report():
    bad = 0
    for f in core_files():
        lines = open(f).read().split("\n")
        rel = os.path.relpath(f, ROOT)
        for a, b, inline in comment_units(lines):
            text = comment_text(lines, a, b, inline)
            for m in CITE.finditer(text):
                up = upstream(m.group(1))
                if up is None:
                    continue
                whole = "\n".join(up)
                ids = [x for x in IDENT.findall(text)
                       if "_" in x and x in whole
                       and not x.startswith("pom") and x not in ("WinUAE", "gencpu", "newcpu")]
                lo, hi = int(m.group(2)), int(m.group(3) or m.group(2))
                window = "\n".join(up[max(0, lo - 26):min(len(up), hi + 25)])
                if ids and not any(x in window for x in ids):
                    bad += 1
                    print(f"{rel}:{a + 1}: {m.group(0)} — none of {ids[:5]} within ±25 lines")
    print(f"{bad} citation(s) whose named identifiers do not land")
    return 0


def apply():
    stamped = 0
    for f in core_files():
        lines = open(f).read().split("\n")
        out = []
        units = {a: (b, inline) for a, b, inline in comment_units(lines)}
        i = 0
        while i < len(lines):
            if i in units:
                b, inline = units[i]
                text = comment_text(lines, i, b, inline)
                if UAE.search(text) and not GRADE_D.search(text):
                    if inline:
                        k = lines[i].index("//")
                        rest = lines[i][k + 2:].lstrip()
                        out.append(f"{lines[i][:k]}// [D] @ {REV}: {rest}")
                        stamped += 1
                        i += 1
                        continue
                    indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
                    out.append(f"{indent}// [D] WinUAE @ {REV} — oracle reading, cited below.")
                    stamped += 1
                out.extend(lines[i:b + 1])
                i = b + 1
                continue
            out.append(lines[i])
            i += 1
        open(f, "w").write("\n".join(out))
    print(f"stamped {stamped} comment unit(s)")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true")
    g.add_argument("--report", action="store_true")
    g.add_argument("--apply", action="store_true")
    a = ap.parse_args()
    sys.exit(check() if a.check else report() if a.report else apply())
