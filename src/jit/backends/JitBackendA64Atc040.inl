// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ATC effects follow the same semantic access slots as the memory protocol.
// Probes stage provenance; retirement commits it. Exact thunks debit only
// earlier operands and roll back their private ATC transaction on replay.
thread_local const Instr* gAtcInstruction040 = nullptr;
thread_local bool gAtcContext040 = false;
thread_local uint8_t gAtcRoutes040 = 0;
constexpr uint32_t kAtcFrame040 = uint32_t(offsetof(Frame, atc));
constexpr uint32_t kAtcFetch040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, fetches));
constexpr uint32_t kAtcRoutes040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, routes));
constexpr uint32_t kAtcAccess040 = kAtcFrame040 + uint32_t(offsetof(AtcReplay040, access));

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

void commitAtcInstruction040(Asm& a, const Layout& L) {
    if (L.is030) return;
    if (!gAtcContext040) {
        const auto count = gAtcInstruction040->fetchWords040;
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
