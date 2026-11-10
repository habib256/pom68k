// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The same semantic-slot ATC transaction used by the AArch64 generator.
void Emitter::beginAtcInstruction040(size_t i) {
    atcContext040_ = false;
    atcRoutes040_ = 0;
    if (L_.is030 || !memoryContractMask(ir_.instrs[i].memory)) return;
    a_.movMI(Sz::Q, F(kFAtc040), 0);
    a_.movMI(Sz::Q, F(kFAtc040 + 8), 0);
    if (ir_.instrs[i].fetchWords040) {
        a_.movzx(Sz::B, R8, at(L_.atcWindow040));
        a_.movRI(R9, ir_.instrs[i].fetchWords040); a_.imulRR(R8, R9);
        a_.movMR(Sz::L, F(kFAtcFetch040), R8);
    }
    // MC68040UM-1993 §4.1: the I-cache stream count waits here too, so an
    // exact thunk commits it before its own I-side accesses.
    if (L_.cache040Live) {
        a_.movMI(Sz::L, F(kFAtcCacheFetches040), int32_t(ir_.instrs[i].fetchWords040 & 3));
        a_.movMI(Sz::B, F(kFAtcCacheSaved040), 0);
    }
    atcContext040_ = true;
}

void Emitter::recordAtcRoute040(AtcRoute route, Reg entry, bool cache) {
    if (L_.is030 || !route.mask || !route.weight) return;
    atcRoutes040_ |= route.mask;
    a_.movzx(Sz::B, R9, mem(entry, cache ? 13 : 6));
    if (route.weight != 1) {
        a_.movRI(R8, route.weight); a_.imulRR(R9, R8);
    }
    for (unsigned i = 0; i < AtcReplay040::Accesses; ++i)
        if (route.mask & (1u << i)) a_.movMR(Sz::B, F(kFAtcRoutes040 + int32_t(i)), R9);
}

// Moira splits a three-byte memory bit field into a word and a byte: two
// data-ATC accesses per direction where the longword proof span recorded
// one (Moira::mmu040AtcLookup counts every access). Clobbers R9 only.
void Emitter::doubleAtcRoute040(uint8_t mask) {
    if (L_.is030 || !(atcRoutes040_ & mask)) return;
    for (unsigned i = 0; i < AtcReplay040::Accesses; ++i)
        if (mask & (1u << i)) {
            a_.movzx(Sz::B, R9, F(kFAtcRoutes040 + int32_t(i)));
            a_.aluRR(Asm::Op::ADD, Sz::L, R9, R9);
            a_.movMR(Sz::B, F(kFAtcRoutes040 + int32_t(i)), R9);
        }
}

void Emitter::prepareAtcThunk040(unsigned access, Reg argument) {
    if (L_.is030) { a_.movRI64(argument, 0); return; }
    a_.movMI(Sz::L, F(kFAtcAccess040), int32_t(access));
    a_.leaRM(argument, F(kFAtc040));
}

// The deferred D-cache delta of the cache-only probes, once their hits are
// published (MC68040UM-1993 §4.1). Clobbers R8.
void Emitter::commitDeferredCacheRot040() {
    a_.movRM(Sz::L, R8, F(kFCacheRotD));
    a_.aluMR(Asm::Op::ADD, Sz::B, at(L_.cache040DRot), R8);
    a_.aluMI(Asm::Op::AND, Sz::B, at(L_.cache040DRot), 3);
}

void Emitter::commitAtcInstruction040() {
    if (L_.is030) return;
    // Natively executed code bypasses the I-cache reads Moira counts, one
    // per fetched word (Moira::pomJitFetch, MoiraCache040.h). Memory-only:
    // no register is live-assumed here.
    const unsigned fetchWords = fetchPath040_ >= 0
        ? unsigned(fetchPath040_) : ir_.instrs[cur_].fetchWords040;
    if (L_.cache040Live) {
        if (!atcContext040_) {
            if (fetchWords & 3) {
                a_.aluMI(Asm::Op::ADD, Sz::B, at(L_.cache040IRot), int32_t(fetchWords & 3));
                a_.aluMI(Asm::Op::AND, Sz::B, at(L_.cache040IRot), 3);
            }
        } else {
            // What no exact thunk committed yet (pomJitAtcReplayBegin040).
            a_.movRM(Sz::L, R8, F(kFAtcCacheFetches040));
            a_.aluMR(Asm::Op::ADD, Sz::B, at(L_.cache040IRot), R8);
            a_.aluMI(Asm::Op::AND, Sz::B, at(L_.cache040IRot), 3);
            a_.movMI(Sz::L, F(kFAtcCacheFetches040), 0);
            a_.movMI(Sz::B, F(kFAtcCacheSaved040), 0);
        }
    }
    if (!atcContext040_) {
        const auto count = fetchWords;
        if (!count) return;
        Label& identity = *a_.fresh();
        a_.aluMI(Asm::Op::CMP, Sz::B, at(L_.atcWindow040), 0);
        a_.jcc(Cc::E, identity);
        a_.aluMI(Asm::Op::ADD, Sz::L, at(L_.atcAccessI040), count);
        a_.bind(identity);
        return;
    }
    a_.movRM(Sz::L, R8, F(kFAtcFetch040));
    a_.aluMR(Asm::Op::ADD, Sz::L, at(L_.atcAccessI040), R8);
    a_.movRM(Sz::L, R8, F(kFAtcRoutes040));
    a_.movRI(R9, 0x01010101); a_.imulRR(R8, R9);
    a_.shiftRI(Sz::L, R8, 5, 24);
    a_.aluMR(Asm::Op::ADD, Sz::L, at(L_.atcAccessD040), R8);
    a_.movMI(Sz::Q, F(kFAtc040), 0); a_.movMI(Sz::Q, F(kFAtc040 + 8), 0);
}
