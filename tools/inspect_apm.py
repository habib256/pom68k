#!/usr/bin/env python3
"""Print an Apple-partitioned disk image's driver descriptor and map.

Written for one question: does this image carry the ATA driver a Quadra 630
demands? The ROM only accepts an ATA disk whose Driver Descriptor Record
holds a driver entry of type $0701, and it then loads that driver from the
disk (measured 2026-09-17; CHANGELOG). So the two things to look for are a
`$0701` entry in block 0 and an `Apple_Driver_ATA` partition in the map.

    tools/inspect_apm.py disk.img [--bytes N]

Reads only the head of the file, so it is safe on a 120 GB sparse image.
"""
import struct
import sys

BLOCK = 512


def pstr(b):
    return b.split(b"\0")[0].decode("mac-roman", "replace")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    head = open(path, "rb").read(BLOCK * 96)
    if len(head) < BLOCK * 2:
        print(f"{path}: shorter than two blocks")
        return 1

    sig = head[:2]
    print(f"{path}")
    if sig != b"ER":
        print("  block 0: no 'ER' driver descriptor record "
              f"(found {sig!r}) — a bare volume or a foreign image")
    else:
        blk_size, blk_count = struct.unpack_from(">HI", head, 2)
        dev_type, dev_id, data = struct.unpack_from(">HHI", head, 8)
        drivers, = struct.unpack_from(">H", head, 0x10)
        print(f"  DDR: {blk_size}-byte blocks, {blk_count} of them, "
              f"devType {dev_type} devId {dev_id} data {data}, "
              f"{drivers} driver(s)")
        for i in range(min(drivers, 61)):
            off = 0x12 + i * 8
            if off + 8 > len(head):
                break
            blk, size, typ = struct.unpack_from(">IHH", head, off)
            note = ""
            if typ == 0x0701:
                note = "  ← ATA: what the Quadra 630's ROM demands"
            elif typ == 0x0001:
                note = "  (SCSI)"
            elif typ == 0x6A:
                note = "  (ddType $6A — the LC II StartBoot entry)"
            print(f"    driver {i}: block {blk}, {size} blocks, "
                  f"type ${typ:04X}{note}")

    print("  partition map:")
    ata = False
    for i in range(1, 96):
        b = i * BLOCK
        if b + BLOCK > len(head) or head[b:b + 2] != b"PM":
            break
        map_blocks, start, count = struct.unpack_from(">III", head, b + 4)
        name = pstr(head[b + 16:b + 48])
        typ = pstr(head[b + 48:b + 80])
        status, = struct.unpack_from(">I", head, b + 88)
        flag = ""
        if typ == "Apple_Driver_ATA":
            flag = "  ← the driver a bootable IDE disk needs"
            ata = True
        print(f"    {i:2d}: {typ:<24} {name:<24} start {start:<8} "
              f"{count:<9} status ${status:08X}{flag}")
        if i >= map_blocks:
            break

    print()
    print("  verdict: " + ("carries an ATA driver partition"
                           if ata else "NO ATA driver partition"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
