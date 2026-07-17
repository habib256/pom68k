#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# Q2.1 — 68040 fuzzing loop: generate random 68LC040 states (gen040 —
# integer + 040 MMU, no FPU, the LC 475 / Quadra 605 CPU), step the WinUAE
# oracle in its 68040 configuration (Q1: oracle_set_model(68040, 0) before
# oracle_init), emit SST040 JSON vectors (sst040 = SST030 + urp040/srp040/
# tc040/itt0/itt1/dtt0/dtt1/mmusr040) for the future Moira 040 replay
# harness (Q2.2+).
#
# Same discipline as fuzz030.py: WinUAE-solo by default — the oracle's
# word is law; disputes vs Moira are arbitrated manually (M68040UM § 3
# MMU, § 4 caches+MOVE16, § 8 exception frames — format $7 access error;
# oracle wins over spec; real-hardware traces welcome). --b keeps the
# differential slot open should a second 040 oracle appear.
#
# 040-specific: a negative oracle_step() (double fault / halt) skips the
# vector — the post-halt state is not architectural — and is counted in
# the final stat line; more than ~1 % halts in a cell means the generator
# is breaking its own infrastructure (stacks/vectors must stay mapped).
#
# Usage (solo — the default path):
#   fuzz040.py --a liboracle_uae.so \
#              --family core|mmu|random|fault --mmu off|identity|tt \
#              --n 1000 --seed 1 --out corpus/core_off.json

from __future__ import annotations

import argparse
import json
import os
import random
import sys

from gen040 import gen_state
from oracle_driver import Oracle, State
import sst040


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
                    choices=["core", "mmu", "random", "fault"])
    ap.add_argument("--mmu", default="off", choices=["off", "identity", "tt"])
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", required=True)
    ap.add_argument("--disputes", default=None,
                    help="dir for disagreement dumps (default: <out-dir>/disputes)")
    args = ap.parse_args()

    oa = Oracle(args.a, model=68040)
    ob = Oracle(args.b, model=68040) if args.b else None
    print(f"oracle A: {oa.name}" + (f" | oracle B: {ob.name}" if ob else " (solo)"))

    rng = random.Random(args.seed)
    disputes_dir = args.disputes or os.path.join(
        os.path.dirname(os.path.abspath(args.out)), "disputes")

    vectors, n_dispute, n_halt = [], 0, 0
    for i in range(args.n):
        st = gen_state(rng, family=args.family, mmu=args.mmu)
        name = f"040 {args.family}/{args.mmu} seed={args.seed} #{i}"

        fa, rama, ca = run_one(oa, st)
        if ca < 0:
            n_halt += 1
            continue
        if ob:
            fb, ramb, _cb = run_one(ob, st)
            diffs = fa.core_diff(fb)
            if rama != ramb:
                diffs.append(f"ram: {len(rama)} vs {len(ramb)} touched cells differ")
            if diffs:
                n_dispute += 1
                os.makedirs(disputes_dir, exist_ok=True)
                with open(os.path.join(disputes_dir,
                          f"040_{args.family}_{args.mmu}_{args.seed}_{i}.json"), "w") as f:
                    json.dump({"name": name,
                               "initial": sst040.state_to_dict(st, st.ram),
                               "a": {"who": oa.name,
                                     "final": sst040.state_to_dict(fa, rama)},
                               "b": {"who": ob.name,
                                     "final": sst040.state_to_dict(fb, ramb)},
                               "diffs": diffs}, f, indent=1)
                continue
        vectors.append(sst040.vector(name, st, fa, rama, ca))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    sst040.save(args.out, vectors)
    total = args.n
    print(f"{len(vectors)}/{total} kept → {args.out}"
          + (f", {n_halt} halts skipped" if n_halt else "")
          + (f", {n_dispute} disputes → {disputes_dir}" if n_dispute else ""))
    return 0 if len(vectors) else 1


if __name__ == "__main__":
    sys.exit(main())
