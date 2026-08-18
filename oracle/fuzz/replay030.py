# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# REPLAY a pinned SST030 corpus through the oracle — no fuzzing, the very
# same initial states — and say, vector by vector, whether the oracle still
# produces the pinned final.
#
# Why this exists (2026-08-18): `sst68030` went red on 14 FPU vectors the
# day the tree was relinked fresh, and the cause was a DELIBERATE Moira
# change (POM68K_VENDOR.md row 26: the 030 FPU trap frame moved from the
# 040's format $3 to the manual's format $2) that never re-asked the oracle.
# The corpora are dated 2026-07-15; the oracle glue gained its 68040 mode
# AFTER that (Q1-Q3, 2026-07-17/18). Two stories fit the same red:
#   * the oracle still says $3  → row 26 overruled the oracle on spec alone,
#     which disputes/NOTES.md reserves for real-hardware traces;
#   * the oracle now says $2    → the pin is stale, and re-finalizing the
#     SAME initial states is the lawful, mechanical repin.
# Only a replay separates them. A re-fuzz (loop.sh) would too, but it draws
# NEW vectors — the pinned set's history (hand-planted FRESTORE frames,
# arbitration-era seeds) is worth keeping, so the repin path here rewrites
# finals under identical initials and identical names.
#
#   python3 replay030.py --a ../../build/oracle_uae/liboracle_uae.so \
#       --corpus ../../tests/data/sst68030/fpu_tt.json \
#       [--names "fpu/tt seed=41 #52,..."] [--repin-out FILE]
#
# Exit 0 = oracle agrees with every pinned final checked; 1 = disagreements
# (listed; with --repin-out the rewritten corpus is emitted either way).

from __future__ import annotations

import argparse
import sys

import sst030
from oracle_driver import Oracle


def ram_delta(pinned: list[tuple[int, int]],
              fresh: list[tuple[int, int]]) -> list[str]:
    p = dict(pinned)
    f = dict(fresh)
    out = []
    for a in sorted(set(p) | set(f)):
        if p.get(a) != f.get(a):
            out.append(f"RAM[${a:06X}] pin={p.get(a)} oracle={f.get(a)}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True, help="oracle .so (its word is law)")
    ap.add_argument("--corpus", required=True, action="append",
                    help="pinned corpus json (repeatable)")
    ap.add_argument("--names", default=None,
                    help="comma-separated vector names; default = all")
    ap.add_argument("--repin-out", default=None,
                    help="rewrite THIS corpus with the oracle's finals "
                         "(single --corpus only)")
    ap.add_argument("--max-report", type=int, default=20)
    args = ap.parse_args()

    if args.repin_out and len(args.corpus) != 1:
        ap.error("--repin-out takes exactly one --corpus")

    only = set(n.strip() for n in args.names.split(",")) if args.names else None
    oracle = Oracle(args.a)
    print(f"oracle: {oracle.name}")

    total = checked = disagree = 0
    for path in args.corpus:
        vecs = sst030.load(path)
        total += len(vecs)
        rewritten = []
        n_dis = 0
        for v in vecs:
            if only and v["name"] not in only:
                rewritten.append(v)
                continue
            st = sst030.dict_to_state(v["initial"])
            oracle.load(st)
            cycles = oracle.step()
            fin = oracle.read_back()
            ram = oracle.ram_diff(dict(st.ram))
            checked += 1

            pin = sst030.dict_to_state(v["final"])
            diffs = fin.core_diff(pin)
            diffs += ram_delta(pin.ram, ram)
            if diffs:
                disagree += 1
                n_dis += 1
                if disagree <= args.max_report:
                    print(f"  DIFF {v['name']}:")
                    for d in diffs[:8]:
                        print(f"       {d}")
            rewritten.append(sst030.vector(v["name"], st, fin, ram, cycles))
        print(f"{path}: {n_dis} disagreement(s) among "
              f"{len(vecs) if not only else 'selected'} vector(s)")
        if args.repin_out:
            sst030.save(args.repin_out, rewritten)
            print(f"  repinned → {args.repin_out}")

    print(f"replay: {checked} checked, {disagree} where the oracle no longer "
          f"matches its own pin")
    return 1 if disagree else 0


if __name__ == "__main__":
    sys.exit(main())
