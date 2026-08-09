#!/usr/bin/env python3
"""Generate CHANGELOG_INDEX.md — one line per dated entry, grouped by subsystem.

CHANGELOG.md is the project's real design record and it is 576 KB / 204 dated
entries: excellent, and no longer readable linearly.  It already carries two
indexes of its own — by date, and by "a question a reader arrives with" — but
neither answers "everything that ever happened to the SCSI stack".  This does.

The grouping is a KEYWORD HEURISTIC over the entry's hook, not a judgement.
An entry that matches nothing lands under "Cross-cutting", which is honest;
an entry filed under the wrong heading is a bug in the table below, and the
fix is to add a keyword rather than to hand-edit the output.

Regenerate with:  python3 tools/changelog_index.py
`docs_test` fails if the index has drifted from CHANGELOG.md.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "CHANGELOG.md"
OUT = ROOT / "CHANGELOG_INDEX.md"

# Ordered: the first group whose pattern matches the hook wins, so the more
# specific subsystems come first.
GROUPS = [
    ("JIT — the second execution engine",
     r"\bjit\b|lockstep|backend|codegen|a64|x86-64|x64\b|pgo\b"),
    ("CPU cores, MMU, FPU and the WinUAE oracle",
     r"\bmoira\b|oracle|68030|68040|68020|68000|sst|pmmu|\bmmu\b|\bfpu\b|68882|"
     r"softfloat|atc\b|i-?cache|cache boost|vecteur|vector"),
    ("MCU firmware LLE — Egret, Cuda, PIC, PG&E",
     r"egret|cuda|68hc05|6805|pic1654|adb\b|pg&e|pge\b|pmu\b|borg"),
    ("Storage — SCSI, IWM, SWIM, media",
     r"scsi|5380|53c96|iwm\b|swim|floppy|disquette|gcr\b|hfs\b|volume|"
     r"cd-?rom|daynaport|rascsi|disque"),
    ("Video — decoders, the raster beam, DAFB",
     r"dafb|valkyrie|raster|framebuffer|video|vram|clut|écran|screen|pixmap|"
     r"gsc\b|toby|beam"),
    ("Sound", r"\basc\b|sound|audio|son\b|chime"),
    ("Serial, LocalTalk and AppleTalk",
     r"appletalk|localtalk|llap|ltoudp|scc\b|8530|afp\b|\bpap\b|macip|netatalk"),
    ("Save states", r"save state|savestate|snapshot|état sauvé|restaur"),
    ("Machine bring-ups",
     r"bring-?up|boots?\b|profile|profil|quadra|centris|performa|powerbook|duo\b|"
     r"iifx|iisi|iici|iivx|iivi|lc \d|lc ii|classic|color classic|mac tv|"
     r"se/30|mac plus|machine"),
    ("Build, packaging and release",
     r"build|cmake|appimage|release|packaging|raspberry|\bpi\b|lto\b|-mcpu|"
     r"-mtune|windows|macos univers|ci\b|workflow"),
    ("Tests, gates and measurement",
     r"gate|ctest|etalon|étalon|test\b|tests\b|soak|persist|mesure|measured|"
     r"benchmark|harness"),
    ("Documentation, audits and reviews",
     r"\bdoc\b|docs\b|changelog|audit|review|revue|sync|status pass|readme"),
]

HEADING = re.compile(r"^## (\d{4}-\d{2}-\d{2})(?: \(([^)]+)\))? — (.*)$")


def slug(text):
    """GitHub's heading anchor: lowercase, drop punctuation, spaces to hyphens."""
    s = text.lower()
    s = re.sub(r"[^\w\s-]", "", s, flags=re.UNICODE)
    return re.sub(r"\s", "-", s.strip())


def classify(hook):
    low = hook.lower()
    for name, pattern in GROUPS:
        if re.search(pattern, low):
            return name
    return "Cross-cutting"


def main():
    lines = SRC.read_text(encoding="utf-8").split("\n")
    entries = []
    for i, line in enumerate(lines):
        m = HEADING.match(line)
        if not m:
            continue
        date, qual, hook = m.group(1), m.group(2), m.group(3)
        anchor = None
        if i > 0:
            a = re.match(r'<a id="([^"]+)"></a>', lines[i - 1].strip())
            if a:
                anchor = a.group(1)
        if not anchor:
            anchor = slug(line[3:])
        entries.append((date, qual, hook, anchor))

    if not entries:
        print("no dated entries found — is CHANGELOG.md intact?", file=sys.stderr)
        return 1

    buckets = {}
    for e in entries:
        buckets.setdefault(classify(e[2]), []).append(e)

    out = []
    out.append("# CHANGELOG — index by subsystem")
    out.append("")
    out.append(f"**Generated** by `tools/changelog_index.py` from the "
               f"{len(entries)} dated entries in `CHANGELOG.md`. Do not edit by "
               f"hand: regenerate. `docs_test` fails when the two drift.")
    out.append("")
    out.append("`CHANGELOG.md` carries two indexes of its own — [by date]"
               "(CHANGELOG.md#index-by-date), newest first, and [by topic]"
               "(CHANGELOG.md#index-by-topic), phrased as the question a reader "
               "arrives with. This third one answers a different question: "
               "*everything that ever happened to one subsystem*.")
    out.append("")
    out.append("Grouping is a keyword heuristic over each entry's hook. An entry "
               "filed under the wrong heading is a bug in the table in "
               "`tools/changelog_index.py` — add a keyword, regenerate; never "
               "edit this file.")
    out.append("")
    order = [g[0] for g in GROUPS] + ["Cross-cutting"]
    out.append("| Subsystem | Entries |")
    out.append("|---|---:|")
    for name in order:
        if name in buckets:
            out.append(f"| [{name}](#{slug(name)}) | {len(buckets[name])} |")
    out.append("")
    out.append("---")
    out.append("")

    for name in order:
        if name not in buckets:
            continue
        out.append(f"## {name}")
        out.append("")
        for date, qual, hook, anchor in sorted(buckets[name],
                                               key=lambda e: (e[0], e[1] or "")):
            when = f"{date} ({qual})" if qual else date
            out.append(f"- **{when}** — [{hook}](CHANGELOG.md#{anchor})")
        out.append("")

    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(entries)} entries in "
          f"{len(buckets)} groups")
    return 0


if __name__ == "__main__":
    sys.exit(main())
