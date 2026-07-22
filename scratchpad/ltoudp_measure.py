#!/usr/bin/env python3
# Passive LToUDP throughput/latency probe for POM68K AppleShare transfers.
# Joins the LToUDP multicast (239.192.76.84:1954), timestamps every frame,
# decodes the 3-byte LLAP header (dst, src, type), and on stop reports:
#   - end-to-end throughput (payload bytes / wall time)
#   - packet rate and size distribution
#   - inter-packet gap distribution  -> latency-bound vs bandwidth-bound
#   - request->response round-trip latency (X->Y frame followed by Y->X)
# Non-invasive: no restart of the router/afpd/emulator needed.
import socket, struct, sys, time, signal

GROUP, PORT = "239.192.76.84", 1954
recs = []  # (t_monotonic, length, dst, src, typ)

def stop(*_):
    report()
    sys.exit(0)

def report():
    if len(recs) < 2:
        print("no traffic captured"); return
    t0 = recs[0][0]; t1 = recs[-1][0]
    dur = t1 - t0
    payload = sum(r[1] for r in recs)          # LLAP bytes (excludes 4B tag)
    print(f"\n=== LToUDP capture: {len(recs)} frames over {dur:.3f}s ===")
    print(f"payload total : {payload} B  ({payload/1024:.1f} KiB)")
    if dur > 0:
        print(f"throughput    : {payload/dur/1024:.1f} KiB/s   ({payload*8/dur/1000:.1f} kbit/s)")
        print(f"frame rate    : {len(recs)/dur:.0f} frames/s")
    # frame size distribution
    from collections import Counter
    sizes = Counter(r[1] for r in recs)
    print("frame sizes   : " + ", ".join(f"{s}B x{c}" for s,c in sorted(sizes.items(), key=lambda x:-x[1])[:8]))
    # type distribution
    types = Counter(r[4] for r in recs)
    tn = {0x01:"sDDP",0x02:"lDDP",0x81:"ENQ",0x82:"ACK",0x84:"RTS",0x85:"CTS"}
    print("llap types    : " + ", ".join(f"{tn.get(t,hex(t))} x{c}" for t,c in sorted(types.items(), key=lambda x:-x[1])))
    # inter-frame gaps
    gaps = sorted((recs[i][0]-recs[i-1][0])*1e3 for i in range(1,len(recs)))
    def pct(p): return gaps[min(len(gaps)-1, int(len(gaps)*p))]
    print(f"inter-frame ms: p50={pct(.5):.3f}  p90={pct(.9):.3f}  p99={pct(.99):.3f}  max={gaps[-1]:.3f}")
    total_gap = sum(gaps)
    big = [g for g in gaps if g > 2.0]
    print(f"gap time>2ms  : {sum(big):.0f}ms in {len(big)} gaps  ({sum(big)/1000/dur*100:.0f}% of wall)" if dur else "")
    # round-trip: a frame X->Y (data/RTS) immediately followed by Y->X
    rtts = []
    for i in range(1, len(recs)):
        _,_,d0,s0,_ = recs[i-1]; t_prev = recs[i-1][0]
        _,_,d1,s1,_ = recs[i];    t_cur  = recs[i][0]
        if d1 == s0 and s1 == d0:           # reply direction
            rtts.append((t_cur - t_prev)*1e3)
    if rtts:
        rtts.sort()
        print(f"reply latency : n={len(rtts)}  p50={rtts[len(rtts)//2]:.3f}ms  p90={rtts[int(len(rtts)*.9)]:.3f}ms  max={rtts[-1]:.3f}ms")
    print("\nverdict: " + (
        "LATENCY-bound (host round-trip dominates; gaps >> frame time = Python router / poll)"
        if total_gap and sum(big) > total_gap*0.5 else
        "BANDWIDTH-bound (frames back-to-back; the wire/CPU pace is the limit)"))

def main():
    dur_limit = float(sys.argv[1]) if len(sys.argv) > 1 else 0
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", PORT))
    mreq = struct.pack("4sl", socket.inet_aton(GROUP), socket.INADDR_ANY)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    s.settimeout(0.5)
    signal.signal(signal.SIGINT, stop); signal.signal(signal.SIGTERM, stop)
    print(f"listening on {GROUP}:{PORT} — copy a file now, Ctrl-C (or {dur_limit}s) to stop", flush=True)
    start = time.monotonic()
    while True:
        try:
            data, _ = s.recvfrom(2048)
        except socket.timeout:
            if dur_limit and time.monotonic()-start > dur_limit: stop()
            continue
        if len(data) < 4+3:  # 4B tag + 3B LLAP header
            continue
        llap = data[4:]
        recs.append((time.monotonic(), len(llap), llap[0], llap[1], llap[2]))
        if dur_limit and time.monotonic()-start > dur_limit: stop()

if __name__ == "__main__":
    main()
