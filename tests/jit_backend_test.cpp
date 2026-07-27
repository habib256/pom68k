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

    jit::Backend* autoPick = jit::selectBackend("auto");
    check(autoPick != nullptr, "auto selection never returns null");
    check(autoPick->usable(), "the selected backend reports itself usable");
    std::printf("  auto -> %s (%s)\n", autoPick->name(), autoPick->description());

    jit::Backend* threaded = jit::selectBackend("threaded");
    check(threaded != nullptr && !std::strcmp(threaded->name(), "threaded"),
          "explicit 'threaded' resolves");
    check(threaded->usable(), "'threaded' is usable everywhere — it is the floor");
    check(!threaded->caps().nativeCode, "'threaded' generates no host code");

    jit::Backend* bogus = jit::selectBackend("no-such-backend");
    check(bogus != nullptr, "an unknown name still yields a backend");
    check(bogus->usable(), "the fallback is usable");

    std::printf("[jit_backend] executable memory (W^X)\n");
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
        check(!buf.writable(), "no longer writable while executable");
        check(buf.alloc(16) == nullptr, "alloc refused while executable");
        check(buf.makeWritable(), "X -> W");
        check(buf.writable(), "writable again");
        buf.release();
        check(!buf.valid(), "release");
    }

    std::printf("[jit_backend] block classifier\n");
    // These MUST end a block: they can change the MMU, a cache or the
    // supervisor bit, which would silently stale the code window.
    checkUnsafe(0x4E73, "RTE");
    checkUnsafe(0x4E75, "RTS");
    checkUnsafe(0x4E72, "STOP");
    checkUnsafe(0x4E70, "RESET");
    checkUnsafe(0x4E7A, "MOVEC from control register");
    checkUnsafe(0x4E7B, "MOVEC to control register");
    checkUnsafe(0x4EB9, "JSR abs.l");
    checkUnsafe(0x4ED0, "JMP (A0)");
    checkUnsafe(0x46C0, "MOVE to SR");
    checkUnsafe(0x40C0, "MOVE from SR");
    checkUnsafe(0x44C0, "MOVE to CCR");
    checkUnsafe(0x007C, "ORI to SR");
    checkUnsafe(0x027C, "ANDI to SR");
    checkUnsafe(0x0A3C, "EORI to CCR");
    checkUnsafe(0x0E00, "MOVES");
    checkUnsafe(0x4AC0, "TAS (locked RMW)");
    checkUnsafe(0x484A, "BKPT");
    checkUnsafe(0x6000, "BRA");
    checkUnsafe(0x67FE, "BEQ");
    checkUnsafe(0x51C8, "DBcc");
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
