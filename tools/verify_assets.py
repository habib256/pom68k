#!/usr/bin/env python3
"""Validate identities of user-provided POM68K reference assets.

Missing files are reported but accepted by default because clean clones cannot
ship Apple firmware. --strict turns missing entries into failures for a private
asset-bearing runner. Present files never soft-pass a size or digest mismatch.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys


def parse_manifest(path: Path):
    seen = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) != 5:
            raise ValueError(f"{path}:{number}: expected five | separated fields")
        label, size_text, digest, relpath, profiles = cells
        try:
            size = int(size_text)
        except ValueError as exc:
            raise ValueError(f"{path}:{number}: invalid size {size_text!r}") from exc
        if size < 0 or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError(f"{path}:{number}: invalid size or lowercase SHA-256")
        rel = Path(relpath)
        if rel.is_absolute() or ".." in rel.parts:
            raise ValueError(f"{path}:{number}: asset path must stay below the repository")
        if relpath in seen:
            raise ValueError(f"{path}:{number}: duplicate asset path {relpath}")
        seen.add(relpath)
        yield label, size, digest, rel, profiles


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", default="assets.lock")
    parser.add_argument("--root", default=".")
    parser.add_argument("--strict", action="store_true",
                        help="fail when a manifest entry is absent")
    args = parser.parse_args()
    manifest = Path(args.manifest)
    root = Path(args.root)
    failures = 0
    present = 0
    try:
        entries = list(parse_manifest(manifest))
    except (OSError, ValueError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    for label, expected_size, expected_digest, rel, profiles in entries:
        path = root / rel
        if not path.is_file():
            print(f"{'FAIL' if args.strict else 'MISS'} {label}: {rel}")
            failures += int(args.strict)
            continue
        present += 1
        actual_size = path.stat().st_size
        actual_digest = sha256(path)
        ok = actual_size == expected_size and actual_digest == expected_digest
        print(f"{'OK  ' if ok else 'FAIL'} {label}: {rel} "
              f"{actual_size} B sha256 {actual_digest} [{profiles}]")
        failures += int(not ok)
    print(f"assets: {len(entries)} declared, {present} present, {failures} failure(s)")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
