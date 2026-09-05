#!/usr/bin/env python3
"""Recompute the four headline numbers docs_test § 7 checks in POM68K_VENDOR.md,
with the same semantics as tests/docs_test.cpp:2060-2140."""
import os, re, sys

W = "/home/gistarcade/src/pom68k/.claude/worktrees/agent-a41945e984124a4b6"
moira = os.path.join(W, "extern/moira/Moira")
vendor_path = os.path.join(W, "extern/moira/POM68K_VENDOR.md")

source_files = marked_files = marked_lines = 0
pom_ids = set()
leaks = []
for root, _dirs, files in os.walk(moira):
    for fn in sorted(files):
        if os.path.splitext(fn)[1] not in (".h", ".cpp"):
            continue
        source_files += 1
        src = open(os.path.join(root, fn), encoding="utf-8", errors="replace").read()
        marked = False
        for line in src.split("\n"):
            if "POM68K" in line:
                marked = True
                marked_lines += 1
        ids = set(m.group(0) for m in re.finditer(r"(?<![A-Za-z0-9_])pom[A-Za-z0-9_]+", src))
        if marked:
            marked_files += 1
        elif ids:
            leaks.append(fn)
        pom_ids |= ids

vendor = open(vendor_path, encoding="utf-8").read()
start = vendor.find("## Inventory of local patches")
end = vendor.find("\n## ", start + 3)
inventory = vendor[start:end]
groups = sum(1 for l in inventory.split("\n")
             if len(l) > 3 and l[0] == "|" and l[1] == " " and l[2].isdigit())

jit_ids = sum(1 for i in pom_ids if i.startswith("pomJit"))
claims = [
    ("identifiers", f"**{len(pom_ids)}** ({jit_ids} of them `pomJit*`)"),
    ("marked lines", f"| **{marked_lines}** |"),
    ("marked files", f"**{marked_files} of {source_files}**"),
    ("patch groups", f"| **{groups}** |"),
]
for what, claim in claims:
    print(f"{what:14} expects {claim!r}  present={claim in vendor}")
if leaks:
    print("BOUNDARY LEAKS:", leaks)
