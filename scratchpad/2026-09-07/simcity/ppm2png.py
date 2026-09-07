#!/usr/bin/env python3
import struct, sys, zlib
def conv(src, dst):
    d = open(src, 'rb').read()
    parts = d.split(b'\n', 3)
    assert parts[0] == b'P6'
    w, h = map(int, parts[1].split()); px = parts[3]
    raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)
for s in sys.argv[1:]: conv(s, s.rsplit('.', 1)[0] + '.png')
