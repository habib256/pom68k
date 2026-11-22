// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

template <Core C> void
Moira::writeStackFrameAEBE(StackFrame &frame)
{
    // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
    // POM68K: the 68000 does not stack its group-0 frame top-down. The bus
    // order is PC low, SR, PC high, IR, access address low, status word,
    // access address high — the three-word order of writeStackFrame0000,
    // continued. Upstream pushed the seven words strictly downwards.
    // [D] SingleStepTests/680x0 v1, `transactions` of every address-error
    //     vector (e.g. "426a [CLR.w (d16, A2)] 1"); counted by
    //     `sst68000 --transactions`.
    U32_DEC(reg.sp, 14);
    write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 12) & ~1, (u16)frame.pc);
    write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp,  8) & ~1, frame.sr);
    write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 10) & ~1, frame.pc >> 16);
    write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp,  6) & ~1, frame.ird);
    write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp,  4) & ~1, (u16)frame.addr);
    write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp,  0) & ~1, frame.code);
    write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp,  2) & ~1, frame.addr >> 16);
}

template <Core C> void
Moira::writeStackFrame0000(u16 sr, u32 pc, u16 nr)
{
    switch (C) {

        case Core::C68000:

            if constexpr (MOIRA_MIMIC_MUSASHI) {

                push<C, Long>(pc);
                push<C, Word>(sr);

            } else {

                U32_DEC(reg.sp, 6);
                write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 4) & ~1, pc & 0xFFFF);
                write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 0) & ~1, sr);
                write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 2) & ~1, pc >> 16);
            }
            break;

        case Core::C68010:
        case Core::C68020:

            if constexpr (MOIRA_MIMIC_MUSASHI) {

                push<C, Word>(nr << 2);
                push<C, Long>(pc);
                push<C, Word>(sr);

            } else {

                // [B] MC68030UM-1989 §7.2.2: no alignment restriction on
                // data operands; a misaligned one takes extra bus cycles.
                // POM68K (LC II, 2026-07-17): write the frame at the TRUE
                // stack addresses — no `& ~1`. The A0 masking is 68000 bus
                // behaviour; the 020/030 write misaligned frames byte-exact
                // (the mode-5 mmuWrite splits odd accesses), while a 68010,
                // a 68000 bus, takes the address error in write<>,
                // and execRte reads back at the true addresses. With the
                // masking, an interrupt taken while SP is odd (QuickDraw's
                // 3-byte-per-pixel stack temps make odd SPs routine)
                // stacked a frame shifted one byte low → RTE read garbage
                // → spurious FORMAT ERROR → ROM system error at app
                // launch (Lode Runner freeze; repro scratchpad/oddframe).
                U32_DEC(reg.sp, 8);
                write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 6), 4 * nr);
                write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 4), pc & 0xFFFF);
                write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 0), sr);
                write<C, AddrSpace::DATA, Word>(U32_ADD(reg.sp, 2), pc >> 16);
            }
            break;
    }
}

template <Core C> void
Moira::writeStackFrame0001(u16 sr, u32 pc, u16 nr)
{
    assert(C == Core::C68020);

    // 0001 | Vector offset
    push<C, Word>(0x1000 | nr << 2);

    // Program counter
    push<C, Long>(pc);

    // Status register
    push<C, Word>(sr);
}

template <Core C> void
Moira::writeStackFrame0010(u16 sr, u32 pc, u32 ia, u16 nr)
{
    assert(C == Core::C68020);

    // Instruction address
    push<C, Long>(ia);

    // 0010 | Vector offset
    push<C, Word>(0x2000 | nr << 2);

    // Program counter
    push<C, Long>(pc);

    // Status register
    push<C, Word>(sr);
}

template <Core C> void
Moira::writeStackFrame1000(StackFrame &frame, u16 sr, u32 pc, u32 ia, u16 nr, u32 addr)
{
    assert(C == Core::C68010);

    // Internal information
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);

    // Instruction input buffer
    push<C, Word>(queue.irc);

    // Unused, reserved
    reg.sp -= 2;

    // Data input buffer
    push<C, Word>(readBuffer);

    // Unused, reserved
    reg.sp -= 2;

    // Data output buffer
    push<C, Word>(writeBuffer);

    // Unused, reserved
    reg.sp -= 2;

    // Fault address
    push<C, Long>(frame.addr);

    // Special status word
    push<C, Word>(frame.ssw);

    // 1000 | Vector offset
    push<C, Word>(0x8000 | nr << 2);

    // Program counter
    push<C, Long>(pc);

    // Status register
    push<C, Word>(sr);
}

template <Core C> void
Moira::writeStackFrame1001(u16 sr, u32 pc, u32 ia, u16 nr)
{
    // Four opaque continuation words. The software-visible contract is
    // their presence and preservation by RTE; the pending FPU dialog lives
    // in `fpu.busy`, while these words mirror the two prefetched words and
    // the command so diagnostics see deterministic non-zero context.
    push<C, Word>(fpu.busyExt);
    push<C, Word>(fpu.busyOpcode);
    push<C, Word>(queue.irc);
    push<C, Word>(queue.ird);

    push<C, Long>(ia);                         // instruction address
    push<C, Word>(u16(0x9000 | nr << 2));     // format $9 | vector offset
    push<C, Long>(pc);                         // next word to fetch
    push<C, Word>(sr);
}

template <Core C> void
Moira::writeStackFrame1010(u16 sr, u32 pc, u16 nr)
{
    // Internal registers
    push<C, Word>(0);
    push<C, Word>(0);

    // Data output buffer
    push<C, Long>(0);

    // Internal registers
    push<C, Word>(0);
    push<C, Word>(0);

    // Data cycle fault address
    push<C, Long>(0);

    // Instruction pipe stage B
    push<C, Word>(0);

    // Instruction pipe stage C
    push<C, Word>(0);

    // Special status word
    push<C, Word>(0);

    // Internal register
    push<C, Word>(0);

    // 1010 | Vector offset
    push<C, Word>(0xA000 | nr << 2);

    // Program counter
    push<C, Long>(pc);

    // Status register
    push<C, Word>(sr);
}

template <Core C> void
Moira::writeStackFrame1011(u16 sr, u32 pc, u32 ia, u16 nr)
{
    // Internal registers
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);
    push<C, Long>(0);

    // Version#, Internal information
    push<C, Word>(0);

    // Internal registers
    push<C, Long>(0);
    push<C, Word>(0);

    // Data input buffer
    push<C, Long>(0);

    // Internal registers
    push<C, Long>(0);

    // Stage B address
    push<C, Long>(0);

    // Internal registers
    push<C, Long>(0);
    push<C, Long>(0);

    // Data output buffer
    push<C, Long>(0);

    // Internal registers
    push<C, Word>(0);
    push<C, Word>(0);

    // Data cycle fault address
    push<C, Long>(0);

    // Instruction pipe stage B
    push<C, Word>(0);

    // Instruction pipe stage C
    push<C, Word>(0);

    // Special status register
    push<C, Word>(0);

    // Internal register
    push<C, Word>(0);

    // 1011 | Vector offset
    push<C, Word>(0xB000 | nr << 2);

    // Program counter
    push<C, Long>(ia);

    // Status register
    push<C, Word>(sr);
}

// [B] MC68030UM-1989 §8.4: frame formats, not opaque internal encodings.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
//
// POM68K O4 slice 3 — 68030 MMU bus-fault frames (MC68030UM-1989 § 8.4),
// byte-for-byte replicas of WinUAE Exception_build_stack_frame cases
// $A/$B (newcpu_common.c) as produced by the mode-5 68030+MMU core:
// prefetch020[] and the pipeline words are always zero there, the
// internal words carry the access log, the pending-fixup encodings and
// the MOVEM/EA restart state. See POM68K_VENDOR.md § MMU bus layer.

template <Core C> void
Moira::writeStackFrameShortBusFault(u16 sr, u32 pc)
{
    push<C, Long>(0);                           // internal (pipeline status)
    push<C, Long>(mmuWb3Data);                  // data output buffer
    push<C, Long>(mmuOpcodeV & 0xFFFF);         // internal (opcode storage)
    push<C, Long>(mmuFaultAddr);                // data cycle fault address
    push<C, Word>(0);                           // instr. pipe stage B
    push<C, Word>(0);                           // instr. pipe stage C
    push<C, Word>(mmuSsw);                      // special status word
    push<C, Word>(mmuWb2Address);               // internal (= mmuState[1])
    push<C, Word>(u16(0xA000 | (2 << 2)));      // format $A | vector offset
    push<C, Long>(pc);
    push<C, Word>(sr);
}

template <Core C> void
Moira::writeStackFrameLongBusFault(u16 sr, u32 pc, int nr)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // A faulting write parks the pending value in the access log so RTE
    // can retry it (WinUAE: mmu030_ad[idx_done].val = wb3_data)
    if (!(mmuSsw & 0x0040) && mmuIdxDone < 10) mmuAd[mmuIdxDone] = mmuWb3Data;

    const u32 ps = (mmuOpcodeV == 0xFFFFFFFF) ? 0x80000000 : 0;
    const int done = mmuIdxDone < 9 ? mmuIdxDone : 8;

    int i;
    for (i = 0; i < done + 1; i++) push<C, Long>(mmuAd[i]);     // access log
    for (; i < 9; i++) {
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // POM68K O5: during an FMOVEM (state[1] FMOVEM flag) the padding
        // slots MAX-2/MAX-1 carry the first two longs of the register in
        // flight (WinUAE Exception_build_stack_frame case 0xB,
        // newcpu_common.c: mmu030_fmovem_store)
        u32 v = 0;
        if (mmuState[1] & 0x2000) {
            if (i == 7) v = mmuFmovemStore[0];
            if (i == 8) v = mmuFmovemStore[1];
        }
        push<C, Long>(v);
    }
    push<C, Word>(u16((mmuIdx & 0xf) | ((mmuIdxDone & 0xf) << 4)
                      | (mmuWb2Status << 8)));  // version / internal info
    push<C, Word>(u16(mmuState[2] | (mmuWb3Status << 8)));
    push<C, Word>(mmuWb2Address);
    push<C, Word>(mmuState[0]);                 // MOVEM counter
    push<C, Long>(mmuFaultAddr);                // data input buffer
    push<C, Long>(ps);
    push<C, Long>(mmuStageB);                   // stage B address
    push<C, Long>(mmuDispStore[1]);
    push<C, Long>(mmuDispStore[0]);
    push<C, Long>(mmuWb3Data);                  // data output buffer
    push<C, Long>(mmuOpcodeV & 0xFFFF);         // internal (opcode storage)
    push<C, Long>(mmuFaultAddr);                // data cycle fault address
    push<C, Word>(0);                           // instr. pipe stage B
    push<C, Word>(0);                           // instr. pipe stage C
    push<C, Word>(mmuSsw);
    push<C, Word>(mmuWb2Address);
    push<C, Word>(u16(0xB000 | (nr << 2)));     // format $B | vector offset
    push<C, Long>(pc);
    push<C, Word>(sr);
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// POM68K O4 slice 4 — 68030 address error on an odd instruction-flow
// target (WinUAE Exception_mmu030 nr == 3, reached from the exception3_*
// hooks in the mmu030 gencpu handlers): vector 3, format $B frame with
// SSW = RW|SIZE_W|FC 6 (constant — S is already set when WinUAE evaluates
// it), fault address = the odd target, restart state and data buffers
// zeroed. The caller has already applied every architectural side effect
// that precedes the fault (pops, (An) updates, CCR/SR changes) and passes
// the stacked PC required by the faulting instruction's convention.
template <Core C> void
Moira::execAddressError030(u32 target, u32 stackedPc)
{
    mmuLogging = false;

    willExecute(M68kException::ADDRESS_ERROR, 3);

    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    SYNC(8);

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE zeroes the restart state; the wb*/stage-B capture is left
    // to the deterministic reset values (oracle_set_state zeroes them)
    mmuState[0] = mmuState[1] = 0;
    mmuDataBuffer = 0;
    mmuFaultAddr = target;
    mmuStageB = 0;
    mmuWb2Address = 0;
    mmuWb2Status = mmuWb3Status = 0;
    mmuWb3Data = 0;
    mmuSsw = 0x0066;                            // RW | SIZE_W | FC = 6

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // The vector is fetched before the frame is stacked (WinUAE order)
    u32 vectorAddr = (reg.vbr & ~u32(0x1)) + 4 * 3;
    u32 newpc = read<C, AddrSpace::DATA, Long>(vectorAddr);

    writeStackFrameLongBusFault<C>(status, stackedPc, 3);

    if (newpc & 1) { halt(); return; }          // double fault

    reg.pc = reg.pc0 = newpc;

    if (debugger.catchpointMatches(3)) didReachCatchpoint(u8(3));
    didJumpToVector(3, reg.pc);

    didExecute(M68kException::ADDRESS_ERROR, 3);
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// POM68K Q2 — 68040 address error on an odd instruction-flow target
// (WinUAE Exception_mmu nr == 3, reached from the exception3_* hooks in
// the mmu040 gencpu handlers): vector 3, format $2 frame, address field
// = the odd target with bit 0 cleared (exception3f: addr &= ~1 on
// 68040+). The caller has already applied the WinUAE per-instruction
// conventions (A7 restores/re-pushes, CCR/SR pops) and passes the
// stacked PC. The vector is fetched before the frame (Exception_mmu
// order); an odd handler address double-faults into HALT.
template <Core C> void
Moira::execAddressError040(u32 target, u32 stackedPc)
{
    willExecute(M68kException::ADDRESS_ERROR, 3);

    trace040Pending = false;        // exceptions cancel a pending trace

    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    SYNC(8);

    u32 vectorAddr = reg.vbr + 4 * 3;
    u32 newpc = read<C, AddrSpace::DATA, Long>(vectorAddr);

    writeStackFrame0010<C>(status, stackedPc, target & ~u32(1), 3);

    if (newpc & 1) { halt(); return; }          // double fault

    reg.pc = reg.pc0 = newpc;
    fullPrefetch<C, POLL>();

    if (debugger.catchpointMatches(3)) didReachCatchpoint(u8(3));
    didJumpToVector(3, reg.pc);

    didExecute(M68kException::ADDRESS_ERROR, 3);
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Bus-error exception processing for MMU translation faults, mirroring
// the CATCH block of WinUAE m68k_run_mmu030 + Exception_mmu030: frame $A
// for a fault on the instruction's last write (PC = next instruction,
// CCR kept), frame $B otherwise (PC = faulted instruction, CCR and the
// (An)± fixups restored; a prefetch-phase fault sets bit 31 of the
// pipeline-status long).
template <Core C> void
Moira::execMmuBusError()
{
    mmuLogging = false;

    u32 currpc;

    // POM68K (host): AVAILABILITY seeds plain-020 restart state before the
    // handler can prefetch and advance pc0. Roll back the replay's CCR/EA
    // adjustments before exposing the exception handler; no 030 private
    // fixup encoding is needed in this model's frame.
    const bool mode5 = (cpuModel == Model::M68030);

    if (mmuPlain020()) {

        currpc = mmuLastWritePc;
        if (mmuSsw & 1) {
            setCCR(u8(mmuCcrSave));
            for (int i = 0; i < 2; ++i)
                if (mmuFixupReg[i]) reg.a[mmuFixupReg[i] & 7] = mmuFixupVal[i];
        }

    } else if (!mode5) {

        currpc = reg.pc0;

    } else if (mmuOpcodeV == 0xFFFFFFFF) {      // fault on the opcode fetch

        currpc = reg.pc0;

    } else if (mmuState[1] & 0x0100) {          // fault on the last write

        currpc = mmuLastWritePc;

    } else {

        setCCR(u8(mmuCcrSave));                 // pre-instruction CCR
        for (int i = 0; i < 2; i++)             // cpu_restore_fixup
            if (mmuFixupReg[i]) reg.a[mmuFixupReg[i] & 7] = mmuFixupVal[i];
        currpc = reg.pc0;
    }

    willExecute(M68kException::BUS_ERROR, 2);

    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    SYNC(8);

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // The vector is fetched before the frame is stacked (WinUAE order);
    // both go through address translation
    u32 vectorAddr = (reg.vbr & ~u32(0x1)) + 4 * 2;
    u32 newpc = read<C, AddrSpace::DATA, Long>(vectorAddr);

    if (mode5 && (mmuState[1] & 0x0100)) {      // stale latch on plain core
        writeStackFrameShortBusFault<C>(status, currpc);
    } else {
        // POM68K (host): retain the original PC in each plain-020 frame's private
        // internal words, including nested faults. A handler that redirects
        // the stacked PC must not lend its completed cycle to a new opcode.
        if (mmuPlain020()) mmuDispStore[0] = currpc;
        writeStackFrameLongBusFault<C>(status, currpc);
    }

    if (newpc & 1) { halt(); return; }          // double fault (vector 2)

    reg.pc = reg.pc0 = newpc;

    // POM68K (host) Phase C (Mac LC, 2026-07-24): the mode-5 loops (M68030 via
    // mmuExecuteStart, ≥M68EC040 via mmu040InstrStart) refetch the opcode
    // from reg.pc at every instruction, so the handler jump lands there.
    // The plain-queue models (68020/EC020/EC030) kept executing the STALE
    // queue.ird — the faulted instruction re-ran at the handler PC and
    // re-faulted forever (the LC ROM's AddrMapFlags probe: MOVEM.L from
    // $50FC0000 looped its own bus-error frame until HALT). Refill the
    // queue like execAddressError040 does; a fault during this prefetch
    // propagates to the caller's catch → halt() = a true double fault.
    if (cpuModel != Model::M68030 && cpuModel < Model::M68EC040) {
        fullPrefetch<C, POLL>();
    }

    if (debugger.catchpointMatches(2)) didReachCatchpoint(u8(2));
    didJumpToVector(2, reg.pc);

    didExecute(M68kException::BUS_ERROR, 2);
}

template <Core C> void
Moira::execAddressError(StackFrame frame, int delay)
{
    u16 status = getSR();

    // Inform the delegate
    willExecute(M68kException::ADDRESS_ERROR, 3);

    // Emulate additional delay
    sync(delay);

    // Enter supervisor mode
    setSupervisorMode(true);

    // Disable tracing
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
    // POM68K: SingleStepTests/680x0 — on the 68000, data faults take 8 idle
    // cycles fewer than upstream Moira, instruction-flow faults 6 fewer.
    // The I/N bit (code bit 3, set at the throw sites) is the discriminator:
    // PC-relative OPERAND reads are data faults too (FC=data).
    // POM68K: and the two idle cycles upstream spent AFTER the frame sit
    // BEFORE it on the 68000 — same total, same corpus evidence as the
    // stacking order in writeStackFrameAEBE.
    SYNC(C == Core::C68000 ? ((frame.code & 0x8) ? 4 : 2) : 8);

    // A misaligned stack pointer will cause a double fault
    if (misaligned<C>(reg.sp)) throw DoubleFault();

    // Write stack frame
    if constexpr (C == Core::C68000) {
        writeStackFrameAEBE<C>(frame);
    } else {
        writeStackFrame1000<C>(frame, status, frame.pc, reg.pc0, 3, frame.addr);
        SYNC(2);
    }

    // Jump to exception vector
    jumpToVector<C>(3);

    // Inform the delegate
    didExecute(M68kException::ADDRESS_ERROR, 3);
}

template <Core C> void
Moira::execBusError(StackFrame frame, int delay)
{
    u16 status = getSR();

    // Inform the delegate
    willExecute(M68kException::BUS_ERROR, 2);

    // Emulate additional delay
    sync(delay);

    // Enter supervisor mode
    setSupervisorMode(true);

    // Disable tracing
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    SYNC(8);

    // A misaligned stack pointer will cause a double fault
    if (misaligned<C>(reg.sp)) throw DoubleFault();

    // Write stack frame
    if constexpr (C == Core::C68000) {
        writeStackFrameAEBE<C>(frame);
    } else {
        writeStackFrame1000<C>(frame, status, frame.pc, reg.pc0, 2, frame.addr);
    }
    SYNC(2);

    // Jump to exception vector
    jumpToVector<C>(2);

    // Inform the delegate
    didExecute(M68kException::BUS_ERROR, 2);
}

// The following definitions are not templated. They are compiled into the
// main translation unit only (see MoiraCore_cpp.h).
#ifdef MOIRA_MAIN_TU

void
Moira::execException(M68kException exc, int nr)
{
    switch (cpuModel) {

        case Model::M68000: execException<Core::C68000>(exc, nr); break;
        case Model::M68010: execException<Core::C68010>(exc, nr); break;
        default:            execException<Core::C68020>(exc, nr); break;
    }
}

#endif

template <Core C> void
Moira::execException(M68kException exc, int nr)
{
    // [B] MC68040UM-1993 §3.5 — an A-line, illegal, CHK or unimplemented
    // instruction in the last word of a page, the next page not resident:
    // the ATC access error is reported instead (POM68K, Moira.h § ircPoison).
    // [D] WinUAE @ Hatari e77819f7 takes the instruction's own exception
    // (no lookahead to fault); the manual wins, NOTES.md D39.
    if constexpr (C == Core::C68020) {
        if (cpuModel >= Model::M68EC040 &&
            (exc == M68kException::LINEA || exc == M68kException::LINEF ||
             exc == M68kException::ILLEGAL || exc == M68kException::CHK)) {
            ircPoisonPreempts040();
        }
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: exception stacking is not part of the 68030
    // instruction-restart access log (WinUAE uses non-state accessors)
    mmuLogging = false;

    u16 status = getSR();

    // Determine the exception vector number
    u16 vector = (exc == M68kException::TRAP) ? u16(exc) + u16(nr) : (exc == M68kException::BKPT) ? 4 : u16(exc);

    // Inform the delegate
    willExecute(exc, vector);

    // Remember the exception vector
    exception = vector;

    // Enter supervisor mode and leave trace mode
    setSupervisorMode(true);
    clearTraceFlags();

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K Q2: on the 68040 any exception cancels a pending trace
    // (WinUAE exception_check_trace: SPCFLAG_DOTRACE is unset, and the
    // <68040 internal-exception retention branch does not apply)
    if (cpuModel >= Model::M68EC040 && exc != M68kException::TRACE) {
        trace040Pending = false;
        flags &= ~State::TRACE_EXC;
    }

    switch (exc) {

        case M68kException::BUS_ERROR:

            // Write stack frame
            writeStackFrame1011<C>(status, reg.pc, reg.pc0, 2);

            // Branch to exception handler
            jumpToVector<C>(2);
            break;

        case M68kException::ILLEGAL:
        case M68kException::LINEA:
        case M68kException::LINEF:

            // Clear any pending trace event
            flags &= ~State::TRACE_EXC;

            SYNC(4);

            // Write stack frame
            if constexpr (C == Core::C68010 || C == Core::C68020) {
                writeStackFrame0000<C>(status, reg.pc0, vector);
            } else {
                writeStackFrame0000<C>(status, reg.pc - 2, vector);
            }

            // Branch to exception handler
            jumpToVector<C, AE_SET_CB3>(vector);
            break;

        case M68kException::BKPT:

            // Clear any pending trace event
            flags &= ~State::TRACE_EXC;

            SYNC(2);
            // [D] WinUAE @ Hatari e77819f7: BKPT is op_illg on the 68020 up,
            // no access at all. Upstream stands in for the breakpoint
            // acknowledge cycle with a data read at PC; on the 020/030/040
            // that cycle is a CPU-space acknowledge, never a translated data
            // read, and the stand-in walked the tables of the BKPT's own page
            // (U set where the opcode came through a transparent window) and
            // could fault there. Kept for the 68010, which has no MMU.
            // Fresh-seed vector "040 random/ttvar seed=77012 #456".
            // [F] Bus timing of the 020+ acknowledge cycle. Settled by: a bus
            // capture of BKPT on a real 68030 or 68040.
            // Gate: moira_rules_test (bkptMakesNoDataAccess).
            if constexpr (C != Core::C68020) (void)readM<C, Mode::DN, Word>(reg.pc);
            SYNC(2);

            // Write stack frame
            writeStackFrame0000<C>(status, U32_SUB(reg.pc, 2), vector);

            // Branch to exception handler
            jumpToVector<C, AE_SET_CB3>(vector);
            break;

        case M68kException::DIVIDE_BY_ZERO:
        case M68kException::CHK:
        case M68kException::TRAPV:

            // [D] SingleStepTests/680x0 68000/v1 @ e0d5ece9670205cc84a0101081837deb446f86a3
            // POM68K: divide-by-zero stacks the DIV instruction's own
            // address; CHK/TRAPV stack the next instruction
            // (SingleStepTests/680x0: the DIV instruction address).
            // [B] MC68030UM-1989 Table 8-7:
            // POM68K: format $2 resumes at the next PC but identifies the
            // instruction that caused the exception in its address field.

            // [B] MC68040UM-1993 §8.4.3 confirms the same two addresses.
            // Gate: moira_rules_test (group2InstructionAddress).
            C == Core::C68020 ?
            writeStackFrame0010<C>(status, reg.pc, reg.pc0, vector) :
            writeStackFrame0000<C>(status,
                // [D] Musashi @ 1efd694 (Runner/Musashi of upstream Moira), run by its Runner:
                //     a zero divide stacks the next PC. POM68K: so it does
                //     under MOIRA_MIMIC_MUSASHI, which that Runner uses.
                (exc == M68kException::DIVIDE_BY_ZERO && !MOIRA_MIMIC_MUSASHI) ? reg.pc0 : reg.pc,
                vector);

            // Branch to exception handler
            jumpToVector<C,AE_SET_RW|AE_SET_IF>(vector);
            traceInstructionTrap<C>(status);
            break;

        case M68kException::PRIVILEGE:

            // Clear any pending trace event
            flags &= ~State::TRACE_EXC;

            SYNC(4);

            // Write stack frame
            writeStackFrame0000<C>(status, reg.pc - 2, vector);

            // Branch to exception handler
            jumpToVector<C,AE_SET_CB3>(vector);
            break;

        case M68kException::TRACE:

            // Clear any pending trace event
            flags &= ~State::TRACE_EXC;

            // Recover from stop state
            flags &= ~State::STOPPED;

            SYNC(4);

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // Write stack frame
            // POM68K Q2: the 68040 stacks a format $2 frame whose address
            // field is WinUAE's regs.trace_pc (Exception_build_stack_
            // frame_common nr == 9; 0 unless the staged path latched it)
            // [B] MC68020UM-1992 Table 6-5 — Trace is listed under the
            //     six-word format $2 frame.
            // [B] MC68030UM-1989 Table 8-7 — the same; the instruction
            //     address is that of the instruction that caused the trace.
            // [D] WinUAE @ Hatari e77819f7 — same frame on the 68020/68030,
            //     address field regs.trace_pc.
            // POM68K: until 2026-11-19 the 68020 and 68030 stacked the
            // four-word format $0 frame here.
            // Gate: sst68030 trace cells (tests/cpu_vectors).
            if (C == Core::C68020) {
                writeStackFrame0010<C>(status, reg.pc, tracePc040, vector);
            } else {
                writeStackFrame0000<C>(status, reg.pc, vector);
            }

            // Branch to exception handler
            jumpToVector<C>(vector);
            break;

        case M68kException::FORMAT_ERROR:

            // Clear any pending trace event
            flags &= ~State::TRACE_EXC;

            // Write stack frame
            if (MOIRA_MIMIC_MUSASHI) {
                writeStackFrame0000<C>(status, reg.pc, vector);
            } else {
                writeStackFrame0000<C>(status, reg.pc - 2, vector);
            }

            // Branch to exception handler
            jumpToVector<C, AE_SET_CB3>(vector);
            break;

        case M68kException::TRAP:

            // Write stack frame
            writeStackFrame0000<C>(status, reg.pc, u16(vector));

            // Branch to exception handler
            jumpToVector<C>(vector);
            traceInstructionTrap<C>(status);
            break;

        default:
            break;
    }

    // Inform the delegate
    didExecute(exc, vector);
}

// The following definitions are not templated. They are compiled into the
// main translation unit only (see MoiraCore_cpp.h).
#ifdef MOIRA_MAIN_TU

void
Moira::execInterrupt(u8 level)
{
    switch (cpuModel) {

        case Model::M68000: execInterrupt<Core::C68000>(level); break;
        case Model::M68010: execInterrupt<Core::C68010>(level); break;
        
        default:
            execInterrupt<Core::C68020>(level);
    }
}

#endif

template <Core C> void
Moira::execInterrupt(u8 level)
{
    assert(level < 8);

    // POM68K (host) O4 slice 3: see execException
    mmuLogging = false;

    // Notify delegate
    willInterrupt(level);

    // Remember the current value of the status register
    u16 status = getSR();

    // Recover from stop state and terminate loop mode
    flags &= ~(State::STOPPED | State::LOOPING);

    // [B] MC68030UM-1989 §8.1.9: level 7 is transition sensitive.
    // POM68K: this acknowledge answers the pending level-7 transition
    // (Moira.h, nmiLatch); a later one needs a new transition or a mask
    // lowered below 7.
    if (level == 7) nmiLatch = false;

    // Clear the polled IPL value
    reg.ipl = 0;

    // Temporarily raise the interrupt threshold
    reg.sr.ipl = level;

    // Enter supervisor mode
    setSupervisorMode(true);

    // Disable tracing
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;

    switch (C) {

        case Core::C68000:
        {
            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // NEOST: cycle-exact diagnostic of the exception sequence
            // (setPomExceptionDiag) — the clock at each step, for a step-by-step
            // diff against WinUAE Exception_ce000.
            const bool excDiag = pomExceptionDiag;
            i64 t0 = excDiag ? getClock() : 0;

            SYNC(6);
            reg.sp -= 6;
            write<C, AddrSpace::DATA, Word>(reg.sp + 4, reg.pc & 0xFFFF);
            i64 t1 = excDiag ? getClock() : 0;

            // NEOST: IACK cycles delegated (stock = 4/4). Lets a host port Hatari's
            // `iack_cycle` faithfully: E-clock (before) + the IACK/idle block
            // (after), computed at the real IACK point — see Moira.h
            // iackSyncBefore/After.
            SYNC(iackSyncBefore(level));
            queue.ird = getIrqVector(level);

            SYNC(iackSyncAfter(level));
            i64 t2 = excDiag ? getClock() : 0;
            write<C, AddrSpace::DATA, Word>(reg.sp + 0, status);
            write<C, AddrSpace::DATA, Word>(reg.sp + 2, reg.pc >> 16);
            if (excDiag) {
                i64 t3 = getClock();
                fprintf(stderr, "[EXC] lvl=%d t0=%lld idle+PClo=%lld iack=%lld SR/PChi=%lld\n",
                        level, (long long)t0, (long long)(t1-t0), (long long)(t2-t1),
                        (long long)(t3-t2));
            }
            break;
        }

        case Core::C68010:

            SYNC(12);
            reg.sp -= 8;
            queue.ird = getIrqVector(level);
            write<C, AddrSpace::DATA, Word>(reg.sp + 4, reg.pc & 0xFFFF);
            write<C, AddrSpace::DATA, Word>(reg.sp + 0, status);
            write<C, AddrSpace::DATA, Word>(reg.sp + 2, reg.pc >> 16);
            write<C, AddrSpace::DATA, Word>(reg.sp + 6, 4 * queue.ird);
            break;

        case Core::C68020:

            queue.ird = getIrqVector(level);

            // POM68K (host) (2026-07-17): pass the RAW vector — the frame writers
            // scale by 4 themselves (`4 * nr` / `nr << 2`, as every
            // execException call site relies on). Passing 4*queue.ird
            // double-scaled the stacked vector-offset word ($190 instead
            // of $64 for autovector 25). The format nibble stayed 0, so
            // RTE never noticed — but any handler reading the offset saw
            // a wrong vector. Found with the Lode Runner odd-SP frame bug
            // (scratchpad/oddframe.cpp shows both).
            if (fpuMidInterruptFrame && cpuModel < Model::M68EC040)
                writeStackFrame1001<C>(status, reg.pc, fpu.busyIa, queue.ird);
            else
                writeStackFrame0000<C>(status, reg.pc, queue.ird);

            {
                const bool masterStack = reg.sr.m;
                if (masterStack) {

                    // POM68K (2026-11-20): the frame above went on the
                    // master stack; clear M first, so the throwaway lands
                    // on the interrupt stack, and stack the SR with S set.
                    // Until then both frames went on the master stack and
                    // M stayed set (as upstream Moira had it before eb9529c).
                    // [B] MC68020UM-1992 §6.1.9 — "the processor clears the
                    //     M bit and creates a throwaway exception stack frame
                    //     on top of the interrupt stack [...] exactly the
                    //     same as that placed on the master stack except
                    //     that the S bit is set [...] The resulting status
                    //     register [...] has the S bit set and the M bit
                    //     cleared."
                    // [B] MC68030UM-1989 §8.1.9 — the same text.
                    // [D] WinUAE @ Hatari e77819f7 — newcpu.c
                    //     Exception_normal and Exception_mmu030
                    //     (`regs.m && interrupt`): the same, below the 68060.
                    // [F] The 68040 takes this path on the two manuals
                    //     above; the interrupt section of its own manual
                    //     was not re-read. Settled by: that section.
                    // Gate: moira_rules_test (masterStackInterrupt).
                    setMasterMode(false);
                    writeStackFrame0001<C>(status | 0x2000, reg.pc, queue.ird);
                }
                // POM68K: the interrupt sequence is billed like the other
                // exceptions of this core (TRAP #n: CYCLES_68020(20)) —
                // until 2026-11-19 it cost 0 cycles, so a burst of
                // interrupts was free in guest time.
                // [B] MC68020UM-1992 §8.2.17 — Interrupt (I-Stack)
                //     26 (2/0/4), (M-Stack) 41 (2/0/8): the "cache case"
                //     column, the one this core's TRAP #n 20 (1/0/4) uses.
                //     The two reads, four (eight) writes are included.
                // [B] MC68030UM-1989 §11.6.17 — the 68030's own figures,
                //     Interrupt (I-Stack) 23 (2/0/4), (M-Stack) 33, i-cache
                //     case; its TRAP #n is 18 where this core charges 20.
                // [F] The core keeps one 68020 timing table for the 68020,
                //     68030 and 68040 alike, as it does for TRAP; the
                //     68040 manual gives no exception timing. Settled by: a
                //     bus trace of an autovectored interrupt on a real
                //     68030 and 68040.
                sync(masterStack ? 41 : 26);
            }
    }

    jumpToVector<C, AE_SET_CB3>(queue.ird);
}
