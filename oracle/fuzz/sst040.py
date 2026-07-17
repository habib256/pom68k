# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# SST040 — the 68040 extension of the SST030 JSON format (sst030.py):
# every SST030 key, plus the eight 040 MMU registers in initial/final:
#
#   urp040 srp040 tc040 itt0 itt1 dtt0 dtt1 mmusr040
#
# Key names match the OracleState / State field names (oracle_api.h Q1):
# "srp" stays the 64-bit 68030 root pointer, "srp040" is the 32-bit 040
# one — no collision, and a SST040 reader loads SST030 corpora unchanged
# (missing 040 keys read as 0 via .get, same convention as the O5 FPU
# fields). All values decimal, one JSON array of vectors.

from __future__ import annotations

from oracle_driver import State
import sst030

_SCALARS_040 = ["urp040", "srp040", "tc040", "itt0", "itt1", "dtt0", "dtt1",
                "mmusr040"]


def state_to_dict(st: State, ram: list[tuple[int, int]]) -> dict:
    d = sst030.state_to_dict(st, ram)
    ram_cells = d.pop("ram")            # keep "ram" last, like sst030
    for k in _SCALARS_040:
        d[k] = getattr(st, k)
    d["ram"] = ram_cells
    return d


def dict_to_state(d: dict) -> State:
    st = sst030.dict_to_state(d)        # tolerates missing 040 keys (SST030)
    for k in _SCALARS_040:
        setattr(st, k, d.get(k, 0))
    return st


def vector(name: str, initial: State, final: State,
           final_ram: list[tuple[int, int]], length: int) -> dict:
    return {
        "name": name,
        "initial": state_to_dict(initial, initial.ram),
        "final": state_to_dict(final, final_ram),
        "length": length,
    }


save = sst030.save
load = sst030.load
