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
constexpr uint32_t MMU_TABLE = 0x40000;
constexpr uint32_t PHYS0 = 0x80000, PHYS1 = 0xA0000;
constexpr uint32_t TC_32K = (1u << 31) | (15u << 20) | (8u << 16) | (9u << 12);
constexpr uint64_t CRP_32K = (uint64_t(0x7FFF0002u) << 32) | MMU_TABLE;

class BerrCpu : public moira::Moira {
public:
    BerrCpu() : mem(1 << 24, 0) { setModel(moira::Model::M68030); }
    std::vector<uint8_t> mem;
    int readFaultsRemaining = -1;
    int writeFaultsRemaining = -1;
    int vector2Count = 0;
    bool traceReads = false;
    std::vector<moira::u32> readLog;

    bool inHole(moira::u32 a) const { return a >= HOLE_LO && a < HOLE_HI; }
    void seedIcacheLine(int line) {
        pomIcache.tag[line & 15] = 0;
        pomIcache.valid[line & 15] = 0xF;
    }
    moira::u8 icacheValidMask(int line) const { return pomIcache.valid[line & 15]; }

private:
    void didChangeCACR(moira::u32 value) override { pomInvalidateIcache030(value); }
    void didJumpToVector(int nr, moira::u32) override {
        if (nr == 2) ++vector2Count;
    }

    bool faultNow(bool write) {
        int &remaining = write ? writeFaultsRemaining : readFaultsRemaining;
        if (remaining < 0) return true;
        if (remaining == 0) return false;
        --remaining;
        return true;
    }
    void recordRead(moira::u32 a) {
        if (traceReads) readLog.push_back(a);
    }

    // extBusError() is [[noreturn]] (throws MmuBusError through the
    // caller); const_cast as in Cpu68k::applyContention — the bus API is
    // const but a fault is real state.
    moira::u8 read8(moira::u32 a) const override {
        const_cast<BerrCpu*>(this)->recordRead(a);
        if (inHole(a) && const_cast<BerrCpu*>(this)->faultNow(false))
            const_cast<BerrCpu*>(this)->extBusError();
        return mem[a & 0xFFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        const_cast<BerrCpu*>(this)->recordRead(a);
        if (inHole(a) && const_cast<BerrCpu*>(this)->faultNow(false))
            const_cast<BerrCpu*>(this)->extBusError();
        return moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        if (inHole(a) && const_cast<BerrCpu*>(this)->faultNow(true))
            const_cast<BerrCpu*>(this)->extBusError();
        const_cast<BerrCpu*>(this)->mem[a & 0xFFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        if (inHole(a) && const_cast<BerrCpu*>(this)->faultNow(true))
            const_cast<BerrCpu*>(this)->extBusError();
        auto &m = const_cast<BerrCpu*>(this)->mem;
        m[a & 0xFFFFFF] = moira::u8(v >> 8);
        m[(a + 1) & 0xFFFFFF] = moira::u8(v);
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

void w16(BerrCpu& cpu, uint32_t a, uint16_t v) {
    cpu.mem[a] = uint8_t(v >> 8); cpu.mem[a + 1] = uint8_t(v);
}
void w32(BerrCpu& cpu, uint32_t a, uint32_t v) {
    w16(cpu, a, uint16_t(v >> 16)); w16(cpu, a + 2, uint16_t(v));
}
void map030(BerrCpu& cpu, bool secondPage) {
    for (int i = 0; i < 512; ++i) w32(cpu, MMU_TABLE + 4 * i, 0);
    w32(cpu, MMU_TABLE + 0, PHYS0 | 1);       // logical $0000-$7FFF
    w32(cpu, MMU_TABLE + 4, secondPage ? PHYS1 | 1 : 0); // $8000-$FFFF
}
void start030(BerrCpu& cpu, uint32_t pc, uint16_t ird, uint16_t irc) {
    cpu.reset();
    cpu.setSR(0x2700);
    cpu.setISP(0x7000); cpu.setSP(0x7000);
    cpu.setPC(pc); cpu.setPC0(pc); cpu.setIRD(ird); cpu.setIRC(irc);
    cpu.setCRP(CRP_32K); cpu.setSRP(0); cpu.setTT0(0); cpu.setTT1(0);
    cpu.setTC(TC_32K);
}

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

    // ── 2. last-write fault: $A frame replays the pending bus cycle ────
    {
        Img m;
        m.cpu.writeFaultsRemaining = 2;                  // initial + first replay
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

        m.cpu.reset();
        m.cpu.setD(0, 0xABCD);
        for (int steps = 64; steps--;) m.cpu.execute();
        check((m.r16(0x2014) >> 12) == 0xA, "write fault: frame format $A (last write)");
        check(m.cpu.vector2Count == 2,
              "write fault: a failed replay stacks a second format-$A frame");
        check(m.r32(0x2018) == HOLE_LO, "write fault: stacked fault address = $F00000");
        check(m.r16(HOLE_LO) == 0xABCD,
              "write fault: RTE completes the pending data-output cycle");
        check(m.r16(0x2010) == 0x5678, "write fault: RTE from $A frame continues after");
    }

    // A format-$B restart must roll back the postincrement before retrying.
    // The transient read succeeds after RTE; A0 advances exactly once.
    {
        Img m;
        m.cpu.readFaultsRemaining = 1;
        m.at(0); m.w32(0x8000); m.w32(0x1180);
        m.at(8); m.w32(0x3180);
        m.cpu.mem[HOLE_LO + 0] = 0x11;
        m.cpu.mem[HOLE_LO + 1] = 0x22;
        m.cpu.mem[HOLE_LO + 2] = 0x33;
        m.cpu.mem[HOLE_LO + 3] = 0x44;

        m.at(0x1180);
        m.w16(0x2018);                                  // MOVE.L (A0)+,D0
        m.w16(0x33FC); m.w16(0xBEEF); m.w32(0x201C);
        m.w16(0x4E72); m.w16(0x2700);

        m.at(0x3180);
        emitSaveW(m, 6, 0x201E);
        m.w16(0x4E73);                                  // RTE → retry MOVE

        m.cpu.reset();
        m.cpu.setA(0, HOLE_LO);
        for (int steps = 64; steps--;) m.cpu.execute();
        check((m.r16(0x201E) >> 12) == 0xB,
              "RTE restart: transient read uses format $B");
        check(m.cpu.getD(0) == 0x11223344,
              "RTE restart: faulted read completes after retry");
        check(m.cpu.getA(0) == HOLE_LO + 4,
              "RTE restart: postincrement is applied exactly once");
        check(m.r16(0x201C) == 0xBEEF,
              "RTE restart: execution continues after retried instruction");
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

    // ── 6. translated instruction stream crosses a page boundary ──────
    // MOVE.L #$11223344,D0 begins at logical $7FFC. Its second immediate
    // word is at $8000, deliberately mapped to a non-contiguous physical
    // page. A translation cached from the opcode page would read the poison
    // $DEAD instead; each instruction word must perform its own MMU lookup.
    {
        BerrCpu cpu;
        map030(cpu, true);
        w16(cpu, PHYS0 + 0x7FFC, 0x203C);       // MOVE.L #imm,D0
        w16(cpu, PHYS0 + 0x7FFE, 0x1122);
        w16(cpu, PHYS0 + 0x8000, 0xDEAD);       // poison contiguous address
        w16(cpu, PHYS1 + 0x0000, 0x3344);
        w16(cpu, PHYS1 + 0x0002, 0x4E71);
        start030(cpu, 0x7FFC, 0x203C, 0x1122);
        cpu.execute();
        check(cpu.getD(0) == 0x11223344,
              "MMU fetch: extension word translated on the new page");
        check(cpu.getPC0() == 0x8002,
              "MMU fetch: instruction completed across the page boundary");
    }

    // Same crossing with page 1 invalid: the extension fetch, not a later
    // opcode fetch, must raise a format-$B frame naming logical $8000.
    {
        BerrCpu cpu;
        map030(cpu, false);
        w16(cpu, PHYS0 + 0x7FFC, 0x203C);
        w16(cpu, PHYS0 + 0x7FFE, 0x1122);
        w32(cpu, PHYS0 + 8, 0x3000);            // vector 2, translated
        w16(cpu, PHYS0 + 0x3000, 0x4E72);       // STOP #$2700
        w16(cpu, PHYS0 + 0x3002, 0x2700);
        start030(cpu, 0x7FFC, 0x203C, 0x1122);
        cpu.execute();
        const uint32_t spPhys = PHYS0 + cpu.getSP();
        const auto r16 = [&](uint32_t a) {
            return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]);
        };
        const auto r32 = [&](uint32_t a) {
            return uint32_t(r16(a)) << 16 | r16(a + 2);
        };
        check((r16(spPhys + 6) >> 12) == 0xB,
              "MMU fetch fault: format $B frame");
        check(r32(spPhys + 16) == 0x8000,
              "MMU fetch fault: frame names extension address $8000");
    }

    // ── 7. PMOVE operand faults pass through active translation ────────
    // WinUAE oracle (68030 mode-5): both directions use a long format-$B
    // frame at the PMOVE PC; SSW differs only by RW ($0345 / $0305).
    const auto checkPmoveFault = [](uint16_t ext, uint16_t expectedSsw,
                                    const char* direction) {
        BerrCpu cpu;
        map030(cpu, false);                         // logical $8000 invalid
        w32(cpu, PHYS0 + 8, 0x3000);               // vector 2
        w16(cpu, PHYS0 + 0x1000, 0xF010);          // PMOVE ...,TT0
        w16(cpu, PHYS0 + 0x1002, ext);
        w16(cpu, PHYS0 + 0x3000, 0x4E72);
        w16(cpu, PHYS0 + 0x3002, 0x2700);
        start030(cpu, 0x1000, 0xF010, ext);
        cpu.setA(0, 0x8000);
        cpu.setTT0(0x12345678);
        cpu.execute();

        const uint32_t spPhys = PHYS0 + cpu.getSP();
        const auto r16 = [&](uint32_t a) {
            return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]);
        };
        const auto r32 = [&](uint32_t a) {
            return uint32_t(r16(a)) << 16 | r16(a + 2);
        };
        char what[96];
        std::snprintf(what, sizeof(what), "PMOVE %s: translated fault uses format $B", direction);
        check((r16(spPhys + 6) >> 12) == 0xB, what);
        std::snprintf(what, sizeof(what), "PMOVE %s: oracle SSW is exact", direction);
        check(r16(spPhys + 10) == expectedSsw, what);
        std::snprintf(what, sizeof(what), "PMOVE %s: logical operand address is stacked", direction);
        check(r32(spPhys + 16) == 0x8000, what);
        std::snprintf(what, sizeof(what), "PMOVE %s: stacked PC identifies PMOVE", direction);
        check(r32(spPhys + 2) == 0x1000, what);
    };
    checkPmoveFault(0x0800, 0x0345, "(A0),TT0");
    checkPmoveFault(0x0A00, 0x0305, "TT0,(A0)");

    // ── 8. FMOVEM memory-indirect EA read order ────────────────────────
    // WinUAE get_disp_ea_020_mmu030 resolves the pointer first, then
    // fmovem2fpp reads the selected register image as three ascending longs.
    // Full extension $0151 = base A0, index suppressed, preindexed indirect,
    // null base/outer displacement. Static-predecrement list $80 selects FP7.
    {
        BerrCpu cpu;
        map030(cpu, false);
        w16(cpu, PHYS0 + 0x1000, 0xF230);       // FMOVEM.X ([A0]),FP7
        w16(cpu, PHYS0 + 0x1002, 0xC080);
        w16(cpu, PHYS0 + 0x1004, 0x0151);
        w16(cpu, PHYS0 + 0x1006, 0x4E71);
        w32(cpu, PHYS0 + 0x2000, 0x3000);       // indirect EA pointer
        w32(cpu, PHYS0 + 0x3000, 0x40000000);   // raw extended 2.x image
        w32(cpu, PHYS0 + 0x3004, 0x80000000);
        w32(cpu, PHYS0 + 0x3008, 0x12345678);
        cpu.setFPUModel(moira::FPUModel::M68882);
        start030(cpu, 0x1000, 0xF230, 0xC080);
        cpu.setA(0, 0x2000);
        cpu.readLog.clear();
        cpu.traceReads = true;
        cpu.execute();
        cpu.traceReads = false;

        const auto first = [&](uint32_t physical) {
            for (size_t i = 0; i < cpu.readLog.size(); ++i)
                if (cpu.readLog[i] == physical) return i;
            return cpu.readLog.size();
        };
        const size_t p0 = first(PHYS0 + 0x2000);
        const size_t p1 = first(PHYS0 + 0x2002);
        const size_t d0 = first(PHYS0 + 0x3000);
        const size_t d1 = first(PHYS0 + 0x3002);
        const size_t d2 = first(PHYS0 + 0x3004);
        const size_t d3 = first(PHYS0 + 0x3006);
        const size_t d4 = first(PHYS0 + 0x3008);
        const size_t d5 = first(PHYS0 + 0x300A);
        check(p0 < p1 && p1 < d0,
              "FMOVEM indirect: pointer long read before register image");
        check(d0 < d1 && d1 < d2 && d2 < d3 && d3 < d4 && d4 < d5,
              "FMOVEM indirect: three operand longs read in ascending order");
        uint32_t fp[3] {};
        cpu.getFP(7, fp);
        check(fp[0] == 0x40000000 && fp[1] == 0x80000000 && fp[2] == 0x12345678,
              "FMOVEM indirect: WinUAE raw FP7 image reproduced");
    }

    // ── 9. exact 68030 instruction-cache clear strobes ────────────────
    // CEI must preserve unrelated lines and the other three longwords in its
    // line; CI clears them all. CAAR A7-A4 selects the line, A3-A2 the word.
    {
        BerrCpu cpu;
        cpu.seedIcacheLine(2);
        cpu.seedIcacheLine(7);
        cpu.setCAAR(0x74);                  // line 7, longword 1
        cpu.setCACR(0x05);                  // EI | CEI
        check(cpu.icacheValidMask(2) == 0xF,
              "CACR.CEI: unrelated instruction-cache line preserved");
        check(cpu.icacheValidMask(7) == 0xD,
              "CACR.CEI: only CAAR-selected longword cleared");

        cpu.seedIcacheLine(2);
        cpu.seedIcacheLine(7);
        cpu.setCACR(0x09);                  // EI | CI
        check(cpu.icacheValidMask(2) == 0 && cpu.icacheValidMask(7) == 0,
              "CACR.CI: complete instruction cache cleared");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
