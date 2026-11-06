#!/usr/bin/env python3
"""Verify — or with --write, record — the identity of the CPU vector corpora.

The three corpora behind `sst68000`, `sst68030` and `sst68040` are too large
for the repository (1.1 GB, 116 MB, 315 MB) and live in git-ignored
`tests/data/`. Until 2026-11-06 nothing pinned them: a gate could be green
on one host and on another against different files, and nobody could tell.
`tests/sst_corpora.lock` records, per file, the size and SHA-256 of what the
gates were pinned against, with each corpus's provenance in its header.

    tools/verify_sst_corpora.py                 # verify what is present
    tools/verify_sst_corpora.py --strict        # an absent corpus is a failure
    tools/verify_sst_corpora.py --write         # re-record from disk
    tools/verify_sst_corpora.py --dir sst68030=/elsewhere/sst68030

An absent corpus prints a `SKIP:` line and does not fail (the harnesses
soft-skip the same way); a present one must match file for file, with no
unlisted `.json` beside the listed ones. Exit 0 = every present corpus
matches; 1 = mismatch, or absence under --strict; 2 = usage.
"""
import argparse
import hashlib
import sys
from pathlib import Path

CORPORA = ("sst68000", "sst68030", "sst68040")

HEADER = """\
# Identity of the CPU vector corpora (tools/verify_sst_corpora.py).
# The files live in git-ignored tests/data/<corpus>/; this manifest is what
# makes two hosts' green runs the same claim.
#
# sst68000 — SingleStepTests/680x0, 68000/v1, upstream commit
#   e0d5ece9670205cc84a0101081837deb446f86a3 (2024-05-14, the only commit that
#   touches that directory). 124 files x 8 065 vectors, stored upstream as
#   .json.gz; the hashes below are of the unpacked .json.
#   Fetch: tests/fetch_sst_68000.sh tests/data/sst68000
# sst68030 — generated here by oracle/fuzz (gen030.py, fuzz030.py) against the
#   WinUAE oracle of oracle/uae (Hatari e77819f7). 3 082 vectors. Several
#   cells date from the two-oracle period and keep only vectors on which the
#   retired Musashi oracle agreed (oracle/fuzz/disputes/NOTES.md); the fpu
#   cells are WinUAE-solo (ruling D22). Seeds are in each vector's name
#   ("core/identity seed=31 #1").
# sst68040 — generated here by oracle/fuzz (gen040.py, fuzz040.py) against the
#   same oracle in 68040 mode, 68LC040 (no FPU). 7 200 vectors.
#
# corpus | file | size | sha256
"""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_manifest(path: Path):
    entries = {name: {} for name in CORPORA}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) != 4 or cells[0] not in entries or not cells[2].isdigit():
            raise ValueError(f"{path}:{number}: expected 'corpus | file | size | sha256'")
        entries[cells[0]][cells[1]] = (int(cells[2]), cells[3])
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", default="tests/sst_corpora.lock")
    parser.add_argument("--root", default=".")
    parser.add_argument("--dir", action="append", default=[],
                        metavar="CORPUS=PATH", help="override a corpus directory")
    parser.add_argument("--strict", action="store_true",
                        help="an absent corpus is a failure")
    parser.add_argument("--write", action="store_true",
                        help="re-record the manifest from disk")
    args = parser.parse_args()

    root = Path(args.root)
    dirs = {name: root / "tests" / "data" / name for name in CORPORA}
    for item in args.dir:
        name, _, value = item.partition("=")
        if name not in dirs or not value:
            print(f"error: --dir expects one of {', '.join(CORPORA)}=PATH", file=sys.stderr)
            return 2
        dirs[name] = Path(value)
    manifest = root / args.manifest

    if args.write:
        lines = [HEADER]
        for name in CORPORA:
            files = sorted(dirs[name].glob("*.json")) if dirs[name].is_dir() else []
            if not files:
                print(f"error: cannot record {name}: no .json in {dirs[name]}", file=sys.stderr)
                return 1
            for path in files:
                lines.append(f"{name} | {path.name} | {path.stat().st_size} | {sha256(path)}\n")
            lines.append("\n")
        manifest.write_text("".join(lines).rstrip("\n") + "\n", encoding="utf-8")
        print(f"wrote {manifest}")
        return 0

    try:
        entries = read_manifest(manifest)
    except (OSError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1

    failed = False
    for name in CORPORA:
        listed = entries[name]
        directory = dirs[name]
        present = {p.name: p for p in directory.glob("*.json")} if directory.is_dir() else {}
        if not present:
            print(f"SKIP: [sst_corpora] {name} absent at {directory}")
            failed |= args.strict
            continue
        problems = []
        for filename in sorted(set(present) - set(listed)):
            problems.append(f"{filename}: not in the manifest")
        for filename, (size, digest) in sorted(listed.items()):
            path = present.get(filename)
            if path is None:
                problems.append(f"{filename}: missing")
            elif path.stat().st_size != size:
                problems.append(f"{filename}: size {path.stat().st_size}, pinned {size}")
            elif sha256(path) != digest:
                problems.append(f"{filename}: content differs from the pinned SHA-256")
        if problems:
            failed = True
            print(f"FAIL: {name}: {len(problems)} file(s) differ from {manifest.name}")
            for problem in problems[:8]:
                print(f"      {problem}")
        else:
            print(f"ok   {name}: {len(listed)} file(s) match {manifest.name}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
