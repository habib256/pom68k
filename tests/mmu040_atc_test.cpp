// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Bare-core, multi-instruction ATC regressions. Single-instruction vectors
// flush their ATCs and cannot test resident translations across MMU writes.
// [B] MC68040UM-1993 §3.3, Figures 3-20/3-21: the tag includes FC2;
// supervisor protection in the descriptor is a separate attribute.
// [B] MC68040UM-1993 §3.7.4: MMU register accesses do not flush the ATCs.

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <string_view>
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
    uint32_t failRead = ~0u, failWrite = ~0u;
    mutable unsigned busErrors = 0;
    bool watching = false;
    mutable unsigned tableReads = 0, tableWrites = 0, operandWrites = 0;

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
             uint32_t attributes = 0x19, bool alternateRoot = false) {
        const uint32_t root = (super ? 0x4000 : 0x6000) +
                              (alternateRoot ? 0x4000 : 0);
        const uint32_t pointers = root + 0x200;
        const uint32_t pointerIndex = (la >> 16) & 0x1FC;
        const uint32_t pages = (super ? 0x10000 : 0x20000) + pointerIndex * 256 +
                               (alternateRoot ? 0x70000 : 0);
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
    void observe(uint32_t a, bool write) const {
        if (!watching) return;
        if (a >= 0x4000 && a < 0x30000) {
            if (write) ++tableWrites; else ++tableReads;
        }
        for (uint32_t base : {0x40000u, 0x50000u, 0x60000u, 0x70000u})
            if (write && a >= base && a < base + 0x2000) ++operandWrites;
    }
    moira::u8 read8(moira::u32 a) const override {
        observe(a, false);
        ++busReads; return mem[a & 0xFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        observe(a, false);
        ++busReads;
        if (a == failRead) {
            ++busErrors;
            const_cast<Cpu*>(this)->extBusError040();
        }
        return uint16_t(mem[a & 0xFFFFF] << 8 | mem[(a + 1) & 0xFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        observe(a, true);
        const_cast<Cpu*>(this)->mem[a & 0xFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        observe(a, true);
        if (a == failWrite) {
            ++busErrors;
            const_cast<Cpu*>(this)->extBusError040();
        }
        const_cast<Cpu*>(this)->put16(a, v);
    }
};

void checkProbe(Cpu &cpu, bool instruction, bool super, bool resident,
                uint32_t expected, bool page8k, uint32_t address = logical) {
    uint32_t pa = 0, base = 0, bytes = 0;
    const auto reads = cpu.busReads;
    const bool found = instruction ?
        cpu.pomJitProbeCode(address, super, pa, base, bytes) :
        cpu.pomJitProbeData(address, super, false, pa, base, bytes, true);
    CHECK(found == resident && (!resident ||
          (pa == expected && base == address && bytes == (page8k ? 8192 : 4096))),
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

void executeAt(Cpu &cpu, uint32_t pc) {
    cpu.setPC(pc); cpu.setPC0(pc); cpu.execute();
}

// Two roots deliberately disagree on both a resident and a fresh page.
// Resident I/D translations must survive MOVEC; misses must use the new
// root, and an explicit PFLUSHA must finally expose it on the old page.
void registerRetention(moira::Model model, bool page8k, bool cache,
                       uint16_t control) {
    Cpu cpu(model);
    cpu.put16(code, 0x2010);                        // MOVE.L (A0),D0
    cpu.put16(code + 2, 0x4E71);
    cpu.put16(code + 4, 0x4E7B);                   // MOVEC D1,Rc
    cpu.put16(code + 6, 0x1000 | control);
    cpu.put16(code + 8, 0xF518);                   // PFLUSHA
    cpu.put16(code + 10, 0x4E71);
    cpu.put16(code + 12, 0x4E71);
    for (bool s : {false, true}) {
        cpu.map(s, page8k, logical, phys[s]);
        cpu.map(s, page8k, logical, changed[s], 0x19, true);
        cpu.map(s, page8k, logical + 0x2000, phys[s] + 0x2000);
        cpu.map(s, page8k, logical + 0x2000, changed[s] + 0x2000, 0x19, true);
        for (uint32_t offset : {0u, 0x2000u}) {
            cpu.put32(phys[s] + offset, 0x70014E71 + uint32_t(s) * 0x10000);
            cpu.put32(changed[s] + offset, 0x70034E71 + uint32_t(s) * 0x10000);
        }
    }
    cpu.start(page8k, cache);
    auto access = [&](bool s, uint32_t la, uint32_t pa) {
        cpu.setSR(s ? 0x2000 : 0);
        executeAt(cpu, la);                         // MOVEQ #n,D0
        CHECK(cpu.getD(0) == ((cpu.get32(pa) >> 16) & 0xFF),
              "MOVEC %03X: stale/lost I translation S=%u LA=%08X",
              control, s, la);
        cpu.setA(0, la); executeAt(cpu, code);
        CHECK(cpu.getD(0) == cpu.get32(pa),
              "MOVEC %03X: stale/lost D translation S=%u LA=%08X",
              control, s, la);
    };
    for (bool s : {false, true}) access(s, logical, phys[s]);

    const uint32_t tc = page8k ? 0xC000 : 0x8000;
    auto movec = [&](uint32_t v) {
        cpu.setSR(0x2000); cpu.setD(1, v);
        const auto generation = cpu.pomJitMmuGen;
        const auto cacheGeneration = cpu.pomJitCache040Gen;
        cpu.pomJitArm(&cpu.mem[code], code, code, 16, true);
        CHECK(cpu.pomJitCovers(code), "MOVEC %03X needs a live fetch window", control);
        executeAt(cpu, code + 4);
        CHECK(cpu.pomJitMmuGen != generation && !cpu.pomJitCovers(code),
              "MOVEC %03X must revoke the old fetch window", control);
        CHECK(cpu.pomJitCache040Gen != cacheGeneration,
              "MOVEC %03X must revoke derived cache publications", control);
        CHECK((control == 0x003 ? cpu.getTC040() :
               control == 0x806 ? cpu.getURP040() : cpu.getSRP040()) == v,
              "MOVEC %03X did not update the register", control);
    };

    if (control == 0x003) {
        // Even a same-value TCR write must retain resident translations
        // after the guest has edited its descriptors.
        for (bool s : {false, true}) cpu.map(s, page8k, logical, changed[s]);
        movec(tc);
    } else {
        movec(control == 0x806 ? 0xA000 : 0x8000);
    }
    for (bool s : {false, true}) {
        checkProbe(cpu, true, s, true, phys[s], page8k);
        checkProbe(cpu, false, s, true, phys[s], page8k);
        access(s, logical, phys[s]);
    }
    if (control == 0x003) {
        movec(tc & ~0x8000u);
        for (bool s : {false, true}) {
            checkProbe(cpu, true, s, true, logical, page8k);
            checkProbe(cpu, false, s, true, logical, page8k);
        }
        // Change P while translation is disabled, then restore it before
        // accessing the retained entries: no assumption about how a
        // resident tag would be reinterpreted at the other page size.
        movec((tc ^ 0x4000u) & ~0x8000u);
        movec(tc & ~0x8000u);
        movec(tc);
        for (bool s : {false, true}) access(s, logical, phys[s]);
    } else {
        for (bool s : {false, true}) {
            const bool newRoot = control == (s ? 0x807 : 0x806);
            access(s, logical + 0x2000, (newRoot ? changed[s] : phys[s]) + 0x2000);
        }
    }
    cpu.setSR(0x2000); executeAt(cpu, code + 8);
    for (bool s : {false, true}) {
        checkProbe(cpu, true, s, false, 0, page8k);
        checkProbe(cpu, false, s, false, 0, page8k);
        const bool newRoot = control == 0x003 || control == (s ? 0x807 : 0x806);
        access(s, logical, newRoot ? changed[s] : phys[s]);
    }
}

void accessMapping(Cpu &cpu, bool instruction, bool super, uint32_t la, uint32_t pa) {
    cpu.setSR(super ? 0x2000 : 0);
    cpu.setA(0, la);
    executeAt(cpu, instruction ? la : code);
    const auto expected = instruction ? (cpu.get32(pa) >> 16) & 0xFF : cpu.get32(pa);
    CHECK(cpu.getD(0) == expected, "%s access S=%u LA=%08X got=%08X expected=%08X",
          instruction ? "I" : "D", super, la, cpu.getD(0), expected);
}

void seedMappings(Cpu &cpu, bool page8k, unsigned globals) {
    cpu.put16(code, 0x2010); cpu.put16(code + 2, 0x4E71);
    cpu.put16(code + 6, 0x4E71);
    cpu.put16(code + 8, 0x2081); cpu.put16(code + 10, 0x4E71);
    for (bool s : {false, true})
    for (unsigned page = 0; page < 2; ++page) {
        const auto off = page * 0x2000;
        const bool global = bool((globals >> s) & 1) != bool(page);
        cpu.map(s, page8k, logical + off, phys[s] + off, 0x19 | (global ? 0x400 : 0));
        cpu.put32(phys[s] + off, 0x70014E71 + uint32_t(s) * 0x10000);
        cpu.put32(changed[s] + off, 0x70034E71 + uint32_t(s) * 0x10000);
    }
}

// [B] M68000PRM-1992 PFLUSH (MC68040), pp. 6-35/6-36:
// page forms select FC2 from DFC and both ATCs; all forms ignore DFC.
void flushEffects(moira::Model model, bool page8k, bool cache,
                  unsigned mode, unsigned dfc, unsigned globals) {
    const auto before = failures;
    Cpu cpu(model);
    seedMappings(cpu, page8k, globals);
    cpu.put16(code + 4, uint16_t(0xF500 | (mode << 3)));
    cpu.start(page8k, cache);
    for (bool s : {false, true})
    for (unsigned page = 0; page < 2; ++page)
    for (bool instruction : {false, true}) {
        const auto off = page * 0x2000;
        accessMapping(cpu, instruction, s, logical + off, phys[s] + off);
    }
    for (bool s : {false, true})
    for (unsigned page = 0; page < 2; ++page) {
        const bool global = bool((globals >> s) & 1) != bool(page);
        cpu.map(s, page8k, logical + page * 0x2000, changed[s] + page * 0x2000,
                0x19 | (global ? 0x400 : 0));
    }
    cpu.setSR(0x2015); cpu.setDFC(dfc); cpu.setA(0, logical + 0x456);
    cpu.setMMUSR040(0x12345678);
    const auto accessesI = cpu.pomJitAtcAccesses040(false);
    const auto accessesD = cpu.pomJitAtcAccesses040(true);
    executeAt(cpu, code + 4);
    CHECK(cpu.pomJitAtcAccesses040(false) == accessesI &&
          cpu.pomJitAtcAccesses040(true) == accessesD,
          "PFLUSH invalidates entries without resetting/debiting the replacement phase");
    CHECK(cpu.getPC() == code + 6 && (cpu.getSR() & 31) == 0x15 &&
          cpu.getMMUSR040() == 0x12345678 && cpu.getA(0) == logical + 0x456,
          "PFLUSH mode=%u DFC=%u changed PC/CCR/MMUSR/An", mode, dfc);
    for (bool s : {false, true})
    for (unsigned page = 0; page < 2; ++page)
    for (bool instruction : {false, true}) {
        const auto off = page * 0x2000;
        const bool global = bool((globals >> s) & 1) != bool(page);
        const bool flushed = (mode >= 2 || (!page && s == bool(dfc & 4))) &&
                             ((mode & 1) || !global);
        checkProbe(cpu, instruction, s, !flushed, phys[s] + off, page8k, logical + off);
        accessMapping(cpu, instruction, s, logical + off,
                      (flushed ? changed[s] : phys[s]) + off);
    }
    if (failures != before)
        std::printf("PFLUSH context: model=%u %uK cache=%u mode=%u DFC=%u globals=%u\n",
                    unsigned(model), page8k ? 8 : 4, cache, mode, dfc, globals);
}

// [B] M68000PRM-1992 PTEST (MC68040), pp. 6-70/6-71:
// DFC selects one ATC/domain. Invalid searches also create an entry.
// Cases: valid, WP, invalid, three descriptor reads, three U/M writes, TTR.
void testEffects(moira::Model model, bool page8k, bool cache,
                 unsigned dfc, bool write, unsigned kind, bool warm) {
    const auto before = failures;
    Cpu cpu(model);
    seedMappings(cpu, page8k, 3);                 // old entries are global
    cpu.put16(code + 4, write ? 0xF548 : 0xF568);
    cpu.put32(8, 0x2000);
    cpu.put16(0x2000, 0x4E71); cpu.put16(0x2002, 0x4E71);
    cpu.start(page8k, cache);
    cpu.setDTT0(0x0000C000);                    // stack/vector identity mapping
    if (warm)
        for (bool s : {false, true})
        for (bool instruction : {false, true})
            accessMapping(cpu, instruction, s, logical, phys[s]);
    const bool super = (dfc & 4) != 0;
    const bool instruction = (dfc & 3) == 2;
    const uint32_t attributes = kind == 2 ? 0 : kind == 1 ? 0x405 : 0x401;
    cpu.map(super, page8k, logical, changed[super], attributes);
    const uint32_t tables[] = {super ? 0x4020u : 0x6020u,
                               super ? 0x4200u : 0x6200u,
                               super ? 0x10000u : 0x20000u};
    if (kind >= 3 && kind <= 5) cpu.failRead = tables[kind - 3];
    if (kind >= 6 && kind <= 8) cpu.failWrite = tables[kind - 6];
    if (kind == 9) {
        if (instruction) cpu.setITT1(0x1000C000);
        else cpu.setDTT1(0x1000C000);
    }
    cpu.setSR(0x2015); cpu.setDFC(dfc); cpu.setA(0, logical + 0x456);
    const auto sp = cpu.getSP();
    const auto generation = cpu.pomJitMmuGen;
    executeAt(cpu, code + 4);
    const bool busError = kind >= 3 && kind <= 8;
    const uint32_t expected = busError ? 0x800 : kind == 2 ? 0 : kind == 9 ? 3 :
        changed[super] | attributes | ((write && kind != 1) ? 0x10 : 0);
    CHECK(cpu.getMMUSR040() == expected,
          "PTEST DFC=%u W=%u kind=%u warm=%u MMUSR=%08X expected=%08X",
          dfc, write, kind, warm, cpu.getMMUSR040(), expected);
    CHECK(cpu.getPC() == code + 6 && cpu.getSP() == sp && !cpu.isHalted() &&
          (cpu.getSR() & 31) == 0x15 && cpu.getA(0) == logical + 0x456,
          "PTEST must complete without an exception or CCR/An change");
    CHECK(cpu.busErrors == unsigned(busError), "PTEST bus-error precondition");
    CHECK(cpu.pomJitMmuGen != generation, "PTEST must revoke derived mappings");
    cpu.failRead = cpu.failWrite = ~0u;
    if (kind == 9) {
        cpu.setITT1(0); cpu.setDTT1(0);
    }
    for (bool s : {false, true})
    for (bool i : {false, true}) {
        const bool selected = s == super && i == instruction;
        const bool resident = selected ? (kind < 2) : warm;
        checkProbe(cpu, i, s, resident, selected ? changed[s] : phys[s], page8k);
    }
    // Repair the descriptor after PTEST. A positive result must keep its
    // mapping; a negative result must still fault, including on a write.
    cpu.map(super, page8k, logical, phys[super]);
    if (kind < 2) {
        accessMapping(cpu, instruction, super, logical, changed[super]);
        if (kind == 1 && !instruction) {
            cpu.setD(1, 0xDEADBEEF);
            executeAt(cpu, code + 8);
            CHECK(cpu.getPC() == 0x2000 && !cpu.isHalted() &&
                  cpu.get32(changed[super]) != 0xDEADBEEF,
                  "PTEST write protection lost after descriptor repair");
        }
    } else if (kind != 9) {
        cpu.setSR(super ? 0x2000 : 0);
        cpu.setA(0, logical); cpu.setD(1, 0xDEADBEEF);
        executeAt(cpu, instruction ? logical : write ? code + 8 : code);
        CHECK(cpu.getPC() == 0x2000 && !cpu.isHalted(),
              "PTEST negative entry lost after descriptor repair, DFC=%u W=%u kind=%u",
              dfc, write, kind);
    }
    if (failures != before)
        std::printf("PTEST context: model=%u %uK cache=%u DFC=%u W=%u kind=%u warm=%u\n",
                    unsigned(model), page8k ? 8 : 4, cache, dfc, write, kind, warm);
}

// [B] MC68040UM-1993 §3.3: a two-bit counter advances for ATC accesses.
// The reset phase is not specified. Compare victim rotations across identical
// runs, rather than assuming an absolute first way or initial counter value.
// [F] Independent I/D phases are the current model; silicon coupling is unknown.
void replacement040() {
    unsigned sequences = 0;
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (bool cache : {false, true})
    for (unsigned domains = 0; domains < 3; ++domains)
    for (bool data : {false, true}) {
        int victims[7][8] {};
        const uint32_t pageBytes = page8k ? 8192 : 4096;
        for (unsigned kind = 0; kind < 7; ++kind) // same set, other set, other side, probe, TTR, MMU off, denial
        for (unsigned hits = 0; hits < 8; ++hits) {
            const auto before = failures;
            Cpu cpu(model);
            const auto privilege = [domains](unsigned way) { return domains == 1 || (domains == 2 && (way & 1)); };
            const auto address = [pageBytes](unsigned way, unsigned set = 0) {
                return logical + (way * 16 + set) * pageBytes + 0x100;
            };
            const auto access = [&](bool side, uint32_t la, bool s, bool permitted = true) {
                cpu.setSR(s ? 0x2000 : 0);
                uint32_t value = 0; uint16_t word = 0;
                const bool ok = side ? cpu.pomJitReadData(la, 4, value) : cpu.pomJitReadProg(la, word);
                return ok == permitted;
            };
            const auto probe = [&](bool side, uint32_t la, bool s) {
                uint32_t pa = 0, base = 0, bytes = 0;
                return side ? cpu.pomJitProbeData(la, s, false, pa, base, bytes, true) :
                              cpu.pomJitProbeCode(la, s, pa, base, bytes);
            };
            for (unsigned way = 0; way < 5; ++way)
                cpu.map(privilege(way), page8k, address(way), phys[privilege(way)], 0x59);
            for (bool s : {false, true}) {
                cpu.map(s, page8k, address(0, 1), phys[s], 0x59);
                cpu.map(s, page8k, address(0, 2), phys[s], s ? 0x59 : 0xD9);
                cpu.put32(phys[s] + 0x100, value[s]);
            }
            cpu.start(page8k, cache); cpu.setDTT0(0x0000C040); cpu.setITT0(0x0000C040);
            CHECK(access(data, address(0, 1), false) && access(!data, address(0, 1), false) &&
                  access(data, address(0, 2), false, false), "replacement: prepare both sides and a cached S denial");
            for (unsigned way = 0; way < 4; ++way)
                CHECK(access(data, address(way), privilege(way)), "replacement: fill the four target ways");
            for (unsigned n = 0; n < hits; ++n) {
                bool ok = false;
                switch (kind) {
                    case 0: ok = access(data, address(0), privilege(0)); break;
                    case 1: ok = access(data, address(0, 1), false); break;
                    case 2: ok = access(!data, address(0, 1), false); break;
                    case 3: ok = probe(data, address(0), privilege(0)); break;
                    case 4: ok = access(data, code, false); break;
                    case 5:
                        cpu.setTC040(page8k ? 0x4000 : 0);
                        ok = access(data, address(0), privilege(0));
                        cpu.setTC040(page8k ? 0xC000 : 0x8000);
                        break;
                    case 6: ok = access(data, address(0, 2), false, false); break;
                }
                CHECK(ok, "replacement: extra accesses meet their intended hit/probe/bypass/denial precondition");
            }
            CHECK(access(data, address(4), privilege(4)), "replacement: fifth conflicting translation");
            unsigned missing = 0;
            int victim = -1;
            for (unsigned way = 0; way < 4; ++way)
                if (!probe(data, address(way), privilege(way))) { ++missing; victim = int(way); }
            CHECK(missing == 1 && probe(data, address(4), privilege(4)),
                  "replacement: observe exactly one victim without mutating ATC state");
            victims[kind][hits] = victim;
            if (before != failures)
                std::printf("replacement context: model=%u 8k=%u cache=%u domains=%u data=%u kind=%u hits=%u\n",
                            unsigned(model), page8k, cache, domains, data, kind, hits);
            ++sequences;
        }
        for (unsigned kind = 0; kind < 7; ++kind) {
            const bool ticks = kind == 0 || kind == 1 || kind == 6;
            for (unsigned n = 0; n < 8; ++n)
                CHECK(victims[kind][n] >= 0 && victims[kind][n] == victims[kind][ticks ? n % 4 : 0],
                      "replacement: two-bit victim phase has period four; probes/bypasses/opposite ATC do not advance it");
            if (ticks)
                for (unsigned a = 0; a < 4; ++a)
                for (unsigned b = a + 1; b < 4; ++b)
                    CHECK(victims[kind][a] != victims[kind][b], "replacement: each additional same-side ATC hit advances the victim");
            for (unsigned n = 0; n < 8; ++n)
                CHECK(victims[kind][n] == victims[ticks ? 0 : 3][n],
                      "replacement: hits in other sets and cached denials tick the same counter, while nonaccesses do not");
        }
    }
    std::printf("ATC replacement: %u executed sequences\n", sequences);
}

// [B] MC68040UM-1993 §3.3: each I/D ATC has sixteen four-way sets;
// index = LA15:12 (4K) or LA16:13 (8K), with FC2 in the tag. Inspect
// residency through read-only probes so the witness cannot change a victim.
void geometry040() {
    unsigned sequences = 0;
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (bool cache : {false, true})
    for (unsigned domains = 0; domains < 3; ++domains) // user, supervisor, mixed
    for (bool conflictData : {false, true})
    for (unsigned set = 0; set < 16; ++set) {
        const auto before = failures;
        Cpu cpu(model);
        const uint32_t pageBytes = page8k ? 8192 : 4096;
        const auto privilege = [domains](unsigned way) { return domains == 1 || (domains == 2 && (way & 1)); };
        const auto address = [pageBytes](unsigned index) { return logical + index * pageBytes + 0x100; };
        const auto read = [&](bool data, unsigned index, bool s, uint32_t expected) {
            cpu.setSR(s ? 0x2000 : 0);
            uint32_t value = 0; uint16_t word = 0;
            return data ? (cpu.pomJitReadData(address(index), 4, value) && value == expected) :
                          (cpu.pomJitReadProg(address(index), word) && word == (expected >> 16));
        };
        const auto probe = [&](bool data, unsigned index, bool s, uint32_t expected) {
            uint32_t pa = 0, base = 0, length = 0;
            const bool ok = data ? cpu.pomJitProbeData(address(index), s, false, pa, base, length, true) :
                                   cpu.pomJitProbeCode(address(index), s, pa, base, length);
            return ok && pa == expected + 0x100 && length == pageBytes;
        };
        for (unsigned index = 0; index < 64; ++index)
            cpu.map(privilege(index / 16), page8k, address(index), phys[privilege(index / 16)], 0x59);
        for (bool s : {false, true}) {
            cpu.put32(phys[s] + 0x100, 0x70014E71 + uint32_t(s) * 0x10000);
            cpu.put32(changed[s] + 0x100, 0x70034E71 + uint32_t(s) * 0x10000);
        }
        cpu.start(page8k, cache); cpu.setDTT0(0x0000C040);
        for (unsigned index = 0; index < 64; ++index)
        for (bool data : {false, true})
            CHECK(read(data, index, privilege(index / 16), 0x70014E71 + uint32_t(privilege(index / 16)) * 0x10000),
                  "geometry: warm all 64 translations on both sides");
        for (unsigned index = 0; index < 64; ++index) {
            const bool s = privilege(index / 16);
            cpu.map(s, page8k, address(index), changed[s], 0x51); // clear U, keep M/R/NC
            for (bool data : {false, true})
                CHECK(probe(data, index, s, phys[s]), "geometry: all 64 entries remain resident before any conflict");
        }
        const unsigned newcomer = 64 + set;
        const bool newSuper = domains == 1 || (domains == 2 && (set & 1));
        cpu.map(newSuper, page8k, address(newcomer), phys[newSuper], 0x59);
        CHECK(read(conflictData, newcomer, newSuper, 0x70014E71 + uint32_t(newSuper) * 0x10000),
              "geometry: fill a fifth translation in one selected set");
        unsigned victims = 0, victim = 0;
        for (unsigned index = 0; index < 64; ++index) {
            const bool s = privilege(index / 16);
            const bool resident = probe(conflictData, index, s, phys[s]);
            if (!resident) { ++victims; victim = index; }
            if (index % 16 != set)
                CHECK(resident, "geometry: a conflict cannot evict another set");
            CHECK(probe(!conflictData, index, s, phys[s]), "geometry: I/D replacement state is independent");
        }
        CHECK(victims == 1 && victim % 16 == set &&
              probe(conflictData, newcomer, newSuper, phys[newSuper]),
              "geometry: a full four-way set replaces exactly one of its entries");
        if (victims == 1) {
            const bool s = privilege(victim / 16);
            CHECK(read(conflictData, victim, s, 0x70034E71 + uint32_t(s) * 0x10000),
                  "geometry: the observed victim re-walks the repaired page");
        }
        if (before != failures)
            std::printf("geometry context: model=%u 8k=%u cache=%u domains=%u data=%u set=%u\n",
                        unsigned(model), page8k, cache, domains, conflictData, set);
        ++sequences;
    }
    std::printf("ATC geometry: %u executed sequences\n", sequences);
}

void failureRetention() {
    // [B] MC68040UM-1993 §§3.3/3.5: lookup retains both failed and
    // protected translations. Permission faults precede an M-bit re-walk.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (unsigned cache = 0; cache < 4; ++cache) // off, NC, WT, CB
    for (bool super : {false, true})
    for (unsigned access = 0; access < 8; ++access) // data R/W, code, PTEST data/code R/W, TAS
    for (unsigned size = 0; size < (access < 2 ? 3u : 1u); ++size)
    for (unsigned kind = 0; kind < 10; ++kind) // invalid R/P/page/indirect, WP R/P/page, S, 2x indirect, allowed
    for (bool modified : {false, true})
    for (bool detour : {false, true}) {
        const bool invalid = kind < 4 || kind == 8, wp = kind >= 4 && kind <= 6;
        const bool probe = access >= 3 && access <= 6, codeSpace = access == 2 || access == 5 || access == 6;
        const bool write = access == 1 || access == 4 || access == 6 || access == 7;
        const bool store = access == 1, tas = access == 7;
        if ((invalid && modified) || (wp && !write && !probe) || (kind == 7 && super) ||
            (kind == 9 && access != 0 && access != 3)) continue;
        const auto before = failures;
        Cpu cpu(model);
        const uint32_t address = logical + 0x100, payload = 0x11223344, testWrite = 0x13579BDF;
        const unsigned bytes = access < 2 ? 1u << size : tas ? 1 : codeSpace ? 2 : 4;
        const uint32_t mask = bytes == 4 ? ~0u : (1u << (8 * bytes)) - 1;
        const uint32_t cm = cache < 2 ? 0x40 : cache == 3 ? 0x20 : 0;
        const uint32_t attributes = 9 | (modified ? 0x10 : 0) | cm;
        const uint32_t fresh[2] = {0x41424344, 0x31323334};
        const uint16_t reads[] = {0x1018, 0x3018, 0x2018}, writes[] = {0x10C0, 0x30C0, 0x20C0};
        cpu.put16(code, probe ? (write ? 0xF548 : 0xF568) : tas ? 0x4AD0 : store ? writes[size] : reads[size]);
        cpu.put16(code + 2, 0x4E71); cpu.put16(code + 4, 0x4E71);
        cpu.put16(code + 0x20, 0xF518); cpu.put16(code + 0x22, 0x4E71);
        cpu.put32(8, 0x2000); cpu.put16(0x2000, 0xF518); cpu.put16(0x2002, 0x4E73); cpu.put16(0x2004, 0x4E71);
        for (bool s : {false, true}) {
            cpu.map(s, page8k, logical, phys[s], attributes);
            cpu.put32(phys[s] + 0x100, codeSpace ? 0x4E704E70 : value[s]);
            cpu.put32(changed[s] + 0x100, codeSpace ? 0x4E714E71 : fresh[s]);
        }
        const uint32_t root = super ? 0x4020 : 0x6020, pointer = super ? 0x4200 : 0x6200;
        const uint32_t page = super ? 0x10000 : 0x20000, indirect = 0x28000;
        if (kind == 0) cpu.put32(root, (super ? 0x4200 : 0x6200));
        if (kind == 1) cpu.put32(pointer, page);
        if (kind == 2) cpu.put32(page, phys[super]);
        if (kind == 3 || kind == 8) {
            cpu.put32(page, indirect | 2);
            cpu.put32(indirect, kind == 3 ? phys[super] : 0x29002);
        }
        if (kind == 4) cpu.put32(root, (super ? 0x4200 : 0x6200) | 6);
        if (kind == 5) cpu.put32(pointer, page | 6);
        if (kind == 6 || kind == 7) cpu.put32(page, phys[super] | attributes | (kind == 6 ? 4 : 0x80));
        cpu.start(page8k, cache != 0); cpu.setDTT0(0x0000C040); cpu.setITT0(0x0000C040);
        cpu.setUSP(0x90000); cpu.setSR(uint16_t(((super || probe) ? 0x2000 : 0) | 0x1F));
        cpu.setA(0, address); cpu.setD(0, payload); cpu.setDFC((super ? 4 : 0) | (codeSpace ? 2 : 1));
        cpu.setMMUSR040(0x12345678);
        if (access == 2) { cpu.setPC(address); cpu.setPC0(address); }
        const uint32_t initialSp = cpu.getSP();
        cpu.watching = true; cpu.execute();
        const bool fault = !probe && kind != 9;
        const uint32_t frameSp = cpu.getSP(), wb3 = fault ? cpu.get32(frameSp + 28) : 0;
        const uint16_t savedSr = cpu.getSR();
        if (probe) {
            const uint32_t expected = invalid ? 0 : phys[super] | cm | (attributes & 0x10) | 1 |
                                                   (wp ? 4 : kind == 7 ? 0x80 : 0);
            CHECK(cpu.getPC0() == code + 2 && frameSp == initialSp && !cpu.isHalted() &&
                  cpu.getMMUSR040() == expected && cpu.getCCR() == 0x1F,
                  "retention: PTEST status/stack precondition kind=%u", kind);
        } else if (fault) {
            const unsigned sz = bytes == 1 ? 0x20 : bytes == 2 ? 0x40 : 0;
            const unsigned attr = sz | (super ? 4 : 0) | (codeSpace ? 2 : 1);
            CHECK(cpu.getPC0() == 0x2000 && frameSp == 0x80000 - 60 && !cpu.isHalted() &&
                  uint16_t(cpu.mem[frameSp + 12] << 8 | cpu.mem[frameSp + 13]) ==
                      (attr | 0x400 | (tas ? 0x200 : write ? 0 : 0x100)) &&
                  cpu.get32(frameSp + 20) == address && cpu.get32(frameSp + 2) ==
                      (access == 2 ? address : store ? code + 2 : code) && cpu.getMMUSR040() == 0x12345678,
                  "retention: original ATC fault/FA/PC/MMUSR precondition");
            CHECK(cpu.operandWrites == 0 && (!store || wb3 == (payload & mask)),
                  "retention: denied access made no operand store; pending store data preserved");
        }
        const uint32_t repair = (kind == 9 && !modified ? 9 : 0x19) | cm;
        for (bool s : {false, true}) cpu.map(s, page8k, logical, changed[s], repair);
        cpu.setSR(super ? 0x201F : 0x1F);
        if (detour) {
            cpu.map(super, page8k, logical + 0x4000, 0x30000, 0x19 | cm);
            cpu.put32(0x30100, 0x33334444);
            uint32_t ignored = 0; uint16_t word = 0;
            CHECK(codeSpace ? cpu.pomJitReadProg(logical + 0x4100, word) :
                              cpu.pomJitReadData(logical + 0x4100, 4, ignored),
                  "retention: detour changes the last-entry shortcut");
        }
        const auto r = cpu.tableReads, w = cpu.tableWrites, stores = cpu.operandWrites;
        uint32_t readValue = 0; uint16_t instruction = 0;
        const bool readOk = codeSpace ? cpu.pomJitReadProg(address, instruction) : cpu.pomJitReadData(address, 4, readValue);
        const bool readable = wp || kind == 9;
        CHECK(readOk == readable && (!readable || (codeSpace ? instruction == 0x4E70 : readValue == value[super])),
              "retention: repair alone must keep R/S denial or original readable PA");
        CHECK(cpu.tableReads == r && cpu.tableWrites == w,
              "retention: cached read/protection fault must not walk repaired tables");
        bool writeOk = false;
        if (!codeSpace) {
            writeOk = cpu.pomJitWriteData(address, 4, testWrite);
            CHECK(writeOk == (kind == 9), "retention: cached R/W/S permissions must precede M update");
            if (kind != 9 || modified)
                CHECK(cpu.tableReads == r && cpu.tableWrites == w,
                      "retention: denied or modified write must not re-walk repaired tables");
            else CHECK(cpu.tableReads > r && cpu.tableWrites > w,
                       "retention: allowed M=0 write must re-walk and set M");
        }
        if (kind != 9) CHECK(cpu.operandWrites == stores,
                             "retention: permission failure must not emit an operand write");
        uint32_t pa = 0, base = 0, pageBytes = 0;
        const bool mapped = codeSpace ? cpu.pomJitProbeCode(address, super, pa, base, pageBytes) :
                                       cpu.pomJitProbeData(address, super, false, pa, base, pageBytes, true);
        const uint32_t retained = kind == 9 && !modified ? changed[super] : phys[super];
        CHECK(mapped == readable && (!mapped || pa == retained + 0x100),
              "retention: read-only probe sees only the retained permitted mapping");
        if (kind == 9) {
            CHECK(cpu.pomJitReadData(address, 4, readValue) && readValue == testWrite,
                  "retention: allowed write reaches the mapping selected by M state");
            if (!modified) CHECK(cpu.get32(page) & 0x10, "retention: M re-walk updates the repaired descriptor");
        }
        // The opposite FC2 domain has no failed entry: it can use its repaired
        // mapping, while the selected domain retains its earlier attributes.
        cpu.setSR(super ? 0x1F : 0x201F);
        CHECK(codeSpace ? (cpu.pomJitReadProg(address, instruction) && instruction == 0x4E71) :
                          (cpu.pomJitReadData(address, 4, readValue) && readValue == fresh[!super]),
              "retention: opposite privilege domain is not poisoned");
        cpu.setSR(super ? 0x201F : 0x1F);
        const auto beforeRetry = cpu.tableReads;
        const bool again = codeSpace ? cpu.pomJitReadProg(address, instruction) : cpu.pomJitReadData(address, 4, readValue);
        CHECK(again == readable && cpu.tableReads == beforeRetry,
              "retention: domain switch cannot discard the selected failure/attributes");
        cpu.setSR(savedSr);
        if (fault) {
            cpu.execute(); // handler PFLUSHA
            if (store) {
                const uint16_t handlerSr = cpu.getSR();
                // WB3 carries the original transfer domain, independent of
                // the supervisor handler's current SR/stack bank.
                cpu.setSR((cpu.mem[frameSp + 15] & 4) ? 0x2010 : 0x10);
                CHECK(cpu.pomJitWriteData(address, bytes, wb3), "retention: software completes pending store in its saved FC domain");
                cpu.setSR(handlerSr);
            }
            cpu.execute(); cpu.execute(); // RTE, retry read/TAS/code or next NOP
            CHECK(!cpu.isHalted() && cpu.getSP() == initialSp && cpu.getPC0() ==
                  (access == 2 ? address + 2 : store ? code + 4 : code + 2),
                  "retention: PFLUSH plus original-frame RTE recovers");
            if (!codeSpace) {
                const uint32_t input = fresh[super] >> (32 - 8 * bytes);
                CHECK(cpu.getA(0) == address + (tas ? 0 : bytes) && cpu.getCCR() == 0x10 &&
                      cpu.getD(0) == (store || tas ? payload : (payload & ~mask) | input),
                      "retention: recovery commits data/flags and one EA update");
            }
        } else {
            cpu.setSR(uint16_t(savedSr | 0x2000));
            executeAt(cpu, code + 0x20); // PFLUSHA after PTEST/allowed control
            cpu.setSR(super ? 0x201F : 0x1F);
        }
        uint32_t finalPa = 0;
        CHECK(codeSpace ? cpu.pomJitReadProg(address, instruction) : cpu.pomJitReadData(address, 4, readValue),
              "retention: repaired mapping is accessible after PFLUSH");
        uint32_t expectedValue = fresh[super];
        if (kind == 9 && !modified) expectedValue = testWrite;
        else if (fault && store) expectedValue = bytes == 4 ? payload :
            ((payload & mask) << (8 * (4 - bytes))) | (fresh[super] & ((1u << (8 * (4 - bytes))) - 1));
        else if (fault && tas) expectedValue |= 0x80000000;
        CHECK(codeSpace ? instruction == 0x4E71 : readValue == expectedValue,
              "retention: repaired data/code bytes match the permitted completion");
        CHECK((codeSpace ? cpu.pomJitProbeCode(address, super, finalPa, base, pageBytes) :
                           cpu.pomJitProbeData(address, super, false, finalPa, base, pageBytes, true)) &&
              finalPa == changed[super] + 0x100,
              "retention: PFLUSH exposes the repaired physical mapping");
        if (failures != before)
            std::printf("retention context: model=%u 8k=%u cache=%u S=%u access=%u bytes=%u kind=%u M=%u detour=%u\n",
                        unsigned(model), page8k, cache, super, access, bytes, kind, modified, detour);
        ++cases;
    }
    std::printf("failure-retention: %u executed sequences\n", cases);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--replacement") {
        replacement040();
        std::printf("mmu040_atc_test: %u failure(s)\n", failures);
        return failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--geometry") {
        geometry040();
        std::printf("mmu040_atc_test: %u failure(s)\n", failures);
        return failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--failure-retention") {
        failureRetention();
        std::printf("mmu040_atc_test: %u failure(s)\n", failures);
        return failures ? 1 : 0;
    }
    failureRetention();
    geometry040();
    replacement040();
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
        for (uint16_t control : {0x003, 0x806, 0x807}) {
            std::printf("MOVEC retention model=%u %uK cache=%u Rc=%03X\n",
                        unsigned(model), page8k ? 8 : 4, cache, control);
            registerRetention(model, page8k, cache, control);
        }
        for (unsigned dfc : {1u, 2u, 5u, 6u}) {
            for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned globals = 0; globals < 4; ++globals)
                flushEffects(model, page8k, cache, mode, dfc, globals);
            for (bool write : {false, true}) {
                for (unsigned kind = 0; kind < 10; ++kind)
                    testEffects(model, page8k, cache, dfc, write, kind, true);
                testEffects(model, page8k, cache, dfc, write, 0, false);
            }
        }
    }
    std::printf("mmu040_atc_test: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
