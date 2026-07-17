# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# ctypes driver for the 68030 oracle shared libraries (oracle/oracle_api.h).
# Loads any .so exposing the common C ABI and drives it over a host-owned
# flat big-endian buffer — WinUAE today, any future oracle interchangeably.

from __future__ import annotations

import ctypes
from dataclasses import dataclass, field

MEM_SIZE = 1 << 24  # 16 MB flat bus, mask 0xFFFFFF — same as tests/sst68000

_INT_FIELDS = [
    ("d", ctypes.c_uint32 * 8),
    ("a", ctypes.c_uint32 * 8),
    ("usp", ctypes.c_uint32), ("isp", ctypes.c_uint32), ("msp", ctypes.c_uint32),
    ("pc", ctypes.c_uint32),
    ("sr", ctypes.c_uint16),
    ("vbr", ctypes.c_uint32),
    ("sfc", ctypes.c_uint32), ("dfc", ctypes.c_uint32),
    ("cacr", ctypes.c_uint32), ("caar", ctypes.c_uint32),
    ("crp", ctypes.c_uint64), ("srp", ctypes.c_uint64),
    ("tc", ctypes.c_uint32),
    ("tt0", ctypes.c_uint32), ("tt1", ctypes.c_uint32),
    ("mmusr", ctypes.c_uint16),
    ("fp", (ctypes.c_uint32 * 3) * 8),
    ("fpcr", ctypes.c_uint32), ("fpsr", ctypes.c_uint32), ("fpiar", ctypes.c_uint32),
    ("stopped", ctypes.c_uint8),
    ("pad", ctypes.c_uint8 * 3),
    # 68040 MMU registers (Q2.1) — appended after stopped+pad exactly like
    # oracle_api.h so every 68030-era field keeps its offset.
    ("urp040", ctypes.c_uint32), ("srp040", ctypes.c_uint32),
    ("tc040", ctypes.c_uint32),
    ("itt0", ctypes.c_uint32), ("itt1", ctypes.c_uint32),
    ("dtt0", ctypes.c_uint32), ("dtt1", ctypes.c_uint32),
    ("mmusr040", ctypes.c_uint32),
]


class COracleState(ctypes.Structure):
    _fields_ = _INT_FIELDS


# Scalar register names, in canonical SST030 JSON order.
# fpcr/fpsr/fpiar joined at O5 (fp0-fp7 are 3-long arrays, handled apart).
# The 040 MMU block joined at Q2.1 (zero on 68030 oracles; sst030.py does
# NOT emit them — sst040.py does).
SCALARS = ["usp", "isp", "msp", "pc", "sr", "vbr", "sfc", "dfc", "cacr",
           "caar", "crp", "srp", "tc", "tt0", "tt1", "mmusr",
           "fpcr", "fpsr", "fpiar", "stopped",
           "urp040", "srp040", "tc040", "itt0", "itt1", "dtt0", "dtt1",
           "mmusr040"]


@dataclass
class State:
    """Python-side 68030 state; a[7] is the SP active per sr's S/M bits."""
    d: list[int] = field(default_factory=lambda: [0] * 8)
    a: list[int] = field(default_factory=lambda: [0] * 8)
    usp: int = 0; isp: int = 0; msp: int = 0
    pc: int = 0; sr: int = 0x2700
    vbr: int = 0; sfc: int = 0; dfc: int = 0
    cacr: int = 0; caar: int = 0
    crp: int = 0; srp: int = 0
    tc: int = 0; tt0: int = 0; tt1: int = 0; mmusr: int = 0
    # 68881/68882 (O5): raw 96-bit extended per oracle_api.h —
    # fp[i] = [sign/exp word << 16, mantissa 63..32, mantissa 31..0]
    fp: list[list[int]] = field(default_factory=lambda: [[0, 0, 0] for _ in range(8)])
    fpcr: int = 0; fpsr: int = 0; fpiar: int = 0
    stopped: int = 0
    # 68040 on-chip MMU (Q2.1) — zero/ignored on 68030 oracles
    urp040: int = 0; srp040: int = 0; tc040: int = 0
    itt0: int = 0; itt1: int = 0; dtt0: int = 0; dtt1: int = 0
    mmusr040: int = 0
    ram: list[tuple[int, int]] = field(default_factory=list)  # (addr, byte)

    def to_c(self) -> COracleState:
        c = COracleState()
        for i in range(8):
            c.d[i] = self.d[i]; c.a[i] = self.a[i]
            for j in range(3):
                c.fp[i][j] = self.fp[i][j]
        for name in SCALARS:
            setattr(c, name, getattr(self, name))
        return c

    @classmethod
    def from_c(cls, c: COracleState) -> "State":
        st = cls()
        st.d = [c.d[i] for i in range(8)]
        st.a = [c.a[i] for i in range(8)]
        st.fp = [[int(c.fp[i][j]) for j in range(3)] for i in range(8)]
        for name in SCALARS:
            setattr(st, name, int(getattr(c, name)))
        return st

    def core_diff(self, other: "State") -> list[str]:
        """Human-readable field-level differences (ram excluded)."""
        out = []
        for i in range(8):
            if self.d[i] != other.d[i]:
                out.append(f"d{i}: {self.d[i]:08x} != {other.d[i]:08x}")
            if self.a[i] != other.a[i]:
                out.append(f"a{i}: {self.a[i]:08x} != {other.a[i]:08x}")
        for i in range(8):
            if self.fp[i] != other.fp[i]:
                x = "".join(f"{w:08x}" for w in self.fp[i])
                y = "".join(f"{w:08x}" for w in other.fp[i])
                out.append(f"fp{i}: {x} != {y}")
        for name in SCALARS:
            x, y = getattr(self, name), getattr(other, name)
            if x != y:
                out.append(f"{name}: {x:x} != {y:x}")
        return out


class Oracle:
    """One loaded oracle .so bound to its own 16 MB buffer.

    model: 68030 (default — no oracle_set_model call, so pre-Q1 .so files
    keep working) or 68040 (oracle_set_model(68040, 0) = 68LC040, integer +
    040 MMU, no FPU — the Q1 gate configuration; must precede oracle_init).
    """

    def __init__(self, so_path: str, model: int = 68030):
        self.lib = ctypes.CDLL(so_path)
        self.lib.oracle_name.restype = ctypes.c_char_p
        self.lib.oracle_init.restype = ctypes.c_int
        self.lib.oracle_init.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
        self.lib.oracle_set_state.argtypes = [ctypes.POINTER(COracleState)]
        self.lib.oracle_get_state.argtypes = [ctypes.POINTER(COracleState)]
        self.lib.oracle_step.restype = ctypes.c_int32
        self.model = model
        if model != 68030:
            self.lib.oracle_set_model.argtypes = [ctypes.c_int, ctypes.c_int]
            self.lib.oracle_set_model.restype = None
            self.lib.oracle_set_model(model, 0)
        self.mem = (ctypes.c_uint8 * MEM_SIZE)()
        rc = self.lib.oracle_init(self.mem, MEM_SIZE)
        if rc != 0:
            raise RuntimeError(f"{so_path}: oracle_init failed ({rc})")
        self.name = self.lib.oracle_name().decode()

    def load(self, st: State) -> None:
        ctypes.memset(self.mem, 0, MEM_SIZE)
        for addr, val in st.ram:
            self.mem[addr & (MEM_SIZE - 1)] = val
        c = st.to_c()
        self.lib.oracle_set_state(ctypes.byref(c))

    def step(self) -> int:
        return int(self.lib.oracle_step())

    def read_back(self) -> State:
        c = COracleState()
        self.lib.oracle_get_state(ctypes.byref(c))
        return State.from_c(c)

    def ram_cells(self, addrs) -> list[tuple[int, int]]:
        """Sample the buffer at the given addresses → sorted (addr, byte)."""
        m = MEM_SIZE - 1
        return [(a, self.mem[a]) for a in sorted({x & m for x in addrs})]

    def ram_diff(self, baseline: dict[int, int]) -> list[tuple[int, int]]:
        """All bytes that differ from `baseline` (addr → byte, 0 default).

        Uses numpy when available (16 MB scan per step is hot), else a
        chunked pure-Python fallback.
        """
        try:
            import numpy as np
            cur = np.frombuffer(self.mem, dtype=np.uint8)
            ref = np.zeros(MEM_SIZE, dtype=np.uint8)
            for a, v in baseline.items():
                ref[a & (MEM_SIZE - 1)] = v
            idx = np.nonzero(cur != ref)[0]
            return [(int(i), int(cur[i])) for i in idx]
        except ImportError:
            out = []
            for i in range(MEM_SIZE):
                v = self.mem[i]
                if v != baseline.get(i, 0):
                    out.append((i, v))
            return out
