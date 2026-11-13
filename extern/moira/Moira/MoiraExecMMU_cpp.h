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


// [B] MC68030UM-1989 §9.8: MMU instruction overview; decode from oracle below.
// [F] Retired Musashi comparison: local patched revision unavailable.
// Settled by: a replay with a pinned replacement oracle.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
//
// POM68K O4 — 68030 MMU instruction execution, TWO-ORACLE ARBITRATED
// (2026-07-15). Originally converged on Musashi alone; the first
// arbitration turn re-ran every dispute (oracle/fuzz/disputes/NOTES.md
// D1-D5) against WinUAE (oracle/uae, hatari e77819f7 cpummu030.c +
// gencpu MMUOP030 handlers). The manual supplies the architectural
// contract; oracle-specific decode/order choices remain grade D. The decode below therefore replicates WinUAE:
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
// Those single-instruction arbitration vectors do not observe retained
// ATC state: the oracles flush it on load. The bus layer below models it.

// Raw physical bus accesses used by the table walk. Both oracles read the
// translation tree in physical space; the bus translation layer is a
// later slice, so these stay untranslated.
u32
Moira::mmuRead32(u32 addr) const
{
    // POM68K: sequence host words; a first-half abort cannot read the second.
    const u32 hi = read16(addr);
    return hi << 16 | read16(addr + 2);
}

void
Moira::mmuWrite32(u32 addr, u32 val) const
{
    write16(addr, u16(val >> 16));
    write16(addr + 2, u16(val));
}

// [F] Retired Musashi comparison: local patched revision unavailable.
// Settled by: a replay with a pinned replacement oracle.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [B] MC68030UM-1989 §9.3 and §9.7.3: transparent translation.
// Transparent translation match (MC68030UM-1989 § 9.3, TTx format § 9.7.3):
// enabled (E, bit 15), address base/mask on A31-A24, FC base/mask, R/W(M).
// On a match the MMUSR T bit is set (§ 9.7.4).
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

// [B] MC68030UM-1989 §9.7.4: MMUSR flags.
// MMUSR accumulation while walking (§ 9.7.4): M from page descriptors,
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

// [B] MC68030UM-1989 §9.5.4 and §9.8: table updates and PTEST/PLOAD.
// History maintenance (§ 9.5.4): the walk sets U in every descriptor it
// traverses, and U+M in the page descriptor on a write access that is not
// write-protected. PTEST performs no updates (§ 9.8: it "does not alter the ATC"); PLOAD does.
void
Moira::mmuUpdateDescriptor(u32 tptr, int type, u32 entry, int rw) const
{
    if (type == 1 && !rw && !(entry & 0x10) && !(entry & 0x04)) {

        mmuWrite32(tptr, entry | 0x08 | 0x10);                  // U + M
    } else if (type != 0 && !(entry & 0x08)) {

        mmuWrite32(tptr, entry | 0x08);                         // U
    }
}

// [B] MC68030UM-1989 §9.5.2: table search; PTEST simplification below.
// [F] Retired Musashi comparison: local patched revision unavailable.
// Settled by: a replay with a pinned replacement oracle.
// Translation-table walk (§ 9.5.2). Short (4-byte) and long (8-byte)
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
    int bitpos = (reg.tc & 0x01000000) ? 16 : 12;       // [B] MC68030UM-1989 §9.5.5.1: function-code lookup. — FCL (§ 9.5.5.1)
    int pageshift = is;
    bool resolved = false;

    addrIn <<= is;

    // A host /BERR on a descriptor fetch ends the search: B|I, and the
    // descriptor that faulted is not counted in N (Moira.h
    // § mmuInTableSearch; [D] WinUAE descr_num-- @ Hatari e77819f7). Every fetch below follows
    // its level++, so the fault always undoes exactly one count.
    mmuInTableSearch = true;
    try {
    do {

        const int indexbits = int(bits >> bitpos) & 0xf;
        // Oracle quirk: indexbits == 0 shifts a u32 by 32, which on x86
        // leaves the value unchanged — replicated with the & 31.
        const u32 tableIndex =
            (bitpos == 16) ? u32(fc) : addrIn >> ((32 - indexbits) & 31);
        u32 entry, entry2;

        bitpos -= 4;
        // [F] Retired Musashi comparison: local patched revision unavailable.
        // Settled by: a replay with a pinned replacement oracle.
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // POM68K D3 (WinUAE-arbitrated 2026-07-15): a level is the last
        // one when the NEXT index field is zero (WinUAE last_table);
        // Musashi's unmasked (bits >> bitpos) still saw the consumed
        // upper fields, so indirection only ever fired at TID.
        // POM68K: past TID (bitpos < 0) there is no next field; the former
        // `!bitpos` test let `bits >> -4` through, which is UB (the hosts
        // masked it to 28 and read 0 — same answer). Vendor row 36.
        const bool indirect = ((bitpos <= 0 || !((bits >> bitpos) & 0xf)) && indexbits);

        switch (type) {

            case 0: // [B] MC68030UM-1989 §9.7.4: invalid descriptor status. — invalid descriptor → MMUSR I (§ 9.7.4)

                // [F] Retired Musashi comparison: local patched revision unavailable.
                // Settled by: a replay with a pinned replacement oracle.
                // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                // POM68K D4 (WinUAE-arbitrated 2026-07-15): OR, don't
                // assign — WP accumulated on the path stays visible
                // (probe: PTESTW through a WP table descriptor into DT=0
                // gives $0C02 on WinUAE; Musashi erased to $0402).
                sr |= 0x0400;
                resolved = true;
                break;

            case 1: // [B] MC68030UM-1989 §9.5.3.1: early termination. — page descriptor (or early termination, § 9.5.3.1)

                if (!ptest) {

                    table &= ~u32(0) << pagesize;
                    addrOut = table + (addrIn >> pageshift);
                }
                // PTEST keeps addrOut at the last descriptor address
                resolved = true;
                break;

            case 2: // [B] MC68030UM-1989 §9.5.1.3: short table descriptor. — valid 4-byte (short) descriptors (§ 9.5.1.3)

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

            case 3: // [B] MC68030UM-1989 §9.5.1.4: long table descriptor. — valid 8-byte (long) descriptors (§ 9.5.1.4)

                level++;
                addrOut = table + (tableIndex << 3);
                entry = mmuRead32(addrOut);
                entry2 = mmuRead32(addrOut + 4);
                type = int(entry) & 3;

                if (indirect && (type == 2 || type == 3)) {

                    level++;
                    addrOut = entry2 & 0xfffffffc;
                    entry = mmuRead32(addrOut);
                    // [F] Retired Musashi comparison: local patched revision unavailable.
                    // Settled by: a replay with a pinned replacement oracle.
                    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
    } catch (MmuTableSearchBusError &) {
        mmuInTableSearch = false;
        if (level > 0) level--;
        sr = u16((sr | 0x8400) & 0xfff0) | u16(level);    // B | I
        return true;
    } catch (...) {
        mmuInTableSearch = false;
        throw;
    }
    mmuInTableSearch = false;

    // [B] MC68030UM-1989 §9.7.4: invalid descriptor at the search limit.
    // POM68K: a descriptor fetched AT the level limit is still examined —
    // an invalid one reports I. The loop above examines a descriptor at
    // the top of the next pass, which the limit prevents.
    // MC68030UM-1989 § 9.7.4, Table 9-3, PTEST levels
    //     1-7: "The I bit is set if the DT field of a table or a page
    //     descriptor encountered during the search is set to invalid" —
    //     whatever ended the search. (The former subsection citation did not exist in either checked edition.)
    // [D] WinUAE cpummu030.c mmu030_table_search @ Hatari e77819f7 checks
    //     each descriptor as fetched, before its level test.
    // Found by fuzz vector "mmu/tt seed=1006 #1188" (PTESTR level 1 into a
    // DT=0 descriptor: MMUSR $001 here, $401 there). Gate: moira_rules_test.
    if (!resolved && type == 0) sr |= 0x0400;

    sr = u16((sr & 0xfff0) | level);
    return resolved;
}

// [B] MC68030UM-1989 §9.7.1 and §9.8: root selection and explicit searches.
// Root pointer selection for PTEST/PLOAD walks: SRP when SRE is set and
// FC is a supervisor code, else CRP (§ 9.7.1). Like both oracles, the
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

// [B] MC68030UM-1989 §9.2, §9.4, §9.5.2 and §8.4: translation architecture.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
//
// POM68K O4 slice 3 — 68030 MMU BUS LAYER (2026-07-15)
//
// Bus-level address translation (MC68030UM-1989 § 9.2), converged on the
// primary oracle WinUAE cpummu030.c (hatari e77819f7): transparent
// translation first (§ 9.3), then the 22-entry ATC (§ 9.4), then a
// table search (§ 9.5.2) that fills the ATC and maintains the U/M bits.
// Translation faults raise a bus error (vector 2) with a format $A frame
// (fault on the instruction's last write) or a format $B frame (any
// other data or instruction-stream fault) — § 8.4. The internal-state
// words of those frames (access log, pending-fixup encodings, SSW,
// MOVEM counters) replicate WinUAE's byte-for-byte, because the fuzzing
// differ compares raw RAM. See POM68K_VENDOR.md § MMU bus layer.

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
    // The model itself lives in Moira.h pomIcacheFetch<>, shared verbatim
    // with mmuExecuteStart's fused ird/irc fetch so it exists exactly once.
    pomIcacheFetch<1>(addr);

    // POM68K O6: recorded here too — with TC.E off mmuTranslateAccess is
    // bypassed, but an unmapped fetch must still be extBusError()-able
    mmuAccAddr = addr; mmuAccSsw = 0x0020; mmuAccFc = fc; mmuAccWrite = false;

#if MOIRA_ENABLE_JIT
    // POM68K JIT code window, 030 flavor (2026-07-28). ONE hook site covers
    // every instruction-stream fetch — mmuExecuteStart's ird/irc AND the
    // readExt 030 branch all come through here. Placed AFTER the icache
    // overlay (its miss penalty is cycle-visible and must keep charging)
    // and after the acc stamp (extBusError readback). pomJitCovers vets
    // generation and privilege; the eviction hook keeps a hit implying
    // "the interpreter's own ATC would have hit too".
    if (const u8 *p; pomJitFetch(addr, 2, p)) {
        return u16(u16(p[0]) << 8 | p[1]);
    }

#endif

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

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Logs the value of a completed instruction-level access
// (ACCESS_EXIT_GET/PUT in WinUAE cpummu030.h)
void
Moira::mmuLogExtWord(u32 value)
{
    if (mmuLogging) {

        pomMmuBumpIdx();
        if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = value;
        pomMmuBumpIdxDone();
    }
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
    // The extended-EA results belong to this instruction too.
    // [D] WinUAE cpummu030.c mmu030_disp_store @ Hatari e77819f7 — kept
    // across instructions, so a $B frame carried the indexed EA of whatever
    // instruction last computed one; under the JIT that instruction may have
    // run as generated code, which keeps no such latch, and the same fault
    // stacked two different frames (sst68030_jit, "random/tt seed=709 #340").
    // Nothing reads the field back (RTE does not restore it); a vector starts
    // from zero, so the corpora are unchanged.
    // [F] for silicon: an internal register. Settled by: the $B frame a real
    // 68030 stacks for a fault that follows an indexed-EA instruction.
    mmuDispStore[0] = mmuDispStore[1] = 0;
    mmuFixupReg[0] = mmuFixupReg[1] = 0;
    mmuCcrSave = getCCR();
    mmuLogging = true;
    mmuRmw = false;
    mmuOpcodeV = 0xFFFFFFFF;

#if MOIRA_ENABLE_JIT
    // POM68K JIT code window, 030 flavour (B.2 slice 4) — the shape
    // mmu040InstrStart already uses below. Placed AFTER the POLL_IPL and the
    // per-instruction MMU resets above, which feed execMmuBusError and may
    // never be skipped. Three conditions, in this order:
    //  (i)   no pre-switch fetch pipe is live. mmuFetchWord serves pipe words
    //        BEFORE the i-cache counters, and pomMmuPipeLive() is what
    //        promises the engine that a PMOVE's shadow counts nothing
    //        (POM68K_VENDOR.md row 32) — so a live pipe takes the old path.
    //  (ii)  no watchpoint is armed. pomJitFetch repeats this test; it is
    //        stated here because the fused path skips two mmuFetchWord bodies.
    //  (iii) the window covers all FOUR bytes. Were only two covered, the
    //        second word would reach read16 — which can clear the ROM overlay
    //        or flush a device's ticks — between the two i-cache charges, and
    //        folding them would stop being exact.
    // What the fused path reproduces byte for byte: the overlay for BOTH
    // words in address order (fetches += 2, and word 1's miss penalty charged
    // before word 2 is looked up — legal only because no bus access, no SYNC
    // (empty on Core::C68020) and no POLL_IPL separates them on this path),
    // and the in-flight access context the SECOND mmuFetchWord would have
    // left, which is the only one extBusError() can ever read back.
    if (!pomMmuPipeLive() && !(flags & State::CHECK_WP)) {
        if (pomInterpWindowOn && !pomJitCovers(reg.pc)) [[unlikely]]
            pomInterpArm(reg.pc);                       // POM68K: interpreter-owned window
        if (const u8 *p; pomJitFetch(reg.pc, 4, p)) {
            pomIcacheFetch<2>(reg.pc);
            mmuAccAddr  = reg.pc + 2;
            mmuAccSsw   = 0x0020;
            mmuAccFc    = u8((reg.sr.s ? 4 : 0) | 2);
            mmuAccWrite = false;
            queue.ird = u16(u16(p[0]) << 8 | p[1]);
            queue.irc = u16(u16(p[2]) << 8 | p[3]);
            mmuOpcodeV = queue.ird;
            return true;
        }
    }

#endif

    try {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [B] MC68030UM-1989 §9.5.4: table-search descriptor updates.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Bus-level translation-table search — WinUAE mmu030_table_search with
// level == 0: FCL lookup, short/long descriptors, limit checks, early
// termination, indirection, U on every traversed descriptor and U+M on
// the page descriptor (§ 9.5.4, skipped on supervisor violation).
// Returns MMUSR-style status bits; pageAddr/ci describe the page.
u16
Moira::mmuBusWalk(u32 addr, u8 fc, bool write, u32 &pageAddr, bool &ci)
{
    // The search, with a host /BERR on a descriptor fetch turned into
    // MMUSR B|I on top of what the search had accumulated (Moira.h
    // § mmuInTableSearch). The ATC entry built from it carries B
    // (mmuAtcFill: busError), and the retried access faults through it on
    // its own logical address — the exception "on the retry of the cycle".
    u16 status = 0;
    ci = false;
    pageAddr = 0;
    mmuInTableSearch = true;
    try {
        mmuBusWalkBody(addr, fc, write, pageAddr, ci, status);
    } catch (MmuTableSearchBusError &) {
        status |= 0x8400;                               // B | I
    } catch (...) {
        mmuInTableSearch = false;
        throw;
    }
    mmuInTableSearch = false;
    return status;
}

void
Moira::mmuBusWalkBody(u32 addr, u8 fc, bool write, u32 &pageAddr, bool &ci,
                      u16 &status)
{
    const u32 tc = reg.tc;

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    const bool super = (fc & 4) != 0;
    bool superViolation = false, writeProtected = false;
    bool earlyTermination = false;
    int t = 0;
    int descrNum = 0;
    u32 descrAddr = 0;

    // [B] MC68030UM-1989 §9.7.1: root pointer.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Root pointer (SRP when SRE and supervisor FC, § 9.7.1); reserved
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

    // [B] MC68030UM-1989 §9.5.5.1: function-code lookup.
    // Function code lookup (§ 9.5.5.1)
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

            // [B] MC68030UM-1989 §9.5.1.1: limit fields.
            // Limit check on long descriptors (§ 9.5.1.1)
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

    // [B] MC68030UM-1989 §9.5.3.2: indirection.
    // Indirect descriptor on the last populated level (§ 9.5.3.2)
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
        // [B] MC68030UM-1989 §9.5.3.1: early-termination address bits.
        // Unused index bits are added to the page address (§ 9.5.3.1)
        u32 unused = 0;
        for (int i = t; i <= lastTable; i++) unused |= tmask[i];
        pageAddr = addr & unused;
    }

    pageAddr += (descrSize == 4 ? descr0 : descr1) & 0xFFFFFF00;

stopSearch:

    return;
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
            pomJitAtcEvict(e.logical & imask, ~imask + 1, (fc & 3) == 2);   // POM68K J3b
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
        pomJitAtcEvict(e.logical & imask, ~imask + 1, (fc & 3) == 2);       // POM68K J3b
    }
    return -1;
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    // POM68K J3b (Moira.h § pomJitAtcEvict): whatever the JIT derived from
    // the replaced entry dies with it — same U-bit argument as the 040.
    if (e.valid && (e.logical != (addr & imask) || e.fc != fc))
        pomJitAtcEvict(e.logical, ~imask + 1, (e.fc & 3) == 2);

    e.logical = addr & imask;
    e.fc = fc;
    e.valid = true;
    e.physical = page & imask;
    e.busError = (status & 0xA400) != 0;        // B | INVALID | SUPER_VIOLATION
    e.cacheInhibit = ci;
    e.modified = (status & 0x0200) != 0;
    e.writeProtect = (status & 0x0800) != 0;
}

// [B] MC68030UM-1989 §9.8: PFLUSH.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// ATC flushes (§ 9.8 PFLUSH variants; WinUAE mmu030_flush_atc_*)
void
Moira::mmuAtcFlushAll()
{
    pomJitMapMoved();               // POM68K JIT (030 window)
    for (auto &e : mmuAtcArr) e.valid = false;
}

void
Moira::mmuAtcFlushFc(u32 fcBase, u32 fcMask)
{
    pomJitMapMoved();               // POM68K JIT (030 window)
    for (auto &e : mmuAtcArr)
        if (e.valid && (fcBase & fcMask) == (e.fc & fcMask)) e.valid = false;
}

void
Moira::mmuAtcFlushPage(u32 addr)
{
    pomJitMapMoved();               // POM68K JIT (030 window)
    addr &= ~mmuPageMask();
    for (auto &e : mmuAtcArr)
        if (e.valid && e.logical == addr) e.valid = false;
}

void
Moira::mmuAtcFlushPageFc(u32 addr, u32 fcBase, u32 fcMask)
{
    pomJitMapMoved();               // POM68K JIT (030 window)
    addr &= ~mmuPageMask();
    for (auto &e : mmuAtcArr)
        if (e.valid && (fcBase & fcMask) == (e.fc & fcMask) && e.logical == addr)
            e.valid = false;
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [B] MC68030UM-1989 §8.2.1: documented SSW fields; oracle-private bits below.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Fault capture + throw (WinUAE mmu030_page_fault): SSW per § 8.2.1
// (oracle-arbitrated encodings, incl. the double DF bit), pending-fixup
// application, data buffer and MOVEM/EA state snapshots for the frame.
void
Moira::mmuPageFault(u32 addr, bool read, u32 sswFlags, u8 fc)
{
    mmuWb2Status = mmuWb3Status = 0;

    if (fc & 1) {                               // data cycle

        // [B] MC68020UM-1992 §6.2.1: bit 9 is reserved, not a DF mirror.
        // POM68K: retain the oracle-private marker only on the 030 path.
        mmuSsw = mmuPlain020() ? 0x0100 : 0x0300;
        if (!mmuPlain020() && !(mmuState[1] & 0x0100)) {          // not the last write

            for (int i = 0; i < 2; i++) {

                u8 enc = mmuFixupReg[i];
                // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
    if (mmuRmw && (!mmuPlain020() || (fc & 1))) mmuSsw |= 0x0080; // RM: data cycle

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
    // POM68K Phase C: also valid for the plain 68020/EC020 core — the
    // read<>/write<> paths record the in-flight sub-access (MoiraDataflow)
    // so the $B frame carries the true fault address (the Mac LC ROM's
    // 32-bit probe catcher compares it before resuming).
    // A descriptor fetch is not the access: the table search reports it
    // (Moira.h § mmuInTableSearch), before any fault state is touched.
    if (mmuInTableSearch) throw MmuTableSearchBusError{};
    mmuPageFault(mmuAccAddr, !mmuAccWrite, mmuAccSsw, mmuAccFc);
}

// POM68K: the 68000/68010 twin — see the declaration in Moira.h.
void
Moira::extBusError000(u32 addr, bool write)
{
    StackFrame frame { };
    const u16 ird = getIRD();
    const u8 fc = readFC();

    frame.code = u16((ird & 0xFFE0) | fc | (write ? 0 : 0x10));
    frame.addr = addr;
    frame.ird  = ird;
    frame.sr   = getSR();
    frame.pc   = getPC();
    frame.fc   = fc;
    frame.ssw  = fc;

    throw BusError(frame);
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Translated read with 68030 bus splitting (WinUAE mmu030_get_* +
// *_unaligned): every sub-access is translated separately; the SUBACCESS
// flags in mmuState[1] and the data buffer track partial completion.
template <Core C, Size S, Flags F> u32
Moira::mmuRead(u32 addr)
{
    // POM68K: the translated 030/040 path bypasses readM's watchpoint
    // hook — match on the LOGICAL address here so the debugger works
    // under the MMU too (found chasing the Q605 _FP68K trap install).
    if ((flags & State::CHECK_WP)
        && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        pomDidReachWatchpoint(addr & addrMask<C>(), S, false,
                              (fcl & 3) == FC::USER_PROG);
    }

    const bool log = mmuLogging;
    if (log) pomMmuBumpIdx();

    // POM68K O6.9: the RTE of a DF-cleared fault frame marked this very
    // access as software-completed — deliver the frame's data input
    // buffer without a bus cycle (Moira.h mmuRteSubst*)
    if (mmuRteSubstArmed && !mmuRteSubstWrite
        && addr == mmuRteSubstAddr && reg.pc0 == mmuRteSubstPc) {
        mmuRteSubstArmed = false;
        const u32 v = CLIP<S>(mmuRteSubstData);
        if (log) {
            if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = v;
            pomMmuBumpIdxDone();
        }
        return v;
    }

    // POM68K B.2 slice 5 (2026-09-04) — the J3 data window, read side, on
    // the 68030. mmu040Read has consulted pomJitData since J3; mmuRead never
    // did, which is why docs/JIT_BRINGUP.md § C.2 could only record
    // POM68K_DATA_WINDOW as a DEAD path on this family. Moira.h
    // § pomJitData030Ok owns the refusals this core owes on top of
    // pomJitData's own (space, fcSource, mmuRmw, and the knob itself).
    //
    // NATURALLY ALIGNED forms only. The unaligned and page-straddling
    // branches below are the only ones that touch mmuState[1] and
    // mmuDataBuffer, and a fast path may not skip that partial-completion
    // bookkeeping — a later fault in the same instruction stacks it.
    //
    // POLL_IPL is reproduced at ITS SIZE'S position, not at a convenient
    // one: the long path polls BEFORE a byte, AFTER a word and BETWEEN the
    // two halves of an aligned long. Moving it would move interrupt
    // recognition. SYNC(x) expands to nothing on Core::C68020
    // (MoiraMacros.h § SYNC), so no cycle accounting is skipped either.
    //
    // The in-flight access context (mmuAccAddr & co., stamped inside
    // mmuTranslateAccess) is deliberately NOT written on a hit — the same
    // argument as mmu040Read: a hit is plain guest memory behind a resident,
    // permitted translation, so no fault is possible here, and the next slow
    // access stamps its own before extBusError() can read it. Exactness is
    // inherited from J3b: pomJitAtcEvict kills the derived slice on every
    // 030 eviction site (mmuAtcLookup, mmuAtcFill), so a hit implies the
    // interpreter's own ATC would have hit and no descriptor U-bit write is
    // skipped. Gate: jit_lockstep_030_test and its x64/a64 twins, run with
    // fresh seeds under POM68K_DATA_WINDOW=1.
    // (The size guard is a safety net, not a live case: Quad/Extended FPU
    // operands are assembled from Long reads and never reach this funnel.)
#if MOIRA_ENABLE_JIT
    if constexpr (S == Byte || S == Word || S == Long) {
        constexpr u32 alignMask = S == Byte ? 0u : S == Word ? 1u : 3u;
        if (!(addr & alignMask) && pomJitData030Ok()) {
            constexpr int n = S == Byte ? 1 : S == Word ? 2 : 4;
            if (const u8 *p = pomJitData<n, false>(addr)) {
                pomJitData030Hits++;
                u32 w;
                if constexpr (S == Byte) {
                    if (F & POLL) POLL_IPL;
                    w = p[0];
                } else if constexpr (S == Word) {
                    w = u32(p[0]) << 8 | p[1];
                    if (F & POLL) POLL_IPL;
                } else {
                    w = u32(p[0]) << 24 | u32(p[1]) << 16;
                    if (F & POLL) POLL_IPL;
                    w |= u32(p[2]) << 8 | p[3];
                }
                if (log) {
                    if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = w;
                    pomMmuBumpIdxDone();
                }
                return w;
            }
            pomJitData030Refusals++;
        }
    }

#endif

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
        pomMmuBumpIdxDone();
    }
    return v;
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Translated write, same structure (WinUAE mmu030_put_* + *_unaligned);
// the data buffer holds the full pending value (ACCESS_CHECK_PUT), so a
// faulting write stacks it as wb3_data.
template <Core C, Size S, Flags F> void
Moira::mmuWrite(u32 addr, u32 val)
{
    // POM68K: logical-address watchpoint hook (see mmuRead above).
    if ((flags & State::CHECK_WP)
        && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        pomDidReachWatchpoint(addr & addrMask<C>(), S, true,
                              (fcl & 3) == FC::USER_PROG);
    }

    const bool log = mmuLogging;
    if (log) {
        pomMmuBumpIdx();
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // gencpu passes byte/word operands through signed variables, so
        // the pending-write buffer (wb3_data in fault frames) holds the
        // sign-extended value. MMU_RAWBUF: the caller passes what the oracle
        // passes — fpp.c's FScc `cc ? 0xff : 0x00` (unsigned int, $000000FF)
        // and gencpu's ADD/SUB `uae_u32 newv = (uN)dst ± (uN)src`, the
        // unmasked 32-bit sum or difference.
        mmuDataBuffer = (F & MMU_RAWBUF) ? val : u32(SEXT<S>(val));
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O6.9: DF-cleared retried write — already done, skip the
    // bus cycle (WinUAE keeps the wb3_data value logged at fault time)
    if (mmuRteSubstArmed && mmuRteSubstWrite
        && addr == mmuRteSubstAddr && reg.pc0 == mmuRteSubstPc) {
        mmuRteSubstArmed = false;
        if (log) {
            if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = mmuDataBuffer;
            pomMmuBumpIdxDone();
        }
        return;
    }

    // POM68K B.2 slice 5 (2026-09-04) — the J3 data window, write side, on
    // the 68030. Everything mmuRead's twin comment above establishes applies
    // unchanged: aligned forms only (the unaligned branches below own
    // mmuState[1]/mmuDataBuffer), POLL_IPL reproduced at its size's position
    // (before a byte, after a word, between the halves of an aligned long),
    // no in-flight stamp on a hit, refusals in Moira.h § pomJitData030Ok.
    //
    // The write guard is not bypassed: a write entry over a 4 KB slice
    // holding translated code carries a non-zero PomJitDtlbEntry::codeMask,
    // and pomJitDataSlow refuses such an entry outright at level 0 — the
    // level-0 page has no room for the per-slice mask, so this window keeps
    // the older, coarser whole-page rule that generated code has since
    // replaced (Moira.cpp § pomJitDataSlow).
#if MOIRA_ENABLE_JIT
    if constexpr (S == Byte || S == Word || S == Long) {
        constexpr u32 alignMask = S == Byte ? 0u : S == Word ? 1u : 3u;
        if (!(addr & alignMask) && pomJitData030Ok()) {
            constexpr int n = S == Byte ? 1 : S == Word ? 2 : 4;
            if (u8 *p = pomJitData<n, true>(addr)) {
                pomJitData030Hits++;
                if constexpr (S == Byte) {
                    if (F & POLL) POLL_IPL;
                    p[0] = u8(val);
                } else if constexpr (S == Word) {
                    p[0] = u8(val >> 8); p[1] = u8(val);
                    if (F & POLL) POLL_IPL;
                } else {
                    p[0] = u8(val >> 24); p[1] = u8(val >> 16);
                    if (F & POLL) POLL_IPL;
                    p[2] = u8(val >> 8);  p[3] = u8(val);
                }
                if (log) {
                    if (mmuIdxDone < 10) mmuAd[mmuIdxDone] = mmuDataBuffer;
                    pomMmuBumpIdxDone();
                }
                return;
            }
            pomJitData030Refusals++;
        }
    }

#endif

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
        pomMmuBumpIdxDone();
    }
}


// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [F] Retired Musashi comparison: local patched revision unavailable.
// Settled by: a replay with a pinned replacement oracle.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Instruction router for the $F000-$F03F window, replicating WinUAE's
// mmu_op30 (see the header comment for the arbitration record).
template <Core C, Instr I, Mode M, Size S> void
Moira::execPGen(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // PC-relative, immediate and unassigned mode-7 encodings
    // ($F03A-$F03F) are not MMU opcodes at all (WinUAE table68k
    // MMUOP030): Line-F even in user mode (probe P14 analogue).
    if constexpr (M == Mode::DIPC || M == Mode::IXPC || M == Mode::IM ||
                  M == Mode::IP) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

        default:    // [D] @ Hatari e77819f7: formats 101/110/111: silent no-op in WinUAE
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

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    // [B] MC68030UM-1989 §9.8: PFLUSH variants.
    // PFLUSH by FC (mode $10) / by FC + EA (mode $18), § 9.8. The FC
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

    // [B] MC68030UM-1989 §9.8: PFLUSH.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: the ATC is real now (§ 9.8; WinUAE
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

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: real flush (WinUAE mmu030_flush_atc_all)
    mmuAtcFlushAll();

    prefetch<C, POLL>();

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPFlush40(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68040UM-1993 §3.7.2: PFLUSH variants.
    // [B] M68000PRM-1992 PFLUSH, pp. 6-35/6-36: DFC selects FC2.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K Q2/Q8: PFLUSHN/PFLUSH/PFLUSHAN/PFLUSHA (MC68040UM-1993 § 3.7.2),
    // supervisor-only (WinUAE cpuemu_31 op_f500: Exception 8 first).
    // Mode field bits 4:3 select the variant (MoiraDasmMMU_cpp.h).
    SUPERVISOR_MODE_ONLY

    const u8 mode = ___________xx___(opcode);
    const u8 an = _____________xxx(opcode);
    switch (mode) {
    case 0:                                         // PFLUSHN (An)
        mmu040AtcFlushPage(readA(an), true, (reg.dfc & 4) != 0);
        break;
    case 1:                                         // PFLUSH (An)
        mmu040AtcFlushPage(readA(an), false, (reg.dfc & 4) != 0);
        break;
    case 2:                                         // PFLUSHAN
        mmu040AtcFlushNonGlobal();
        break;
    default:                                        // PFLUSHA
        mmu040AtcFlushAll();
        break;
    }

    prefetch<C, POLL>();

    CYCLES_68020(4)

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execPLoad(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68030UM-1989 §9.8: PLOAD.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // MC68030UM-1989 § 9.8 PLOAD: table search + ATC fill. The
    // search's history updates (U, and U+M for PLOADW, § 9.5.4) land in
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

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    // [B] MC68030UM-1989 §9.8 and §9.7.5.3: PMOVE/configuration exception.
    // [F] Retired Musashi comparison: local patched revision unavailable.
    // Settled by: a replay with a pinned replacement oracle.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // MC68030UM-1989 § 9.8 PMOVE: move to/from TT0/TT1, TC (32-bit),
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

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K O4 slice 3: the register transfer itself is not part of the
    // instruction-restart access log (WinUAE x_get_long, non-state)
    mmuLogging = false;

    switch (modes >> 10 & 0x1f) {       // [D] @ Hatari e77819f7: fmt low bits + preg, WinUAE-style

        case 0x02:  // TT0

            if (rw) writeM<C, M, Long>(ea, reg.tt0);
            else    reg.tt0 = readM<C, M, Long>(ea);
            break;

        case 0x03:  // TT1

            if (rw) writeM<C, M, Long>(ea, reg.tt1);
            else    reg.tt1 = readM<C, M, Long>(ea);
            break;

        case 0x10:  // [B] MC68030UM-1989 §9.7.2: TC. — TC (§ 9.7.2)

            if (rw) {

                writeM<C, M, Long>(ea, reg.tc);

            } else {

                // POM68K O6: the words already in the 030 pipe were
                // fetched under the OLD translation — capture them
                // before the switch (LC II ROM "pmove tc; nop; bne; jmp")
                mmuCapturePipe();
                reg.tc = readM<C, M, Long>(ea);
                // POM68K JIT: TC.E is the switch between "translation off,
                // logical == physical" and a page-table walk, so every
                // cached mapping — the code window, the data TLB and every
                // recorded block — describes the wrong machine the instant
                // it moves. The ATC flushers bump the generation but a
                // PMOVE to TC does not touch the ATC, so it bumps its own.
                // (The 040 twin is covered: setTC040 flushes the ATC.)
                pomJitMapMoved();

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
                pomJitMapMoved();       // POM68K JIT: see the TC case
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
                pomJitMapMoved();       // POM68K JIT: see the TC case
                if ((reg.crp >> 32 & 3) == 0) {
                    execMmuConfigError<C>();
                    trapped = true;
                }
            }
            break;

        case 0x18:  // [B] MC68030UM-1989 §9.7.4 and §9.8: MMUSR/PMOVE. — MMUSR (§ 9.7.4); fd must be clear

            if (fd) {

                execLineF<C, I, M, S>(opcode);
                return;
            }
            if (rw) writeM<C, M, Word>(ea, reg.mmusr);
            else    reg.mmusr = u16(readM<C, M, Word>(ea));
            break;

        default:    // [D] @ Hatari e77819f7: not an MMU register (WinUAE: "Bad PMOVE")

            execLineF<C, I, M, S>(opcode);
            return;
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    // [B] MC68030UM-1989 §9.8: PTEST.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // MC68030UM-1989 § 9.8, PTEST. WinUAE-arbitrated: level 0 searches the
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

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    // [B] MC68040UM-1993 §3.7.3: PTEST table search and ATC effects.
    // [B] M68000PRM-1992 PTEST, pp. 6-70/6-71:
    // flush/refill the ATC selected by DFC, including negative entries.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K Q3: PTESTR/PTESTW (MC68040UM-1993 § 3.7.3; WinUAE mmu_op_real,
    // PTEST branch), supervisor-only. DFC selects the address space
    // (super = dfc&4, data = (dfc&3) != 2); a TTR hit reports T|R (or B
    // when a PTESTW hits a write-protected TTR), otherwise the walk's
    // page descriptor lands in MMUSR040 (0 when invalid; WinUAE
    // mmu_fill_atc status = phys | G|Ux|S|CM|M|W|R).
    // The PRM selects one I/D ATC; WinUAE's mmu_flush_atc flushes both.
    // The manual takes precedence (mmu040_atc_test).
    SUPERVISOR_MODE_ONLY

    int n = _____________xxx(opcode);
    bool write = (opcode & 0x20) == 0;          // $F548 = PTESTW
    u32 addr = readA(n);
    bool super = (reg.dfc & 4) != 0;
    bool data = (reg.dfc & 3) != 2;

    mmu040AtcFlushPage(addr, false, super, data ? 1 : 0);
    int ttr = mmu040MatchTTR(addr, super, data);
    if (ttr) {
        reg.mmusr040 = (ttr == 2 && write) ? 0x800     // MMU_MMUSR_B
                                           : 0x003;    // MMU_MMUSR_T | R
    } else {
        u32 status = 0;
        bool busError = false;
        // A descriptor transfer error reports B without raising the
        // access's format-$7 exception or disturbing its fault stamp.
        mmuInTableSearch = true;
        try {
            status = mmu040Walk<C>(addr, super, write);
        } catch (MmuTableSearchBusError &) {
            busError = true;
        } catch (...) {
            mmuInTableSearch = false;
            throw;
        }
        mmuInTableSearch = false;
        u32 maski = mmu040PageMaskI();
        reg.mmusr040 = busError ? 0x800 : (status & maski)
                     | (status & 0x7F7);               // G|Ux|S|CM|M|W|R
        if (mmu040AtcArmed) mmu040AtcFill(data, addr, status, super);
    }

    prefetch<C, POLL>();

    CYCLES_68020(8)

    FINALIZE
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// -----------------------------------------------------------------------------
// POM68K Q3 — 68040 MMU bus translation, modelled byte-for-byte on the
// primary oracle (WinUAE cpummu.c / cpummu.h mmu040 accessors, hatari
// e77819f7). Q8 adds a separate I/D ATC overlay that preserves U/M/WP
// semantics (write hits on unmodified pages re-walk). POM68K_MMU040_WALK
// restores the walk-per-access baseline for differential checks.
// -----------------------------------------------------------------------------

void
Moira::mmu040AtcFlushAll()
{
    pomJitMapMoved();                 // POM68K JIT: every cached mapping dies
    mmu040AtcCowBoth();
    for (auto &e : mmu040AtcI) e.valid = false;
    for (auto &e : mmu040AtcD) e.valid = false;
    mmu040AtcLastI[0] = mmu040AtcLastI[1] = 0;
    mmu040AtcLastD[0] = mmu040AtcLastD[1] = 0;
}

void
Moira::mmu040AtcFlushNonGlobal()
{
    pomJitMapMoved();                 // POM68K JIT
    mmu040AtcCowBoth();
    for (auto &e : mmu040AtcI)
        if (e.valid && !(e.status & 0x400)) e.valid = false;
    for (auto &e : mmu040AtcD)
        if (e.valid && !(e.status & 0x400)) e.valid = false;
}

void
Moira::mmu040AtcFlushPage(u32 addr, bool nonGlobalOnly, bool super, int space)
{
    pomJitMapMoved();                 // POM68K JIT
    mmu040AtcCowBoth();
    const u32 imask = mmu040PageMaskI();
    addr &= imask;
    auto flush = [&](Mmu040AtcEntry* arr) {
        for (int i = 0; i < MMU040_ATC_ENTRIES; i++) {
            auto &e = arr[i];
            if (!e.valid || e.supervisor != super || (e.logical & imask) != addr) continue;
            if (nonGlobalOnly && (e.status & 0x400)) continue;
            e.valid = false;
        }
    };
    if (space != 1) flush(mmu040AtcI);
    if (space != 0) flush(mmu040AtcD);
}

#if MOIRA_ENABLE_JIT
bool
Moira::pomJitAtcBacked040(u32 addr, bool data, bool super) const
{
    return cpuModel >= Model::M68EC040 && mmu040Enabled() &&
           !mmu040MatchTTR(addr, super, data);
}

// POM68K: exact native accesses are speculative until the instruction owns
// their result. Keep their ATC mutations and debits atomic with that commit;
// a refused native attempt must not choose the following access's victim.
void
Moira::pomJitAtcReplayBegin040(PomJitAtcReplay040& replay, u32 addr,
                              bool data, unsigned access) noexcept
{
    if (cpuModel < Model::M68EC040) return;
    // The thunk's own accesses may fill the I-cache (a JSR target, a
    // program-space read): Moira counted this instruction's stream words
    // before them, so commit the staged count first (MoiraCache040.h).
    if (replay.cacheFetches) {
        if (!replay.cacheSaved) { replay.cacheRot = pomCache040I.rot; replay.cacheSaved = 1; }
        pomCache040I.count(replay.cacheFetches);
        replay.cacheFetches = 0;
    }
    const auto capture = [&](bool side) {
        const u32 bit = 1u << unsigned(side);
        if (replay.active & bit) return;
        // Counter and last-hit indices now; the entries on their first
        // mutation (pomAtcCow040Save).
        auto* dst = replay.opaque + unsigned(side) * PomJitAtcReplay040::SnapshotBytes;
        constexpr auto bytes = sizeof(mmu040AtcD);
        const u32 count = side ? mmu040AtcAccessD : mmu040AtcAccessI;
        std::memcpy(dst + bytes, &count, sizeof(count));
        std::memcpy(dst + bytes + sizeof(count),
                    side ? mmu040AtcLastD : mmu040AtcLastI, 2);
        replay.active |= bit;
        pomAtcCow040 = &replay;
    };
    if (replay.fetches) {
        capture(false);
        mmu040AtcAccessI += replay.fetches;
        replay.fetches = 0;
    }
    u32 completed = 0;
    for (unsigned i = 0; i < access && i < PomJitAtcReplay040::Accesses; ++i) {
        completed += (replay.routes >> (i * 8)) & 0xFF;
        replay.routes &= ~(u32(0xFF) << (i * 8));
    }
    if (completed) {
        capture(true);
        mmu040AtcAccessD += completed;
    }
    if (access < PomJitAtcReplay040::Accesses)
        replay.routes &= ~(u32(0xFF) << (access * 8)); // the exact accessor owns this one
    if (pomJitAtcBacked040(addr, data, bool(reg.sr.s))) capture(data);
}

void
Moira::pomAtcCow040Save(bool side) noexcept
{
    PomJitAtcReplay040& replay = *pomAtcCow040;
    // Retired or aborted since Begin: nothing is owed, stop asking.
    if (!(replay.active & 3)) { pomAtcCow040 = nullptr; return; }
    const u32 begun = 1u << unsigned(side), saved = 4u << unsigned(side);
    if (!(replay.active & begun) || (replay.active & saved)) return;
    std::memcpy(replay.opaque + unsigned(side) * PomJitAtcReplay040::SnapshotBytes,
                side ? mmu040AtcD : mmu040AtcI, sizeof(mmu040AtcD));
    replay.active |= saved;
}

void
Moira::pomJitAtcReplayAbort040(PomJitAtcReplay040& replay) noexcept
{
    for (bool side : {false, true}) {
        if (!(replay.active & (1u << unsigned(side)))) continue;
        const auto* src = replay.opaque + unsigned(side) * PomJitAtcReplay040::SnapshotBytes;
        auto* entries = side ? mmu040AtcD : mmu040AtcI;
        constexpr auto bytes = sizeof(mmu040AtcD);
        if (replay.active & (4u << unsigned(side))) std::memcpy(entries, src, bytes);
        auto& count = side ? mmu040AtcAccessD : mmu040AtcAccessI;
        std::memcpy(&count, src + bytes, sizeof(count));
        std::memcpy(side ? mmu040AtcLastD : mmu040AtcLastI,
                    src + bytes + sizeof(count), 2);
    }
    // A completed thunk may have published a derived mapping before a
    // later operand refused. Restoring only the entries could retain that
    // now-unbacked publication; invalidation is conservative and monotonic.
    if (replay.active) pomJitMapMoved();
    replay.active = replay.fetches = replay.routes = 0;
    // The replayed instruction counts its own stream words again in Moira.
    if (replay.cacheSaved) pomCache040I.rot = replay.cacheRot;
    replay.cacheSaved = 0; replay.cacheFetches = 0;
}

#endif

int
Moira::mmu040AtcLookup(bool data, u32 addr, bool write, bool super)
{
    ++(data ? mmu040AtcAccessD : mmu040AtcAccessI);
    Mmu040AtcEntry* arr = data ? mmu040AtcD : mmu040AtcI;
    i8* last = data ? mmu040AtcLastD : mmu040AtcLastI;
    const u32 imask = mmu040PageMaskI();
    const u32 maddr = addr & imask;
    const unsigned set = mmu040AtcSet(addr);

    {
        auto &e = arr[last[write]];
        if (unsigned(last[write]) >= set && unsigned(last[write]) < set + MMU040_ATC_WAYS &&
            e.valid && e.supervisor == super && (e.logical & imask) == maddr) {
            // [B] MC68040UM-1993 §§3.3/3.5: decide R/W/S permission
            // before re-walking an otherwise permitted M=0 write.
            // A repaired descriptor cannot revoke a cached denial.
            if (!write || !(e.status & 1) || (e.status & 0x10) ||
                (e.status & 4) || (!super && (e.status & 0x80))) {
                return last[write];
            }
            pomAtcCow040Touch(data);
            e.valid = false;
            pomJitAtcEvict(e.logical & imask, ~imask + 1, !data);   // POM68K J3b
        }
    }

    for (unsigned i = set; i < set + MMU040_ATC_WAYS; i++) {
        auto &e = arr[i];
        if (!e.valid || e.supervisor != super || (e.logical & imask) != maddr) continue;
        if (!write || !(e.status & 1) || (e.status & 0x10) ||
            (e.status & 4) || (!super && (e.status & 0x80))) {
            last[write] = i8(i);
            return i;
        }
        pomAtcCow040Touch(data);
        e.valid = false;
        pomJitAtcEvict(e.logical & imask, ~imask + 1, !data);       // POM68K J3b
    }
    return -1;
}

void
Moira::mmu040AtcFill(bool data, u32 addr, u32 status, bool super)
{
    // POM68K M0 probe (docs/CACHE_040.md): histogram the descriptor CM
    // bits Mac OS actually maps, per ATC fill. Diagnostic only, prints at
    // process exit; recorded in POM68K_VENDOR.md. The host arms it through
    // setPomCmStats040() (POM68K_040_CM_STATS in POM68K) — one member test
    // on the fill path, no environment read in the core.
    if (pomCmStats040) {
        struct CmStats {
            unsigned long long n[2][4] {};
            ~CmStats() {
                static const char* kCm[4] =
                    { "writethrough", "copyback", "serial-nc", "nonserial-nc" };
                for (int d = 0; d < 2; d++)
                    for (int c = 0; c < 4; c++)
                        if (n[d][c])
                            std::fprintf(stderr, "[cm040] %s %-12s %llu fills\n",
                                         d ? "data" : "inst", kCm[c], n[d][c]);
            }
        };
        static CmStats s;
        s.n[data ? 1 : 0][(status >> 5) & 3]++;
    }
    Mmu040AtcEntry* arr = data ? mmu040AtcD : mmu040AtcI;
    i8* last = data ? mmu040AtcLastD : mmu040AtcLastI;
    const u32 imask = mmu040PageMaskI();

    const unsigned set = mmu040AtcSet(addr);
    unsigned i;
    for (i = set; i < set + MMU040_ATC_WAYS; i++) if (!arr[i].valid) break;
    if (i == set + MMU040_ATC_WAYS)
        i = set + ((data ? mmu040AtcAccessD : mmu040AtcAccessI) & 3);
    // [B] MC68040UM-1993 §3.3: the original access retries after fill.
    // [F] Installation/retry debits after victim selection. The exact clock
    // edge and PTEST phase are unknown. Settled by: real 040 traces.
    ++(data ? mmu040AtcAccessD : mmu040AtcAccessI);

    pomAtcCow040Touch(data);
    auto &e = arr[i];
    // POM68K J3b (Moira.h § pomJitAtcEvict): whatever the JIT derived from
    // the entry being REPLACED dies with it, so a window/TLB hit implies a
    // resident ATC entry and the walk/U-bit pattern stays bit-identical to
    // an engine with no windows at all.
    if (e.valid && ((e.logical & imask) != (addr & imask) ||
                    e.supervisor != super))
        pomJitAtcEvict(e.logical & imask, ~imask + 1, !data);
    e.logical = addr & imask;
    e.physical = status & imask;
    e.status = status;
    e.supervisor = super;
    e.valid = true;
    last[0] = last[1] = i8(i);
}

template <Core C> bool
Moira::mmu040InstrStart()
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

#if MOIRA_ENABLE_JIT
    // POM68K JIT code window (Moira.h § PomJitWindow). Placed AFTER the
    // POLL_IPL above and AFTER the eight per-instruction MMU resets — those
    // feed execMmu040BusError's restart/last-write dichotomy and may never
    // be skipped. Both words come from the window or neither does: serving
    // only `ird` from it would change which page the `irc` lookahead
    // touches for an instruction sitting on a page's last word, and with it
    // the fault frame.
    if (pomInterpWindowOn && !pomJitCovers(reg.pc)) [[unlikely]]
        pomInterpArm(reg.pc);                           // POM68K: interpreter-owned window
    if (const u8 *p; pomJitFetch(reg.pc, 4, p)) {
        queue.ird = u16(u16(p[0]) << 8 | p[1]);
        queue.irc = u16(u16(p[2]) << 8 | p[3]);
        // Keep the fetch descriptor of the two skipped mmu040Read calls.
        // Data accessors, including MOVE16, replace it with their own
        // logical access context before reaching translation or the bus.
        mmu040StampFetch(reg.pc + 2);
        return true;
    }

#endif

    try {

#if MOIRA_ENABLE_JIT
        if (pomJitTimingProbe) pomJitTiming.fetchWords040 += 2;
#endif
        queue.ird = mmu040FetchWord<C>(reg.pc);
        queue.irc = mmu040FetchWord<C>(reg.pc + 2);

    } catch (MmuBusError &) {

        try { execMmu040BusError<C>(); } catch (...) { halt(); }
        return false;
    }

    return true;
}

#if MOIRA_ENABLE_JIT
void
Moira::pomInterpArm(u32 pc)
{
    // jit::Engine::armWindow without its block bookkeeping, decision for
    // decision (src/jit/JitEngine.cpp): no arm while the post-PMOVE fetch
    // pipe lives (its fetches do not count on the i-cache); the probe must
    // confirm the translation without touching anything; the host span must
    // be plain memory; the page and the span bound the window; and an
    // architectural 040 I-cache may need one interpreted fetch to fill the
    // line before the window can expose it. A failed arm backs off 32
    // instructions, as the engine does — probing every instruction is what
    // made the 030 machines slower under the JIT than interpreted.
    if (pomInterpBackoff > 0) { pomInterpBackoff--; return; }
    if (pomMmuPipeLive()) [[unlikely]] { pomJitDisarm(); return; }

    const bool super = pomJitSuper();
    u32 phys = 0, pageBase = 0, pageLen = 0;
    if (!pomJitProbeCode(pc, super, phys, pageBase, pageLen)) {
        pomJitDisarm(); pomInterpBackoff = 32; return;
    }
    const u32 offset = pc - pageBase;
    const u32 physPage = phys - offset;

    u32 span = 0;
    const u8 *host = pomCodeSpanFn(pomCodeSpanCtx, physPage, span);
    constexpr u32 kMinWindow = 4;
    if (!host || span < kMinWindow) { pomJitDisarm(); pomInterpBackoff = 32; return; }

    const u32 len = pageLen < span ? pageLen : span;
    if (len < kMinWindow || pc - pageBase > len - kMinWindow) {
        pomJitDisarm(); pomInterpBackoff = 32; return;
    }

    pomJitArm(host, pageBase, physPage, len, super);
    if (!pomJitCovers(pc)) pomJitDisarm();
}

bool
Moira::pomJitProbeCode(u32 logical, bool super,
                       u32 &phys, u32 &pageBase, u32 &pageLen) const
{
    // POM68K JIT (src/jit/POM68K_JIT.md § 4). A PROBE, not a translation:
    // it must not touch guest memory, must not throw, and must not disturb
    // any CPU state, because it runs BETWEEN instructions on behalf of the
    // engine. That rules out mmu040Walk — a walk writes the descriptor U/M
    // bits back through mmuWrite32 (see mmu040Walk below) and faults by
    // throwing. On a miss the engine simply does not arm the window: the
    // interpreter then fetches normally, fills the ATC, and the next probe
    // succeeds.
    //
    // Plain-020 branch (2026-07-28): no MMU at all, so translation is
    // identity and the only question is the machine's — codeSpan decides
    // what is plain memory. Any page granularity works for a window bound;
    // 4 KB matches the rest of the machinery. No supervisor-only pages
    // without an MMU, so privilege never refuses (the window still re-arms
    // on privilege change via pomJitCovers' super check, harmlessly).
    if (cpuModel == Model::M68020 || cpuModel == Model::M68EC020) {
        pageBase = logical & ~4095u;
        pageLen  = 4096;
        phys     = logical;
        return true;
    }

    // 68000/68010 branch (2026-08-06): no MMU either, so translation is
    // identity — with one thing the 020 does not have to say. These cores
    // drive a 24-bit bus, and read<> masks every access with addrMask<C>()
    // while the window is keyed on the UNMASKED pc. A pc above $FFFFFF
    // would therefore name one byte to the interpreter and another to the
    // window, so it is refused outright rather than masked: the window
    // covers only addresses that are already their own bus address. Real
    // Mac code never leaves the low 24 bits, so this refuses nothing that
    // was going to be hot.
    if (cpuModel == Model::M68000 || cpuModel == Model::M68010) {
        if (logical > 0x00FFFFFFu) return false;
        pageBase = logical & ~4095u;
        pageLen  = 4096;
        phys     = logical;
        return true;
    }

    // 030 branch (2026-07-28): same three rules against the 030's own
    // translation model — TT registers (OK-match only), TC.E-off identity,
    // then a read-only scan of the 22-entry fc-tagged ATC. The page size
    // comes from TC and can be anything from 256 bytes to 32 KB; the window
    // machinery is size-agnostic as long as pageBase/pageLen are honest.
    if (cpuModel == Model::M68030) {
        const u8 fc = u8((super ? 4 : 0) | 2);          // program space
        const u32 mask = mmuPageMask();                  // low bits of a page
        pageBase = logical & ~mask;
        pageLen  = mask + 1;

        // POM68K patch 31: both answers below are identity, so they carry
        // an identity bound rather than TC's unprogrammed page size.
        if (mmuMatchTTAccess(logical, fc, false)) {
            pomIdentityProbeBound(logical, pageBase, pageLen);
            phys = logical; return true;
        }
        if (!(reg.tc & 0x80000000)) {
            pomIdentityProbeBound(logical, pageBase, pageLen);
            phys = logical; return true;
        }

        // O(1) first: the interpreter's own last-hit memo. mmuAtcFill sets
        // it to the slot it just filled, so the page whose eviction killed
        // the window is right here by the time the engine re-probes — which
        // is exactly the churn that made the MMU-on 030 machines SLOWER
        // under the JIT than interpreted (the 22-entry scan ran on every
        // re-arm, and evictions forced a re-arm every few dozen
        // instructions).
        {
            const MmuAtcEntry &e = mmuAtcArr[mmuAtcLast[fc & 7][0]];
            if (e.valid && e.fc == fc && e.logical == pageBase) {
                if (e.busError) return false;
                phys = e.physical | (logical & mask);
                return true;
            }
        }
        for (const MmuAtcEntry &e : mmuAtcArr) {
            if (!e.valid || e.fc != fc || e.logical != pageBase) continue;
            if (e.busError) return false;                // invalid / S-only page
            phys = e.physical | (logical & mask);
            return true;
        }
        return false;
    }
    if (cpuModel < Model::M68EC040) return false;

    const u32 maski = mmu040PageMaskI();
    const u32 mmuPageBase = logical & maski;
    pageBase = mmuPageBase;
    pageLen  = ~maski + 1;
    const auto finish = [&](u32 p) {
        phys = p;
        return true;
    };

    // Transparent translation is identity and cannot fault on a read.
    if (mmu040MatchTTR(logical, super, false)) return finish(logical);
    if (!mmu040Enabled()) return finish(logical);
    if (!mmu040AtcArmed) return false;      // POM68K_MMU040_WALK differential mode

    // Read-only scan of the INSTRUCTION ATC. Deliberately not
    // mmu040AtcLookup(): that one updates the pseudo-LRU, and a probe has
    // no business reordering the cache the instruction stream is using.
    // O(1) first, same memo trick as the 030 branch above.
    const unsigned set = mmu040AtcSet(logical);
    {
        const Mmu040AtcEntry &e = mmu040AtcI[mmu040AtcLastI[0]];
        if (unsigned(mmu040AtcLastI[0]) >= set && unsigned(mmu040AtcLastI[0]) < set + MMU040_ATC_WAYS &&
            e.valid && e.supervisor == super && (e.logical & maski) == mmuPageBase) {
            if (!(e.status & 1)) return false;
            if (!super && (e.status & 0x80)) return false;
            return finish(e.physical | (logical & ~maski));
        }
    }
    for (unsigned i = set; i < set + MMU040_ATC_WAYS; ++i) {
        const Mmu040AtcEntry &e = mmu040AtcI[i];
        if (!e.valid || e.supervisor != super || (e.logical & maski) != mmuPageBase) continue;
        if (!(e.status & 1)) return false;                  // invalid descriptor
        if (!super && (e.status & 0x80)) return false;      // supervisor-only page
        return finish(e.physical | (logical & ~maski));
    }
    return false;
}

bool
Moira::pomJitProbeData(u32 logical, bool super, bool write,
                       u32 &phys, u32 &pageBase, u32 &pageLen,
                       bool allowCache040) const
{
    // POM68K J2 (src/jit/POM68K_JIT.md § 8). The data twin of
    // pomJitProbeCode, and held to the same three rules: it must not touch
    // guest memory, must not throw, and must not disturb any CPU state —
    // it runs on behalf of GENERATED code that has no exception frame and
    // no way to undo a side effect.
    //
    // Everything mmu040Translate would do that a probe may not do is a
    // refusal here instead: no page-table walk (mmu040Walk writes the U/M
    // descriptor bits back through mmuWrite32), no ATC replacement-state update,
    // and no fault. A refusal costs nothing but a bail-out to the
    // interpreter, which then does the real thing.

    // 030 branch (2026-08-10, docs/JIT_BRINGUP.md § C.2) — the data-space
    // twin of pomJitProbeCode's 030 branch above, held to the same three
    // rules, plus the two a write has that a read does not.
    //
    // The function code is DATA space (1 user / 5 supervisor) where the code
    // probe uses program space (2 / 6). That is not cosmetic: the 68030 ATC
    // tags every entry with its fc and matches it EXACTLY (mmuAtcArr[].fc),
    // so probing the data side with the program fc would miss every entry
    // and silently refuse everything — a whole engine that looks merely slow.
    if (cpuModel == Model::M68030) {
        const u8 fc = u8((super ? 4 : 0) | 1);          // data space
        const u32 mask = mmuPageMask();
        pageBase = logical & ~mask;
        pageLen  = mask + 1;

        // mmuMatchTTAccess already honours the direction (a read-transparent
        // TT register returns false for a write), so unlike the 040's TTR
        // there is no separate write-protect answer to decode here.
        // POM68K patch 31: identity answers, identity bound (see the code
        // probe above and Moira.h pomIdentityProbeBound).
        if (mmuMatchTTAccess(logical, fc, write)) {
            pomIdentityProbeBound(logical, pageBase, pageLen);
            phys = logical; return true;
        }
        if (!(reg.tc & 0x80000000)) {
            pomIdentityProbeBound(logical, pageBase, pageLen);
            phys = logical; return true;
        }

        // Read-only scan, O(1) first through the interpreter's own last-hit
        // memo — same argument as the code probe: eviction churn re-probes
        // constantly, and the 22-entry scan on every one of them is what
        // made the MMU-on 030 machines slower under the engine than
        // interpreted.
        const auto usable = [&](const MmuAtcEntry &e) -> int {
            if (!e.valid || e.fc != fc || e.logical != pageBase) return -1;
            if (e.busError) return 0;                   // invalid / S-only
            if (write) {
                if (e.writeProtect) return 0;
                // An unmodified page owes its descriptor an M bit on the
                // first write, and setting it is a guest-memory STORE
                // (mmuTranslateAccess does it through mmuWrite32). A probe
                // may not perform one, so refuse and let the interpreter.
                if (!e.modified) return 0;
            }
            return 1;
        };
        {
            const MmuAtcEntry &e = mmuAtcArr[mmuAtcLast[fc & 7][0]];
            const int v = usable(e);
            if (v == 0) return false;
            if (v > 0) { phys = e.physical | (logical & mask); return true; }
        }
        for (const MmuAtcEntry &e : mmuAtcArr) {
            const int v = usable(e);
            if (v == 0) return false;
            if (v > 0) { phys = e.physical | (logical & mask); return true; }
        }
        return false;
    }

    if (cpuModel < Model::M68EC040) return false;
    // The ordinary data TLB must never route around the D-cache. The J4
    // publisher is the sole exception: after an exact access has completed
    // it needs a read-only ATC lookup to associate that logical line with
    // the physical Cache040::Line that now contains the authoritative data.
    if (pomCache040On && !allowCache040) return false;

    const u32 maski = mmu040PageMaskI();
    pageBase = logical & maski;
    pageLen  = ~maski + 1;

    // Transparent translation is identity. A TTR match of 2 means the
    // region is write-protected — a write there must fault, so refuse it.
    if (const int ttr = mmu040MatchTTR(logical, super, true)) {
        if (write && ttr == 2) return false;
        phys = logical;
        return true;
    }
    if (!mmu040Enabled()) { phys = logical; return true; }
    if (!mmu040AtcArmed) return false;      // POM68K_MMU040_WALK differential mode

    const unsigned set = mmu040AtcSet(logical);
    for (unsigned i = set; i < set + MMU040_ATC_WAYS; ++i) {
        const Mmu040AtcEntry &e = mmu040AtcD[i];
        if (!e.valid || e.supervisor != super || (e.logical & maski) != pageBase) continue;
        if (!(e.status & 1)) return false;                  // invalid descriptor
        if (!super && (e.status & 0x80)) return false;      // supervisor-only page
        if (write) {
            if (e.status & 4) return false;                 // write-protected
            // An unmodified page owes the descriptor an M-bit write-back on
            // the first write (mmu040AtcLookup invalidates the entry and
            // re-walks for exactly this). That write-back is a guest-memory
            // store; refuse and let the interpreter perform it.
            if (!(e.status & 0x10)) return false;
        }
        phys = e.physical | (logical & ~maski);
        return true;
    }
    return false;
}

void
Moira::pomJitCache040Publish(u32 logical, int bytes, bool write)
{
    // A non-zero hit charge has to pass through sync(), where the machine
    // catches peripherals up. Generated line loads intentionally contain no
    // call, so do not publish under that timing policy.
    if (!pomCache040Active() || !(reg.cacr & 0x80000000) ||
        pomCache040Timing.hit != 0 || bytes <= 0 || bytes > 16 ||
        unsigned(logical & 15) > 16u - unsigned(bytes)) return;

    // The exact access immediately preceding this call has filled/hit the
    // data ATC. Re-probe without changing replacement state or guest descriptors;
    // this is association only, never an access in its own right.
    u32 phys = 0, pageBase = 0, pageLen = 0;
    if (!pomJitProbeData(logical, bool(reg.sr.s), write,
                         phys, pageBase, pageLen, true)) return;
    (void)pageBase; (void)pageLen;
    if (unsigned(phys & 15) > 16u - unsigned(bytes)) return;

    // Keep the resident entry identity for FC2 diagnostics and publish the
    // provenance used to debit the ATC counter at native retirement.
    u8 atcIndex = 0xFF;
    if (!mmu040MatchTTR(logical, bool(reg.sr.s), true) && mmu040Enabled()) {
        const int idx = int(mmu040AtcLastD[write ? 1 : 0]);
        const unsigned set = mmu040AtcSet(logical);
        if (idx < int(set) || idx >= int(set + MMU040_ATC_WAYS)) return;
        const Mmu040AtcEntry &atc = mmu040AtcD[idx];
        if (!atc.valid || atc.supervisor != bool(reg.sr.s) ||
            (atc.logical & mmu040PageMaskI()) !=
                          (logical & mmu040PageMaskI())) return;
        atcIndex = u8(idx);
    }

    Cache040::Line *line = pomCache040D.lookup(phys);
    if (!line) return;                   // disabled/NC/WT write miss

    const u32 logicalTag = pomJitCache040Tag(logical, bool(reg.sr.s));
    const u32 slot = (logical >> 4) & (PomJitCache040Table::kEntries - 1);
    const auto publish = [&](PomJitCache040Entry &e) {
        e.tag = logicalTag;
        e.physicalTag = line->tag;
        e.generation = pomJitCache040Gen;
        e.atcIndex = atcIndex;
        e.atc040 = atcIndex != 0xFF;
        e.line = line;
    };
    publish(pomJitCache040R.e[slot]);

    // A write entry is stronger than a read entry: the exact access proved
    // both write permission/M-bit state and copyback policy. `pomCache040Cm`
    // is the attribute captured by that access's translation, so an alias
    // through a write-through mapping can never inherit permission merely
    // because the physical line happens to be dirty through another alias.
    if (write && pomCache040Cm == Cache040::CM_COPYBACK) {
        const unsigned first = unsigned(phys & 15) >> 2;
        const unsigned last = unsigned((phys & 15) + u32(bytes) - 1) >> 2;
        const u8 dirty = u8((1u << first) | (1u << last));
        // This is also the publication proof: an exact copyback store must
        // have made every covered longword authoritative in the line before
        // generated code is allowed to continue the sequence natively.
        if ((line->dirty & dirty) == dirty)
            publish(pomJitCache040W.e[slot]);
    }
}

Moira::PomJitLayout
Moira::pomJitLayout() const
{
    // Offsets are taken from a LIVE object rather than with offsetof(),
    // which is only conditionally supported on a polymorphic type. The
    // backend addresses everything off the `Moira*` it is handed, so these
    // are the offsets it needs — not the ones a derived machine class
    // would compute.
    const auto at = [this](const void *p) {
        return u32(uintptr_t(p) - uintptr_t(this));
    };
    PomJitLayout l{};
    l.d = at(&reg.d[0]);      l.a = at(&reg.a[0]);
    l.pc = at(&reg.pc);       l.pc0 = at(&reg.pc0);
    l.srT1 = at(&reg.sr.t1);  l.srT0 = at(&reg.sr.t0);
    l.srS = at(&reg.sr.s);    l.srM = at(&reg.sr.m);
    l.srX = at(&reg.sr.x);    l.srN = at(&reg.sr.n);
    l.srZ = at(&reg.sr.z);    l.srV = at(&reg.sr.v);
    l.srC = at(&reg.sr.c);    l.srIpl = at(&reg.sr.ipl);
    l.regIpl = at(&reg.ipl);  l.iplPin = at(&ipl);
    l.clock = at(&clock);     l.flags = at(&flags);
    l.ird = at(&queue.ird);   l.irc = at(&queue.irc);
    l.dtlbR = at(&pomJitDtlbR.e[0]);       l.dtlbW = at(&pomJitDtlbW.e[0]);
    l.cache040R = at(&pomJitCache040R.e[0]);
    l.cache040W = at(&pomJitCache040W.e[0]);
    l.cache040Gen = at(&pomJitCache040Gen);
    l.cache040Hits = at(&pomCache040D.hits);
    l.cache040IRot = at(&pomCache040I.rot);
    l.cache040DRot = at(&pomCache040D.rot);
    l.cache040NativeReadHits = at(&pomJitCache040NativeReadHits);
    l.cache040NativeWriteHits = at(&pomJitCache040NativeWriteHits);
    l.cache040Live = pomCache040Active();
    l.atcWindow040 = at(&pomJitWindow.atc040);
    l.atcAccessI040 = at(&mmu040AtcAccessI);
    l.atcAccessD040 = at(&mmu040AtcAccessD);
    l.movemArmed = at(&mmu040MovemArmed);
    l.cacr = at(&reg.cacr);
    l.icTag = at(&pomIcache.tag[0]);       l.icValid = at(&pomIcache.valid[0]);
    l.icFetches = at(&pomIcache.fetches);  l.icHits = at(&pomIcache.hits);
    l.icMisses = at(&pomIcache.misses);    l.icPenalty = at(&pomIcache.missPenalty);
    l.icLive = pomIcache.armed && cpuModel == Model::M68030;
    l.is030 = cpuModel == Model::M68030;
    l.mmuRmw = at(&mmuRmw);
    return l;
}

// The access half of the JIT data path, for both MMU generations. These
// perform the REAL access through the SAME entry point the interpreter uses
// for that model, and convert the one thing a JIT frame cannot survive — a
// thrown fault — into `false`. They must not be "an equivalent access": a
// 68030 sent down the 040 path would translate through the wrong ATC, take
// the wrong fault frame and skip the 030's restartable-write bookkeeping.
//
// 030 branch added 2026-08-10 (docs/JIT_BRINGUP.md § C.3). Two things it has
// to establish that the 040 entry point takes as an argument:
//
//   * the FUNCTION CODE. mmuRead/mmuWrite read it back with readFC(), which
//     is (sr.s ? 4 : 0) | fcl — so the data-space fcl has to be set here,
//     exactly as the interpreter's setFC<M>() does before its own access.
//   * `fcSource`. MOVES and the SFC/DFC registers redirect readFC() to an
//     alternate space; that is Kind::Unsafe and cannot appear inside a
//     block, but a stale non-zero fcSource would silently translate this
//     access in the wrong space, so it is refused rather than assumed.
bool
Moira::pomJitReadData(u32 addr, int bytes, u32 &out) noexcept
{
    try {
        if (cpuModel == Model::M68030) {
            if (fcSource != 0) return false;
            setFC(FC::USER_DATA);               // readFC() ORs in sr.s
            switch (bytes) {
                case 1: out = mmuRead<Core::C68020, Byte, 0>(addr); return true;
                case 2: out = mmuRead<Core::C68020, Word, 0>(addr); return true;
                default: out = mmuRead<Core::C68020, Long, 0>(addr); return true;
            }
        }
        switch (bytes) {
            case 1: out = mmu040Read<Core::C68020, Byte>(addr, true); break;
            case 2: out = mmu040Read<Core::C68020, Word>(addr, true); break;
            default: out = mmu040Read<Core::C68020, Long>(addr, true); break;
        }
        pomJitCache040Publish(addr, bytes, false);
        return true;
    } catch (...) {
        return false;
    }
}

bool
Moira::pomJitWriteData(u32 addr, int bytes, u32 val) noexcept
{
    try {
        if (cpuModel == Model::M68030) {
            if (fcSource != 0) return false;
            setFC(FC::USER_DATA);
            switch (bytes) {
                case 1: mmuWrite<Core::C68020, Byte, 0>(addr, val); return true;
                case 2: mmuWrite<Core::C68020, Word, 0>(addr, val); return true;
                default: mmuWrite<Core::C68020, Long, 0>(addr, val); return true;
            }
        }
        switch (bytes) {
            case 1: mmu040Write<Core::C68020, Byte>(addr, val, true); break;
            case 2: mmu040Write<Core::C68020, Word>(addr, val, true); break;
            default: mmu040Write<Core::C68020, Long>(addr, val, true); break;
        }
        // A copyback write miss allocates a line; a write-through hit
        // refreshes one. Publishing it here lets a later sole read use the
        // resident authoritative bytes without ever routing the write itself
        // around dirty-mask and restart bookkeeping.
        pomJitCache040Publish(addr, bytes, true);
        return true;
    } catch (...) {
        return false;
    }
}

// A native JSR/JMP target read (execJsr/execJmp eventually publish the first
// target word in queue.irc). The 030 keeps its explicit mode-5 path; the 040
// uses the same read<PROG> funnel as the interpreted instruction, including
// ITT/ATC/cache/fault behaviour.
bool
Moira::pomJitReadProg(u32 addr, u16 &out) noexcept
{
    try {
        if (fcSource != 0) return false;
        if (cpuModel == Model::M68030) {
            setFC(FC::USER_PROG);
            out = u16(mmuRead<Core::C68020, Word, 0>(addr));
        } else if (cpuModel >= Model::M68EC040) {
            out = u16(read<Core::C68020, AddrSpace::PROG, Word>(addr));
        } else {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Complete the non-restartable half of a native 68040 JSR.
// Generated code may compute the EA (and, for a future full-format
// admission, perform its pointer read) while the instruction is pristine.
// From the first stack mutation onward this helper owns the transaction so
// that the push and I-cache state stand exactly where the interpreter leaves
// them. A target fetch fault is not processed here: the JSR has retired and
// the next instruction start takes it (execJsr).
bool
Moira::pomJitJsr040(u32 instructionPc, u32 returnAddress, u32 target,
                    u16 opcode) noexcept
{
    // The JIT normally relies on a pristine interpreter replay to establish
    // mmu040InstrStart's bookkeeping. This path cannot replay after its
    // push, so establish the same per-instruction state before any access.
    mmu040EffAddr = 0;
    mmu040AccFirst = true;
    mmu040AccSplit = false;
    mmu040LastWrite = false;
    mmu040Moves = -1;
    mmu040Lrmw = false;
    mmuFixupReg[0] = mmuFixupReg[1] = 0;
    mmu040CcrSave = u8(getCCR());

    reg.pc0 = instructionPc;
    reg.pc = returnAddress;
    queue.ird = opcode;

    try {
        // execJsr's exact architectural order. push() deliberately leaves
        // mmu040LastWrite armed: if the following instruction-space access
        // faults, execMmu040BusError stacks the post-push/next-PC frame.
        push<Core::C68020, Long, POLL>(returnAddress);
        reg.pc = target;
        // execJsr: a fault on the target fetch belongs to the next
        // instruction start, not to the retired JSR.
        try {
            queue.irc = u16(read<Core::C68020, AddrSpace::PROG, Word>(target));
        } catch (const MmuBusError &) { }
        prefetch<Core::C68020>();
        return true;
    } catch (const std::exception &exc) {
        processException(exc);
        return false;
    }
}

#endif

int
Moira::mmu040MatchTTR(u32 addr, bool super, bool data, u32 *cm) const
{
    // POM68K: answered from a table of the 512 possible inputs. The test
    // below ran on every access — instruction fetches included — and was
    // 9.9 % of the 68040 interpreter (scratchpad/2026-11-06/
    // INTERP_PROFILE.md); the registers it decodes change on MOVEC only.
    // The table is rebuilt from mmu040MatchTTRPair itself, so the two
    // cannot disagree.
    const u32 ttr0 = data ? reg.dtt0 : reg.itt0;
    const u32 ttr1 = data ? reg.dtt1 : reg.itt1;
    Mmu040TtrTable &tab = mmu040TtrTab[data];
    if (tab.ttr[0] != ttr0 || tab.ttr[1] != ttr1) [[unlikely]]
        mmu040TtrRebuild(tab, ttr0, ttr1);

    const u8 e = tab.entry[super][addr >> 24];
    if (cm && e) *cm = e >> 2;
    return e & 3;
}

void
Moira::mmu040TtrRebuild(Mmu040TtrTable &tab, u32 ttr0, u32 ttr1) const
{
    for (int super = 0; super < 2; super++) {
        for (u32 msb = 0; msb < 256; msb++) {
            u32 cm = 0;
            const int match =
                mmu040MatchTTRPair(ttr0, ttr1, msb << 24, super != 0, &cm);
            tab.entry[super][msb] = u8(match ? (match | cm << 2) : 0);
        }
    }
    tab.ttr[0] = ttr0;
    tab.ttr[1] = ttr1;
}

int
Moira::mmu040MatchTTRPair(u32 ttr0, u32 ttr1, u32 addr, bool super, u32 *cm)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE mmu_do_match_ttr over the dtt/itt pair (mmu_match_ttr)
    const u32 ttrs[2] = { ttr0, ttr1 };

    for (u32 ttr : ttrs) {

        if (!(ttr & 0x8000)) continue;                  // E

        u8 msb  = u8(((addr ^ ttr) & 0xFF000000) >> 24);
        u8 mask = u8((ttr & 0x00FF0000) >> 16);
        if (msb & ~mask) continue;

        if (!(ttr & 0x4000)) {                          // S-field != both
            if ((((ttr >> 13) & 1) != 0) != super) continue;
        }
        if (cm) *cm = (ttr >> 5) & 3;                   // M1: TTR CM field
        return (ttr & 4) ? 2 : 1;                       // W -> match + WP
    }
    return 0;
}

void
Moira::pomCache040Touch(bool data, u32 pa, bool write, int cm, int szCode)
{
    // M2: translation records attributes only. The data-bearing helper is
    // called at the exact bus-access site, so a split/misaligned transfer
    // dirties only the bytes actually transferred and a failed line fill
    // can roll back the new line without guessing from the SSW size.
    (void)pa; (void)write;
    pomCache040Cm = cm;
    pomCache040Move16 = szCode == 3;
    pomCache040Enabled = (reg.cacr & (data ? 0x80000000 : 0x8000)) != 0;

    // [B] MC68040UM-1993 §7.4.1 and §7.4.3: implicitly noncacheable
    // byte/word/long read/write transfers include RMW and MOVES accesses,
    // independently of descriptor CM. §4.3.2 also specifies serialization
    // for locked accesses; serialization alone was not the cache-inhibit proof.
    // POM68K: inhibit this access, not the cache controller. An enabled
    // D-cache must still push/invalidate a matching line before bypassing it
    // (MC68040UM-1993 §§4.3.2/7.4.5). Clearing Enabled skipped that step.
    if ((data && mmu040Lrmw) || mmu040Moves >= 0)
        pomCache040Cm = Cache040::CM_NC;
}

namespace {
// POM68K: cache-bus probes defer frame construction until the controller
// can distinguish a fill abort from a physical push and its pending store.
constexpr int kCache040ReadFillProbe = -1, kCache040PushProbe = -2;
struct Cache040ReadFillBusError {};
struct Cache040PushBusError {};
}

void
Moira::pomCache040Writeback(u32 base, const Cache040::Line &line)
{
    // [B] MC68040UM-1993 §8.4.6.4: only a push fault uses a physical FA;
    // the operand that follows a successful push still has its own logical
    // address, size and direction. POM68K: scope the temporary bus stamp.
    const u32 savedAddress = mmu040AccAddr, savedValue = mmu040AccVal;
    const int savedSize = mmu040AccSz;
    const bool savedWrite = mmu040AccWrite, savedData = mmu040AccData;
    // [B] MC68040UM-1993 §4.6.2: two or more dirty longwords require
    // a whole-line push; one dirty longword uses a longword transfer.
    assert(line.dirty);
    const bool fullLine = (line.dirty & (line.dirty - 1)) != 0;
    const int first = fullLine ? 0 : (line.dirty & 1) ? 0 : (line.dirty & 2) ? 1 : (line.dirty & 4) ? 2 : 3;
    try {
        for (int lw = first; lw < (fullLine ? 4 : first + 1); lw++) {
            const int o = 4 * lw;
            const u32 a = base + u32(o);
            const u32 value = u32(line.data[o]) << 24 | u32(line.data[o + 1]) << 16 |
                              u32(line.data[o + 2]) << 8 | line.data[o + 3];
            mmu040AccAddr = a; mmu040AccVal = value;
            mmu040AccSz = kCache040PushProbe; mmu040AccWrite = true; mmu040AccData = true;
            write16(a, u16(value >> 16)); write16(a + 2, u16(value));
            if (pomCache040Timing.pushLong > 0) sync(pomCache040Timing.pushLong);
        }
    } catch (const Cache040PushBusError&) {
        mmu040AccSz = savedSize;
        const bool super = mmu040Moves >= 0 ? (mmu040Moves & 4) != 0 : bool(reg.sr.s);
        const int fc = (super ? 4 : 0) | (savedData ? 1 : 2);
        try {
            // Preserve the caller's restart/continuation and pending WB2/3
            // policy before replacing only the faulted bus transaction.
            mmu040Fault<Core::C68020>(savedAddress, savedValue, fc, savedWrite, savedSize, false);
        } catch (const MmuBusError&) {
            // [B] MC68040UM-1993 §§8.4.6.2/8.4.6.4-8.4.6.6, Table 8-6:
            // push = write, TM/TT=0, physical FA=WB1A, no WB1 valid bit,
            // and all four PD words, even for a single-longword push.
            // Keep continuation/LK, not the caller's RW/ATC/MA/size/space.
            // POM68K: WB3Address remains the independent logical store.
            mmu040Ssw = u16((mmu040Ssw & 0xF200) | (fullLine ? 0x60 : 0));
            mmu040FaultAddr = base + 4 * u32(first);
            for (int lw = 0; lw < 4; lw++) {
                const int o = 4 * lw;
                mmu040Move16[lw] = u32(line.data[o]) << 24 | u32(line.data[o + 1]) << 16 |
                                   u32(line.data[o + 2]) << 8 | line.data[o + 3];
            }
            throw;
        }
    } catch (...) {
        mmu040AccSz = savedSize;
        throw;
    }
    mmu040AccAddr = savedAddress; mmu040AccVal = savedValue;
    mmu040AccSz = savedSize; mmu040AccWrite = savedWrite; mmu040AccData = savedData;
}

void
Moira::pomCache040Writeback(u32 base, const Cache040::Evicted &line)
{
    Cache040::Line view;
    view.valid = line.valid; view.dirty = line.dirty;
    for (int i = 0; i < 16; i++) view.data[i] = line.data[i];
    pomCache040Writeback(base, view);
}

Cache040::Line *
Moira::pomCache040Fill(Cache040 &cache, u32 pa, int readBytes,
                      Cache040::Line *partial)
{
    const bool data = &cache == &pomCache040D;
    if (Cache040::Line *hit = cache.lookup(pa)) {
        cache.hits++;
        if (pomCache040Timing.hit > 0) sync(pomCache040Timing.hit);
        return hit;
    }

    Cache040::Evicted evicted;
    Cache040::Line *line = cache.allocate(pa, &evicted);
    const u32 base = pa & ~u32(15);
    const int originalSize = mmu040AccSz;
    const int first = int((pa >> 2) & 3);
    const int last = readBytes ? int(((pa & 15) + readBytes - 1) >> 2) : first;
    bool rolledBack = false, readComplete = false;
    auto rollback = [&] {
        if (rolledBack) return;
        rolledBack = true;
        cache.invalidateLine(base);
        if (evicted.valid && evicted.dirty) {
            Cache040::Line *old = cache.allocate(evicted.base);
            old->dirty = evicted.dirty;
            for (int i = 0; i < 16; i++) old->data[i] = evicted.data[i];
        }
    };
    try {
        for (int beat = 0; beat < 4; beat++) {
            const int lw = (first + beat) & 3;
            const int o = 4 * lw;
            const u32 a = base + u32(o);
            u16 hi, lo;
            // [B] MC68040UM-1993 §§4.6.1/7.6.1: abort an unused read
            // beat without an architectural exception. §§8.4.6.2/
            // 8.4.6.4 retain the original size and logical initial FA.
            // POM68K: probe before touching frame, continuation or pending
            // write state; a successful fill never replaces the caller stamp.
            mmu040AccSz = kCache040ReadFillProbe;
            try {
                hi = read16(a); lo = read16(a + 2);
            } catch (const Cache040ReadFillBusError&) {
                mmu040AccSz = originalSize;
                if (readBytes) {
                    assert(partial);
                    *partial = *line; partial->valid = false;
                }
                rollback();
                if (!readBytes || (lw >= first && lw <= last)) {
                    const bool super = mmu040Moves >= 0 ? (mmu040Moves & 4) != 0 : bool(reg.sr.s);
                    const int fc = (super ? 4 : 0) | (data ? 1 : 2);
                    try {
                        mmu040Fault<Core::C68020>(mmu040AccAddr, 0, fc, false, originalSize, false);
                    } catch (const MmuBusError&) {
                        if (!readBytes) {
                            // [B] MC68040UM-1993 §8.4.6/Table 8-6: a
                            // read error has no WB1/WB2; WB3 may hold an
                            // outstanding store, with register-aligned data.
                            // [F] The synchronous write-miss model puts its
                            // pending store in WB3 and conservatively faults
                            // every failed allocation beat. Exact stage and
                            // unused-beat delivery need a real 040 capture.
                            // Settled by: write-miss beat faults on silicon.
                            // Gate: cache040_test, software WB3/RTE completion.
                            mmu040Wb3Status = u16(0x80 | (mmu040Ssw & 0x7F));
                            const u32 mask = originalSize == 0 ? 0xFF : originalSize == 1 ? 0xFFFF : ~u32(0);
                            mmu040Wb3Data = mmu040FullVal & mask;
                        }
                        throw;
                    }
                }
                if (pomCache040Timing.lineFill > 0) sync(pomCache040Timing.lineFill);
                return partial; // requested bytes survive in the caller's line buffer
            } catch (...) {
                mmu040AccSz = originalSize;
                throw;
            }
            mmu040AccSz = originalSize;
            line->data[o] = u8(hi >> 8); line->data[o + 1] = u8(hi);
            line->data[o + 2] = u8(lo >> 8); line->data[o + 3] = u8(lo);
        }
        // [B] MC68040UM-1993 §4.6.2: a replacement read completes
        // before the saved dirty victim is pushed. An aborted read must
        // restore that victim without prematurely writing it to RAM.
        readComplete = true;
        if (evicted.valid && evicted.dirty)
            pomCache040Writeback(evicted.base, evicted);
        if (pomCache040Timing.lineFill > 0) sync(pomCache040Timing.lineFill);
        return line;
    } catch (...) {
        // [B] MC68040UM-1993 §7.6.1: a failed replacement read restores
        // its dirty victim; a failed push keeps the successfully read line.
        if (!readComplete) rollback();
        throw;
    }
}

void
Moira::pomCache040PushHit(u32 pa)
{
    pomCache040D.drainLine(pa, [&](u32 base, const Cache040::Line &line) {
        pomCache040Writeback(base, line);
    });
}

u32
Moira::pomCache040ReadValue(u32 pa, bool data, int bytes)
{
    Cache040 &cache = data ? pomCache040D : pomCache040I;
    const bool cachable = pomCache040On && pomCache040Enabled &&
        pomCache040Cm < Cache040::CM_SERIAL_NC && !pomCache040Move16;

    if (!cachable) {
        // A cache-inhibited data reference first pushes a dirty alias or
        // invalidates a clean one. MOVE16 follows the same coherency rule.
        if (pomCache040On && pomCache040Enabled && data) {
            Cache040::Line *line = cache.lookup(pa);
            if (line && line->dirty) pomCache040PushHit(pa);
            else if (line) cache.invalidateLine(pa);
        }
        if (bytes == 1) return read8(pa);
        if (bytes == 2) return read16(pa);
        const u32 hi = read16(pa);              // sequenced: high word first
        return hi << 16 | read16(pa + 2);
    }

    u32 result = 0;
    int done = 0;
    while (done < bytes) {
        const u32 a = pa + u32(done);
        const int off = int(a & 15);
        const int chunk = std::min(bytes - done, 16 - off);
        Cache040::Line partial;
        // [B] MC68040UM-1993 §4.1: one count per half-line read (I and D).
        // A miss is an access too: it counts after selecting its victim,
        // whether or not its fill completes.
        const unsigned halves = Cache040::halfLines(u32(off), chunk);
        Cache040::Line *line;
        try { line = pomCache040Fill(cache, a, chunk, &partial); }
        catch (...) { cache.count(halves); throw; }
        cache.count(halves);
        for (int i = 0; i < chunk; i++) result = result << 8 | line->data[off + i];
        done += chunk;
    }
    return result;
}

void
Moira::pomCache040WriteValue(u32 pa, u32 val, bool data, int bytes)
{
    Cache040 &cache = data ? pomCache040D : pomCache040I;
    const bool cachable = pomCache040On && pomCache040Enabled && data &&
        pomCache040Cm < Cache040::CM_SERIAL_NC && !pomCache040Move16;

    if (!cachable) {
        if (pomCache040On && pomCache040Enabled && data) {
            Cache040::Line *line = cache.lookup(pa);
            if (line && line->dirty) pomCache040PushHit(pa);
            else if (line) cache.invalidateLine(pa);
        }
        if (bytes == 1) write8(pa, u8(val));
        else if (bytes == 2) write16(pa, u16(val));
        else { write16(pa, u16(val >> 16)); write16(pa + 2, u16(val)); }
        return;
    }

    // Write-through never allocates on a miss. On a hit it updates the
    // resident copy and also performs the external write below.
    const bool copyback = pomCache040Cm == Cache040::CM_COPYBACK;
    int done = 0;
    while (done < bytes) {
        const u32 a = pa + u32(done);
        Cache040::Line *line = cache.lookup(a);
        if (line) {
            cache.hits++;
            if (pomCache040Timing.hit > 0) sync(pomCache040Timing.hit);
        } else if (copyback) {
            // [B] MC68040UM-1993 §4.1: the copyback line access counts even
            // if its fill aborts.
            try { line = pomCache040Fill(cache, a, 0); }
            catch (...) { cache.count(1); throw; }
        }

        const int off = int(a & 15);
        const int chunk = std::min(bytes - done, 16 - off);
        // [B] MC68040UM-1993 §4.1: one count per line written in copyback.
        if (copyback) cache.count(1);
        if (line) {
            for (int i = 0; i < chunk; i++)
                line->data[off + i] = u8(val >> (8 * (bytes - done - i - 1)));
            if (copyback) {
                for (int i = 0; i < chunk; i++)
                    line->dirty |= u8(1u << ((off + i) >> 2));
            }
        }
        done += chunk;
    }

    if (!copyback) {
        // [B] MC68040UM-1993 §4.1: one count per write-through bus transfer.
        cache.count(Cache040::transfers(pa, bytes));
        if (bytes == 1) write8(pa, u8(val));
        else if (bytes == 2) write16(pa, u16(val));
        else { write16(pa, u16(val >> 16)); write16(pa + 2, u16(val)); }
    }
}

bool
Moira::pomSnoop040Read(u32 pa, u8 *dst, int bytes, bool invalidate)
{
    if (!pomCache040On || !dst || bytes <= 0) return false;
    bool supplied = false;
    for (int i = 0; i < bytes; i++) {
        const u32 a = pa + u32(i);
        Cache040::Line *line = pomCache040D.lookup(a);
        const u8 dirtyBit = u8(1u << ((a & 15) >> 2));
        if (!line || !(line->dirty & dirtyBit)) continue;
        dst[i] = line->data[a & 15];
        supplied = true;
    }
    if (invalidate) {
        const u64 end = u64(pa) + u64(bytes);
        for (u64 a = pa & ~u32(15); a < end; a += 16)
            pomCache040D.invalidateLine(u32(a));
    }
    return supplied;
}

bool
Moira::pomSnoop040Write(u32 pa, const u8 *src, int bytes, bool sink)
{
    if (!pomCache040On || !src || bytes <= 0) return false;
    bool inhibitMemory = false;
    if (!sink || bytes == 16) {
        const u64 end = u64(pa) + u64(bytes);
        for (u64 a = pa & ~u32(15); a < end; a += 16)
            pomCache040D.invalidateLine(u32(a));
        return false;
    }
    for (int i = 0; i < bytes; i++) {
        const u32 a = pa + u32(i);
        Cache040::Line *line = pomCache040D.lookup(a);
        if (!line) continue;
        const u8 dirtyBit = u8(1u << ((a & 15) >> 2));
        const bool dirtyHit = (line->dirty & dirtyBit) != 0;
        if (dirtyHit) inhibitMemory = true;
        const int off = int(a & 15);
        line->data[off] = src[i];
        if (dirtyHit) line->dirty |= dirtyBit;
    }
    return inhibitMemory;
}

void
Moira::pomCacheOp040(u16 opcode)
{
    // [B] MC68040UM-1993 §4.2: CINV/CPUSH use physical operands.
    // CINV/CPUSH acting on the M2 data-bearing model (MC68040UM-1993 § 4.2).
    // Bits 7-6 = caches (01 DC, 10 IC, 11 both, 00 none), bit 5 = push,
    // bits 4-3 = scope (01 line, 10 page, 11 all), bits 2-0 = An.
    // CPUSH writes each dirty longword before invalidating the selected
    // line(s); both instructions act with caches disabled too (UM §4.2).
    const bool push = opcode & 0x20;
    const int scope = (opcode >> 3) & 3;

    // An is already physical: no TTR, ATC lookup or descriptor access.
    const u32 pa = reg.a[opcode & 7];
    const u32 pmask = mmu040PageMaskI();

    auto apply = [&](Cache040 &c, bool data) {
        auto sink = [&](u32 base, const Cache040::Line &line) {
            pomCache040Writeback(base, line);
        };
        switch (scope) {
            case 1:
                if (push && data) (void)c.drainLine(pa, sink);
                else if (push) (void)c.pushLine(pa);
                else c.invalidateLine(pa);
                break;
            case 2:
                if (push && data) (void)c.drainPage(pa, pmask, sink);
                else if (push) (void)c.pushPage(pa, pmask);
                else c.invalidatePage(pa, pmask);
                break;
            default:
                if (push && data) (void)c.drainAll(sink);
                else if (push) (void)c.pushAll();
                else c.invalidateAll();
                break;
        }
    };
    if (opcode & 0x40) apply(pomCache040D, true);
    if (opcode & 0x80) {
        apply(pomCache040I, false);
        // Native blocks embed instruction bytes. An IC invalidate/push is
        // the boundary at which those bytes may architecturally change.
        pomJitMapMoved();
    }
}

template <Core C> u32
Moira::mmu040Walk(u32 addr, bool super, bool write)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
    u32 previousAddress = descAddr, previous = desc;

    descAddr = (desc & 0xFFFFFE00) | ((addr >> 16) & 0x1FC);
    desc = mmuRead32(descAddr);
    // [B] MC68040UM-1993 Figure 3-10: a scheduled table-descriptor U
    // write occurs after the next descriptor has been read successfully.
    // [D] WinUAE cpummu.c:695-742 @ Hatari e77819f7 writes U eagerly.
    // POM68K: a failed next read must leave the prior history untouched.
    if (!(previous & 8)) mmuWrite32(previousAddress, previous | 8);
    if (!(desc & 2)) return 0;                          // invalid pointer
    wp |= desc;
    previousAddress = descAddr; previous = desc;

    if (reg.tc040 & 0x4000) {                           // 8K pages
        descAddr = (desc & 0xFFFFFF80) + ((addr >> 11) & 0x7C);
    } else {                                            // 4K pages
        descAddr = (desc & 0xFFFFFF00) + ((addr >> 10) & 0xFC);
    }
    desc = mmuRead32(descAddr);
    if (!(previous & 8)) mmuWrite32(previousAddress, previous | 8);
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
Moira::mmu040TranslateHot(u32 addr, u32 val, bool super, bool data,
                          bool write, int szCode)
{
    // POM68K: mmu040Translate for the access that neither faults nor
    // walks, with the cache model off — an instruction fetch or a data
    // access answered by a TTR, by a disabled MMU, or by the ATC line the
    // previous access of its kind hit. Line for line the same decisions:
    //   - TTR first; a write to a write-protected match is left to the
    //     slow path, which faults;
    //   - MMU off: identity;
    //   - the last-hit probe of mmu040AtcLookup, with its rule that a
    //     write to a page neither modified nor protected re-walks, then
    //     the permission test of mmu040Translate. The counter advances only
    //     once the access is known to succeed here — on every other
    //     outcome the slow path below starts from an untouched state.
    // 16.6 % + 9.9 % + 9.1 % of the 68040 interpreter were the three
    // out-of-line calls this replaces (INTERP_PROFILE.md, 2026-11-06).
    // Gate: the fixed-cycle bench fingerprint and the 68040 vector corpora.
    if (!pomCache040On && !(mmu040Lrmw && data)) [[likely]] {

        const int ttr = mmu040MatchTTR(addr, super, data);
        if (ttr) {
            if (!(write && ttr == 2)) return addr;
        } else if (!mmu040Enabled()) {
            return addr;
        } else if (mmu040AtcArmed) {
            const u32 imask = mmu040PageMaskI();
            Mmu040AtcEntry *arr = data ? mmu040AtcD : mmu040AtcI;
            const int idx = (data ? mmu040AtcLastD : mmu040AtcLastI)[write];
            const unsigned set = mmu040AtcSet(addr);
            const Mmu040AtcEntry &e = arr[idx];
            const u32 status = e.status;
            if (idx >= int(set) && idx < int(set + MMU040_ATC_WAYS) &&
                e.valid && e.supervisor == super &&
                (e.logical & imask) == (addr & imask) &&
                (!write || (status & 0x10) || (status & 4)) &&
                !((write && (status & 4)) || (!super && (status & 0x80)) ||
                  !(status & 1))) {
                ++(data ? mmu040AtcAccessD : mmu040AtcAccessI);
                return e.physical | (addr & ~imask);
            }
        }
    }
    return mmu040Translate<C>(addr, val, super, data, write, szCode);
}

u32
Moira::mmu040BusRead(u32 pa, bool data, int bytes)
{
    if (!pomCache040On) [[likely]] {
        if (bytes == 1) return read8(pa);
        if (bytes == 2) return read16(pa);
        // High word first, as the bus does it: the two calls of
        // `read16(pa) << 16 | read16(pa + 2)` are unsequenced in C++.
        const u32 hi = read16(pa);
        return hi << 16 | read16(pa + 2);
    }
    return pomCache040ReadValue(pa, data, bytes);
}

template <Core C> u16
Moira::mmu040FetchWord(u32 addr)
{
    // The stamp is the one mmu040Read leaves (mmu040StampFetch says so
    // for the fetch window): an external /BERR on the bus read below, or a
    // fault in the translation, must find it in place.
    const u32 pageMask = ~mmu040PageMaskI();
    if (!(flags & State::CHECK_WP) && !pomCache040On && mmu040Moves < 0 &&
        (addr & pageMask) != pageMask) [[likely]] {
        mmu040StampFetch(addr);
        const u32 pa = mmu040TranslateHot<C>(addr, 0, bool(reg.sr.s), false,
                                             false, 1);
        return u16(read16(pa & addrMask<C>()));
    }
    return u16(mmu040Read<C, Word>(addr, false));
}

void
Moira::mmu040BusWrite(u32 pa, u32 val, bool data, int bytes)
{
    if (!pomCache040On) [[likely]] {
        if (bytes == 1) write8(pa, u8(val));
        else if (bytes == 2) write16(pa, u16(val));
        else { write16(pa, u16(val >> 16)); write16(pa + 2, u16(val)); }
        return;
    }
    pomCache040WriteValue(pa, val, data, bytes);
}

template <Core C> u32
Moira::mmu040Translate(u32 addr, u32 val, bool super, bool data,
                       bool write, int szCode)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE mmu_get/put_* accessor heads: TTR match first (a WP match
    // faults writes even with translation disabled), then the walk.
    // Locked RMW cycles translate as WRITES from the read on (WinUAE
    // mmu_get_user_*: the lrmw accessors pass write = true).
    int fc = (super ? 4 : 0) | (data ? 1 : 2);
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // The locked-RMW write-flavor applies to DATA accesses only — the
    // CAS extension word still fetches through the plain read path
    // (WinUAE wraps only the get_lrmw_* data accessors)
    bool asWrite = write || (mmu040Lrmw && data);

    u32 ttrCm = 0;
    int ttr = mmu040MatchTTR(addr, super, data, &ttrCm);
    if (ttr) {
        if (asWrite && ttr == 2) mmu040Fault<C>(addr, val, fc, asWrite, szCode);
        // POM68K M1: transparent regions cache per the TTR's CM field
        if (pomCache040On) pomCache040Touch(data, addr, asWrite, int(ttrCm), szCode);
        return addr;
    }
    if (!mmu040Enabled()) {
        // [B] MC68040UM-1993 §4.3: default write-through with translation disabled.
        // POM68K M1: translation disabled → default attributes, CM =
        // cachable/writethrough (MC68040UM-1993 § 4.3, translation-disabled default)
        if (pomCache040On) pomCache040Touch(data, addr, asWrite, 0, szCode);
        return addr;
    }

    u32 maski = mmu040PageMaskI();
    u32 status;

    if (mmu040AtcArmed) {
        int hit = mmu040AtcLookup(data, addr, asWrite, super);
        if (hit >= 0) {
            const auto &e = (data ? mmu040AtcD : mmu040AtcI)[hit];
            status = e.status;
            if ((asWrite && (status & 4)) || (!super && (status & 0x80)) ||
                !(status & 1)) {
                mmu040Fault<C>(addr, val, fc, asWrite, szCode);
            }
            const u32 hpa = e.physical | (addr & ~maski);
            if (pomCache040On)
                pomCache040Touch(data, hpa, asWrite, int((status >> 5) & 3),
                                 szCode);
            return hpa;
        }
    }

    // [B] MC68040UM-1993 §3.5 and §8.4.6.2: a descriptor-cycle /BERR
    // creates a nonresident ATC entry and faults the original access.
    // POM68K: unwind before building the frame, with the caller's unchanged
    // logical address/value/size/FC, not the descriptor read/write cycle.
    mmuInTableSearch = true;
    try {
        status = mmu040Walk<C>(addr, super, asWrite);
    } catch (const MmuTableSearchBusError&) {
        mmuInTableSearch = false;
        if (mmu040AtcArmed) mmu040AtcFill(data, addr, 0, super);
        mmu040Fault<C>(addr, val, fc, asWrite, szCode);
        throw; // mmu040Fault always throws
    } catch (...) {
        mmuInTableSearch = false;
        throw;
    }
    mmuInTableSearch = false;

    // [B] MC68040UM-1993 §3.5: create the ATC entry, then retry access
    // through its cached R/W/S attributes, including a failed translation.
    // POM68K: publishing after validation lost cold validity/protection faults.
    if (mmu040AtcArmed) mmu040AtcFill(data, addr, status, super);
    if ((asWrite && (status & 4)) || (!super && (status & 0x80)) ||
        !(status & 1)) {
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // locked-RMW reads fault with WRITE semantics (WinUAE passes
        // write = true through mmu_get_user_*; SSW.LK strips RW anyway)
        mmu040Fault<C>(addr, val, fc, asWrite, szCode);
    }
    const u32 wpa = (status & maski) | (addr & ~maski);
    if (pomCache040On)
        pomCache040Touch(data, wpa, asWrite, int((status >> 5) & 3), szCode);
    return wpa;
}

template <Core C> void
Moira::mmu040Fault(u32 addr, u32 val, int fc, bool write, int szCode,
                   bool atc)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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

    if (atc) ssw |= 0x0400;                             // ATC (MMU fault)

    u32 fa = addr;
    if (!mmu040AccFirst) {                              // split, later part
        fa = mmu040AccBase;                             // misalignednotfirst
        // [B] MC68040UM-1993 §§8.4.6.2/8.4.6.4: FA stays initial for
        // every fault, but MA marks only a second-page ATC fault.
        // [D] WinUAE cpummu.c:989-997 @ Hatari e77819f7 instead sets
        // MA on every later-fragment exception. The primary rule wins.
        // POM68K: keep physical /BERR distinct from translation faults.
        if (atc) ssw |= 0x0800;                          // MA
    }
    if (write && mmu040AccSplit) mmu040Wb3Data = mmu040FullVal;

    mmu040Ssw = ssw;
    mmu040FaultAddr = fa;
    mmu040Wb3Address = fa;

    throw MmuBusError();
}

template <Core C, Size S> u32
Moira::mmu040Read(u32 addr, bool data)
{
    // POM68K: logical-address watchpoint hook (see mmuRead above — the
    // 040 bus path bypasses readM's check just like the 030 one).
    if ((flags & State::CHECK_WP)
        && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        pomDidReachWatchpoint(addr & addrMask<C>(), S, false,
                              !data);
    }

#if MOIRA_ENABLE_JIT
    // JIT trace: one architectural data access, whatever its internal
    // page/alignment split. Its completion count positions any later
    // POLL_IPL (PomJitTiming.iplPollPositions); no poll fires inside
    // this call, so counting on entry equals counting on return.
    if (data && pomJitTimingProbe) [[unlikely]] pomJitTiming.dataAccesses++;

    // POM68K J3 — the data window (Moira.h § pomJitData). A hit is plain
    // guest memory behind a resident, permitted translation: no fault is
    // possible, so the in-flight access context is not stamped — the same
    // contract as the fetch window and the JIT's inline loads, and the
    // next slow access stamps its own before anything can read it. The
    // SYNC(2)s skipped with the long path expand to nothing on the 040.
    if (data && !pomCache040On) [[likely]] {
        constexpr int n = S == Byte ? 1 : S == Word ? 2 : 4;
        if (const u8 *p = pomJitData<n, false>(addr)) {
            if constexpr (S == Byte) return p[0];
            else if constexpr (S == Word) return u16(u16(p[0]) << 8 | p[1]);
            else return u32(p[0]) << 24 | u32(p[1]) << 16 |
                        u32(p[2]) << 8 | p[3];
        }
    }

#endif

    const bool super = mmu040Moves >= 0 ? (mmu040Moves & 4) != 0
                                        : bool(reg.sr.s);
    const u32 pageMask = ~mmu040PageMaskI();

    mmu040AccBase = addr;
    mmu040AccFirst = true;
    mmu040AccSplit = false;
    mmu040AccAddr = addr;                               // extBusError040
    mmu040AccVal = 0;
    mmu040AccSz = S == Byte ? 0 : S == Word ? 1 : 2;
    mmu040AccWrite = false;
    mmu040AccData = data;

    if constexpr (S == Byte) {

        u32 pa = mmu040TranslateHot<C>(addr, 0, super, data, false, 0);
        return mmu040BusRead(pa & addrMask<C>(), data, 1);
    }

    if constexpr (S == Word) {

        if ((addr & pageMask) + 2 > pageMask + 1) {     // page-crossing
            mmu040AccSplit = true;
            u32 hi = mmu040TranslateHot<C>(addr, 0, super, data, false, 1);
            u32 r = mmu040BusRead(hi & addrMask<C>(), data, 1) << 8;
            mmu040AccFirst = false;
            u32 lo = mmu040TranslateHot<C>(addr + 1, 0, super, data, false, 1);
            r |= mmu040BusRead(lo & addrMask<C>(), data, 1);
            return r;
        }
        u32 pa = mmu040TranslateHot<C>(addr, 0, super, data, false, 1);
        return mmu040BusRead(pa & addrMask<C>(), data, 2);
    }

    if constexpr (S == Long) {

        if ((addr & pageMask) + 4 > pageMask + 1) {     // page-crossing
            mmu040AccSplit = true;
            if (!(addr & 1)) {                          // word halves
                u32 hi = mmu040TranslateHot<C>(addr, 0, super, data, false, 2);
                u32 r = mmu040BusRead(hi & addrMask<C>(), data, 2) << 16;
                mmu040AccFirst = false;
                u32 lo = mmu040TranslateHot<C>(addr + 2, 0, super, data, false, 2);
                r |= mmu040BusRead(lo & addrMask<C>(), data, 2);
                return r;
            }
            u32 r = 0;                                  // four bytes
            for (int i = 0; i < 4; i++) {
                u32 pa = mmu040TranslateHot<C>(addr + i, 0, super, data, false, 2);
                r = r << 8 | mmu040BusRead(pa & addrMask<C>(), data, 1);
                mmu040AccFirst = false;
            }
            return r;
        }
        u32 pa = mmu040TranslateHot<C>(addr, 0, super, data, false, 2);
        return mmu040BusRead(pa & addrMask<C>(), data, 4);
    }
}

template <Core C, Size S> void
Moira::mmu040Write(u32 addr, u32 val, bool data)
{
    // POM68K: logical-address watchpoint hook (see mmuRead above).
    if ((flags & State::CHECK_WP)
        && debugger.watchpointMatches(addr & addrMask<C>(), S)) {
        pomDidReachWatchpoint(addr & addrMask<C>(), S, true,
                              !data);
    }

#if MOIRA_ENABLE_JIT
    // JIT trace: see mmu040Read — one data access per call.
    if (data && pomJitTimingProbe) [[unlikely]] pomJitTiming.dataAccesses++;

    // POM68K J3 — the data window, write side. The last-write marker is
    // replicated bit for bit from the long path below: a LATER access in
    // the SAME instruction can still fault, and the format $7 frame stacks
    // this dichotomy — a fast path that skipped it would change which pc a
    // two-write instruction's second fault reports.
    if (data && !pomCache040On) [[likely]] {
        constexpr int n = S == Byte ? 1 : S == Word ? 2 : 4;
        if (u8 *p = pomJitData<n, true>(addr)) {
            if (!mmu040MovemArmed && !mmu040LastWrite) {
                mmu040LastWrite = true;
                mmu040LastWritePc = reg.pc;
            }
            if constexpr (S == Byte) { p[0] = u8(val); }
            else if constexpr (S == Word) { p[0] = u8(val >> 8); p[1] = u8(val); }
            else { p[0] = u8(val >> 24); p[1] = u8(val >> 16);
                   p[2] = u8(val >> 8); p[3] = u8(val); }
            return;
        }
    }

#endif

    const bool super = mmu040Moves >= 0 ? (mmu040Moves & 4) != 0
                                        : bool(reg.sr.s);
    const u32 pageMask = ~mmu040PageMaskI();
#if MOIRA_ENABLE_JIT
    const auto publishJitLine = [&] {
        // Generated AArch64 stores conservatively replay a first cache miss
        // through this exact instruction path. Publish that successful
        // access so the next execution can prove a native copyback hit.
        // No engine means no consumer and no extra read-only ATC probe.
        if (data && pomJitCache040Consumer)
            pomJitCache040Publish(addr, S == Byte ? 1 : S == Word ? 2 : 4,
                                  true);
    };

#endif

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
    mmu040AccAddr = addr;                               // extBusError040
    mmu040AccVal = val;
    mmu040AccSz = S == Byte ? 0 : S == Word ? 1 : 2;
    mmu040AccWrite = true;
    mmu040AccData = data;

    if constexpr (S == Byte) {

        u32 pa = mmu040TranslateHot<C>(addr, val, super, data, true, 0);
        mmu040BusWrite(pa & addrMask<C>(), val, data, 1);
#if MOIRA_ENABLE_JIT
        publishJitLine();
#endif
        return;
    }

    if constexpr (S == Word) {

        if ((addr & pageMask) + 2 > pageMask + 1) {
            mmu040AccSplit = true;
            u32 hi = mmu040TranslateHot<C>(addr, val >> 8, super, data, true, 1);
            mmu040BusWrite(hi & addrMask<C>(), val >> 8, data, 1);
            mmu040AccFirst = false;
            u32 lo = mmu040TranslateHot<C>(addr + 1, val, super, data, true, 1);
            mmu040BusWrite(lo & addrMask<C>(), val, data, 1);
#if MOIRA_ENABLE_JIT
            publishJitLine();
#endif
            return;
        }
        u32 pa = mmu040TranslateHot<C>(addr, val, super, data, true, 1);
        mmu040BusWrite(pa & addrMask<C>(), val, data, 2);
#if MOIRA_ENABLE_JIT
        publishJitLine();
#endif
        return;
    }

    if constexpr (S == Long) {

        if ((addr & pageMask) + 4 > pageMask + 1) {
            mmu040AccSplit = true;
            if (!(addr & 1)) {
                u32 hi = mmu040TranslateHot<C>(addr, val >> 16, super, data, true, 2);
                mmu040BusWrite(hi & addrMask<C>(), val >> 16, data, 2);
                mmu040AccFirst = false;
                u32 lo = mmu040TranslateHot<C>(addr + 2, val, super, data, true, 2);
                mmu040BusWrite(lo & addrMask<C>(), val, data, 2);
#if MOIRA_ENABLE_JIT
                publishJitLine();
#endif
                return;
            }
            for (int i = 0; i < 4; i++) {
                u8 b = u8(val >> (24 - 8 * i));
                u32 pa = mmu040TranslateHot<C>(addr + i, b, super, data, true, 2);
                mmu040BusWrite(pa & addrMask<C>(), b, data, 1);
                mmu040AccFirst = false;
            }
#if MOIRA_ENABLE_JIT
            publishJitLine();
#endif
            return;
        }
        u32 pa = mmu040TranslateHot<C>(addr, val, super, data, true, 2);
        mmu040BusWrite(pa & addrMask<C>(), val, data, 4);
#if MOIRA_ENABLE_JIT
        publishJitLine();
#endif
        return;
    }
}

template <Core C> void
Moira::mmu040GetMove16(u32 addr)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE mmu_get_move16: line-aligned, one translation, four
    // physical longs into the line buffer (frame PD0-3)
    const bool super = reg.sr.s;
    u32 a = addr & ~u32(15);

    // [B] MC68040UM-1993 §§8.4.6.2/8.4.6.4: SIZE=line, TT=MOVE16,
    // data FC and the initial logical line, not the current physical beat.
    // POM68K: never inherit an opcode/window stamp for a data transfer.
    mmu040EffAddr = a;
    mmu040AccBase = a;
    mmu040AccFirst = true;
    mmu040AccSplit = false;
    mmu040AccAddr = a; mmu040AccVal = 0; mmu040AccSz = 3;
    mmu040AccWrite = false; mmu040AccData = true;

    u32 pa = mmu040Translate<C>(a, 0, super, true, false, 3);
    for (int i = 0; i < 4; i++) {
        mmu040Move16[i] = pomCache040ReadValue(
            (pa + 4 * i) & addrMask<C>(), true, 4);
    }
    SYNC(8);
}

template <Core C> void
Moira::mmu040PutMove16(u32 addr)
{
    const bool super = reg.sr.s;
    u32 a = addr & ~u32(15);

    // [B] MC68040UM-1993 §§8.4.6.2/8.4.6.4: a MOVE16 write fault
    // identifies the logical destination line. POM68K: the existing WB2
    // capture also needs that destination, not a stale effective address.
    mmu040EffAddr = a;
    mmu040AccBase = a;
    mmu040AccFirst = true;
    mmu040AccSplit = false;
    mmu040AccAddr = a; mmu040AccVal = mmu040Move16[0]; mmu040AccSz = 3;
    mmu040AccWrite = true; mmu040AccData = true;

    u32 pa = mmu040Translate<C>(a, mmu040Move16[0], super, true, true, 3);
    for (int i = 0; i < 4; i++) {
        pomCache040WriteValue((pa + 4 * i) & addrMask<C>(),
                              mmu040Move16[i], true, 4);
    }
    SYNC(8);
}

void
Moira::extBusError040()
{
    // [B] M68000PRM-1992 PTEST, p. 6-71: a probe reports MMUSR.B.
    // [B] MC68040UM-1993 §8.4.6.2: an ordinary descriptor error faults the
    // original access with ATC. Each caller owns its side-effect-free unwind.
    if (mmuInTableSearch) throw MmuTableSearchBusError{};

    // POM68K: a read-fill beat is not necessarily demanded. Its controller
    // catches this private signal before any architectural fault is built.
    if (mmu040AccSz == kCache040ReadFillProbe) throw Cache040ReadFillBusError{};
    if (mmu040AccSz == kCache040PushProbe) throw Cache040PushBusError{};

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // External /BERR (unmapped I/O): the machine calls this from inside
    // a bus callback; the captured access context builds the same
    // format $7 frame as a translation fault, with SSW.ATC clear
    // (WinUAE mmu_hardware_bus_error -> mmu_bus_error nonmmu = true).
    // A failed M2 line fill rolls its partial allocation back at the fill
    // site before this callback raises the architectural fault.
    bool super = mmu040Moves >= 0 ? (mmu040Moves & 4) != 0 : bool(reg.sr.s);
    int fc = (super ? 4 : 0) | (mmu040AccData ? 1 : 2);
    mmu040Fault<Core::C68020>(mmu040AccAddr, mmu040AccVal, fc,
                              mmu040AccWrite, mmu040AccSz, false);
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
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
    //
    // POM68K: a floating-point instruction never ends on a "last write".
    // [D] WinUAE fpp.c @ Hatari e77819f7 — the FPU stores (FMOVE to memory,
    //     FMOVEM, FSAVE) go through put_fp_value/x_cp_put_*, which gencpu's
    //     gen_set_fault_pc never brackets: a faulting store restarts the
    //     instruction. Decided at fault time from the opcode (cpID 1,
    //     $F200-$F3FF) so the store path carries no extra test.
    // [F] Not checked against a manual or a 68040: MC68040UM-1993 § 8.4 does not
    //     single the FPU stores out. Settled by: a capture of the format $7
    //     frame of a faulting FMOVE.X FPn,(An)+ on a real 68040.
    // Gate: moira_rules_test (FMOVE to a faulting page), fpu corpus replay.
    // [D] WinUAE cpuemu_31.c op_0188/op_01c8 @ Hatari e77819f7:
    // MOVEP stores also keep mmu_restart=true on every byte, including the
    // final one. The handler/RTE must return to the unfinished MOVEP.
    // [F] The generic format-$7 diagram in MC68040UM-1993 §8.4 gives a
    // next-instruction PC, without specifying a partial MOVEP sequence.
    // Settled by: a real 68040 capture of a faulting MOVEP.W/.L store.
    // Gate: moira_rules_test (byte faults, data window, cache and RTE).
    u32 framePc;
    if (mmu040LastWrite && (queue.ird & 0xFE00) != 0xF200 &&
        (queue.ird & 0xF1B8) != 0x0188) {
        framePc = mmu040LastWritePc;
        mmu040LastWrite = false;
    } else {
        mmu040LastWrite = false;
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

    // [B] MC68040UM-1993 §8.4.6.4/Table 8-6: only a physical push
    // has write + TM/TT=0. Its WB1A equals FA; WB3A stays logical.
    const bool pushFault = (mmu040Ssw & 0x011F) == 0;
    // Format $7 frame (Exception_build_stack_frame case 0x7)
    push<C, Long>(mmu040Move16[3]);         // PD3
    push<C, Long>(mmu040Move16[2]);         // PD2
    push<C, Long>(mmu040Move16[1]);         // PD1
    push<C, Long>(mmu040Move16[0]);         // WB1D/PD0
    push<C, Long>(pushFault ? mmu040FaultAddr : 0); // WB1A
    push<C, Long>(0);                       // WB2D
    push<C, Long>(mmu040Wb2Address);        // WB2A
    push<C, Long>(mmu040Wb3Data);           // WB3D
    push<C, Long>(mmu040Wb3Address);        // WB3A
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
