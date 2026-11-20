#!/usr/bin/env python3
"""Build the two archives POM68K's downloaders read, from a private asset tree.

    build_asset_archives.py firm  [--root DIR] [--out pom68k_firm.zip]  [--strict]
    build_asset_archives.py media [--root DIR] [--out pom68k_media.zip] [--strict]
    build_asset_archives.py --self-test

firm   One member per ROM-catalogue row (src/RomCatalog.h), named by its
       catalogue path ("roms/cuda/341s0788.bin", ...). The dumps are found
       anywhere below <root>/roms by SHA-256, whatever their local name, so
       a tree laid out like the "macroms" collection builds the same archive
       as one laid out like the catalogue. A file of the right size and the
       wrong digest is never packed. This is the zip RomFetch reads as its
       primary source (kRomArchiveUrl).
media  Every file tools/test_media.sha256 lists, at its listed path, each
       one SHA-256-checked first. This is the zip tools/fetch_test_media.py
       reads.

Without --out nothing is written: the command reports what the archive
would hold, what is missing and the sizes. With --out the archive is
written there — never inside this repository (refused), because ROMs and
system media are private inputs that must not be committed. Members carry a
fixed timestamp and are sorted, so two builds of the same tree are
byte-identical. --strict exits 1 when anything is missing.

This script uploads nothing. Publishing an archive is a manual step.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CATALOGUE = REPO / "src" / "RomCatalog.h"
MEDIA_MANIFEST = REPO / "tools" / "test_media.sha256"
FIXED_TIME = (1980, 1, 1, 0, 0, 0)
CHUNK = 1 << 20

TABLE_RE = re.compile(r"kRomDumps\[\]\s*=\s*\{(.*?)\n\};", re.S)
ROW_RE = re.compile(r"\{(\s*\.id\s*=[^{}]*)\}", re.S)
FIELD_RE = re.compile(r'\.(\w+)\s*=\s*("(?:[^"\\]|\\.)*"|[^,}]+)')


def parse_catalogue(text: str) -> list[dict]:
    """The rows of kRomDumps: id, label, path, size, sha256, retroBios, ..."""
    table = TABLE_RE.search(text)
    if not table:
        raise ValueError("kRomDumps table not found")
    rows = []
    for block in ROW_RE.findall(table.group(1)):
        row = {}
        for name, raw in FIELD_RE.findall(block):
            raw = raw.strip()
            if raw.startswith('"'):
                row[name] = raw[1:-1]
            elif raw == "nullptr":
                row[name] = None
            elif re.fullmatch(r"0[xX][0-9a-fA-F]+", raw):
                row[name] = int(raw, 16)
            elif raw.isdigit():
                row[name] = int(raw)
            else:
                row[name] = raw
        for key in ("id", "label", "path", "size", "sha256"):
            if key not in row:
                raise ValueError(f"catalogue row without .{key}: {block[:60]!r}")
        rows.append(row)
    return rows


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_by_digest(roms: Path, rows: list[dict]) -> dict[str, Path]:
    """sha256 -> first file below `roms` with that digest (only files whose
    size is a catalogue size are hashed). Symlinks are followed, loops not."""
    sizes = {row["size"] for row in rows}
    wanted = {row["sha256"] for row in rows}
    found: dict[str, Path] = {}
    seen_dirs = set()
    for dirpath, dirnames, filenames in os.walk(roms, followlinks=True):
        real = os.path.realpath(dirpath)
        if real in seen_dirs:
            dirnames[:] = []
            continue
        seen_dirs.add(real)
        dirnames.sort()
        for name in sorted(filenames):
            path = Path(dirpath) / name
            try:
                if not path.is_file() or path.stat().st_size not in sizes:
                    continue
            except OSError:
                continue
            digest = sha256_file(path)
            if digest in wanted and digest not in found:
                found[digest] = path
    return found


def refuse_in_repo(out: Path) -> None:
    out = out.resolve()
    try:
        out.relative_to(REPO)
    except ValueError:
        return
    raise SystemExit(f"refusing to write {out}: it is inside the repository "
                     f"({REPO}); ROMs and media are never committed")


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o644 << 16
    return info


def human(n: int) -> str:
    return f"{n} bytes ({n / 2**20:.1f} MiB)"


def build_firm(root: Path, rows: list[dict], out: Path | None) -> tuple[list, list]:
    found = find_by_digest(root / "roms", rows)
    present = [(row, found[row["sha256"]]) for row in rows if row["sha256"] in found]
    missing = [row for row in rows if row["sha256"] not in found]
    if out is not None:
        refuse_in_repo(out)
        out.parent.mkdir(parents=True, exist_ok=True)
        tmp = out.with_name(out.name + ".part")
        with zipfile.ZipFile(tmp, "w") as zf:
            for row, path in sorted(present, key=lambda p: p[0]["path"]):
                zf.writestr(zip_info(row["path"]), path.read_bytes())
        os.replace(tmp, out)
    return present, missing


def read_media_manifest(path: Path) -> list[tuple[str, str]]:
    entries = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        digest, rel = line.split("  ", 1)
        entries.append((digest.lower(), rel))
    return entries


def build_media(root: Path, entries: list[tuple[str, str]], out: Path | None):
    present, missing, wrong = [], [], []
    for digest, rel in entries:
        path = root / rel
        if not path.is_file():
            missing.append(rel)
        elif sha256_file(path) != digest:
            wrong.append(rel)
        else:
            present.append((rel, path))
    if out is not None:
        refuse_in_repo(out)
        out.parent.mkdir(parents=True, exist_ok=True)
        tmp = out.with_name(out.name + ".part")
        with zipfile.ZipFile(tmp, "w", allowZip64=True) as zf:
            for rel, path in sorted(present):
                with path.open("rb") as src, zf.open(zip_info(rel), "w", force_zip64=True) as dst:
                    for chunk in iter(lambda: src.read(CHUNK), b""):
                        dst.write(chunk)
        os.replace(tmp, out)
    return present, missing, wrong


def run_firm(args) -> int:
    rows = parse_catalogue(CATALOGUE.read_text(encoding="utf-8"))
    out = Path(args.out) if args.out else None
    present, missing = build_firm(Path(args.root), rows, out)
    total = sum(row["size"] for row, _ in present)
    print(f"firm: {len(present)} of {len(rows)} catalogue dumps found below "
          f"{Path(args.root) / 'roms'}, {human(total)} unpacked")
    served = sum(1 for row, _ in present if row.get("retroBios"))
    print(f"firm: {served} of them are also on RetroBIOS, "
          f"{len(present) - served} only in this archive")
    for row in missing:
        print(f"MISSING {row['id']:16} {row['label']}: {row['path']}")
    if out is not None:
        print(f"firm: wrote {out} — {human(out.stat().st_size)}")
    return int(bool(missing) and args.strict)


def run_media(args) -> int:
    entries = read_media_manifest(Path(args.manifest))
    out = Path(args.out) if args.out else None
    present, missing, wrong = build_media(Path(args.root), entries, out)
    total = sum(path.stat().st_size for _, path in present)
    print(f"media: {len(present)} of {len(entries)} listed files present and "
          f"verified, {human(total)}")
    for rel, path in present:
        print(f"  {path.stat().st_size:>12}  {rel}")
    for rel in missing:
        print(f"MISSING {rel}")
    for rel in wrong:
        print(f"WRONG   {rel}: SHA-256 differs from the manifest (not packed)")
    if out is not None:
        print(f"media: wrote {out} — {human(out.stat().st_size)}")
    return int(bool(missing or wrong) and args.strict)


def self_test() -> int:
    rows = parse_catalogue(CATALOGUE.read_text(encoding="utf-8"))
    assert len(rows) >= 50, len(rows)
    assert len({r["path"] for r in rows}) == len(rows), "duplicate catalogue path"
    assert all(r["path"].startswith("roms/") for r in rows)
    assert all(re.fullmatch(r"[0-9a-f]{64}", r["sha256"]) for r in rows)
    assert all(isinstance(r["size"], int) and r["size"] > 0 for r in rows)
    assert sum(1 for r in rows if r.get("retroBios")) == 20
    pmu = next(r for r in rows if r["id"] == "pmu-pb160")
    assert pmu["path"] == "roms/pmu/pb160.bin" and pmu["retroBios"] is None
    entries = read_media_manifest(MEDIA_MANIFEST)
    assert entries and all(re.fullmatch(r"[0-9a-f]{64}", d) for d, _ in entries)
    assert len({rel for _, rel in entries}) == len(entries), "duplicate media path"

    a, b, c = os.urandom(4096), os.urandom(2048), os.urandom(1024)
    synthetic = [
        {"id": "a", "label": "A", "path": "roms/cuda/a.bin", "size": 4096,
         "sha256": hashlib.sha256(a).hexdigest()},
        {"id": "b", "label": "B", "path": "roms/1MB ROMs/b.ROM", "size": 2048,
         "sha256": hashlib.sha256(b).hexdigest()},
        {"id": "c", "label": "C", "path": "roms/pmu/c.bin", "size": 1024,
         "sha256": hashlib.sha256(c).hexdigest()},
        {"id": "d", "label": "D", "path": "roms/d.bin", "size": 8,
         "sha256": "0" * 64},
    ]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / "tree"
        (root / "roms/archive/deep").mkdir(parents=True)
        (root / "roms/archive/deep/whatever name.bin").write_bytes(a)
        (root / "roms/b-local-name.rom").write_bytes(b)
        (root / "roms/c.bin").write_bytes(bytes(1024))        # right size, wrong bytes
        out1, out2 = Path(tmp) / "one.zip", Path(tmp) / "two.zip"
        present, missing = build_firm(root, synthetic, out1)
        build_firm(root, synthetic, out2)
        assert [r["id"] for r, _ in present] == ["a", "b"], present
        assert [r["id"] for r in missing] == ["c", "d"], missing
        with zipfile.ZipFile(out1) as zf:
            assert sorted(zf.namelist()) == ["roms/1MB ROMs/b.ROM", "roms/cuda/a.bin"]
            assert zf.read("roms/cuda/a.bin") == a
        assert out1.read_bytes() == out2.read_bytes(), "builds are not reproducible"
        try:
            build_firm(root, synthetic, REPO / "build-never" / "x.zip")
            raise AssertionError("an archive inside the repository was not refused")
        except SystemExit as refusal:
            assert "inside the repository" in str(refusal)
        assert not (REPO / "build-never").exists()

        (root / "hdv/ref").mkdir(parents=True)
        disk = os.urandom(70000)
        (root / "hdv/ref/Disk.vhd").write_bytes(disk)
        (root / "disks35").mkdir()
        (root / "disks35/Wrong.dsk").write_bytes(b"x" * 10)
        manifest = [(hashlib.sha256(disk).hexdigest(), "hdv/ref/Disk.vhd"),
                    (hashlib.sha256(b"y" * 10).hexdigest(), "disks35/Wrong.dsk"),
                    ("1" * 64, "hdv/Absent.vhd")]
        media = Path(tmp) / "media.zip"
        present, missing, wrong = build_media(root, manifest, media)
        assert [rel for rel, _ in present] == ["hdv/ref/Disk.vhd"]
        assert missing == ["hdv/Absent.vhd"] and wrong == ["disks35/Wrong.dsk"]
        with zipfile.ZipFile(media) as zf:
            assert zf.namelist() == ["hdv/ref/Disk.vhd"]
            assert zf.read("hdv/ref/Disk.vhd") == disk
    print(f"build_asset_archives: self-test OK ({len(rows)} catalogue rows, "
          f"{len(entries)} media entries)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("kind", nargs="?", choices=("firm", "media"))
    parser.add_argument("--root", default=str(REPO),
                        help="tree holding roms/, hdv/, disks35/ (default: the repository)")
    parser.add_argument("--out", help="write the archive here (outside the repository)")
    parser.add_argument("--manifest", default=str(MEDIA_MANIFEST),
                        help="media manifest (default: tools/test_media.sha256)")
    parser.add_argument("--strict", action="store_true", help="exit 1 when anything is missing")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.kind == "firm":
        return run_firm(args)
    if args.kind == "media":
        return run_media(args)
    parser.error("choose firm or media (or --self-test)")
    return 2


if __name__ == "__main__":
    sys.exit(main())
