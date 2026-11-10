#!/usr/bin/env python3
"""Recreate seed 845 #1967 without stepping the preceding 1967 vectors.

Uses the pinned oracle ABI and generator recipe from POM68K 4f0747b4.
No Macintosh assets. Run from any directory with --oracle and --out.
"""
import argparse
import hashlib
import json
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "oracle/fuzz"))
import gen040
import sst040
from fuzz040 import run_one
from oracle_driver import Oracle

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--oracle", required=True, help="pinned liboracle_uae shared library")
parser.add_argument("--out", required=True, help="output SST040 JSON file")
args = parser.parse_args()
gen040.set_page_shift(13)
rng = random.Random(845)
for _ in range(1968):
    state = gen040.gen_state(rng, family="random", mmu="identity")
fingerprint = hashlib.sha256(json.dumps(
    sst040.state_to_dict(state, state.ram), sort_keys=True,
    separators=(",", ":")).encode()).hexdigest()
if fingerprint != "3f4397871e664e123a43e07db7c5b5c46c8a18e7233b0153764e280fde3fd3de":
    raise SystemExit("Generator drift: restore the oracle/fuzz recipe from 4f0747b4")
oracle = Oracle(args.oracle, model=68040, fpu=0)
final, ram, cycles = run_one(oracle, state)
out = Path(args.out)
out.parent.mkdir(parents=True, exist_ok=True)
sst040.save(str(out), [sst040.vector(
    "040 random/identity/8k seed=845 #1967", state, final, ram, cycles, 1)])
print(f"Wrote {out}")
