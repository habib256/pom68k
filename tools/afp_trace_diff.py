#!/usr/bin/env python3
"""Compare two q605_afp_live_etalon traces boundary by boundary.

The gate prints one `trace:` line per phase boundary — machine clock,
architectural fingerprint, cumulative AFP/network/wire counters. Two runs that
first disagree at boundary N diverged before N; that is how a guest-time
difference between two hosts, or two engines, is localised (CHANGELOG
2026-09-11 (fourth)).

    tools/afp_trace_diff.py reference.txt other.log

Either side may be a saved trace file or a raw `ctest -V` log (the `NNN: `
prefix is stripped). `refused=`, added on 2026-09-12, is ignored so a
reference recorded before it still compares.
"""
import re
import sys


def load(path):
    rows = []
    with open(path, errors="replace") as source:
        for line in source:
            line = re.sub(r"^\d+: ", "", line.strip())
            if not line.startswith("trace:"):
                continue
            parts = line.split()
            if len(parts) < 2:
                raise ValueError(f"{path}: trace boundary has no name")
            fields = dict(p.split("=", 1) for p in parts[2:] if "=" in p)
            fields.pop("refused", None)
            rows.append((parts[1], fields))
    if not rows:
        raise ValueError(f"{path}: no trace boundaries found")
    return rows


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    try:
        a, b = load(sys.argv[1]), load(sys.argv[2])
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2
    print(f"{len(a)} vs {len(b)} boundaries")
    first = None
    for i, ((na, ka), (nb, kb)) in enumerate(zip(a, b)):
        diffs = [k for k in dict.fromkeys((*ka, *kb)) if ka.get(k) != kb.get(k)]
        if na != nb:
            diffs.insert(0, "name")
        mark = "!!" if diffs else "  "
        detail = "identical" if not diffs else " ".join(
            f"name:{na}|{nb}" if k == "name" else
            f"{k}:{ka.get(k)}|{kb.get(k)}" for k in diffs)
        print(f"{mark} {i:2d} {na:28s} {detail}")
        if diffs and first is None:
            first = i
    if len(a) != len(b) and first is None:
        first = min(len(a), len(b))
    print("first divergence:",
          "none" if first is None else f"boundary {first}")
    return 1 if first is not None else 0


if __name__ == "__main__":
    sys.exit(main())
