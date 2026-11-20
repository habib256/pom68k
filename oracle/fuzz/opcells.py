#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# Targeted cells: N random states per instruction FORM, where the random
# families reach a form once in thousands of vectors or never (the CCR
# immediates, STOP, MOVEC, LINK A7, the 68040's pipeline-synchronizing
# instructions). Written on 2026-11-20 for the trace sweep; a form list is
# cheap to extend.
#
#   opcells.py generate --a liboracle_uae.so --cpu 030 --trace t0 --out cells/v.json
#   sst68030 cells --dump > cells/dump.txt
#   opcells.py report cells
#
# Every "forms" vector starts on EXG D0,D0 and runs two steps, so that under
# --trace t0|t1 the form is the traced instruction (the oracle never traces
# the first step after a load; fuzz030.py --trace).
#
# --set classes is the class-balanced sweep: N states for each small
# ENCODING CLASS with its register fields, extension words and operands
# drawn at random — one step untraced, or behind the lead-in under --trace. A uniform random opcode is a MOVE two times in
# three; LINK is 16 words of 65 536 and LINK A7 two of them, which is how
# the 68030's LINK A7 (ruling D42) and the 68040's MULx.L Dn,Dm:Dm survived
# every random corpus until 2026-11-20.
#
# The report says, per
# form, how many vectors the oracle traced and how many the runner failed,
# so "traced by the oracle, failing here" and "not traced, failing" read
# apart. A failing form is a question, not a verdict: the format-$2
# instruction-address ruling (D12) fails CHK/TRAPV/TRAPcc cells on its own,
# and a stopped processor differs in PC alone.

from __future__ import annotations

import argparse
import collections
import json
import random
import re
import sys

# (name, opcode words). Supervisor state is forced; A0 points at RAM.
COMMON = [
    ("NOP", [0x4E71]), ("MOVEQ", [0x7001]),
    ("ORI_CCR", [0x003C, 0x0001]), ("ANDI_CCR", [0x023C, 0x00FE]), ("EORI_CCR", [0x0A3C, 0x0001]),
    ("MOVE_D0_CCR", [0x44C0]), ("MOVE_IMM_CCR", [0x44FC, 0x0005]),
    ("ORI_SR_0", [0x007C, 0x0000]), ("ANDI_SR_clrT0", [0x027C, 0xBFFF]), ("EORI_SR_0", [0x0A7C, 0x0000]),
    ("MOVE_SR_T0", [0x46FC, 0x6700]), ("MOVE_SR_noT", [0x46FC, 0x2700]), ("MOVE_SR_T1", [0x46FC, 0xA700]),
    ("STOP_T0", [0x4E72, 0x6700]), ("STOP_noT", [0x4E72, 0x2700]),
    ("RTS", [0x4E75]), ("RTR", [0x4E77]), ("RTD", [0x4E74, 0x0004]), ("RTE", [0x4E73]),
    ("TRAPV", [0x4E76]), ("TRAP1", [0x4E41]), ("CHK", [0x4181]), ("DIVU", [0x80C1]),
    ("MOVEC_D0_VBR", [0x4E7B, 0x0801]), ("MOVEC_VBR_D0", [0x4E7A, 0x0801]),
    ("MOVE_A0_USP", [0x4E60]), ("MOVE_USP_A0", [0x4E68]), ("MOVE_SR_D0", [0x40C0]),
    ("BRA", [0x6004]), ("BEQ", [0x6704]), ("BSR", [0x6104]),
    ("DBRA", [0x51C8, 0x0004]), ("DBT", [0x50C8, 0x0004]),
    ("JMP_abs", [0x4EF9, 0x0001, 0x0800]), ("JSR_abs", [0x4EB9, 0x0001, 0x0800]), ("JMP_ind", [0x4ED0]),
    ("LINK_W_A7", [0x4E57, 0x0010]), ("LINK_L_A7", [0x480F, 0x0000, 0x0010]),
    ("LINK_W_A6", [0x4E56, 0xFFF0]), ("UNLK", [0x4E5E]),
    ("RESET", [0x4E70]), ("TAS_D0", [0x4AC0]),
    ("MOVES_w", [0x0E50, 0x0800]), ("MOVES_r", [0x0E50, 0x0000]), ("CAS", [0x0ED0, 0x0040]),
    ("BKPT", [0x4848]), ("ILLEGAL", [0x4AFC]), ("TRAPF", [0x51FC]), ("TRAPT", [0x50FC]),
    ("Scc", [0x50C0]), ("SWAP", [0x4840]), ("EXT", [0x4880]), ("PEA", [0x4850]), ("LEA", [0x41D0]),
    ("FNOP", [0xF280, 0x0000]), ("FBT", [0xF28F, 0x0004]), ("FBF", [0xF280, 0x0004]),
    ("FDBF", [0xF248, 0x0000, 0x0004]), ("FDBT", [0xF248, 0x000F, 0x0004]),
    ("FSAVE", [0xF310]), ("FMOVEM_out", [0xF210, 0xF0FF]), ("FMOVE_D0_FP0", [0xF200, 0x4000]),
    ("FScc", [0xF240, 0x000F]), ("FTRAPF", [0xF27C, 0x0000]),
]
ONLY_030 = [("PFLUSHA", [0xF000, 0x2400]), ("PMOVE_TC", [0xF010, 0x4200])]
ONLY_040 = [
    ("CINVA", [0xF4D8]), ("CPUSHA", [0xF4F8]), ("CINVL", [0xF448]),
    ("PFLUSHA", [0xF518]), ("PFLUSH", [0xF508]), ("PTESTR", [0xF568]), ("MOVE16", [0xF620, 0x9000]),
    ("FRESTORE", [0xF350]), ("FMOVEM_in", [0xF210, 0xD0FF]),
    ("FMOVEM_ctl_out", [0xF210, 0xBC00]), ("FMOVEM_ctl_in", [0xF210, 0x9C00]),
    ("FMOVE_FPCR_D0", [0xF200, 0xB000]), ("FMOVE_D0_FPCR", [0xF200, 0x9000]), ("FTST", [0xF200, 0x003A]),
]
TRACE_FRAME = 0x2024    # format $2, vector 9


def classes(cpu: str) -> list[tuple[str, int, int, int]]:
    """(name, base opcode, mask of the bits drawn at random, extension words)."""
    out: list[tuple[str, int, int, int]] = []
    c = lambda name, base, mask=0, ext=0: out.append((name, base, mask, ext))
    c("LINK_W", 0x4E50, 7, 1); c("LINK_L", 0x4808, 7, 2); c("UNLK", 0x4E58, 7)
    c("SWAP", 0x4840, 7); c("EXT_W", 0x4880, 7); c("EXT_L", 0x48C0, 7); c("EXTB", 0x49C0, 7)
    c("EXG_DD", 0xC140, 0x0E07); c("EXG_AA", 0xC148, 0x0E07); c("EXG_DA", 0xC188, 0x0E07)
    c("ABCD_R", 0xC100, 0x0E07); c("ABCD_M", 0xC108, 0x0E07)
    c("SBCD_R", 0x8100, 0x0E07); c("SBCD_M", 0x8108, 0x0E07)
    c("PACK_R", 0x8140, 0x0E07, 1); c("PACK_M", 0x8148, 0x0E07, 1)
    c("UNPK_R", 0x8180, 0x0E07, 1); c("UNPK_M", 0x8188, 0x0E07, 1)
    for size, n in ((0, "B"), (1, "W"), (2, "L")):
        c("CMPM_" + n, 0xB108 | size << 6, 0x0E07)
        c("ADDX_R_" + n, 0xD100 | size << 6, 0x0E07); c("ADDX_M_" + n, 0xD108 | size << 6, 0x0E07)
        c("SUBX_R_" + n, 0x9100 | size << 6, 0x0E07); c("SUBX_M_" + n, 0x9108 | size << 6, 0x0E07)
        c("CHK2CMP2_" + n, 0x00D0 | size << 9, 7, 1); c("MOVES_" + n, 0x0E10 | size << 6, 7, 1)
        c("CLR_predec_" + n, 0x4220 | size << 6, 7); c("NEG_postinc_" + n, 0x4418 | size << 6, 7)
        c("TST_postinc_" + n, 0x4A18 | size << 6, 7)
    c("DBcc", 0x50C8, 0x0F07, 1); c("Scc_Dn", 0x50C0, 0x0F07); c("Scc_ind", 0x50D0, 0x0F07)
    c("Scc_predec", 0x50E0, 0x0F07)
    c("MOVEP_W_R", 0x0108, 0x0E07, 1); c("MOVEP_L_R", 0x0148, 0x0E07, 1)
    c("MOVEP_W_M", 0x0188, 0x0E07, 1); c("MOVEP_L_M", 0x01C8, 0x0E07, 1)
    c("TRAPcc_W", 0x50FA, 0x0F00, 1); c("TRAPcc_L", 0x50FB, 0x0F00, 2); c("TRAPcc", 0x50FC, 0x0F00)
    c("RTD", 0x4E74, 0, 1); c("RTR", 0x4E77)
    c("MOVE_to_USP", 0x4E60, 7); c("MOVE_from_USP", 0x4E68, 7)
    c("MOVEC_from", 0x4E7A, 0, 1); c("MOVEC_to", 0x4E7B, 0, 1)
    c("CAS2_W", 0x0CFC, 0, 2); c("CAS2_L", 0x0EFC, 0, 2)
    c("CAS_B", 0x0AD0, 7, 1); c("CAS_W", 0x0CD0, 7, 1); c("CAS_L", 0x0ED0, 7, 1)
    c("TAS_ind", 0x4AD0, 7); c("TAS_Dn", 0x4AC0, 7); c("NBCD_Dn", 0x4800, 7); c("NBCD_ind", 0x4810, 7)
    c("MULL_Dn", 0x4C00, 7, 1); c("MULL_ind", 0x4C10, 7, 1)
    c("DIVL_Dn", 0x4C40, 7, 1); c("DIVL_ind", 0x4C50, 7, 1)
    for t, n in enumerate(("BFTST", "BFEXTU", "BFCHG", "BFEXTS", "BFCLR", "BFFFO", "BFSET", "BFINS")):
        c(n + "_Dn", 0xE8C0 | t << 8, 7, 1); c(n + "_ind", 0xE8D0 | t << 8, 7, 1)
    for t, n in enumerate(("AS", "LS", "ROX", "RO")):
        c(n + "R_mem", 0xE0D0 | t << 9, 7); c(n + "L_mem", 0xE1D0 | t << 9, 7)
    c("BKPT", 0x4848, 7)
    c("MOVEM_predec_L", 0x48E0, 7, 1); c("MOVEM_postinc_L", 0x4CD8, 7, 1)
    c("MOVEM_predec_W", 0x48A0, 7, 1); c("MOVEM_postinc_W", 0x4C98, 7, 1)
    c("MOVEB_Dn_predecA7", 0x1F00, 7); c("MOVEB_postincA7_Dn", 0x101F, 0x0E00)
    c("MOVEL_A7_predecA7", 0x2F0F); c("MOVEL_A7_postincA7", 0x2ECF)
    c("PEA_A7", 0x4857); c("JSR_A7", 0x4E97)
    c("ADDQ_A7", 0x504F, 0x0E00); c("SUBQ_A7", 0x514F, 0x0E00); c("ADDQB_predecA7", 0x5027, 0x0E00)
    c("MOVE_from_SR", 0x40C0, 7); c("MOVE_from_CCR", 0x42C0, 7)
    c("MOVE_to_CCR", 0x44C0, 7); c("MOVE_to_SR", 0x46C0, 7)
    c("ANDI_SR", 0x027C, 0, 1); c("ORI_SR", 0x007C, 0, 1); c("EORI_SR", 0x0A7C, 0, 1)
    c("ANDI_CCR", 0x023C, 0, 1); c("ORI_CCR", 0x003C, 0, 1); c("EORI_CCR", 0x0A3C, 0, 1)
    c("CHK_W", 0x4180, 0x0E07); c("CHK_L", 0x4100, 0x0E07)
    c("DIVU_Dn", 0x80C0, 0x0E07); c("DIVS_Dn", 0x81C0, 0x0E07)
    c("MULU_Dn", 0xC0C0, 0x0E07); c("MULS_Dn", 0xC1C0, 0x0E07)
    c("BSET_Dn_ind", 0x01D0, 0x0E07); c("BCHG_imm_Dn", 0x0840, 7, 1); c("BTST_imm_ind", 0x0810, 7, 1)
    c("CMPA_W", 0xB0C8, 0x0E07); c("ADDA_W", 0xD0C8, 0x0E07)
    c("SUBA_L", 0x91C8, 0x0E07); c("MOVEA_W", 0x3048, 0x0E07)
    c("LEA_d16PC", 0x41FA, 0x0E00, 1); c("PEA_d16", 0x4868, 7, 1); c("JMP_d16PC", 0x4EFA, 0, 1)
    c("BSR_W", 0x6100, 0, 1); c("BSR_L", 0x61FF, 0, 2); c("Bcc_L", 0x60FF, 0x0F00, 2)
    c("RESET", 0x4E70); c("TRAPV", 0x4E76); c("STOP", 0x4E72, 0, 1)
    c("ILLEGAL", 0x4AFC); c("LINEA", 0xA000, 0x0FFF)
    if cpu == "040":
        c("MOVE16_abs", 0xF600, 0x1F, 2); c("MOVE16_pi", 0xF620, 7, 1)
        c("CINV", 0xF408, 0xD7); c("CPUSH", 0xF428, 0xD7)
        c("PFLUSH040", 0xF500, 0x1F); c("PTEST040", 0xF548, 0x27)
    else:
        c("CALLM", 0x06D0, 7, 1); c("RTM", 0x06C0, 0xF)
    return out


# EA kinds of the addressing-mode sweep: (mode bits, register drawn at
# random, EA extension words). "ixb" is a brief index word, "ixf" a full
# one restricted to its defined encodings (bit 3 clear, a base
# displacement size, no reserved I/IS), so that a failure is never a
# reserved-encoding question.
EA_KINDS = {
    "dn": (0x00, True), "an": (0x08, True), "ind": (0x10, True), "pi": (0x18, True),
    "pd": (0x20, True), "d16": (0x28, True), "ixb": (0x30, True), "ixf": (0x30, True),
    "absw": (0x38, False), "absl": (0x39, False), "pcd16": (0x3A, False),
    "pcixb": (0x3B, False), "pcixf": (0x3B, False), "imm": (0x3C, False),
}
CTL = ("ind", "d16", "ixb", "ixf", "absw", "absl", "pcd16", "pcixb", "pcixf")
CTL_ALT = CTL[:6]
MEM_ALT = ("ind", "pi", "pd") + CTL[1:6]
DATA_ALT = ("dn",) + MEM_ALT
DATA = DATA_ALT + ("pcd16", "pcixb", "pcixf", "imm")


def ea_classes(cpu: str) -> list[tuple]:
    """(name, base opcode, random opcode bits, [(base, mask)] per instruction
    extension word, EA kinds, immediate words) — the 68020 additions and the
    system instructions over every addressing mode they accept, with their
    extension words drawn inside the defined encodings (a uniform 16-bit
    MOVEC or PMOVE operand is illegal almost every time)."""
    out: list[tuple] = []
    c = lambda name, base, mask, shapes, kinds, imm=0: out.append((name, base, mask, shapes, kinds, imm))
    for t, n in enumerate(("BFTST", "BFEXTU", "BFCHG", "BFEXTS", "BFCLR", "BFFFO", "BFSET", "BFINS")):
        c(n, 0xE8C0 | t << 8, 0, [(0, 0x7FFF)], ("dn",) + (CTL if t in (0, 1, 3, 5) else CTL_ALT))
    for size, n in ((1, "B"), (2, "W"), (3, "L")):
        c("CAS_" + n, 0x08C0 | size << 9, 0, [(0, 0x01C7)], MEM_ALT)
    for size, n in ((0, "B"), (1, "W"), (2, "L")):
        c("CHK2CMP2_" + n, 0x00C0 | size << 9, 0, [(0, 0xF800)], CTL)
        c("MOVES_" + n, 0x0E00 | size << 6, 0, [(0, 0xF800)], MEM_ALT)
        c("TST_" + n, 0x4A00 | size << 6, 0, [], (("an",) if size else ()) + ("pcd16", "pcixb", "pcixf", "imm"),
          2 if size == 2 else 1)
        c("CMPI_" + n, 0x0C00 | size << 6, 0, [], ("pcd16", "pcixb", "pcixf"), -(2 if size == 2 else 1))
    c("CAS2_W", 0x0CFC, 0, [(0, 0xF1C7), (0, 0xF1C7)], ())
    c("CAS2_L", 0x0EFC, 0, [(0, 0xF1C7), (0, 0xF1C7)], ())
    c("MULL", 0x4C00, 0, [(0, 0x7C07)], DATA, 2)
    c("DIVL", 0x4C40, 0, [(0, 0x7C07)], DATA, 2)
    c("CHK_W", 0x4180, 0x0E00, [], DATA, 1); c("CHK_L", 0x4100, 0x0E00, [], DATA, 2)
    c("TAS", 0x4AC0, 0, [], DATA_ALT); c("NBCD", 0x4800, 0, [], DATA_ALT)
    c("Scc", 0x50C0, 0x0F00, [], DATA_ALT)
    c("MOVEM_W_out", 0x4880, 0, [(0, 0xFFFF)], ("pd",) + CTL_ALT)
    c("MOVEM_L_out", 0x48C0, 0, [(0, 0xFFFF)], ("pd",) + CTL_ALT)
    c("MOVEM_W_in", 0x4C80, 0, [(0, 0xFFFF)], ("pi",) + CTL)
    c("MOVEM_L_in", 0x4CC0, 0, [(0, 0xFFFF)], ("pi",) + CTL)
    c("PEA", 0x4840, 0, [], CTL); c("LEA", 0x41C0, 0x0E00, [], CTL)
    c("JMP", 0x4EC0, 0, [], CTL); c("JSR", 0x4E80, 0, [], CTL)
    c("MOVE_from_SR", 0x40C0, 0, [], DATA_ALT); c("MOVE_from_CCR", 0x42C0, 0, [], DATA_ALT)
    c("MOVE_to_CCR", 0x44C0, 0, [], DATA, 1); c("MOVE_to_SR", 0x46C0, 0, [], DATA, 1)
    for t, n in enumerate(("BTST", "BCHG", "BCLR", "BSET")):
        c(n + "_dyn", 0x0100 | t << 6, 0x0E00, [], DATA if t == 0 else DATA_ALT, 1)
        c(n + "_imm", 0x0800 | t << 6, 0, [(0, 0x00FF)],
          DATA_ALT + (("pcd16", "pcixb", "pcixf") if t == 0 else ()))
    for t, n in enumerate(("AS", "LS", "ROX", "RO")):
        c(n + "R_mem", 0xE0C0 | t << 9, 0, [], MEM_ALT); c(n + "L_mem", 0xE1C0 | t << 9, 0, [], MEM_ALT)
    c("ADDQ_W_An", 0x5040, 0x0E00, [], ("an",)); c("SUBQ_L_An", 0x5180, 0x0E00, [], ("an",))
    c("ADDA_W", 0xD0C0, 0x0E00, [], DATA + ("an",), 1); c("CMPA_L", 0xB1C0, 0x0E00, [], DATA + ("an",), 2)
    c("MOVEC_from_00x", 0x4E7A, 0, [(0x0000, 0xF007)], ()); c("MOVEC_to_00x", 0x4E7B, 0, [(0x0000, 0xF007)], ())
    c("MOVEC_from_80x", 0x4E7A, 0, [(0x0800, 0xF007)], ()); c("MOVEC_to_80x", 0x4E7B, 0, [(0x0800, 0xF007)], ())
    if cpu == "040":
        c("MOVE16_abs", 0xF600, 0x1F, [(0x0000, 0x00FF), (0, 0xFFFF)], ())
        c("MOVE16_pi", 0xF620, 7, [(0x8000, 0x7000)], ())
    else:
        c("PMOVE_TC_SRP_CRP", 0xF000, 0, [(0x4000, 0x1F00)], CTL_ALT)
        c("PMOVE_TT", 0xF000, 0, [(0x0800, 0x0700)], CTL_ALT)
        c("PMOVE_MMUSR", 0xF000, 0, [(0x6000, 0x0200)], CTL_ALT)
        c("PFLUSH_PLOAD", 0xF000, 0, [(0x2000, 0x1FFF)], CTL_ALT)
        c("PTEST", 0xF000, 0, [(0x8000, 0x1FFF)], CTL_ALT)
    return out


def ea_words(rng: random.Random, kind: str, imm: int) -> list[int]:
    """The extension words of one EA of `kind`; `imm` is the immediate size
    in words."""
    if kind in ("d16", "pcd16") and rng.getrandbits(1):
        # Half of the displacements stay inside the seeded bytes around An
        # (or the code around PC): a random one reads the zeroes of an empty
        # RAM, and DIVL divides by zero every time.
        return [rng.randrange(-16, 13) & 0xFFFF]
    if kind in ("d16", "absw", "pcd16"):
        return [rng.getrandbits(16)]
    if kind == "absl":
        return [rng.getrandbits(8), rng.getrandbits(16)]
    if kind == "imm":
        return [rng.getrandbits(16) for _ in range(imm)]
    if kind in ("ixb", "pcixb"):
        return [rng.getrandbits(16) & ~0x0100]
    if kind in ("ixf", "pcixf"):
        bd = rng.choice((1, 2, 3))
        iis = rng.choice((0, 1, 2, 3, 5, 6, 7))
        suppress = rng.getrandbits(1)
        if suppress:
            iis = rng.choice((0, 1, 2, 3))
        word = (rng.getrandbits(16) & 0xFE80) | 0x0100 | suppress << 6 | bd << 4 | iis
        words = [word] + [rng.getrandbits(16) for _ in range(bd - 1)]
        if bd == 3:
            words[1] &= 0x00FF
        od = iis & 3
        return words + ([rng.getrandbits(16) for _ in range(od - 1)] if od > 1 else [])
    return []


def spread(rng: random.Random, mask: int) -> int:
    """Random bits in the positions `mask` selects."""
    value, bit = 0, 1
    while bit <= mask:
        if mask & bit and rng.getrandbits(1):
            value |= bit
        bit <<= 1
    return value


def generate(args) -> int:
    if args.cpu == "030":
        from gen030 import gen_state
        import sst030 as sst
        import fuzz030 as fz
    else:
        from gen040 import gen_state
        import sst040 as sst
        import fuzz040 as fz
    from oracle_driver import Oracle

    oracle = Oracle(args.a) if args.cpu == "030" else Oracle(args.a, model=68040, fpu=68040)
    rng = random.Random(args.seed)
    vectors = []
    if args.set in ("classes", "ea"):
        # Untraced: one step. Traced: the lead-in (EXG D0,D0) takes the
        # step the oracle never traces, and the class member is the second.
        traced = args.trace != "none"
        bit = {"none": 0, "t0": 0x4000, "t1": 0x8000}[args.trace]
        table = classes(args.cpu)
        members = [(name, lambda b=base, m=mask, e=ext:
                    [b | spread(rng, m)] + [rng.getrandbits(16) for _ in range(e)])
                   for name, base, mask, ext in table]
        if args.set == "ea":
            # One class per (instruction, addressing mode).
            members = []
            for name, base, mask, shapes, kinds, imm in ea_classes(args.cpu):
                def draw(b=base, m=mask, sh=shapes, kind=None, imm=imm):
                    op = b | spread(rng, m)
                    if kind:
                        bits, has_reg = EA_KINDS[kind]
                        op |= bits | (rng.randrange(8) if has_reg else 0)
                    inst = [sb | spread(rng, sm) for sb, sm in sh]
                    # A negative `imm` is an immediate that precedes the EA
                    # extension (CMPI); a positive one is the EA itself.
                    lead = [rng.getrandbits(16) for _ in range(-imm)] if imm < 0 else []
                    return [op] + inst + lead + (ea_words(rng, kind, imm) if kind else [])
                if not kinds:
                    members.append((name, draw))
                for kind in kinds:
                    members.append((f"{name}.{kind}", lambda d=draw, k=kind: d(kind=k)))
        for name, member in members:
            for _ in range(args.n):
                st = gen_state(rng, family="core", mmu=args.mmu)
                if args.user:
                    st.sr = (st.sr & 0x0FFF) | bit
                    if args.stack == "data":
                        # The user stack on a data page, whatever its kind:
                        # pushes and pops fault, and the frame still goes to
                        # a sound supervisor stack.
                        st.usp = st.a[rng.randrange(7)] & ~1
                    st.a[7] = st.usp
                else:
                    st.sr = (st.sr & 0x3FFF) | 0x2000 | bit
                    st.a[7] = st.msp if st.sr & 0x1000 else st.isp
                code = ([0xC140] if traced else []) + member() + [0x4E71] * 6
                if traced:
                    st.pc -= 2
                ram = [(ad, b) for ad, b in st.ram if not st.pc <= ad < st.pc + 2 * len(code)]
                # Operands: random bytes around every address register, or
                # each memory form would read the zeroes of an empty RAM
                # (DIVL (An) divided by zero 120 times of 120 without this).
                for an in st.a[:7]:
                    ram += [((an + k) & 0xFFFFFF, rng.getrandbits(8)) for k in range(-16, 16)]
                for k, w in enumerate(code):
                    ram += [(st.pc + 2 * k, w >> 8), (st.pc + 2 * k + 1, w & 0xFF)]
                st.ram = ram
                steps = 2 if traced else 1
                final, touched, cycles = fz.run_one(oracle, st, steps)
                if cycles < 0:
                    continue
                vectors.append(sst.vector(f"op={name} #{len(vectors)} trace={args.trace}",
                                          st, final, touched, cycles, steps))
        sst.save(args.out, vectors)
        print(f"{len(vectors)} vectors over {len(members)} classes → {args.out}")
        return 0
    forms = COMMON + (ONLY_030 if args.cpu == "030" else ONLY_040)
    mask = {"none": 0, "t0": 0x4000, "t1": 0x8000}[args.trace]
    for name, words in forms:
        for i in range(args.n):
            st = gen_state(rng, family="core", mmu="off")
            st.sr = (st.sr & 0x3FFF) | 0x2000 | mask
            st.a[7] = st.msp if st.sr & 0x1000 else st.isp
            st.a[0] = 0x00090000 + 8 * i
            code = [0xC140] + words + [0x4E71] * 6          # EXG D0,D0; the form; NOPs
            base = st.pc - 2
            ram = [(ad, b) for ad, b in st.ram if not base <= ad < base + 2 * len(code)]
            for k, w in enumerate(code):
                ram += [(base + 2 * k, w >> 8), (base + 2 * k + 1, w & 0xFF)]
            st.pc, st.ram = base, ram
            final, touched, cycles = fz.run_one(oracle, st, 2)
            if cycles < 0:
                continue                                    # the oracle halted
            vectors.append(sst.vector(f"op={name} #{len(vectors)} trace={args.trace}",
                                      st, final, touched, cycles, 2))
    sst.save(args.out, vectors)
    print(f"{len(vectors)} vectors over {len(forms)} forms → {args.out}")
    return 0


def report(args) -> int:
    vectors = json.load(open(f"{args.dir}/v.json"))
    dump = open(f"{args.dir}/dump.txt").read()
    failed = {int(m.group(1)) for m in re.finditer(r"== op=\S+ #(\d+) ", dump)}
    rows: dict[str, list[int]] = collections.OrderedDict()
    for v in vectors:
        name = re.search(r"op=(\S+)", v["name"]).group(1)
        index = int(re.search(r"#(\d+)", v["name"]).group(1))
        before, after = dict(v["initial"]["ram"]), dict(v["final"]["ram"])
        sp = v["final"]["a7"]
        word = lambda a: (after.get(a, before.get(a, 0)) << 8) | after.get(a + 1, before.get(a + 1, 0))
        traced = word(sp + 6) == TRACE_FRAME
        rows.setdefault(name, [0, 0, 0, 0])[(0 if traced else 2) + (index in failed)] += 1
    print(f"{'form':<16} {'traced ok':>9} {'traced FAIL':>11} {'plain ok':>8} {'plain FAIL':>10}")
    for name, r in rows.items():
        if args.all or r[1] or r[3]:
            print(f"{name:<16} {r[0]:>9} {r[1]:>11} {r[2]:>8} {r[3]:>10}")
    always = [n for n, r in rows.items() if r[0] + r[1] and not r[2] + r[3]]
    print("always traced by the oracle:", " ".join(always) or "—")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--a", required=True, help="oracle library (WinUAE)")
    g.add_argument("--cpu", default="030", choices=["030", "040"])
    g.add_argument("--trace", default="none", choices=["none", "t0", "t1"])
    g.add_argument("--set", default="forms", choices=["forms", "classes", "ea"],
                   help="forms: fixed instruction forms behind a lead-in, two steps; "
                        "classes: small encoding classes with random fields, one step; "
                        "ea: the 68020 additions and system instructions per addressing mode")
    g.add_argument("--mmu", default="off", choices=["off", "identity", "tt"],
                   help="classes and ea: the translation the states are drawn under "
                        "(identity: random page kinds, so the members fault)")
    g.add_argument("--user", action="store_true",
                   help="classes and ea: user mode, for the privilege violations")
    g.add_argument("--stack", default="fixed", choices=["fixed", "data"],
                   help="with --user: data puts the user stack on a data page, so that "
                        "pushes and pops fault under --mmu identity")
    g.add_argument("--n", type=int, default=12, help="states per form or class")
    g.add_argument("--seed", type=int, default=77)
    g.add_argument("--out", required=True)
    r = sub.add_parser("report")
    r.add_argument("dir", help="directory holding v.json and the runner's --dump as dump.txt")
    r.add_argument("--all", action="store_true", help="print every form, not only the failing ones")
    args = ap.parse_args()
    return generate(args) if args.command == "generate" else report(args)


if __name__ == "__main__":
    sys.exit(main())
