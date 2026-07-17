#!/usr/bin/env python3
# POM68K — wrap a bare HFS volume image ('LK' boot blocks at block 0)
# into a bootable Apple SCSI disk image: Driver Descriptor Map ('ER') +
# Apple partition map + Apple_Driver43 partition + the HFS volume.
# The DDM/map/driver template is lifted from an existing well-formed
# image (default hdv/boot.vhd, Apple_Driver43 layout @64+32).
#
# The LC II ROM's boot scan only loads a driver whose DDM entry has
# ddType $6A (probed against the real ROM at $A07264 — a lone type
# $0001 entry is ignored and the ROM keeps showing the blinking ?), so
# the driver entry is mirrored with that type.
#
# Usage: wrap_hfs.py <bare_hfs.vhd> <output.vhd> [template.vhd]

import struct
import sys


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    tpl = sys.argv[3] if len(sys.argv) > 3 else 'hdv/boot.vhd'

    hfs = open(src, 'rb').read()
    if hfs[:2] != b'LK':
        print(f'{src}: no LK boot blocks at 0 — is it a bare HFS volume?')
        return 1
    if len(hfs) % 512:
        print(f'{src}: not a multiple of 512 bytes')
        return 1
    hfs_blocks = len(hfs) // 512

    t = open(tpl, 'rb').read(512 * 96)       # DDM + map + driver
    if t[:2] != b'ER':
        print(f'{tpl}: no DDM — bad template')
        return 1

    head = bytearray(t)
    total = 96 + hfs_blocks

    # DDM: device size
    struct.pack_into('>I', head, 4, total)

    # Driver entry with ddType $6A (see header comment)
    blk, size, typ = struct.unpack('>IHH', head[0x12:0x1A])
    if typ != 0x6A:
        struct.pack_into('>IHH', head, 0x1A, blk, size, 0x6A)
        struct.pack_into('>H', head, 0x10, 2)      # sbDrvrCount = 2

    # Patch the Apple_HFS partition entry to cover the appended volume
    i = 1
    while i < 64:
        b = i * 512
        if head[b:b+2] != b'PM':
            break
        typ_s = bytes(head[b+48:b+80]).rstrip(b'\0')
        if typ_s == b'Apple_HFS':
            struct.pack_into('>I', head, b+8, 96)          # pmPyPartStart
            struct.pack_into('>I', head, b+12, hfs_blocks) # pmPartBlkCnt
            struct.pack_into('>I', head, b+84, hfs_blocks) # pmDataCnt
        i += 1

    with open(dst, 'wb') as f:
        f.write(head)
        f.write(hfs)
    print(f'{dst}: {total} blocks (driver template from {tpl}, '
          f'HFS {hfs_blocks} blocks at 96, ddType $6A entry added)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
