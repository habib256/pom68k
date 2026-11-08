#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Evidence grades in the Moira core's comments (CLAUDE.md § Evidence grades).

Every comment block in extern/moira/Moira/ that cites WinUAE or
SingleStepTests — grade D — must name the revision it read: WinUAE
`[D] … @ Hatari e77819f7`, the tree pinned under oracle/uae/upstream;
SingleStepTests `[D] SingleStepTests/680x0 68000/v1 @ e0d5ece…`, the
corpus pinned in tests/sst_corpora.lock. A grade-D citation without its
revision designates nothing (upstream line numbers move on every commit).
A reader porting the core must be able to tell a reading of WinUAE from a
manual (`[B]`) before deciding whether a divergence is deliberate.

  --check   the gate: every WinUAE-citing block carries `[D]` and a
            revision; every `file.c:N[-M]` it cites exists in the pinned
            tree and the range lies inside the file. Every SingleStepTests
            block has its own grade-D source/revision header; another
            oracle's revision does not qualify. Fetcher and corpus lock
            must agree with that pin.
  --report  for each line citation, whether an identifier named in the
            same comment occurs near the cited lines (±25) — the way a
            citation written against another revision shows (fpp.c:1678
            for `*adp = ad;`, at 1687 in e77819f7). Advisory.
  --apply   insert `// [D] WinUAE @ Hatari e77819f7 — oracle reading,
            cited below.` at the head of every ungraded WinUAE block, at
            the block's indentation; inline (`code; // WinUAE …`) comments
            get the bracket in place. Also insert a pinned SingleStepTests
            header on its ungraded blocks. Run --report first and fix the lines
            it names: --apply stamps a revision, it does not verify one.

Gate: core_grades_test (cmake/Pom68kCpuGates.cmake).
"""
import argparse
import glob
import os
import re
import sys

REV = "Hatari e77819f7"
SST_REV = "e0d5ece9670205cc84a0101081837deb446f86a3"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "extern", "moira", "Moira")
UPSTREAM = os.path.join(ROOT, "oracle", "uae", "upstream")

UAE = re.compile(r"\bWinUAE\b|\bgencpu\b|\bnewcpu\b|\bfpp\.c\b|\bcpummu(?:030)?\b")
GRADE_D = re.compile(r"\[D\]")
D_EVIDENCE = re.compile(r"\[D\]((?:(?!\[[A-F]\]).)*)", re.DOTALL)
REVISION = re.compile(r"@\s*(?:Hatari\s+)?e77819f7\b")
SST = re.compile(r"\bSingleStepTests\b")
SST_HEADER = re.compile(r"^\s*//\s*\[D\]\s+SingleStepTests/680x0\s+68000/v1"
                        r"\s+@\s+([0-9a-f]{7,40})\b", re.MULTILINE)
CITE = re.compile(r"\b((?:gencpu|newcpu|newcpu_common|fpp|fpp_softfloat|cpummu030|cpummu|readcpu)\.c)"
                  r":(\d+)(?:-(\d+))?")
IDENT = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]{3,})\b")
QUOTED_OR_COMMENT = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//')


def core_files():
    return sorted(glob.glob(os.path.join(CORE, "*.h")) +
                  glob.glob(os.path.join(CORE, "*.cpp")))


def comment_start(line):
    """Locate // outside the core's ordinary string/character literals."""
    for match in QUOTED_OR_COMMENT.finditer(line):
        if match.group() == "//":
            return match.start()
    return -1


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
            if comment_start(lines[i]) >= 0 and not s.startswith("#"):
                yield i, i, True
            i += 1


def comment_text(lines, a, b, inline):
    if inline:
        return lines[a][comment_start(lines[a]):]
    return "\n".join(lines[a:b + 1])


def sst_problems(text):
    if not SST.search(text):
        return []
    headers = SST_HEADER.findall(text)
    if not headers:
        return ["SingleStepTests cited without its own [D] source/revision header"]
    if any(not SST_REV.startswith(rev) for rev in headers):
        return [f"SingleStepTests revision differs from the corpus pin (@ {SST_REV})"]
    return []


def sst_pin_problems(fetcher, lock):
    problems = []
    if not re.search(r'^REF="' + SST_REV + r'"$', fetcher, re.MULTILINE):
        problems.append("SingleStepTests fetcher differs from the evidence pin")
    if not re.search(r"^#\s+" + SST_REV + r"\b", lock, re.MULTILINE):
        problems.append("SingleStepTests corpus lock differs from the evidence pin")
    return problems


def comment_problems(text):
    problems = sst_problems(text)
    if UAE.search(text):
        if not GRADE_D.search(text):
            problems.append("WinUAE cited without a [D] grade")
        if not REVISION.search(text):
            problems.append(f"WinUAE cited without its pinned revision (@ {REV})")
        elif not graded_uae(text):
            problems.append("WinUAE revision is not in its own [D] evidence")
    return problems


def graded_uae(text):
    return any(UAE.search(part) and REVISION.search(part)
               for part in D_EVIDENCE.findall(text))


_upstream_cache = {}


def upstream(name):
    if name not in _upstream_cache:
        p = os.path.join(UPSTREAM, name)
        _upstream_cache[name] = (open(p, errors="replace").read().split("\n")
                                 if os.path.exists(p) else None)
    return _upstream_cache[name]


def check():
    with open(os.path.join(ROOT, "tests", "fetch_sst_68000.sh")) as f:
        fetcher = f.read()
    with open(os.path.join(ROOT, "tests", "sst_corpora.lock")) as f:
        lock = f.read()
    problems = sst_pin_problems(fetcher, lock)
    blocks = sst_blocks = 0
    for f in core_files():
        lines = open(f).read().split("\n")
        rel = os.path.relpath(f, ROOT)
        for a, b, inline in comment_units(lines):
            text = comment_text(lines, a, b, inline)
            if SST.search(text):
                sst_blocks += 1
            problems.extend(f"{rel}:{a + 1}: {p}" for p in comment_problems(text))
            if not UAE.search(text):
                continue
            blocks += 1
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
          f"core, {sst_blocks} SingleStepTests blocks, {len(problems)} problem(s)")
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
        original = open(f).read()
        lines = original.split("\n")
        out = []
        units = {a: (b, inline) for a, b, inline in comment_units(lines)}
        i = 0
        while i < len(lines):
            if i in units:
                b, inline = units[i]
                text = comment_text(lines, i, b, inline)
                headers = []
                if SST.search(text) and not SST_HEADER.search(text):
                    headers.append(f"[D] SingleStepTests/680x0 68000/v1 @ {SST_REV}")
                # A block already carrying a grade needs a provenance
                # review, not a second stamp hiding an old/wrong revision.
                # Decide before inserting the new corpus header, so an
                # originally ungraded mixed-source block gets both heads.
                if UAE.search(text) and not GRADE_D.search(text):
                    headers.append(f"[D] WinUAE @ {REV} — oracle reading, cited below.")
                if headers:
                    stamped += len(headers)
                    if inline:
                        k = comment_start(lines[i])
                        rest = lines[i][k + 2:].lstrip()
                        out.append(f"{lines[i][:k]}// {' — '.join(headers)} — {rest}")
                        i += 1
                        continue
                    indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
                    out.extend(f"{indent}// {header}" for header in headers)
                out.extend(lines[i:b + 1])
                i = b + 1
                continue
            out.append(lines[i])
            i += 1
        updated = "\n".join(out)
        if updated != original:
            open(f, "w").write(updated)
    print(f"stamped {stamped} evidence header(s)")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true")
    g.add_argument("--report", action="store_true")
    g.add_argument("--apply", action="store_true")
    a = ap.parse_args()
    sys.exit(check() if a.check else report() if a.report else apply())
