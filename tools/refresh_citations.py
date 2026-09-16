#!/usr/bin/env python3
"""Find — and with --fix, repair — `file:line` citations whose anchor moved.

The docs cite code as `path.ext:first[-last]` several hundred times, and
docs_test § 10 proves each range exists inside its file. That is the weak
half: a citation can point at the wrong code and stay green. The sound half
needs an ANCHOR — an identifier the sentence itself names in backticks
(`loadPram`, `kCpuHz230`, `jit_lockstep_a64_coarse_test`) that the reader
expects to find in the cited lines. This tool applies that rule:

  * a citation is ANCHORED when one such identifier from the surrounding
    sentence (±200 characters) occurs within the cited range, three lines
    of slack either side;
  * it has DRIFTED when none does but at least one of them occurs elsewhere
    in the cited file — the code moved, the number did not;
  * otherwise it is UNANCHORED: the sentence names nothing a machine can
    check, and the citation is trusted as before.

Only code-like identifiers count: four characters or more, with an
uppercase letter, an underscore, a `::` or a trailing `()`, so a stray
prose word in backticks (`save`, `conservative`) cannot anchor or accuse.
And only a FUNCTION, MEMBER or MACRO can accuse — `loadPram`, `montype_`,
`POM68K_SST_DIR` — never a type name: `Cpu030` occurs on every other line
of Cpu030.h and would drag a citation to its first mention. A type name
can still anchor (confirm) a range. The repair moves the range onto the
accusing anchor's occurrence NEAREST the old line, width kept.

    tools/refresh_citations.py            # list drifted citations, exit 1 if any
    tools/refresh_citations.py --fix      # rewrite each drifted range onto the
                                          # anchor's current lines (width kept)
    tools/refresh_citations.py --all      # also list anchored/unanchored counts per doc

docs_test § 10 runs the same rule and fails on a drifted citation; run
--fix, read the diff, commit.
"""
import argparse
import glob
import os
import re
import sys

DOCS = ["CLAUDE.md", "README.md", "DEV.md", "TODO.md",
        "src/jit/POM68K_JIT.md", "extern/moira/POM68K_VENDOR.md"]
ROOTS = ["", "src/", "src/jit/", "src/jit/backends/", "tests/", "tools/",
         "oracle/", "extern/moira/Moira/", ".github/workflows/"]
CITE = re.compile(r"`([A-Za-z0-9_./-]+\.[A-Za-z]+):(\d+)(?:-(\d+))?`")
IDENT = re.compile(r"`([A-Za-z_][A-Za-z0-9_:]*)(\(\))?`")
SLACK = 3
WINDOW = 200


def code_like(name):
    return (len(name) >= 4 and
            (any(c.isupper() for c in name) or "_" in name or "::" in name))


def can_accuse(leaf, called):
    # A function/member/macro, not a CamelCase type name.
    if called or "_" in leaf:
        return True
    return leaf[0].islower() and any(c.isupper() for c in leaf)


def resolve(root, path):
    for base in ROOTS:
        candidate = os.path.join(root, base + path)
        if os.path.isfile(candidate) and os.path.basename(candidate) == os.path.basename(path):
            return candidate
    return None


def classify(root, doc, text):
    """Yield (kind, match, path, first, last, anchors, where) per citation."""
    cache = {}
    for m in CITE.finditer(text):
        path, first = m.group(1), int(m.group(2))
        last = int(m.group(3)) if m.group(3) else first
        resolved = resolve(root, path)
        if not resolved:
            continue
        if resolved not in cache:
            with open(resolved, encoding="utf-8", errors="replace") as f:
                cache[resolved] = f.read().split("\n")
        lines = cache[resolved]
        window = text[max(0, m.start() - WINDOW):m.end() + WINDOW]
        anchors, accusers = [], []
        stem = os.path.splitext(os.path.basename(path))[0]
        for im in IDENT.finditer(window):
            name = im.group(1)
            if name == path or "." in name:
                continue
            leaf = name.split("::")[-1]
            if (code_like(name) or im.group(2)) and len(leaf) >= 4 and leaf != stem:
                if leaf not in anchors:
                    anchors.append(leaf)
                if can_accuse(leaf, bool(im.group(2))) and leaf not in accusers:
                    accusers.append(leaf)
        if not anchors:
            yield ("unanchored", m, path, first, last, [], [])
            continue
        lo, hi = max(1, first - SLACK), min(len(lines), last + SLACK)
        in_range = [a for a in anchors if any(a in lines[i - 1] for i in range(lo, hi + 1))]
        if in_range:
            yield ("anchored", m, path, first, last, in_range, [])
            continue
        where = []
        for a in accusers:
            hits = [i + 1 for i, line in enumerate(lines) if a in line]
            if hits:
                hits.sort(key=lambda h: abs(h - first))   # nearest the old line first
                where.append((a, hits))
        if where:
            yield ("drifted", m, path, first, last, anchors, (where, len(lines)))
        else:
            yield ("unanchored", m, path, first, last, anchors, [])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--fix", action="store_true", help="rewrite drifted citations in place")
    ap.add_argument("--all", action="store_true", help="print per-doc counts of every kind")
    args = ap.parse_args()
    root = args.root
    docs = DOCS + sorted("docs/" + os.path.basename(p) for p in glob.glob(os.path.join(root, "docs", "*.md")))
    drifted_total = 0
    for doc in docs:
        path = os.path.join(root, doc)
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            text = f.read()
        counts = {"anchored": 0, "drifted": 0, "unanchored": 0}
        edits = []
        for kind, m, cited, first, last, anchors, where in classify(root, doc, text):
            counts[kind] += 1
            if kind != "drifted":
                continue
            drifted_total += 1
            # The repair: the first anchor's first occurrence, the range's
            # width kept, so `f:10-14` for a symbol now at 30 becomes `f:30-34`.
            (anchor, hits), file_lines = where[0][0], where[1]
            new_first = hits[0]
            new_last = min(new_first + (last - first), file_lines)   # never past the file
            new = "`%s:%d%s`" % (cited, new_first, ("-%d" % new_last) if last != first else "")
            print("%s: %s -> %s  (%s at %s)" % (doc, m.group(0), new, anchor,
                                                 ",".join(str(h) for h in hits[:4])))
            edits.append((m.start(), m.end(), new))
        if args.all:
            print("  %-36s anchored %3d  drifted %3d  unanchored %3d" %
                  (doc, counts["anchored"], counts["drifted"], counts["unanchored"]))
        if args.fix and edits:
            for start, end, new in sorted(edits, reverse=True):
                text = text[:start] + new + text[end:]
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
    print("drifted citations: %d%s" % (drifted_total, " (rewritten)" if args.fix and drifted_total else ""))
    return 1 if drifted_total and not args.fix else 0


if __name__ == "__main__":
    sys.exit(main())
