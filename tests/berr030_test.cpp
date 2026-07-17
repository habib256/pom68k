// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6 slice 1 gate — external /BERR plumbing (extern/moira, see
// POM68K_VENDOR.md § External /BERR + RTE $A). A 68030 on a flat 16 MB
// bus with an unmapped hole at $F00000-$F0FFFF whose read/write callbacks
// assert /BERR via Moira::extBusError(), the way V8Memory will for
// unmapped LC II I/O and the SCSI pseudo-DMA timeout:
//
//   1. data READ fault   → vector 2, format $B frame, fault address and
//      SSW (DF|DF2|RW|fc5) stacked; handler fixes the stacked PC, RTE
//      resumes (the LC II ROM address-map-probe pattern).
//   2. last-WRITE fault  → format $A frame, stacked PC = next
//      instruction; plain RTE continues (exercises the new RTE-$A path).
//   3. instruction FETCH fault → format $B, SSW = FB|RB|RW|word|fc6.
//   4. fault while STACKING the fault frame (SSP in the hole) → double
//      fault → HALT.
//
// Exit 0 = pass, 1 = fail.

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t HOLE_LO = 0xF00000, HOLE_HI = 0xF10000;

class BerrCpu : public moira::Moira {
public:
    BerrCpu() : mem(1 << 24, 0) { setModel(moira::Model::M68030); }
    std::vector<uint8_t> mem;

    bool inHole(moira::u32 a) const { return a >= HOLE_LO && a < HOLE_HI; }

private:
    // extBusError() is [[noreturn]] (throws MmuBusError through the
    // caller); const_cast as in Cpu68k::applyContention — the bus API is
    // const but a fault is real state.
    moira::u8 read8(moira::u32 a) const override {
        if (inHole(a)) const_cast<BerrCpu*>(this)->extBusError();
        return mem[a & 0xFFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (inHole(a)) const_cast<BerrCpu*>(this)->extBusError();
        return moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        if (inHole(a)) const_cast<BerrCpu*>(this)->extBusError();
        const_cast<BerrCpu*>(this)->mem[a & 0xFFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        if (inHole(a)) const_cast<BerrCpu*>(this)->extBusError();
        write8(a, moira::u8(v >> 8));
        write8(a + 1, moira::u8(v));
    }
};

int gFails = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

struct Img {
    BerrCpu cpu;
    uint32_t p = 0;

    void w16(uint16_t v) { cpu.mem[p++] = uint8_t(v >> 8); cpu.mem[p++] = uint8_t(v); }
    void w32(uint32_t v) { w16(uint16_t(v >> 16)); w16(uint16_t(v)); }
    void at(uint32_t a) { p = a; }

    uint16_t r16(uint32_t a) const {
        return uint16_t((cpu.mem[a] << 8) | cpu.mem[a + 1]);
    }
    uint32_t r32(uint32_t a) const {
        return uint32_t(r16(a)) << 16 | r16(a + 2);
    }

    void run(int steps = 64) {
        cpu.reset();                    // SSP/PC from $0/$4
        while (steps--) cpu.execute();
    }
};

// Handler building block: MOVE.W d16(A7),abs.l / MOVE.L d16(A7),abs.l /
// MOVE.L #imm,2(A7) / RTE — the frame fields live at SR+0, PC+2, FMT+6,
// SSW+10, fault address +16 (writeStackFrame{Short,Long}BusFault order).
void emitSaveW(Img& m, uint16_t off, uint32_t dst) { m.w16(0x33EF); m.w16(off); m.w32(dst); }
void emitSaveL(Img& m, uint16_t off, uint32_t dst) { m.w16(0x23EF); m.w16(off); m.w32(dst); }
void emitSetStackedPc(Img& m, uint32_t pc) { m.w16(0x2F7C); m.w32(pc); m.w16(0x0002); }

} // namespace

int main() {
    std::printf("berr030_test — external /BERR + RTE $A/$B (O6 slice 1)\n");

    // ── 1. data read fault: $B frame, handler PC fixup, resume ─────────
    {
        Img m;
        m.at(0); m.w32(0x8000); m.w32(0x1000);          // SSP, PC
        m.at(8); m.w32(0x3000);                         // vector 2

        m.at(0x1000);
        m.w16(0x2039); m.w32(HOLE_LO);                  // MOVE.L $F00000.L,D0
        m.w16(0x33FC); m.w16(0x1234); m.w32(0x2000);    // MOVE.W #$1234,$2000.L
        m.w16(0x4E72); m.w16(0x2700);                   // STOP #$2700

        m.at(0x3000);
        emitSaveW(m, 6, 0x2004);                        // format word
        emitSaveW(m, 10, 0x200C);                       // SSW
        emitSaveL(m, 16, 0x2008);                       // fault address
        emitSetStackedPc(m, 0x1006);                    // skip the probe
        m.w16(0x4E73);                                  // RTE

        m.run();
        check(m.r16(0x2000) == 0x1234, "read fault: handler RTE resumed past the probe");
        check((m.r16(0x2004) >> 12) == 0xB, "read fault: frame format $B");
        check(m.r32(0x2008) == HOLE_LO, "read fault: stacked fault address = $F00000");
        check(m.r16(0x200C) == 0x0345, "read fault: SSW = DF|DF2|RW|long|fc5 ($0345)");
    }

    // ── 2. last-write fault: $A frame, plain RTE continues ─────────────
    {
        Img m;
        m.at(0); m.w32(0x8000); m.w32(0x1100);
        m.at(8); m.w32(0x3100);

        m.at(0x1100);
        m.w16(0x33C0); m.w32(HOLE_LO);                  // MOVE.W D0,$F00000.L
        m.w16(0x33FC); m.w16(0x5678); m.w32(0x2010);    // MOVE.W #$5678,$2010.L
        m.w16(0x4E72); m.w16(0x2700);                   // STOP #$2700

        m.at(0x3100);
        emitSaveW(m, 6, 0x2014);                        // format word
        emitSaveL(m, 16, 0x2018);                       // fault address
        m.w16(0x4E73);                                  // RTE, no PC fixup

        m.run();
        check((m.r16(0x2014) >> 12) == 0xA, "write fault: frame format $A (last write)");
        check(m.r32(0x2018) == HOLE_LO, "write fault: stacked fault address = $F00000");
        check(m.r16(0x2010) == 0x5678, "write fault: RTE from $A frame continues after");
    }

    // ── 3. instruction fetch fault: $B frame, FB|RB SSW ─────────────────
    {
        Img m;
        m.at(0); m.w32(0x8000); m.w32(0x1200);
        m.at(8); m.w32(0x3200);

        m.at(0x1200);
        m.w16(0x4EF9); m.w32(HOLE_LO);                  // JMP $F00000.L
        m.at(0x1206);
        m.w16(0x33FC); m.w16(0x9ABC); m.w32(0x2020);    // MOVE.W #$9ABC,$2020.L
        m.w16(0x4E72); m.w16(0x2700);                   // STOP #$2700

        m.at(0x3200);
        emitSaveW(m, 6, 0x2024);                        // format word
        emitSaveW(m, 10, 0x202C);                       // SSW
        emitSaveL(m, 16, 0x2028);                       // fault address
        emitSetStackedPc(m, 0x1206);
        m.w16(0x4E73);                                  // RTE

        m.run();
        check((m.r16(0x2024) >> 12) == 0xB, "fetch fault: frame format $B");
        check(m.r32(0x2028) == HOLE_LO, "fetch fault: stacked fault address = $F00000");
        check(m.r16(0x202C) == 0x5066, "fetch fault: SSW = FB|RB|RW|word|fc6 ($5066)");
        check(m.r16(0x2020) == 0x9ABC, "fetch fault: handler RTE resumed");
    }

    // ── 5. Mac OS slot-probe recovery: RTE with DF set retries the ─────
    // cycle; clearing DF (bclr #0 on the stacked SSW high byte) completes
    // the read from the frame's data input buffer without a bus cycle
    // (POM68K_VENDOR.md § RTE $B honors a software-cleared SSW.DF —
    // the GISTPERSO $1313E/$1315E handler, O6.9)
    {
        Img m;
        m.at(0); m.w32(0x8000); m.w32(0x1300);
        m.at(8); m.w32(0x3300);                         // vector 2 → handler 1
        m.cpu.mem[0x2030] = 0x77;                       // must be overwritten

        m.at(0x1300);
        m.w16(0x1039); m.w32(HOLE_LO + 0xAB);           // MOVE.B $F000AB.L,D0
        m.w16(0x13C0); m.w32(0x2030);                   // MOVE.B D0,$2030.L
        m.w16(0x33FC); m.w16(0xAAAA); m.w32(0x2032);    // MOVE.W #$AAAA,$2032.L
        m.w16(0x4E72); m.w16(0x2700);                   // STOP #$2700

        m.at(0x3300);                                   // 1st fault: keep DF,
        emitSaveW(m, 10, 0x2036);                       // save SSW, re-vector
        m.w16(0x23FC); m.w32(0x3340); m.w32(0x0008);    // MOVE.L #$3340,$8.L
        m.w16(0x4E73);                                  // RTE → retry (DF set)

        m.at(0x3340);                                   // 2nd fault: give up
        m.w16(0x33FC); m.w16(0x0001); m.w32(0x2038);    // MOVE.W #1,$2038.L
        m.w16(0x08AF); m.w16(0x0000); m.w16(0x000A);    // BCLR #0,($A,A7) = DF
        m.w16(0x4E73);                                  // RTE → continue

        m.run();
        check(m.r16(0x2036) == 0x0355, "probe: SSW = DF|DF2|RW|byte|fc5 ($0355)");
        check(m.r16(0x2038) == 1, "probe: RTE with DF set re-ran and re-faulted");
        check(m.cpu.mem[0x2030] == 0xAB, "probe: DF cleared -> read = data input buffer byte");
        check(m.r16(0x2032) == 0xAAAA, "probe: execution continued past the probe");
    }

    // ── 4. fault while stacking the fault frame → double fault → HALT ──
    {
        Img m;
        m.at(0); m.w32(HOLE_LO + 0x800); m.w32(0x1000); // SSP inside the hole
        m.at(8); m.w32(0x3000);

        m.at(0x1000);
        m.w16(0x2039); m.w32(HOLE_LO);                  // MOVE.L $F00000.L,D0
        m.at(0x3000); m.w16(0x4E73);

        m.run(8);
        check(m.cpu.isHalted(), "stacking fault: double fault halts the CPU");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
