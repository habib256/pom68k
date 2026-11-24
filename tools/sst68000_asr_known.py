#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Write the @SR lines of tests/sst68000.known from the SingleStepTests corpus.

  sst68000_asr_known.py tests/data/sst68000 > rows   (then paste under the header)

For every register-form ASR vector whose shift count exceeds the operand width,
the manual's X and C are the last bit shifted out ([B] M68000PRM-1992 ASR, PDF
p. 126: "Set according to the last bit shifted out of the operand"; the sign
is shifted in, so past the width every bit shifted out is the sign). The
corpus clears both. A line is written where the two disagree: the corpus's
final SR with X and C set to the sign. Reads the corpus only, never the core.
"""
import json
import sys


def main():
    rows = 0
    for size, width in (("b", 8), ("w", 16), ("l", 32)):
        with open(f"{sys.argv[1]}/ASR.{size}.json", encoding="utf-8") as f:
            vectors = json.load(f)
        for v in vectors:
            op = v["initial"]["prefetch"][0]
            # 1110 ccc 0 ss i 00 rrr: ASR, register form
            if op >> 12 != 0xE or (op >> 6) & 3 == 3 or (op >> 8) & 1 or (op >> 3) & 3:
                continue
            reg = (op >> 9) & 7
            count = v["initial"][f"d{reg}"] % 64 if (op >> 5) & 1 else (reg or 8)
            if count <= width:
                continue
            sign = (v["initial"][f"d{op & 7}"] >> (width - 1)) & 1
            corpus = v["final"]["sr"]
            manual = (corpus & ~0x11) | (0x11 if sign else 0)
            if manual != corpus:
                print(f"@SR {manual:04x} | {v['name']}")
                rows += 1
    print(f"{rows} rows", file=sys.stderr)


if __name__ == "__main__":
    main()
