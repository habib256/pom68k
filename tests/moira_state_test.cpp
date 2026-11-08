// POM68K — instruction-boundary state on a flat bus, without a Macintosh.
// Save while a 68882 result or a 68030 DF-cleared retry is pending, restore
// into a fresh CPU, then compare execution with the uninterrupted CPU.
#include "Moira.h"
#include "../src/SaveState.h"

#include <cstdio>
#include <vector>

namespace {
constexpr moira::u32 kHole = 0x80000;
class Cpu : public moira::Moira {
public:
    std::vector<moira::u8> mem = std::vector<moira::u8>(1 << 20);
    mutable int holeReads = 0;
    moira::i64 irqClock = -1;
    explicit Cpu(moira::Model model, moira::FPUModel fpu = moira::FPUModel::NONE) {
        setModel(model); setFPUModel(fpu);
    }
    template<class Ar> void state(Ar& ar) { pomVisitState(ar); }
    void debugFlags(int value) { flags = (flags & ~debugMask()) | value; }
    int debugFlags() const { return flags & debugMask(); }
    static constexpr int debugMask() {
        return moira::State::CHECK_BP | moira::State::CHECK_WP |
               moira::State::CHECK_CP | moira::State::LOGGING;
    }
    void word(moira::u32 a, moira::u16 v) { mem[a] = v >> 8; mem[a + 1] = v; }
    void lng(moira::u32 a, moira::u32 v) { word(a, v >> 16); word(a + 2, v); }
    moira::u16 word(moira::u32 a) const { return mem[a] << 8 | mem[a + 1]; }
    void program(moira::u32 a, std::initializer_list<moira::u16> words) {
        for (auto w : words) { word(a, w); a += 2; }
    }
    void start() { lng(0, 0x8000); lng(4, 0x1000); reset(); }
private:
    moira::u8 read8(moira::u32 a) const override {
        if (a == kHole) { ++holeReads; const_cast<Cpu*>(this)->extBusError(); }
        return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override { return word(a & 0xfffff); }
    void write8(moira::u32 a, moira::u8 v) const override {
        const_cast<Cpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        const_cast<Cpu*>(this)->word(a & 0xfffff, v);
    }
    void sync(int cycles) override {
        clock += cycles;
        if (irqClock >= 0 && clock >= irqClock) { irqClock = -1; setIPL(3); }
    }
};
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
    failures += !ok;
}
std::vector<moira::u8> save(Cpu& cpu) {
    std::vector<moira::u8> bytes;
    sav::Writer ar(bytes); cpu.state(ar); return bytes;
}
void restore(Cpu& src, Cpu& dst) {
    auto bytes = save(src);
    dst.mem = src.mem;
    sav::Reader ar(bytes.data(), bytes.size()); dst.state(ar);
    check(ar.ok() && ar.remaining() == 0, "CPU archive consumed exactly");
}
void pending68882() {
    Cpu direct(moira::Model::M68030, moira::FPUModel::M68882);
    direct.program(0x1000, {0xf23c, 0x4000, 0, 4, 0xf200, 0x0004, 0x4e71});
    direct.lng(0x6c, 0x3400);
    direct.program(0x3400, {0xf321, 0xf359, 0x4e73}); // FSAVE, FRESTORE, RTE
    direct.start(); direct.execute();                // FP0 = 4
    direct.setSR(0x2000); direct.setA(1, 0x5000);
    direct.irqClock = direct.getClock() + 16;
    direct.execute();                               // interrupted SQRT
    check(direct.getPC0() == 0x3400 && direct.word(direct.getSP() + 6) == 0x906c,
          "68882: save at handler boundary with format-$9 continuation");
    Cpu loaded(moira::Model::M68030, moira::FPUModel::M68882);
    restore(direct, loaded);
    for (int i = 0; i < 3; ++i) { direct.execute(); loaded.execute(); }
    moira::u32 fp[3]; loaded.getFP(0, fp);
    check(fp[0] == 0x40000000 && fp[1] == 0x80000000 && fp[2] == 0,
          "68882: restored SQRT(4) completes with FP0 = 2");
    check(direct.mem == loaded.mem && save(direct) == save(loaded),
          "68882: restored FSAVE/FRESTORE/RTE matches uninterrupted state and RAM");
}
void clearedDf030() {
    Cpu direct(moira::Model::M68030);
    direct.program(0x1000, {0x1039, 0x0008, 0x0000, 0x4e71}); // MOVE.B hole,D0
    direct.lng(8, 0x3000);
    direct.program(0x3000, {0x08af, 0, 0x000a, 0x4e73}); // BCLR #0,10(SP); RTE
    direct.start(); direct.execute();                 // /BERR
    const auto sp = direct.getSP();
    check(direct.getPC0() == 0x3000 && (direct.word(sp + 6) >> 12) == 0xb,
          "68030: operand fault reaches format-$B handler");
    direct.lng(sp + 44, 0x123456ab);                  // software data input buffer
    direct.execute(); direct.execute();              // clear DF, RTE arms retry
    check(direct.getPC0() == 0x1000 && direct.holeReads == 1,
          "68030: save between RTE and the retried instruction");
    Cpu loaded(moira::Model::M68030);
    restore(direct, loaded);
    direct.execute(); loaded.execute();
    check(loaded.getPC0() == 0x1006 && (loaded.getD(0) & 0xff) == 0xab && !loaded.holeReads,
          "68030: restored retry consumes stacked data without another bus read");
    check(direct.mem == loaded.mem && save(direct) == save(loaded),
          "68030: restored DF-cleared retry matches uninterrupted state and RAM");
}
void pipe030() {
    Cpu direct(moira::Model::M68030);
    direct.program(0x1000, {0xf010, 0x4000, 0x7001, 0x7202, 0x4e71, 0x4e71});
    direct.lng(0x4000, 0);                            // PMOVE (A0),TC (E stays clear)
    direct.start(); direct.setA(0, 0x4000); direct.execute();
    check(direct.getPC0() == 0x1004 && direct.pomMmuPipeLive(),
          "68030: PMOVE leaves the old instruction stream live at a boundary");
    direct.program(0x1004, {0x7007, 0x7208});          // old prefetched words must win
    Cpu loaded(moira::Model::M68030);
    restore(direct, loaded);
    direct.execute(); loaded.execute();
    check(direct.getD(0) == 1 && loaded.getD(0) == 1,
          "68030: restored pipe executes captured MOVEQ #1, not patched #7");
    check(save(direct) == save(loaded), "68030: restored pipe matches uninterrupted CPU");
}
void frame040(bool busy) {
    Cpu direct(moira::Model::M68040, moira::FPUModel::M68040);
    direct.program(0x1000, {0xf200, moira::u16(busy ? 0x0080 : 0x0001), 0x4e71});
    direct.lng(busy ? 55 * 4 : 11 * 4, 0x3000);
    direct.program(0x3000, {0xf310, 0x4e71});          // FSAVE (A0)
    direct.start(); direct.setA(0, 0x4000);
    const moira::u32 fp[] = {busy ? 0u : 0x3fff0000u,
                           busy ? 0x40000000u : 0x80000000u, 0x12345678u};
    direct.setFP(0, fp); direct.execute();
    check(direct.getPC0() == 0x3000,
          busy ? "68040: datatype trap reaches boundary before FSAVE BUSY"
               : "68040: FINT trap reaches boundary before FSAVE UNIMP");
    Cpu loaded(moira::Model::M68040, moira::FPUModel::M68040);
    restore(direct, loaded);
    direct.execute(); loaded.execute();
    check(direct.word(0x4000) == (busy ? 0x4160 : 0x4130), "68040: expected FSAVE frame header");
    check(direct.mem == loaded.mem && save(direct) == save(loaded),
          busy ? "68040: restored BUSY micro-state gives identical FSAVE payload"
               : "68040: restored UNIMP micro-state gives identical FSAVE payload");
}
void hostState() {
    Cpu src(moira::Model::M68LC040), dst(moira::Model::M68LC040);
    src.start(); dst.start();
    src.debugFlags(moira::State::CHECK_BP);
    dst.debugFlags(moira::State::CHECK_WP);
    const auto plain = save(src);
    src.debugFlags(moira::State::LOGGING);
    check(save(src) == plain, "host: debugger requests do not enter the archive");
    src.mmu040MovemArmed = true; src.mmu040MovemEa = 0x12345678;
    restore(src, dst);
    check(dst.debugFlags() == moira::State::CHECK_WP && dst.getModel() == moira::Model::M68LC040,
          "host: load preserves live debugger requests and model");
    check(dst.mmu040MovemArmed && dst.mmu040MovemEa == 0x12345678,
          "host: explicitly armed MOVEM restart latch round-trips");
    src.mmu040MovemArmed = false;
    src.mmu040MovemEa = 0xabcdef00;
    const auto inactive = save(src);
    src.mmu040MovemEa = 0;
    check(save(src) == inactive, "host: inactive MOVEM scratch EA does not affect the archive");
    restore(src, dst);
    check(!dst.mmu040MovemArmed && !dst.mmu040MovemEa,
          "host: inactive saved latch clears an armed destination");
}
}
int main() {
    pending68882(); clearedDf030(); pipe030(); frame040(false); frame040(true);
    hostState();
    std::printf("moira_state_test: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
