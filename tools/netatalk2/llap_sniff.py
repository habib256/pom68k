#!/usr/bin/env python3
"""What actually crosses the LToUDP cable, decoded.

Joins the same multicast group the emulator, TashRouter and any other
LToUDP node use (239.192.76.84:1954) and prints one line per frame: the
LLAP addresses, the DDP header, and for NBP the function and its tuples —
which is what says whether a lookup was answered and by whom.

No privileges: it is an ordinary multicast listener, so it can run beside a
session without disturbing it (a node ignores datagrams carrying another
instance's sender tag).

    tools/netatalk2/llap_sniff.py [seconds] [--quiet] [--checksum]

  --quiet     drop the router's RTMP heartbeat and LLAP ENQ storms
  --checksum  verify each long-header datagram's DDP checksum the way a Mac
              does, and say so. A carried 0 means "not computed" and is
              never verified; a mismatch is silently discarded by the
              guest, which looks exactly like a server that never answered.

It earned its place on 2026-09-18: the Chooser listed no file server while
this showed the whole exchange — BrRq out, the router's LkUp, and afpd's
LkUp-Reply coming back with a correct checksum. The frames were on the
cable; what dropped them was the guest's own half-duplex receiver window
(CHANGELOG 2026-09-18, Scc8530 "receiver off = no ear").
"""
import socket
import struct
import sys
import time

GROUP, PORT = '239.192.76.84', 1954
NBP_FUNC = {1: 'BrRq', 2: 'LkUp', 3: 'LkUp-Reply', 4: 'FwdReq'}
DDP_TYPE = {1: 'RTMP-data', 2: 'NBP', 3: 'ATP', 4: 'AEP', 5: 'RTMP-request',
            6: 'ZIP', 7: 'ADSP'}


def ddp_checksum(body):
    """Inside Macintosh: Networking — add each byte, rotate left one bit."""
    acc = 0
    for byte in body:
        acc = (acc + byte) & 0xFFFF
        acc = ((acc << 1) | (acc >> 15)) & 0xFFFF
    return acc or 0xFFFF


def pascal(data, offset):
    length = data[offset]
    return data[offset + 1:offset + 1 + length].decode('mac-roman', 'replace'), \
        offset + 1 + length


def decode(frame, checksum):
    if len(frame) < 3:
        return 'runt'
    dst, src, lap = frame[0], frame[1], frame[2]
    if lap == 0x81:
        return f'{src}->{dst} ENQ'
    if lap == 0x82:
        return f'{src}->{dst} ACK'
    if lap == 0x84:
        return f'{src}->{dst} RTS'
    if lap == 0x85:
        return f'{src}->{dst} CTS'
    if lap not in (1, 2):
        return f'{src}->{dst} lap=${lap:02X}'
    ddp = frame[3:]
    note = ''
    if lap == 1:                       # short header: no nets, no checksum
        if len(ddp) < 5:
            return 'short-runt'
        dsock, ssock, dtype, body = ddp[2], ddp[3], ddp[4], ddp[5:]
        route = 'local'
    else:                              # long header
        if len(ddp) < 13:
            return 'long-runt'
        length = struct.unpack('>H', ddp[0:2])[0] & 0x3FF
        carried = struct.unpack('>H', ddp[2:4])[0]
        dnet, snet = struct.unpack('>HH', ddp[4:8])
        dnode, snode, dsock, ssock, dtype = ddp[8], ddp[9], ddp[10], ddp[11], ddp[12]
        body = ddp[13:]
        route = f'{snet}.{snode}->{dnet}.{dnode}'
        if checksum and len(ddp) >= length:
            if carried == 0:
                note = ' checksum=none'
            else:
                computed = ddp_checksum(ddp[4:length])
                note = (f' checksum=${carried:04X} OK' if computed == carried
                        else f' checksum=${carried:04X} MISMATCH '
                             f'(computed ${computed:04X}) — a Mac DISCARDS this')
    line = (f'{src}->{dst} [{route}] {DDP_TYPE.get(dtype, f"type{dtype}")} '
            f'sock {ssock}->{dsock}{note}')
    if dtype == 2 and len(body) >= 2:
        func, count = body[0] >> 4, body[0] & 0x0F
        line += f' {NBP_FUNC.get(func, func)} x{count}'
        offset = 2
        for _ in range(count):
            if offset + 5 > len(body):
                break
            tnet, tnode, tsock = struct.unpack('>HBB', body[offset:offset + 4])
            offset += 5                # net, node, socket, enumerator
            try:
                obj, offset = pascal(body, offset)
                typ, offset = pascal(body, offset)
                zone, offset = pascal(body, offset)
            except (IndexError, UnicodeDecodeError):
                break
            line += f' | {obj}:{typ}@{zone} at {tnet}.{tnode}:{tsock}'
    return line


def main():
    seconds = 600
    quiet = '--quiet' in sys.argv
    checksum = '--checksum' in sys.argv
    for arg in sys.argv[1:]:
        if not arg.startswith('-'):
            seconds = int(arg)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', PORT))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                    struct.pack('4s4s', socket.inet_aton(GROUP),
                                socket.inet_aton('0.0.0.0')))
    sock.settimeout(1.0)
    print(f'sniffing {GROUP}:{PORT} for {seconds}s', flush=True)
    deadline, seen = time.time() + seconds, 0
    while time.time() < deadline:
        try:
            data, _ = sock.recvfrom(2048)
        except socket.timeout:
            continue
        seen += 1
        tag = struct.unpack('>I', data[:4])[0]   # per-instance sender tag
        text = decode(data[4:], checksum)
        if quiet and ('ENQ' in text or 'RTMP-data' in text):
            continue
        print(f'{time.strftime("%H:%M:%S")} tag={tag:08X} {text}', flush=True)
    print(f'{seen} datagrams seen', flush=True)


if __name__ == '__main__':
    main()
