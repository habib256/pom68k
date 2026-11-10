// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ATC effects follow the same semantic access slots as the memory protocol.
// Probes stage provenance; retirement commits it. Exact thunks debit only
// earlier operands and roll back their private ATC transaction on replay.
thread_local const Instr* gAtcInstruction040 = nullptr;
thread_local bool gAtcContext040 = false;
thread_local uint8_t gAtcRoutes040 = 0;
// A branch path's own stream-access count (branchFetchWords040); -1 = the
// traced fetchWords040.
thread_local int gFetchPath040 = -1;
constexpr uint32_t kAtcFrame040 = uint32_t(offsetof(Frame, atc));
constexpr uint32_t kAtcFetch040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, fetches));
constexpr uint32_t kAtcRoutes040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, routes));
constexpr uint32_t kAtcAccess040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, access));
constexpr uint32_t kAtcCacheFetches040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, cacheFetches));
constexpr uint32_t kAtcCacheSaved040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, cacheSaved));

void initAtcContext040(Asm& a, const Layout& L) {
    if (L.is030) return;
    a.strX(31, 1, kAtcFrame040);
    a.strX(31, 1, kAtcFrame040 + 8);
    if (gAtcInstruction040->fetchWords040) {
        a.ldrB(12, 0, L.atcWindow040);
        a.movW(13, gAtcInstruction040->fetchWords040);
        a.mulW(12, 12, 13);
        a.strW(12, 1, kAtcFetch040);
    }
    // MC68040UM-1993 §4.1: the I-cache stream count waits here too, so an
    // exact thunk commits it before its own I-side accesses.
    if (L.cache040Live) {
        a.movW(12, gAtcInstruction040->fetchWords040 & 3);
        a.strW(12, 1, kAtcCacheFetches040);
        a.strB(31, 1, kAtcCacheSaved040);
    }
    gAtcContext040 = true;
}

void beginAtcInstruction040(Asm& a, const Layout& L, const Instr& in) {
    gAtcInstruction040 = &in;
    gAtcContext040 = false;
    gAtcRoutes040 = 0;
    if (!L.is030 && memoryContractMask(in.memory)) initAtcContext040(a, L);
}

void recordAtcRoute040(Asm& a, const Layout& L, AtcRoute route,
                        unsigned entry, bool cache) {
    if (L.is030 || !route.mask || !route.weight) return;
    if (!gAtcContext040) initAtcContext040(a, L);
    gAtcRoutes040 |= route.mask;
    a.ldrB(12, entry, cache ? 13 : 6);
    if (route.weight != 1) {
        a.movW(13, route.weight);
        a.mulW(12, 12, 13);
    }
    for (unsigned i = 0; i < AtcReplay040::Accesses; ++i)
        if (route.mask & (1u << i)) a.strB(12, 1, kAtcRoutes040 + i);
}

// Moira splits a three-byte memory bit field into a word and a byte: two
// data-ATC accesses per direction where the longword proof span recorded
// one (Moira::mmu040AtcLookup counts every access).
void doubleAtcRoute040(Asm& a, const Layout& L, uint8_t mask) {
    if (L.is030 || !(gAtcRoutes040 & mask)) return;
    for (unsigned i = 0; i < AtcReplay040::Accesses; ++i)
        if (mask & (1u << i)) {
            a.ldrB(12, 1, kAtcRoutes040 + i);
            a.addW(12, 12, 12);
            a.strB(12, 1, kAtcRoutes040 + i);
        }
}

void prepareAtcThunk040(Asm& a, const Layout& L, unsigned access,
                         unsigned argument) {
    if (L.is030) { a.movX(argument, 0); return; }
    if (!gAtcContext040) initAtcContext040(a, L);
    if (access < AtcReplay040::Accesses) gAtcRoutes040 |= uint8_t(1u << access);
    a.movW(12, access); a.strW(12, 1, kAtcAccess040);
    a.address(argument, 1, kAtcFrame040);
}

constexpr uint32_t kCacheRotD = uint32_t(offsetof(Frame, cacheRotD));

// MC68040UM-1993 §4.1: add w12 to a cache's two-bit replacement counter
// (Moira::pomCache040{I,D}.rot). Clobbers w13.
void addCacheRot040(Asm& a, uint32_t rot) {
    a.ldrB(13, 0, rot);
    a.addW(13, 13, 12);
    a.movW(12, 3); a.andW(13, 13, 12);
    a.strB(13, 0, rot);
}

// The deferred D-cache delta of the cache-only probes, once their hits are
// published (commitReplayableMemory, the atomic MOVE cache pair).
void commitDeferredCacheRot040(Asm& a, const Layout& L) {
    a.ldrW(12, 1, kCacheRotD);
    addCacheRot040(a, L.cache040DRot);
}

void commitAtcInstruction040(Asm& a, const Layout& L) {
    if (L.is030) return;
    // Natively executed code bypasses the I-cache reads Moira counts, one
    // per fetched word (Moira::pomJitFetch, MoiraCache040.h).
    const unsigned fetchWords = gFetchPath040 >= 0
        ? unsigned(gFetchPath040) : gAtcInstruction040->fetchWords040;
    if (L.cache040Live) {
        if (!gAtcContext040) {
            if (fetchWords & 3) {
                a.movW(12, fetchWords & 3);
                addCacheRot040(a, L.cache040IRot);
            }
        } else {
            // What no exact thunk committed yet (pomJitAtcReplayBegin040).
            a.ldrW(12, 1, kAtcCacheFetches040);
            addCacheRot040(a, L.cache040IRot);
            a.strW(31, 1, kAtcCacheFetches040);
            a.strB(31, 1, kAtcCacheSaved040);
        }
    }
    if (!gAtcContext040) {
        const auto count = fetchWords;
        if (!count) return;
        const int identity = a.label();
        a.ldrB(12, 0, L.atcWindow040); a.cbzW(12, identity);
        a.ldrW(12, 0, L.atcAccessI040);
        a.addImmW(12, 12, count); a.strW(12, 0, L.atcAccessI040);
        a.bind(identity);
        return;
    }
    a.ldrW(12, 1, kAtcFetch040);
    a.ldrW(13, 0, L.atcAccessI040);
    a.addW(13, 13, 12); a.strW(13, 0, L.atcAccessI040);
    // Each of the four little-endian bytes is a weight <=16. Their sum
    // fits one byte; multiplying by 0x01010101 collects it in the top byte.
    a.ldrW(12, 1, kAtcRoutes040);
    a.movW(13, 0x01010101); a.mulW(12, 12, 13); a.lsrW(12, 12, 24);
    a.ldrW(13, 0, L.atcAccessD040);
    a.addW(13, 13, 12); a.strW(13, 0, L.atcAccessD040);
    a.strX(31, 1, kAtcFrame040); a.strX(31, 1, kAtcFrame040 + 8);
}
