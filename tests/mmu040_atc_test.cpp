// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Bare-core, multi-instruction ATC regressions. Single-instruction vectors
// flush their ATCs and cannot distinguish two resident FC2 translations.
// [B] MC68040UM-1993 §3.3, Figures 3-20/3-21: the tag includes FC2;
// supervisor protection in the descriptor is a separate attribute.

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

unsigned failures = 0;
#define CHECK(ok, ...) do { if (!(ok)) { \
    std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

constexpr uint32_t logical = 0x10000000;
constexpr uint32_t code = 0x1000;
constexpr uint32_t phys[2] = {0x50000, 0x40000}; // indexed by supervisor
constexpr uint32_t changed[2] = {0x70000, 0x60000};
constexpr uint32_t value[2] = {0xB1B2B3B4, 0xA1A2A3A4};

class Cpu : public moira::Moira {
public:
    std::vector<uint8_t> mem = std::vector<uint8_t>(1 << 20);
    mutable unsigned busReads = 0;

    explicit Cpu(moira::Model model) { setModel(model); }
    void put16(uint32_t a, uint16_t v) {
        mem[a & 0xFFFFF] = uint8_t(v >> 8);
        mem[(a + 1) & 0xFFFFF] = uint8_t(v);
    }
    void put32(uint32_t a, uint32_t v) {
        put16(a, uint16_t(v >> 16)); put16(a + 2, uint16_t(v));
    }
    uint32_t get32(uint32_t a) const {
        return uint32_t(mem[a]) << 24 | uint32_t(mem[a + 1]) << 16 |
               uint32_t(mem[a + 2]) << 8 | mem[a + 3];
    }

    void map(bool super, bool page8k, uint32_t la, uint32_t pa,
             uint32_t attributes = 0x19) {
        const uint32_t root = super ? 0x4000 : 0x6000;
        const uint32_t pointers = root + 0x200;
        const uint32_t pointerIndex = (la >> 16) & 0x1FC;
        const uint32_t pages = (super ? 0x10000 : 0x20000) + pointerIndex * 256;
        put32(root + ((la >> 23) & 0x1FC), pointers | 2);
        put32(pointers + pointerIndex, pages | 2);
        const uint32_t pageIndex = page8k ? (la >> 11) & 0x7C :
                                               (la >> 10) & 0xFC;
        // U/M already set, no protection: a later write must use the
        // resident mapping without the first-write descriptor re-search.
        put32(pages + pageIndex, pa | attributes);
    }

    void start(bool page8k, bool cache) {
        put32(0, 0x80000); put32(4, code);
        reset();
        setITT0(0x0000C000);             // common test driver, both modes
        setURP040(0x6000); setSRP040(0x4000);
        setTC040(page8k ? 0xC000 : 0x8000);
        setPomCache040(cache);
        setCACR(cache ? 0x80008000 : 0);
        setA(0, logical);
    }

private:
    moira::u8 read8(moira::u32 a) const override {
        ++busReads; return mem[a & 0xFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        ++busReads;
        return uint16_t(mem[a & 0xFFFFF] << 8 | mem[(a + 1) & 0xFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        const_cast<Cpu*>(this)->mem[a & 0xFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        const_cast<Cpu*>(this)->put16(a, v);
    }
};

void checkProbe(Cpu &cpu, bool instruction, bool super, bool resident,
                uint32_t expected, bool page8k) {
    uint32_t pa = 0, base = 0, bytes = 0;
    const auto reads = cpu.busReads;
    const bool found = instruction ?
        cpu.pomJitProbeCode(logical, super, pa, base, bytes) :
        cpu.pomJitProbeData(logical, super, false, pa, base, bytes, true);
    CHECK(found == resident && (!resident ||
          (pa == expected && base == logical && bytes == (page8k ? 8192 : 4096))),
          "%s probe S=%u: found=%u, PA=%08X, expected resident=%u PA=%08X",
          instruction ? "I" : "D", super, found, pa, resident, expected);
    CHECK(cpu.busReads == reads, "read-only ATC probe touched the bus");
}

void dataDomains(moira::Model model, bool page8k, bool cache, bool firstSuper) {
    Cpu cpu(model);
    for (unsigned i = 0; i < 4; ++i) cpu.put16(code + i * 2, 0x2010); // (A0),D0
    for (unsigned i = 4; i < 6; ++i) cpu.put16(code + i * 2, 0x2081); // D1,(A0)
    cpu.put16(code + 12, 0x4E71); cpu.put16(code + 14, 0x4E71);
    for (bool s : {false, true}) {
        cpu.map(s, page8k, logical, phys[s]);
        cpu.put32(phys[s], value[s]); cpu.put32(changed[s], 0xDEADBEEF);
    }
    cpu.start(page8k, cache);
    for (unsigned step = 0; step < 4; ++step) {
        const bool s = firstSuper != bool(step & 1);
        if (step == 2)
            for (bool mode : {false, true}) cpu.map(mode, page8k, logical, changed[mode]);
        cpu.setSR(s ? 0x2000 : 0);
        cpu.execute();
        CHECK(cpu.getD(0) == value[s],
              "D mode %u step %u: %08X, expected %08X", s, step, cpu.getD(0), value[s]);
        if (step == 0) checkProbe(cpu, false, !s, false, 0, page8k);
    }
    for (bool s : {false, true}) checkProbe(cpu, false, s, true, phys[s], page8k);
    for (unsigned step = 0; step < 2; ++step) {
        const bool s = firstSuper != bool(step & 1);
        cpu.setSR(s ? 0x2000 : 0); cpu.setD(1, value[s] ^ 0xFFFFFFFF);
        cpu.execute();
    }
    for (bool s : {false, true}) {
        CHECK(cpu.get32(phys[s]) == (value[s] ^ 0xFFFFFFFF),
              "D store used the wrong FC2 mapping, S=%u", s);
        CHECK(cpu.get32(changed[s]) == 0xDEADBEEF,
              "D mode switch discarded a resident translation, S=%u", s);
    }
}

void instructionDomains(moira::Model model, bool page8k, bool cache, bool firstSuper) {
    Cpu cpu(model);
    cpu.put16(code, 0x4E71); cpu.put16(code + 2, 0x4E71);
    for (bool s : {false, true}) {
        cpu.map(s, page8k, logical, phys[s]);
        cpu.put16(phys[s], uint16_t(0x7001 + s));
        cpu.put16(phys[s] + 2, 0x4E71);
        cpu.put16(changed[s], uint16_t(0x7003 + s));
        cpu.put16(changed[s] + 2, 0x4E71);
    }
    cpu.start(page8k, cache);
    for (unsigned step = 0; step < 4; ++step) {
        const bool s = firstSuper != bool(step & 1);
        if (step == 2)
            for (bool mode : {false, true}) cpu.map(mode, page8k, logical, changed[mode]);
        cpu.setSR(s ? 0x2000 : 0);
        cpu.setPC(logical); cpu.setPC0(logical);
        cpu.execute();
        CHECK(cpu.getD(0) == uint32_t(1 + s),
              "I mode %u step %u: %08X, expected %u", s, step, cpu.getD(0), 1 + s);
        if (step == 0) checkProbe(cpu, true, !s, false, 0, page8k);
    }
    for (bool s : {false, true}) {
        checkProbe(cpu, true, s, true, phys[s], page8k);
        checkProbe(cpu, false, s, false, 0, page8k); // I entries are not D entries
    }
}

void movesDomains(moira::Model model, bool page8k, bool cache) {
    Cpu cpu(model);
    for (unsigned i = 0; i < 6; ++i) {
        cpu.put16(code + i * 4, 0x0E90);    // MOVES.L (A0),D0 / D1,(A0)
        cpu.put16(code + i * 4 + 2, i < 4 ? 0 : 0x1800);
    }
    cpu.put16(code + 24, 0x4E71); cpu.put16(code + 26, 0x4E71);
    for (bool s : {false, true}) {
        cpu.map(s, page8k, logical, phys[s]); cpu.put32(phys[s], value[s]);
    }
    cpu.start(page8k, cache); cpu.setSR(0x2000);
    for (unsigned i = 0; i < 4; ++i) {
        const bool s = !(i & 1);
        cpu.setSFC(s ? 5 : 1); cpu.execute();
        CHECK(cpu.getD(0) == value[s],
              "MOVES SFC=%u read %08X, expected %08X", s ? 5 : 1, cpu.getD(0), value[s]);
    }
    for (bool s : {false, true}) {
        cpu.setDFC(s ? 5 : 1); cpu.setD(1, value[s] ^ 0xFFFFFFFF); cpu.execute();
        CHECK(cpu.get32(phys[s]) == (value[s] ^ 0xFFFFFFFF),
              "MOVES DFC=%u used the current SR instead of access FC2", s ? 5 : 1);
    }
}

void movesPublication(moira::Model model, bool page8k) {
    Cpu cpu(model);
    cpu.put16(code, 0x0E90); cpu.put16(code + 2, 0x1800); // MOVES.L D1,(A0)
    cpu.put16(code + 4, 0x4E71); cpu.put16(code + 6, 0x4E71);
    for (bool s : {false, true}) {
        cpu.map(s, page8k, logical, phys[s], 0x39); // copyback, U/M/R
        cpu.put32(phys[s], value[s]);
    }
    cpu.start(page8k, true); cpu.setSR(0x2000);
    const auto slot = (logical >> 4) & (moira::Moira::PomJitCache040Table::kEntries - 1);
    CHECK(cpu.pomJitWriteData(logical, 4, value[1] ^ 1),
          "prepare a dirty supervisor cache line");
    const auto supervisorIndex = cpu.pomJitCache040R.e[slot].atcIndex;
    cpu.setSR(0);
    uint32_t got = 0;
    CHECK(cpu.pomJitReadData(logical, 4, got) && got == value[0],
          "prepare the distinct user ATC and cache line");
    cpu.setSR(0x2000); cpu.setDFC(1); cpu.setD(1, value[0] ^ 0xFFFFFFFF);
    cpu.pomJitCache040R.e[slot] = {};
    cpu.pomJitCache040W.e[slot] = {};
    cpu.pomJitCache040Consumer = true;
    cpu.execute();
    CHECK(cpu.get32(phys[0]) == (value[0] ^ 0xFFFFFFFF) &&
          cpu.get32(phys[1]) == value[1],
          "MOVES user store must spare dirty supervisor data");
    // A publication for the current supervisor context may never borrow
    // the access's user ATC index. The native guard would then validate
    // the wrong last-hit/MRU state even if the cache bytes were correct.
    for (const auto *entry : {&cpu.pomJitCache040R.e[slot],
                              &cpu.pomJitCache040W.e[slot]}) {
        if (entry->generation == cpu.pomJitCache040Gen &&
            entry->tag == cpu.pomJitCache040Tag(logical, true))
            CHECK(entry->atcIndex == supervisorIndex,
                  "supervisor publication borrowed the user access's ATC index");
    }
}

} // namespace

int main() {
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (bool cache : {false, true}) {
        for (bool firstSuper : {false, true}) {
            std::printf("FC2 model=%u %uK cache=%u first S=%u\n",
                        unsigned(model), page8k ? 8 : 4, cache, firstSuper);
            dataDomains(model, page8k, cache, firstSuper);
            instructionDomains(model, page8k, cache, firstSuper);
        }
        movesDomains(model, page8k, cache);
        if (cache) movesPublication(model, page8k);
    }
    std::printf("mmu040_atc_test: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
