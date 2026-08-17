// POM68K — MC68020 CALLM/RTM architectural module-support gate.

#include "Moira.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

class TestCpu : public moira::Moira {
public:
    TestCpu() : mem(1 << 20, 0) { setModel(moira::Model::M68020); }
    std::vector<uint8_t> mem;
    mutable std::vector<std::pair<uint32_t, uint32_t>> cpuWrites;
    uint8_t cal = 0xa6;
    uint8_t status = 1;
    bool cpuSpacePresent = true;

private:
    moira::u8 read8(moira::u32 a) const override { return mem[a & 0xfffff]; }
    moira::u16 read16(moira::u32 a) const override {
        return moira::u16((mem[a & 0xfffff] << 8) | mem[(a + 1) & 0xfffff]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        const_cast<TestCpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        write8(a, moira::u8(v >> 8)); write8(a + 1, moira::u8(v));
    }
    bool readModuleCpuSpace8(moira::u32 a, moira::u8& v) const override {
        if (!cpuSpacePresent) return false;
        if (a == 0x10000) v = cal;
        else if (a == 0x10004) v = status;
        else return false;
        return true;
    }
    bool writeModuleCpuSpace8(moira::u32 a, moira::u8 v) const override {
        if (!cpuSpacePresent) return false;
        cpuWrites.emplace_back(a, v);
        return a == 0x10008 || a == 0x1000c;
    }
    bool writeModuleCpuSpace32(moira::u32 a, moira::u32 v) const override {
        if (!cpuSpacePresent) return false;
        cpuWrites.emplace_back(a, v);
        return a >= 0x10040 && a <= 0x1005c;
    }
};

int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { \
    std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

void p8(TestCpu& c, uint32_t a, uint8_t v) { c.mem[a & 0xfffff] = v; }
void p16(TestCpu& c, uint32_t a, uint16_t v) {
    p8(c, a, uint8_t(v >> 8)); p8(c, a + 1, uint8_t(v));
}
void p32(TestCpu& c, uint32_t a, uint32_t v) {
    p16(c, a, uint16_t(v >> 16)); p16(c, a + 2, uint16_t(v));
}
uint32_t g32(const TestCpu& c, uint32_t a) {
    return uint32_t(c.mem[a] << 24 | c.mem[a + 1] << 16 |
                    c.mem[a + 2] << 8 | c.mem[a + 3]);
}

void prepare(TestCpu& c, uint32_t control, uint16_t entryWord, uint8_t argc = 4)
{
    std::fill(c.mem.begin(), c.mem.end(), 0);
    c.cpuWrites.clear(); c.cpuSpacePresent = true; c.status = 1; c.cal = 0xa6;
    p32(c, 14 * 4, 0x3000);                // format-error vector
    p16(c, 0x1000, 0x06d0);                // CALLM #n,(A0)
    p16(c, 0x1002, argc);
    p32(c, 0x2000, control);               // module descriptor
    p32(c, 0x2004, 0x2200);                // entry-word pointer
    p32(c, 0x2008, 0x55667788);            // module data pointer
    p32(c, 0x200c, 0x9000);                // optional new stack pointer
    p16(c, 0x2200, entryWord);
    p16(c, 0x2202, uint16_t(0x06c0 | ((entryWord >> 12) & 15))); // RTM Rn

    c.reset();
    c.setSR(0x2715);
    c.setSP(0x8000);
    c.setA(0, 0x2000);
    c.setD(2, 0x11223344);
    c.setPC(0x1000); c.setPC0(0x1000);
    c.setIRD(0x06d0); c.setIRC(argc);
}

} // namespace

int main()
{
    TestCpu c;

    // Type $00, option 000: build the exact 24-byte frame on the old stack.
    prepare(c, 0x00000000, 0x2000);         // select D2
    c.execute();
    CHECK(c.getPC0() == 0x2202, "type00 CALLM PC0=%08X", c.getPC0());
    CHECK(c.getSP() == 0x7fe8, "type00 CALLM SP=%08X", c.getSP());
    CHECK(c.getD(2) == 0x55667788, "type00 module D2=%08X", c.getD(2));
    CHECK(g32(c, 0x7fe8) == 0x00000015, "frame control=%08X", g32(c, 0x7fe8));
    CHECK(g32(c, 0x7fec) == 4, "frame argc=%08X", g32(c, 0x7fec));
    CHECK(g32(c, 0x7ff0) == 0x2000, "frame descriptor=%08X", g32(c, 0x7ff0));
    CHECK(g32(c, 0x7ff4) == 0x1004, "frame saved PC=%08X", g32(c, 0x7ff4));
    CHECK(g32(c, 0x7ff8) == 0x11223344, "frame saved D2=%08X", g32(c, 0x7ff8));
    CHECK(g32(c, 0x7ffc) == 0, "option000 saved SP=%08X", g32(c, 0x7ffc));

    c.setCCR(0);                            // prove RTM restores the frame CCR
    c.execute();
    CHECK(c.getPC0() == 0x1004, "type00 RTM PC0=%08X", c.getPC0());
    CHECK(c.getSP() == 0x8004, "type00 RTM SP=%08X", c.getSP());
    CHECK(c.getD(2) == 0x11223344, "type00 RTM D2=%08X", c.getD(2));
    CHECK(c.getCCR() == 0x15, "type00 RTM CCR=%02X", c.getCCR());

    // Option 100 keeps the caller's argument-stack pointer in the frame.
    prepare(c, 0x80000000, 0x2000, 7);
    c.execute();
    CHECK(g32(c, c.getSP() + 0x14) == 0x8000,
          "option100 saved SP=%08X", g32(c, c.getSP() + 0x14));

    // Type $01, status 4: CPU-space validation, stack switch and argument copy.
    prepare(c, 0x015a0000, 0x2000);
    p32(c, 0x8000, 0xdeadbeef);
    c.status = 4;
    c.execute();
    CHECK(c.getSP() == 0x8fe8, "type01 switched SP=%08X", c.getSP());
    CHECK(g32(c, 0x9000) == 0xdeadbeef, "type01 copied args=%08X", g32(c, 0x9000));
    CHECK(g32(c, 0x8fe8) == 0x01a60015, "type01 frame control=%08X", g32(c, 0x8fe8));
    CHECK(g32(c, 0x8ffc) == 0x8000, "type01 saved SP=%08X", g32(c, 0x8ffc));
    CHECK(c.cpuWrites.size() == 2 && c.cpuWrites[0].first == 0x10054 &&
          c.cpuWrites[0].second == 0x2000 && c.cpuWrites[1].first == 0x10008 &&
          c.cpuWrites[1].second == 0x5a, "type01 CALLM CPU-space sequence");

    c.cpuWrites.clear(); c.status = 4;
    c.execute();
    CHECK(c.getPC0() == 0x1004 && c.getSP() == 0x8004,
          "type01 RTM PC0/SP=%08X/%08X", c.getPC0(), c.getSP());
    CHECK(c.cpuWrites.size() == 1 && c.cpuWrites[0].first == 0x1000c &&
          c.cpuWrites[0].second == 0xa6, "type01 RTM DAL write");

    // Invalid descriptors and absent access-control hardware take vector 14.
    prepare(c, 0x20000000, 0x2000);         // unsupported OPT=001
    c.execute();
    CHECK(c.getPC0() == 0x3000 && c.getD(2) == 0x11223344,
          "bad descriptor did not take format error cleanly");

    prepare(c, 0x015a0000, 0x2000);
    c.cpuSpacePresent = false;
    c.execute();
    CHECK(c.getPC0() == 0x3000 && c.getD(2) == 0x11223344,
          "absent type01 hardware did not take format error cleanly");

    if (failures) {
        std::printf("[callm_rtm_test] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[callm_rtm_test] OK: type00 + type01 stack/access-control paths\n");
    return 0;
}
