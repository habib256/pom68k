// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// JIT gate: the portability seam. No emulated machine, no assets — this
// checks the things that decide whether the JIT exists at all on a given
// host (src/jit/POM68K_JIT.md invariants 6 and 7):
//
//   * backend selection, including that `auto` and an unknown name both
//     land on the portable `threaded` floor;
//   * the W^X executable-memory allocator, or a clean refusal on a platform
//     that has none (Emscripten, iOS, a hardened kernel);
//   * the block classifier's safety rules — the ones that keep MMU, cache
//     and supervisor-state instructions out of a block.

#include "jit/JitBackend.h"
#include "jit/JitCodeBuffer.h"
#include "jit/JitIr.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL: %s\n", what); failures++; }
}

void checkUnsafe(uint16_t op, const char* what) {
    check(jit::classify(op) == jit::Kind::Unsafe, what);
}

void checkSafe(uint16_t op, const char* what) {
    check(jit::classify(op) != jit::Kind::Unsafe, what);
}

}  // namespace

int main() {
    std::printf("[jit_backend] backend registry\n");

    int count = 0;
    const char* const* names = jit::backendNames(count);
    check(count >= 1, "at least one backend is compiled in");
    bool hasThreaded = false;
    for (int i = 0; i < count; i++) {
        std::printf("  compiled: %s\n", names[i]);
        if (!std::strcmp(names[i], "threaded")) hasThreaded = true;
    }
    check(hasThreaded, "the portable 'threaded' backend is always compiled in");

    // Selection is per GUEST family as well as per host: a code generator
    // written against one 68k family is wrong on another, not merely slower
    // (JitBackend.h § GuestFamily). These calls therefore name the guest.
    jit::Backend* autoPick = jit::selectBackend("auto", jit::kGuest68040);
    check(autoPick != nullptr, "auto selection never returns null");
    check(autoPick->usable(), "the selected backend reports itself usable");
    std::printf("  auto (68040) -> %s (%s)\n",
                autoPick->name(), autoPick->description());

    jit::Backend* threaded = jit::selectBackend("threaded", jit::kGuest68040);
    check(threaded != nullptr && !std::strcmp(threaded->name(), "threaded"),
          "explicit 'threaded' resolves");
    check(threaded->usable(), "'threaded' is usable everywhere — it is the floor");
    check(!threaded->caps().nativeCode, "'threaded' generates no host code");

    jit::Backend* bogus = jit::selectBackend("no-such-backend", jit::kGuest68040);
    check(bogus != nullptr, "an unknown name still yields a backend");
    check(bogus->usable(), "the fallback is usable");

    // ── Guest-family scope (the jit_lcii_boot_etalon timeout, 2026-07-30) ──
    // The x86-64 generator is written against the 68040's instruction-boundary
    // contract; handed the 68030 LC II it wedged the guest in the ROM's Egret
    // handshake loop for a full hour. Selection must refuse that combination
    // instead of shipping it, and `threaded` must remain valid for every
    // family so `auto` always has a correct floor.
    std::printf("[jit_backend] guest-family scope\n");
    check((threaded->caps().guestFamilies & jit::kGuestAny) == jit::kGuestAny,
          "'threaded' declares every guest family — it replays Moira's handlers");
    for (uint32_t fam : { jit::kGuest68000, jit::kGuest68020,
                          jit::kGuest68030, jit::kGuest68040 }) {
        jit::Backend* b = jit::selectBackend("auto", fam);
        check(b != nullptr && (b->caps().guestFamilies & fam) != 0,
              "auto never returns a backend invalid for the guest it was asked about");
    }
    // The concrete regression: the x86-64 generator is 68040-only, so asking
    // for it on a 68030 must NOT come back as x64. Before 2026-07-30 it did,
    // and jit_lcii_boot_etalon spent an hour wedged proving it.
    if (jit::CodeBuffer::supported()) {
        jit::Backend* on030 = jit::selectBackend("x64", jit::kGuest68030);
        check(std::strcmp(on030->name(), "x86-64") != 0,
              "x64 requested for a 68030 guest is refused, not honoured");
        jit::Backend* on040 = jit::selectBackend("x64", jit::kGuest68040);
        check(!std::strcmp(on040->name(), "x86-64"),
              "…and it is still served for the 68040 it was written for");
    }

    // Every backend compiled in must DECLARE a scope: the caps field defaults
    // to 0 = undeclared so a new backend cannot inherit a silent "works
    // everywhere", and an undeclared one resolves to `threaded` on every
    // family. NOTE the keys, not the display names — selectBackend() matches
    // on "x64", never on "x86-64", and using the wrong list here made this
    // very check a no-op on its first outing.
    int keyCount = 0;
    const char* const* keys = jit::backendKeys(keyCount);
    check(keyCount == count, "one registry key per compiled backend");
    for (int i = 0; i < keyCount; i++) {
        bool anyFamily = false;
        for (uint32_t fam : { jit::kGuest68000, jit::kGuest68020,
                              jit::kGuest68030, jit::kGuest68040 }) {
            jit::Backend* b = jit::selectBackend(keys[i], fam);
            if (!std::strcmp(b->name(), names[i])) anyFamily = true;
        }
        char msg[128];
        std::snprintf(msg, sizeof msg,
                      "backend '%s' declares at least one guest family", keys[i]);
        check(anyFamily, msg);
    }

    std::printf("[jit_backend] executable memory\n");
    if (!jit::CodeBuffer::supported()) {
        // A legitimate outcome, not an error: this host cannot give us
        // executable pages, so `auto` must have chosen a non-generating
        // backend — which the checks above already proved usable.
        std::printf("  no executable memory on this host — code generators "
                    "are unavailable, portable backend stands in\n");
        check(!autoPick->caps().nativeCode,
              "a code-generating backend was NOT selected without W^X memory");
    } else {
        jit::CodeBuffer buf;
        check(buf.reserve(4096), "reserve");
        check(buf.valid() && buf.capacity() >= 4096, "capacity");
        check(buf.writable(), "starts writable");
        uint8_t* p = buf.alloc(32, 16);
        check(p != nullptr, "alloc");
        check((reinterpret_cast<uintptr_t>(p) & 15) == 0, "alignment honoured");
        if (p) std::memset(p, 0x90, 32);
        check(buf.alloc(1u << 30) == nullptr, "over-allocation is refused");
        check(buf.makeExecutable(), "W -> X");
        if (buf.unified()) {
            // The kernel granted one RWX mapping, so the transitions are
            // no-ops by design: a code generator that flipped protection per
            // block would pay an mprotect PAIR per block, which measured as
            // the largest single cost in the whole backend.
            std::printf("  (unified RWX mapping — W/X transitions are no-ops)\n");
            check(buf.writable(), "stays writable");
            check(buf.alloc(16) != nullptr, "alloc still served");
        } else {
            check(!buf.writable(), "no longer writable while executable");
            check(buf.alloc(16) == nullptr, "alloc refused while executable");
        }
        check(buf.makeWritable(), "X -> W");
        check(buf.writable(), "writable again");
        buf.release();
        check(!buf.valid(), "release");
    }

    // What the ACTIVE backend claims it can turn into host code. On a host
    // with no code generator every answer is false and this section simply
    // records that — the point is that the census and the block builder
    // agree with the backend actually selected.
    {
        // Name the code generator rather than taking `auto`: auto's answer
        // is a measured performance choice (JitBackend.cpp), and this
        // section is about what the generator CAN do, not what ships.
        jit::Backend* b = jit::selectBackend("x64", jit::kGuest68040);
        std::printf("[jit_backend] native coverage (%s)\n", b->name());
        const bool gen = b->caps().nativeCode;
        // The two opcodes a Mac ROM's hardware poll loops are built from.
        check(b->canEmit(0x082B) == gen, "BTST #imm,d16(A3)");
        check(b->canEmit(0x66F8) == gen, "BNE.S -8");
        check(b->canEmit(0x2ADC) == gen, "MOVE.L (A4)+,(A5)+");
        check(b->canEmit(0x7000) == gen, "MOVEQ #0,D0");
        check(b->canEmit(0xD3C1) == gen, "ADDA.L D1,A1");
        // …and forms no backend may claim: they are Unsafe, or they use an
        // addressing mode this generator does not decode.
        // LINK/UNLK/NOP are the $4Exx carve-out: no control transfer, no
        // SR/MMU/cache state, and 3.6 % of a real workload sitting at every
        // function entry and exit. They are compiled, and they no longer
        // end a block.
        check(b->canEmit(0x4E71) == gen, "NOP");
        check(b->canEmit(0x4E56) == gen, "LINK A6,#d16");
        check(b->canEmit(0x4E5E) == gen, "UNLK A6");
        check(jit::classify(0x4E71) != jit::Kind::Unsafe, "NOP does not end a block");
        check(jit::classify(0x4E56) != jit::Kind::Unsafe, "LINK does not end a block");
        check(jit::classify(0x4E5E) != jit::Kind::Unsafe, "UNLK does not end a block");
        // JSR/BSR/RTS are compiled as block TERMINATORS: 7 % of a real
        // workload, and each one used to be both an interpreter round trip
        // and a block boundary the linker could not cross.
        check(b->canEmit(0x4E75) == gen, "RTS");
        check(b->canEmit(0x4EB9) == gen, "JSR abs.l");
        check(b->canEmit(0x6100) == gen, "BSR");
        check(jit::endsBlockAfter(jit::classify(0x4E75)), "RTS terminates a block");
        check(jit::endsBlockAfter(jit::classify(0x4EB9)), "JSR terminates a block");
        check(jit::endsBlockAfter(jit::classify(0x6100)), "BSR terminates a block");
        check(jit::branchWords(0x4E75) == 1, "RTS is one word");
        check(jit::branchWords(0x4EB9) == 3, "JSR abs.l is three words");
        check(jit::branchWords(0x4EAE) == 2, "JSR d16(A6) is two words");
        // …while the rest of the $4Exx group still ends a block BEFORE it.
        check(!b->canEmit(0x4E73), "RTE is never native");
        check(b->canEmit(0x4ED0), "JMP (A0) is compiled (census 2026-07-30)");
        check(jit::branchWords(0x4EF9) == 3, "JMP abs.l is three words");
        check(b->canEmit(0x48E7), "MOVEM.L regs,-(SP) is compiled");
        check(b->canEmit(0x4CDF), "MOVEM.L (SP)+,regs is compiled");
        check(b->canEmit(0x51C8), "DBRA is compiled");
        check(!b->canEmit(0xF200), "F-line is never native");
        check(!b->canEmit(0x0130), "BTST Dn,d8(A0,Xn) — indexed mode");
        check(!b->canEmit(0x0108), "MOVEP is not BTST");
        check(!b->canEmit(0x81C0), "DIVU is not an ALU direction");
        check(!b->canEmit(0xC1C0), "MULS is not an ALU direction");
        check(!b->canEmit(0xC101), "ABCD is not OR-to-ea");
        check(!b->canEmit(0xB108), "CMPM is not EOR-to-ea");
    }

    std::printf("[jit_backend] block classifier\n");
    // These MUST end a block: they can change the MMU, a cache or the
    // supervisor bit, which would silently stale the code window.
    checkUnsafe(0x4E73, "RTE");
    checkUnsafe(0x4E76, "TRAPV");
    checkUnsafe(0x4E72, "STOP");
    checkUnsafe(0x4E70, "RESET");
    checkUnsafe(0x4E7A, "MOVEC from control register");
    checkUnsafe(0x4E7B, "MOVEC to control register");
    // JMP graduated from Unsafe to a Branch TERMINATOR (census 2026-07-30:
    // 0.7 % of the idle Finder) — for the plain EA modes only; the 68020
    // indexed modes keep their own decoder problem and stay Unsafe.
    check(jit::endsBlockAfter(jit::classify(0x4ED0)), "JMP (A0) terminates a block");
    check(jit::endsBlockAfter(jit::classify(0x4EF9)), "JMP (xxx).L terminates a block");
    checkUnsafe(0x4EF0, "JMP indexed stays Unsafe");
    // DBcc was already a terminator; the census pass made it EMITTABLE.
    check(jit::endsBlockAfter(jit::classify(0x51C8)), "DBRA terminates a block");
    checkUnsafe(0x46C0, "MOVE to SR");
    checkUnsafe(0x40C0, "MOVE from SR");
    checkUnsafe(0x44C0, "MOVE to CCR");
    checkUnsafe(0x007C, "ORI to SR");
    checkUnsafe(0x027C, "ANDI to SR");
    checkUnsafe(0x0A3C, "EORI to CCR");
    checkUnsafe(0x0E00, "MOVES");
    checkUnsafe(0x4AC0, "TAS (locked RMW)");
    checkUnsafe(0x484A, "BKPT");
    checkUnsafe(0x50FA, "TRAPcc");
    checkUnsafe(0xA000, "A-line");
    checkUnsafe(0xF000, "F-line (MMU / CINV / MOVE16 / FPU)");
    checkUnsafe(0xF518, "CINV");

    // These are ordinary straight-line code and must NOT end a block.
    checkSafe(0x2000, "MOVE.L D0,D0");
    checkSafe(0x3040, "MOVEA.W D0,A0");
    checkSafe(0x7000, "MOVEQ");
    checkSafe(0xD080, "ADD.L D0,D0");
    checkSafe(0x9080, "SUB.L");
    checkSafe(0xB080, "CMP.L");
    checkSafe(0xC080, "AND.L");
    checkSafe(0x0680, "ADDI.L");
    checkSafe(0x0C80, "CMPI.L");
    checkSafe(0x4280, "CLR.L D0");
    checkSafe(0x4A80, "TST.L D0");
    checkSafe(0x41F0, "LEA");
    checkSafe(0x48E7, "MOVEM");
    checkSafe(0xE188, "LSL.L");
    checkSafe(0x5280, "ADDQ.L");
    checkSafe(0x57C0, "Scc");

    // Branches are neither: they END a block, and are part of it. A
    // backend that can evaluate the condition and compute the target keeps
    // a loop inside generated code instead of returning to the engine at
    // every iteration — which is the whole reason J2 exists.
    check(jit::endsBlockAfter(jit::classify(0x6000)), "BRA terminates a block");
    check(jit::endsBlockAfter(jit::classify(0x67FE)), "BEQ terminates a block");
    check(jit::endsBlockAfter(jit::classify(0x51C8)), "DBcc terminates a block");
    check(!jit::endsBlock(jit::classify(0x6000)), "BRA is not Unsafe");
    check(jit::branchWords(0x67FE) == 1, "BEQ.B is one word");
    check(jit::branchWords(0x6700) == 2, "BEQ.W is two words");
    check(jit::branchWords(0x67FF) == 3, "BEQ.L is three words");
    check(jit::branchWords(0x51C8) == 2, "DBcc is two words");

    // Trap-capable but straight-line: allowed in a block, flagged for a
    // future code generator, and caught at replay by the pc check.
    check(jit::classify(0x80C0) == jit::Kind::Muldiv, "DIVU is Muldiv");
    check(jit::classify(0x4C40) == jit::Kind::Muldiv, "DIVL (020+) is Muldiv");
    check(jit::classify(0x4180) == jit::Kind::Muldiv, "CHK is trap-capable");
    check(jit::instrFlags(0x80C0, jit::Kind::Muldiv) & jit::FlagMayTrap,
          "Muldiv carries FlagMayTrap");

    if (failures) {
        std::printf("[jit_backend] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[jit_backend] OK\n");
    return 0;
}
