#!/usr/bin/env python3
"""List the file directory of a Macintosh File System (MFS) volume image.

MFS is the flat file system of the 400 K System floppies the Macintosh 128K
and 512K boot (System 1.x-2.x). Layout, from Inside Macintosh II "The File
Manager: Data Organization on Volumes" (pp. II-119..II-123):

  sector 0-1   boot blocks
  sector 2     volume information (64 bytes) followed by the block map:
               one 12-bit entry per allocation block, numbered from 2
  drDirSt      file directory, drBlLen sectors of packed entries that never
               straddle a sector; an entry whose flFlags bit 7 is clear ends
               the entries of that sector
  drAlBlSt     first sector of the allocation blocks (block 2 starts there)

Usage: tools/mfs_ls.py <image.dsk> [--dump NAME [--rsrc]]
Prints one line per file: flags, type/creator, data and resource fork lengths,
creation/modification dates, name. --dump writes a fork to stdout.
"""
import argparse
import datetime
import struct
import sys

MAC_EPOCH = datetime.datetime(1904, 1, 1)


def mac_date(seconds):
    return (MAC_EPOCH + datetime.timedelta(seconds=seconds)).strftime("%Y-%m-%d %H:%M:%S")


def parse(image):
    if len(image) < 1024 + 64:
        raise ValueError("image too small for a volume information block")
    vib = image[1024:1024 + 64]
    (sig, cr, bk, atrb, nmfls, dirst, bllen, nmalblks, alblksiz, clpsiz,
     alblst, nxtfnum, freebks) = struct.unpack(">HIIHHHHHIIHIH", vib[:36])
    if sig != 0xD2D7:
        raise ValueError("not an MFS volume (signature %04X)" % sig)
    name = vib[37:37 + vib[36]].decode("mac_roman")
    # Block map: 12-bit entries for allocation blocks 2 .. nmalblks+1.
    bm = image[1024 + 64:]
    def entry(block):
        i = block - 2
        base = (i // 2) * 3
        if i % 2 == 0:
            return (bm[base] << 4) | (bm[base + 1] >> 4)
        return ((bm[base + 1] & 0x0F) << 8) | bm[base + 2]
    def fork(start, length):
        out = bytearray()
        blk = start
        guard = 0
        while blk not in (0, 1) and len(out) < length:
            if blk < 2 or blk >= nmalblks + 2:
                raise ValueError("fork chain leaves the volume at block %d" % blk)
            off = alblst * 512 + (blk - 2) * alblksiz
            out += image[off:off + alblksiz]
            blk = entry(blk)
            guard += 1
            if guard > nmalblks:
                raise ValueError("fork chain loops")
        return bytes(out[:length])
    files = []
    for sector in range(dirst, dirst + bllen):
        off = sector * 512
        end = off + 512
        while off + 51 <= end:
            flags = image[off]
            if not flags & 0x80:
                break
            (typ, fnum, stblk, lglen, pylen, rstblk, rlglen, rpylen, crdat,
             mddat) = struct.unpack(">BIHIIHIIII", image[off + 1:off + 1 + 33])
            # flTyp is at +1, flUsrWds (16 bytes) at +2, flFlNum at +18 ...
            usrwds = image[off + 2:off + 18]
            (fnum, stblk, lglen, pylen, rstblk, rlglen, rpylen, crdat,
             mddat) = struct.unpack(">IHIIHIIII", image[off + 18:off + 18 + 32])
            nlen = image[off + 50]
            fname = image[off + 51:off + 51 + nlen].decode("mac_roman")
            files.append(dict(flags=flags, type=usrwds[0:4], creator=usrwds[4:8],
                              fnum=fnum, data=(stblk, lglen, pylen),
                              rsrc=(rstblk, rlglen, rpylen), created=crdat,
                              modified=mddat, name=fname))
            size = 51 + nlen
            off += size + (size & 1)
    return dict(name=name, created=cr, backup=bk, attrs=atrb, count=nmfls,
                dir=(dirst, bllen), blocks=(nmalblks, alblksiz, alblst),
                next_file=nxtfnum, free=freebks, files=files, fork=fork)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image")
    ap.add_argument("--dump", metavar="NAME", help="write this file's fork to stdout")
    ap.add_argument("--rsrc", action="store_true", help="the resource fork instead of data")
    args = ap.parse_args()
    image = open(args.image, "rb").read()
    vol = parse(image)
    if args.dump:
        for f in vol["files"]:
            if f["name"] == args.dump:
                start, lglen, _ = f["rsrc"] if args.rsrc else f["data"]
                sys.stdout.buffer.write(vol["fork"](start, lglen))
                return 0
        print("no such file: " + args.dump, file=sys.stderr)
        return 1
    n, siz, st = vol["blocks"]
    print("volume '%s'  created %s  files %d (dir %d)  alloc %d x %d B from sector %d  free %d  next file # %d"
          % (vol["name"], mac_date(vol["created"]), vol["count"], len(vol["files"]),
             n, siz, st, vol["free"], vol["next_file"]))
    for f in vol["files"]:
        print("  %s %s/%s  data %7d  rsrc %7d  %s  %s  %s"
              % ("L" if f["flags"] & 1 else "-", f["type"].decode("mac_roman"),
                 f["creator"].decode("mac_roman"), f["data"][1], f["rsrc"][1],
                 mac_date(f["created"]), mac_date(f["modified"]), f["name"]))
    if vol["count"] != len(vol["files"]):
        print("WARNING: drNmFls says %d, directory holds %d" % (vol["count"], len(vol["files"])))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
