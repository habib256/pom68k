#!/usr/bin/env python3
"""Validate identities and roles of user-provided POM68K reference assets.

Missing files are reported but accepted by default because clean clones cannot
ship Apple firmware. --strict turns missing entries into failures for a private
asset-bearing runner. Present files never soft-pass a size or digest mismatch.
Reference disks must live below hdv/ref so a GUI session cannot write them.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
from pathlib import Path, PurePosixPath
import re
import sys


@dataclass(frozen=True)
class Asset:
    role: str
    label: str
    size: int
    digest: str
    path: Path
    profiles: str


ROLE_ROOTS = {
    "firmware": ("roms",),
    "machine-rom": ("roms",),
    "declaration-rom": ("roms",),
    "reference-disk": ("hdv", "ref"),
}
PROFILE_RE = re.compile(r"[a-z0-9]+")
CATALOG_ROW_RE = re.compile(
    r'^\s*\{\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"([a-z0-9]+)"\s*,', re.MULTILINE)


def parse_manifest(path: Path):
    seen = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) != 6:
            raise ValueError(f"{path}:{number}: expected six | separated fields")
        role, label, size_text, digest, relpath, profiles = cells
        if role not in ROLE_ROOTS:
            allowed = ", ".join(ROLE_ROOTS)
            raise ValueError(f"{path}:{number}: invalid role {role!r}; expected {allowed}")
        if not label:
            raise ValueError(f"{path}:{number}: asset label must not be empty")
        try:
            size = int(size_text)
        except ValueError as exc:
            raise ValueError(f"{path}:{number}: invalid size {size_text!r}") from exc
        if size <= 0 or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError(f"{path}:{number}: invalid size or lowercase SHA-256")
        posix = PurePosixPath(relpath)
        rel = Path(relpath)
        if rel.is_absolute() or ".." in rel.parts or posix.as_posix() != relpath:
            raise ValueError(f"{path}:{number}: asset path must stay below the repository")
        root_parts = ROLE_ROOTS[role]
        if rel.parts[:len(root_parts)] != root_parts:
            expected = "/".join(root_parts) + "/"
            raise ValueError(f"{path}:{number}: {role} path must be below {expected}")
        if role == "reference-disk" and rel.suffix.lower() not in {".dsk", ".vhd"}:
            raise ValueError(f"{path}:{number}: reference disk must be a .dsk or .vhd image")
        if relpath in seen:
            raise ValueError(f"{path}:{number}: duplicate asset path {relpath}")
        seen.add(relpath)
        profile_list = profiles.split(",")
        if (not profiles or len(profile_list) != len(set(profile_list))
                or any(not PROFILE_RE.fullmatch(profile) for profile in profile_list)):
            raise ValueError(
                f"{path}:{number}: profiles must be unique comma-separated catalogue slugs")
        yield Asset(role, label, size, digest, rel, profiles)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_profile_slugs(entries, root: Path) -> None:
    """Cross-check profile fields when the source catalogue is available."""
    catalog = root / "src" / "MachineCatalog.h"
    if not catalog.is_file():
        return
    slugs = set(CATALOG_ROW_RE.findall(catalog.read_text(encoding="utf-8")))
    if not slugs:
        raise ValueError(f"{catalog}: could not read any machine profile slugs")
    used = {profile for entry in entries for profile in entry.profiles.split(",")}
    unknown = sorted(used - slugs)
    if unknown:
        raise ValueError(f"unknown MachineCatalog profile slug(s): {', '.join(unknown)}")
    machine_rom_profiles = Counter(
        profile
        for entry in entries if entry.role == "machine-rom"
        for profile in entry.profiles.split(","))
    missing = sorted(slugs - machine_rom_profiles.keys())
    duplicated = sorted(
        profile for profile, count in machine_rom_profiles.items() if count != 1)
    if missing or duplicated:
        details = []
        if missing:
            details.append(f"missing machine ROM: {', '.join(missing)}")
        if duplicated:
            details.append(f"multiple machine ROMs: {', '.join(duplicated)}")
        raise ValueError("MachineCatalog coverage error (" + "; ".join(details) + ")")


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
        validate_profile_slugs(entries, root)
    except (OSError, ValueError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    for entry in entries:
        path = root / entry.path
        if not path.is_file():
            print(f"{'FAIL' if args.strict else 'MISS'} {entry.role:15} "
                  f"{entry.label}: {entry.path}")
            failures += int(args.strict)
            continue
        present += 1
        actual_size = path.stat().st_size
        actual_digest = sha256(path)
        ok = actual_size == entry.size and actual_digest == entry.digest
        print(f"{'OK  ' if ok else 'FAIL'} {entry.role:15} {entry.label}: "
              f"{entry.path} {actual_size} B sha256 {actual_digest} "
              f"[{entry.profiles}]")
        failures += int(not ok)
    roles = Counter(entry.role for entry in entries)
    role_summary = ", ".join(f"{role}={roles[role]}" for role in ROLE_ROOTS)
    print(f"assets: {len(entries)} declared ({role_summary}), {present} present, "
          f"{failures} failure(s)")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
