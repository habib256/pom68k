# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# SST030 — the 68030 extension of the SingleStepTests/680x0 JSON format
# used by tests/sst68000.cpp, and the exchange format of the whole oracle
# phase (TODO.md § Phase 2). Differences vs the 68000 format:
#
#   * a7 is explicit, alongside usp/isp/msp (68000 had usp/ssp);
#   * added scalar keys: vbr sfc dfc cacr caar crp srp tc tt0 tt1 mmusr
#     stopped (crp/srp are 64-bit decimals — the C++ scanner parses u64);
#   * no "prefetch" (functional accuracy — the LC II target);
#   * "length" = cycles from oracle A, ADVISORY only (harness may ignore).
#
# All values decimal (the hand-rolled C++ scanner never sees hex).
# File layout: one JSON array of vectors, same as upstream SST.

from __future__ import annotations

import json

from oracle_driver import State

# O5: fpcr/fpsr/fpiar scalars + "fp0".."fp7" (each a list of exactly 3
# u32 words, the OracleState.fp[i] raw-extended layout) — always emitted,
# like every other field; readers use .get() so pre-O5 corpora still load.
_SCALARS = ["usp", "isp", "msp", "pc", "sr", "vbr", "sfc", "dfc", "cacr",
            "caar", "crp", "srp", "tc", "tt0", "tt1", "mmusr",
            "fpcr", "fpsr", "fpiar", "stopped"]


def state_to_dict(st: State, ram: list[tuple[int, int]]) -> dict:
    d: dict = {}
    for i in range(8):
        d[f"d{i}"] = st.d[i]
    for i in range(8):
        d[f"a{i}"] = st.a[i]
    for k in _SCALARS:
        d[k] = getattr(st, k)
    for i in range(8):
        d[f"fp{i}"] = list(st.fp[i])
    d["ram"] = [[a, v] for a, v in ram]
    return d


def dict_to_state(d: dict) -> State:
    st = State()
    st.d = [d[f"d{i}"] for i in range(8)]
    st.a = [d[f"a{i}"] for i in range(8)]
    for k in _SCALARS:
        setattr(st, k, d.get(k, 0))
    st.fp = [list(d.get(f"fp{i}", [0, 0, 0])) for i in range(8)]
    st.ram = [(a, v) for a, v in d.get("ram", [])]
    return st


def vector(name: str, initial: State, final: State,
           final_ram: list[tuple[int, int]], length: int) -> dict:
    return {
        "name": name,
        "initial": state_to_dict(initial, initial.ram),
        "final": state_to_dict(final, final_ram),
        "length": length,
    }


def save(path: str, vectors: list[dict]) -> None:
    with open(path, "w") as f:
        json.dump(vectors, f, separators=(",", ":"))


def load(path: str) -> list[dict]:
    with open(path) as f:
        return json.load(f)
