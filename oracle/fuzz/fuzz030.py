#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# O3 — fuzzing loop: generate random 68030 states (gen030), step the
# oracle, emit SST030 JSON vectors for tests/sst68030 to replay against
# Moira. Since the Musashi retirement (disputes/NOTES.md, 2026-07-15) the
# loop is WinUAE-solo: the oracle's word is law; disputes vs Moira are
# arbitrated manually (MC68030UM / MC68881-882UM reading; oracle wins
# over spec; real-hardware traces welcome).
#
# Usage (solo — the default path):
#   fuzz030.py --a liboracle_uae.so \
#              --family core|mmu|random|fault|fpu --mmu off|identity|tt \
#              --n 1000 --seed 1 --out corpus/core_off.json
#
# --b <second .so> keeps the differential mode alive should a second
# oracle return: only vectors both oracles agree on are emitted, and
# disagreements are quarantined in disputes/ for manual+spec arbitration.

from __future__ import annotations

import argparse
import json
import os
import random
import sys

from gen030 import gen_state
from oracle_driver import Oracle, State
import sst030


def run_one(o: Oracle, st: State):
    o.load(st)
    cycles = o.step()
    fin = o.read_back()
    ram = o.ram_diff(dict(st.ram))
    return fin, ram, cycles


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True,
                    help="oracle .so (WinUAE; solo = its word is law)")
    ap.add_argument("--b", help="optional second oracle .so (differential mode)")
    ap.add_argument("--family", default="core",
                    choices=["core", "mmu", "random", "fault", "fpu"])
    ap.add_argument("--mmu", default="off", choices=["off", "identity", "tt"])
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", required=True)
    ap.add_argument("--disputes", default=None,
                    help="dir for disagreement dumps (default: <out-dir>/disputes)")
    args = ap.parse_args()

    oa = Oracle(args.a)
    ob = Oracle(args.b) if args.b else None
    print(f"oracle A: {oa.name}" + (f" | oracle B: {ob.name}" if ob else " (solo)"))

    rng = random.Random(args.seed)
    disputes_dir = args.disputes or os.path.join(
        os.path.dirname(os.path.abspath(args.out)), "disputes")

    vectors, n_dispute = [], 0
    for i in range(args.n):
        st = gen_state(rng, family=args.family, mmu=args.mmu)
        name = f"{args.family}/{args.mmu} seed={args.seed} #{i}"

        fa, rama, ca = run_one(oa, st)
        if ob:
            fb, ramb, _cb = run_one(ob, st)
            diffs = fa.core_diff(fb)
            if rama != ramb:
                diffs.append(f"ram: {len(rama)} vs {len(ramb)} touched cells differ")
            if diffs:
                n_dispute += 1
                os.makedirs(disputes_dir, exist_ok=True)
                with open(os.path.join(disputes_dir,
                          f"{args.family}_{args.mmu}_{args.seed}_{i}.json"), "w") as f:
                    json.dump({"name": name,
                               "initial": sst030.state_to_dict(st, st.ram),
                               "a": {"who": oa.name,
                                     "final": sst030.state_to_dict(fa, rama)},
                               "b": {"who": ob.name,
                                     "final": sst030.state_to_dict(fb, ramb)},
                               "diffs": diffs}, f, indent=1)
                continue
        vectors.append(sst030.vector(name, st, fa, rama, ca))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    sst030.save(args.out, vectors)
    total = args.n
    print(f"{len(vectors)}/{total} kept → {args.out}"
          + (f", {n_dispute} disputes → {disputes_dir}" if n_dispute else ""))
    return 0 if len(vectors) else 1


if __name__ == "__main__":
    sys.exit(main())
