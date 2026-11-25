// Local evidence only: runs Apple's 68040 FPSP (from a user-provided Quadra 605
// ROM, never committed) on Moira for a few unimplemented FPU instructions.
#include "Moira.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include <iterator>

using namespace moira;

class Cpu : public Moira {
public:
    using Moira::reg;
    using Moira::fpu;
    mutable std::vector<u8> ram = std::vector<u8>(1 << 24);
    std::vector<u8> rom;
    static constexpr u32 romBase = 0x40800000;

    u8 read8(u32 a) const override {
        if (a >= romBase && a < romBase + rom.size()) return rom[a - romBase];
        return a < ram.size() ? ram[a] : 0;
    }
    u16 read16(u32 a) const override { return read8(a) << 8 | read8(a + 1); }
    u32 read32(u32 a) const override { return read16(a) << 16 | read16(a + 2); }
    void write8(u32 a, u8 v) const override { if (a < ram.size()) ram[a] = v; else printf("  write outside RAM %08x\n", a); }
    void write16(u32 a, u16 v) const override { write8(a, v >> 8); write8(a + 1, v & 0xFF); }
    void write32(u32 a, u32 v) const override { write16(a, v >> 16); write16(a + 2, v & 0xFFFF); }

    Cpu(const char *path, u32 entry) {
        std::ifstream f(path, std::ios::binary);
        rom.assign(std::istreambuf_iterator<char>(f), {});
        setModel(CPUModel::M68040);
        setCPModel(CPModel::NONE);
        write32(0, 0x80000); write32(4, 0x1000);
        reset();
        write32(11 * 4, romBase + entry);           // F-line vector -> fpsp_unimp
        for (u32 v = 2; v < 64; v++) if (v != 11) write32(v * 4, 0x9000);  // anything else: trap at $9000
        write16(0x9000, 0x4E72); write16(0x9002, 0x2700);                  // STOP #$2700
    }
};

static double toD(const MFloat80 &x) {
    if (x.isZero()) return 0;
    int e = int(x.signExp() & 0x7FFF) - 16383;
    double m = std::ldexp(double(x.mantissa()), -63);
    double d = std::ldexp(m, e);
    return (x.signExp() & 0x8000) ? -d : d;
}
static MFloat80 fromD(double d) {
    if (d == 0) return MFloat80(0, 0);
    int e; double m = std::frexp(std::fabs(d), &e);    // m in [0.5,1)
    u64 man = u64(std::ldexp(m, 64));
    u16 se = u16(e - 1 + 16383) | (d < 0 ? 0x8000 : 0);
    return MFloat80(se, man);
}

static u32 gFpcr = 0;
static bool gLog = false;
static void runCase(const char *path, u32 entry, const char *name, std::initializer_list<u16> code,
                    double fp0, double fp1, double expect, u32 a0data[3] = nullptr)
{
    Cpu cpu(path, entry);
    u32 addr = 0x1000;
    for (auto w : code) { cpu.write16(addr, w); addr += 2; }
    u32 end = addr;
    cpu.write16(addr, 0x4E71); cpu.write16(addr + 2, 0x4E71);
    cpu.fpu.fpr[0] = fromD(fp0); cpu.fpu.fpr[1] = fromD(fp1);
    cpu.fpu.fpcr = gFpcr;
    cpu.reg.a[0] = 0x3000;
    if (a0data) for (int i = 0; i < 3; i++) cpu.write32(0x3000 + 4 * i, a0data[i]);
    cpu.debugger.jump(0x1000);
    int n = 0;
    for (; n < 200000; n++) {
        if (cpu.getPC0() == end || cpu.getPC0() == 0x9000) break;
        u16 op = cpu.read16(cpu.getPC0());
        bool fsave = (op & 0xFFC0) == 0xF300, frestore = (op & 0xFFC0) == 0xF340;
        if (gLog && frestore && (op & 0x38) == 0x18) printf("    FRESTORE (A%d)+ format %08x\n", op & 7, cpu.read32(cpu.reg.a[op & 7]));
        cpu.execute();
        if (gLog && fsave && (op & 0x38) == 0x20) printf("    FSAVE -(A%d)    format %08x\n", op & 7, cpu.read32(cpu.reg.a[op & 7]));
    }
    bool ok = cpu.getPC0() == end && std::fabs(toD(cpu.fpu.fpr[0]) - expect) <= 1e-12 * std::fabs(expect) + 1e-300;
    printf("%-28s pc=%08x after %6d instr  fp0=%04x:%016llx = %.17g  expect %.17g  %s\n", name,
           cpu.getPC0(), n, cpu.fpu.fpr[0].signExp(), (unsigned long long)cpu.fpu.fpr[0].mantissa(),
           toD(cpu.fpu.fpr[0]), expect, ok ? "OK" : "WRONG");
}

int main(int argc, char **argv)
{
    const char *path = argv[1];
    u32 entry = u32(strtoul(argv[2], nullptr, 16));
    u32 single[3] = { 0x3F000000, 0, 0 };               // 0.5f
    u32 packed[3] = { 0x40010005, 0, 0 };               // 5E-1
    runCase(path, entry, "FSIN.X FP1,FP0",    { 0xF200, 0x040E }, 0, 0.5, std::sin(0.5));
    runCase(path, entry, "FETOX.X FP0,FP0",   { 0xF200, 0x0010 }, 1.0, 0, std::exp(1.0));
    runCase(path, entry, "FMOD.X FP1,FP0",    { 0xF200, 0x0421 }, 7.5, 2.0, 1.5);
    runCase(path, entry, "FSCALE.X FP1,FP0",  { 0xF200, 0x0426 }, 3.0, 2.0, 12.0);
    runCase(path, entry, "FSIN.S (A0),FP0",   { 0xF210, 0x440E }, 0, 0, std::sin(0.5), single);
    runCase(path, entry, "FSIN.P (A0),FP0",   { 0xF210, 0x4C0E }, 0, 0, std::sin(0.5), packed);
    runCase(path, entry, "FMOD.P (A0),FP0",   { 0xF210, 0x4C21 }, 7.25, 0, 0.25, packed);
    u32 sden[3] = { 0x00000001, 0, 0 };                 // smallest single denormal
    u32 xden[3] = { 0x00000000, 0x00000000, 0x00000003 }; // extended denormal
    u32 xunn[3] = { 0x3FFF0000, 0x40000000, 0x00000000 }; // unnormal 0.5
    runCase(path, entry, "FSIN.S denormal",   { 0xF210, 0x440E }, 0, 0, std::ldexp(1.0, -149), sden);
    runCase(path, entry, "FSIN.X denormal",   { 0xF210, 0x480E }, 0, 0, std::ldexp(3.0, -16445), xden);
    runCase(path, entry, "FSIN.X unnormal",   { 0xF210, 0x480E }, 0, 0, std::sin(0.5), xunn);
    gFpcr = 0x1000;  // OVFL enabled: the FPSP builds a busy frame and restores it
    gLog = true;
    runCase(path, entry, "FETOX ovfl (enabled)", { 0xF200, 0x0410 }, 0, 20000.0, INFINITY);
    runCase(path, entry, "FETOX ovfl; FSAVE -(A7)", { 0xF200, 0x0410, 0xF327 }, 0, 20000.0, INFINITY);
    gLog = false;
    gFpcr = 0;
    runCase(path, entry, "FMOVE.L D0,FP2; FSIN", { 0xF200, 0x4100, 0xF200, 0x040E }, 0, 0.5, std::sin(0.5));
    runCase(path, entry, "FMOVECR #$32,FP0",  { 0xF200, 0x5C32 }, 0, 0, 1.0);
    return 0;
}
