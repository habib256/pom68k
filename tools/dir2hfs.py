#!/usr/bin/env python3
# POM68K — bake a host directory into mountable classic-HFS volume(s).
#
# The output is a *bare* data-only HFS volume (zero boot blocks — NOT
# bootable, so StartBoot's 6→0 scan skips it); ScsiDisk::open's flat-HFS
# façade (DDM + partition map synthesized at attach time, detected via the
# MDB 'BD' signature) mounts it as a SCSI disk. Attach it as a
# secondary drive (GUI "Disques" menu, or `./POM68K ROM boot.vhd INPUT-1.vhd`)
# and the volume appears on the desktop. Write-back works: the guest may
# unstuff/save onto it and the flat image keeps the changes.
#
# Content handling (classic Mac transport formats):
#   .bin        MacBinary I/II → decoded to native data+resource forks with
#               the embedded Type/Creator (usable immediately, no unstuffing).
#   .zip        expanded host-side; members are processed recursively
#               (__MACOSX AppleDouble members are skipped).
#   .sit .hqx   kept as-is with StuffIt Expander Type/Creator — resource
#               forks survive inside the archive; unstuff in the guest.
#   .img        DiskCopy 4.2 Type/Creator (mount with DiskCopy/ShrinkWrap).
#   .toast .cdr .iso  SKIPPED by default (attach those directly as SCSI
#               disks — they are CD/disk images); include with --all.
#
# HFS limits honoured: 2 GiB volume ceiling (65535 allocation blocks), 31-char
# MacRoman names (sanitized + deduped). Files are first-fit packed into
# INPUT-1.vhd, INPUT-2.vhd, … capped at --max-mb (default 1900).
#
# Requires `machfs` (pip). Repo convention: python3 -m venv .venv-tools &&
# .venv-tools/bin/pip install machfs; run with .venv-tools/bin/python.
#
# Usage:
#   dir2hfs.py <src-dir> [out-prefix] [--max-mb N] [--only GLOB] [--all]
#   dir2hfs.py --selftest        # fixture bake + re-read (CTest gate)

import fnmatch
import io
import struct
import sys
import zipfile
from pathlib import Path

try:
    import machfs
except ImportError:
    machfs = None

KB = 1024
MB = 1024 * KB
ALLOC_MAX = 65535           # HFS allocation-block count ceiling
NAME_MAX = 31

TYPE_BY_EXT = {
    '.sit': (b'SIT!', b'SIT!'),
    '.hqx': (b'TEXT', b'SITx'),
    '.img': (b'dImg', b'dCpy'),
    '.image': (b'dImg', b'dCpy'),
    '.txt': (b'TEXT', b'ttxt'),
    '.mov': (b'MooV', b'TVOD'),
}
SKIP_EXT = {'.toast', '.cdr', '.iso'}


def macbinary_decode(raw):
    """Return (name, type, creator, data, rsrc) or None if not MacBinary."""
    if len(raw) < 128:
        return None
    h = raw[:128]
    namelen = h[1]
    if h[0] != 0 or not (1 <= namelen <= 63) or h[74] != 0:
        return None
    dlen, rlen = struct.unpack('>II', h[83:91])
    if 128 + ((dlen + 127) & ~127) + rlen > len(raw) or dlen > 0x7FFFFF00:
        return None
    name = h[2:2 + namelen].decode('mac_roman', 'replace')
    ftype, creator = h[65:69], h[69:73]
    data = raw[128:128 + dlen]
    roff = 128 + ((dlen + 127) & ~127)
    rsrc = raw[roff:roff + rlen]
    return name, ftype, creator, data, rsrc


def sanitize(name, used):
    name = name.replace(':', '-').strip()
    name = name.encode('mac_roman', 'replace').decode('mac_roman')
    if len(name) > NAME_MAX:                      # keep the extension visible
        stem, dot, ext = name.rpartition('.')
        if dot and len(ext) <= 6:
            name = stem[:NAME_MAX - len(ext) - 1] + '.' + ext
        else:
            name = name[:NAME_MAX]
    base = name
    n = 2
    while name.lower() in used:
        suffix = f'~{n}'
        name = base[:NAME_MAX - len(suffix)] + suffix
        n += 1
    used.add(name.lower())
    return name


def gather(src, only, include_all, cd_out=None):
    """Yield (hfs-name-hint, type, creator, data, rsrc) for every payload.
    CD/disk images are dropped from the volume; when cd_out is set they are
    written there as flat files (attachable directly as SCSI disks)."""
    out = []

    def add_payload(name, raw):
        ext = Path(name).suffix.lower()
        if ext in SKIP_EXT and not include_all:
            if cd_out is not None:
                dst = Path(cd_out) / Path(name).name
                if not dst.exists():
                    dst.write_bytes(raw)
                print(f'  CD/disk image → {dst} (attach directly)')
            else:
                print(f'  skip (CD/disk image, attach directly): {name}')
            return
        mb = macbinary_decode(raw) if ext in ('.bin', '.macbin') else None
        if mb:
            mname, ftype, creator, data, rsrc = mb
            out.append((mname, ftype, creator, data, rsrc))
            return
        ftype, creator = TYPE_BY_EXT.get(ext, (b'BINA', b'????'))
        out.append((name, ftype, creator, raw, b''))

    for p in sorted(src.iterdir()):
        if not p.is_file():
            continue
        if only and not fnmatch.fnmatch(p.name.lower(), only.lower()):
            continue
        if p.suffix.lower() == '.zip':
            try:
                with zipfile.ZipFile(p) as z:
                    for m in z.infolist():
                        base = Path(m.filename).name
                        if (m.is_dir() or not base or base.startswith('.')
                                or '__MACOSX' in m.filename):
                            continue
                        add_payload(base, z.read(m))
            except zipfile.BadZipFile:
                print(f'  bad zip, kept raw: {p.name}')
                add_payload(p.name, p.read_bytes())
        else:
            add_payload(p.name, p.read_bytes())
    return out


def bake(volname, files, out_path):
    v = machfs.Volume()
    v.name = volname
    used = set()
    total = 0
    for name, ftype, creator, data, rsrc in files:
        f = machfs.File()
        f.type, f.creator = ftype, creator
        f.data, f.rsrc = data, rsrc
        v[sanitize(name, used)] = f
        total += len(data) + len(rsrc)
    # Volume size: payload + catalog/bitmap headroom, 512-aligned, and an
    # allocation block small enough to stay under 65535 blocks.
    size = max(2 * MB, int(total * 1.05) + 4 * MB)
    size = (size + 511) & ~511
    image = bytearray(v.write(size=size, align=512, desktopdb=True,
                              bootable=False))
    # Boot blocks stay ZERO (data-only volume): ScsiDisk's façade detects the
    # bare volume by the MDB 'BD' signature at $400. Do NOT stamp 'LK' —
    # StartBoot scans SCSI 6→0, would see this higher-ID volume first,
    # believe the LK and jump into zeroed boot blocks (ROM serial-debugger
    # stub, boot dead before video).
    Path(out_path).write_bytes(image)
    print(f'  {out_path}: {volname!r}, {len(files)} files, '
          f'{len(image) // MB} MB')


def selftest():
    if machfs is None:
        print('SKIP: machfs not installed (.venv-tools)')
        return 0
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        # Fixture: a MacBinary file (type/creator + both forks) and a .sit.
        name = b'Fixture App'
        data, rsrc = b'D' * 300, b'R' * 200
        h = bytearray(128)
        h[1] = len(name)
        h[2:2 + len(name)] = name
        h[65:69], h[69:73] = b'APPL', b'TEST'
        h[83:91] = struct.pack('>II', len(data), len(rsrc))
        pad = (-len(data)) % 128
        (td / 'fixture.bin').write_bytes(bytes(h) + data + b'\0' * pad + rsrc)
        (td / 'game.sit').write_bytes(b'SIT!' + b'x' * 100)
        (td / 'movie.toast').write_bytes(b'y' * 100)         # must be skipped
        files = gather(td, None, False)
        assert len(files) == 2, files
        out = td / 'TEST.vhd'
        bake('SELFTEST', files, out)
        img = out.read_bytes()
        assert img[:2] == b'\0\0', 'boot blocks must stay zero (data-only)'
        assert img[1024:1026] == b'BD', 'HFS MDB signature missing'
        v = machfs.Volume()
        v.read(img)
        # machfs read() drops drVN (upstream quirk) — check the MDB directly:
        # 28p Pascal string at MDB+36 ($424).
        namelen = img[0x424]
        assert img[0x425:0x425 + namelen] == b'SELFTEST', img[0x424:0x444]
        f = v['Fixture App']
        assert (f.type, f.creator) == (b'APPL', b'TEST')
        assert f.data == data and f.rsrc == rsrc
        assert v['game.sit'].type == b'SIT!'
    print('PASS: dir2hfs selftest (MacBinary decode, data-only bake, round-trip)')
    return 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    if machfs is None:
        print('machfs missing: python3 -m venv .venv-tools && '
              '.venv-tools/bin/pip install machfs')
        return 2
    args = [a for a in argv if not a.startswith('--')]
    if not args:
        print(__doc__ or 'usage: dir2hfs.py <src-dir> [out-prefix] '
              '[--max-mb N] [--only GLOB] [--all]')
        return 2
    src = Path(args[0])
    prefix = args[1] if len(args) > 1 else 'hdv/INPUT'
    max_mb = 1900
    only = None
    if '--max-mb' in argv:
        max_mb = int(argv[argv.index('--max-mb') + 1])
    if '--only' in argv:
        only = argv[argv.index('--only') + 1]
    include_all = '--all' in argv

    out_dir = Path(prefix).parent
    out_dir.mkdir(parents=True, exist_ok=True)
    files = gather(src, only, include_all,
                   cd_out=None if include_all else out_dir)
    if not files:
        print('nothing to bake')
        return 1
    # First-fit split under the per-volume cap.
    cap = max_mb * MB
    vols, cur, cur_sz = [], [], 0
    for f in files:
        sz = len(f[3]) + len(f[4]) + 64 * KB
        if sz > cap:
            print(f'  too big for one volume, skipped: {f[0]}')
            continue
        if cur and cur_sz + sz > cap:
            vols.append(cur)
            cur, cur_sz = [], 0
        cur.append(f)
        cur_sz += sz
    if cur:
        vols.append(cur)
    for i, chunk in enumerate(vols, 1):
        suffix = f'-{i}' if len(vols) > 1 else ''
        bake(f'INPUT{suffix}', chunk, f'{prefix}{suffix}.vhd')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
