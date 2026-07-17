// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

// POM68K O4 slice 3: SKIP_LAST_RD advances the PC without refilling the
// queue; on the 68030 the consumed word must still enter the access log
// (WinUAE reads every extension word at consumption time)
template <Core C> void
Moira::skipExt()
{
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] mmuLogExtWord(queue.irc);
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

            // POM68K O4 slice 3: arm the pending-fixup slot (WinUAE
            // mmufixup) — its encoding lands in $B fault frames and the
            // register is restored from it on non-last-write faults
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030 && (F & MMU_NOFIXUP) == 0) [[unlikely]]
                    mmuArmFixup<S>(int(n), false);
            }
            break;
        }
        case 4:  // -(An)
        {
            if ((F & IMPL_DEC) == 0) SYNC(2);
            result = readA(n) - ((n == 7 && S == Byte) ? 2 : S);

            // POM68K O4 slice 3: see case 3
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030 && (F & MMU_NOFIXUP) == 0) [[unlikely]]
                    mmuArmFixup<S>(int(n), true);
            }
            break;
        }
        case 5: // (d,An)
        {
            u32 an = readA(n);
            i16  d = (i16)queue.irc;

            result = U32_ADD(an, d);
            if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 6: // (d,An,Xi)
        {
            if constexpr (C == Core::C68020) {

                // POM68K O4 slice 3: 68030 extended EAs go through the
                // disp-store machinery (WinUAE get_disp_ea_020_mmu030)
                if (cpuModel == Model::M68030) [[unlikely]] {
                    result = computeEAdisp030<C, M, S, F>(readA(n));
                    break;
                }

                if (queue.irc & 0x100) {
                    result = computeEAfull<C, M, S, F>(readA(n));
                } else {
                    result = computeEAbrief<C, M, S, F>(readA(n));
                }

            } else {

                i8   d = (i8)queue.irc;
                u32 an = readA(n);
                u32 xi = readR((queue.irc >> 12) & 0b1111);

                result = U32_ADD3(an, d, ((queue.irc & 0x800) ? xi : SEXT<Word>(xi)));

                SYNC(2);
                if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            }
            break;
        }
        case 7: // ABS.W
        {
            result = (i16)queue.irc;
            readBuffer = queue.irc;

            if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 8: // ABS.L
        {
            // POM68K O4 slice 3: the 68030 logs ABS.L as ONE long access
            // (WinUAE next_ilong_mmu030_state), not two word accesses
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030) [[unlikely]] {
                    bool logSave = mmuLogging;
                    mmuLogging = false;
                    result = queue.irc << 16;
                    readExt<C>();
                    result |= queue.irc;
                    readBuffer = queue.irc;
                    if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { reg.pc += 2; }
                    mmuLogging = logSave;
                    mmuLogExtWord(result);
                    break;
                }
            }

            result = queue.irc << 16;
            readExt<C>();
            result |= queue.irc;
            readBuffer = queue.irc;

            if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 9: // (d,PC)
        {
            i16  d = (i16)queue.irc;

            result = U32_ADD(reg.pc, d);
            if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
            break;
        }
        case 10: // (d,PC,Xi)
        {
            if constexpr (C == Core::C68020) {

                // POM68K O4 slice 3: see case 6
                if (cpuModel == Model::M68030) [[unlikely]] {
                    result = computeEAdisp030<C, M, S, F>(reg.pc);
                    break;
                }

                if (queue.irc & 0x100) {
                    result = computeEAfull<C, M, S, F>(reg.pc);
                } else {
                    result = computeEAbrief<C, M, S, F>(reg.pc);
                }

            } else {

                i8   d = (i8)queue.irc;
                u32 xi = readR((queue.irc >> 12) & 0b1111);

                result = U32_ADD3(reg.pc, d, ((queue.irc & 0x800) ? xi : SEXT<Word>(xi)));
                SYNC(2);
                if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }
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

    u16 ext   = queue.irc;
    u16 rn    = xxxx____________ (ext);
    u16 lw    = ____x___________ (ext);
    u16 scale = _____xx_________ (ext);
    u16 disp  = ________xxxxxxxx (ext);

    u32 xn = u32(u64(lw ? readR(rn) : SEXT<Word>(readR(rn))) << scale);
    result = U32_ADD3(an, i8(disp), xn);

    SYNC(2);
    if ((F & SKIP_LAST_RD) == 0) { readExt<C>(); } else { skipExt<C>(); }

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

    u16  ext   = queue.irc;
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

    // Wipe out an if base register is present
    if (bs) an = 0;

    // Check if index is present
    if (!is) xn = (lw ? readR(rn) : SEXT<Word>(readR(rn))) << scale;

    // Compute effective address
    // POM68K O4 slice 4 (WinUAE-arbitrated): memory indirection only
    // happens when the outer-displacement bits (I/IS & 3) are nonzero —
    // the reserved encoding I/IS = 100 computes base + bd + Xn without a
    // memory fetch (WinUAE get_disp_ea_020: `if (dp & 0x3) base =
    // get_long(base)`). Upstream dereferenced on 100 like Musashi did.
    if ((iis & 0b011) == 0) {
        result = an + bd + xn;
    } else if (iis & 0b100) {
        result = readM<C, M, Long>(an + bd) + xn + od;
    } else {
        result = readM<C, M, Long>(an + bd + xn) + od;
    }

    // Add the number of extra cycles consumed in this addressing mode
    cp += penaltyCycles<C, M, S>(ext);

    return result;
}

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

    u32 result;
    if (queue.irc & 0x100) {
        result = computeEAfull<C, M, S, F>(base);
    } else {
        result = computeEAbrief<C, M, S, F>(base);
    }

    mmuIdx = oldIdx;                    // WinUAE: mmu030_idx = oldidx
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

            // Emulate (An)+ register modification
            // POM68K: applied BEFORE the access — real 68000 updates An even
            // when the access raises an address error (SingleStepTests/680x0;
            // see extern/moira/POM68K_VENDOR.md)
            updateAnPI<M, S>(n);

            // Read from effective address
            *result = readM<C, M, S, F>(*ea);
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

            writeBuffer = (S == Long) ? u16(val >> 16) : u16(val & 0xFFFF);

            // Compute effective address
            u32 ea = computeEA<C, M, S>(n);

            // Emulate -(An) register modification
            updateAnPD<M, S>(n);

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
            }

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

            writeBuffer = (S == Long) ? u16(val >> 16) : u16(val & 0xFFFF);

            // POM68K O4 slice 3: see the other writeOp overload
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030) [[unlikely]] {
                    mmuState[1] |= 0x0100;      // LASTWRITE
                    mmuLastWritePc = reg.pc;
                }
            }

            // Write to effective address
            writeM<C, M, S, F>(ea, val);
    }
}

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
    // POM68K: operand reads always use the DATA space — SingleStepTests
    // address-error frames carry FC=101 even for PC-relative operands
    return read<C, AddrSpace::DATA, S, F>(addr);
}

template <Core C, AddrSpace AS, Size S, Flags F> u32
Moira::read(u32 addr)
{
    u32 result;

    // Update function code pins
    setFC(AS == AddrSpace::DATA ? FC::USER_DATA : FC::USER_PROG);

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
    }

    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F>(addr));
    }

    // Check if a watchpoint has been reached
    // NEOST : on masque l'adresse au BUS 24 bits (addrMask, comme l'accès réel plus bas)
    // AVANT le test — sinon un accès I/O en court absolu ($8001.w → EA $FFFF8001) ne
    // matcherait jamais un watchpoint posé sur $FF8001. Cohérent avec le décodage du Bus.
    if ((flags & State::CHECK_WP) && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        didReachWatchpoint(addr & addrMask<C>());
    }

    if constexpr (S == Byte) {

        if (F & POLL) POLL_IPL;
        result = read8(addr & addrMask<C>());
        SYNC(2);
    }

    if constexpr (S == Word) {

        if (F & POLL) POLL_IPL;
        result = read16(addr & addrMask<C>());
        SYNC(2);
    }

    if constexpr (S == Long) {

        result = read16(addr & addrMask<C>()) << 16;
        SYNC(4);
        if (F & POLL) POLL_IPL;
        result |= read16((addr + 2) & addrMask<C>());
        SYNC(2);
    }

    return result;
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

    // POM68K O4 slice 3: 68030 bus-level address translation (see read;
    // slice 4: taken for M68030 even with TC.E off — logging/splitting)
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            mmuWrite<C, S, F>(addr, val);
            return;
        }
    }

    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F|AE_WRITE>(addr));
    }

    // Check if a watchpoint has been reached
    // NEOST : adresse masquée au bus 24 bits avant le test (cf. peekM ci-dessus).
    if ((flags & State::CHECK_WP) && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        didReachWatchpoint(addr & addrMask<C>());
    }

    if constexpr (S == Byte) {

        if (F & POLL) POLL_IPL;
        write8(addr & addrMask<C>(), (u8)val);
        SYNC(2);
    }

    if constexpr (S == Word) {

        if (F & POLL) POLL_IPL;
        write16(addr & addrMask<C>(), (u16)val);
        SYNC(2);
    }

    if constexpr (S == Long) {

        if (F & REVERSE) {

            write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            SYNC(4);
            if (F & POLL) POLL_IPL;
            write16(addr & addrMask<C>(), u16(val >> 16));
            SYNC(2);

        } else {

            write16(addr & addrMask<C>(), u16(val >> 16));
            SYNC(4);
            if (F & POLL) POLL_IPL;
            write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            SYNC(2);
        }
    }
}

template <Core C, Size S> u32
Moira::readI()
{
    u32 result;

    // POM68K O4 slice 3: the 68030 logs a long immediate as ONE access
    // (WinUAE next_ilong_mmu030_state)
    if constexpr (C == Core::C68020 && S == Long) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            bool logSave = mmuLogging;
            mmuLogging = false;
            result = queue.irc << 16;
            readExt<C>();
            result |= queue.irc;
            readExt<C>();
            mmuLogging = logSave;
            mmuLogExtWord(result);
            readBuffer = queue.irc;
            return result;
        }
    }

    switch (S) {

        case Byte:

            result = (u8)queue.irc;
            readExt<C>();
            break;

        case Word:

            result = queue.irc;
            readExt<C>();
            break;

        case Long:

            result = queue.irc << 16;
            readExt<C>();
            result |= queue.irc;
            readExt<C>();
            break;

        default:
            fatalError;
    }
    readBuffer = queue.irc;

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

    // POM68K O4 slice 3: mode-5 68030 has no prefetch queue — the next
    // opcode is fetched (through translation) at the start of the next
    // instruction (mmuExecuteStart); queue refills are suppressed here.
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] return;
    }

    queue.ird = queue.irc;
    queue.irc = (u16)read<C, AddrSpace::PROG, Word, F>(reg.pc + 2);
    readBuffer = queue.irc;
}

template <Core C, Flags F, int delay> void
Moira::fullPrefetch()
{
    // POM68K O4 slice 3: see prefetch — jumps do not fetch the target on
    // the mode-5 68030; the fetch happens at the next instruction start
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            reg.pc0 = reg.pc;
            return;
        }
    }

    assert(!misaligned<C>(reg.pc));

    queue.irc = (u16)read<C, AddrSpace::PROG, Word>(reg.pc);
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

template <Core C> void
Moira::readExt()
{
    // POM68K O4 slice 3: on the 68030 the CONSUMED extension word is the
    // one the oracle reads (and logs) at this point (WinUAE
    // next_iword_mmu030_state); the refill of irc is Moira's one-slot
    // lookahead, translated but unlogged.
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            mmuLogExtWord(queue.irc);
            reg.pc += 2;
            queue.irc = mmuFetchWord(reg.pc);
            return;
        }
    }

    assert(!misaligned<C>(reg.pc));

    reg.pc += 2;
    queue.irc = (u16)read<C, AddrSpace::PROG, Word>(reg.pc);
}

template <Core C, Size S> u32
Moira::readExt()
{
    u32 result = queue.irc;
    readExt<C>();

    if constexpr (S == Long) {

        result = result << 16 | queue.irc;
        readExt<C>();
    }

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
            
        } else if (C == Core::C68000) {

            throw AddressError(makeFrame<F|AE_PROG>(reg.pc, vectorAddr));

        } else {

            queue.irc = readBuffer = u16(reg.pc);
            writeBuffer = u16(4 * nr);
            
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
    // POM68K O4 slice 3: not on the mode-5 68030 (fill_prefetch is a
    // no-op there; the handler opcode is fetched at the next step)
    if constexpr (C == Core::C68020) {
        if (cpuModel == Model::M68030) [[unlikely]] {
            reg.pc0 = reg.pc;
            if (debugger.catchpointMatches(nr)) didReachCatchpoint(u8(nr));
            didJumpToVector(nr, reg.pc);
            return;
        }
    }
    queue.irc = (u16)read<C, AddrSpace::PROG, Word>(reg.pc);
    SYNC(2);
    prefetch<C, POLL>();

    // NEOST : diag cycle-exact (NEOST_EXC_DIAG=1) — horloge en FIN de jumpToVector,
    // à corréler avec [EXC] (execInterrupt) et le timestamp Tracer du handler.
    static const bool neostExcDiag = std::getenv("NEOST_EXC_DIAG") != nullptr;
    if (neostExcDiag)
        fprintf(stderr, "[JTV] nr=%d clk=%lld\n", nr, (long long)getClock());

    // Stop emulation if the exception should be catched
    if (debugger.catchpointMatches(nr)) didReachCatchpoint(u8(nr));

    didJumpToVector(nr, reg.pc);
}

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
