// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

#pragma once

#include "MoiraConfig.h"
#include "MoiraTypes.h"
#include "MoiraDebugger.h"

namespace moira {

class Moira {
    
    friend class Debugger;
    friend class Breakpoints;
    friend class Watchpoints;
    friend class Catchpoints;
    
    
    //
    // Configuration
    //
    
protected:
    
    // Emulated CPU model
    Model cpuModel = Model::M68000;

    // Instruction set used by the disassembler
    Model dasmModel = Model::M68000;

    // POM68K O5: FPU attached through the coprocessor interface (68882 on
    // the Mac LC II PDS card). NONE keeps the jump table byte-identical to
    // stock Moira (F2xx = Line-F). Configured via setFPUModel().
    FPUModel fpuModel = FPUModel::NONE;
    // When set, FPU-less 040 F2xx stacks classic format $0 Line-F (Mac OS
    // PACK 4) instead of architectural format $4. sst68040 leaves this clear.
    bool fpuDisabledSaneFline = false;
    
    // Visual style for disassembled instructions
    DasmStyle instrStyle;
    
    // Visual style for data dumps
    DasmStyle dataStyle;
    
    
    //
    // Subcomponents
    //
    
public:
    
    // Debugger handling breakpoints, watchpoints, catchpoints, and instruction tracing
    Debugger debugger = Debugger(*this);
    
    
    //
    // Internals
    //
    
protected:
    
    // Number of elapsed cycles since power-up
    i64 clock {};
    
    // The CPU's register set
    Registers reg {};
    
    // Prefetch queue for fetching instructions
    PrefetchQueue queue {};
    
    // Interrupt mode
    IrqMode irqMode {IrqMode::AUTO};
    
    // Current value on the IPL (Interrupt Priority Level) pins
    u8 ipl {};

    // NEOST_IPLFETCH : historique de la broche IPL pour reconnaissance différée
    // (port fidèle de WinUAE ipl_fetch_next). iplDelay4 == 0 ⇒ feature OFF, donc
    // pollIpl() ≡ reg.ipl = ipl. Réglé via setIplDelay(). Edit dans le sous-module
    // → cf. docs/MOIRA_WINUAE_CONVERGENCE.md (clobbérable au submodule update).
    u8  iplPrev {};            // valeur précédente de la broche (≙ regs.ipl_pin_p)
    i64 iplChangeClock {};     // horloge (cycles) du dernier changement (≙ ipl_pin_change_evt)
    i64 iplChangeClockPrev {-1000};  // horloge du changement PRÉCÉDENT (≙ ipl_pin_change_evt_p)
    i64 iplDelay4 {0};         // seuil « broche stabilisée » (cyc) ; 0 ⇒ OFF
    i64 iplDelay2 {0};         // seuil « ancienne valeur » (cyc)
    // Report « différé » de WinUAE (regs.ipl[1]) : un changement de broche vu <2 cyc
    // avant l'échantillon n'est PAS pris pour la frontière qui vient, mais devient
    // la valeur POLLÉE à la frontière SUIVANTE (rotation ipl[0]=ipl[1] de la run
    // loop, newcpu.c:5693). −1 = rien en attente.
    int iplDeferred {-1};

    // POM68K: one-instruction interrupt-recognition delay after a mask-
    // lowering SR-write (see setSR / the run loop). 68020+ only.
    int irqDelay {0};

    // POM68K Q2 — 68040 trace machinery (WinUAE MakeFromSR_x one-shot +
    // do_trace): trace040Pending fires vector 9 AFTER the current
    // instruction (armed by an SR write whose OLD Tx bits were set, even
    // if the write cleared them, or by a staged TRACE_EXC on the 040);
    // tracePc040 is WinUAE's regs.trace_pc — the format-$2 frame's
    // address field (0 unless the staged path latched it).
    bool trace040Pending {false};
    u32 tracePc040 {0};

    // Value on the lower two function code pins (FC1|FC0)
    u8 fcl {2};
    
    // Source of the function code pins
    u8 fcSource {};
    
    // Remembers the vector number of the most recent exception
    int exception {};
    
    // Cycle penalty (for 68020+ extended addressing modes)
    int cp {};
    
    // Controls exact timing of instructions running in loop mode (68010 only)
    int loopModeDelay {2};
    
    // Read buffer (appears in 68010 exception frames)
    u16 readBuffer {};
    
    // Write buffer (appears in 68010 exception frames)
    u16 writeBuffer {};
    
    // State flags used internally
    int flags {};
    
    
    //
    // Lookup tables
    //
    
private:
    
    // Jump table holding the instruction handlers
    typedef void (Moira::*ExecPtr)(u16);
    ExecPtr *exec = nullptr;
    
    // Jump table holding the loop mode instruction handlers (68010 only)
    ExecPtr *loop = nullptr;
    
    // Jump table holding the disassembler handlers
    typedef void (Moira::*DasmPtr)(StrWriter&, u32&, u16) const;
    DasmPtr *dasm = nullptr;
    
    // Table holding instruction information
    InstrInfo *info = nullptr;
    
    
    //
    // Constructing
    //
    
public:
    
    // Constructs and initializes a Moira instance
    Moira();
    
    //  Destroys the Moira instance
    virtual ~Moira();
    
protected:
    
    // Creates or updates the jump tables for execution and disassembly
    void createJumpTable(Model cpuModel, Model dasmModel);
    void createJumpTable(Model model) { createJumpTable(model, model); }
    
private:
    
    // Core routine for creating jump tables
    template <Core C> void createJumpTable(Model model, bool registerDasm);
    
    
    //
    // Configuring
    //
    
public:
    
    // Sets the emulated CPU models
    void setModel(Model cpuModel, Model dasmModel);
    void setModel(Model model) { setModel(model, model); }

    // POM68K O5: attaches or detaches an FPU (rebuilds the jump table and
    // resets the FPU to its power-up state, MC68881/882UM § 6.1)
    void setFPUModel(FPUModel model);
    FPUModel getFPUModel() const { return fpuModel; }
    Model getModel() const { return cpuModel; }
    // Mac OS PACK 4 wants format $0 F-line on FPU-less 040; oracle wants $4.
    void setFpuDisabledSaneFline(bool v) { fpuDisabledSaneFline = v; }
    bool getFpuDisabledSaneFline() const { return fpuDisabledSaneFline; }
    
    // Configures the syntax style for disassembly output
    void setDasmSyntax(Syntax value);
    
    // Sets the number format for disassembly output
    void setDasmNumberFormat(DasmNumberFormat value) { setNumberFormat(instrStyle, value); }
    
    // Sets the letter case for disassembly output
    void setDasmLetterCase(LetterCase value);
    
    // Sets the indentation for disassembly output
    void setDasmIndentation(int value) { instrStyle.tab = value; }
    
    // Sets the number format for data dumps
    void setDumpNumberFormat(DasmNumberFormat value) { setNumberFormat(instrStyle, value); }
    
    // Sets the indentation for data dumps
    void setDumpIndentation(int value) { dataStyle.tab = value; }
    
private:
    
    // Helper function
    void setNumberFormat(DasmStyle &style, const DasmNumberFormat &value);
    
    
    //
    // Querying CPU properties
    //
    
public:
    
    // Checks if the emulated CPU model has a coprocessor interface
    bool hasCPI() const;
    
    // Checks if the emulated CPU model has a memory management unit (MMU)
    bool hasMMU() const;
    
    // Checks if the emulated CPU model has a floating-point unit (FPU)
    bool hasFPU() const;
    
    // Returns the cache register mask, indicating the accessible CACR bits
    u32 cacrMask() const;
    
    // Returns the address bus mask, which defines the CPU's addressable memory range
    u32 addrMask() const;
    
protected:
    
    // Returns the address bus mask for a specific CPU core type
    template <Core C> u32 addrMask() const;
    
    
    //
    // Running the CPU
    //
    
public:
    
    // Performs a hard reset, simulating the native power-up sequence
    void reset();
    
    // Executes a single instruction
    void execute();

    // Executes instructions for the given number of cycles.
    //
    // Note: Since the emulartor cannot stop in the middle of an instruction,
    // the number of actually elapsed cycles may exceed the specified cycle
    // budget.
    void execute(i64 cycles);
    
    // Executes instructions until a specific cycle count is reached.
    void executeUntil(i64 cycle);
    
    // Checks if the CPU is in a HALT state
    bool isHalted() const { return flags & State::HALTED; }

    // POM68K: checks if the CPU sits in a STOP instruction (waiting for
    // an interrupt) — the diagnostic that separates "spinning" from
    // "stopped on an IPL that never rises" (tests/lcii_trace.cpp)
    bool isStopped() const { return flags & State::STOPPED; }


    //
    // POM68K JIT seam (src/jit/POM68K_JIT.md)
    //
    // POM68K's second execution engine lives OUTSIDE this vendored core, in
    // src/jit/, and drives this object. It needs four things Moira does not
    // otherwise expose. They are public rather than protected on purpose:
    // jit::Engine holds a `moira::Moira&`, not a machine wrapper, so that
    // one engine serves every 68040 machine profile — and a public,
    // non-virtual member call is the only shape that stays a DIRECT call.
    // (POM68K_VENDOR.md § "willFetchInstr … Folded inline": a per-instruction
    // indirect call once measured ~11% of the whole emulator.)
    //
    // Everything below is INERT until a jit::Engine arms it. Disarmed, the
    // whole seam costs the interpreter one predictable branch per
    // instruction — the PomIcache precedent.

public:

    // Instruction-fetch code window. While armed, the 68040 opcode, the
    // lookahead at pc+2 and every extension word whose LOGICAL address falls
    // inside [base, base+len) are served straight from `host` instead of
    // walking the ATC and re-entering the machine's memory map.
    //
    // `host` points INTO the guest RAM/ROM buffer, so guest writes are
    // visible to it immediately: self-modifying code needs no invalidation
    // here. What can go stale is the TRANSLATION — hence `super` and `gen`.
    struct PomJitWindow {
        const u8 *host = nullptr;   // host bytes for logical address `base`
        u32  base = 0;
        u32  len  = 0;              // >= 4 whenever armed (ird + irc)
        u32  gen  = 0;              // pomJitMmuGen at validation time
        bool armed = false;
        bool super = false;         // sr.s the translation was validated for
    };
    PomJitWindow pomJitWindow;

    // Bumped by every event that can change a logical→physical mapping:
    // ATC flushes (PFLUSH family, TC/URP/SRP writes) and TTR writes. A
    // window whose `gen` no longer matches is refused without any further
    // check — the one thing an address-range test cannot detect.
    u32 pomJitMmuGen = 0;

    // Every site that can change a logical→physical mapping goes through
    // here. The generation counter alone is enough for the code window,
    // which re-checks it on every fetch; the data TLB is read by GENERATED
    // code that checks nothing but the tag, so it has to be emptied at the
    // source instead.
    void pomJitMapMoved() { pomJitMmuGen++; pomJitDtlbFlush(); }

    // ── J3b: derived state dies WITH the ATC entry it derives from ──────
    // The windows and the data TLB serve accesses without re-walking, which
    // is exactly what an ATC hit does — so a hit through them is bit-exact
    // as long as the backing ATC entry is still resident. Once that entry
    // is EVICTED, the interpreter's next access re-walks and re-sets the
    // descriptor's U bit; an entry that outlived the eviction would skip
    // that guest-visible write. Invisible for weeks — until Mac OS VM
    // started READING the U bits for page aging (it does, on 8 KB pages,
    // once the System is up), at which point each engine's different
    // survival pattern turned into a different guest EXECUTION: three
    // engines, three futures, all "correct" and none comparable. Called on
    // both eviction sites (replacement in mmu040AtcFill, the write-M
    // invalidate in mmu040AtcLookup): kills the derived slices, the hot
    // pair and the code window for THAT page only, in O(slices).
    // `code` says WHICH derived state the evicted entry backed: the fetch
    // window derives from instruction-space residency and the data TLB from
    // data-space residency, and killing both on either eviction (the first
    // cut) threw the code window away every time a DATA page sharing its
    // logical page churned — pure waste, since the I-side entry that
    // justifies the window was still resident. The exactness argument is
    // per-space: a window hit skips a FETCH walk, so it must die with the
    // I-entry; a TLB hit skips a DATA walk, so it dies with the D-entry.
    void pomJitAtcEvict(u32 logicalPage, u32 pageLen, bool code) {
        if (code) {
            if (pomJitWindow.armed &&
                pomJitWindow.base >= logicalPage &&
                pomJitWindow.base < logicalPage + pageLen)
                pomJitDisarm();
            return;
        }
        for (u32 off = 0; off < pageLen; off += 4096) {
            const u32 slice = (logicalPage + off) >> 12;
            for (PomJitDtlb *t : { &pomJitDtlbR, &pomJitDtlbW }) {
                PomJitDtlbEntry &e = t->e[slice & (PomJitDtlb::kEntries - 1)];
                if ((e.tag & 0x7FFFFFFFu) == slice) { e.tag = PomJitDtlb::kEmpty; e.host = nullptr; }
            }
            if ((pomJitDataR1.tag & 0x7FFFFFFFu) == slice) pomJitDataR1 = {};
            if ((pomJitDataW1.tag & 0x7FFFFFFFu) == slice) pomJitDataW1 = {};
        }
    }

    void pomJitArm(const u8 *host, u32 base, u32 len, bool super) {
        pomJitWindow.host  = host;
        pomJitWindow.base  = base;
        pomJitWindow.len   = len;
        pomJitWindow.gen   = pomJitMmuGen;
        pomJitWindow.super = super;
        pomJitWindow.armed = host != nullptr && len >= 4;
    }
    void pomJitDisarm() { pomJitWindow.armed = false; pomJitWindow.host = nullptr; }

    // True when `addr` and the three bytes after it can be fetched from the
    // window under the CURRENT privilege level and MMU generation.
    bool pomJitCovers(u32 addr) const {
        const PomJitWindow &w = pomJitWindow;
        return w.armed && w.gen == pomJitMmuGen && w.super == bool(reg.sr.s) &&
               u32(addr - w.base) <= w.len - 4;
    }

    // Reads a word out of the window without touching the bus. The caller
    // must have established pomJitCovers(addr).
    u16 pomJitPeek(u32 addr) const {
        const u8 *p = pomJitWindow.host + (addr - pomJitWindow.base);
        return u16(u16(p[0]) << 8 | p[1]);
    }

    bool pomJitIdle()  const { return flags == 0; }
    bool pomJitSuper() const { return bool(reg.sr.s); }
    u16  pomJitIrd()   const { return queue.ird; }

    // Runs exactly one instruction on the 68040 fast path. The CALLER must
    // have established pomJitIdle(); everything else — the per-instruction
    // MMU state reset, POLL_IPL, the handler dispatch, exception processing
    // and the staged trace — happens here, in the same single copy
    // execute() uses. Returns false when the instruction did not retire
    // normally, which is always a "go back through execute()" signal.
    bool pomJitExecOne();

    // Side-effect-free translation probe for a CODE address. TTR match,
    // MMU-disabled identity, then a read-only scan of the instruction ATC.
    // It deliberately does NOT walk the page tables: a walk writes the U/M
    // descriptor bits back into guest RAM (MoiraExecMMU_cpp.h mmu040Walk)
    // and can throw, neither of which may happen outside an instruction.
    // A miss simply returns false — the interpreter then fetches normally,
    // fills the ATC, and the next probe succeeds.
    bool pomJitProbeCode(u32 logical, bool super,
                         u32 &phys, u32 &pageBase, u32 &pageLen) const;

    // ── J2: the data-side twin of the code window ────────────────────────
    // Generated host code cannot call mmu040Read: that path throws on a
    // fault, and a C++ exception may not cross a JIT frame (there is no
    // unwind information for machine code we emitted ourselves). So the
    // x86-64 backend translates data addresses through this direct-mapped
    // cache instead, entirely inline, and BAILS OUT of the compiled block —
    // at an instruction boundary, before committing anything — whenever the
    // lookup misses. The interpreter then runs that one instruction and
    // takes the fault, fills the ATC, or touches the I/O register, exactly
    // as it always did.
    //
    // An entry is only ever created for an address the engine has PROVEN
    // (jit::Engine::fillDtlb) to be plain guest memory with the access
    // already permitted by a resident ATC entry — no page-table walk, no
    // descriptor U/M write-back, no I/O side effect can hide behind a hit.
    // Read and write are separate caches because a page can be readable and
    // write-protected, and because a write to an unmodified page has to
    // re-walk so the table search sets M (mmu040AtcLookup).
    // 16 bytes so that generated code reaches an entry with a single
    // lea reg, [cpu + index*16 + table] — the tag and the host pointer then
    // sit at +0 and +8 of one cache line's worth of addressing.
    struct PomJitDtlbEntry {
        u32 tag;                        // logical page number (addr >> 12)
        u32 pad;
        u8* host;                       // host bytes for that page, byte 0
    };
    struct PomJitDtlb {
        static constexpr u32 kEntries = 256;     // direct-mapped, 4 KB/table
        static constexpr u32 kEmpty = 0xFFFFFFFF;  // no logical page is ~0

        PomJitDtlbEntry e[kEntries];

        PomJitDtlb() { clear(); }
        void clear() {
            for (u32 i = 0; i < kEntries; i++) { e[i].tag = kEmpty; e[i].host = nullptr; }
        }
    };
    PomJitDtlb pomJitDtlbR, pomJitDtlbW;

    // Level 0 of the data window: ONE entry per direction, sitting in the
    // hot part of the object next to the fetch window. The 256-entry table
    // below is level 1. This split is measured, not aesthetic: consulting
    // the 8 KB table directly from the interpreter LOST ~10 % — streaming
    // guest data kept evicting the table's own cache lines, so every probe
    // added an L1 miss to an access the long path would have served out of
    // hot machine state. One 16-byte entry per direction stays resident by
    // construction, and real 68k data traffic is page-local enough (a copy
    // loop is one read page + one write page) that it catches most of what
    // the big table would have.
    struct PomJitDataPage { u32 tag = 0xFFFFFFFF; u8 *host = nullptr; };
    PomJitDataPage pomJitDataR1, pomJitDataW1;

    void pomJitDtlbFlush() {
        pomJitDtlbR.clear(); pomJitDtlbW.clear();
        pomJitDataR1 = {}; pomJitDataW1 = {};
    }

    // ── J3: the data window — the same DTLB, consulted by the INTERPRETER ──
    // The fetch window took instruction fetches off the long path; this
    // takes DATA accesses off it, for both engines at once: mmu040Read and
    // mmu040Write try the DTLB before the ATC/translate/decode chain. The
    // fill callback is bound by jit::Engine (which owns the refusal rules —
    // plain RAM/ROM only, resident+permitted ATC entries only, never a page
    // holding translated code); when no engine exists the pointer is null
    // and the whole path is one dead branch.
    //
    // Invalidation, exhaustively: MMU/TTR changes (pomJitMapMoved), the
    // machine's own remaps (each map's jitMapChanged flushes the CPU DTLB
    // directly — the overlay flip writes no ATC), privilege changes
    // (setSupervisorFlags — entries are probed under the CURRENT privilege
    // and carry no super bit), and every engine flush/evict.
    uint8_t *(*pomJitDtlbFillFn)(void *, u32, int) = nullptr;
    void *pomJitDtlbFillCtx = nullptr;

    // The privilege level rides in TAG BIT 31 (a page number is 20 bits),
    // so user and supervisor entries coexist and NOTHING is flushed on an
    // exception or an RTE. The first cut flushed the whole table from
    // setSupervisorFlags instead — correct, and a disaster: several
    // thousand interrupts a second each emptied the cache, the hit rate
    // collapsed, and the "fast" path measured 15 % SLOWER than no path.
    static u32 pomJitDataTag(u32 addr, bool super) {
        return (addr >> 12) | (super ? 0x80000000u : 0u);
    }

    // The fill half, out of line on purpose: pomJitData is inlined into
    // every mmu040Read/Write instantiation, and the interpreter's hot loop
    // pays for those bytes whether the path hits or not.
    u8 *pomJitDataSlow(u32 addr, u32 want, bool w) noexcept;

    template <int N, bool W> u8 *pomJitData(u32 addr) {
        if (flags & State::CHECK_WP) return nullptr;    // watchpoints see all
        if (mmu040Moves >= 0) return nullptr;           // alternate FC space
        if (!W && mmu040Lrmw) return nullptr;           // locked reads = writes
        const u32 off = addr & 4095;
        if (off > 4096u - N) return nullptr;            // page straddle
        const u32 want = pomJitDataTag(addr, bool(reg.sr.s));
        PomJitDataPage &c = W ? pomJitDataW1 : pomJitDataR1;
        if (c.tag == want) [[likely]]
            return c.host ? c.host + off : nullptr;     // null = remembered refusal
        // Levels 1 and 2 (the table, then the engine's fill) out of line —
        // and the no-engine case falls out for free: with the callback null
        // nothing ever fills, so the level-0 tag never matches.
        u8 *base = pomJitDataSlow(addr, want, W);
        return base ? base + off : nullptr;
    }

    // The other half of the data path: what generated code calls when the
    // TLB refuses — an I/O register, a page the probe could not confirm, an
    // access that straddles two pages. It performs the REAL access, through
    // the same mmu040 path the interpreter uses, and converts the one thing
    // a JIT frame cannot survive — a thrown fault — into `false`.
    //
    // A false answer means NOTHING was committed on the guest side, so the
    // caller's contract is simply to leave the instruction alone and let the
    // interpreter re-run it from the boundary; it will fault identically and
    // stack the frame properly.
    bool pomJitReadData(u32 addr, int bytes, u32 &out) noexcept;
    bool pomJitWriteData(u32 addr, int bytes, u32 val) noexcept;

    // Charges `cycles` to the clock THROUGH the machine's sync(), which is
    // where peripheral catch-up hangs. Generated code must not simply add
    // to `clock`: the CPU wrapper batches VIA/ASC/SWIM/MCU time off this
    // call, and a block that skipped it would run the guest forward while
    // the machine around it stood still.
    void pomJitSync(int cycles) { sync(cycles); }

    // True when POLL_IPL is the plain `reg.ipl = ipl` assignment, which is
    // the only form generated code models. The deferred-recognition feature
    // (NEOST_IPLFETCH, setIplDelay) is off on every Mac profile; a backend
    // must refuse to compile if it is ever turned on.
    bool pomJitSimpleIpl() const { return iplDelay4 == 0; }

    // Side-effect-free translation probe for a DATA address, the twin of
    // pomJitProbeCode. `write` applies the two rules a read does not have:
    // the page must not be write-protected, and it must already be marked
    // modified — a write to an unmodified page owes the descriptor an M-bit
    // write-back, which is a guest-memory store a probe may not perform.
    bool pomJitProbeData(u32 logical, bool super, bool write,
                         u32 &phys, u32 &pageBase, u32 &pageLen) const;

    // ── the 020 fetch seam (2026-07-28) ─────────────────────────────────
    // A plain 68020 has no MMU: fetches go read<PROG> → the machine's
    // decode. The window replaces exactly that tail, but the HEAD of a
    // read<PROG,Word> is behavioural state that must be replicated, not
    // skipped: the POLL_IPL that rides on the prefetch (interrupt
    // recognition), the FC pins, and the in-flight access stamp an
    // external /BERR would read back (the Mac LC ROM's 32-bit probe pins
    // that contract — MoiraDataflow read<> kPlain020 block). Returns false
    // when the window cannot serve the address; the caller then takes the
    // ordinary path, which performs those same steps itself.
    template <bool P> bool pomJitFetch020(u32 addr, u16 &out) {
        const u8 *p;
        if (!pomJitFetch(addr, 2, p)) return false;
        if constexpr (P) pollIpl();
        setFC(FC::USER_PROG);
        mmuAccAddr = addr; mmuAccSsw = 0x20;
        mmuAccFc = readFC(); mmuAccWrite = false;
        out = u16(u16(p[0]) << 8 | p[1]);
        return true;
    }

    // Byte offsets, measured from this object's address, of every field a
    // code generator has to touch. Returned as a struct rather than exposed
    // as public members so the register file stays private, and computed
    // from a live object so no offsetof() on a polymorphic type is needed.
    // src/jit/backends/JitBackendX64.cpp is the only consumer.
    struct PomJitLayout {
        u32 d, a;                       // reg.d[0], reg.a[0]
        u32 pc, pc0;
        u32 srX, srN, srZ, srV, srC, srS;
        u32 regIpl, iplPin;
        u32 clock, flags;
        u32 ird, irc;
        u32 dtlbR, dtlbW;               // base of each PomJitDtlb::e[]
        // POM68K JIT: the 68040 MOVEM restart latch. Armed between a
        // faulted MOVEM and its RTE-restarted completion; a compiled MOVEM
        // must bail to the interpreter while it is set, because the
        // restart must resume from the SAVED ea, not a recomputed one.
        u32 movemArmed;
    };
    PomJitLayout pomJitLayout() const;

private:

    // The window read shared by the three instruction-fetch sites: two on
    // the 68040 (mmu040InstrStart, readExt) and one on the 68030
    // (mmuFetchWord) — the seam stopped being 040-only in 2026-07-28.
    // `n` bytes must fit; callers pass 4 (ird + irc) or 2 (extension word).
    bool pomJitFetch(u32 addr, u32 n, const u8 *&p) const {
        const PomJitWindow &w = pomJitWindow;
        if (!w.armed) return false;
        if (u32(addr - w.base) > w.len - n) return false;
        if (w.gen != pomJitMmuGen || w.super != bool(reg.sr.s)) return false;
        // A watchpoint must still see every fetch (mmu040Read's CHECK_WP
        // hook). CHECK_WP is a `flags` bit, so the engine is already out of
        // the way — this is belt and braces on a cold branch.
        if (flags & State::CHECK_WP) return false;
        p = w.host + (addr - w.base);
        return true;
    }

    // Reproduces the in-flight access context an mmu040Read<C, Word> would
    // have left behind. Only read back by extBusError040(), and only
    // inherited (rather than set) by MOVE16 — but skipping it would make an
    // external bus error stack a different frame under the JIT than under
    // the interpreter.
    void pomJitStampAccess(u32 addr) {
        mmu040AccBase  = addr;
        mmu040AccAddr  = addr;
        mmu040AccVal   = 0;
        mmu040AccSz    = 1;                 // Word
        mmu040AccWrite = false;
        mmu040AccData  = false;             // instruction space
        mmu040AccFirst = true;
        mmu040AccSplit = false;
    }

    // Processes an exception that was caught during execution
    void processException(const std::exception &exception);
    
    // Processes an exception for a specific CPU core type
    template <Core C> void processException(const std::exception &exception);
    
    // Performs a core-specific reset routine
    template <Core C> void reset();
    
    // Checks for a pending interrupt and handles it if necessary
    bool checkForIrq();
    
    // Puts the CPU into a HALT state, stopping execution
    void halt();
    
    
    //
    // Running the disassembler
    //
    
public:
    
    // Disassembles an instruction and returns the instruction size in bytes
    int disassemble(char *str, u32 addr) const;
    
    // Creates a textual representation of the status register
    void disassembleSR(char *str) const { disassembleSR(str, reg.sr); }
    
    // Creates a textual representation of a given status register
    void disassembleSR(char *str, const StatusRegister &sr) const;
    
    // Converts an integer value to a textual representation
    void dump8(char *str, u8 value) const;
    void dump16(char *str, u16 value) const;
    void dump24(char *str, u32 value) const;
    void dump32(char *str, u32 value) const;
    
    // Converts multiple 16-bit values to a textual representation
    void dump16(char *str, u16 values[], int cnt) const;
    void dump16(char *str, u32 addr, int cnt) const;
    
    // Returns instruction metadata for a given opcode
    InstrInfo getInstrInfo(u16 op) const;

    
    //
    // Interfacing with other components
    //
    
protected:
    
#if MOIRA_VIRTUAL_API == true
    
    // Advances the internal clock by the specified number of cycles
    virtual void sync(int cycles) { clock += cycles; }
    
    // Reads a value from memory
    virtual u8 read8(u32 addr) const = 0;
    virtual u16 read16(u32 addr) const = 0;
    
    // Reads a 16-bit value from memory during the reset routine
    virtual u16 read16OnReset(u32 addr) const { return read16(addr); }
    
    // Reads a 16-bit value from memory for disassembly purposes
    virtual u16 read16Dasm(u32 addr) const { return read16(addr); }
    
    // Writes a value into memory
    virtual void write8(u32 addr, u8 val) const = 0;
    virtual void write16(u32 addr, u16 val) const = 0;
    
    // Provides the interrupt vector for a given interrupt level in USER mode
    virtual u16 readIrqUserVector(u8 level) const { return 0; }

    
    //
    // State delegates
    //
    
    // Called when the CPU is reset
    virtual void cpuDidReset() { }
    
    // Called when the CPU enters the HALT state
    virtual void cpuDidHalt() { }
    
    
    //
    // Instruction delegates
    //
    
    // Called before an instruction is executed
    virtual void willExecute(const char *func, Instr I, Mode M, Size S, u16 opcode) { }
    
    // Called after an instruction has been executed
    virtual void didExecute(const char *func, Instr I, Mode M, Size S, u16 opcode) { }
    
    
    //
    // Exception delegates
    //
    
    // Called before an exception is executed
    virtual void willExecute(M68kException exc, u16 vector) { }
    
    // Called after an exception has been executed
    virtual void didExecute(M68kException exc, u16 vector) { }
    
    // Called when an interrupt is about to be processed
    virtual void willInterrupt(u8 level) { }

    // NEOST : cycles idle autour du CYCLE D'IACK du 68000 (execInterrupt<C68000>,
    // entre l'empilement de PClo et celui de SR/PChi). Stock Moira = SYNC(4) avant
    // la lecture du vecteur et SYNC(4) après. Un sous-classeur (NeoST) peut y porter
    // le modèle Hatari/WinUAE `iack_cycle` (newcpu.c) : attente E-clock (avant) +
    // bloc IACK→DTACK + idle (après), calculés À CE POINT précis — la phase
    // d'horloge y est déjà quantifiée par l'alignement bus du push PClo.
    virtual int iackSyncBefore(u8 level) { return 4; }
    virtual int iackSyncAfter(u8 level) { return 4; }

    // Called when the CPU jumps to an exception vector
    virtual void didJumpToVector(int nr, u32 addr) { }

    
    //
    // Cache register delegates
    //
    
    // Called when the CACR register is modified
    virtual void didChangeCACR(u32 value) { }

    // Called when the CAAR register is modified
    virtual void didChangeCAAR(u32 value) { }

    // POM68K: 68030 instruction-cache timing overlay, folded inline into
    // mmuFetchWord. It was a virtual willFetchInstr(addr, super) hook fired
    // per instruction word; the indirect call + out-of-line model cost ~11%
    // of the whole emulator (TODO § Performance), and everything it needs
    // (reg.cacr, reg.sr.s, clock) lives right here. The wrapper (Cpu030)
    // arms it, owns the knobs and reads the stats; the model itself is the
    // MC68030UM §6 on-chip i-cache: 256 bytes = 16 lines × 4 longwords,
    // LOGICAL, direct-mapped, tag = A[31:8] + supervisor(FC2), per-longword
    // valid bits, whole-line evict on tag change, gated on CACR bit 0.
    // A miss charges `missPenalty` cycles (the fetch bus access the real
    // chip pays only on a miss). Zero cost when not armed.
    struct PomIcache {
        bool armed = false;         // wrapper opted in (LC II machine)
        i32  missPenalty = 0;       // cycles charged per i-cache miss
        u32  tag[16] = {};
        u8   valid[16] = {};
        i64  fetches = 0, hits = 0, misses = 0;
        void reset() {              // CACR clear strobes / hard reset
            for (int i = 0; i < 16; i++) { tag[i] = 0xFFFFFFFFu; valid[i] = 0; }
        }
    };
    PomIcache pomIcache;

    
    //
    // Debugger delegates
    //
    
    // Called when a soft stop is reached
    virtual void didReachSoftstop(u32 addr) { }
    
    // Called when a breakpoint is hit
    virtual void didReachBreakpoint(u32 addr) { }
    
    // Called when a watchpoint is hit
    virtual void didReachWatchpoint(u32 addr) { }
    
    // Called when a catchpoint is hit
    virtual void didReachCatchpoint(u8 vector) { }
    
    // Called when a software trap is hit
    virtual void didReachSoftwareTrap(u32 addr) { }
    
#else
    
    // Advances the internal clock by the specified number of cycles
    void sync(int cycles);
    
    // Reads a value from memory
    u8 read8(u32 addr) const;
    u16 read16(u32 addr) const;
    
    // Reads a 16-bit value from memory during the reset routine
    u16 read16OnReset(u32 addr) const;
    
    // Reads a 16-bit value from memory for disassembly purposes
    u16 read16Dasm(u32 addr) const;
    
    // Writes a value into memory
    void write8(u32 addr, u8 val) const;
    void write16(u32 addr, u16 val) const;
    
    // Provides the interrupt vector for a given interrupt level in USER mode
    u16 readIrqUserVector(u8 level) const;

    
    //
    // State delegates
    //
    
    // Called when the CPU is reset
    void cpuDidReset();
    
    // Called when the CPU enters the HALT state
    void cpuDidHalt();
    
    
    //
    // Instruction delegates
    //
    
    // Called before an instruction is executed
    void willExecute(const char *func, Instr I, Mode M, Size S, u16 opcode);
    
    // Called after an instruction has been executed
    void didExecute(const char *func, Instr I, Mode M, Size S, u16 opcode);
    
    
    //
    // Exception delegates
    //
    
    // Called before an exception is executed
    void willExecute(M68kException exc, u16 vector);
    
    // Called after an exception has been executed
    void didExecute(M68kException exc, u16 vector);
    
    // Called when an interrupt is about to be processed
    void willInterrupt(u8 level);
    
    // Called when the CPU jumps to an exception vector
    void didJumpToVector(int nr, u32 addr);
    
    
    //
    // Cache register delegates
    //
    
    // Called when the CACR register is modified
    void didChangeCACR(u32 value);
    
    // Called when the CAAR register is modified
    void didChangeCAAR(u32 value);
    
    
    //
    // Debugger delegates
    //
    
    // Called when a soft stop is reached
    void didReachSoftstop(u32 addr);
    
    // Called when a breakpoint is hit
    void didReachBreakpoint(u32 addr);
    
    // Called when a watchpoint is hit
    void didReachWatchpoint(u32 addr);
    
    // Called when a catchpoint is hit
    void didReachCatchpoint(u8 vector);
    
    // Called when a software trap is hit
    void didReachSoftwareTrap(u32 addr);
    
#endif
    
    //
    // Accessing the clock
    //
    
public:
    
    // Returns the current CPU clock cycle count (elapsed cycles since power-up)
    i64 getClock() const { return clock; }
    
    // Sets the CPU clock cycle count
    void setClock(i64 val) { clock = val; }
    
    
    //
    // Accessing registers
    //
    
public:
    
    // Gets or sets the value of a data register (0 - 7)
    u32 getD(int n) const { return readD(n); }
    void setD(int n, u32 v) { writeD(n, v); }
    
    // Gets or sets the value of an address register (0 - 7)
    u32 getA(int n) const { return readA(n); }
    void setA(int n, u32 v) { writeA(n, v); }
    
    // Gets or sets the value of the program counter (PC)
    u32 getPC() const { return reg.pc; }
    void setPC(u32 val) { reg.pc = val; }
    
    // Gets or sets the address of the currently executed instruction
    u32 getPC0() const { return reg.pc0; }
    void setPC0(u32 val) { reg.pc0 = val; }
    
    // Gets or sets the IRC register, which is part of the prefetch queue
    u16 getIRC() const { return queue.irc; }
    void setIRC(u16 val) { queue.irc = val; }
    
    // Gets or sets the IRD register, which is part of the prefetch queue
    u16 getIRD() const { return queue.ird; }
    void setIRD(u16 val) { queue.ird = val; }
    
    // Gets or sets the value of the Condition Code Register (CCR)
    u8 getCCR() const;
    void setCCR(u8 val);
    
    // Gets or sets the value of the Status Register (SR)
    u16 getSR() const;
    void setSR(u16 val);
    
    // Gets or sets the current value of the stack pointer (SP)
    u32 getSP() const { return reg.sp; }
    void setSP(u32 val) { reg.sp = val; }
    
    // Gets or sets the User Stack Pointer (USP)
    u32 getUSP() const { return !reg.sr.s ? reg.sp : reg.usp; }
    void setUSP(u32 val) { if (!reg.sr.s) reg.sp = val; else reg.usp = val; }
    
    // Gets or sets the Interrupt Stack Pointer (ISP)
    u32 getISP() const { return (reg.sr.s && !reg.sr.m) ? reg.sp : reg.isp; }
    void setISP(u32 val) { if (reg.sr.s && !reg.sr.m) reg.sp = val; else reg.isp = val; }
    
    // Gets or sets the Master Stack Pointer (MSP)
    u32 getMSP() const { return (reg.sr.s && reg.sr.m) ? reg.sp : reg.msp; }
    void setMSP(u32 val) { if (reg.sr.s && reg.sr.m) reg.sp = val; else reg.msp = val; }
    
    // Gets or sets the Vector Base Register (VBR)
    u32 getVBR() const { return reg.vbr; }
    void setVBR(u32 val) { reg.vbr = val; }
    
    // Gets or sets the Source Function Code (SFC)
    u32 getSFC() const { return reg.sfc; }
    void setSFC(u32 val) { reg.sfc = val & 0b111; }
    
    // Gets or sets the Destination Function Code (DFC)
    u32 getDFC() const { return reg.dfc; }
    void setDFC(u32 val) { reg.dfc = val & 0b111; }
    
    // Gets or sets the Cache Control Register (CACR)
    u32 getCACR() const { return reg.cacr; }
    void setCACR(u32 val);
    
    // Gets or sets the Cache Address Register (CAAR)
    u32 getCAAR() const { return reg.caar; }
    void setCAAR(u32 val);

    // POM68K: gets or sets the 68030 MMU registers (MC68030UM § 9.7.2)
    u64 getCRP() const { return reg.crp; }
    void setCRP(u64 val) { reg.crp = val; }

    u64 getSRP() const { return reg.srp; }
    void setSRP(u64 val) { reg.srp = val; }

    u32 getTC() const { return reg.tc; }
    void setTC(u32 val) { reg.tc = val; }

    u32 getTT0() const { return reg.tt0; }
    void setTT0(u32 val) { reg.tt0 = val; }

    u32 getTT1() const { return reg.tt1; }
    void setTT1(u32 val) { reg.tt1 = val; }

    u16 getMMUSR() const { return reg.mmusr; }
    void setMMUSR(u16 val) { reg.mmusr = val; }

    // POM68K Q2: gets or sets the 68040 MMU registers (M68040UM § 3.1.1).
    // Setter masks mirror the oracle (WinUAE newcpu_common.c m68k_move2c,
    // 68040 rows): TC keeps E|P, ITT/DTT keep $FFFFE364, URP/SRP/MMUSR
    // store all 32 bits.
    // POM68K JIT: the three ATC-flushing setters below reach pomJitMmuGen
    // through mmu040AtcFlushAll(); the four TTR setters do not flush the
    // ATC, so they bump the generation themselves.
    u32 getURP040() const { return reg.urp040; }
    void setURP040(u32 val) { reg.urp040 = val; mmu040AtcFlushAll(); }

    u32 getSRP040() const { return reg.srp040; }
    void setSRP040(u32 val) { reg.srp040 = val; mmu040AtcFlushAll(); }

    u32 getTC040() const { return reg.tc040; }
    void setTC040(u32 val) {
        reg.tc040 = val & 0xC000;
        mmu040AtcFlushAll();                 // page size / enable change
    }

    // POM68K Q8: disable the 040 ATC and force walk-per-access (oracle-
    // identical mode used for differential checks). Default is ATC on.
    void setMmu040AtcArmed(bool on) { mmu040AtcArmed = on; }
    bool mmu040AtcIsArmed() const { return mmu040AtcArmed; }

    u32 getITT0() const { return reg.itt0; }
    void setITT0(u32 val) { reg.itt0 = val & 0xFFFFE364; pomJitMapMoved(); }

    u32 getITT1() const { return reg.itt1; }
    void setITT1(u32 val) { reg.itt1 = val & 0xFFFFE364; pomJitMapMoved(); }

    u32 getDTT0() const { return reg.dtt0; }
    void setDTT0(u32 val) { reg.dtt0 = val & 0xFFFFE364; pomJitMapMoved(); }

    u32 getDTT1() const { return reg.dtt1; }
    void setDTT1(u32 val) { reg.dtt1 = val & 0xFFFFE364; pomJitMapMoved(); }

    u32 getMMUSR040() const { return reg.mmusr040; }
    void setMMUSR040(u32 val) { reg.mmusr040 = val; }

    // POM68K O5: 68882 programmer's model (MC68881/882UM § 1.4). getFP /
    // setFP use the SST030 raw-word contract: w[0] = (sign|exp) << 16,
    // w[1] = mantissa bits 63..32, w[2] = bits 31..0.
    //
    // State-restore convention (solo-corpus convergence): loading FPU
    // state through any of these setters leaves the FPU in the NON-NULL
    // (idle) state — the WinUAE oracle glue forces regs.fpu_state = 1
    // after oracle_set_state, so a subsequent FSAVE emits the 68882 IDLE
    // frame, never the 4-byte NULL frame. A freshly reset FPU that has
    // executed nothing still FSAVEs a NULL frame.
    void getFP(int n, u32 w[3]) const {
        w[0] = u32(fpu.fp[n].high) << 16;
        w[1] = u32(fpu.fp[n].low >> 32);
        w[2] = u32(fpu.fp[n].low);
    }
    void setFP(int n, const u32 w[3]) {
        fpu.fp[n].high = u16(w[0] >> 16);
        fpu.fp[n].low = u64(w[1]) << 32 | w[2];
        fpu.state = 1;
    }

    // 6888x register masks: FPCR bits 3-0 always read 0 (§ 4.2, WinUAE
    // fpcr_mask = 0xfff0); FPSR bits 31-28 and 2-0 always 0 (fpsr_mask)
    u32 getFPCR() const { return fpu.fpcr & 0xfff0; }
    void setFPCR(u32 val) { fpu.fpcr = val & 0xfff0; fpu.state = 1; }

    u32 getFPSR() const { return fpu.fpsr & 0x0ffffff8; }
    void setFPSR(u32 val) { fpu.fpsr = val & 0x0ffffff8; fpu.state = 1; }

    u32 getFPIAR() const { return fpu.fpiar; }
    void setFPIAR(u32 val) { fpu.fpiar = val; fpu.state = 1; }


    //
    // 68882 FPU state (POM68K O5 slice 2, MoiraExecFPU_cpp.h)
    //
    // Execution is backed by extern/softfloat (same softfloat family as
    // the primary oracle, WinUAE); the 6888x semantics are ported from
    // WinUAE fpp.c / fpp_softfloat.c with file:line citations. Nothing
    // here is reachable while fpuModel == NONE (the jump table then holds
    // stock Line-F handlers). See extern/moira/POM68K_VENDOR.md § FPU.
    //

protected:

    struct {

        FpuExtended fp[8];  // FP0-FP7 (reset = nonsignaling NaN, § 6.1)
        u32 fpcr {};        // control register (enables + mode byte)
        u32 fpsr {};        // status register (cc, quotient, exc, accrued)
        u32 fpiar {};       // instruction address register

        // Frame micro-state (WinUAE regs.fpu_state / fpu_exp_state):
        // state 0 = reset/null (FSAVE writes a NULL frame), 1 = idle
        int state {};
        int expState {};    // nonzero = exceptional state pending in frame
        int expPend {};     // pending arithmetic exception vector (0 = none)
        u32 fsaveCcr {};    // IDLE-frame command/condition register
        u32 fsaveEo[3] {};  // IDLE-frame exceptional operand
        u32 ea {};          // last operand EA (WinUAE regs.fp_ea) — lands
                            // in the format $3 post-instruction frame
    } fpu;

private:

    // Power-up / null-frame state (MC68881/882UM § 6.1: control registers
    // zeroed, data registers = nonsignaling NaN $7FFF FFFF...FFFF)
    void fpuResetState();

    // FPCR mode byte -> softfloat rounding mode/precision (fpp_softfloat.c
    // fp_set_mode) + the 6888x NaN/infinity special flags
    void fpuSetMode();

    // FPSR maintenance (WinUAE fpp.c fpsr_* helpers)
    void fpuClearStatus();                      // exc byte + host flags
    u32  fpuMakeStatus();                       // host flags -> FPSR + accrued
    void fpuSetResultFlags(const FpuExtended &r); // condition code byte
    void fpuSetQuotient(u64 quot, u8 sign);
    void fpuGetQuotient(u64 *quot, u8 *sign);

    // The 32 conditional predicates (MC68881UM § 4.6.2.2, 6888x table);
    // sets BSUN/AIOP on the IEEE non-aware predicates, returns -2 when an
    // enabled BSUN trap was taken
    template <Core C> int fpuCondEval(u16 cond);

    // Pending pre-instruction exception check (WinUAE fp_exception_pending)
    template <Core C> bool fpuCheckPending();

    // FPU exception: pre-instruction = format $0 (PC = re-executing FP
    // instruction), post-instruction = format $3 with fp_ea (WinUAE
    // Exception_build_stack_frame_common nr 48-55)
    template <Core C> void execFpuException(u16 vector, bool pre);

    // FRESTORE invalid-frame format error (vector 14) — stacks WinUAE's
    // PC (past all consumed words), ruling D21
    template <Core C> void execFRestoreFormatError();

    // POM68K Q4: 68LC040/68EC040 F2xx with no FPU — vector 11, format $4
    // "FP disabled" frame {SR, PC-after-consumed-words, $402C, EA,
    // instruction PC} (WinUAE fpu_op_illg + Exception_build_stack_frame
    // case 0x4). The caller consumed the shape's words and passes the EA
    // per WinUAE's fault_if_no_fpu call site (0 for most shapes).
    template <Core C> void execFpuDisabled040(u32 ea);

    // Arms a plain (An)± fixup for FPU operands on the 68030 (WinUAE
    // fpp.c mmufixup arming): the register is RESTORED on a non-lastwrite
    // bus fault, but the $A/$B frame's wb2/wb3 status byte stays 0
    void fpuArmFixup(int n);

    // Arithmetic-exception latch + 68882 FSAVE frame data capture
    // (WinUAE fpsr_check_arithmetic_exception, 6888x branch); ea = the
    // operand's effective address (0 for register/immediate operands),
    // latched into fpu.ea for the format $3 frame
    bool fpuCheckArithException(const FpuExtended &src, u16 opcode, u16 ext, u32 ea);

    // Normalizes denormal/unnormal operands (6888x supports denormals:
    // WinUAE normalize_or_fault_if_no_denormal_support, 6888x branch)
    void fpuNormalize(FpuExtended &v);

    // FMOVECR constant ROM incl. undefined offsets (WinUAE fpu_get_constant)
    bool fpuGetConstant(FpuExtended &v, int cr);

    // Opmode dispatch (WinUAE fp_arithmetic); returns true when the result
    // is to be stored in the destination register
    bool fpuArithmetic(const FpuExtended &src, FpuExtended &dst, u16 ext);

    // Operand transfer, all 7 formats (WinUAE get_fp_value/put_fp_value2);
    // return 1 = done, 0 = invalid encoding (Line-F); ea receives the
    // memory operand's effective address (untouched otherwise)
    template <Core C, Mode M> int fpuGetSource(u16 opcode, u16 ext, FpuExtended &src, u32 &ea);
    template <Core C, Mode M> int fpuPutDest(u16 opcode, u16 ext, FpuExtended value, u32 &ea);


    //
    // Supervisor mode
    //
    
public:
    
    // Sets or clears supervisor mode
    void setSupervisorMode(bool value);
    
    // Sets or clears master mode
    void setMasterMode(bool value);
    
    // Sets or clear the supervisor and master flags
    void setSupervisorFlags(bool s, bool m);
    
    
    //
    // Trace Flags
    //
    
private:
    
    // Enables or disables trace mode (T1 flag)
    void setTraceFlag() { reg.sr.t1 = true; flags |= State::TRACING; }
    void clearTraceFlag() { reg.sr.t1 = false; flags &= ~State::TRACING; }
    
    // Enables or disables trace mode (T0 flag)
    void setTrace0Flag() { reg.sr.t0 = true; }
    void clearTrace0Flag() { reg.sr.t0 = false; }
    
    // Disables both trace flags (T0 and T1)
    void clearTraceFlags() { clearTraceFlag(); clearTrace0Flag(); }
    
    
    //
    // Register Access
    //
    
protected:
    
    // Reads a value from a data register (D0, D1 ... D7)
    template<Size S = Long> u32 readD(int n) const;
    
    // Reads a value from an address register (A0, A1 ... A7)
    template <Size S = Long> u32 readA(int n) const;
    
    // Reads a value from a register (D0, D1 ... D7, A0, A1 ... A7)
    template <Size S = Long> u32 readR(int n) const;
    
    // Writes a value to a data register (D0, D1 ... D7)
    template <Size S = Long> void writeD(int n, u32 v);
    
    // Writes a value to an address register (A0, A1 ... A7)
    template <Size S = Long> void writeA(int n, u32 v);
    
    // Writes a value to a register (D0, D1 ... D7, A0, A1 ... A7)
    template <Size S = Long> void writeR(int n, u32 v);

    
    //
    // Instruction Analysis
    //
    
public:
    
    // Retrieves the availability mask for a given instruction
    u16 availabilityMask(Instr I) const;
    u16 availabilityMask(Instr I, Mode M, Size S) const;
    u16 availabilityMask(Instr I, Mode M, Size S, u16 ext) const;
    
    // Checks if a given CPU model supports a specific instruction
    bool isAvailable(Model model, Instr I) const;
    bool isAvailable(Model model, Instr I, Mode M, Size S) const;
    bool isAvailable(Model model, Instr I, Mode M, Size S, u16 ext) const;
    
    
    //
    // Extension Word Validation
    //

private:

    // Validates extension words for a given instruction
    bool isValidExt(Instr I, Mode M, u16 op, u32 ext) const;
    
    // Validates extension words for MMU-related instructions
    bool isValidExtMMU(Instr I, Mode M, u16 op, u32 ext) const;
    
    // Validates extension words for FPU-related instructions
    bool isValidExtFPU(Instr I, Mode M, u16 op, u32 ext) const;


    //
    // 68030 MMU instruction support (POM68K O4, MoiraExecMMU_cpp.h)
    //
    // Two-oracle arbitrated (2026-07-15): WinUAE (oracle/uae, primary) +
    // Musashi (oracle/musashi); rulings in oracle/fuzz/disputes/NOTES.md.
    // Manual references MC68030UM § 9. Physical bus accesses (table
    // walks) go through the raw read16/write16 interface.
    //

private:

    // Raw 32-bit physical bus access (table walk / descriptor update)
    u32 mmuRead32(u32 addr) const;
    void mmuWrite32(u32 addr, u32 val) const;

    // FC field decoding of PTEST/PFLUSH/PLOAD extension words (§ 9.7.6);
    // false = undecodable field (bits 4-3 = 11) → Line-F
    bool mmuFCFromModes(u16 modes, int &fc) const;

    // Transparent translation register match (§ 9.5.4)
    bool mmuMatchTT(u32 tt, u32 addr, int fc, int rw, u16 &sr) const;

    // MMUSR bit accumulation from a fetched descriptor (§ 9.7.2.6)
    void mmuUpdateSR(int type, u32 entry, int fc, bool isLong, u16 &sr) const;

    // History (U) / modified (M) descriptor maintenance in RAM (§ 9.5.3.5)
    void mmuUpdateDescriptor(u32 tptr, int type, u32 entry, int rw) const;

    // Translation-table walk (§ 9.5.3); returns true when resolved
    bool mmuWalkTables(u32 addrIn, int type, u32 table, int fc, int limit,
                       int rw, bool ptest, u32 &addrOut, u16 &sr) const;

    // Root pointer selection for a walk (SRP when SRE + supervisor FC)
    void mmuRootPointer(int fc, u32 &table, int &type) const;

    // EA decode for the $F000-$F03F window (WinUAE MMUOP030 order:
    // computed before validation, side effects survive Line-F)
    template <Core C, Mode M> u32 mmuDecodeEA(int n);

    // MMU configuration exception (vector 56, format 0 frame)
    template <Core C> void execMmuConfigError();


    //
    // 68030 MMU bus layer (POM68K O4 slice 3, MoiraExecMMU_cpp.h)
    //
    // Address translation on every bus access when cpuModel == M68030 and
    // TC.E is set (MC68030UM § 9.5), modeled byte-for-byte on the primary
    // oracle, WinUAE cpummu030.c (hatari e77819f7) — including the 22-entry
    // ATC, the format $A/$B bus-fault frames and their internal-state words
    // (access log, SSW, pending register fixups). All members and hooks are
    // compiled out of the Core::C68000/C68010 template instantiations; the
    // Mac Plus path is untouched. See POM68K_VENDOR.md § MMU bus layer.
    //

public:

    // POM68K O6 (LC II): external /BERR. A memory-system bus callback
    // (read8/16, write8/16) calls this when the machine flags the current
    // physical (sub-)access as a bus error — unmapped I/O (the LC II ROM's
    // address-map probe relies on it) or the SCSI pseudo-DMA DRQ timeout.
    // M68030 model only: funnels into mmuPageFault so the stacked $A/$B
    // frame is exactly what a translation fault at the same point would
    // produce. Throws MmuBusError (never returns); the per-instruction
    // handler turns it into a vector-2 exception, a nested fault → HALT.
    [[noreturn]] void extBusError();

protected:

    // Address translation cache — 22 fully-associative entries with a
    // pseudo-LRU history bit (§ 9.5.2; WinUAE MMU030_ATC_LINE)
    struct MmuAtcEntry {
        u32 logical;            // page-aligned logical address
        u32 physical;           // page-aligned physical address
        u8  fc;                 // function code (exact match)
        bool valid;
        bool busError;          // invalid / supervisor-only page
        bool writeProtect;
        bool modified;
        bool cacheInhibit;
        bool mru;               // history bit
    };
    static constexpr int MMU_ATC_ENTRIES = 22;
    MmuAtcEntry mmuAtcArr[MMU_ATC_ENTRIES] {};
    // POM68K perf (2026-07-17): O(1) pseudo-LRU + last-hit probe. The
    // profile showed 38% of LC II time inside the two 22-entry scans run
    // on EVERY memory access (mmuAtcLookup + mmuAtcTouch). mruCount
    // mirrors the number of set history bits (every transition goes
    // through mmuAtcTouch or the resets); mmuAtcLast[fc][rw] remembers
    // the line of the previous hit so page-local access streams probe
    // one entry instead of scanning — semantics and LRU updates are
    // identical (the probe runs the same checks and the same touch).
    int mmuAtcMruCount {0};
    i8  mmuAtcLast[8][2] {};

    // Per-instruction restart/fault bookkeeping (WinUAE globals in
    // comments). Values land verbatim in the $A/$B frame internal words.
    u16 mmuState[3] {};         // mmu030_state[0..2] (MOVEM count, flags, EA words)
    u32 mmuAd[10] {};           // mmu030_ad[].val — completed-access value log
    u32 mmuFmovemStore[2] {};   // mmu030_fmovem_store — first two longs of the
                                // FMOVEM register in flight (O5: $B-frame slots)
    int mmuIdx {};              // mmu030_idx — accesses attempted
    int mmuIdxDone {};          // mmu030_idx_done — accesses completed
    u32 mmuDataBuffer {};       // mmu030_data_buffer_out
    u32 mmuDispStore[2] {};     // mmu030_disp_store (extended-EA results)
    u32 mmuOpcodeV {0xFFFFFFFF};// mmu030_opcode ($FFFFFFFF = prefetch phase)
    u8  mmuFixupReg[2] {};      // mmufixup encodings (wb2/wb3 status bytes)
                                // O5: bit 7 = plain (FPU) fixup — restore
                                // active but the frame status byte reads 0
                                // (WinUAE mmu030fixupreg returns 0 when the
                                // fpp.c-armed reg lacks the 0x300 flags)
    u32 mmuFixupVal[2] {};      // mmufixup .value (An before adjustment)
    bool mmuLogging {};         // instruction-level accesses feed mmuAd
    bool mmuRmw {};             // islrmw030 — inside a locked RMW cycle
    u16 mmuCcrSave {};          // CCR at instruction start ($B fault restore)
    u32 mmuLastWritePc {};      // next-instruction PC for $A (last-write) faults
    // POM68K O6 (LC II): 68030 prefetch-pipe carry-over across a
    // translation change. A PMOVE to TC/CRP/SRP captures the next words
    // through the STILL-CURRENT mapping; mmuFetchWord serves them while
    // execution stays linear and drops them on the first jump. The real
    // 030 pipe holds ~3 words fetched pre-switch — Apple's ROMs bank on
    // it ("pmove tc; nop; bne; jmp (An)", LC II ROM $A416AA-$A416B6);
    // a start-of-instruction refetch through the NEW map reads garbage.
    u32 mmuPipeAddr {};
    u16 mmuPipe[4] {};
    int mmuPipeCnt {};

    // POM68K O6 (LC II): context of the physical (sub-)access in flight,
    // recorded by mmuTranslateAccess/mmuFetchWord before every read8/16
    // and write8/16 on the M68030 model — extBusError() replays it into
    // mmuPageFault when the machine asserts /BERR from a bus callback
    u32 mmuAccAddr {};
    u32 mmuAccSsw {};
    u8  mmuAccFc {};
    bool mmuAccWrite {};
    // POM68K O6.9: RTE of a $B fault frame whose SSW DF bit was cleared
    // by the handler (bit 9 = "frame carried DF" marker, per WinUAE's
    // encoding, survives; bit 8 cleared) — the faulted data cycle must
    // NOT be re-run. Moira re-executes the faulting instruction, so a
    // one-shot latch completes the retried access without a bus cycle:
    // a read yields the frame's data input buffer, a write is skipped
    // (WinUAE m68k_do_rte_mmu030 "DF not set: mark access as done").
    // This is Mac OS's slot-probe recovery protocol (bclr #0 on the
    // stacked SSW high byte, then RTE).
    bool mmuRteSubstArmed {};
    bool mmuRteSubstWrite {};
    u32 mmuRteSubstAddr {};
    u32 mmuRteSubstData {};     // stacked data input buffer
    u32 mmuRteSubstPc {};       // stacked PC (the faulting instruction)

    // Fault capture, valid while a MmuBusError unwinds (mmu030_page_fault)
    u32 mmuFaultAddr {};
    u16 mmuSsw {};
    u32 mmuStageB {};           // mm030_stageb_address
    u32 mmuWb3Data {};          // regs.wb3_data (pending write value)
    u16 mmuWb2Address {};       // regs.wb2_address (= mmuState[1] at fault)
    u16 mmuWb2Status {};        // regs.wb2_status (fixup 0 encoding)
    u16 mmuWb3Status {};        // regs.wb3_status (fixup 1 encoding)

private:

    // True when bus-level translation is active
    bool mmuActive() const {
        return cpuModel == Model::M68030 && (reg.tc & 0x80000000);
    }

    // Page masks derived from TC.PS (§ 9.7.2.2)
    u32 mmuPageMask() const { return (u32(1) << (reg.tc >> 20 & 0xf)) - 1; }

    // Per-instruction state reset + mode-5-style opcode fetch; returns
    // false when the fetch faulted (exception already processed)
    template <Core C> bool mmuExecuteStart();

    // Instruction-stream word fetch (translated, unlogged, never split)
    u16 mmuFetchWord(u32 addr);

    // Captures the prefetch pipe before a translation change (see the
    // mmuPipe members above); never faults — a short capture just ends
    void mmuCapturePipe();

    // Logs the consumption of an extension word (WinUAE next_iword_state)
    void mmuLogExtWord(u32 value);

    // Arms a pending (An)+/-(An) fixup (WinUAE mmufixup[])
    template <Size S> void mmuArmFixup(int n, bool predec);

    // Transparent translation match for bus accesses (OK-match only)
    bool mmuMatchTTAccess(u32 addr, u8 fc, bool write) const;

    // Bus-level table search (WinUAE mmu030_table_search, level 0):
    // U/M maintenance, limit checks; returns MMUSR-style status bits
    u16 mmuBusWalk(u32 addr, u8 fc, bool write, u32 &pageAddr, bool &ci);

    // ATC operations (§ 9.5.2)
    // POM68K perf (callgrind 2026-07-17): lookup/touch/translate run on
    // EVERY memory access but GCC keeps them out of line (the 22-entry
    // fallback scan trips its size heuristics) — ~35 Ir/access of pure
    // call overhead, ~9% of the whole emulator. Everything lives in the
    // Moira.cpp TU, so forcing the inline changes no behaviour.
#if defined(__GNUC__) || defined(__clang__)
#define MOIRA_HOT_INLINE __attribute__((always_inline)) inline
#else
#define MOIRA_HOT_INLINE inline
#endif
    MOIRA_HOT_INLINE int  mmuAtcLookup(u32 addr, u8 fc, bool write);
    void mmuAtcFill(u32 addr, u8 fc, bool write);
    MOIRA_HOT_INLINE void mmuAtcTouch(int i);
    void mmuAtcFlushAll();
    void mmuAtcFlushFc(u32 fcBase, u32 fcMask);
    void mmuAtcFlushPage(u32 addr);
    void mmuAtcFlushPageFc(u32 addr, u32 fcBase, u32 fcMask);

protected:
    // POM68K save states: restoring a snapshot replaces RAM (and therefore
    // the page tables) underneath a CPU whose ATCs still cache the OLD
    // translations, so both have to be dropped. The flushes are private and
    // no public setter reaches the 030 one (setURP040 covers only the 040),
    // hence this one-line seam for src/MoiraSnapshot.h. Flushing the set the
    // current model does not use is free — it is already empty.
    void pomFlushAtcs() { mmuAtcFlushAll(); mmu040AtcFlushAll(); }

private:

    // Logical → physical for one bus (sub-)access; throws MmuBusError
    MOIRA_HOT_INLINE u32 mmuTranslateAccess(u32 addr, u8 fc, bool write, u32 sswFlags);

    // Fault capture + throw (WinUAE mmu030_page_fault)
    [[noreturn]] void mmuPageFault(u32 addr, bool read, u32 sswFlags, u8 fc);

    // Translated read/write with 68030 bus splitting + access log
    template <Core C, Size S, Flags F> u32 mmuRead(u32 addr);
    template <Core C, Size S, Flags F> void mmuWrite(u32 addr, u32 val);

    // Bus-fault exception processing (frame $A/$B + vector 2)
    template <Core C> void execMmuBusError();
    template <Core C> void writeStackFrameShortBusFault(u16 sr, u32 pc);
    template <Core C> void writeStackFrameLongBusFault(u16 sr, u32 pc, int nr = 2);

    // POM68K O4 slice 4 — 68030 odd-address instruction-fetch fault
    // (WinUAE Exception_mmu030 vector 3: format $B frame, SSW $0066).
    // mmuCheckOddPc raises it and returns true when `target` is odd.
    template <Core C> void execAddressError030(u32 target, u32 stackedPc);
    template <Core C> bool mmuCheckOddPc(u32 target, u32 stackedPc);

    // POM68K Q2: 68040 odd instruction-flow target — vector 3, format $2
    // frame (WinUAE Exception_mmu nr == 3). Call sites apply the WinUAE
    // per-instruction A7/CCR conventions first (see the exec handlers).
    template <Core C> void execAddressError040(u32 target, u32 stackedPc);

    //
    // POM68K Q3 — 68040 MMU bus translation (WinUAE cpummu.c, mmu040)
    //

public:

    // Fault capture for the format $7 frame, plus the MOVEM restart
    // latch (WinUAE mmu040_movem) and the MOVE16 line buffer (frame
    // PD0-3). mmu040EffAddr mirrors WinUAE regs.mmu_effective_addr:
    // only MOVEM/MOVE16 faults set it (reset per instruction — the
    // oracle's per-vector state load zeroes it).
    u32 mmu040FaultAddr {};
    u32 mmu040EffAddr {};
    u16 mmu040Ssw {};
    u32 mmu040Wb3Data {};
    u16 mmu040Wb3Status {}, mmu040Wb2Status {};
    u32 mmu040Wb2Address {};
    u32 mmu040Move16[4] {};
    bool mmu040MovemArmed {};
    u32 mmu040MovemEa {};

private:

    // Context of the (possibly page-split) access in flight — later
    // sub-accesses fault with FA = the base address and SSW.MA set
    // (WinUAE misalignednotfirst); split write faults report the FULL
    // value in WB3D (the unaligned CATCH overwrites wb3_data)
    u32 mmu040AccBase {};
    bool mmu040AccFirst {true};
    bool mmu040AccSplit {};
    u32 mmu040FullVal {};
    u8 mmu040CcrSave {};

    // Last-write dichotomy (gencpu gen_set_fault_pc, mmu040 build): a
    // fault on the instruction's LAST write stacks the NEXT instruction
    // and skips every restore (the OS completes the write from WB3);
    // any other fault restarts (CCR + (An)± fixups + PC = instruction).
    bool mmu040LastWrite {};
    u32 mmu040LastWritePc {};

    // MOVES fc override (WinUAE ismoves: SFC/DFC drive the translation
    // and mangle the SSW fc bits); -1 = inactive
    int mmu040Moves {-1};

    // TAS/CAS locked RMW: reads translate as WRITES (TTR write check +
    // M-bit walk) and faults carry SSW.LK with RW cleared
    bool mmu040Lrmw {};

public:

    // POM68K Q5 — external /BERR entry for the 68040 machine (the O6
    // extBusError twin): called from INSIDE a read8/16 / write8/16 bus
    // callback when the machine asserts /BERR (unmapped I/O). Replays
    // the captured in-flight access into mmu040Fault with the ATC bit
    // CLEAR (WinUAE mmu_hardware_bus_error, nonmmu = true).
    [[noreturn]] void extBusError040();

private:

    // In-flight access context for extBusError040
    u32 mmu040AccAddr {};
    u32 mmu040AccVal {};
    int mmu040AccSz {2};
    bool mmu040AccWrite {};
    bool mmu040AccData {true};

    bool mmu040Enabled() const { return (reg.tc040 & 0x8000) != 0; }
    u32 mmu040PageMaskI() const { return (reg.tc040 & 0x4000) ? 0xFFFFE000 : 0xFFFFF000; }

    // POM68K Q8: separate instruction/data ATCs (M68040UM §3.3). 32 fully
    // associative lines each with pseudo-LRU; page size follows TC.P
    // (4K/8K). Global bit G ($400) is honoured by PFLUSHN/PFLUSHAN.
    // Behaviour matches walk-per-access for U/M/WP (write hits on
    // unmodified pages re-walk). POM68K_MMU040_WALK disables the overlay.
    struct Mmu040AtcEntry {
        u32 logical = 0;            // page-aligned logical
        u32 physical = 0;           // page-aligned physical
        u32 status = 0;             // descriptor status (WP|G|S|CM|M|R…)
        bool valid = false;
        bool mru = false;
    };
    static constexpr int MMU040_ATC_ENTRIES = 32;
    Mmu040AtcEntry mmu040AtcI[MMU040_ATC_ENTRIES] {};
    Mmu040AtcEntry mmu040AtcD[MMU040_ATC_ENTRIES] {};
    int mmu040AtcMruI {0}, mmu040AtcMruD {0};
    i8  mmu040AtcLastI[2] {};       // [write]
    i8  mmu040AtcLastD[2] {};
    bool mmu040AtcArmed {true};

    void mmu040AtcFlushAll();
    void mmu040AtcFlushNonGlobal();
    void mmu040AtcFlushPage(u32 addr, bool nonGlobalOnly);
    void mmu040AtcTouch(Mmu040AtcEntry* arr, int& mruCount, int i);
    int  mmu040AtcLookup(bool data, u32 addr, bool write);
    void mmu040AtcFill(bool data, u32 addr, u32 status);

    // Per-instruction loop head: translated opcode/irc fetch (mode-5
    // pattern); returns false when the fetch faulted
    template <Core C> bool mmu040InstrStart();

    // Transparent translation: 0 = no match, 1 = match, 2 = match + WP
    int mmu040MatchTTR(u32 addr, bool super, bool data) const;

    // 3-level table walk with U/M maintenance (WinUAE mmu_fill_atc);
    // returns the page descriptor with accumulated WP, or 0 if invalid
    template <Core C> u32 mmu040Walk(u32 addr, bool super, bool write);

    // Logical -> physical for one (sub-)access; throws MmuBusError
    template <Core C> u32 mmu040Translate(u32 addr, u32 val, bool super,
                                          bool data, bool write, int szCode);

    // Fault capture + throw (WinUAE mmu_bus_error, 68040 branch);
    // szCode: 0 = byte, 1 = word, 2 = long, 3 = MOVE16 line;
    // atc = false for external /BERR (nonmmu — SSW.ATC stays clear)
    template <Core C> [[noreturn]] void mmu040Fault(u32 addr, u32 val, int fc,
                                                    bool write, int szCode,
                                                    bool atc = true);

    // Translated read/write with 68040 page-boundary splitting
    template <Core C, Size S> u32 mmu040Read(u32 addr, bool data);
    template <Core C, Size S> void mmu040Write(u32 addr, u32 val, bool data);

    // MOVE16 line transfer through translation (size-16 fault reports)
    template <Core C> void mmu040GetMove16(u32 addr);
    template <Core C> void mmu040PutMove16(u32 addr);

    // Bus-fault exception processing (format $7 frame + vector 2)
    template <Core C> void execMmu040BusError();

    // Rebuilds the per-instruction access log so a fault frame stacks
    // exactly what WinUAE's get_iword/get_long_mmu030_state logged
    void mmuLogReset() { mmuIdx = mmuIdxDone = 0; for (auto &v : mmuAd) v = 0; }


    //
    // Disassembler Support
    //
    
private:
    
    // Returns an availability string for a given instruction
    const char *availabilityString(Instr I, Mode M, Size S, u16 ext);

    
    //
    // Loop Mode Detection
    //
    
    // Checks if an instruction is a loop mode instruction
    template <Instr I> constexpr bool looping() {
        return I >= Instr::ABCD_LOOP && I <= Instr::TST_LOOP;
    }
    
    
    //
    // Managing the Function Code Pins
    //
    
public:
    
    // Reads the current value of the function code pins
    u8 readFC() const;
    
private:
    
    // Sets the function code pins to a specific value
    void setFC(u8 value);
    
    // Sets the function code pins based on the provided addressing mode.
    template <Mode M> void setFC();

    
    //
    // Handling Interrupts
    //
    
public:
    
    // Retrieves the value on the Interrupt Priority Level (IPL) pins
    u8 getIPL() const { return ipl; }
    
    // Sets the value on the Interrupt Priority Level (IPL) pins
    void setIPL(u8 val);

    // NEOST_IPLFETCH : règle les seuils (cyc) de reconnaissance IPL différée.
    // d4 == 0 désactive la feature (POLL_IPL ≡ reg.ipl = ipl, défaut).
    void setIplDelay(i64 d4, i64 d2) { iplDelay4 = d4; iplDelay2 = d2; }

    // NEOST_IPLFETCH : applique la règle d'échantillonnage de l'IPL (≡ POLL_IPL).
    void pollIpl();

private:
    
    // Selects the IRQ vector based on the interrupt level
    u16 getIrqVector(u8 level) const;
    
private:
    
#include "MoiraInit.h"
#include "MoiraALU.h"
#include "MoiraDataflow.h"
#include "MoiraExceptions.h"
#include "MoiraDasm.h"
};

}
