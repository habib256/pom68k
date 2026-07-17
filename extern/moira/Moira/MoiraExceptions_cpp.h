// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

template <Core C> void
Moira::writeStackFrameAEBE(StackFrame &frame)
{
    // Push PC
    push<C, Word>((u16)frame.pc);
    push<C, Word>(frame.pc >> 16);

    // Push SR and IRD
    push<C, Word>(frame.sr);
    push<C, Word>(frame.ird);

    // Push address
    push<C, Word>((u16)frame.addr);
    push<C, Word>(frame.addr >> 16);

    // Push memory access type and function code
    push<C, Word>(frame.code);
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

                // POM68K (LC II, 2026-07-17): write the frame at the TRUE
                // stack addresses — no `& ~1`. The A0 masking is 68000 bus
                // behaviour; the 010/020/030 write misaligned frames
                // byte-exact (the mode-5 mmuWrite splits odd accesses),
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

//
// POM68K O4 slice 3 — 68030 MMU bus-fault frames (MC68030UM § 8.1.4),
// byte-for-byte replicas of WinUAE Exception_build_stack_frame cases
// $A/$B (newcpu_common.c) as produced by the mode-5 68030+MMU core:
// prefetch020[] and the pipeline words are always zero there, the
// internal words carry the access log, the pending-fixup encodings and
// the MOVEM/EA restart state. See POM68K_VENDOR.md § MMU bus layer.
//

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
    // A faulting write parks the pending value in the access log so RTE
    // can retry it (WinUAE: mmu030_ad[idx_done].val = wb3_data)
    if (!(mmuSsw & 0x0040) && mmuIdxDone < 10) mmuAd[mmuIdxDone] = mmuWb3Data;

    const u32 ps = (mmuOpcodeV == 0xFFFFFFFF) ? 0x80000000 : 0;
    const int done = mmuIdxDone < 9 ? mmuIdxDone : 8;

    int i;
    for (i = 0; i < done + 1; i++) push<C, Long>(mmuAd[i]);     // access log
    for (; i < 9; i++) {
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

template <Core C> bool
Moira::mmuCheckOddPc(u32 target, u32 stackedPc)
{
    if constexpr (C == Core::C68020) {

        if (cpuModel == Model::M68030 && (target & 1)) {

            execAddressError030<C>(target, stackedPc);
            return true;
        }
    }
    return false;
}

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

    if (mmuOpcodeV == 0xFFFFFFFF) {             // fault on the opcode fetch

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

    // The vector is fetched before the frame is stacked (WinUAE order);
    // both go through address translation
    u32 vectorAddr = (reg.vbr & ~u32(0x1)) + 4 * 2;
    u32 newpc = read<C, AddrSpace::DATA, Long>(vectorAddr);

    if (mmuState[1] & 0x0100) {
        writeStackFrameShortBusFault<C>(status, currpc);
    } else {
        writeStackFrameLongBusFault<C>(status, currpc);
    }

    if (newpc & 1) { halt(); return; }          // double fault (vector 2)

    reg.pc = reg.pc0 = newpc;

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
    // POM68K: SingleStepTests/680x0 — on the 68000, data faults take 8 idle
    // cycles fewer than upstream Moira, instruction-flow faults 6 fewer.
    // The I/N bit (code bit 3, set at the throw sites) is the discriminator:
    // PC-relative OPERAND reads carry FC=program but still get 0 idle.
    SYNC(C == Core::C68000 ? ((frame.code & 0x8) ? 2 : 0) : 8);

    // A misaligned stack pointer will cause a double fault
    if (misaligned<C>(reg.sp)) throw DoubleFault();

    // Write stack frame
    if (C == Core::C68000) {
        writeStackFrameAEBE<C>(frame);
    } else {
        writeStackFrame1000<C>(frame, status, frame.pc, reg.pc0, 3, frame.addr);
    }
    SYNC(2);

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
    if (C == Core::C68000) {
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

void
Moira::execException(M68kException exc, int nr)
{
    switch (cpuModel) {

        case Model::M68000: execException<Core::C68000>(exc, nr); break;
        case Model::M68010: execException<Core::C68010>(exc, nr); break;
        default:            execException<Core::C68020>(exc, nr); break;
    }
}

template <Core C> void
Moira::execException(M68kException exc, int nr)
{
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
            if (C == Core::C68010 || C == Core::C68020) {
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
            (void)readM<C, Mode::DN, Word>(reg.pc);
            SYNC(2);

            // Write stack frame
            writeStackFrame0000<C>(status, U32_SUB(reg.pc, 2), vector);

            // Branch to exception handler
            jumpToVector<C, AE_SET_CB3>(vector);
            break;

        case M68kException::DIVIDE_BY_ZERO:
        case M68kException::CHK:
        case M68kException::TRAPV:

            // Write stack frame
            // POM68K: divide-by-zero stacks the DIV instruction's own
            // address; CHK/TRAPV stack the next instruction
            // (SingleStepTests/680x0, CLK use_current_instruction_pc).
            // O4 slice 4: on the 020/030 the format-$2 "instruction
            // address" field also holds the NEXT instruction (WinUAE
            // Exception_cpu oldpc, probed; Musashi patched to match).
            C == Core::C68020 ?
            writeStackFrame0010<C>(status, reg.pc, reg.pc, vector) :
            writeStackFrame0000<C>(status,
                exc == M68kException::DIVIDE_BY_ZERO ? reg.pc0 : reg.pc,
                vector);

            // Branch to exception handler
            jumpToVector<C,AE_SET_RW|AE_SET_IF>(vector);
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

            // Write stack frame
            writeStackFrame0000<C>(status, reg.pc, vector);

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
            break;

        default:
            break;
    }

    // Inform the delegate
    didExecute(exc, vector);
}

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

template <Core C> void
Moira::execInterrupt(u8 level)
{
    assert(level < 8);

    // POM68K O4 slice 3: see execException
    mmuLogging = false;

    // Notify delegate
    willInterrupt(level);

    // Remember the current value of the status register
    u16 status = getSR();

    // Recover from stop state and terminate loop mode
    flags &= ~(State::STOPPED | State::LOOPING);

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
            // NEOST : diag cycle-exact de la séquence d'exception (NEOST_EXC_DIAG=1) —
            // horloge à chaque étape, pour le diff pas-à-pas avec WinUAE Exception_ce000.
            static const bool excDiag = std::getenv("NEOST_EXC_DIAG") != nullptr;
            i64 t0 = excDiag ? getClock() : 0;

            SYNC(6);
            reg.sp -= 6;
            write<C, AddrSpace::DATA, Word>(reg.sp + 4, reg.pc & 0xFFFF);
            i64 t1 = excDiag ? getClock() : 0;

            // NEOST : cycles d'IACK délégués (stock = 4/4). Permet le port fidèle
            // de Hatari `iack_cycle` : E-clock (avant) + bloc IACK/idle (après),
            // calculés au point d'IACK réel — cf. Moira.h iackSyncBefore/After.
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

            // POM68K (2026-07-17): pass the RAW vector — the frame writers
            // scale by 4 themselves (`4 * nr` / `nr << 2`, as every
            // execException call site relies on). Passing 4*queue.ird
            // double-scaled the stacked vector-offset word ($190 instead
            // of $64 for autovector 25). The format nibble stayed 0, so
            // RTE never noticed — but any handler reading the offset saw
            // a wrong vector. Found with the Lode Runner odd-SP frame bug
            // (scratchpad/oddframe.cpp shows both).
            writeStackFrame0000<C>(status, reg.pc, queue.ird);

            if (reg.sr.m) {

                writeStackFrame0001<C>(status, reg.pc, queue.ird);
            }
    }

    jumpToVector<C, AE_SET_CB3>(queue.ird);
}
