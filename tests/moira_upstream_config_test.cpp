// POM68K — Moira fork under upstream's configuration settings.
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// Built twice: against `moira` (POM68K's MoiraConfig.h) and against
// `moira_upstream_config`, the same sources compiled with the three settings
// upstream Moira ships differently (MOIRA_PRECISE_TIMING false,
// MOIRA_EMULATE_ADDRESS_ERROR false, MOIRA_MIMIC_MUSASHI true). Upstream's own
// test runner (Runner/, against Musashi) uses those settings, so the fork must
// still build and honour them.
//
// The cell: a taken branch to an odd target. With address errors emulated the
// 68000, 68030 and 68040 take vector 3; with them disabled no model does, as in
// upstream, where misaligned() returns false. The 68030/68040 checks had been
// added outside misaligned() and ignored the setting (Moira::oddFlowTarget).

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

class TestCpu : public moira::Moira {
public:
    explicit TestCpu(moira::Model m) : mem(1 << 20, 0) { setModel(m); }
    std::vector<uint8_t> mem;

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
};

int failures = 0;

void p16(TestCpu& c, uint32_t a, uint16_t v) {
    c.mem[a] = uint8_t(v >> 8); c.mem[a + 1] = uint8_t(v);
}
void p32(TestCpu& c, uint32_t a, uint32_t v) {
    p16(c, a, uint16_t(v >> 16)); p16(c, a + 2, uint16_t(v));
}

// BRA.B *+3 at $1000: the target is $1003.
void oddBranch(moira::Model model, const char* name)
{
    TestCpu c(model);
    p32(c, 3 * 4, 0x3000);           // address-error vector
    p16(c, 0x1000, 0x6001);          // BRA.B $1003
    p16(c, 0x1002, 0x4e71);          // NOP
    c.reset();
    c.setSR(0x2700);
    c.setSP(0x8000);
    c.setPC(0x1000); c.setPC0(0x1000);
    c.setIRD(0x6001); c.setIRC(0x4e71);
    c.execute();

    const bool vector3 = c.getPC0() == 0x3000 || c.getPC() == 0x3000 ||
                         c.getPC() == 0x3002;
    if (vector3 != bool(MOIRA_EMULATE_ADDRESS_ERROR)) {
        std::printf("  FAIL %s: odd BRA target %s vector 3 (PC=%08X PC0=%08X),"
                    " MOIRA_EMULATE_ADDRESS_ERROR=%d\n", name,
                    vector3 ? "took" : "did not take", c.getPC(), c.getPC0(),
                    int(MOIRA_EMULATE_ADDRESS_ERROR));
        failures++;
    }
}

} // namespace

int main()
{
    std::printf("moira_upstream_config_test: PRECISE_TIMING=%d "
                "EMULATE_ADDRESS_ERROR=%d MIMIC_MUSASHI=%d\n",
                int(MOIRA_PRECISE_TIMING), int(MOIRA_EMULATE_ADDRESS_ERROR),
                int(MOIRA_MIMIC_MUSASHI));
    oddBranch(moira::Model::M68000, "68000");
    oddBranch(moira::Model::M68030, "68030");
    oddBranch(moira::Model::M68LC040, "68LC040");
    oddBranch(moira::Model::M68040, "68040");
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
