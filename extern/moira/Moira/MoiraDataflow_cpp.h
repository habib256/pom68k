// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

/* Register accessors and the address bus mask.
 *
 * These live here rather than in Moira.cpp so that they stay visible to the
 * per-core translation units, which need to inline them into the instruction
 * handlers. addrMask is evaluated on every memory access.
 */
template <Core C> u32
Moira::addrMask() const
{
    if constexpr (C == Core::C68020) {

        return cpuModel == Model::M68EC020 ? 0x00FFFFFF : 0xFFFFFFFF;

    } else {

        return 0x00FFFFFF;
    }
}

template <Size S> u32
Moira::readD(int n) const
{
    return CLIP<S>(reg.d[n]);
}

template <Size S> u32
Moira::readA(int n) const
{
    return CLIP<S>(reg.a[n]);
}

template <Size S> u32
Moira::readR(int n) const
{
    return CLIP<S>(reg.r[n]);
}

template <Size S> void
Moira::writeD(int n, u32 v)
{
    reg.d[n] = WRITE<S>(reg.d[n], v);
}

template <Size S> void
Moira::writeA(int n, u32 v)
{
    reg.a[n] = WRITE<S>(reg.a[n], v);
}

template <Size S> void
Moira::writeR(int n, u32 v)
{
    reg.r[n] = WRITE<S>(reg.r[n], v);
}

template <Mode M> void
Moira::setFC()
{
    if constexpr (MOIRA_EMULATE_FC) {
        
        // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
        // POM68K: operand accesses always drive FC=data — SingleStepTests
        // address-error frames want FC=101 even for PC-relative reads
        fcl = FC::USER_DATA;
    }
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// POM68K O4 slice 3: SKIP_LAST_RD advances the PC without refilling the
// queue; on the 68030 the consumed word must still enter the access log
// (WinUAE reads every extension word at consumption time)
template <Core C> void
Moira::skipExt()
{
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] mmuLogExtWord(ircWord());
        if (ircPoison) [[unlikely]] ircRefetch();   // POM68K (host): Moira.h § ircPoison
    }
    reg.pc += 2;
}

template <Core C, Mode M, Size S, Flags F> u32
Moira::computeEA(u32 n) {

    assert(n < 8);

    u32 result;

    switch ((int)M) {

        case 0:  // Dn
        case 1:  // An
        {
            result = n;
            break;
        }
        case 2:  // (An)
        {
            result = readA(n);
            break;
        }
        case 3:  // (An)+
        {
            result = readA(n);

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // POM68K O4 slice 3: arm the pending-fixup slot (WinUAE
            // mmufixup) — its encoding lands in $B fault frames and the
            // register is restored from it on non-last-write faults
            if constexpr (C == Core::C68020) {
                // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                // POM68K Q3: the 68040 restores (An)± on non-last-write
                // faults too (WinUAE cpu_restore_fixup)
                if ((mmuPlain020() || cpuModel == Model::M68030 || cpuModel >= Model::M68EC040)
                    && (F & MMU_NOFIXUP) == 0) [[unlikely]]
                    mmuArmFixup<S>(int(n), false);
            }
            break;
        }
        case 4:  // -(An)
        {
            if constexpr ((F & IMPL_DEC) == 0) SYNC(2);
            result = readA(n) - ((n == 7 && S == Byte) ? 2 : S);

            // POM68K (host) O4 slice 3: see case 3
            if constexpr (C == Core::C68020) {
                // POM68K (host) Q3: see case 3
                if ((mmuPlain020() || cpuModel == Model::M68030 || cpuModel >= Model::M68EC040)
                    && (F & MMU_NOFIXUP) == 0) [[unlikely]]
                    mmuArmFixup<S>(int(n), true);
            }
            break;
        }
        case 5: // (d,An)
        {
            u32 an = readA(n);
            i16  d = (i16)ircWord();

            result = U32_ADD(an, d);
            if constexpr ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 6: // (d,An,Xi)
        {
            if constexpr (C == Core::C68020) {

                // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                // POM68K O4 slice 3: 68030 extended EAs go through the
                // disp-store machinery (WinUAE get_disp_ea_020_mmu030)
                if (cpuModel == Model::M68030) [[unlikely]] {
                    result = computeEAdisp030<C, M, S, F>(readA(n));
                    break;
                }

                if (ircWord() & 0x100) {
                    result = computeEAfull<C, M, S, F>(readA(n));
                } else {
                    result = computeEAbrief<C, M, S, F>(readA(n));
                }

            } else {

                i8   d = (i8)ircWord();
                u32 an = readA(n);
                u32 xi = readR((ircWord() >> 12) & 0b1111);

                result = U32_ADD3(an, d, ((ircWord() & 0x800) ? xi : SEXT<Word>(xi)));

                SYNC(2);
                if constexpr ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            }
            break;
        }
        case 7: // ABS.W
        {
            result = (i16)ircWord();
            pomSetRB<C>(queue.irc);  // POM68K (host): 68010-only (Moira.h pomSetRB)

            if constexpr ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 8: // ABS.L
        {
            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // POM68K O4 slice 3: the 68030 logs ABS.L as ONE long access
            // (WinUAE next_ilong_mmu030_state), not two word accesses
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030) [[unlikely]] {
                    bool logSave = mmuLogging;
                    mmuLogging = false;
                    result = ircLong<C>();
                    pomSetRB<C>(queue.irc);  // POM68K (host): 68010-only (Moira.h pomSetRB)
                    if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { reg.pc += 2; }
                    mmuLogging = logSave;
                    mmuLogExtWord(result);
                    break;
                }
            }

            result = ircLong<C>();
            pomSetRB<C>(queue.irc);  // POM68K (host): 68010-only (Moira.h pomSetRB)

            if constexpr ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 9: // (d,PC)
        {
            i16  d = (i16)ircWord();

            result = U32_ADD(reg.pc, d);
            if constexpr ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 10: // (d,PC,Xi)
        {
            if constexpr (C == Core::C68020) {

                // POM68K (host) O4 slice 3: see case 6
                if (cpuModel == Model::M68030) [[unlikely]] {
                    result = computeEAdisp030<C, M, S, F>(reg.pc);
                    break;
                }

                if (ircWord() & 0x100) {
                    result = computeEAfull<C, M, S, F>(reg.pc);
                } else {
                    result = computeEAbrief<C, M, S, F>(reg.pc);
                }

            } else {

                i8   d = (i8)ircWord();
                u32 xi = readR((ircWord() >> 12) & 0b1111);

                result = U32_ADD3(reg.pc, d, ((ircWord() & 0x800) ? xi : SEXT<Word>(xi)));
                SYNC(2);
                if constexpr ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            }
            break;
        }
        case 11: // Im
        {
            result = readI<C, S>();
            break;
        }
        default:
        {
            fatalError;
        }
    }
    return result;
}

template <Core C, Mode M, Size S, Flags F> u32
Moira::computeEAbrief(u32 an)
{
    u32 result;

    //   15 - 12    11   10   09   08   07   06   05   04   03   02   01   00
    // -----------------------------------------------------------------------
    // | REGISTER | LW | SCALE   | 0  | DISPLACEMENT                         |
    // -----------------------------------------------------------------------

    u16 ext   = ircWord();
    u16 rn    = xxxx____________ (ext);
    u16 lw    = ____x___________ (ext);
    u16 scale = _____xx_________ (ext);
    u16 disp  = ________xxxxxxxx (ext);

    u32 xn = u32(u64(lw ? readR(rn) : SEXT<Word>(readR(rn))) << scale);
    result = U32_ADD3(an, i8(disp), xn);

    SYNC(2);
    if constexpr ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }

    return result;
}

template <Core C, Mode M, Size S, Flags F> u32
Moira::computeEAfull(u32 an)
{
    u32 result;

    //   15 - 12    11   10   09   08   07   06   05   04   03   02   01   00
    // -----------------------------------------------------------------------
    // | REGISTER | LW | SCALE   | 1  | BS | IS | BD SIZE  | 0  | IIS        |
    // -----------------------------------------------------------------------

    u16  ext   = ircWord();
    u16  rn    = xxxx____________ (ext);
    u16  lw    = ____x___________ (ext);
    u16  scale = _____xx_________ (ext);
    u16  bs    = ________x_______ (ext);
    u16  is    = _________x______ (ext);
    u16  iis   = _____________xxx (ext);

    u32 xn = 0, bd = 0, od = 0;

    // Read extension words
    readExt<C>();
    auto dw = baseDispWords(ext);
    if (dw == 1) bd = SEXT<Word>(readExt<C, Word>());
    if (dw == 2) bd = readExt<C, Long>();
    auto ow = outerDispWords(ext);
    if (ow == 1) od = SEXT<Word>(readExt<C, Word>());
    if (ow == 2) od = readExt<C, Long>();

    // [B] MC68040UM-1993 §§8.4.6.2/8.4.6.7: a continued indexed
    // MOVEM uses the stacked EA, without repeating memory indirection.
    // POM68K: consume its extensions, but never re-read the pointer.
    if constexpr (C == Core::C68020 && (F & MMU_NOFIXUP) &&
                  (M == Mode::IX || M == Mode::IXPC)) {
        if (cpuModel >= Model::M68EC040 && mmu040MovemArmed) {
            budget.cp += penaltyCycles<C, M, S>(ext);
            return mmu040MovemEa;
        }
    }

    // Wipe out an if base register is present
    if (bs) an = 0;

    // Check if index is present
    if (!is) xn = (lw ? readR(rn) : SEXT<Word>(readR(rn))) << scale;

    // [F] Retired Musashi comparison: local patched revision unavailable.
    // Settled by: a replay with a pinned replacement oracle.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Compute effective address
    // POM68K O4 slice 4 (WinUAE-arbitrated): memory indirection only
    // happens when the outer-displacement bits (I/IS & 3) are nonzero —
    // the reserved encoding I/IS = 100 computes base + bd + Xn without a
    // memory fetch (WinUAE get_disp_ea_020: `if (dp & 0x3) base =
    // get_long(base)`). Upstream dereferenced on 100 like Musashi did.
    // [D] Musashi @ 1efd694 (Runner/Musashi of upstream Moira), run by its Runner:
    //     the reserved 100 dereferences. POM68K: so it does under
    //     MOIRA_MIMIC_MUSASHI, the configuration that Runner uses.
    if ((iis & 0b011) == 0 && !(MOIRA_MIMIC_MUSASHI && iis == 0b100)) {
        result = an + bd + xn;
    } else {
        // [D] WinUAE cpuemu_31.c op_0af0 (CAS) @ Hatari e77819f7: the
        // pointer of a memory-indirect EA is an ordinary read, made by
        // x_get_disp_ea_020 before the locked operand read begins. CAS and
        // TAS arm the lock (mmuRmw, mmu040Lrmw) before readOp, so suspend
        // it here: a fault on the pointer is a plain read, not locked and
        // not write-probed. Restored only on success — the fault frame is
        // built after unwinding and must not see the lock.
        // Fresh-seed vector: `040 random/identity seed=75010 #758`.
        // Gate: moira_rules_test (indirectPointerNotLocked040).
        bool rmw030 = false, rmw040 = false;
        if constexpr (C == Core::C68020) {
            rmw030 = mmuRmw; rmw040 = mmu040Lrmw;
            mmuRmw = mmu040Lrmw = false;
        }
        const u32 pointer = (iis & 0b100) ? an + bd : an + bd + xn;
        result = readM<C, M, Long>(pointer) + ((iis & 0b100) ? xn : 0) + od;
        if constexpr (C == Core::C68020) {
            mmuRmw = rmw030; mmu040Lrmw = rmw040;
        }
    }

    // Add the number of extra cycles consumed in this addressing mode
    budget.cp += penaltyCycles<C, M, S>(ext);

    return result;
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// POM68K O4 slice 3: 68030 extended-EA computation with the WinUAE
// get_disp_ea_020_mmu030 restart semantics: the inner extension-word and
// memory-indirect accesses are rewound from the access log; the result is
// parked in mmuDispStore with the DISP0/1 flag and the consumed word
// count (state[2] nibble) — exactly what a $B fault frame stacks.
template <Core C, Mode M, Size S, Flags F> u32
Moira::computeEAdisp030(u32 base)
{
    const int idx = (mmuState[1] & 0x0001) ? 1 : 0;

    const int oldIdx = mmuIdx, oldDone = mmuIdxDone;
    const u32 pcBefore = reg.pc;

    // RTE $B continuation (Moira.h, mmuReplayArmed). The entries this EA
    // makes in the access log are rewound below, so the frame's log holds
    // them only if the fault fell inside this very computation.
    // [D] WinUAE cpummu030.c get_disp_ea_020_mmu030 @ Hatari e77819f7: an
    //     EA the frame marks complete (state[1] DISP0/DISP1) is taken from
    //     mmu030_disp_store without an access; one it does not is redone
    //     from the log, and `mmu030_idx_done = oldidx` ends the replay there.
    // POM68K: a complete EA is recomputed with the replay off instead of
    // read back from the frame (its pointer is read again on the bus);
    // an incomplete one replays, then stops the replay. Before 2026-11-20
    // neither was done: after a fault on the pointer of ([An]), the access
    // that followed was served the extension word, or a store was dropped
    // as "already made". Gate: fault_transparency_test.
    const bool replaying = mmuReplayActive;
    const bool complete = replaying && (mmuReplayAd[9] & (1u << idx));
    if (complete) mmuReplayActive = false;

    u32 result;
    if (ircWord() & 0x100) {
        result = computeEAfull<C, M, S, F>(base);
    } else {
        result = computeEAbrief<C, M, S, F>(base);
    }

    if (complete) mmuReplayActive = true;
    else if (replaying && int(mmuReplayDone) > oldDone) mmuReplayDone = u8(oldDone);

    mmuIdx = oldIdx;                    // [D] @ Hatari e77819f7: WinUAE: mmu030_idx = oldidx
    mmuIdxDone = oldDone;

    const u32 pcadd = (reg.pc - pcBefore) >> 1;
    mmuState[1] |= u16(1 << idx);
    mmuState[2] |= u16((pcadd & 15) << (idx * 4));
    mmuDispStore[idx] = result;

    return result;
}

template <Core C, Mode M, Size S, Flags F> void
Moira::readOp(int n, u32 *ea, u32 *result)
{
    switch (M) {

        case Mode::DN: *result = readD<S>(n);   break;
        case Mode::AN: *result = readA<S>(n);   break;
        case Mode::IM: *result = readI<C, S>(); break;

        default:

            // Compute effective address
            *ea = computeEA<C, M, S, F>(n);

            // Emulate -(An) register modification
            updateAnPD<M, S>(n);

            // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // Emulate (An)+ register modification
            // POM68K: applied BEFORE the access — real 68000 updates An even
            // when the access raises an address error (SingleStepTests/680x0;
            // see extern/moira/POM68K_VENDOR.md).
            // Q3: the 68040 increments AFTER the access (gencpu mmu040
            // order) — a faulting read leaves An untouched, no fixups.
            if (C != Core::C68020 || cpuModel < Model::M68EC040)
                updateAnPI<M, S>(n);

            // Read from effective address
            *result = readM<C, M, S, F>(*ea);
            if (C == Core::C68020 && cpuModel >= Model::M68EC040)
                updateAnPI<M, S>(n);
    }
}

template <Core C, Mode M, Size S, Flags F> void
Moira::writeOp(int n, u32 val)
{
    switch (M) {

        case Mode::DN: writeD<S>(n, val); break;
        case Mode::AN: writeA<S>(n, val); break;
        case Mode::IM: fatalError;

        default:

            pomSetWB<C>((S == Long) ? u16(val >> 16) : u16(val & 0xFFFF));  // POM68K (host): 68010-only

            // Compute effective address
            u32 ea = computeEA<C, M, S>(n);

            // Emulate -(An) register modification
            updateAnPD<M, S>(n);

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // POM68K O4 slice 3: the 68030 updates (An)+ BEFORE its last
            // write and marks the write as restartable (gencpu
            // gen_set_fault_pc: a fault here stacks a format $A frame
            // with the next-instruction PC and keeps the updated CCR)
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030) [[unlikely]] {
                    updateAnPI<M, S>(n);
                    mmuState[1] |= 0x0100;      // LASTWRITE
                    mmuLastWritePc = reg.pc;
                    writeM<C, M, S, F>(ea, val);
                    return;
                }
                // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                // POM68K Q3: 68040 — (An)+ adjusts BEFORE the write, and
                // the destination store is the instruction's LAST write
                // (gencpu: incpci + instruction_pc=next + mmu_restart=false
                // before the put; oracle-probed 2026-07-18)
                if (cpuModel >= Model::M68EC040) [[unlikely]] {
                    updateAnPI<M, S>(n);
                    mmu040LastWrite = true;
                    mmu040LastWritePc = reg.pc;
                    writeM<C, M, S, F>(ea, val);
                    mmu040LastWrite = false;
                    return;
                }
            }

            // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
            // Write to effective address
            // POM68K: WRITE faults leave (An)+ unmodified - only reads
            // pre-update An (SingleStepTests/680x0, MOVE.w x,(An)+ vectors)
            writeM<C, M, S, F>(ea, val);

            // Emulate (An)+ register modification
            updateAnPI<M, S>(n);
    }
}

template <Core C, Mode M, Size S, Flags F> void
Moira::writeOp(int n, u32 ea, u32 val)
{
    switch (M) {

        case Mode::DN: writeD<S>(n, val); break;
        case Mode::AN: writeA<S>(n, val); break;
        case Mode::IM: fatalError;

        default:

            pomSetWB<C>((S == Long) ? u16(val >> 16) : u16(val & 0xFFFF));  // POM68K (host): 68010-only

            // POM68K (host) O4 slice 3: see the other writeOp overload
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030) [[unlikely]] {
                    mmuState[1] |= 0x0100;      // LASTWRITE
                    mmuLastWritePc = reg.pc;
                }
                // POM68K (host) Q3: 68040 last-write marker (see overload 1)
                if (cpuModel >= Model::M68EC040) [[unlikely]] {
                    mmu040LastWrite = true;
                    mmu040LastWritePc = reg.pc;
                    writeM<C, M, S, F>(ea, val);
                    mmu040LastWrite = false;
                    break;
                }
            }

            // Write to effective address
            writeM<C, M, S, F>(ea, val);
    }
}

// The following definitions are not templated. They are compiled into the
// main translation unit only (see MoiraCore_cpp.h).
#ifdef MOIRA_MAIN_TU

void
Moira::updateAn(Mode M, Size S, int n)
{
    if ((int)M == 3) U32_INC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
    if ((int)M == 4) U32_DEC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

void
Moira::updateAnPI(Mode M, Size S, int n)
{
    if ((int)M == 3) U32_INC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

void
Moira::updateAnPD(Mode M, Size S, int n)
{
    if ((int)M == 4) U32_DEC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

void
Moira::undoAn(Mode M, Size S, int n)
{
    if ((int)M == 3) U32_DEC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
    if ((int)M == 4) U32_INC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

void
Moira::undoAnPI(Mode M, Size S, int n)
{
    if ((int)M == 3) U32_DEC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

void
Moira::undoAnPD(Mode M, Size S, int n)
{
    if ((int)M == 4) U32_INC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

#endif

template <Mode M, Size S> void
Moira::updateAn(int n)
{
    updateAnPI<M, S>(n);
    updateAnPD<M, S>(n);
}

template <Mode M, Size S> void
Moira::updateAnPI(int n)
{
    if constexpr ((int)M == 3) U32_INC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

template <Mode M, Size S> void
Moira::updateAnPD(int n)
{
    if constexpr ((int)M == 4) U32_DEC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

template <Mode M, Size S> void
Moira::undoAn(int n)
{
    undoAnPI<M, S>(n);
    undoAnPD<M, S>(n);
}

template <Mode M, Size S> void
Moira::undoAnPI(int n)
{
    if constexpr ((int)M == 3) U32_DEC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

template <Mode M, Size S> void
Moira::undoAnPD(int n)
{
    if constexpr ((int)M == 4) U32_INC(reg.a[n], (n == 7 && S == Byte) ? 2 : S);
}

template <Core C, Mode M, Size S, Flags F> u32
Moira::readM(u32 addr)
{
    // [B] M68000PRM-1992 §2, §2.2.11: data items "accessed with the program
    // counter relative addressing modes ... classify as program references",
    // "a program reference allowed only for reads".
    // [D] fx68k @ 0602ee46 (as NeoST reads it) and MAME agree.
    // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
    // puts FC=101 (data) in the address-error frames of PC-relative operands;
    // a lower grade does not overrule the manual, so those vectors are
    // recorded divergences of sst68000.
    // [B] MC68040UM-1993 §8.4.6.7: on the 68040 the function codes of PC
    // relative operand accesses "are converted to data references", and
    // the TM field of the access-error frame shows them so.
    // [B] MC68030UM-1989 §2: data items accessed with the PC-relative modes
    // "are classified as program references"; §6.1.2: the data cache
    // still stores them (no 030 data cache is modelled here).
    // POM68K: the 68000 to 68030 read in program space; the 68040 is right
    // in data space. On the 020/030 the read stays an OPERAND cycle for the
    // fault machinery (mmuPcOperand: DF frame, RTE data substitution), and
    // the JIT leaves such reads to this path on the 68030 (JitIr.h
    // withhold030ProgramOperands).
    if constexpr (isPrgMode(M)) {
        if constexpr (C != Core::C68020) {
            return read<C, AddrSpace::PROG, S, F>(addr);
        } else if (cpuModel < Model::M68EC040) {
            struct Scope {
                bool &flag;
                explicit Scope(bool &f) : flag(f) { flag = true; }
                ~Scope() { flag = false; }
            } scope(mmuPcOperand);
            return read<C, AddrSpace::PROG, S, F>(addr);
        }
    }
    return read<C, AddrSpace::DATA, S, F>(addr);
}

template <Core C, Flags F> u32
Moira::read2x16(u32 lo, u32 hi)
{
    u32 result = read16(lo) << 16;
    SYNC(4);
    if constexpr (F & POLL) POLL_IPL;
    result |= read16(hi);
    SYNC(2);
    return result;
}

template <Core C, Flags F> void
Moira::write2x16(u32 lo, u32 hi, u32 valHi, u32 valLo)
{
    if constexpr (F & REVERSE) {

        write16(hi, u16(valLo));
        SYNC(4);
        if constexpr (F & POLL) POLL_IPL;
        write16(lo, u16(valHi));
        SYNC(2);

    } else {

        write16(lo, u16(valHi));
        SYNC(4);
        if constexpr (F & POLL) POLL_IPL;
        write16(hi, u16(valLo));
        SYNC(2);
    }
}

template <Core C, Flags F> u32
Moira::read2x8(u32 addr)
{
    u32 result = u32(read8(addr)) << 8;
    SYNC(4);
    if constexpr (F & POLL) POLL_IPL;
    result |= u32(read8(addr + 1));
    SYNC(2);
    return result;
}

template <Core C, Flags F> void
Moira::write2x8(u32 addr, u32 val)
{
    if constexpr (F & REVERSE) {

        write8(addr + 1, u8(val));
        SYNC(4);
        if constexpr (F & POLL) POLL_IPL;
        write8(addr, u8(val >> 8));
        SYNC(2);

    } else {

        write8(addr, u8(val >> 8));
        SYNC(4);
        if constexpr (F & POLL) POLL_IPL;
        write8(addr + 1, u8(val));
        SYNC(2);
    }
}

template <Core C, Flags F> u32
Moira::read4x8(u32 addr)
{
    u32 result = u32(read8(addr)) << 24;
    SYNC(4);
    result |= u32(read8(addr + 1)) << 16;
    SYNC(4);
    result |= u32(read8(addr + 2)) << 8;
    SYNC(4);
    if constexpr (F & POLL) POLL_IPL;
    result |= u32(read8(addr + 3));
    SYNC(2);
    return result;
}

template <Core C, Flags F> void
Moira::write4x8(u32 addr, u32 val)
{
    if constexpr (F & REVERSE) {

        write8(addr + 3, u8(val));
        SYNC(4);
        write8(addr + 2, u8(val >> 8));
        SYNC(4);
        write8(addr + 1, u8(val >> 16));
        SYNC(4);
        if constexpr (F & POLL) POLL_IPL;
        write8(addr, u8(val >> 24));
        SYNC(2);

    } else {

        write8(addr, u8(val >> 24));
        SYNC(4);
        write8(addr + 1, u8(val >> 16));
        SYNC(4);
        write8(addr + 2, u8(val >> 8));
        SYNC(4);
        if constexpr (F & POLL) POLL_IPL;
        write8(addr + 3, u8(val));
        SYNC(2);
    }
}

template <Core C, Flags F> u32
Moira::readLongSplit(u32 addr)
{
    /* The addressed port decides how the transfer is carried out. A 32 bit
     * port delivers the longword in a single bus cycle, a 16 bit port needs
     * two and an 8 bit port four (see dsack).
     */
    switch (portSize(dsack(addr))) {

        case 4:
        {
            u32 result = read32(addr);
            if constexpr (F & POLL) POLL_IPL;
            SYNC(2);
            return result;
        }
        case 2:

            return read2x16<C,F>(addr, addr + 2);

        default:

            return read4x8<C,F>(addr);
    }
}

template <Core C, Flags F> void
Moira::writeLongSplit(u32 addr, u32 val)
{
    // See readLongSplit(): the port width decides how the transfer is split
    switch (portSize(dsack(addr))) {

        case 4:

            write32(addr, val);
            if constexpr (F & POLL) POLL_IPL;
            SYNC(2);
            return;

        case 2:

            write2x16<C,F>(addr, addr + 2, val >> 16, val & 0xFFFF);
            return;

        default:

            write4x8<C,F>(addr, val);
            return;
    }
}

template <Core C, AddrSpace AS, Size S, Flags F> u32
Moira::read(u32 addr)
{
    u32 result;

    // Update function code pins
    setFC(AS == AddrSpace::DATA ? FC::USER_DATA : FC::USER_PROG);

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: 68030 bus-level address translation. Compiled
    // out of the 68000/68010 instantiations; a single predictable branch
    // for the 68020 core (see POM68K_VENDOR.md § MMU bus layer).
    // Slice 4: the funnel is taken for the M68030 model even with TC.E
    // off — WinUAE's *_mmu030_state accessors always log/split accesses
    // (translation is the only conditional part), and the access log is
    // what a vector-3 format $B frame stacks on an odd-PC address error.
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            return mmuRead<C, S, F>(addr);
        }
        // POM68K (host) Q3: 68040 bus translation (TTR + walk, page-boundary
        // splitting, format $7 faults) — identity-fast when TC.E and all
        // TTR.E bits are clear
        if (cpuModel >= Model::M68EC040) [[unlikely]] {
            return mmu040Read<C, S>(addr, AS == AddrSpace::DATA);
        }
    }

    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F>(addr));
    }

    // Check if a watchpoint has been reached
    // NEOST: mask the address to the 24-bit BUS (addrMask, as the real access below)
    // BEFORE the test — otherwise an I/O access in absolute short ($8001.w → EA
    // $FFFF8001) would never match a watchpoint set on $FF8001. Consistent with
    // the bus decoding.
    if ((flags & State::CHECK_WP) && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        pomDidReachWatchpoint(addr & addrMask<C>(), S, false,
                              AS == AddrSpace::PROG);
    }

    // [B] MC68020UM-1992 §6.2.2: a DF-cleared data cycle is complete.
    // POM68K: consume the software result before any physical bus callback.
    // POM68K (ruling D46): a PC-relative operand read is a data cycle in
    // program space (mmuPcOperand), so it is substituted the same way.
    if constexpr (C == Core::C68020) {
        constexpr u32 firstOffset = (S == Long && (F & REVERSE)) ? 2 : 0;
        if ((AS == AddrSpace::DATA || mmuPcOperand) &&
            mmuSubstitutePlain020(addr + firstOffset, false)) {
            if (F & POLL) POLL_IPL;
            return CLIP<S>(mmuRteSubstData);
        }
    }

    // [B] MC68020UM-1992 §6.2.2: the long frame holds the data fault
    //     address at SP+$10 and the data input buffer at SP+$2C.
    // POM68K Phase C (Mac LC/Mac II, 2026-07-24): record the in-flight
    // sub-access on the PLAIN 68020/EC020 path too, so a device raising
    // /BERR (extBusError) stacks a $B frame with the RIGHT fault address.
    // The LC ROM's 32-bit probe installs a temp vector-2 catcher that
    // compares the frame's fault address with the probed one — a stale
    // mmuAccAddr made it forward every probe fault to SysError (DS 1).
    constexpr bool kPlain020 = (C == Core::C68020);

    if constexpr (S == Byte) {

        if constexpr (F & POLL) POLL_IPL;
        if constexpr (kPlain020) {
            mmuAccAddr = addr; mmuAccSsw = 0x10;
            mmuAccFc = readFC(); mmuAccWrite = false;
        }
        result = read8(addr & addrMask<C>());
        SYNC(2);
    }

    if constexpr (S == Word) {

        // An 8 bit port needs a second bus cycle to deliver a word
        // POM68K (host): compiled with the 68020 i-cache only (busSizing)
        if constexpr (busSizing<C>) {

            if (pomSized020() && portSize(dsack(addr & addrMask<C>())) == 1) {
                mmuAccAddr = addr; mmuAccSsw = 0x20;
                mmuAccFc = readFC(); mmuAccWrite = false;
                return read2x8<C,F>(addr & addrMask<C>());
            }
        }

        if constexpr (F & POLL) POLL_IPL;
        if constexpr (kPlain020) {
            mmuAccAddr = addr; mmuAccSsw = 0x20;
            mmuAccFc = readFC(); mmuAccWrite = false;
        }
        result = read16(addr & addrMask<C>());
        SYNC(2);
    }

    if constexpr (S == Long) {

        if constexpr (kPlain020) {
            mmuAccAddr = addr; mmuAccSsw = 0x00;
            mmuAccFc = readFC(); mmuAccWrite = false;
        }

        // An aligned longword takes four bus cycles on an 8 bit port;
        // everything else is the two-word transfer below.
        // POM68K: compiled with the 68020 i-cache only (busSizing). The
        // plain-020 RTE substitution covers the first cycle (above), not
        // the later bytes of a four-cycle transfer.
        // POM68K: upstream also issues one read32 on a 32-bit port. The fork
        // keeps the two-word transfer there: its /BERR record, format-$B
        // fault address and RTE substitution (berr020_test, Mac OS slot
        // probing) live in it, and the default read32 is those two word
        // calls anyway. A 32-bit port thus changes only the instruction
        // fetch (line fill and charge, readInstr). Gate: berr020_test
        // (the whole matrix under DSACK_32).
        // [F] Unknown: the fault address and SSW size a /BERR on a sized
        // cycle stacks. Settled by: a dynamic-bus-sizing /BERR cell.
        if constexpr (busSizing<C>) {

            if (pomSized020() && (addr & 3) == 0 &&
                portSize(dsack(addr & addrMask<C>())) == 1) {
                return readLongSplit<C,F>(addr & addrMask<C>());
            }
        }

        if constexpr (F & REVERSE) {

            // POM68K (host): low word first — the read twin of write<>'s REVERSE.
            if constexpr (kPlain020) mmuAccAddr = addr + 2;
            result = read16((addr + 2) & addrMask<C>());
            SYNC(4);
            if constexpr (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr; mmuAccSsw = 0x20; }
            if constexpr (kPlain020) {
                if ((AS == AddrSpace::DATA || mmuPcOperand) &&
                    mmuSubstitutePlain020(addr, false))
                    result |= u32(u16(mmuRteSubstData)) << 16;
                else result |= read16(addr & addrMask<C>()) << 16;
            } else result |= read16(addr & addrMask<C>()) << 16;
            SYNC(2);

        } else {

            result = read16(addr & addrMask<C>()) << 16;
            SYNC(4);
            if constexpr (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr + 2; mmuAccSsw = 0x20; }
            if constexpr (kPlain020) {
                if ((AS == AddrSpace::DATA || mmuPcOperand) &&
                    mmuSubstitutePlain020(addr + 2, false))
                    result |= u16(mmuRteSubstData);
                else result |= read16((addr + 2) & addrMask<C>());
            } else result |= read16((addr + 2) & addrMask<C>());
            SYNC(2);
        }
    }

    return result;
}

template <Core C, Flags F> u16
Moira::readInstr(u32 addr)
{
    // POM68K (host): the cache serves the 68020/EC020 only. The 68030 and 68040
    // never get here (prefetch, fullPrefetch and readExt return into their
    // own fetch paths first) and keep their own cache models; the EC030,
    // which this fork runs on the plain path, fetches from the bus as before
    // (its cache geometry is not the 68020's). Every other fetch is the
    // plain PROG read, exactly as before the cache arrived.
    if constexpr (C == Core::C68020 && MOIRA_EMULATE_ICACHE) {

        if (cpuModel == Model::M68020 || cpuModel == Model::M68EC020) {

            // Update function code pins
            setFC(FC::USER_PROG);
            SYNC(2);

            // Check if a watchpoint has been reached (POM68K (host): masked to the
            // bus as in read<>, and reported through the same delegate)
            if ((flags & State::CHECK_WP) &&
                debugger.watchpointMatches(addr & addrMask<C>(), Word)) {
                pomDidReachWatchpoint(addr & addrMask<C>(), Word, false, true);
            }

            if constexpr (F & POLL) POLL_IPL;

            // POM68K: a /BERR raised by the fill stacks this word's address
            // (row 21). [F] Unknown: which of a line fill's cycles the
            // silicon reports. Settled by: a fetch /BERR cell on a cached
            // 68020.
            mmuAccAddr = addr; mmuAccSsw = 0x20;
            mmuAccFc = readFC(); mmuAccWrite = false;

            // Route read access through the instruction cache
            bool busAccess;
            u16 result = readInstructionCache(addr & addrMask<C>(), busAccess);

            // [B] MC68020UM-1992 §8.2: the CYCLES_68020 tables are the
            // manual's cache case ("the instruction is in the cache but has
            // no overlap", 0 instruction accesses). A fetch served without a
            // bus cycle (a cache hit, or the latch's second word) therefore
            // adds nothing. A fetch that makes a bus cycle moves the
            // instruction towards the worst case: +2 from a 32-bit port,
            // the manual's assumption (8 x NOP + DBF: cache 22, worst 33;
            // 8 x MOVE.L D0,(A0) + DBF: 38 and 49; DBF alone: 6 and 9).
            // POM68K: upstream added 0 / 4 per bus cycle and -2 per hit,
            // which counted the cache twice: a warm loop came out below the
            // manual's BEST case (8 x NOP + DBF at 4.5 clocks against 19),
            // and its cycle budget had to borrow to keep the clock moving.
            // Gate: icache020_test (manual loops, warm and cold).
            // [F] Unknown: the +6 from a 16- or 8-bit port keeps upstream's
            // 4-clock spacing between port widths, shifted with the anchor.
            // Settled by: a bus capture of a 68020 on a 16-bit port.
            if (busAccess) {
                budget.cp += portSize(dsack(addr & addrMask<C>())) == 4 ? 2 : 6;
            }
            return result;
        }
    }

    return (u16)read<C, AddrSpace::PROG, Word, F>(addr);
}

template <Core C, Mode M, Size S, Flags F> void
Moira::writeM(u32 addr, u32 val)
{
    if constexpr (isPrgMode(M)) {
        write<C, AddrSpace::PROG, S, F>(addr, val);
    } else {
        write<C, AddrSpace::DATA, S, F>(addr, val);
    }
}

template <Core C, AddrSpace AS, Size S, Flags F> void
Moira::write(u32 addr, u32 val)
{
    // Update function code pins
    setFC(AS == AddrSpace::DATA ? FC::USER_DATA : FC::USER_PROG);

    // POM68K (host) O4 slice 3: 68030 bus-level address translation (see read;
    // slice 4: taken for M68030 even with TC.E off — logging/splitting)
    if constexpr (C == Core::C68020) {
        // [D] Musashi @ 1efd694 (Runner/Musashi of upstream Moira), run by its Runner:
        //     a misaligned word or long is written as whole words, and the
        //     words of a predecremented long low word first. POM68K: under
        //     MOIRA_MIMIC_MUSASHI a 68030 or 68040 that translates nothing
        //     (TC.E and every TT/TTR enable clear) writes through the plain
        //     68020 path below, which does that; with translation on, and
        //     in every other configuration, the bus layers split a
        //     misaligned access into byte and word cycles in address order.
        const bool plainWrite = MOIRA_MIMIC_MUSASHI &&
            (cpuModel == Model::M68030
             ? !((reg.tc & 0x80000000) || ((reg.tt0 | reg.tt1) & 0x8000))
             : !((reg.tc040 | reg.itt0 | reg.itt1 | reg.dtt0 | reg.dtt1) & 0x8000));
        if (cpuModel == Model::M68030 && !plainWrite) [[unlikely]] {
            mmuWrite<C, S, F>(addr, val);
            return;
        }
        // POM68K (host) Q3: 68040 bus translation (see read)
        if (cpuModel >= Model::M68EC040 && !plainWrite) [[unlikely]] {
            mmu040Write<C, S>(addr, val, AS == AddrSpace::DATA);
            return;
        }
    }

    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F|AE_WRITE>(addr));
    }

    // Check if a watchpoint has been reached
    // NEOST: address masked to the 24-bit bus before the test (see read<> above).
    if ((flags & State::CHECK_WP) && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        pomDidReachWatchpoint(addr & addrMask<C>(), S, true,
                              AS == AddrSpace::PROG);
    }

    // [B] MC68020UM-1992 §6.2.2: a DF-cleared data cycle is complete.
    // POM68K: consume the software result before any physical bus callback.
    if constexpr (C == Core::C68020 && AS == AddrSpace::DATA) {
        constexpr u32 firstOffset = (S == Long && (F & REVERSE)) ? 2 : 0;
        if (mmuSubstitutePlain020(addr + firstOffset, true)) {
            if (F & POLL) POLL_IPL;
            return;
        }
    }

    // POM68K (host) Phase C: in-flight sub-access recording on the plain
    // 68020/EC020 path — see the read<> counterpart above.
    constexpr bool kPlain020 = (C == Core::C68020);

    if constexpr (kPlain020) mmuDataBuffer = val;

    if constexpr (S == Byte) {

        if constexpr (F & POLL) POLL_IPL;
        if constexpr (kPlain020) {
            mmuAccAddr = addr; mmuAccSsw = 0x10;
            mmuAccFc = readFC(); mmuAccWrite = true;
        }
        write8(addr & addrMask<C>(), (u8)val);
        SYNC(2);
    }

    if constexpr (S == Word) {

        // An 8 bit port needs a second bus cycle to accept a word
        // POM68K (host): compiled with the 68020 i-cache only (busSizing)
        if constexpr (busSizing<C>) {

            if (pomSized020() && portSize(dsack(addr & addrMask<C>())) == 1) {
                mmuAccAddr = addr; mmuAccSsw = 0x20;
                mmuAccFc = readFC(); mmuAccWrite = true;
                write2x8<C,F>(addr & addrMask<C>(), val);
                return;
            }
        }

        if constexpr (F & POLL) POLL_IPL;
        if constexpr (kPlain020) {
            mmuAccAddr = addr; mmuAccSsw = 0x20;
            mmuAccFc = readFC(); mmuAccWrite = true;
        }
        write16(addr & addrMask<C>(), (u16)val);
        SYNC(2);
    }

    if constexpr (S == Long) {

        if constexpr (kPlain020) {
            mmuAccAddr = addr; mmuAccSsw = 0x00;
            mmuAccFc = readFC(); mmuAccWrite = true;
        }

        // See read<>: four cycles on an 8 bit port
        // POM68K (host): compiled with the 68020 i-cache only (busSizing); a 32-bit
        // port keeps the two-word transfer, as in read<>
        if constexpr (busSizing<C>) {

            if (pomSized020() && (addr & 3) == 0 &&
                portSize(dsack(addr & addrMask<C>())) == 1) {
                writeLongSplit<C,F>(addr & addrMask<C>(), val);
                return;
            }
        }

        if constexpr (F & REVERSE) {

            if constexpr (kPlain020) mmuAccAddr = addr + 2;
            write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            SYNC(4);
            if constexpr (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr; mmuAccSsw = 0x20; }
            if constexpr (kPlain020 && AS == AddrSpace::DATA) {
                mmuDataBuffer = u16(val >> 16);
                if (!mmuSubstitutePlain020(addr, true))
                    write16(addr & addrMask<C>(), u16(val >> 16));
            } else write16(addr & addrMask<C>(), u16(val >> 16));
            SYNC(2);

        } else {

            write16(addr & addrMask<C>(), u16(val >> 16));
            SYNC(4);
            if constexpr (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr + 2; mmuAccSsw = 0x20; }
            if constexpr (kPlain020 && AS == AddrSpace::DATA) {
                mmuDataBuffer = u16(val);
                if (!mmuSubstitutePlain020(addr + 2, true))
                    write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            } else write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            SYNC(2);
        }
    }
}

template <Core C, Size S> u32
Moira::readI()
{
    u32 result;

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: the 68030 logs a long immediate as ONE access
    // (WinUAE next_ilong_mmu030_state)
    if constexpr (C == Core::C68020 && S == Long) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            bool logSave = mmuLogging;
            mmuLogging = false;
            result = ircLong<C>();
            readExt<C>();
            mmuLogging = logSave;
            mmuLogExtWord(result);
            pomSetRB<C>(queue.irc);  // POM68K (host): 68010-only (Moira.h pomSetRB)
            return result;
        }
    }

    switch (S) {

        case Byte:

            result = (u8)ircWord();
            readExt<C>();
            break;

        case Word:

            result = ircWord();
            readExt<C>();
            break;

        case Long:

            result = ircLong<C>();
            readExt<C>();
            break;

        default:
            fatalError;
    }
    pomSetRB<C>(queue.irc);  // POM68K (host): 68010-only

    return result;
}

template <Core C, Size S, Flags F> void
Moira::push(u32 val)
{
    reg.sp -= S;
    write<C, AddrSpace::DATA, S, F>(reg.sp, val);
}

template <Core C, Size S, Flags F> u32
Moira::pop()
{
    u32 result = read<C, AddrSpace::DATA, S, F>(reg.sp);
    reg.sp += S;
    return result;
}

template <Core C, Size S> bool
Moira::misaligned(u32 addr)
{
    if constexpr (MOIRA_EMULATE_ADDRESS_ERROR && C != Core::C68020 && S != Byte) {
        return addr & 1;
    } else {
        return false;
    }
}

template <Flags F> StackFrame
Moira::makeFrame(u32 addr, u32 pc, u16 sr, u16 ird)
{
    StackFrame frame;
    u16 read = 0x10;

    // Prepare
    if constexpr (F & AE_WRITE) read = 0;
    if constexpr (F & AE_PROG) setFC(FC::USER_PROG);
    if constexpr (F & AE_DATA) setFC(FC::USER_DATA);

    // Create
    frame.code = (ird & 0xFFE0) | (u16)readFC() | read;
    frame.addr = addr;
    frame.ird = ird;
    frame.sr = sr;
    frame.pc = pc;
    frame.fc = readFC();
    frame.ssw = frame.fc;

    // Adjust
    if constexpr (F & AE_INC_PC) frame.pc += 2;
    if constexpr (F & AE_DEC_PC) frame.pc -= 2;
    // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
    // POM68K: SingleStepTests/680x0 — on the 68000, every data-fault frame
    // stacks a PC two bytes lower than upstream Moira computes, uniformly
    // across addressing modes (see extern/moira/POM68K_VENDOR.md)
    if constexpr (!(F & AE_PROG)) {
        if (cpuModel == Model::M68000) frame.pc -= 2;
    }
    if constexpr (F & AE_INC_A) frame.addr += 2;
    if constexpr (F & AE_DEC_A) frame.addr -= 2;
    if constexpr (F & AE_SET_CB3) frame.code |= (1 << 3);
    if constexpr (F & AE_SET_RW) frame.ssw |= (1 << 8);
    if constexpr (F & AE_SET_DF) frame.ssw |= (1 << 12);
    if constexpr (F & AE_SET_IF) frame.ssw |= (1 << 13);

    return frame;
}

template <Flags F> StackFrame
Moira::makeFrame(u32 addr, u32 pc)
{
    return makeFrame<F>(addr, pc, getSR(), getIRD());
}

template <Flags F> StackFrame
Moira::makeFrame(u32 addr)
{
    return makeFrame<F>(addr, getPC(), getSR(), getIRD());
}

template <Core C, Flags F> void
Moira::prefetch()
{
    /* Whereas pc is a moving target (it moves forward while an instruction is
     * being processed, pc0 stays stable throughout the entire execution of
     * an instruction. It always points to the start address of the currently
     * executed instruction.
     */
    reg.pc0 = reg.pc;

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: mode-5 68030 has no prefetch queue — the next
    // opcode is fetched (through translation) at the start of the next
    // instruction (mmuExecuteStart); queue refills are suppressed here.
    // Q3: same model for the 68040 (mmu040InstrStart) — a tail refill
    // would touch/fault pages WinUAE only reads at the NEXT step.
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030 || cpuModel >= Model::M68EC040) [[unlikely]] return;
    }

    queue.ird = ircWord();
#if MOIRA_ENABLE_JIT
    // POM68K (host) JIT code window, 020 flavor (Moira.h § pomJitFetch020); the
    // cycle-exact 68000/68010 cores take the pomJitFetch000 flavor instead,
    // which reproduces read<>'s SYNCs and bus model. Either way pomJitFetch
    // refuses unless a jit::Engine armed the window.
    if constexpr (C == Core::C68020) {
        if (u16 w; pomJitFetch020<(F & POLL) != 0>(reg.pc + 2, w)) {
            queue.irc = w;
            pomSetRB<C>(w);  // POM68K (host): 68010-only
            return;
        }
    } else {
        if (u16 w; pomJitFetch000<C, (F & POLL) != 0>(reg.pc + 2, w)) {
            queue.irc = w;
            pomSetRB<C>(w);  // POM68K (host): 68010-only
            return;
        }
    }
#endif

    queue.irc = readInstr<C, F>(reg.pc + 2);
    pomSetRB<C>(queue.irc);  // POM68K (host): 68010-only
}

template <Core C, Flags F, int delay> void
Moira::fullPrefetch()
{
    // FLOW marks the reload after a change of flow — a taken branch, a
    // jump or a return — as opposed to a not-taken DBcc's or an SR write's
    // (Moira.h, traceFlow).
    if constexpr (C == Core::C68020 && (F & FLOW)) traceFlow();

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: see prefetch — jumps do not fetch the target on
    // the mode-5 68030; the fetch happens at the next instruction start.
    // Q3: same for the 68040.
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030 || cpuModel >= Model::M68EC040) [[unlikely]] {
            reg.pc0 = reg.pc;
            return;
        }
    }

    assert(!misaligned<C>(reg.pc));

    // POM68K (host): the cache functions live in Moira.cpp; without the cache the
    // per-core TUs must not pay a call to an empty function on every jump.
    if constexpr (MOIRA_EMULATE_ICACHE) flushInstructionLatch();

#if MOIRA_ENABLE_JIT
    if constexpr (C == Core::C68020) {           // POM68K (host) instruction window
        if (u16 w; pomJitFetch020<false>(reg.pc, w)) {
            queue.irc = w;
            if (delay) SYNC(delay);
            prefetch<C, F>();
            return;
        }
    } else {                                     // POM68K (host) cycle-exact window
        if (u16 w; pomJitFetch000<C, false>(reg.pc, w)) {
            queue.irc = w;
            if (delay) SYNC(delay);
            prefetch<C, F>();
            return;
        }
    }
#endif
    queue.irc = readInstr<C>(reg.pc);
    if (delay) SYNC(delay);
    prefetch<C, F>();
}

template <Core C> void
Moira::noPrefetch(int delay)
{
    assert(flags & State::LOOPING);

    reg.pc0 = reg.pc;
    std::swap(queue.irc, queue.ird);
    if (delay) SYNC(delay);
}

// POM68K (host): one long extension word pair (Moira.h § ircLongAt).
template <Core C> u32
Moira::ircLong()
{
    ircLongAt = reg.pc;
    u32 value = ircWord() << 16;
    readExt<C>();
    value |= ircWord();
    ircLongAt = ircNoLong;
    return value;
}

template <Core C> void
Moira::readExt()
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: on the 68030 the CONSUMED extension word is the
    // one the oracle reads (and logs) at this point (WinUAE
    // next_iword_mmu030_state); the refill of irc is Moira's one-slot
    // lookahead, translated but unlogged.
    if constexpr (C == Core::C68020) {
        if (ircPoison) [[unlikely]] ircRefetch();   // POM68K (host): Moira.h § ircPoison
        if (cpuModel == Model::M68030) [[unlikely]] {
            mmuLogExtWord(ircWord());
            reg.pc += 2;
            queue.irc = mmuLookaheadWord(reg.pc);
            return;
        }
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // POM68K Q3: 68040 — the next extension word is fetched through
        // translation at consumption time (WinUAE next_iword_mmu040)
        if (cpuModel >= Model::M68EC040) [[unlikely]] {
            reg.pc += 2;
#if MOIRA_ENABLE_JIT
            // POM68K (host) JIT code window — the second and by far the busiest
            // fetch site: every extension word past the lookahead
            // (immediates, abs.l, 68020 extended EAs) comes through here.
            if (const u8 *p; pomJitFetch(reg.pc, 2, p)) {
                queue.irc = u16(u16(p[0]) << 8 | p[1]);
                mmu040StampFetch(reg.pc);
                return;
            }
            if (pomJitTimingProbe)
                ++pomJitTiming.fetchWords040;
#endif

            queue.irc = mmu040LookaheadWord<C>(reg.pc);
            return;
        }
    }

    assert(!misaligned<C>(reg.pc));

    reg.pc += 2;
#if MOIRA_ENABLE_JIT
    if constexpr (C == Core::C68020) {           // POM68K (host) instruction window
        if (u16 w; pomJitFetch020<false>(reg.pc, w)) {
            queue.irc = w;
            return;
        }
    } else {                                     // POM68K (host) cycle-exact window
        if (u16 w; pomJitFetch000<C, false>(reg.pc, w)) {
            queue.irc = w;
            return;
        }
    }
#endif
    queue.irc = readInstr<C>(reg.pc);
}

template <Core C, Size S> u32
Moira::readExt()
{
    // [D] WinUAE cpummu030.c get_disp_ea_020_mmu030 @ Hatari e77819f7 —
    // POM68K: a LONG base or outer displacement is ONE entry of the 68030
    // access log (next_ilong_mmu030_state), as ABS.L and a long immediate
    // already are. Logged as two words, a fault on the memory-indirect
    // fetch stacked a format-$B frame with one entry too many
    // ("random/identity seed=6107 #1726"). Gate: moira_rules_test
    // (fullEaLongDisplacementLog030).
    if constexpr (C == Core::C68020 && S == Long) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            bool logSave = mmuLogging;
            mmuLogging = false;
            u32 value = ircLong<C>();
            readExt<C>();
            mmuLogging = logSave;
            mmuLogExtWord(value);
            return value;
        }
    }

    u32 result;
    if constexpr (S == Long) {
        result = ircLong<C>();
    } else {
        result = ircWord();
    }
    readExt<C>();

    return result;
}

template <Core C, Flags F> void
Moira::jumpToVector(int nr)
{
    u32 vbr = C == Core::C68000 ? 0 : reg.vbr;
    u32 vectorAddr = (vbr & ~0x1) + 4 * nr;
    u32 oldpc = reg.pc;

    // Update the program counter
    reg.pc = read<C, AddrSpace::DATA, Long>(vectorAddr);

    // Check for address error
    if (misaligned<C>(reg.pc)) {

        if (nr == 3) {
            
            throw DoubleFault();
            
        } else if constexpr (C == Core::C68000) {

            throw AddressError(makeFrame<F|AE_PROG>(reg.pc, vectorAddr));

        } else {

            queue.irc = u16(reg.pc);
            pomSetRB<C>(queue.irc);  // POM68K (host): 68010-only
            pomSetWB<C>(u16(4 * nr));  // POM68K (host): 68010-only
            
            switch (M68kException(nr)) {
                    
                case M68kException::ILLEGAL:
                case M68kException::LINEA:
                case M68kException::LINEF:
                case M68kException::PRIVILEGE:
                    
                    throw AddressError(makeFrame<F|AE_DEC_PC|AE_PROG|AE_SET_RW|AE_SET_IF>(reg.pc, oldpc));
                    
                default:
                    
                    throw AddressError(makeFrame<F|AE_PROG|AE_SET_RW|AE_SET_IF>(reg.pc, oldpc));
            }
            /*
            if (nr == ILLEGAL || nr == LINEA || nr == LINEF || nr == EXC_PRIVILEGE) {
                throw AddressError(makeFrame<F|AE_DEC_PC|AE_PROG|AE_SET_RW|AE_SET_IF>(reg.pc, oldpc));
            } else {
                throw AddressError(makeFrame<F|AE_PROG|AE_SET_RW|AE_SET_IF>(reg.pc, oldpc));
            }
            */
        }
        return;
    }

    // Update the prefetch queue
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: not on the mode-5 68030 (fill_prefetch is a
    // no-op there; the handler opcode is fetched at the next step).
    // Q3: same for the 68040.
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030 || cpuModel >= Model::M68EC040) [[unlikely]] {
            reg.pc0 = reg.pc;
            if (debugger.catchpointMatches(nr)) didReachCatchpoint(u8(nr));
            didJumpToVector(nr, reg.pc);
            return;
        }
    }
    queue.irc = pomProgWord<C>(reg.pc);   // POM68K (host): cached on the 68020
    SYNC(2);
    prefetch<C, POLL>();

    // NEOST: cycle-exact diagnostic (setPomExceptionDiag) — the clock at the END of
    // jumpToVector, to correlate with [EXC] (execInterrupt) and the handler's
    // Tracer timestamp.
    if (pomExceptionDiag) [[unlikely]]
        fprintf(stderr, "[JTV] nr=%d clk=%lld\n", nr, (long long)getClock());

    // Stop emulation if the exception should be catched
    if (debugger.catchpointMatches(nr)) didReachCatchpoint(u8(nr));

    didJumpToVector(nr, reg.pc);
}

// The following definitions are not templated. They are compiled into the
// main translation unit only (see MoiraCore_cpp.h).
#ifdef MOIRA_MAIN_TU

int
Moira::baseDispWords(u16 ext) const
{
    u16 xx = __________xx____ (ext);

    bool base_disp      = (xx >= 2);
    bool base_disp_long = (xx == 3);

    return base_disp ? (base_disp_long ? 2 : 1) : 0;
}

int
Moira::outerDispWords(u16 ext) const
{
    u16 xx = ______________xx (ext);

    bool outer_disp      = (xx >= 2);
    bool outer_disp_long = (xx == 3);

    return outer_disp ? (outer_disp_long ? 2 : 1) : 0;
}

#endif

template <Core C, Mode M, Size S> int
Moira::penaltyCycles(u16 ext) const
{
    constexpr u8 delay[64] = {

        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  5,  7,  7,  0,  5,  7,  7,  0,  5,  7,  7,  0,  5,  7,  7,
        2,  7,  9,  9,  0,  7,  9,  9,  0,  7,  9,  9,  0,  7,  9,  9,
        6, 11, 13, 13,  0, 11, 13, 13,  0, 11, 13, 13,  0, 11, 13, 13
    };

    if constexpr (C == Core::C68020 && (M == Mode::IX || M == Mode::IXPC)) {

        if (ext & 0x100) return delay[ext & 0x3F];
    }

    return 0;
}

// Explicit template instantiations
template void Moira::fullPrefetch<Core::C68000, POLL>();
