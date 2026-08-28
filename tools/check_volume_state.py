"""What state are a gate's media actually in? — drVolAtrb bit 8, per image.

Why this exists
---------------
`CLAUDE.md` carries this as its own method rule, in capitals: **read
`drVolAtrb` bit 8 on a gate's disk image before theorising about its code.**
It is there because the IIfx gates were red for two days and twice diagnosed
as a code regression, when the cause was a boot volume that had never been
cleanly unmounted (`CHANGELOG.md` 2026-08-06 late night). The rule was
learnt; the measurement stayed manual, which means it is taken *after* the
theorising, not before.

This is the measurement, as one command:

    tools/check_volume_state.py                 # every image under hdv/
    tools/check_volume_state.py --dir cd        # somewhere else
    tools/check_volume_state.py --strict        # non-zero if a REFERENCE
                                                # disk is dirty or drifted

Two independent facts per image, and they answer different questions:

* **CLEAN / DIRTY** — `drVolAtrb` bit 8. DIRTY does not mean broken: the
  qualified `System 7.1 HD.dsk` is dirty and green. It means a boot that
  takes the dirty-volume path, which on System 7.6 tears the video driver
  down mid-boot. When a gate goes red, this line is the first thing to read.
* **MATCH / DRIFTED** — sha256 against the identity `assets.lock` recorded
  when the gate corpus qualified it. A reference disk is supposed to be
  immutable, which is why the schema puts it under `hdv/ref/`; an image sat
  flat in `hdv/` can still be opened writable by a GUI session, and then the
  corpus is no longer running on the bytes it was qualified against.

Neither is a gate. `verify_assets.py` owns identity as a pass/fail; this owns
the *state* a red gate needs explained, and prints it whether or not anything
is wrong.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

IMAGE_SUFFIXES = (".vhd", ".dsk", ".hdv", ".hda", ".img", ".dmg")


def be16(b: bytes, off: int = 0) -> int:
    return (b[off] << 8) | b[off + 1]


def be32(b: bytes, off: int = 0) -> int:
    return int.from_bytes(b[off:off + 4], "big")


def read_mdb(f, offset: int):
    """(volume name, drVolAtrb) if an HFS MDB sits at `offset`, else None."""
    f.seek(offset)
    mdb = f.read(512)
    if len(mdb) != 512 or be16(mdb) != 0x4244:          # 'BD'
        return None
    length = min(mdb[36], 27)                            # drVN, a Str27
    name = mdb[37:37 + length].decode("mac-roman", "replace")
    return name, be16(mdb, 10)


def probe_hfs(path: Path):
    """Mirror of testasset::probeHfs — Apple Partition Map, else flat 1024."""
    with path.open("rb") as f:
        block = f.read(512)
        if len(block) == 512 and be16(block) == 0x4552:  # 'ER'
            size = be16(block, 2)
            if size not in (512, 1024, 2048):
                size = 512
            for index in range(1, 65):
                f.seek(index * 512)
                entry = f.read(512)
                if len(entry) != 512 or be16(entry) != 0x504D:   # 'PM'
                    break
                kind = entry[48:80].split(b"\0")[0].decode("latin1")
                if kind != "Apple_HFS":
                    continue
                found = read_mdb(f, be32(entry, 8) * size + 1024)
                if found:
                    return found
        return read_mdb(f, 1024)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def reference_disks(root: Path) -> dict:
    """basename → (declared path, sha256) for every reference-disk row."""
    lock = root / "assets.lock"
    out = {}
    if not lock.exists():
        return out
    for line in lock.read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.split("|")]
        if len(cells) != 6 or cells[0] != "reference-disk":
            continue
        _, _, _, digest, relpath, _ = cells
        out[Path(relpath).name] = (relpath, digest)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dir", default="hdv",
                        help="directory to walk (default: hdv)")
    parser.add_argument("--root", default=".", help="source root")
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 if a declared reference disk is dirty "
                             "or has drifted from its recorded identity")
    parser.add_argument("--no-digest", action="store_true",
                        help="skip hashing (state only, no identity check)")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    target = (root / args.dir) if not Path(args.dir).is_absolute() else Path(args.dir)
    if not target.is_dir():
        print(f"no such directory: {target}", file=sys.stderr)
        return 2

    declared = reference_disks(root)
    images = sorted(p for p in target.rglob("*")
                    if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES)
    if not images:
        print(f"no disk images under {target}")
        return 0

    dirty_refs, drifted_refs, dirty, total = [], [], 0, 0
    for path in images:
        probed = probe_hfs(path)
        rel = path.relative_to(root) if path.is_relative_to(root) else path
        if probed is None:
            print(f"{'no HFS MDB':<13} {'':<8} {'':<26} {rel}")
            continue
        total += 1
        name, atrb = probed
        clean = bool(atrb & 0x0100)
        dirty += 0 if clean else 1

        identity = ""
        row = declared.get(path.name)
        if row and not args.no_digest:
            declared_path, digest = row
            same = sha256(path) == digest
            where = "" if str(rel) == declared_path else "  FLAT"
            identity = ("  MATCH" if same else "  DRIFTED") + where
            if not same:
                drifted_refs.append(str(rel))
            if not clean:
                dirty_refs.append(str(rel))

        state = "CLEAN" if clean else "*** DIRTY ***"
        print(f"{state:<13} atrb=0x{atrb:04x}  vol={name!r:<24} "
              f"{rel}{identity}")

    print(f"\n{total} HFS volume(s): {total - dirty} clean, {dirty} dirty; "
          f"{len(declared)} reference identity(ies) declared")
    if dirty_refs:
        print("reference disks NOT cleanly unmounted: " + ", ".join(dirty_refs))
    if drifted_refs:
        print("reference disks that no longer match assets.lock: " +
              ", ".join(drifted_refs))
    if args.strict and (dirty_refs or drifted_refs):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
