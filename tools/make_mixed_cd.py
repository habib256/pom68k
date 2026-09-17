#!/usr/bin/env python3
"""Synthesize a mixed-mode CD image (.cue + .bin) for the CDDA work.

A flat 2048-byte image cannot carry an audio track at all, so the CD-audio
gates need a raw 2352-byte disc: track 1 MODE1/2352 holding a data volume,
then CD-DA tracks. Real mixed-mode discs are other people's music; this
builds an equivalent from nothing, so the asset is reproducible, tiny and
free of any rights question (TODO § Médias optiques, CHANGELOG 2026-09-17).

    tools/make_mixed_cd.py out.cue --data volume.iso --tone 440:2 --tone 660:2

Each --tone is frequency_hz:seconds, written as 44.1 kHz 16-bit stereo.
MODE1/2352 framing is sync + header (MSF, mode 1) + 2048 user bytes + a
zeroed EDC/ECC tail, which is what a drive hands back when the initiator
asks for raw sectors; POM68K only ever reads the user 2048 back out.
"""
import argparse, math, struct, sys

RAW, USER, AUDIO_RATE = 2352, 2048, 44100
SYNC = bytes([0x00] + [0xFF] * 10 + [0x00])

def bcd(v): return ((v // 10) << 4) | (v % 10)

def mode1(user: bytes, lba: int) -> bytes:
    f = lba + 150                      # sector 0 is at MSF 00:02:00
    head = bytes([bcd(f // (60 * 75)), bcd((f // 75) % 60), bcd(f % 75), 0x01])
    return SYNC + head + user.ljust(USER, b"\0") + b"\0" * (RAW - 16 - USER)

def tone(freq: float, secs: float) -> bytes:
    n = int(AUDIO_RATE * secs)
    n -= n % 588                        # whole 2352-byte sectors
    out = bytearray()
    for i in range(n):
        s = int(20000 * math.sin(2 * math.pi * freq * i / AUDIO_RATE))
        out += struct.pack("<hh", s, s)
    return bytes(out)

def msf(lba: int) -> str:
    f = lba + 150
    return "%02d:%02d:%02d" % (f // (60 * 75), (f // 75) % 60, f % 75)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cue")
    ap.add_argument("--data", required=True, help="2048-byte-sector volume for track 1")
    ap.add_argument("--tone", action="append", default=[], metavar="HZ:SECONDS")
    args = ap.parse_args()

    data = open(args.data, "rb").read()
    if len(data) % USER:
        print("data image is not a whole number of 2048-byte sectors", file=sys.stderr)
        return 1

    binPath = args.cue[:-4] + ".bin" if args.cue.endswith(".cue") else args.cue + ".bin"
    lba, blob, cue = 0, bytearray(), ["FILE \"%s\" BINARY" % binPath.split("/")[-1]]
    cue += ["  TRACK 01 MODE1/2352", "    INDEX 01 %s" % msf(0)]
    for s in range(len(data) // USER):
        blob += mode1(data[s * USER:(s + 1) * USER], lba); lba += 1

    for n, spec in enumerate(args.tone, start=2):
        hz, secs = spec.split(":")
        pcm = tone(float(hz), float(secs))
        lba += 150                                  # 2 s pregap before audio
        blob += b"\0" * (150 * RAW)
        cue += ["  TRACK %02d AUDIO" % n, "    INDEX 01 %s" % msf(lba)]
        blob += pcm; lba += len(pcm) // RAW

    open(binPath, "wb").write(bytes(blob))
    open(args.cue, "w").write("\n".join(cue) + "\n")
    print("%s: %d tracks, %d sectors (%.1f MB)"
          % (args.cue, 1 + len(args.tone), lba, len(blob) / 1e6))
    return 0

if __name__ == "__main__":
    sys.exit(main())
