// Compare the same unaccelerated CPU workload with and without the JIT seam.
// The transcript covers each instruction's portable state and ordered bus I/O.
#include "Moira.h"

#include <cstdio>
#include <type_traits>
#include <vector>

namespace {
template<class T> concept HasWindow = requires(T c) { c.pomJitWindow; };
template<class T> concept HasDtlb = requires(T c) { c.pomJitDtlbR; };
template<class T> concept HasPublication = requires(T c) { c.pomJitCache040R; };
template<class T> concept HasStep = requires(T c) { c.pomJitExecOne(); };
template<class T> concept HasProbe = requires(T c) { c.pomJitBeginTiming(); };
static_assert(HasWindow<moira::Moira> == bool(MOIRA_ENABLE_JIT));
static_assert(HasDtlb<moira::Moira> == bool(MOIRA_ENABLE_JIT));
static_assert(HasProbe<moira::Moira> == bool(MOIRA_ENABLE_JIT));
static_assert(HasPublication<moira::Moira> == bool(MOIRA_ENABLE_JIT));
static_assert(HasStep<moira::Moira> == bool(MOIRA_ENABLE_JIT));

template<bool Loading> struct Archive {
    static constexpr bool loading = Loading;
    std::vector<uint64_t>& cells;
    size_t cursor = 0;
    template<class T> void add(T& v) {
        if constexpr (std::is_array_v<T>) {
            for (auto& e : v) add(e);
        } else if constexpr (Loading) {
            v = T(cells.at(cursor++));
        } else cells.push_back(uint64_t(v));
    }
    template<class... T> void operator()(T&... v) { (add(v), ...); }
};

struct Digest {
    static constexpr bool loading = false;
    uint64_t value = 14695981039346656037ull;
    template<class T> void add(const T& v) {
        if constexpr (std::is_array_v<T>) {
            for (const auto& e : v) add(e);
        } else {
            // Fixed order, no padding or host-endian representation.
            const uint64_t n = uint64_t(v);
            for (unsigned i = 0; i < sizeof(T); ++i) {
                value ^= uint8_t(n >> (8 * i)); value *= 1099511628211ull;
            }
        }
    }
    template<class... T> void operator()(const T&... v) { (add(v), ...); }
};

class Cpu : public moira::Moira {
public:
    std::vector<moira::u8> mem = std::vector<moira::u8>(1 << 20);
    mutable Digest bus;
    void word(uint32_t a, uint16_t v) { mem[a] = uint8_t(v >> 8); mem[a + 1] = uint8_t(v); }
    void lng(uint32_t a, uint32_t v) { word(a, uint16_t(v >> 16)); word(a + 2, uint16_t(v)); }
    uint32_t lng(uint32_t a) const {
        return uint32_t(mem[a]) << 24 | uint32_t(mem[a + 1]) << 16 |
               uint32_t(mem[a + 2]) << 8 | mem[a + 3];
    }
private:
    moira::u8 read8(moira::u32 a) const override {
        const auto v = mem[a & 0xfffff]; bus(a, v, getClock(), uint8_t(0)); return v;
    }
    moira::u16 read16(moira::u32 a) const override {
        const auto v = moira::u16(mem[a & 0xfffff] << 8 | mem[(a + 1) & 0xfffff]);
        bus(a, v, getClock(), uint8_t(1)); return v;
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        bus(a, v, getClock(), uint8_t(2)); const_cast<Cpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        bus(a, v, getClock(), uint8_t(3)); const_cast<Cpu*>(this)->word(a & 0xfffff, v);
    }
};

bool run(moira::Model model, int mapping, bool cache) {
    Cpu c;
    c.setModel(model);
    c.lng(0, 0x80000); c.lng(4, 0x1000);
    // Sixteen read/modify/write iterations, then STOP. Data spans cache lines.
    const uint16_t program[] = {0x2010, 0x5280, 0x2080, 0x5888,
                                0x51c9, 0xfff6, 0x4e72, 0x2700, 0x4e71};
    for (unsigned i = 0; i < sizeof(program) / sizeof(*program); ++i)
        c.word(0x1000 + i * 2, program[i]);
    for (unsigned i = 0; i < 16; ++i) c.lng(0x50000 + i * 4, i * 0x1020304);
    if (cache) {
        c.word(0x100c, 0xf4f8); // CPUSHA BC before STOP: make dirty data visible
        c.word(0x100e, 0x4e72); c.word(0x1010, 0x2700); c.word(0x1012, 0x4e71);
    }
    c.reset(); c.setSR(0x2700); c.setD(1, 15);
    const uint32_t data = mapping ? 0x10000000 : 0x50000;
    c.setA(0, data);
    if (mapping && model == moira::Model::M68030) {
        // 32K pages, one short-descriptor level, identity code/stack and
        // a non-identity operand page. Hardware-rule gates hold the encoding.
        c.lng(0x40000, 0x19);
        // TC's initial shift ignores the high byte, so use a low logical page.
        c.setA(0, 0x10000); c.lng(0x40008, 0x50019);
        c.setCRP((uint64_t(0x7fff0002) << 32) | 0x40000);
        c.setTC(0x80f89000);
    } else if (mapping) {
        c.setITT0(0x0000c000); // code/stack transparent, operands translated
        c.lng(0x4000 + ((data >> 23) & 0x1fc), 0x4202);
        c.lng(0x4200 + ((data >> 16) & 0x1fc), 0x10002);
        c.lng(0x10000, 0x50039); // resident copyback page, U/M already set
        c.setSRP040(0x4000); c.setURP040(0x4000);
        c.setTC040(mapping == 2 ? 0xc000 : 0x8000);
    } else if (cache) {
        c.setDTT0(0x0000c020); // copyback while translation is disabled
    }
    c.setPomCache040(cache);
    c.setCACR(cache ? 0x80008000 : 0);
    Digest states;
    unsigned steps = 0;
    while (!c.isStopped() && !c.isHalted() && steps < 200) {
        c.execute(); c.pomVisitState(states); ++steps;
        if (steps == 40) {
            std::vector<uint64_t> saved;
            Archive<false> save{saved}; c.pomVisitState(save);
            c.setD(0, 0xdeadbeef);
            Archive<true> load{saved}; c.pomVisitState(load);
            std::vector<uint64_t> again;
            Archive<false> verify{again}; c.pomVisitState(verify);
            if (again != saved || load.cursor != saved.size()) return false;
        }
    }
    bool ok = steps < 200 && !c.isHalted() && uint16_t(c.getD(1)) == 0xffff;
    for (unsigned i = 0; i < 16; ++i)
        ok &= c.lng(0x50000 + i * 4) == i * 0x1020304 + 1;
    Digest memory; for (const auto v : c.mem) memory.add(v);
    std::printf("model=%u map=%d cache=%d steps=%u state=%016llx bus=%016llx ram=%016llx %s\n",
                unsigned(model), mapping, cache, steps,
                (unsigned long long)states.value, (unsigned long long)c.bus.value,
                (unsigned long long)memory.value, ok ? "PASS" : "FAIL");
    return ok;
}
} // namespace

int main() {
    bool ok = true;
    for (auto m : {moira::Model::M68000, moira::Model::M68010, moira::Model::M68020,
                   moira::Model::M68030, moira::Model::M68LC040, moira::Model::M68040}) {
        ok &= run(m, 0, false);
        if (m == moira::Model::M68030) ok &= run(m, 1, false);
        if (m >= moira::Model::M68LC040) {
            for (int mapping : {1, 2}) ok &= run(m, mapping, false);
            for (int mapping : {0, 1, 2}) ok &= run(m, mapping, true);
        }
    }
    std::fprintf(stderr, "MOIRA_ENABLE_JIT=%d sizeof(Moira)=%zu\n", MOIRA_ENABLE_JIT, sizeof(moira::Moira));
    return ok ? 0 : 1;
}
