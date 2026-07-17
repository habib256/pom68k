# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# Random 68040 state generator for the Q2 fuzzing loop (TODO.md § Phase 3).
# Target configuration = the Q1 oracle gate: 68LC040 (integer + 040 MMU,
# no FPU — the LC 475 / Quadra 605 CPU). Reuses gen030.py's 16 MB bus
# layout, vector table/landing pads, stacks and its integer/random/fault
# opcode families; what is 040-specific here:
#
#   * core family adds MOVE16 (all five forms, $F600-$F63F — M68040UM
#     § MOVE16: line transfers, low 4 EA bits ignored) and CINV/CPUSH
#     ($F400-$F4FF, cache maintenance, supervisor-only — M68040UM § 4.5/4.6);
#   * mmu family speaks the 040 dialect: PTESTR/PTESTW ($F568/$F548 + An,
#     walk per DFC), the four PFLUSH forms ($F500-$F51F), and MOVEC of the
#     040 control registers (TC $003, ITT0 $004, ITT1 $005, DTT0 $006,
#     DTT1 $007, MMUSR $805, URP $806, SRP $807 — M68040UM § 3.5.1);
#   * MMU trees are REAL 040 tables in the flat buffer (M68040UM § 3.1/3.2:
#     4K pages, TC=$8000, logical = RI(a31-25) : PI(a24-18) : PGI(a17-12)
#     : offset), built as a SPARSE identity map — only the pointer-table
#     slots covering the infrastructure + data window and the tables
#     themselves are resident, everything else takes a format $7 access
#     error (vectors/stacks stay mapped so fault frames push cleanly);
#   * fault family aims at invalid / write-protected / supervisor-only /
#     remapped 4K pages (W bit 2, S bit 7 of the page descriptor), and the
#     table walk's U/M descriptor updates land in the RAM diff.
#
# Layout of the 16 MB bus (identity-mapped unless remapped):
#   $000000  vector table            $008000  per-vector landing pads
#   $001000  ISP/MSP, $002000 USP    $010000  code page (PC starts here)
#   $020000+ data pages              $200000  040 MMU tables (root/ptr/page)

from __future__ import annotations

import random

from oracle_driver import State
from gen030 import (MASK, VEC_BASE, CODE_BASE, DATA_BASE,
                    STACK_ISP, STACK_MSP, STACK_USP,
                    build_vector_table, _w16, _w32,
                    _op_random, _op_core, _op_fault)

# ── 040 MMU table builder (M68040UM § 3.1/3.2) ─────────────────────────────
TC040_FUZZ = 0x8000        # E=1, P=0 → 4K pages
PAGE_SHIFT = 12
PAGE_SIZE = 1 << PAGE_SHIFT

TABLE_BASE = 0x200000
ROOT_T = TABLE_BASE            # 128 entries × 4 B (512-byte aligned)
PTR_T = TABLE_BASE + 0x200     # 128 entries × 4 B
PAGE_T = TABLE_BASE + 0x400    # one 64-entry (256 B) page table per mapped PI

# Mapped pointer-table indexes (256 KB each). PI 0-4 = $000000-$13FFFF
# (vectors, stacks, pads, code, data window incl. the d16 overshoot),
# PI 8 = $200000-$23FFFF (the tables themselves). All accesses are < 16 MB
# so RI is always 0: the root table needs a single resident entry.
MAPPED_PI = [0, 1, 2, 3, 4, 8]

UDT_RESIDENT = 2       # upper (root/pointer) descriptor: resident
PDT_RESIDENT = 1       # page descriptor: resident
PD_WP = 1 << 2         # W  — write-protected
PD_SUPER = 1 << 7      # S  — supervisor-only


def build_identity_tables(ram: dict[int, int], rng: random.Random,
                          holes: int = 4, wp_pages: int = 4,
                          super_pages: int = 3, remaps: int = 3) -> dict:
    """Sparse 040 identity tree with a few invalid / write-protected /
    supervisor-only / remapped 4K pages sprinkled over the DATA window.
    Returns {page_index: kind} so the generator can aim operands."""
    for i in range(128):
        _w32(ram, ROOT_T + 4 * i, 0)                       # invalid (UDT=0)
    _w32(ram, ROOT_T + 0, PTR_T | UDT_RESIDENT)
    for i in range(128):
        _w32(ram, PTR_T + 4 * i, 0)
    for k, pi in enumerate(MAPPED_PI):
        table = PAGE_T + k * 0x100
        _w32(ram, PTR_T + 4 * pi, table | UDT_RESIDENT)
        for i in range(64):
            page = (pi << 6) | i
            _w32(ram, table + 4 * i, (page << PAGE_SHIFT) | PDT_RESIDENT)

    def entry(idx: int) -> int:
        return PAGE_T + MAPPED_PI.index(idx >> 6) * 0x100 + (idx & 63) * 4

    # Bad pages only inside the data window — never the infrastructure
    # (vectors/stacks/pads/code < $15000, tables at $200000).
    kinds: dict[int, str] = {}
    candidates = list(range(DATA_BASE >> PAGE_SHIFT,
                            (DATA_BASE + 0x100000) >> PAGE_SHIFT))
    picks = rng.sample(candidates,
                       min(len(candidates), holes + wp_pages + super_pages + remaps))
    for i, idx in enumerate(picks):
        phys = idx << PAGE_SHIFT
        if i < holes:
            _w32(ram, entry(idx), 0)                       # PDT=0 invalid
            kinds[idx] = "invalid"
        elif i < holes + wp_pages:
            _w32(ram, entry(idx), phys | PD_WP | PDT_RESIDENT)
            kinds[idx] = "wp"
        elif i < holes + wp_pages + super_pages:
            _w32(ram, entry(idx), phys | PD_SUPER | PDT_RESIDENT)
            kinds[idx] = "super"
        else:
            other = rng.choice(candidates)
            _w32(ram, entry(idx), (other << PAGE_SHIFT) | PDT_RESIDENT)
            kinds[idx] = f"remap->{other}"
    return kinds


# Transparent windows (M68040UM § 3.1.2: base $00, address mask $FF —
# every a31-24 matches; E bit 15; S field bits 14-13: 00 user-only,
# 01 supervisor-only, 1x both modes).
TT040_ALL = (0x00 << 24) | (0xFF << 16) | (1 << 15) | (1 << 14)
TT040_SUPER = (0x00 << 24) | (0xFF << 16) | (1 << 15) | (1 << 13)


# ── 040 opcode families ────────────────────────────────────────────────────

def _op_move16(rng: random.Random) -> list[int]:
    """MOVE16, all five forms (M68040UM § MOVE16). Absolute-long targets
    are 16-aligned addresses in the data window."""
    ax, ay = rng.randrange(8), rng.randrange(8)
    a = (DATA_BASE + rng.randrange(0x100000)) & ~15
    hi, lo = a >> 16, a & 0xFFFF
    form = rng.randrange(5)
    if form == 0:
        return [0xF620 | ax, 0x8000 | (ay << 12)]          # (Ax)+,(Ay)+
    return [[0xF600 | ax, hi, lo],                         # (An)+,(xxx).L
            [0xF608 | ax, hi, lo],                         # (xxx).L,(An)+
            [0xF610 | ax, hi, lo],                         # (An),(xxx).L
            [0xF618 | ax, hi, lo]][form - 1]               # (xxx).L,(An)


def _op_cache(rng: random.Random) -> list[int]:
    """CINV/CPUSH $F400|cc<<6|P<<5|ss<<3|An (M68040UM § 4.5/4.6): cache
    NC/DC/IC/BC, scope line/page/all. Supervisor-only (privilege violation
    from user mode); cache=0 / scope=0 encodings sprinkled in for the
    illegal-decode paths."""
    cache = rng.choice([1, 2, 3, 3, rng.randrange(4)])
    scope = rng.choice([1, 2, 3, 3, rng.randrange(4)])
    push = rng.getrandbits(1)
    return [0xF400 | (cache << 6) | (push << 5) | (scope << 3) | rng.randrange(8)]


def _op_core040(rng: random.Random) -> list[int]:
    """Common integer set (gen030) + the two 040-only instruction groups."""
    r = rng.random()
    if r < 0.50:
        return _op_core(rng)
    if r < 0.80:
        return _op_move16(rng)
    return _op_cache(rng)


# MOVEC control-register numbers valid on the 68040 (M68040UM § 3.5.1 +
# M68000PRM MOVEC). The 040 MMU set first (the Q2 target), then the
# classics; $802 (CAAR — gone on 040) and random numbers cover the
# illegal-control-register exception path.
_MOVEC_040 = [0x806, 0x807, 0x003, 0x004, 0x005, 0x006, 0x007, 0x805,
              0x000, 0x001, 0x002, 0x800, 0x801, 0x803, 0x804]


def _op_mmu040(rng: random.Random) -> list[int]:
    r = rng.random()
    if r < 0.35:
        # PTESTR $F568|An / PTESTW $F548|An — walks per DFC, fills MMUSR.
        # An biased to A0 (gen_state aims A0 at interesting pages).
        an = 0 if rng.random() < 0.5 else rng.randrange(8)
        return [rng.choice([0xF568, 0xF548]) | an]
    if r < 0.60:
        # PFLUSHN (An) / PFLUSH (An) / PFLUSHAN / PFLUSHA ($F500-$F51F);
        # the An field is unused by the A forms but fuzzed anyway.
        return [rng.choice([0xF500, 0xF508, 0xF510, 0xF518]) | rng.randrange(8)]
    # MOVEC ($4E7A ctrl→Rn / $4E7B Rn→ctrl), 040 registers weighted.
    ctrl = rng.choice(_MOVEC_040 + [0x802, rng.getrandbits(12)])
    ext = (rng.getrandbits(4) << 12) | ctrl
    return [rng.choice([0x4E7A, 0x4E7B]), ext]


def _op_fault040(rng: random.Random) -> list[int]:
    """gen030's translated-fault set (generic 68k memory ops on A0) plus
    MOVE16 aimed at A0 so line transfers fault too. The absolute leg
    points at the data window (usually good — occasionally a bad page
    itself, which is fine coverage)."""
    if rng.random() < 0.70:
        return _op_fault(rng)
    a = (DATA_BASE + rng.randrange(0x100000)) & ~15
    hi, lo = a >> 16, a & 0xFFFF
    return rng.choice([
        [0xF620, 0x8000 | (1 << 12)],                      # (A0)+,(A1)+  read bad
        [0xF600, hi, lo],                                  # (A0)+,(xxx).L read bad
        [0xF608, hi, lo],                                  # (xxx).L,(A0)+ write bad
        [0xF610, hi, lo],                                  # (A0),(xxx).L  read bad
        [0xF618, hi, lo],                                  # (xxx).L,(A0)  write bad
    ])


FAMILIES = {"random": _op_random, "core": _op_core040, "mmu": _op_mmu040,
            "fault": _op_fault040}


def gen_state(rng: random.Random, family: str = "core",
              mmu: str = "off") -> State:
    """One complete initial 68040 state. mmu: off | identity | tt."""
    st = State()
    ram: dict[int, int] = {}
    build_vector_table(ram)

    st.d = [rng.getrandbits(32) for _ in range(8)]
    st.a = [(DATA_BASE + rng.randrange(0, 0x100000)) & ~1 for _ in range(8)]
    st.usp, st.isp, st.msp = STACK_USP, STACK_ISP, STACK_MSP

    super_mode = rng.random() < 0.7  # MMU/cache ops need S=1 to be interesting
    m_bit = rng.random() < 0.3
    st.sr = (0x2000 if super_mode else 0) | (0x1000 if m_bit and super_mode else 0) \
            | (0x0700) | rng.getrandbits(5)
    st.a[7] = (st.msp if m_bit else st.isp) if super_mode else st.usp

    st.pc = CODE_BASE + rng.randrange(0, 0x4000) & ~1
    st.vbr = VEC_BASE
    # DFC drives the PTEST walk (user 1/2 vs supervisor 5/6); the other
    # values exercise the odd function codes.
    st.sfc = rng.choice([1, 2, 5, 6, rng.randrange(8)])
    st.dfc = rng.choice([1, 2, 5, 6, rng.randrange(8)])

    kinds: dict[int, str] = {}
    if mmu == "identity":
        kinds = build_identity_tables(ram, rng)
        st.tc040 = TC040_FUZZ
        st.urp040 = st.srp040 = ROOT_T
    elif mmu == "tt":
        # MMU enabled with the same tree behind. ITT0 matches everything in
        # both modes (fetches always transparent); DTT0 matches supervisor
        # only, so user-mode data accesses FALL THROUGH the windows and
        # walk the real tree — both the TT-hit and the TT-miss paths are
        # exercised, and the fault family still reaches its bad pages.
        kinds = build_identity_tables(ram, rng)
        st.tc040 = TC040_FUZZ
        st.urp040 = st.srp040 = ROOT_T
        st.itt0 = TT040_ALL
        st.dtt0 = TT040_SUPER
    # else: MMU off (tc040.E = 0, roots/windows zero)

    if family == "fault":
        # Aim A0 at a bad page; offsets include odd addresses and page
        # boundaries so sub-accesses straddle good/bad 4K pages.
        bad = sorted(kinds)
        page = rng.choice(bad) if bad else (DATA_BASE >> PAGE_SHIFT)
        offset = rng.choice([
            rng.randrange(PAGE_SIZE) & ~1,
            rng.randrange(PAGE_SIZE) | 1,
            0, 1, 2, PAGE_SIZE - 1, PAGE_SIZE - 2, PAGE_SIZE - 3, PAGE_SIZE - 4,
        ])
        st.a[0] = ((page << PAGE_SHIFT) + offset) & MASK
        st.a[1] = (DATA_BASE + rng.randrange(0, 0x10000)) & ~1
    elif family == "mmu" and rng.random() < 0.6:
        # Aim A0 (the biased PTEST target) at an interesting page — one of
        # the bad pages, or an unmapped address (walk hits an invalid
        # upper descriptor; with mmu=off the "tree" under URP=0 is the
        # vector table, also a legitimate PTEST result).
        if kinds and rng.random() < 0.7:
            page = rng.choice(sorted(kinds))
            st.a[0] = ((page << PAGE_SHIFT) + rng.randrange(PAGE_SIZE)) & MASK
        else:
            st.a[0] = rng.getrandbits(32)

    words = FAMILIES[family](rng)

    if family in ("core", "fault"):
        # MOVE16 sources must show in the RAM diff: seed one 16-byte line
        # at each A-register (aligned down, as the silicon does) and at the
        # absolute-long leg of the generated op, if any.
        for i in range(8):
            base = st.a[i] & ~15 & MASK
            for off in range(16):
                ram[(base + off) & MASK] = rng.getrandbits(8)
        if (words[0] & 0xFFF8) in (0xF608, 0xF618) and len(words) == 3:
            base = ((words[1] << 16) | words[2]) & ~15 & MASK
            for off in range(16):
                ram[(base + off) & MASK] = rng.getrandbits(8)

    addr = st.pc
    for w in words:
        _w16(ram, addr, w); addr += 2
    # Pad the rest of the code stream with NOPs then a self-loop.
    for _ in range(8):
        _w16(ram, addr, 0x4E71); addr += 2
    _w16(ram, addr, 0x60FE)

    # Scatter some data so reads see non-zero bytes.
    for _ in range(64):
        ram[(DATA_BASE + rng.randrange(0, 0x100000)) & MASK] = rng.getrandbits(8)

    st.ram = sorted(ram.items())
    return st
