// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

bool
Moira::isValidExtMMU(Instr I, Mode M, u16 op, u32 ext) const
{
    auto preg  = [ext]() { return ext >> 10 & 0b111;   };
    auto a     = [ext]() { return ext >>  8 & 0b1;     };
    auto mode  = [ext]() { return ext >> 10 & 0b111;   };
    auto mask  = [ext]() { return ext >>  5 & 0b1111;  }; // 68851 mask is 4 bit
    auto reg   = [ext]() { return ext >>  5 & 0b111;   };
    auto fc    = [ext]() { return ext       & 0b11111; };

    auto validFC = [&]() {
        return fc() <= 1 || (fc() >= 8); // Binutils checks M68851
    };

    switch (I) {

        case Instr::PFLUSHA:

            return (op & 0xFF) == 0 && mask() == 0 && fc() == 0;

        case Instr::PFLUSH:

            // Check mode
            if (mode() != 0b100 && mode() != 0b110) return false;

            // Check EA mode
            if (mode() == 0b110) {
                if (M != Mode::AI && M != Mode::DI && M != Mode::IX && M != Mode::AW && M != Mode::AL) {
                    return false;
                }
            }
            return validFC();

        case Instr::PLOAD:

            // Check EA mode
            if (M != Mode::AI && M != Mode::DI && M != Mode::IX && M != Mode::AW && M != Mode::AL) {
                return false;
            }

            return validFC();

        case Instr::PMOVE:

            if ((ext & 0x200)) {
                if (M == Mode::DIPC || M == Mode::IXPC || M == Mode::IM) return false;
            }
            if (M == Mode::IP) return false;

            switch (ext >> 13 & 0b111) {

                case 0b000:

                    // Check register field
                    if (preg() != 0b010 && preg() != 0b011) return false;

                    // If memory is written, flushing is mandatory
                    if ((ext & 0x300) == 0x300) return false;
                    return true;

                case 0b010:

                    // If memory is written, flushing is mandatory
                    if ((ext & 0x300) == 0x300) return false;

                    if ((ext & 0x300) == 0) {
                        if (preg() != 0) {
                            if (M == Mode::PI || M == Mode::PD || M == Mode::IM || M == Mode::IP) return false;
                        }
                    }

                    // Check register field (binutils accepts all M68851 registers)
                    if ((ext & 0x100) == 0) {
                        if (preg() != 0) {
                            if (M == Mode::DN || M == Mode::AN) return false;
                        }
                    }
                    return true;

                case 0b011:

                    return true;

                default:
                    return false;
            }
            break;

        case Instr::PTEST:

            // When A is 0, reg must be 0
            if (a() == 0 && reg() != 0) return false;

            // Check FC
            if ((fc() & 0b11000) == 0 && (fc() & 0b110) != 0) return false;

            // Check EA mode
            if (M != Mode::AI && M != Mode::DI && M != Mode::IX && M != Mode::AW && M != Mode::AL) return false;

            return true;

        default:
            fatalError;
    }
}


//
// POM68K O4 — 68030 MMU instruction execution, TWO-ORACLE ARBITRATED
// (2026-07-15). Originally converged on Musashi alone; the first
// arbitration turn re-ran every dispute (oracle/fuzz/disputes/NOTES.md
// D1-D5) against WinUAE (oracle/uae, hatari e77819f7 cpummu030.c +
// gencpu MMUOP030 handlers), which the MC68030 User's Manual backs on
// every conflict. The decode below therefore replicates WinUAE:
//
//   * D1  — PMOVE/PTEST/PLOAD/PFLUSH are privileged (vector 8); S is
//           checked before the extension word is fetched.
//   * D2  — PMOVE MMUSR,Dn does not exist: Dn/An/(An)+/-(An)/#imm/
//           PC-relative EAs raise Line-F (the old "full register
//           replace" quirk is unreachable on a real 68030).
//   * D3  — the second long of an 8-byte indirect descriptor is read
//           from target+4 (Musashi re-read +0).
//   * D4  — an invalid descriptor ORs I into the accumulated MMUSR
//           (WP collected on the way down stays visible).
//   * D5  — PTEST/PLOAD run the table search even with TC.E = 0
//           (both oracles agree).
//   * D6b — every reserved extension-word field is validated: nonzero
//           low byte, rw+fd, bad preg, bad fc (bits 4-3 = 11), PTEST
//           level 0 with A=1, PFLUSH bad mode, PLOAD nonzero unused
//           bits — all Line-F. Formats 101/110/111 are silent no-ops.
//
// The EA is computed BEFORE validation, WinUAE gencpu order: extension
// words are consumed and (An)+/-(An) adjustments survive a Line-F trap
// (probes P10b/P12). Line-F stacks a format $0 frame with the
// instruction address; vector 56 stacks format $0 with the next PC.
// No ATC is modelled: both oracles flush theirs on every state load, so
// single-instruction vectors never observe ATC state.
//

// Raw physical bus accesses used by the table walk. Both oracles read the
// translation tree in physical space; the bus translation layer is a
// later slice, so these stay untranslated.
u32
Moira::mmuRead32(u32 addr) const
{
    return u32(read16(addr)) << 16 | read16(addr + 2);
}

void
Moira::mmuWrite32(u32 addr, u32 val) const
{
    write16(addr, u16(val >> 16));
    write16(addr + 2, u16(val));
}

// FC field of PTEST/PFLUSH/PLOAD extension words, WinUAE
// mmu_op30_helper_get_fc: bits 4-3 select the source — 10 immediate,
// 01 Dn, 00 SFC/DFC on bit 0 alone (so 00010 reads SFC!), 11 undecodable
// (returns false -> Line-F). Arbitrated vs Musashi's fallback-to-0.
bool
Moira::mmuFCFromModes(u16 modes, int &fc) const
{
    switch (modes & 0x18) {

        case 0x10: fc = modes & 7; return true;
        case 0x08: fc = int(reg.d[modes & 7] & 7); return true;
        case 0x00: fc = int((modes & 1) ? reg.dfc : reg.sfc); return true;

        default:   return false;
    }
}

// Transparent translation match (MC68030UM § 9.5.4, TTx format § 9.7.3):
// enabled (E, bit 15), address base/mask on A31-A24, FC base/mask, R/W(M).
// On a match the MMUSR T bit is set (§ 9.7.2.6).
bool
Moira::mmuMatchTT(u32 tt, u32 addr, int fc, int rw, u16 &sr) const
{
    if (!(tt & 0x8000)) return false;

    u32 base = tt & 0xff000000;
    u32 mask = ((tt << 8) & 0xff000000) ^ 0xff000000;
    u32 fcmask = ~tt & 7;
    u32 fcbits = tt >> 4 & 7;
    int rwmask = (tt & 0x100) ? 0 : 1;
    int rwbit  = (tt & 0x200) ? 1 : 0;

    if ((addr & mask) != (base & mask)) return false;
    if ((u32(fc) & fcmask) != (fcbits & fcmask)) return false;
    if ((rw & rwmask) != (rwbit & rwmask)) return false;

    sr |= 0x0040;               // T — transparent
    return true;
}

// MMUSR accumulation while walking (§ 9.7.2.6): M from page descriptors,
// WP from any valid descriptor, S from long descriptors on user accesses.
void
Moira::mmuUpdateSR(int type, u32 entry, int fc, bool isLong, u16 &sr) const
{
    switch (type) {

        case 0:                 // invalid — no flags

            break;

        case 1:                 // page descriptor

            if (entry & 0x10) sr |= 0x0200;                     // M
            [[fallthrough]];

        case 2:                 // valid 4-byte
        case 3:                 // valid 8-byte

            if (entry & 0x04) sr |= 0x0800;                     // WP
            if (isLong && !(fc & 4) && (entry & 0x100)) sr |= 0x2000; // S
            break;

        default:
            break;
    }
}

// History maintenance (§ 9.5.3.5): the walk sets U in every descriptor it
// traverses, and U+M in the page descriptor on a write access that is not
// write-protected. PTEST performs no updates (§ 9.7.6); PLOAD does.
void
Moira::mmuUpdateDescriptor(u32 tptr, int type, u32 entry, int rw) const
{
    if (type == 1 && !rw && !(entry & 0x10) && !(entry & 0x04)) {

        mmuWrite32(tptr, entry | 0x08 | 0x10);                  // U + M
    } else if (type != 0 && !(entry & 0x08)) {

        mmuWrite32(tptr, entry | 0x08);                         // U
    }
}

// Translation-table walk (§ 9.5.3). Short (4-byte) and long (8-byte)
// descriptors, early termination (page descriptor above the last level),
// indirect descriptors on the last populated level, FCL initial lookup,
// early abort on WP (write access) and S-only (user access). Limit fields
// of long descriptors are ignored (Musashi ignores them too; revisit with
// the translation corpora). Returns true when resolved; sr gets the
// accumulated flags | level count.
bool
Moira::mmuWalkTables(u32 addrIn, int type, u32 table, int fc, int limit,
                     int rw, bool ptest, u32 &addrOut, u16 &sr) const
{
    int level = 0;
    const u32 bits = reg.tc & 0xffff;                   // TIA..TID
    const int pagesize = int(reg.tc >> 20) & 0xf;       // PS
    const int is = int(reg.tc >> 16) & 0xf;             // IS
    int bitpos = (reg.tc & 0x01000000) ? 16 : 12;       // FCL (§ 9.5.3.1)
    int pageshift = is;
    bool resolved = false;

    addrIn <<= is;

    do {

        const int indexbits = int(bits >> bitpos) & 0xf;
        // Oracle quirk: indexbits == 0 shifts a u32 by 32, which on x86
        // leaves the value unchanged — replicated with the & 31.
        const u32 tableIndex =
            (bitpos == 16) ? u32(fc) : addrIn >> ((32 - indexbits) & 31);
        u32 entry, entry2;

        bitpos -= 4;
        // POM68K D3 (WinUAE-arbitrated 2026-07-15): a level is the last
        // one when the NEXT index field is zero (WinUAE last_table);
        // Musashi's unmasked (bits >> bitpos) still saw the consumed
        // upper fields, so indirection only ever fired at TID.
        const bool indirect = ((!bitpos || !((bits >> bitpos) & 0xf)) && indexbits);

        switch (type) {

            case 0: // invalid descriptor → MMUSR I (§ 9.7.2.6)

                // POM68K D4 (WinUAE-arbitrated 2026-07-15): OR, don't
                // assign — WP accumulated on the path stays visible
                // (probe: PTESTW through a WP table descriptor into DT=0
                // gives $0C02 on WinUAE; Musashi erased to $0402).
                sr |= 0x0400;
                resolved = true;
                break;

            case 1: // page descriptor (or early termination, § 9.5.3.4)

                if (!ptest) {

                    table &= ~u32(0) << pagesize;
                    addrOut = table + (addrIn >> pageshift);
                }
                // PTEST keeps addrOut at the last descriptor address
                resolved = true;
                break;

            case 2: // valid 4-byte (short) descriptors (§ 9.5.1.2)

                level++;
                addrOut = table + (tableIndex << 2);
                entry = mmuRead32(addrOut);
                type = int(entry) & 3;

                if (indirect && (type == 2 || type == 3)) {

                    level++;
                    addrOut = entry & 0xfffffffc;
                    entry = mmuRead32(addrOut);
                    type = int(entry) & 3;
                }

                table = entry & 0xfffffff0;
                mmuUpdateSR(type, entry, fc, false, sr);
                if (!ptest) mmuUpdateDescriptor(addrOut, type, entry, rw);
                break;

            case 3: // valid 8-byte (long) descriptors (§ 9.5.1.2)

                level++;
                addrOut = table + (tableIndex << 3);
                entry = mmuRead32(addrOut);
                entry2 = mmuRead32(addrOut + 4);
                type = int(entry) & 3;

                if (indirect && (type == 2 || type == 3)) {

                    level++;
                    addrOut = entry2 & 0xfffffffc;
                    entry = mmuRead32(addrOut);
                    // POM68K D3 (WinUAE-arbitrated 2026-07-15): the
                    // second long lives at +4. Musashi re-read +0; the
                    // Musashi oracle has been fixed to match.
                    entry2 = mmuRead32(addrOut + 4);
                    type = int(entry) & 3;
                }

                table = entry2 & 0xfffffff0;
                mmuUpdateSR(type, entry, fc, true, sr);
                if (!ptest) mmuUpdateDescriptor(addrOut, type, entry, rw);
                break;

            default:
                break;
        }

        if (!ptest) {

            // Early aborts: WP on a write access, S-only on a user access
            if (!rw && (sr & 0x0800)) { break; }
            if (!(fc & 4) && (sr & 0x2000)) { break; }
        }

        addrIn <<= indexbits;
        pageshift += indexbits;

    } while (level < limit && !resolved);

    sr = u16((sr & 0xfff0) | level);
    return resolved;
}

// Root pointer selection for PTEST/PLOAD walks: SRP when SRE is set and
// FC is a supervisor code, else CRP (§ 9.7.2.2). Like both oracles, the
// caller walks even when translation is off (TC.E = 0, dispute D5) — a
// zeroed root pointer then reads as DT invalid → MMUSR I.
void
Moira::mmuRootPointer(int fc, u32 &table, int &type) const
{
    if ((reg.tc & 0x02000000) && (fc & 4)) {    // SRE

        table = u32(reg.srp) & 0xfffffff0;
        type = int(reg.srp >> 32) & 3;

    } else {

        table = u32(reg.crp) & 0xfffffff0;
        type = int(reg.crp >> 32) & 3;
    }
}

//
// POM68K O4 slice 3 — 68030 MMU BUS LAYER (2026-07-15)
//
// Bus-level address translation (MC68030UM § 9.5), converged on the
// primary oracle WinUAE cpummu030.c (hatari e77819f7): transparent
// translation first (§ 9.5.4), then the 22-entry ATC (§ 9.5.2), then a
// table search (§ 9.5.3) that fills the ATC and maintains the U/M bits.
// Translation faults raise a bus error (vector 2) with a format $A frame
// (fault on the instruction's last write) or a format $B frame (any
// other data or instruction-stream fault) — § 8.1.4. The internal-state
// words of those frames (access log, pending-fixup encodings, SSW,
// MOVEM counters) replicate WinUAE's byte-for-byte, because the fuzzing
// differ compares raw RAM. See POM68K_VENDOR.md § MMU bus layer.
//

// Instruction-stream word fetch (WinUAE x_prefetch / mmu030_get_iword):
// translated when TC.E is set, never logged, never split.
u16
Moira::mmuFetchWord(u32 addr)
{
    // POM68K O6: serve linear fetches from the pre-switch pipe after a
    // PMOVE to TC/CRP/SRP; any fetch outside the window kills it
    if (mmuPipeCnt > 0) {

        u32 off = addr - mmuPipeAddr;
        if (!(off & 1) && off / 2 < u32(mmuPipeCnt)) return mmuPipe[off / 2];
        mmuPipeCnt = 0;
    }

    const u8 fc = u8((reg.sr.s ? 4 : 0) | 2);

    // POM68K: 68030 i-cache overlay — every instruction-word fetch, LOGICAL
    // address (the 030 caches are logical), before translation. Inline
    // (was a virtual hook — Moira.h § PomIcache); model rationale in Cpu030.h.
    if (pomIcache.armed) {
        pomIcache.fetches++;
        if (reg.cacr & 0x1) {                   // System enables the i-cache
            u32 t    = (addr >> 8) | (reg.sr.s ? 0x80000000u : 0u);
            int line = int((addr >> 4) & 15);
            u8  bit  = u8(1u << ((addr >> 2) & 3));
            if (pomIcache.tag[line] == t && (pomIcache.valid[line] & bit)) {
                pomIcache.hits++;
            } else {                            // direct-mapped: evict on tag change
                if (pomIcache.tag[line] != t) { pomIcache.tag[line] = t; pomIcache.valid[line] = 0; }
                pomIcache.valid[line] |= bit;
                pomIcache.misses++;
                clock += pomIcache.missPenalty; // a miss pays the fetch bus cycles
            }
        }
    }

    // POM68K O6: recorded here too — with TC.E off mmuTranslateAccess is
    // bypassed, but an unmapped fetch must still be extBusError()-able
    mmuAccAddr = addr; mmuAccSsw = 0x0020; mmuAccFc = fc; mmuAccWrite = false;

    if (reg.tc & 0x80000000) {

        addr = mmuTranslateAccess(addr, fc, false, 0x0020);
    }
    return read16(addr & addrMask<Core::C68020>());
}

// POM68K O6: pipe capture — called by the PMOVE handlers with the OLD
// TC/CRP/SRP still in force. Faults during capture (next code page not
// mapped under the old view) just shorten the pipe.
void
Moira::mmuCapturePipe()
{
    mmuPipeCnt = 0;
    mmuPipeAddr = reg.pc;
    for (int i = 0; i < 4; i++) {
        try {
            mmuPipe[i] = mmuFetchWord(reg.pc + u32(2 * i));
        } catch (MmuBusError &) {
            break;
        }
        mmuPipeCnt = i + 1;
    }
}

// Logs the value of a completed instruction-level access
// (ACCESS_EXIT_GET/PUT in WinUAE cpummu030.h)
void
Moira::mmuLogExtWord(u32 value)
{
    if (mmuLogging) {

        mmuIdx++;
        if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = value;
        mmuIdxDone++;
    }
}

// Arms a pending (An)+/-(An) fixup (WinUAE mmufixup[] + mmu030fixupreg
// encoding: reg | size << 3 | predec << 5 | valid << 6). The encoded byte
// lands in the wb2/wb3 status bytes of a $B fault frame; the value is
// what cpu_restore_fixup restores.
template <Size S> void
Moira::mmuArmFixup(int n, bool predec)
{
    int slot = mmuFixupReg[0] ? 1 : 0;
    u8 sz = S == Byte ? 0 : S == Word ? 1 : 2;

    mmuFixupReg[slot] = u8(n | (sz << 3) | (predec ? 0x20 : 0) | 0x40);
    mmuFixupVal[slot] = reg.a[n];
}

// Per-instruction state reset + mode-5-style opcode fetch (WinUAE
// m68k_run_mmu030 loop head). Returns false when the fetch faulted (the
// exception has been processed).
template <Core C> bool
Moira::mmuExecuteStart()
{
    // POM68K O6: the mode-5 loop suppressed the end-of-instruction
    // prefetches that carried POLL_IPL — without this, a dbra-only loop
    // (no data accesses) never samples the IPL lines and interrupts
    // arrive late (the LC II ROM's VIA-T2-driven TimeDBRA calibration
    // at $A00820 depends on prompt level-1 delivery).
    POLL_IPL;

    mmuState[0] = mmuState[1] = mmuState[2] = 0;
    mmuIdx = mmuIdxDone = 0;
    for (auto &v : mmuAd) v = 0;
    mmuFixupReg[0] = mmuFixupReg[1] = 0;
    mmuCcrSave = getCCR();
    mmuLogging = true;
    mmuRmw = false;
    mmuOpcodeV = 0xFFFFFFFF;

    try {

        // Mode-5 WinUAE reads the opcode through translation at the start
        // of every instruction (x_prefetch); extension words follow at
        // consumption time. Moira's queue is therefore refetched here and
        // the queue-refill prefetches at instruction end are suppressed.
        queue.ird = mmuFetchWord(reg.pc);
        queue.irc = mmuFetchWord(reg.pc + 2);

    } catch (MmuBusError &) {

        try { execMmuBusError<C>(); } catch (...) { halt(); }
        return false;
    }

    mmuOpcodeV = queue.ird;
    return true;
}

// Transparent translation match for bus accesses — OK-match only, wrong
// r/w direction falls through to normal translation (WinUAE
// mmu030_match_ttr_access / mmu030_match_lrmw_ttr_access)
bool
Moira::mmuMatchTTAccess(u32 addr, u8 fc, bool write) const
{
    for (u32 tt : { reg.tt0, reg.tt1 }) {

        if (!(tt & 0x8000)) continue;

        u32 fcmask = ~tt & 7;
        if ((tt >> 4 & 7 & fcmask) != (u32(fc) & fcmask)) continue;

        u32 mask = ~(((tt & 0x00ff0000) << 8) | 0x00ffffff);
        if ((tt & 0xff000000 & mask) != (addr & mask)) continue;

        if (mmuRmw && (fc & 1)) {           // locked RMW needs RWM = 1

            if (tt & 0x100) return true;
            continue;
        }
        if (tt & 0x100) return true;        // RWM: both directions
        if (tt & 0x200) { if (!write) return true; }    // read transparent
        else            { if (write)  return true; }    // write transparent
    }
    return false;
}

// Bus-level translation-table search — WinUAE mmu030_table_search with
// level == 0: FCL lookup, short/long descriptors, limit checks, early
// termination, indirection, U on every traversed descriptor and U+M on
// the page descriptor (§ 9.5.3.5, skipped on supervisor violation).
// Returns MMUSR-style status bits; pageAddr/ci describe the page.
u16
Moira::mmuBusWalk(u32 addr, u8 fc, bool write, u32 &pageAddr, bool &ci)
{
    const u32 tc = reg.tc;

    // Table index masks and shifts (WinUAE mmu030_decode_tc: TI fields
    // are consumed up to the first zero field)
    u32 tmask[4] = {}; u8 tshift[4] = {}; int lastTable = 0;
    {
        int shift = 32 - (tc >> 16 & 15);
        for (int i = 0; i < 4; i++) {
            int ti = tc >> (12 - 4 * i) & 15;
            if (!ti) break;
            shift -= ti;
            tshift[i] = u8(shift & 31);
            tmask[i] = ((u32(1) << ti) - 1) << (shift & 31);
            lastTable = i;
        }
    }

    u16 status = 0;
    ci = false;
    pageAddr = 0;

    const bool super = (fc & 4) != 0;
    bool superViolation = false, writeProtected = false;
    bool earlyTermination = false;
    int t = 0;
    int descrNum = 0;
    u32 descrAddr = 0;

    // Root pointer (SRP when SRE and supervisor FC, § 9.7.2.2); reserved
    // bits of the upper long are masked (WinUAE RP_ZERO_BITS)
    u64 rp = ((tc & 0x02000000) && super) ? reg.srp : reg.crp;
    u32 descr0 = u32(rp >> 32) & ~u32(0x0000FFFC);
    u32 descr1 = u32(rp);
    int descrSize = 8;
    int nextSize = 0;
    int type = int(descr0) & 3;

    switch (type) {

        case 0: status |= 0x0400; goto stopSearch;          // invalid
        case 1: earlyTermination = true; goto pageDescriptor;
        case 2: nextSize = 4; break;
        case 3: nextSize = 8; break;
    }

    // Function code lookup (§ 9.5.3.1)
    if (tc & 0x01000000) {

        u32 tableAddr = (descrSize == 4 ? descr0 : descr1) & 0xFFFFFFF0;
        descrNum++;
        descrAddr = tableAddr + u32(fc) * nextSize;
        descr0 = mmuRead32(descrAddr);
        if (nextSize == 8) descr1 = mmuRead32(descrAddr + 4);
        descrSize = nextSize;
        type = int(descr0) & 3;

        switch (type) {

            case 0: status |= 0x0400; goto stopSearch;
            case 1: earlyTermination = true; goto pageDescriptor;
            case 2: nextSize = 4; break;
            case 3: nextSize = 8; break;
        }
    }

    // Upper level tables
    do {

        if (descrNum) {

            if (descrSize == 8 && (descr0 & 0x100) && !super) superViolation = true;
            if (descr0 & 0x04) writeProtected = true;

            if (!(descr0 & 0x08) && !superViolation) {
                descr0 |= 0x08;                             // U (history)
                mmuWrite32(descrAddr, descr0);
            }
            status |= superViolation ? 0x2000 : 0;
            status |= writeProtected ? 0x0800 : 0;
        }

        {
            u32 tableAddr = (descrSize == 4 ? descr0 : descr1) & 0xFFFFFFF0;
            u32 tableIndex = (addr & tmask[t]) >> tshift[t];
            t++;

            // Limit check on long descriptors (§ 9.5.1.5)
            if (descrSize == 8) {
                u32 limit = (descr0 & 0x7FFF0000) >> 16;
                if ((descr0 & 0x80000000) && tableIndex < limit) { status |= 0x4400; goto stopSearch; }
                if (!(descr0 & 0x80000000) && tableIndex > limit) { status |= 0x4400; goto stopSearch; }
            }

            descrNum++;
            descrAddr = tableAddr + tableIndex * nextSize;
        }
        descr0 = mmuRead32(descrAddr);
        if (nextSize == 8) descr1 = mmuRead32(descrAddr + 4);
        descrSize = nextSize;
        type = int(descr0) & 3;

        switch (type) {

            case 0: status |= 0x0400; goto stopSearch;
            case 1:
                if (t <= lastTable) earlyTermination = true;
                goto pageDescriptor;
            case 2: nextSize = 4; break;
            case 3: nextSize = 8; break;
        }

    } while (t <= lastTable);

    // Indirect descriptor on the last populated level (§ 9.5.3.3)
    {
        u32 indirect = (descrSize == 4 ? descr0 : descr1) & 0xFFFFFFFC;
        descrNum++;
        descrAddr = indirect;
        descr0 = mmuRead32(descrAddr);
        if (nextSize == 8) descr1 = mmuRead32(descrAddr + 4);
        descrSize = nextSize;
        type = int(descr0) & 3;

        if (type != 1) { status |= 0x0400; goto stopSearch; }
    }

pageDescriptor:

    if (descrNum) {

        if (descrSize == 8 && (descr0 & 0x100) && !super) superViolation = true;
        if (descr0 & 0x04) writeProtected = true;

        if (!superViolation) {

            bool dirty = false;
            if (!(descr0 & 0x10) && write && !writeProtected) { descr0 |= 0x10; dirty = true; }   // M
            if (!(descr0 & 0x08)) { descr0 |= 0x08; dirty = true; }                               // U
            if (dirty) mmuWrite32(descrAddr, descr0);
        }
        status |= superViolation ? 0x2000 : 0;
        status |= writeProtected ? 0x0800 : 0;
        ci = (descr0 & 0x40) != 0;
        status |= (descr0 & 0x10) ? 0x0200 : 0;
    }

    if (earlyTermination) {

        if (descrNum || !(tc & 0x01000000)) {
            if (descrSize == 8) {
                u32 tableIndex = (addr & tmask[t]) >> tshift[t];
                u32 limit = (descr0 & 0x7FFF0000) >> 16;
                if ((descr0 & 0x80000000) && tableIndex < limit) { status |= 0x4400; goto stopSearch; }
                if (!(descr0 & 0x80000000) && tableIndex > limit) { status |= 0x4400; goto stopSearch; }
            }
        }
        // Unused index bits are added to the page address (§ 9.5.3.4)
        u32 unused = 0;
        for (int i = t; i <= lastTable; i++) unused |= tmask[i];
        pageAddr = addr & unused;
    }

    pageAddr += (descrSize == 4 ? descr0 : descr1) & 0xFFFFFF00;

stopSearch:

    return status;
}

// Pseudo-LRU history maintenance (WinUAE mmu030_atc_handle_history_bit)
void
Moira::mmuAtcTouch(int i)
{
    // POM68K perf: O(1) equivalent of the original two-pass scan.
    // mmuAtcMruCount always equals the number of set history bits
    // (transitions happen only here and in the resets), so "no clear
    // bit left" is a counter test instead of a 22-entry walk.
    auto &e = mmuAtcArr[i];
    if (e.mru) return;
    e.mru = true;
    if (++mmuAtcMruCount >= MMU_ATC_ENTRIES) {
        for (auto &x : mmuAtcArr) x.mru = false;
        e.mru = true;
        mmuAtcMruCount = 1;
    }
}

// ATC lookup (WinUAE mmu030_logical_is_in_atc): exact FC, page-aligned
// address; a valid write hit on an unmodified, unprotected page is
// invalidated so the table search re-runs and sets M.
int
Moira::mmuAtcLookup(u32 addr, u8 fc, bool write)
{
    const u32 imask = ~mmuPageMask();
    const u32 maddr = addr & imask;

    // POM68K perf: probe the line that satisfied the previous lookup for
    // this (fc, direction) first — page-local access streams (nearly all
    // of them) then check one entry instead of scanning 22. The checks,
    // the write-upgrade invalidation and the LRU touch are the same as
    // the scan below, so behaviour is identical; a stale remembered line
    // simply fails the compare and falls through to the full scan.
    {
        auto &e = mmuAtcArr[mmuAtcLast[fc & 7][write]];
        if (e.valid && e.fc == fc && (e.logical & imask) == maddr) {
            if (!write || e.modified || e.writeProtect || e.busError) {
                mmuAtcTouch(mmuAtcLast[fc & 7][write]);
                return mmuAtcLast[fc & 7][write];
            }
            e.valid = false;
        }
    }

    for (int i = 0; i < MMU_ATC_ENTRIES; i++) {

        auto &e = mmuAtcArr[i];
        if (!e.valid || e.fc != fc || (e.logical & imask) != maddr) continue;

        if (!write || e.modified || e.writeProtect || e.busError) {

            mmuAtcTouch(i);
            mmuAtcLast[fc & 7][write] = i8(i);
            return i;
        }
        e.valid = false;
    }
    return -1;
}

// Table search + ATC entry creation (tail of WinUAE mmu030_table_search)
void
Moira::mmuAtcFill(u32 addr, u8 fc, bool write)
{
    u32 page = 0; bool ci = false;
    u16 status = mmuBusWalk(addr, fc, write, page, ci);

    int i;
    for (i = 0; i < MMU_ATC_ENTRIES; i++) if (!mmuAtcArr[i].valid) break;
    if (i == MMU_ATC_ENTRIES) {
        for (i = 0; i < MMU_ATC_ENTRIES; i++) if (!mmuAtcArr[i].mru) break;
    }
    if (i >= MMU_ATC_ENTRIES) i = 0;
    mmuAtcTouch(i);

    const u32 imask = ~mmuPageMask();
    auto &e = mmuAtcArr[i];

    e.logical = addr & imask;
    e.fc = fc;
    e.valid = true;
    e.physical = page & imask;
    e.busError = (status & 0x2400) != 0;        // INVALID | SUPER_VIOLATION
    e.cacheInhibit = ci;
    e.modified = (status & 0x0200) != 0;
    e.writeProtect = (status & 0x0800) != 0;
}

// ATC flushes (§ 9.7.3 PFLUSH variants; WinUAE mmu030_flush_atc_*)
void
Moira::mmuAtcFlushAll()
{
    for (auto &e : mmuAtcArr) e.valid = false;
}

void
Moira::mmuAtcFlushFc(u32 fcBase, u32 fcMask)
{
    for (auto &e : mmuAtcArr)
        if (e.valid && (fcBase & fcMask) == (e.fc & fcMask)) e.valid = false;
}

void
Moira::mmuAtcFlushPage(u32 addr)
{
    addr &= ~mmuPageMask();
    for (auto &e : mmuAtcArr)
        if (e.valid && e.logical == addr) e.valid = false;
}

void
Moira::mmuAtcFlushPageFc(u32 addr, u32 fcBase, u32 fcMask)
{
    addr &= ~mmuPageMask();
    for (auto &e : mmuAtcArr)
        if (e.valid && (fcBase & fcMask) == (e.fc & fcMask) && e.logical == addr)
            e.valid = false;
}

// Logical → physical for one bus (sub-)access; faults on invalid /
// supervisor-only pages and on write-protected writes (WinUAE
// mmu030_put_atc / mmu030_get_atc / *_generic)
u32
Moira::mmuTranslateAccess(u32 addr, u8 fc, bool write, u32 sswFlags)
{
    // POM68K O6: remember the sub-access so a device asserting /BERR from
    // the physical read/write callback (extBusError) faults it exactly
    mmuAccAddr = addr; mmuAccSsw = sswFlags; mmuAccFc = fc; mmuAccWrite = write;

    // POM68K O4 slice 4: the funnel now routes every M68030 access here;
    // with translation disabled the address passes through untouched
    if (!(reg.tc & 0x80000000)) return addr;

    if (fc == 7) return addr;                   // CPU space is never mapped

    if ((reg.tt0 | reg.tt1) & 0x8000) {
        if (mmuMatchTTAccess(addr, fc, write)) return addr;
    }

    // Locked RMW cycles probe the ATC as writes even when reading; the
    // RMW property never applies to instruction-stream fetches (WinUAE
    // sets islrmw030 only around the data access itself)
    const bool rmw = mmuRmw && (fc & 1);
    const bool lookupWrite = write || rmw;

    int line = mmuAtcLookup(addr, fc, lookupWrite);
    if (line < 0) {
        mmuAtcFill(addr, fc, lookupWrite);
        line = mmuAtcLookup(addr, fc, lookupWrite);
    }
    if (line < 0) mmuPageFault(addr, !write, sswFlags, fc);

    const auto &e = mmuAtcArr[line];
    if (e.busError || (lookupWrite && e.writeProtect))
        mmuPageFault(addr, !write, sswFlags, fc);

    return (e.physical & ~mmuPageMask()) | (addr & mmuPageMask());
}

// Fault capture + throw (WinUAE mmu030_page_fault): SSW per § 8.2.1
// (oracle-arbitrated encodings, incl. the double DF bit), pending-fixup
// application, data buffer and MOVEM/EA state snapshots for the frame.
void
Moira::mmuPageFault(u32 addr, bool read, u32 sswFlags, u8 fc)
{
    mmuWb2Status = mmuWb3Status = 0;

    if (fc & 1) {                               // data cycle

        mmuSsw = 0x0300;                        // DF | DF << 1
        if (!(mmuState[1] & 0x0100)) {          // not the last write

            for (int i = 0; i < 2; i++) {

                u8 enc = mmuFixupReg[i];
                // O5: plain FPU fixups (bit 7) keep the status byte 0
                // and get no ± adjustment — WinUAE mmu030fixupreg
                // returns 0 for fpp.c-armed fixups (no 0x300 flags);
                // cpu_restore_fixup still restores the register
                if (enc & 0x80) enc = 0;
                (i == 0 ? mmuWb2Status : mmuWb3Status) = enc;
                if (enc) {                      // mmu030fixupmod: the
                    int r = enc & 7;            // (An)± adjustment survives
                    i32 adj = (enc & 0x20) ? -1 : 1;
                    adj = i32(u32(adj) << (enc >> 3 & 3));
                    reg.a[r] += u32(adj);
                    mmuFixupVal[i] += u32(adj);
                }
            }
        }

    } else {                                    // instruction stream

        sswFlags = 0x0020;                      // size = word
        mmuSsw = 0x5000;                        // FB | RB
    }

    if (read) mmuSsw |= 0x0040;                 // RW
    mmuSsw |= u16(sswFlags) | fc;
    if (mmuRmw) mmuSsw |= 0x0080;               // RM

    mmuFaultAddr = addr;
    mmuWb3Data = mmuDataBuffer;
    mmuWb2Address = mmuState[1];
    mmuStageB = addr;
    mmuRmw = false;
    mmuRteSubstArmed = false;               // a new fault voids the latch

    throw MmuBusError{};
}

// POM68K O6: external /BERR — replays the recorded in-flight sub-access
// into the translation-fault machinery. The MmuBusError unwinds through
// mmuRead/mmuWrite/mmuFetchWord into the per-instruction handler, which
// stacks the same $A/$B frame a real bus error at that point would.
void
Moira::extBusError()
{
    assert(cpuModel == Model::M68030);
    mmuPageFault(mmuAccAddr, !mmuAccWrite, mmuAccSsw, mmuAccFc);
}

// Translated read with 68030 bus splitting (WinUAE mmu030_get_* +
// *_unaligned): every sub-access is translated separately; the SUBACCESS
// flags in mmuState[1] and the data buffer track partial completion.
template <Core C, Size S, Flags F> u32
Moira::mmuRead(u32 addr)
{
    const bool log = mmuLogging;
    if (log) mmuIdx++;

    // POM68K O6.9: the RTE of a DF-cleared fault frame marked this very
    // access as software-completed — deliver the frame's data input
    // buffer without a bus cycle (Moira.h mmuRteSubst*)
    if (mmuRteSubstArmed && !mmuRteSubstWrite
        && addr == mmuRteSubstAddr && reg.pc0 == mmuRteSubstPc) {
        mmuRteSubstArmed = false;
        const u32 v = CLIP<S>(mmuRteSubstData);
        if (log) {
            if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = v;
            mmuIdxDone++;
        }
        return v;
    }

    const u8 fc = readFC();
    const u32 rm = (mmuRmw && (fc & 1)) ? 0x0080 : 0;
    u32 v;

    SYNC(2);

    if constexpr (S == Byte) {

        if (F & POLL) POLL_IPL;
        v = read8(mmuTranslateAccess(addr, fc, false, 0x10 | rm) & addrMask<C>());
        SYNC(2);

    } else if constexpr (S == Word) {

        if (addr & 1) {

            mmuState[1] |= 0x04;                // unalign_init
            mmuDataBuffer = u32(read8(mmuTranslateAccess(addr, fc, false, 0x20 | rm) & addrMask<C>())) << 8;
            mmuState[1] |= 0x08;                // unalign_set(0)
            mmuDataBuffer |= read8(mmuTranslateAccess(addr + 1, fc, false, 0x10 | rm) & addrMask<C>());
            mmuState[1] &= u16(~0xFCu);         // unalign_clear
            v = mmuDataBuffer & 0xFFFF;

        } else {

            v = read16(mmuTranslateAccess(addr, fc, false, 0x20 | rm) & addrMask<C>());
        }
        if (F & POLL) POLL_IPL;
        SYNC(2);

    } else {

        if ((addr & 3) == 0) {

            u32 phys = mmuTranslateAccess(addr, fc, false, 0x00 | rm) & addrMask<C>();
            v = u32(read16(phys)) << 16;
            SYNC(4);
            if (F & POLL) POLL_IPL;
            v |= read16(phys + 2);
            SYNC(2);

        } else if ((addr & 1) == 0) {

            mmuState[1] |= 0x84;                // SUBACCESSL | SUBACCESS0
            mmuDataBuffer = u32(read16(mmuTranslateAccess(addr, fc, false, 0x00 | rm) & addrMask<C>())) << 16;
            SYNC(4);
            mmuState[1] |= 0x08;
            if (F & POLL) POLL_IPL;
            mmuDataBuffer |= read16(mmuTranslateAccess(addr + 2, fc, false, 0x20 | rm) & addrMask<C>());
            mmuState[1] &= u16(~0xFCu);
            SYNC(2);
            v = mmuDataBuffer;

        } else {

            mmuState[1] |= 0xC4;                // L | X | SUBACCESS0
            mmuDataBuffer = u32(read8(mmuTranslateAccess(addr, fc, false, 0x00 | rm) & addrMask<C>())) << 24;
            mmuState[1] |= 0x08;
            mmuDataBuffer |= u32(read16(mmuTranslateAccess(addr + 1, fc, false, 0x20 | rm) & addrMask<C>())) << 8;
            SYNC(4);
            mmuState[1] |= 0x10;
            if (F & POLL) POLL_IPL;
            mmuDataBuffer |= read8(mmuTranslateAccess(addr + 3, fc, false, 0x10 | rm) & addrMask<C>());
            mmuState[1] &= u16(~0xFCu);
            SYNC(2);
            v = mmuDataBuffer;
        }
    }

    if (log) {
        if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = v;
        mmuIdxDone++;
    }
    return v;
}

// Translated write, same structure (WinUAE mmu030_put_* + *_unaligned);
// the data buffer holds the full pending value (ACCESS_CHECK_PUT), so a
// faulting write stacks it as wb3_data.
template <Core C, Size S, Flags F> void
Moira::mmuWrite(u32 addr, u32 val)
{
    const bool log = mmuLogging;
    if (log) {
        mmuIdx++;
        // gencpu passes byte/word operands through signed variables, so
        // the pending-write buffer (wb3_data in fault frames) holds the
        // sign-extended value
        mmuDataBuffer = u32(SEXT<S>(val));
    }

    // POM68K O6.9: DF-cleared retried write — already done, skip the
    // bus cycle (WinUAE keeps the wb3_data value logged at fault time)
    if (mmuRteSubstArmed && mmuRteSubstWrite
        && addr == mmuRteSubstAddr && reg.pc0 == mmuRteSubstPc) {
        mmuRteSubstArmed = false;
        if (log) {
            if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = mmuDataBuffer;
            mmuIdxDone++;
        }
        return;
    }

    const u8 fc = readFC();
    const u32 rm = (mmuRmw && (fc & 1)) ? 0x0080 : 0;

    SYNC(2);

    if constexpr (S == Byte) {

        if (F & POLL) POLL_IPL;
        write8(mmuTranslateAccess(addr, fc, true, 0x10 | rm) & addrMask<C>(), u8(val));
        SYNC(2);

    } else if constexpr (S == Word) {

        if (addr & 1) {

            mmuState[1] |= 0x04;
            write8(mmuTranslateAccess(addr, fc, true, 0x20 | rm) & addrMask<C>(), u8(val >> 8));
            mmuState[1] |= 0x08;
            write8(mmuTranslateAccess(addr + 1, fc, true, 0x10 | rm) & addrMask<C>(), u8(val));
            mmuState[1] &= u16(~0xFCu);

        } else {

            write16(mmuTranslateAccess(addr, fc, true, 0x20 | rm) & addrMask<C>(), u16(val));
        }
        if (F & POLL) POLL_IPL;
        SYNC(2);

    } else {

        if ((addr & 3) == 0) {

            u32 phys = mmuTranslateAccess(addr, fc, true, 0x00 | rm) & addrMask<C>();
            write16(phys, u16(val >> 16));
            SYNC(4);
            if (F & POLL) POLL_IPL;
            write16(phys + 2, u16(val));
            SYNC(2);

        } else if ((addr & 1) == 0) {

            mmuState[1] |= 0x84;
            write16(mmuTranslateAccess(addr, fc, true, 0x00 | rm) & addrMask<C>(), u16(val >> 16));
            SYNC(4);
            mmuState[1] |= 0x08;
            if (F & POLL) POLL_IPL;
            write16(mmuTranslateAccess(addr + 2, fc, true, 0x20 | rm) & addrMask<C>(), u16(val));
            mmuState[1] &= u16(~0xFCu);
            SYNC(2);

        } else {

            mmuState[1] |= 0xC4;
            write8(mmuTranslateAccess(addr, fc, true, 0x00 | rm) & addrMask<C>(), u8(val >> 24));
            mmuState[1] |= 0x08;
            write16(mmuTranslateAccess(addr + 1, fc, true, 0x20 | rm) & addrMask<C>(), u16(val >> 8));
            SYNC(4);
            mmuState[1] |= 0x10;
            if (F & POLL) POLL_IPL;
            write8(mmuTranslateAccess(addr + 3, fc, true, 0x10 | rm) & addrMask<C>(), u8(val));
            mmuState[1] &= u16(~0xFCu);
            SYNC(2);
        }
    }

    if (log) {
        if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = mmuDataBuffer;
        mmuIdxDone++;
    }
}


// EA decode for the whole $F000-$F03F window, replicating WinUAE's
// generated MMUOP030 handlers: the EA is computed before any validation,
// so extension words are consumed and the (An)+/-(An) adjustment survives
// a subsequent Line-F trap (arbitration probes P10b/P12). Dn/An carry no
// EA (WinUAE passes 0).
template <Core C, Mode M> u32
Moira::mmuDecodeEA(int n)
{
    if constexpr (M == Mode::AI) {

        return readA(n);

    } else if constexpr (M == Mode::PI) {

        u32 ea = readA(n);
        writeA(n, ea + 4);
        return ea;

    } else if constexpr (M == Mode::PD) {

        u32 ea = readA(n) - 4;
        writeA(n, ea);
        return ea;

    } else if constexpr (M == Mode::DI || M == Mode::IX || M == Mode::AW ||
                         M == Mode::AL) {

        return computeEA<C, M, Long>(n);

    } else {

        return 0;
    }
}

// MMU configuration exception, vector 56 (invalid TC or root pointer
// written by PMOVE). WinUAE-arbitrated frame: four-word format $0 with
// the next PC (probe: SR / PC past all consumed words / $00E0). The
// Musashi oracle pushed a format $2 frame and has been patched to match.
template <Core C> void
Moira::execMmuConfigError()
{
    u16 status = getSR();

    // Enter supervisor mode and leave trace mode
    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;

    // reg.pc already points past the words consumed so far (execute()
    // pre-increments it), matching both oracles' PC at trap time.
    writeStackFrame0000<C>(status, reg.pc, 56);

    jumpToVector<C>(56);
}

// Instruction router for the $F000-$F03F window, replicating WinUAE's
// mmu_op30 (see the header comment for the arbitration record).
template <Core C, Instr I, Mode M, Size S> void
Moira::execPGen(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // PC-relative, immediate and unassigned mode-7 encodings
    // ($F03A-$F03F) are not MMU opcodes at all (WinUAE table68k
    // MMUOP030): Line-F even in user mode (probe P14 analogue).
    if constexpr (M == Mode::DIPC || M == Mode::IXPC || M == Mode::IM ||
                  M == Mode::IP) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    // D1: MMU instructions are privileged; WinUAE's generated handlers
    // check S before fetching the extension word.
    SUPERVISOR_MODE_ONLY

    auto ext = queue.irc;

    switch (ext >> 13 & 7) {

        case 0b000:
        case 0b010:
        case 0b011:

            execPMove<C, Instr::PMOVE, M, S>(opcode);
            return;

        case 0b001:

            switch (ext >> 8 & 0x1f) {

                case 0x00:
                case 0x02:

                    execPLoad<C, Instr::PLOAD, M, S>(opcode);
                    return;

                case 0x04:

                    execPFlusha<C, Instr::PFLUSHA, M, S>(opcode);
                    return;

                case 0x10:
                case 0x18:

                    execPFlush<C, Instr::PFLUSH, M, S>(opcode);
                    return;

                default:

                    break;
            }
            break;

        case 0b100:

            execPTest<C, Instr::PTEST, M, S>(opcode);
            return;

        default:    // formats 101/110/111: silent no-op in WinUAE
                    // (mmu_op30 has no case for them), but the extension
                    // word and the EA extension words are consumed
        {
            (void)readI<C, Word>();
            (void)mmuDecodeEA<C, M>(_____________xxx(opcode));

            prefetch<C, POLL>();

            FINALIZE
            return;
        }
    }

    // Consume the words like WinUAE before trapping (PC is rewound by
    // the Line-F frame anyway, which stacks the instruction address)
    (void)readI<C, Word>();
    (void)mmuDecodeEA<C, M>(_____________xxx(opcode));

    execLineF<C, I, M, S>(opcode);
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPFlush(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // PFLUSH by FC (mode $10) / by FC + EA (mode $18), § 9.7.3. The FC
    // field is validated (Line-F on bits 4-3 = 11); the fc+ea form also
    // validates the EA. POM68K O4 slice 3: the flushes are real.
    int n = _____________xxx(opcode);
    u16 modes = u16(readI<C, Word>());
    u32 ea = mmuDecodeEA<C, M>(n);

    constexpr bool invalidEA = (M == Mode::DN || M == Mode::AN ||
                                M == Mode::PI || M == Mode::PD);
    int fc;

    if (!mmuFCFromModes(modes, fc) ||
        ((modes >> 8 & 0x1f) == 0x18 && invalidEA)) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    // POM68K O4 slice 3: the ATC is real now (§ 9.7.3; WinUAE
    // mmu_op30_pflush modes $10/$18)
    u32 fcMask = u32(modes >> 5) & 7;

    if ((modes >> 8 & 0x1f) == 0x18) {
        mmuAtcFlushPageFc(ea, u32(fc), fcMask);
    } else {
        mmuAtcFlushFc(u32(fc), fcMask);
    }

    prefetch<C, POLL>();

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPFlusha(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // PFLUSHA (mode $04): the fc/mask bits must be zero (WinUAE
    // mmu_op30_pflush, probe P11c); any EA is accepted — even (An)+,
    // whose increment sticks (probe P1) — since no EA is used.
    int n = _____________xxx(opcode);
    u16 modes = u16(readI<C, Word>());
    (void)mmuDecodeEA<C, M>(n);

    if (modes & 0x7f) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    // POM68K O4 slice 3: real flush (WinUAE mmu030_flush_atc_all)
    mmuAtcFlushAll();

    prefetch<C, POLL>();

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPFlush40(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // POM68K Q2: PFLUSHN/PFLUSH/PFLUSHAN/PFLUSHA (M68040UM § 3.4.1),
    // supervisor-only (WinUAE cpuemu_31 op_f500: Exception 8 first). No
    // 040 ATC is modelled yet (Q3), so a valid encoding is a no-op.
    SUPERVISOR_MODE_ONLY

    prefetch<C, POLL>();

    CYCLES_68020(4)

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPLoad(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // MC68030UM § 9.7 PLOAD: table search + ATC fill (not modelled). The
    // search's history updates (U, and U+M for PLOADW, § 9.5.3.5) land in
    // RAM, early aborts included. MMUSR is NOT affected. WinUAE goes
    // straight to the table search — no TT match, no fc=7 bypass — and
    // runs even with translation disabled (D5). Unused extension bits
    // and the FC field are validated (Line-F).
    int n = _____________xxx(opcode);
    u16 modes = u16(readI<C, Word>());
    u32 addr = mmuDecodeEA<C, M>(n);

    constexpr bool invalidEA = (M == Mode::DN || M == Mode::AN ||
                                M == Mode::PI || M == Mode::PD);
    int fc;

    if (invalidEA || (modes & 0x1e0) || !mmuFCFromModes(modes, fc)) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    int rw = modes >> 9 & 1;                // PLOADR = 1, PLOADW = 0

    // POM68K O4 slice 3: WinUAE mmu_op30_pload = flush the page, then a
    // level-0 table search that fills the ATC (U/M updates included,
    // even with TC.E = 0 — dispute D5). MMUSR is not affected.
    mmuAtcFlushPage(addr);
    mmuAtcFill(addr, u8(fc), rw == 0);

    prefetch<C, POLL>();

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPMove(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // MC68030UM § 9.7.5 PMOVE: move to/from TT0/TT1, TC (32-bit),
    // SRP / CRP (64-bit), MMUSR (16-bit). WinUAE-arbitrated validation:
    // memory-indirectable EA only, zero low byte, no rw+fd, no fd on
    // MMUSR, only the six real registers — everything else Line-F.
    // Writing TC with E=1 validates PS+IS+TIA..TID == 32 and PS >= 8,
    // writing SRP/CRP validates DT != 0; a failure keeps the (E-cleared)
    // value and takes the MMU configuration exception (vector 56) —
    // Musashi-converged: WinUAE cannot arbitrate the TC case (it enables
    // the broken tree and double-faults on its own fake prefetch).
    int n = _____________xxx(opcode);
    u16 modes = u16(readI<C, Word>());
    u32 ea = mmuDecodeEA<C, M>(n);

    constexpr bool invalidEA = (M == Mode::DN || M == Mode::AN ||
                                M == Mode::PI || M == Mode::PD);

    const int rw = modes >> 9 & 1;
    const int fd = modes >> 8 & 1;

    if (invalidEA || (modes & 0xff) || (rw && fd)) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    bool trapped = false;

    // POM68K O4 slice 3: the register transfer itself is not part of the
    // instruction-restart access log (WinUAE x_get_long, non-state)
    mmuLogging = false;

    switch (modes >> 10 & 0x1f) {       // fmt low bits + preg, WinUAE-style

        case 0x02:  // TT0

            if (rw) writeM<C, M, Long>(ea, reg.tt0);
            else    reg.tt0 = readM<C, M, Long>(ea);
            break;

        case 0x03:  // TT1

            if (rw) writeM<C, M, Long>(ea, reg.tt1);
            else    reg.tt1 = readM<C, M, Long>(ea);
            break;

        case 0x10:  // TC (§ 9.7.2.2)

            if (rw) {

                writeM<C, M, Long>(ea, reg.tc);

            } else {

                // POM68K O6: the words already in the 030 pipe were
                // fetched under the OLD translation — capture them
                // before the switch (LC II ROM "pmove tc; nop; bne; jmp")
                mmuCapturePipe();
                reg.tc = readM<C, M, Long>(ea);

                if (reg.tc & 0x80000000) {

                    // E=1: PS+IS+TIA+TIB+TIC+TID must sum to 32 and PS
                    // must be >= 8 (256-byte pages)
                    int bits = 0;
                    for (int sh = 20; sh >= 0; sh -= 4) {
                        bits += reg.tc >> sh & 0xf;
                    }
                    if (bits != 32 || !(reg.tc >> 23 & 1)) {

                        reg.tc &= ~u32(0x80000000); // E cleared
                        execMmuConfigError<C>();
                        trapped = true;
                    }
                }
            }
            break;

        case 0x12:  // SRP: DT 0 (invalid) not allowed

            if (rw) {

                writeM<C, M, Long>(ea, u32(reg.srp >> 32));
                writeM<C, M, Long>(ea + 4, u32(reg.srp));

            } else {

                mmuCapturePipe();       // POM68K O6: see the TC case
                reg.srp = u64(readM<C, M, Long>(ea)) << 32;
                reg.srp |= readM<C, M, Long>(ea + 4);
                if ((reg.srp >> 32 & 3) == 0) {
                    execMmuConfigError<C>();
                    trapped = true;
                }
            }
            break;

        case 0x13:  // CRP: DT 0 (invalid) not allowed

            if (rw) {

                writeM<C, M, Long>(ea, u32(reg.crp >> 32));
                writeM<C, M, Long>(ea + 4, u32(reg.crp));

            } else {

                mmuCapturePipe();       // POM68K O6: see the TC case
                reg.crp = u64(readM<C, M, Long>(ea)) << 32;
                reg.crp |= readM<C, M, Long>(ea + 4);
                if ((reg.crp >> 32 & 3) == 0) {
                    execMmuConfigError<C>();
                    trapped = true;
                }
            }
            break;

        case 0x18:  // MMUSR (§ 9.7.5.5); fd must be clear

            if (fd) {

                execLineF<C, I, M, S>(opcode);
                return;
            }
            if (rw) writeM<C, M, Word>(ea, reg.mmusr);
            else    reg.mmusr = u16(readM<C, M, Word>(ea));
            break;

        default:    // not an MMU register (WinUAE: "Bad PMOVE")

            execLineF<C, I, M, S>(opcode);
            return;
    }

    // POM68K O4 slice 3: a register write with FD clear flushes the whole
    // ATC (WinUAE mmu_op30_pmove tail; skipped when the write trapped,
    // and never for MMUSR)
    if (!trapped && !rw && !fd && (modes >> 10 & 0x1f) != 0x18) {
        mmuAtcFlushAll();
    }

    if (!trapped) prefetch<C, POLL>();

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPTest(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // MC68030UM § 9.7.6 PTEST. WinUAE-arbitrated: level 0 searches the
    // TT registers and the (empty) ATC only — a TT match sets T, a miss
    // sets I; level > 0 walks the translation tree directly (no TT
    // match, no fc=7 bypass), regardless of TC.E (D5). Validation:
    // memory-indirectable EA, decodable FC, and level 0 cannot return a
    // descriptor (A=1 → Line-F, probe P6). No descriptor U/M updates.
    int n = _____________xxx(opcode);
    u16 modes = u16(readI<C, Word>());
    u32 vAddr = mmuDecodeEA<C, M>(n);

    constexpr bool invalidEA = (M == Mode::DN || M == Mode::AN ||
                                M == Mode::PI || M == Mode::PD);

    const int level = modes >> 10 & 7;
    const int rw = modes >> 9 & 1;      // PTESTR = 1, PTESTW = 0
    int fc;

    if (invalidEA || !mmuFCFromModes(modes, fc) ||
        (!level && (modes & 0x100))) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    u16 sr = 0;

    if (!level) {

        // POM68K O4 slice 3: real ATC search (WinUAE
        // mmu030_ptest_atc_search): TT match reports T; then the ATC is
        // searched with the RAW (unmasked) EA — a WinUAE quirk kept
        // as-is, oracle wins; a miss reports I.
        if (!mmuMatchTT(reg.tt0, vAddr, fc, rw, sr) &&
            !mmuMatchTT(reg.tt1, vAddr, fc, rw, sr)) {

            int hit = -1;
            for (int i = 0; i < MMU_ATC_ENTRIES; i++) {
                const auto &e = mmuAtcArr[i];
                if (e.valid && e.fc == u8(fc) && e.logical == vAddr) { hit = i; break; }
            }
            if (hit < 0) {
                sr = 0x0400;
            } else {
                const auto &e = mmuAtcArr[hit];
                if (e.busError) sr |= 0x8400;               // B | I
                if (e.writeProtect) sr |= 0x0800;           // WP
                if (e.modified) sr |= 0x0200;               // M
            }
        }

    } else {

        u32 table, addrOut = 0;
        int type;
        mmuRootPointer(fc, table, type);
        (void)mmuWalkTables(vAddr, type, table, fc, level, rw, true,
                            addrOut, sr);

        if (modes & 0x100) writeA(modes >> 5 & 7, addrOut);
    }

    reg.mmusr = sr;

    prefetch<C, POLL>();

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPTest40(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // POM68K Q3: PTESTR/PTESTW (M68040UM § 3.4.2; WinUAE mmu_op_real,
    // PTEST branch), supervisor-only. DFC selects the address space
    // (super = dfc&4, data = (dfc&3) != 2); a TTR hit reports T|R (or B
    // when a PTESTW hits a write-protected TTR), otherwise the walk's
    // page descriptor lands in MMUSR040 (0 when invalid; WinUAE
    // mmu_fill_atc status = phys | G|Ux|S|CM|M|W|R).
    SUPERVISOR_MODE_ONLY

    int n = _____________xxx(opcode);
    bool write = (opcode & 0x20) == 0;          // $F548 = PTESTW
    u32 addr = readA(n);
    bool super = (reg.dfc & 4) != 0;
    bool data = (reg.dfc & 3) != 2;

    int ttr = mmu040MatchTTR(addr, super, data);
    if (ttr) {
        reg.mmusr040 = (ttr == 2 && write) ? 0x800     // MMU_MMUSR_B
                                           : 0x003;    // MMU_MMUSR_T | R
    } else {
        u32 status = mmu040Walk<C>(addr, super, write);
        u32 maski = mmu040PageMaskI();
        reg.mmusr040 = (status & maski)
                     | (status & 0x7F7);               // G|Ux|S|CM|M|W|R
    }

    prefetch<C, POLL>();

    CYCLES_68020(8)

    FINALIZE
}

// -----------------------------------------------------------------------------
// POM68K Q3 — 68040 MMU bus translation, modelled byte-for-byte on the
// primary oracle (WinUAE cpummu.c / cpummu.h mmu040 accessors, hatari
// e77819f7). No architectural ATC is modelled: the oracle flushes its
// ATC on every state load, so a walk-on-every-access model is
// behaviourally identical (U/M rewrites of already-set bits produce no
// RAM diff); a perf ATC overlay can come later (030 precedent).
// -----------------------------------------------------------------------------

template <Core C> bool
Moira::mmu040InstrStart()
{
    // Same shape as the mode-5 68030 loop head (mmuExecuteStart): the
    // 040 oracle has no prefetch queue — the opcode is fetched through
    // translation at every instruction start (WinUAE x_prefetch(0)) and
    // extension words at consumption time (readExt); the queue-refill
    // prefetches at instruction end are suppressed. The irc lookahead at
    // pc + 2 is Moira's one-slot model (known limit, shared with the
    // 030: visible only when an instruction sits at a page's last word).
    POLL_IPL;

    mmu040EffAddr = 0;
    mmu040AccFirst = true;
    mmu040AccSplit = false;
    mmu040LastWrite = false;
    mmu040Moves = -1;
    mmu040Lrmw = false;
    mmuFixupReg[0] = mmuFixupReg[1] = 0;
    mmu040CcrSave = u8(getCCR());

    try {

        queue.ird = u16(mmu040Read<C, Word>(reg.pc, false));
        queue.irc = u16(mmu040Read<C, Word>(reg.pc + 2, false));

    } catch (MmuBusError &) {

        try { execMmu040BusError<C>(); } catch (...) { halt(); }
        return false;
    }

    return true;
}

int
Moira::mmu040MatchTTR(u32 addr, bool super, bool data) const
{
    // WinUAE mmu_do_match_ttr over the dtt/itt pair (mmu_match_ttr)
    const u32 ttrs[2] = { data ? reg.dtt0 : reg.itt0,
                          data ? reg.dtt1 : reg.itt1 };

    for (u32 ttr : ttrs) {

        if (!(ttr & 0x8000)) continue;                  // E

        u8 msb  = u8(((addr ^ ttr) & 0xFF000000) >> 24);
        u8 mask = u8((ttr & 0x00FF0000) >> 16);
        if (msb & ~mask) continue;

        if (!(ttr & 0x4000)) {                          // S-field != both
            if ((((ttr >> 13) & 1) != 0) != super) continue;
        }
        return (ttr & 4) ? 2 : 1;                       // W -> match + WP
    }
    return 0;
}

template <Core C> u32
Moira::mmu040Walk(u32 addr, bool super, bool write)
{
    // WinUAE mmu_fill_atc: descriptors are fetched/updated with
    // supervisor function codes on the raw physical bus. Returns the
    // page descriptor (phys | status bits, R = bit 0) with the WP bit
    // accumulated over all levels, or 0 when any level is invalid.
    u32 wp = 0;

    u32 desc = super ? reg.srp040 : reg.urp040;
    u32 descAddr = (desc & 0xFFFFFE00) | ((addr >> 23) & 0x1FC);
    desc = mmuRead32(descAddr);
    if (!(desc & 2)) return 0;                          // invalid root
    wp |= desc;
    if (!(desc & 8)) mmuWrite32(descAddr, desc | 8);    // U

    descAddr = (desc & 0xFFFFFE00) | ((addr >> 16) & 0x1FC);
    desc = mmuRead32(descAddr);
    if (!(desc & 2)) return 0;                          // invalid pointer
    wp |= desc;
    if (!(desc & 8)) mmuWrite32(descAddr, desc | 8);

    if (reg.tc040 & 0x4000) {                           // 8K pages
        descAddr = (desc & 0xFFFFFF80) + ((addr >> 11) & 0x7C);
    } else {                                            // 4K pages
        descAddr = (desc & 0xFFFFFF00) + ((addr >> 10) & 0xFC);
    }
    desc = mmuRead32(descAddr);
    if ((desc & 3) == 2) {                              // indirect (once)
        descAddr = desc & 0xFFFFFFFC;
        desc = mmuRead32(descAddr);
    }
    if (!(desc & 1)) return 0;                          // invalid page
                                                        // (incl. 2x indirect)
    wp |= desc;
    if (write) {
        if ((wp & 4) || ((desc & 0x80) && !super)) {
            // write will fault: only U is maintained
            if (!(desc & 8)) { desc |= 8; mmuWrite32(descAddr, desc); }
        } else if ((desc & 0x18) != 0x18) {
            desc |= 0x18;                               // U + M
            mmuWrite32(descAddr, desc);
        }
    } else {
        if (!(desc & 8)) { desc |= 8; mmuWrite32(descAddr, desc); }
    }
    desc |= wp & 4;                                     // accumulated WP
    return desc;
}

template <Core C> u32
Moira::mmu040Translate(u32 addr, u32 val, bool super, bool data,
                       bool write, int szCode)
{
    // WinUAE mmu_get/put_* accessor heads: TTR match first (a WP match
    // faults writes even with translation disabled), then the walk.
    // Locked RMW cycles translate as WRITES from the read on (WinUAE
    // mmu_get_user_*: the lrmw accessors pass write = true).
    int fc = (super ? 4 : 0) | (data ? 1 : 2);
    // The locked-RMW write-flavor applies to DATA accesses only — the
    // CAS extension word still fetches through the plain read path
    // (WinUAE wraps only the get_lrmw_* data accessors)
    bool asWrite = write || (mmu040Lrmw && data);

    int ttr = mmu040MatchTTR(addr, super, data);
    if (ttr) {
        if (asWrite && ttr == 2) mmu040Fault<C>(addr, val, fc, asWrite, szCode);
        return addr;
    }
    if (!mmu040Enabled()) return addr;

    u32 status = mmu040Walk<C>(addr, super, asWrite);

    if ((asWrite && (status & 4)) || (!super && (status & 0x80)) ||
        !(status & 1)) {
        // locked-RMW reads fault with WRITE semantics (WinUAE passes
        // write = true through mmu_get_user_*; SSW.LK strips RW anyway)
        mmu040Fault<C>(addr, val, fc, asWrite, szCode);
    }
    u32 maski = mmu040PageMaskI();
    return (status & maski) | (addr & ~maski);
}

template <Core C> void
Moira::mmu040Fault(u32 addr, u32 val, int fc, bool write, int szCode)
{
    // WinUAE mmu_bus_error, 68040 branch — SSW + writeback capture
    u16 ssw = 0;

    if (mmu040Moves >= 0) {                             // ismoves
        fc = mmu040Moves;
        if ((fc & 3) == 0 || (fc & 3) == 3) {
            ssw |= 0x0010;                              // TT1
        } else if (fc & 2) {
            fc = (fc & 4) | 1;
        }
        mmu040Moves = -1;
    }

    ssw |= u16(fc & 7);                                 // TM

    switch (szCode) {
        case 0: ssw |= 0x0020; break;                   // SIZE_B
        case 1: ssw |= 0x0040; break;                   // SIZE_W
        default: break;                                 // SIZE_L = 0
    }

    mmu040Wb3Status = write ? u16(0x80 | (ssw & 0x7F)) : 0;
    mmu040Wb3Data = val;
    mmu040Wb2Status = 0;
    if (!write) ssw |= 0x0100;                          // RW

    if (szCode == 3) {                                  // MOVE16 line
        ssw |= 0x0060;                                  // SIZE_CL
        ssw |= 0x0008;                                  // TT0
        mmu040EffAddr &= ~u32(15);
        if (write) {
            mmu040Wb3Status &= ~0x80;
            mmu040Wb2Status = u16(0x80 | 0x60 | (ssw & 0x1F));
            mmu040Wb2Address = mmu040EffAddr;
        }
    }

    if (mmu040MovemArmed) {                             // MOVEM restart
        ssw |= 0x1000;                                  // CM
        mmu040EffAddr = mmu040MovemEa;
        mmu040MovemArmed = false;
    }
    if (mmu040Lrmw) {                                   // locked RMW
        ssw |= 0x0200;                                  // LK
        ssw &= u16(~0x0100);                            // RW cleared
        mmu040Lrmw = false;
    }

    ssw |= 0x0400;                                      // ATC (MMU fault)

    u32 fa = addr;
    if (!mmu040AccFirst) {                              // split, later part
        fa = mmu040AccBase;                             // misalignednotfirst
        ssw |= 0x0800;                                  // MA
    }
    if (write && mmu040AccSplit) mmu040Wb3Data = mmu040FullVal;

    mmu040Ssw = ssw;
    mmu040FaultAddr = fa;

    throw MmuBusError();
}

template <Core C, Size S> u32
Moira::mmu040Read(u32 addr, bool data)
{
    const bool super = mmu040Moves >= 0 ? (mmu040Moves & 4) != 0
                                        : bool(reg.sr.s);
    const u32 pageMask = ~mmu040PageMaskI();

    mmu040AccBase = addr;
    mmu040AccFirst = true;
    mmu040AccSplit = false;

    if constexpr (S == Byte) {

        u32 pa = mmu040Translate<C>(addr, 0, super, data, false, 0);
        SYNC(2);
        u32 r = read8(pa & addrMask<C>());
        SYNC(2);
        return r;
    }

    if constexpr (S == Word) {

        if ((addr & pageMask) + 2 > pageMask + 1) {     // page-crossing
            mmu040AccSplit = true;
            u32 hi = mmu040Translate<C>(addr, 0, super, data, false, 1);
            SYNC(2);
            u32 r = u32(read8(hi & addrMask<C>())) << 8;
            mmu040AccFirst = false;
            u32 lo = mmu040Translate<C>(addr + 1, 0, super, data, false, 1);
            r |= read8(lo & addrMask<C>());
            SYNC(2);
            return r;
        }
        u32 pa = mmu040Translate<C>(addr, 0, super, data, false, 1);
        SYNC(2);
        u32 r = read16(pa & addrMask<C>());
        SYNC(2);
        return r;
    }

    if constexpr (S == Long) {

        if ((addr & pageMask) + 4 > pageMask + 1) {     // page-crossing
            mmu040AccSplit = true;
            if (!(addr & 1)) {                          // word halves
                u32 hi = mmu040Translate<C>(addr, 0, super, data, false, 2);
                SYNC(2);
                u32 r = u32(read16(hi & addrMask<C>())) << 16;
                SYNC(4);
                mmu040AccFirst = false;
                u32 lo = mmu040Translate<C>(addr + 2, 0, super, data, false, 2);
                r |= read16(lo & addrMask<C>());
                SYNC(2);
                return r;
            }
            u32 r = 0;                                  // four bytes
            for (int i = 0; i < 4; i++) {
                u32 pa = mmu040Translate<C>(addr + i, 0, super, data, false, 2);
                r = r << 8 | read8(pa & addrMask<C>());
                mmu040AccFirst = false;
            }
            SYNC(8);
            return r;
        }
        u32 pa = mmu040Translate<C>(addr, 0, super, data, false, 2);
        SYNC(2);
        u32 r = u32(read16(pa & addrMask<C>())) << 16;
        SYNC(4);
        r |= read16((pa + 2) & addrMask<C>());
        SYNC(2);
        return r;
    }
}

template <Core C, Size S> void
Moira::mmu040Write(u32 addr, u32 val, bool data)
{
    const bool super = mmu040Moves >= 0 ? (mmu040Moves & 4) != 0
                                        : bool(reg.sr.s);
    const u32 pageMask = ~mmu040PageMaskI();

    // Default last-write marking (gencpu gen_set_fault_pc): the store an
    // instruction ends on faults with PC = next instruction and no
    // restore. reg.pc points at the last consumed word = next - 2...
    // no: at the final store every extension word has been consumed, so
    // reg.pc IS the next instruction (Moira's pc tracks irc). Sites
    // whose write precedes a deferred word (MOVE to ABS.L) pre-arm the
    // marker with the corrected PC; MOVEM transfers are never last
    // writes (the CM restart latch covers them).
    if (data && !mmu040MovemArmed && !mmu040LastWrite) {
        mmu040LastWrite = true;
        mmu040LastWritePc = reg.pc;
    }

    mmu040AccBase = addr;
    mmu040AccFirst = true;
    mmu040AccSplit = false;
    mmu040FullVal = val;

    if constexpr (S == Byte) {

        u32 pa = mmu040Translate<C>(addr, val, super, data, true, 0);
        SYNC(2);
        write8(pa & addrMask<C>(), u8(val));
        SYNC(2);
        return;
    }

    if constexpr (S == Word) {

        if ((addr & pageMask) + 2 > pageMask + 1) {
            mmu040AccSplit = true;
            u32 hi = mmu040Translate<C>(addr, val >> 8, super, data, true, 1);
            SYNC(2);
            write8(hi & addrMask<C>(), u8(val >> 8));
            mmu040AccFirst = false;
            u32 lo = mmu040Translate<C>(addr + 1, val, super, data, true, 1);
            write8(lo & addrMask<C>(), u8(val));
            SYNC(2);
            return;
        }
        u32 pa = mmu040Translate<C>(addr, val, super, data, true, 1);
        SYNC(2);
        write16(pa & addrMask<C>(), u16(val));
        SYNC(2);
        return;
    }

    if constexpr (S == Long) {

        if ((addr & pageMask) + 4 > pageMask + 1) {
            mmu040AccSplit = true;
            if (!(addr & 1)) {
                u32 hi = mmu040Translate<C>(addr, val >> 16, super, data, true, 2);
                SYNC(2);
                write16(hi & addrMask<C>(), u16(val >> 16));
                SYNC(4);
                mmu040AccFirst = false;
                u32 lo = mmu040Translate<C>(addr + 2, val, super, data, true, 2);
                write16(lo & addrMask<C>(), u16(val));
                SYNC(2);
                return;
            }
            for (int i = 0; i < 4; i++) {
                u8 b = u8(val >> (24 - 8 * i));
                u32 pa = mmu040Translate<C>(addr + i, b, super, data, true, 2);
                write8(pa & addrMask<C>(), b);
                mmu040AccFirst = false;
            }
            SYNC(8);
            return;
        }
        u32 pa = mmu040Translate<C>(addr, val, super, data, true, 2);
        SYNC(2);
        write16(pa & addrMask<C>(), u16(val >> 16));
        SYNC(4);
        write16((pa + 2) & addrMask<C>(), u16(val));
        SYNC(2);
        return;
    }
}

template <Core C> void
Moira::mmu040GetMove16(u32 addr)
{
    // WinUAE mmu_get_move16: line-aligned, one translation, four
    // physical longs into the line buffer (frame PD0-3)
    const bool super = reg.sr.s;
    u32 a = addr & ~u32(15);

    mmu040AccBase = a;
    mmu040AccFirst = true;
    mmu040AccSplit = false;

    u32 pa = mmu040Translate<C>(a, 0, super, true, false, 3);
    for (int i = 0; i < 4; i++) {
        mmu040Move16[i] = u32(read16((pa + 4 * i) & addrMask<C>())) << 16
                        | read16((pa + 4 * i + 2) & addrMask<C>());
    }
    SYNC(8);
}

template <Core C> void
Moira::mmu040PutMove16(u32 addr)
{
    const bool super = reg.sr.s;
    u32 a = addr & ~u32(15);

    mmu040AccBase = a;
    mmu040AccFirst = true;
    mmu040AccSplit = false;

    u32 pa = mmu040Translate<C>(a, mmu040Move16[0], super, true, true, 3);
    for (int i = 0; i < 4; i++) {
        write16((pa + 4 * i) & addrMask<C>(), u16(mmu040Move16[i] >> 16));
        write16((pa + 4 * i + 2) & addrMask<C>(), u16(mmu040Move16[i]));
    }
    SYNC(8);
}

// Bus-fault exception processing: WinUAE m68k_run_mmu040 CATCH (restart:
// CCR + PC restored, (An)± fixups undone) + Exception_mmu nr == 2
// (vector fetched before the frame; both go through translation — a
// nested fault double-faults into HALT via processException).
template <Core C> void
Moira::execMmu040BusError()
{
    // gencpu's gen_set_fault_pc dichotomy (probed 2026-07-18): a fault
    // on the instruction's LAST write sets instruction_pc = NEXT
    // instruction and mmu_restart = false — no CCR/PC/fixup restore,
    // the (An)± adjustment survives (the handler completes the write
    // from WB3 and RTEs past the instruction). Every other fault
    // restarts: CCR restored to the pre-instruction value, (An)±
    // fixups undone, stacked PC = the instruction.
    u32 framePc;
    if (mmu040LastWrite) {
        framePc = mmu040LastWritePc;
        mmu040LastWrite = false;
    } else {
        setCCR(u8(mmu040CcrSave));
        for (int i = 0; i < 2; i++) {
            if (mmuFixupReg[i]) reg.a[mmuFixupReg[i] & 7] = mmuFixupVal[i];
            mmuFixupReg[i] = 0;
        }
        framePc = reg.pc0;
    }
    mmu040Moves = -1;
    mmu040Lrmw = false;
    reg.pc = framePc;

    willExecute(M68kException::BUS_ERROR, 2);

    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    trace040Pending = false;
    SYNC(8);

    u32 newpc = read<C, AddrSpace::DATA, Long>(reg.vbr + 4 * 2);

    // Format $7 frame (Exception_build_stack_frame case 0x7)
    push<C, Long>(mmu040Move16[3]);         // PD3
    push<C, Long>(mmu040Move16[2]);         // PD2
    push<C, Long>(mmu040Move16[1]);         // PD1
    push<C, Long>(mmu040Move16[0]);         // WB1D/PD0
    push<C, Long>(0);                       // WB1A
    push<C, Long>(0);                       // WB2D
    push<C, Long>(mmu040Wb2Address);        // WB2A
    push<C, Long>(mmu040Wb3Data);           // WB3D
    push<C, Long>(mmu040FaultAddr);         // WB3A
    push<C, Long>(mmu040FaultAddr);         // FA
    push<C, Word>(0);
    push<C, Word>(mmu040Wb2Status);
    mmu040Wb2Status = 0;
    push<C, Word>(mmu040Wb3Status);
    mmu040Wb3Status = 0;
    push<C, Word>(mmu040Ssw);
    push<C, Long>(mmu040EffAddr);
    push<C, Word>(0x7000 | 2 << 2);
    push<C, Long>(reg.pc);                  // instruction, or next on last-write
    push<C, Word>(status);

    if (newpc & 1) { halt(); return; }      // double fault

    reg.pc = reg.pc0 = newpc;
    fullPrefetch<C, POLL>();

    if (debugger.catchpointMatches(2)) didReachCatchpoint(u8(2));
    didJumpToVector(2, reg.pc);

    didExecute(M68kException::BUS_ERROR, 2);
}
