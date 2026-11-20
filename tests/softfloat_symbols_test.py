#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
"""Every external symbol of the vendored SoftFloat carries the private prefix.

extern/softfloat/softfloat-prefix.h renames the library's exports so that it
links next to another SoftFloat-2a (upstream Moira's test runner links
Musashi, which bundles one). A function added to the library without a line
in that header would export a bare name again; this gate lists them.

    softfloat_symbols_test.py --nm NM --lib libsoftfloat68k.a
"""
import argparse
import subprocess
import sys

PREFIX = "pom68k_sf_"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--lib", required=True)
    args = ap.parse_args()
    out = subprocess.run([args.nm, "-g", "--defined-only", args.lib],
                         check=True, capture_output=True, text=True).stdout
    names = set()
    for line in out.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1].isupper():
            name = fields[2]
            # Mach-O prepends an underscore to every C symbol.
            names.add(name[1:] if name.startswith("_" + PREFIX) else name)
    if not names:
        print("softfloat_symbols_test: FAIL: no exported symbol found in", args.lib)
        return 1
    bare = sorted(n for n in names if not n.startswith(PREFIX))
    if bare:
        print("softfloat_symbols_test: FAIL: exports without the prefix "
              "(add them to extern/softfloat/softfloat-prefix.h):")
        for n in bare:
            print("  " + n)
        return 1
    print(f"softfloat_symbols_test: OK: {len(names)} exports, all {PREFIX}*")
    return 0


if __name__ == "__main__":
    sys.exit(main())
