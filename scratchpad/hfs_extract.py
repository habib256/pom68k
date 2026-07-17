#!/usr/bin/env python3
# Minimal HFS (classic, not HFS+) reader: list catalog files, extract a
# file's resource fork, and dump its CODE resources.
import struct, sys

def be16(b, o): return struct.unpack_from('>H', b, o)[0]
def be32(b, o): return struct.unpack_from('>I', b, o)[0]

def find_hfs_partition(img):
    if img[:2] == b'LK' or be16(img, 1024) == 0x4244:
        return 0                       # bare volume
    # Apple partition map
    i = 1
    while i < 64:
        b = i * 512
        if img[b:b+2] != b'PM': break
        typ = img[b+48:b+80].rstrip(b'\0')
        if typ == b'Apple_HFS':
            return be32(img, b+8) * 512
        i += 1
    raise SystemExit('no Apple_HFS partition')

class Vol:
    def __init__(self, img, base):
        self.img, self.base = img, base
        mdb = base + 1024
        assert be16(img, mdb) == 0x4244, 'no MDB'
        self.alBlkSiz = be32(img, mdb+20)
        self.alBlSt   = be16(img, mdb+28)
        self.ctExt    = [(be16(img, mdb+150+i*4), be16(img, mdb+152+i*4)) for i in range(3)]

    def ablock(self, n):
        return self.base + (self.alBlSt*512) + n*self.alBlkSiz

    def read_extents(self, extrec, length):
        out = bytearray()
        for start, cnt in extrec:
            if cnt == 0: continue
            off = self.ablock(start)
            out += self.img[off : off + cnt*self.alBlkSiz]
        return bytes(out[:length])

    def catalog_leaves(self):
        # catalog file bytes (first 3 extents are enough for the header + walk)
        cat = self.read_extents(self.ctExt, 10**9)
        nodesz = be16(cat, 32)   # hdr node: desc(14) + depth(2) root(4) nrecs(4) fnode(4) lnode(4) → nodeSize @ 32
        # bt header: after 14-byte node desc: depth(2) root(4) nrecs(4) fnode(4) lnode(4) nodesize(2)
        fnode = be32(cat, 14+2+4+4)
        n = fnode
        while n:
            nd = n * nodesz
            flink = be32(cat, nd)
            nrecs = be16(cat, nd+10)
            for r in range(nrecs):
                roff = be16(cat, nd + nodesz - 2*(r+1))
                yield cat, nd + roff
            n = flink

    def files(self):
        for cat, ro in self.catalog_leaves():
            klen = cat[ro]
            name = cat[ro+7 : ro+7+cat[ro+6]].decode('mac_roman', 'replace')
            do = ro + 1 + klen
            if do % 2: do += 1
            if cat[do] == 2:           # file record
                f = {
                    'name': name,
                    'type': cat[do+4:do+8].decode('mac_roman','replace'),
                    'creator': cat[do+8:do+12].decode('mac_roman','replace'),
                    'dataLen': be32(cat, do+26),
                    'rsrcLen': be32(cat, do+36),
                    'extData': [(be16(cat,do+74+i*4), be16(cat,do+76+i*4)) for i in range(3)],
                    'extRsrc': [(be16(cat,do+86+i*4), be16(cat,do+88+i*4)) for i in range(3)],
                }
                yield f

def resources(fork):
    dataOff, mapOff = be32(fork,0), be32(fork,4)
    tlOff = be16(fork, mapOff+24)
    count = (be16(fork, mapOff+tlOff) + 1) & 0xFFFF
    for t in range(count):
        e = mapOff + tlOff + 2 + t*8
        rtype = fork[e:e+4]
        n = be16(fork, e+4) + 1
        rlo = be16(fork, e+6)
        for r in range(n):
            re_ = mapOff + tlOff + rlo + r*12
            rid = struct.unpack_from('>h', fork, re_)[0]
            doff = be32(fork, re_+4) & 0xFFFFFF
            dlen = be32(fork, dataOff + doff)
            yield rtype, rid, fork[dataOff+doff+4 : dataOff+doff+4+dlen]

if __name__ == '__main__':
    img = open(sys.argv[1], 'rb').read()
    vol = Vol(img, find_hfs_partition(img))
    pattern = sys.argv[2] if len(sys.argv) > 2 else None
    for f in vol.files():
        if pattern and pattern.lower() not in f['name'].lower(): continue
        print(f"{f['name']!r:42} type={f['type']} creator={f['creator']} "
              f"data={f['dataLen']} rsrc={f['rsrcLen']}")
        if len(sys.argv) > 3 and sys.argv[3] == 'extract' and f['rsrcLen']:
            fork = vol.read_extents(f['extRsrc'], f['rsrcLen'])
            safe = f['name'].replace('/','_').replace(' ','_')
            for rtype, rid, data in resources(fork):
                if rtype == b'CODE':
                    fn = f"{safe}.CODE.{rid}.bin"
                    open(fn,'wb').write(data)
                    print(f"  CODE {rid}: {len(data)} bytes -> {fn}")
