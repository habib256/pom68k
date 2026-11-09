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
Manual editions and checked locators live in tests/core_manuals.lock. B is
primary-document evidence, not proof that every implementation choice in the
same block agrees with it. F names the missing witness. Historical Musashi
comparisons whose patched local tree is lost stay F; CLK aliases add no
independent evidence to the pinned SingleStepTests corpus.

  --check   the gate: every WinUAE-citing block carries `[D]` and a
            revision; every `file.c:N[-M]` it cites exists in the pinned
            tree and the range lies inside the file. Every SingleStepTests
            block has its own grade-D source/revision header; another
            oracle's revision does not qualify. Fetcher and corpus lock
            must agree with that pin. Manual references need their own
            B/F edition header and a reviewed locator; F needs a witness.
  --verify-manual-files DIRECTORY
            optional reviewer check: the four fetched PDFs match their
            locked hashes. Normal CI needs no PDFs and performs no fetch.
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
            It never promotes a manual citation to B automatically.

Gate: core_grades_test (cmake/Pom68kCpuGates.cmake).
"""
import argparse
import glob
import hashlib
import json
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
MANUAL_LOCK = os.path.join(ROOT, "tests", "core_manuals.lock")
MANUAL_FAMILY = re.compile(
    r"\b(?:MC68030UM|M?C?68040UM|MC68881UM|MC68882UM|M68000PRM|"
    r"MC68881/(?:MC68882|882)\s*UM)"
    r"(?:-\d{4})?\b")
MANUAL_HEAD = re.compile(r"^\s*//\s*\[([BF])\]\s+([^\n]*)", re.MULTILINE)
LOCATOR = re.compile(r"§\s*(\d+(?:\.\d+)*)|\b((?:Table|Figure)\s+[A-Z0-9]+-\d+)\b")


def manual_catalog():
    with open(MANUAL_LOCK, encoding="utf-8") as f:
        return json.load(f)["sources"]


def manual_problems(text, catalog=None):
    """Hold edition/locator attribution, not semantic agreement with a manual.

    A B header identifies reviewed primary evidence. F identifies a missing
    witness or a deliberate approximation, and must say what settles it.
    Other sources' D/B headers cannot qualify a manual reference.
    """
    references = set(MANUAL_FAMILY.findall(text))
    if not references:
        # A repository's own numbered section is not a manual citation.
        external = re.sub(r"[\w./-]+\.(?:md|h|cpp)\s+§\s*\d+(?:\.\d+)*", "", text)
        if LOCATOR.search(external):
            return ["anonymous manual locator needs a named edition and grade"]
        return []
    catalog = manual_catalog() if catalog is None else catalog
    problems = []
    for source in sorted(references):
        if source not in catalog:
            problems.append(f"manual {source} has no checked edition in core_manuals.lock")
            continue
        heads = [(grade, line) for grade, line in MANUAL_HEAD.findall(text)
                 if source in MANUAL_FAMILY.findall(line)]
        if not heads:
            problems.append(f"manual {source} needs its own [B] or [F] source header")
        elif all(grade == "F" for grade, _ in heads) and "Settled by:" not in text:
            problems.append(f"manual {source}: [F] must name its missing witness (Settled by:)")
        # Single-manual units also check inherited/anonymous paragraph and
        # table references. With multiple manuals, check each attributed
        # header; do not silently assign one manual's paragraph to another.
        fragments = [text] if len(references) == 1 else [line for _, line in heads]
        locators = set()
        for fragment in fragments:
            for section, table in LOCATOR.findall(fragment):
                locators.add("§" + section if section else table)
        for locator in sorted(locators):
            if locator not in catalog[source]["locators"]:
                problems.append(f"{source}: {locator} is not a reviewed locator")
        if any(grade == "B" for grade, _ in heads) and not locators:
            # PRM instructions are named entries, not numbered subsections.
            names = catalog[source]["locators"]
            if not any(re.search(r"\b" + re.escape(name) + r"\b", line)
                       for grade, line in heads if grade == "B"
                       for name in names if not name.startswith(("§", "Table ", "Figure "))):
                problems.append(f"manual {source}: [B] needs a reviewed locator")
    return problems


def catalog_problems(catalog):
    problems = []
    for source, entry in catalog.items():
        if not re.fullmatch(r"[0-9a-f]{64}", entry.get("sha256", "")):
            problems.append(f"{source}: missing PDF SHA-256")
        if not entry.get("edition") or not entry.get("url", "").startswith("https://"):
            problems.append(f"{source}: missing edition/source URL")
        pages = entry.get("pdf_pages", 0)
        for locator, page in entry.get("locators", {}).items():
            if not isinstance(page, int) or not 1 <= page <= pages:
                problems.append(f"{source}: {locator} points outside the PDF")
    return problems


def verify_manual_files(directory):
    """Optional reviewer check. Normal CI never needs the PDF payloads."""
    failures = []
    for source, entry in manual_catalog().items():
        path = os.path.join(directory, entry["file"])
        if not os.path.isfile(path):
            failures.append(f"{source}: missing {path}")
        else:
            with open(path, "rb") as f:
                digest = hashlib.sha256(f.read()).hexdigest()
            if digest != entry["sha256"]:
                failures.append(f"{source}: PDF differs from the reviewed bytes")
    for failure in failures:
        print("FAIL", failure)
    print(f"manual PDF identity: {len(failures)} problem(s)")
    return 1 if failures else 0


def other_emulator_problems(text):
    # Compatibility syntax is an API choice, not a claim of hardware proof.
    if text.strip() == "// Musashi compatibility":
        return []
    names = re.findall(r"\bMusashi\b|\bCLK\b|Clock Signal", text)
    if not names:
        return []
    problems = []
    for name in set(names):
        evidence = re.findall(r"\[([DF])\]([^\n]*)", text)
        own = [(grade, line) for grade, line in evidence if name in line]
        if not own:
            problems.append(f"{name}: needs its own [D] revision or explicit [F] witness")
        elif any(grade == "D" and not re.search(
                r"@\s*(?:[0-9a-f]{7,40}\b|[vV]?\d+(?:\.\d+)+\b|"
                r"[A-Za-z][\w.-]*\d[\w.-]*\b)", line) for grade, line in own):
            problems.append(f"{name}: [D] needs a revision")
        elif all(grade == "F" for grade, _ in own) and "Settled by:" not in text:
            problems.append(f"{name}: [F] needs its missing witness (Settled by:)")
    return problems


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
    problems = (sst_problems(text) + manual_problems(text) +
                other_emulator_problems(text))
    if "[F]" in text and "Settled by:" not in text:
        problems.append("[F] needs its missing witness (Settled by:)")
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
    problems = sst_pin_problems(fetcher, lock) + catalog_problems(manual_catalog())
    blocks = sst_blocks = manual_blocks = 0
    for f in core_files():
        lines = open(f).read().split("\n")
        rel = os.path.relpath(f, ROOT)
        for a, b, inline in comment_units(lines):
            text = comment_text(lines, a, b, inline)
            if SST.search(text):
                sst_blocks += 1
            if MANUAL_FAMILY.search(text):
                manual_blocks += 1
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
          f"core, {sst_blocks} SingleStepTests blocks, {manual_blocks} manual blocks, "
          f"{len(problems)} problem(s)")
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
    g.add_argument("--verify-manual-files", metavar="DIRECTORY")
    a = ap.parse_args()
    sys.exit(verify_manual_files(a.verify_manual_files) if a.verify_manual_files
             else check() if a.check else report() if a.report else apply())
