# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# Random 68030 state generator for the differential fuzzing loop (O3).
# Builds complete initial states — registers, vector table, stacks, code —
# and, in MMU modes, REAL translation trees in the flat 16 MB buffer so
# PTEST/PLOAD and translated accesses walk actual descriptors
# (MC68030 User's Manual § 9: TC/RP formats, short descriptors).
#
# Layout of the 16 MB bus (physical):
#   $000000  vector table (256 * 4, via VBR=0 or relocated)
#   $001000  supervisor stacks (ISP/MSP), $002000 user stack
#   $004000  MMU translation tables (root/pointer/page levels)
#   $010000  code page (PC starts here, logical — identity or remapped)
#   $020000+ data pages (targets of generated operands)

from __future__ import annotations

import random

from oracle_driver import State

MASK = 0xFFFFFF

VEC_BASE = 0x000000
STACK_ISP = 0x001800
STACK_MSP = 0x001F00
STACK_USP = 0x002800
TABLE_BASE = 0x004000
CODE_BASE = 0x010000
DATA_BASE = 0x020000

# Each exception handler is a distinct STOP-style landing pad so the taken
# vector is identifiable from the final PC. We use NOP+BRA.S self loops.
HANDLER_BASE = 0x008000
HANDLER_STRIDE = 0x20


def _w16(ram: dict[int, int], addr: int, v: int) -> None:
    ram[addr & MASK] = (v >> 8) & 0xFF
    ram[(addr + 1) & MASK] = v & 0xFF


def _w32(ram: dict[int, int], addr: int, v: int) -> None:
    _w16(ram, addr, v >> 16)
    _w16(ram, addr + 2, v)


def build_vector_table(ram: dict[int, int], vbr: int = VEC_BASE) -> None:
    """256 vectors, each pointing at its own landing pad (NOP; BRA.S *)."""
    for vec in range(256):
        pad = HANDLER_BASE + vec * HANDLER_STRIDE
        _w32(ram, vbr + vec * 4, pad)
        _w16(ram, pad, 0x4E71)          # NOP
        _w16(ram, pad + 2, 0x60FE)      # BRA.S * (self-loop marker)


# ── MMU table builder ──────────────────────────────────────────────────────
# Scheme: TC = E | PS=15 (32K pages) | IS=8 | TIA=9  → single-level table,
# 512 short descriptors covering the 24-bit space (logical bits 23-15 index
# the table after the 8-bit initial shift ignores a31-24).
# Short descriptors (4 bytes):  DT=0 invalid, DT=1 page descriptor.
#   page descriptor: page-addr | flags — WP bit 2, U bit 3, M bit 4, CI bit 6.

TC_FUZZ = (1 << 31) | (15 << 20) | (8 << 16) | (9 << 12)  # E, PS=15, IS=8, TIA=9
PAGE_SHIFT = 15
PAGE_SIZE = 1 << PAGE_SHIFT
N_PAGES = 1 << 9  # 512 entries

DT_INVALID = 0
DT_PAGE = 1


def build_identity_table(ram: dict[int, int], rng: random.Random,
                         holes: int = 4, wp_pages: int = 4,
                         remaps: int = 4) -> dict:
    """Single-level identity map with a few invalid / write-protected /
    remapped pages sprinkled in. Returns {page_index: kind} for the
    generator to aim operands at interesting pages."""
    kinds: dict[int, str] = {}
    for idx in range(N_PAGES):
        phys = idx << PAGE_SHIFT
        _w32(ram, TABLE_BASE + idx * 4, phys | DT_PAGE)
    # Never break the infrastructure pages (vectors/stacks/tables/code/pads).
    protected = set(range(0, (CODE_BASE + PAGE_SIZE) >> PAGE_SHIFT))
    candidates = [i for i in range(N_PAGES) if i not in protected]
    picks = rng.sample(candidates, min(len(candidates), holes + wp_pages + remaps))
    for i, idx in enumerate(picks):
        ent = TABLE_BASE + idx * 4
        if i < holes:
            _w32(ram, ent, DT_INVALID)
            kinds[idx] = "invalid"
        elif i < holes + wp_pages:
            _w32(ram, ent, (idx << PAGE_SHIFT) | (1 << 2) | DT_PAGE)  # WP
            kinds[idx] = "wp"
        else:
            other = rng.choice(candidates)
            _w32(ram, ent, (other << PAGE_SHIFT) | DT_PAGE)
            kinds[idx] = f"remap->{other}"
    return kinds


# CRP: high long = L/U=0, limit=$7FFF (unlimited ascending), DT=2 (short 4-byte
# descriptors at next level); low long = table physical address.
CRP_FUZZ = ((0x7FFF << 16 | 2) << 32) | TABLE_BASE


# ── Opcode families ────────────────────────────────────────────────────────

def _op_random(rng: random.Random) -> list[int]:
    """Any 16-bit word + up to 5 random extension words. Illegal patterns
    are valuable too (they must take identical exception paths)."""
    return [rng.getrandbits(16) for _ in range(1 + rng.randrange(6))]


def _op_core(rng: random.Random) -> list[int]:
    """Well-formed common integer ops hitting memory via (An)/(d16,An)."""
    choices = [
        lambda: [0x2028 | rng.randrange(8) << 9, rng.getrandbits(16)],   # MOVE.L d16(A0),Dn
        lambda: [0x2140 | rng.randrange(8), rng.getrandbits(16)],        # MOVE.L Dn,d16(A0)
        lambda: [0xD090 | rng.randrange(8) << 9],                        # ADD.L (A0),Dn
        lambda: [0x4E75],                                                # RTS
        lambda: [0x4E73],                                                # RTE
        lambda: [0x4E40 | rng.randrange(16)],                            # TRAP #n
        lambda: [0x4850 | rng.randrange(8)],                             # PEA (An)
        lambda: [0x48E0 | rng.getrandbits(8), rng.getrandbits(16)],      # MOVEM
        _moves(rng),
    ]
    c = rng.choice(choices)
    return c() if callable(c) else c


def _moves(rng: random.Random) -> list[int]:
    """MOVES.B/W/L via (An)/(An)+/-(An)/d16(An), both directions (O4
    slice 3: exercises SFC/DFC function codes through translation)."""
    size = rng.choice([0x0000, 0x0040, 0x0080])          # B/W/L
    eamode = rng.choice([0x10, 0x18, 0x20, 0x28])        # (An)/(An)+/-(An)/d16
    an = rng.randrange(8)
    ext = (rng.randrange(16) << 12) | (rng.getrandbits(1) << 11)
    words = [0x0E00 | size | eamode | an, ext]
    if eamode == 0x28:
        words.append(rng.getrandbits(16) & 0x7FFF)
    return words


def _op_mmu(rng: random.Random) -> list[int]:
    """F-line MMU ops with plausible extension words (68030: PMOVE, PTEST,
    PFLUSH, PLOAD — coprocessor id 0, MC68030 manual § 9.7)."""
    ea_mode = rng.choice([0x10, 0x28])  # (A0) or d16(A0)
    base = 0xF000 | ea_mode
    kind = rng.randrange(4)
    if kind == 0:
        # PMOVE to/from TC/SRP/CRP (fmt 010) or TT0/TT1 (fmt 000) or MMUSR (fmt 011)
        preg = rng.choice([(0b010, 0b000), (0b010, 0b010), (0b010, 0b011),
                           (0b000, 0b010), (0b000, 0b011), (0b011, 0b000)])
        rw = rng.getrandbits(1)  # 0 = mem→reg, 1 = reg→mem
        fd = 0
        ext = (preg[0] << 13) | (preg[1] << 10) | (rw << 9) | (fd << 8)
    elif kind == 1:
        # PTEST: 100 | LEVEL(3) | RW | A | AREG(3) | FC(5)
        level = rng.randrange(8)
        rw = rng.getrandbits(1)
        fc = rng.choice([0b10001, 0b10101, 0b01000 | rng.randrange(8)])
        ext = (0b100 << 13) | (level << 10) | (rw << 9) | fc
    elif kind == 2:
        # PFLUSH: 001 | MODE(3) | 0 | 0 | MASK... (fmt 0011 00MM MASK FC)
        mode = rng.choice([0b001, 0b100, 0b110])  # PFLUSHA / PFLUSH fc / fc,ea
        mask = rng.getrandbits(3) << 5
        fc = rng.getrandbits(5)
        ext = (0b001 << 13) | (mode << 10) | mask | fc
    else:
        # PLOAD: 001 | 000 | R/W | ... (fmt 0010 00x0 000 FC)
        rw = rng.getrandbits(1)
        ext = (0b001 << 13) | (rw << 9) | rng.getrandbits(5)
    words = [base, ext]
    if ea_mode == 0x28:
        words.append(rng.getrandbits(16) & 0x7FFF)
    return words


# ── Fault family (O4 slice 3) ─────────────────────────────────────────────
# Memory ops aimed straight at the table's invalid / write-protected /
# remapped pages so the corpus exercises translated bus FAULTS: format $A
# frames (last-write faults), format $B frames (read / mid-instruction
# faults), pending (An)± fixups, unaligned sub-access splits crossing page
# boundaries, MOVEM restart counters, MOVES and locked-RMW (TAS/CAS).
# A0 points at the chosen target, A1 at a good data page.

def _op_fault(rng: random.Random) -> list[int]:
    dn = rng.randrange(8)
    choices = [
        [0x2080 | dn],                                    # MOVE.L Dn,(A0)   $A
        [0x3080 | dn],                                    # MOVE.W Dn,(A0)
        [0x1080 | dn],                                    # MOVE.B Dn,(A0)
        [0x20C0 | dn],                                    # MOVE.L Dn,(A0)+
        [0x2100 | dn],                                    # MOVE.L Dn,-(A0)
        [0x2010],                                         # MOVE.L (A0),D0   $B
        [0x2018],                                         # MOVE.L (A0)+,D0
        [0x2020],                                         # MOVE.L -(A0),D0
        [0x1010],                                         # MOVE.B (A0),D0
        [0x3028, rng.getrandbits(16) & 0x7FFF],           # MOVE.W d16(A0),D0
        [0x2140 | dn, rng.getrandbits(16) & 0x7FFF],      # MOVE.L Dn,d16(A0)
        [0x2290],                                         # MOVE.L (A0),(A1)
        [0x22D8],                                         # MOVE.L (A0)+,(A1)+
        [0x0650, rng.getrandbits(16)],                    # ADDI.W #imm,(A0) rmw
        [0x48D0, rng.getrandbits(16)],                    # MOVEM.L list,(A0)
        [0x48E0, rng.getrandbits(16)],                    # MOVEM.L list,-(A0)
        [0x4CD0, rng.getrandbits(16)],                    # MOVEM.L (A0),list
        [0x4CD8, rng.getrandbits(16)],                    # MOVEM.L (A0)+,list
        [0x4AD0],                                         # TAS (A0)  (locked RMW)
        [0x0CD0, (2 << 6) | 1],                           # CAS.W D2,D1,(A0)
        [0x0E10 | rng.choice([0x00, 0x40, 0x80]),
         (rng.randrange(16) << 12) | (rng.getrandbits(1) << 11)],  # MOVES (A0)
        [0x2F10],                                         # MOVE.L (A0),-(SP)
    ]
    return rng.choice(choices)


# ── FPU family (O5 slice 1) ────────────────────────────────────────────────
# 68881/68882 coprocessor-id-1 ops (MC68881/MC68882UM § 4): general
# $F200|EA + command word (arithmetic, FMOVE all 7 data formats, FMOVECR,
# FMOVE(M) of control/FP registers), FBcc W/L, FScc/FDBcc/FTRAPcc/FNOP,
# FSAVE/FRESTORE. Weighted toward arithmetic. A-register targets are
# pre-seeded with special extended values by gen_state (family == "fpu").

# Monadic opmodes (command-word bits 6-0, MC68881UM Table 4-14) — incl.
# FMOVE-to-reg (0x00) and the transcendentals.
_FPU_MONADIC = [
    0x00,  # FMOVE
    0x01,  # FINT
    0x02,  # FSINH
    0x03,  # FINTRZ
    0x04,  # FSQRT
    0x06,  # FLOGNP1
    0x08,  # FETOXM1
    0x09,  # FTANH
    0x0A,  # FATAN
    0x0C,  # FASIN
    0x0D,  # FATANH
    0x0E,  # FSIN
    0x0F,  # FTAN
    0x10,  # FETOX
    0x11,  # FTWOTOX
    0x12,  # FTENTOX
    0x14,  # FLOGN
    0x15,  # FLOG10
    0x16,  # FLOG2
    0x18,  # FABS
    0x19,  # FCOSH
    0x1A,  # FNEG
    0x1C,  # FACOS
    0x1D,  # FCOS
    0x1E,  # FGETEXP
    0x1F,  # FGETMAN
    0x3A,  # FTST
]
_FPU_DYADIC = [
    0x20,  # FDIV
    0x21,  # FMOD
    0x22,  # FADD
    0x23,  # FMUL
    0x24,  # FSGLDIV
    0x25,  # FREM
    0x26,  # FSCALE
    0x27,  # FSGLMUL
    0x28,  # FSUB
    0x38,  # FCMP
]

# source format code (command-word bits 12-10) → immediate operand words
_FPU_FMT_WORDS = {0: 2, 1: 2, 2: 6, 3: 6, 4: 1, 5: 4, 6: 1}  # L S X P W D B
_FPU_FMT_DREG_OK = (0, 1, 4, 6)  # only B/W/L/S legal in a data register


def _fp_special_words(rng: random.Random) -> list[int]:
    """One 96-bit extended value as the 3 raw u32 of OracleState.fp[i]:
    [ (sign|15-bit exp) << 16, mantissa 63..32, mantissa 31..0 ].
    Mix of ±0/±inf/qNaN/sNaN/denormal/unnormal/normal (MC68881UM § 3.2)."""
    s = rng.getrandbits(1) << 31
    r = rng.random()
    if r < 0.10:                                    # ±0
        return [s, 0, 0]
    if r < 0.20:                                    # ±inf
        return [s | (0x7FFF << 16), 0, 0]
    if r < 0.30:                                    # quiet NaN (mant bit 62 set)
        return [s | (0x7FFF << 16),
                0xC0000000 | rng.getrandbits(30), rng.getrandbits(32)]
    if r < 0.38:                                    # signaling NaN (bit 62 clear)
        return [s | (0x7FFF << 16),
                0x80000000 | rng.getrandbits(30) & ~0x40000000,
                rng.getrandbits(32) | 1]
    if r < 0.46:                                    # denormal (exp 0, int bit 0)
        return [s, rng.getrandbits(31), rng.getrandbits(32) | 1]
    if r < 0.54:                                    # unnormal (exp != 0, int bit 0)
        return [s | (rng.randrange(1, 0x7FFF) << 16),
                rng.getrandbits(31), rng.getrandbits(32)]
    exp = rng.choice([                              # normal, across magnitudes
        0x3FFF + rng.randrange(-64, 65),
        rng.randrange(1, 0x7FFF),
        rng.choice([1, 2, 0x7FFD, 0x7FFE]),
    ])
    return [s | (exp << 16), 0x80000000 | rng.getrandbits(31),
            rng.getrandbits(32)]


def _fpu_imm_words(rng: random.Random, fmt: int) -> list[int]:
    """Immediate operand: raw words, biased toward special extended
    patterns for the X format, random bits otherwise."""
    n = _FPU_FMT_WORDS[fmt]
    if fmt == 2 and rng.random() < 0.7:  # X: real special value
        w = _fp_special_words(rng)
        return [w[0] >> 16, 0,  # sign/exp word, unused word
                w[1] >> 16, w[1] & 0xFFFF, w[2] >> 16, w[2] & 0xFFFF]
    return [rng.getrandbits(16) for _ in range(n)]


# ── FRESTORE frame seeding (O5 follow-up: FRESTORE acceptance) ────────────
# At ~60 % of FRESTORE operand addresses gen_state plants a well-formed
# state-frame image instead of leaving pure random bytes, so the whole
# frame-acceptance matrix of WinUAE fpuop_restore (oracle/uae/upstream/
# fpp.c:2755-2807) is actually exercised: NULL, 68881/68882 IDLE,
# 68881/68882 BUSY, the version-hack 68040 frames ($41 only —
# fpp.c:2799-2802 with fpu_no_unimplemented=false, get_fpu_version(68040)
# == $41), and wrong-version / wrong-length rejects (vector 14). Note the
# version byte of BOTH 6888x frame flavours is $1F (get_fpu_version,
# fpp.c:1432-1449) — there is no $1E.

def _plant_frestore_frame(ram: dict[int, int], rng: random.Random,
                          addr: int) -> None:
    """One state-frame image at addr: header long + plausible body."""
    r = rng.random()
    if r < 0.15:
        ver, size = 0x00, rng.choice([0x00, 0x18, 0x38])   # NULL (any size)
    elif r < 0.40:
        ver, size = 0x1F, 0x38                             # 68882 IDLE
    elif r < 0.55:
        ver, size = 0x1F, 0x18                             # 68881 IDLE
    elif r < 0.65:
        ver, size = 0x1F, 0xD4                             # 68882 BUSY (skip)
    elif r < 0.70:
        ver, size = 0x1F, 0xB4                             # 68881 BUSY (skip)
    elif r < 0.80:
        ver = 0x41                                         # 68040 hack frames
        size = rng.choice([0x00, 0x28, 0x30, 0x60])        # idle/unimp/busy
    elif r < 0.90:
        ver = rng.choice([0x40, 0x01, 0x1E, 0x42, 0xFF,    # wrong version
                          rng.randrange(1, 0x100)])        # -> vector 14
        if ver in (0x1F, 0x41):                            # keep it wrong
            ver += 0x21
        size = rng.choice([0x00, 0x18, 0x38, 0x60])
    else:
        ver = 0x1F                                         # wrong length
        size = rng.choice([0x00, 0x04, 0x20, 0x3C, 0x60, 0xFF])
    _w32(ram, addr, (ver << 24) | (size << 16) | rng.getrandbits(16))

    if ver == 0x1F and size in (0x18, 0x38):
        # Plausible IDLE internals: command/condition long, random internal
        # registers, an extended special as the exceptional operand, the
        # operand register, then BIU flags — bit 27 clear re-arms a pending
        # exception on FRESTORE (fpp.c:2781-2787), so flip it randomly.
        _w32(ram, addr + 4, rng.getrandbits(32))
        eo = addr + 8 + (size - 24)
        for a in range(addr + 8, eo, 4):
            _w32(ram, a, rng.getrandbits(32))
        w = _fp_special_words(rng)
        _w32(ram, eo, w[0]); _w32(ram, eo + 4, w[1]); _w32(ram, eo + 8, w[2])
        _w32(ram, eo + 12, rng.getrandbits(32))
        biu = rng.getrandbits(32)
        if rng.random() < 0.5:
            biu |= 0x08000000
        else:
            biu &= ~0x08000000
        _w32(ram, eo + 16, biu)
    elif ver == 0x41:
        # 68040 body: keep CU_SAVEPC (top byte of the long at +4) off $FE
        # so the unimplemented BUSY-resume path never triggers.
        _w32(ram, addr + 4, rng.getrandbits(24))
        for off in range(8, size + 4, 4):
            _w32(ram, addr + off, rng.getrandbits(32))
    elif size:
        for off in range(4, min(size, 0xD4) + 4, 4):       # skipped body
            _w32(ram, addr + off, rng.getrandbits(32))


def _op_fpu(rng: random.Random) -> list[int]:
    an = rng.randrange(8)
    cond = rng.randrange(32)

    def mem_ea() -> tuple[int, list[int]]:
        """(An)/(An)+/-(An)/d16(An) — memory-alterable, read+write safe."""
        c = rng.randrange(4)
        if c == 0:
            return 0x10 | an, []
        if c == 1:
            return 0x18 | an, []
        if c == 2:
            return 0x20 | an, []
        return 0x28 | an, [rng.getrandbits(16) & 0x7FFF]

    r = rng.random()
    if r < 0.40:
        # arithmetic: dyadic 60 % / monadic (incl. transcendentals) 40 %,
        # FSINCOS sprinkled in; sources reg-to-reg or ea-to-reg (any fmt)
        if rng.random() < 0.6:
            opmode = rng.choice(_FPU_DYADIC)
        elif rng.random() < 0.12:
            opmode = 0x30 | rng.randrange(8)     # FSINCOS, cos → FPc
        else:
            opmode = rng.choice(_FPU_MONADIC)
        dst = rng.randrange(8)
        if rng.random() < 0.5:                   # R/M=0: FPm source
            ext = (rng.randrange(8) << 10) | (dst << 7) | opmode
            return [0xF200, ext]
        fmt = rng.choice([0, 1, 2, 2, 3, 4, 5, 6])  # X twice: main format
        ext = 0x4000 | (fmt << 10) | (dst << 7) | opmode
        if fmt in _FPU_FMT_DREG_OK and rng.random() < 0.2:
            return [0xF200 | rng.randrange(8), ext]        # Dn source
        if rng.random() < 0.15:                             # #imm source
            return [0xF23C, ext] + _fpu_imm_words(rng, fmt)
        ea, extra = mem_ea()
        return [0xF200 | ea, ext] + extra
    if r < 0.52:
        # FMOVE FPn,<ea> (opclass 011) in all 7 formats; P with k-factor
        fmt = rng.choice([0, 1, 2, 3, 3, 4, 5, 6])          # P twice: weak spot
        src = rng.randrange(8)
        k = 0
        if fmt == 3:
            k = rng.getrandbits(7)               # static k-factor
        ext = 0x6000 | (fmt << 10) | (src << 7) | k
        if fmt in _FPU_FMT_DREG_OK and rng.random() < 0.2:
            return [0xF200 | rng.randrange(8), ext]
        ea, extra = mem_ea()
        return [0xF200 | ea, ext] + extra
    if r < 0.57:
        # FMOVECR #cc,FPn (opclass 010, R/M=1, fmt 111)
        off = rng.choice([0x00, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                          0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
                          0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
                          0x3C, 0x3D, 0x3E, 0x3F,
                          rng.getrandbits(7)])   # incl. undefined offsets
        return [0xF200, 0x5C00 | (rng.randrange(8) << 7) | off]
    if r < 0.65:
        # FMOVE(M) control registers (opclass 100/101), list bits 12-10 =
        # FPCR|FPSR|FPIAR; memory EA (Dn only for a single register)
        sel = rng.randrange(1, 8)
        dr = rng.getrandbits(1)                  # 0 = ea→cr, 1 = cr→ea
        ext = ((0b101 if dr else 0b100) << 13) | (sel << 10)
        if sel in (1, 2, 4) and rng.random() < 0.25:
            return [0xF200 | rng.randrange(8), ext]        # Dn
        if not dr and rng.random() < 0.15:                 # #imm source
            return [0xF23C, ext] + [rng.getrandbits(16) for _ in range(2 * bin(sel).count("1"))]
        ea, extra = mem_ea()
        return [0xF200 | ea, ext] + extra
    if r < 0.73:
        # FMOVEM of FP registers (opclass 110/111): static and dynamic
        # lists, predecrement and postincrement forms (MC68881UM § 4.5.3)
        dr = rng.getrandbits(1)                  # 0 = mem→regs, 1 = regs→mem
        dynamic = rng.random() < 0.3
        lst = (rng.randrange(8) << 4) if dynamic else max(1, rng.getrandbits(8))
        if dr:                                   # regs→mem: -(An) or control
            if rng.random() < 0.6:
                ea, extra = 0x20 | an, []
                mode = 0b01 if dynamic else 0b00     # predecrement forms
            else:
                ea, extra = mem_ea()
                if (ea & 0x38) in (0x18, 0x20):      # not legal for static ctl
                    ea = 0x10 | an
                mode = 0b11 if dynamic else 0b10
            ext = 0xE000 | (mode << 11) | lst
        else:                                    # mem→regs: (An)+/control
            if rng.random() < 0.6:
                ea, extra = 0x18 | an, []
            else:
                ea, extra = 0x10 | an, []
            mode = 0b11 if dynamic else 0b10         # postincrement forms
            ext = 0xC000 | (mode << 11) | lst
        return [0xF200 | ea, ext] + extra
    if r < 0.80:
        # conditionals: FBcc.W/.L, FScc, FDBcc, FTRAPcc, FNOP
        c = rng.randrange(5)
        if c == 0:
            return [0xF280 | cond, rng.choice([2, 4, 8, 0x10, 0x20])]
        if c == 1:
            return [0xF2C0 | cond, 0, rng.choice([2, 4, 8, 0x10, 0x20])]
        if c == 2:
            if rng.random() < 0.4:
                return [0xF240 | rng.randrange(8), cond]   # FScc Dn
            ea, extra = mem_ea()
            return [0xF240 | ea, cond] + extra
        if c == 3:
            return [0xF248 | rng.randrange(8), cond, rng.choice([2, 4, 8])]
        return rng.choice([
            [0xF27A, cond, rng.getrandbits(16)],           # FTRAPcc.W
            [0xF27B, cond, rng.getrandbits(16), rng.getrandbits(16)],
            [0xF27C, cond],                                # FTRAPcc (no opd)
            [0xF280, 0x0000],                              # FNOP
        ])
    if r < 0.90:
        # FSAVE <ea> ($F300): -(An) / (An) / d16(An)
        ea = rng.choice([0x20 | an, 0x10 | an, 0x28 | an])
        w = [0xF300 | ea]
        if (ea & 0x38) == 0x28:
            w.append(rng.getrandbits(16) & 0x7FFF)
        return w
    # FRESTORE <ea> ($F340): (An) / (An)+ / d16(An)
    ea = rng.choice([0x10 | an, 0x18 | an, 0x28 | an])
    w = [0xF340 | ea]
    if (ea & 0x38) == 0x28:
        w.append(rng.getrandbits(16) & 0x7FFF)
    return w


FAMILIES = {"random": _op_random, "core": _op_core, "mmu": _op_mmu,
            "fault": _op_fault, "fpu": _op_fpu}


def gen_state(rng: random.Random, family: str = "core",
              mmu: str = "off") -> State:
    """One complete initial state. mmu: off | identity | tt."""
    st = State()
    ram: dict[int, int] = {}
    build_vector_table(ram)

    st.d = [rng.getrandbits(32) for _ in range(8)]
    # Address regs point at data pages (word-aligned) so EA ops hit real RAM.
    st.a = [(DATA_BASE + rng.randrange(0, 0x100000)) & ~1 for _ in range(8)]
    st.usp, st.isp, st.msp = STACK_USP, STACK_ISP, STACK_MSP

    super_mode = rng.random() < 0.7  # MMU/priv ops need S=1 to be interesting
    m_bit = rng.random() < 0.3
    st.sr = (0x2000 if super_mode else 0) | (0x1000 if m_bit and super_mode else 0) \
            | (0x0700) | rng.getrandbits(5)
    st.a[7] = (st.msp if m_bit else st.isp) if super_mode else st.usp

    st.pc = CODE_BASE + rng.randrange(0, 0x4000) & ~1
    st.vbr = VEC_BASE
    st.sfc, st.dfc = rng.randrange(8), rng.randrange(8)

    kinds: dict[int, str] = {}
    if mmu == "identity":
        kinds = build_identity_table(ram, rng)
        st.tc, st.crp = TC_FUZZ, CRP_FUZZ
        st.srp = CRP_FUZZ
    elif mmu == "tt":
        # MMU enabled but everything matches TT0 (transparent): base $00,
        # mask $FF (all a31-24 values), E bit 15, both FC ignored via mask.
        kinds = build_identity_table(ram, rng)
        st.tc, st.crp, st.srp = TC_FUZZ, CRP_FUZZ, CRP_FUZZ
        st.tt0 = (0x00 << 24) | (0xFF << 16) | (1 << 15) | (1 << 10) | (7 << 4)
    # else: MMU off (tc.E = 0)

    if family == "fault":
        # Aim A0 at one of the table's bad pages (O4 slice 3); with no
        # bad page available (mmu off) fall back to a random data page.
        # Offsets deliberately include odd addresses and page boundaries
        # so unaligned sub-accesses straddle good/bad pages.
        bad = sorted(kinds)
        page = rng.choice(bad) if bad else (DATA_BASE >> PAGE_SHIFT)
        offset = rng.choice([
            rng.randrange(PAGE_SIZE) & ~1,
            rng.randrange(PAGE_SIZE) | 1,
            0, 1, 2, PAGE_SIZE - 1, PAGE_SIZE - 2, PAGE_SIZE - 3, PAGE_SIZE - 4,
        ])
        st.a[0] = ((page << PAGE_SHIFT) + offset) & MASK
        st.a[1] = (DATA_BASE + rng.randrange(0, 0x10000)) & ~1

    # The op is generated before the family-specific RAM seeding so the
    # seeding can aim at its operands (FRESTORE frame planting below).
    words = FAMILIES[family](rng)

    if family == "fpu":
        # 68881/68882 initial state (O5 slice 1): special values in every
        # FP register, random rounding/precision, exception enables mostly
        # masked (they trap through vectors 48-54 when set — ~10 %).
        st.fp = [_fp_special_words(rng) for _ in range(8)]
        fpcr = rng.getrandbits(8) & 0xF0             # PREC+RND, bits 7-4
        if rng.random() < 0.10:
            fpcr |= (rng.getrandbits(8) << 8) & 0xFF00   # enable byte
        st.fpcr = fpcr                               # fpcr_mask 0xfff0
        st.fpsr = rng.getrandbits(32) & 0x0FFFFFF8   # CC/quot/EXC/AEXC
        # Seed each A-register target with a raw 96-bit extended special
        # value on both sides of the pointer (covers (An)/(An)+ and -(An),
        # and the narrower B/W/L/S/D/P reads of the same bytes) + a random
        # tail so multi-operand reads see nonzero data.
        for i in range(8):
            base = st.a[i] & MASK
            for org, w in ((base, _fp_special_words(rng)),
                           (base - 12, _fp_special_words(rng))):
                if rng.random() < 0.8:
                    _w32(ram, org, w[0])
                    _w32(ram, org + 4, w[1])
                    _w32(ram, org + 8, w[2])
                else:
                    for off in range(12):
                        ram[(org + off) & MASK] = rng.getrandbits(8)
            for off in range(12, 24):
                ram[(base + off) & MASK] = rng.getrandbits(8)

        # Targeted FRESTORE seeding: plant a well-formed frame image at
        # the operand address ~60 % of the time (after the special-value
        # seeding above so the frame wins the overlap).
        if (words[0] & 0xFFC0) == 0xF340 and rng.random() < 0.60:
            mode, an = (words[0] >> 3) & 7, words[0] & 7
            ea = st.a[an] + (words[1] if mode == 5 else 0)  # d16 is 0..$7FFF
            _plant_frestore_frame(ram, rng, ea & MASK)

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
