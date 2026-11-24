#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Prove that extern/moira/Moira is Dirk Hoffmann's base plus the patch queue.

  moira_queue.py check [--build-dir build]

`check` copies extern/moira/Moira to a temporary directory, reverse-applies
extern/moira/patches/NNNN-*.patch from the last to the first, and compares
the git tree hash of the result with line 1 of patches/BASE.tree (the tree of
`Moira/` at the commit in patches/BASE); the rest of BASE.tree is that tree's
`git ls-tree -r` listing, from which a mismatch names the first differing
file. It also checks that the patches are numbered 0001..N without a gap and
touch `Moira/` only, that patches/queue.tsv has one well-formed row per patch,
that every gate the TSV names is in the configure-time roster
(<build-dir>/pom68k_gates.tsv, or pom68k_gates_absent.tsv for a gate this host
does not register), and that POM68K_VENDOR.md's inventory has N rows.

Needs `git` on PATH (plumbing in the temporary directory only); it does not
need the repository's own .git, nor the network. Exit status 0 on success.
"""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
MOIRA = ROOT / "extern" / "moira"
PATCHES = MOIRA / "patches"
COLUMNS = ["num", "file", "state", "pr", "pr_head", "relation", "repro",
           "evidence", "gates", "note"]
STATES = {"local", "proposed", "merged", "dropped"}
RELATIONS = {"", "identical", "same-behaviour", "partial", "diverged"}


def git(cwd, *args):
    r = subprocess.run(["git", "-c", "core.autocrlf=false", "-c",
                        "core.excludesFile=/dev/null", *args], cwd=cwd,
                       capture_output=True, text=True)
    return r.returncode, r.stdout.strip(), r.stderr.strip()


def check_tree(patches, errors):
    lines = (PATCHES / "BASE.tree").read_text().splitlines()
    expected = lines[0].strip()
    listing = [l for l in lines[1:] if l and not l.startswith("#")]
    with tempfile.TemporaryDirectory(prefix="moira_queue_") as tmp:
        shutil.copytree(MOIRA / "Moira", pathlib.Path(tmp) / "Moira",
                        ignore=shutil.ignore_patterns(".DS_Store"))
        git(tmp, "init", "-q")
        for p in reversed(patches):
            rc, _, err = git(tmp, "apply", "-R", "--whitespace=nowarn", str(p))
            if rc:
                first = next((l for l in err.splitlines() if l), err)
                errors.append(f"{p.name} does not reverse-apply: {first}")
                return
        git(tmp, "add", "-f", "-A", "Moira")
        _, root, _ = git(tmp, "write-tree")
        _, tree, _ = git(tmp, "rev-parse", f"{root}:Moira")
        if tree == expected:
            return
        _, got, _ = git(tmp, "ls-tree", "-r", tree)
        have = dict(l.split("\t", 1)[::-1] for l in got.splitlines())
        want = dict(l.split("\t", 1)[::-1] for l in listing)
        diff = sorted(f for f in have.keys() | want.keys()
                      if have.get(f) != want.get(f))
        where = (f"first differing file Moira/{diff[0]}"
                 + ("" if diff[0] in have else " (missing)")
                 + ("" if diff[0] in want else " (not in the base)")
                 if diff else "no file differs from the BASE.tree listing")
        errors.append(f"Moira/ minus the queue is tree {tree}, BASE.tree says "
                      f"{expected}: {where}")


def registered_gates(build_dir):
    gates = set()
    for name in ("pom68k_gates.tsv", "pom68k_gates_absent.tsv"):
        path = build_dir / name
        if path.exists():
            gates |= {l.split("\t")[0] for l in path.read_text().splitlines() if l}
    return gates


def check_tsv(patches, build_dir, errors):
    rows = [l.split("\t") for l in (PATCHES / "queue.tsv").read_text()
            .splitlines() if l and not l.startswith("#")]
    if not rows or rows[0] != COLUMNS:
        errors.append("queue.tsv: header must be " + "\t".join(COLUMNS))
        return
    rows = rows[1:]
    if len(rows) != len(patches):
        errors.append(f"queue.tsv has {len(rows)} rows for {len(patches)} patches")
    gates = registered_gates(build_dir)
    if not gates:
        errors.append(f"no gate roster in {build_dir} (configure it first)")
    cases = set(re.findall(r'std::puts\("(\d+)\. ', (ROOT / "tools" / "dirk_moira"
                                                     / "findings_repro.cpp").read_text()))
    for i, row in enumerate(rows, 1):
        if len(row) != len(COLUMNS):
            errors.append(f"queue.tsv row {i}: {len(row)} fields, not {len(COLUMNS)}")
            continue
        r = dict(zip(COLUMNS, row))
        where = f"queue.tsv row {i} ({r['file']})"
        if (r["num"] != str(i) or i > len(patches) or r["file"] != patches[i - 1].name) \
                and not any("num/file" in e for e in errors):
            errors.append(f"{where}: num/file do not match patch {i:04d} (first mismatch)")
        if r["state"] not in STATES or r["relation"] not in RELATIONS:
            errors.append(f"{where}: bad state or relation")
        if bool(r["pr"]) != bool(r["relation"]) or (
                r["pr"] and not (r["pr"].isdigit() and re.fullmatch(r"[0-9a-f]{40}", r["pr_head"]))):
            errors.append(f"{where}: a PR needs a number, a 40-hex head and a relation")
        if r["state"] == "proposed" and not r["pr"]:
            errors.append(f"{where}: proposed without a PR")
        if r["repro"] and r["repro"] not in cases:
            errors.append(f"{where}: findings_repro.cpp has no case {r['repro']}")
        if not r["evidence"]:
            errors.append(f"{where}: evidence is empty (write none, sst-only or inferred)")
        for g in filter(None, r["gates"].split(",")):
            if gates and g not in gates:
                errors.append(f"{where}: gate {g} is not registered")


def check(args):
    errors = []
    patches = sorted(PATCHES.glob("*.patch"))
    for n, p in enumerate(patches, 1):
        if not p.name.startswith(f"{n:04d}-"):
            errors.append(f"patch {n:04d} missing: found {p.name} in its place")
            break
        for path in re.findall(r"^diff --git a/(\S+) b/(\S+)", p.read_text(), re.M):
            if not all(x.startswith("Moira/") for x in path):
                errors.append(f"{p.name} touches {path[0]} outside Moira/")
    vendor = (MOIRA / "POM68K_VENDOR.md").read_text()
    inventory = vendor.split("## Inventory of local patches", 1)[-1].split("\n## ", 1)[0]
    vendor_rows = len(re.findall(r"^\| \d", inventory, re.M))
    if vendor_rows != len(patches):
        errors.append(f"POM68K_VENDOR.md inventory has {vendor_rows} rows for {len(patches)} patches")
    check_tsv(patches, pathlib.Path(args.build_dir), errors)
    check_tree(patches, errors)
    for e in errors:
        print("FAIL:", e)
    if not errors:
        print(f"moira_queue: Moira/ == BASE + {len(patches)} patches; queue.tsv and inventory agree")
    return 1 if errors else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--build-dir", default=str(ROOT / "build"),
                   help="configured tree holding pom68k_gates.tsv")
    return check(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())
