// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// Fault transparency: an access fault whose handler lets the access through
// and returns must leave the state of a run that never faulted.
// [B] MC68030UM-1989 §8.2.1 — an RTE from a bus-fault frame reruns the
//     faulted cycle (format $A) or continues the instruction (format $B).
// [B] MC68040UM-1993 §8.4.6 — a faulted read restarts the instruction.
//
// Every instruction of a generated list runs once on a flat bus while the
// board records the cycles it makes — operands, extension words, prefetch.
// Then, for each recorded cycle in turn, the board answers that one cycle
// with /BERR — once — and the handler is a bare RTE. Registers, CCR, PC and the data region are
// compared with the reference run.
//
// The oracle vectors judge the FRAME of a fault; nothing judged what the
// frame is for. The first run of this idea, by hand (moira_rules_test
// stackFaultRoundTrip, 2026-11-20), found a 68030 JSR pushing four bytes
// low after its rerun and an RTR popping two bytes far.
//
// Scope. 68030: read and write cycles, a bare RTE. 68040: the same, with
// the handler completing the frame's pending write-backs before its RTE, as
// a 68040 access-error handler must (run() below); the stores of CAS, CAS2
// and the bit-field instructions are left out (see main()). MOVES, MOVE
// from SR, FSAVE, FRESTORE, RTE and the 68030's PMOVE run in supervisor
// mode on the swept stack, where the fault frame itself lands.
// The plain 68020 completes its cycles in software
// (berr020_test) and is not swept here. The 68030 runs with a 68882, the
// 68040 with its own FPU; FP registers and FPCR/FPSR/FPIAR are compared. A long operand is two word cycles
// on this bus; the second is faulted as well as the first, which is what a
// 16-bit port with dynamic bus sizing can do to a 68030.
//
// Exit 0 = pass, 1 = fail. `--verbose` lists every instruction; a number
// restricts the run to that instruction index.

#include "MoiraCompat.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCode = 0x1000, kHandler = 0x2000, kTrap = 0x2800, kTarget = 0x3000;
constexpr uint32_t kSuperStack = 0x8000;
constexpr uint32_t kData = 0x40000, kDataSize = 0x10000;
constexpr uint32_t kUserStack = kData + 0xC000;
constexpr uint32_t kPointee = kData + 0x9000;
constexpr uint16_t kNop = 0x4e71, kRte = 0x4e73;

struct Cycle {
    uint32_t addr; bool write;
    bool operator==(const Cycle& o) const { return addr == o.addr && write == o.write; }
};

class Cpu : public MoiraCompat {
public:
    explicit Cpu(moira::CPUModel model) : mem(1 << 20, 0), model_(model) { setModel(model); }
    std::vector<uint8_t> mem;
    bool record = false;
    mutable std::vector<Cycle> cycles;
    mutable bool armed = false;
    Cycle hole {};
    mutable unsigned trips = 0;

private:
    moira::CPUModel model_;
    // The cycles the instruction under test makes: its operands, its own
    // words and prefetch, and the first words of a JSR/BSR/RTS target.
    static bool swept(moira::u32 a) {
        a &= 0xffffff;
        return a - kData < kDataSize || a - kCode < 0x100 || a - kTarget < 0x10;
    }
    void cycle(moira::u32 a, bool write) const {
        a &= 0xffffff;
        if (armed && a == hole.addr && write == hole.write) {
            armed = false;                      // one /BERR, then the board answers
            ++trips;
            auto& self = *const_cast<Cpu*>(this);
            if (model_ >= moira::CPUModel::M68EC040) self.extBusError040();
            else self.extBusError();
        }
        if (record && swept(a)) cycles.push_back({ a, write });
    }
    moira::u8 read8(moira::u32 a) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        cycle(a, false);
        return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        cycle(a, false);
        return moira::u16((mem[a & 0xfffff] << 8) | mem[(a + 1) & 0xfffff]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        cycle(a, true);
        const_cast<Cpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        cycle(a, true);
        auto& m = const_cast<Cpu*>(this)->mem;
        m[a & 0xfffff] = moira::u8(v >> 8); m[(a + 1) & 0xfffff] = moira::u8(v);
    }
};

struct Inst {
    std::vector<uint16_t> words;
    bool a0IsCode = false;      // JSR through A0
    bool only030 = false;
    bool super = false;         // privileged: runs in supervisor mode, on the swept stack
    int cell = -1;              // a word preset where A1 points (and a zero word after it)
    std::vector<uint16_t> stack;    // words preset at the stack pointer (an RTE frame)
    std::string name;
};

struct Final {
    uint32_t d[8] {}, a[7] {}, usp = 0, pc = 0;
    uint16_t ccr = 0;
    uint32_t fp[8][3] {}, fpcr = 0, fpsr = 0, fpiar = 0;
    bool trapped = false, wedged = false;
    unsigned trips = 0;
    std::vector<uint8_t> data;
};

uint32_t rnd(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

void p16(Cpu& c, uint32_t a, uint16_t v) { c.mem[a] = uint8_t(v >> 8); c.mem[a + 1] = uint8_t(v); }
void p32(Cpu& c, uint32_t a, uint32_t v) { p16(c, a, uint16_t(v >> 16)); p16(c, a + 2, uint16_t(v)); }

Final run(moira::CPUModel model, const Inst& in, uint32_t seed, const Cycle* hole,
          std::vector<Cycle>* cyclesOut)
{
    Cpu c(model);
    for (uint32_t v = 0; v < 256; v++) p32(c, v * 4, kTrap);
    p32(c, 2 * 4, kHandler);
    p16(c, kHandler, kRte);
    for (uint32_t a = kTrap; a < kTrap + 16; a += 2) p16(c, a, kNop);
    for (uint32_t a = kTarget; a < kTarget + 16; a += 2) p16(c, a, kNop);
    for (uint32_t a = kCode; a < kCode + 0x100; a += 2) p16(c, a, kNop);
    for (size_t i = 0; i < in.words.size(); i++) p16(c, kCode + 2 * uint32_t(i), in.words[i]);

    uint32_t s = seed * 2654435761u + 1;
    for (uint32_t a = kData; a < kData + kDataSize; a++) c.mem[a] = uint8_t(rnd(s));
    uint32_t a[7];
    for (int i = 0; i < 7; i++) a[i] = kData + 0x4000 + 0x800 * uint32_t(i) + (rnd(s) & 0x1e);
    // What a pop, an UNLK or a memory-indirect operand reads as an address.
    p32(c, kUserStack, kTarget); p32(c, kUserStack + 2, kTarget);
    p32(c, a[3], kPointee);
    p32(c, a[6], kData + 0xA000);
    if (in.cell >= 0) p32(c, a[1], uint32_t(in.cell) << 16);
    for (size_t i = 0; i < in.stack.size(); i++) p16(c, kUserStack + 2 * uint32_t(i), in.stack[i]);

    if (model == moira::CPUModel::M68030) c.setFPUModel(MoiraFpu::M68882);
    c.reset();
    for (int i = 0; i < 8; i++) {
        const uint32_t w[3] = { 0x3ffd0000u + ((rnd(s) & 7) << 16) + (rnd(s) & 0x80000000u),
                                0x80000000u | rnd(s), rnd(s) };
        c.setFP(i, w);
    }
    c.setSR(0x2700);
    c.setSP(kSuperStack);
    c.setPC(kCode); c.setPC0(kCode);
    c.setIRD(in.words[0]); c.setIRC(in.words.size() > 1 ? in.words[1] : kNop);
    for (int i = 0; i < 7; i++) c.setD(i, rnd(s));
    c.setD(7, rnd(s) & 0x3e);                  // the index register of mode 6
    for (int i = 0; i < 7; i++) c.setA(i, a[i]);
    if (in.a0IsCode) c.setA(0, kTarget);
    c.setSFC(5); c.setDFC(5);
    if (in.super) {
        // The interrupt stack is the swept one: the fault frame lands below
        // the instruction's own stack data and is left there by the RTE.
        c.setSR(uint16_t(0x2700 | (rnd(s) & 0x1f)));
        c.setSP(kUserStack);
    } else {
        c.setSR(uint16_t(rnd(s) & 0x1f));
        c.setUSP(kUserStack); c.setSP(kUserStack);
    }

    if (hole) { c.hole = *hole; c.armed = true; }
    c.record = cyclesOut != nullptr;

    Final f;
    auto step = [&] { try { c.execute(); } catch (...) { f.wedged = true; } };
    step();
    c.record = false;
    bool writtenBack = false;
    for (int i = 0; i < 8 && !f.wedged; i++) {
        if (c.getPC0() - kTrap < 16) { f.trapped = true; break; }
        if (c.getPC0() == kHandler) {
            if (model >= moira::CPUModel::M68EC040 && !writtenBack) {
                // What a 68040 access-error handler does before its RTE:
                // the pending write-backs of the format-$7 frame.
                // [B] MC68040UM-1993 §8.4.6.5 — WB2 and WB3 are completed by
                //     software when their V bit is set (WB2 only for a
                //     normal access, TT = 0).
                writtenBack = true;
                const uint32_t sp = c.getSP();

                auto g16 = [&](uint32_t a) { return uint32_t(c.mem[a] << 8 | c.mem[a + 1]); };
                auto g32 = [&](uint32_t a) { return g16(a) << 16 | g16(a + 2); };
                const struct { uint32_t status, addr, data; } wb[2] = {
                    { sp + 0x10, sp + 0x20, sp + 0x24 },    // WB2
                    { sp + 0x0e, sp + 0x18, sp + 0x1c },    // WB3
                };
                for (int n = 0; n < 2; n++) {
                    const uint32_t st = g16(wb[n].status);
                    if (!(st & 0x80) || (n == 0 && (st & 0x18))) continue;
                    const uint32_t a = g32(wb[n].addr) & 0xfffff, d = g32(wb[n].data);
                    switch ((st >> 5) & 3) {
                        case 1: c.mem[a] = uint8_t(d); break;
                        case 2: c.mem[a] = uint8_t(d >> 8); c.mem[a + 1] = uint8_t(d); break;
                        case 0: c.mem[a] = uint8_t(d >> 24); c.mem[a + 1] = uint8_t(d >> 16);
                                c.mem[a + 2] = uint8_t(d >> 8); c.mem[a + 3] = uint8_t(d); break;
                        default: break;
                    }
                }
            }
            step();                             // the RTE
        } else if (hole && c.getPC0() == kCode && i < 4) {
            step();                             // a restarted instruction
        } else {
            break;
        }
    }
    if (c.getPC0() - kTrap < 16 || c.getPC0() == kHandler) f.trapped = true;
    for (int i = 0; i < 8; i++) f.d[i] = c.getD(i);
    for (int i = 0; i < 7; i++) f.a[i] = c.getA(i);
    f.usp = c.getUSP(); f.pc = c.getPC0(); f.ccr = uint16_t(c.getSR() & 0xff);
    for (int i = 0; i < 8; i++) c.getFP(i, f.fp[i]);
    f.fpcr = c.getFPCR(); f.fpsr = c.getFPSR(); f.fpiar = c.getFPIAR();
    f.trips = c.trips;
    f.data.assign(c.mem.begin() + kData, c.mem.begin() + kData + kDataSize);
    if (in.super) {
        // What lies below the final stack pointer is spent frames.
        const uint32_t sp = c.getSR() & 0x2000 ? c.getSP() : c.getISP();
        for (uint32_t a = kUserStack - 0x400; a < sp && a < kData + kDataSize; a++) f.data[a - kData] = 0;
    }
    if (cyclesOut) *cyclesOut = c.cycles;
    return f;
}

std::string differ(const Final& got, const Final& want)
{
    char b[160];
    if (got.wedged) return "execute() threw";
    if (got.trapped) return "ended in an exception handler";
    if (got.pc != want.pc) { std::snprintf(b, sizeof b, "PC0 %08X want %08X", got.pc, want.pc); return b; }
    if (got.usp != want.usp) { std::snprintf(b, sizeof b, "USP %08X want %08X", got.usp, want.usp); return b; }
    for (int i = 0; i < 7; i++) if (got.a[i] != want.a[i]) {
        std::snprintf(b, sizeof b, "A%d %08X want %08X", i, got.a[i], want.a[i]); return b; }
    for (int i = 0; i < 8; i++) if (got.d[i] != want.d[i]) {
        std::snprintf(b, sizeof b, "D%d %08X want %08X", i, got.d[i], want.d[i]); return b; }
    for (int i = 0; i < 8; i++) for (int j = 0; j < 3; j++) if (got.fp[i][j] != want.fp[i][j]) {
        std::snprintf(b, sizeof b, "FP%d.%d %08X want %08X", i, j, got.fp[i][j], want.fp[i][j]); return b; }
    if (got.fpcr != want.fpcr) { std::snprintf(b, sizeof b, "FPCR %08X want %08X", got.fpcr, want.fpcr); return b; }
    if (got.fpsr != want.fpsr) { std::snprintf(b, sizeof b, "FPSR %08X want %08X", got.fpsr, want.fpsr); return b; }
    if (got.fpiar != want.fpiar) { std::snprintf(b, sizeof b, "FPIAR %08X want %08X", got.fpiar, want.fpiar); return b; }
    if (got.ccr != want.ccr) { std::snprintf(b, sizeof b, "CCR %02X want %02X", got.ccr, want.ccr); return b; }
    for (uint32_t i = 0; i < kDataSize; i++) if (got.data[i] != want.data[i]) {
        std::snprintf(b, sizeof b, "RAM[%06X] %02X want %02X", kData + i, got.data[i], want.data[i]);
        return b; }
    return "";
}

// ---- the instruction list ---------------------------------------------------

std::vector<Inst> list;
uint32_t gen = 0x2026b115;

void add(const char* name, std::vector<uint16_t> words, bool a0IsCode = false, bool only030 = false)
{
    Inst in; in.words = std::move(words); in.a0IsCode = a0IsCode; in.only030 = only030;
    char b[96]; std::string s = name;
    for (uint16_t w : in.words) { std::snprintf(b, sizeof b, " %04X", w); s += b; }
    in.name = s;
    list.push_back(std::move(in));
}

// One memory EA: mode 2 (An), 3 (An)+, 4 -(An), 5 d16(An), 6 d8(An,D7.W),
// 7 ([A3]) and 8 ([A3],od.W) through the pointer cell A3 addresses.
struct Ea { uint16_t bits; std::vector<uint16_t> ext; };
Ea ea(int mode, int reg)
{
    switch (mode) {
        case 5: return { uint16_t(0x28 | reg), { uint16_t(int16_t(int(rnd(gen) % 33) - 16) * 2) } };
        case 6: return { uint16_t(0x30 | reg), { uint16_t(0x7000 | (rnd(gen) & 0x3e)) } };
        case 7: return { uint16_t(0x30 | 3), { 0x0151 } };
        case 8: return { uint16_t(0x30 | 3), { 0x0152, uint16_t((rnd(gen) & 0x1e) + 2) } };
        default: return { uint16_t(mode << 3 | reg), {} };
    }
}
void addEa(const char* name, uint16_t opcode, std::vector<uint16_t> own, const Ea& e)
{
    std::vector<uint16_t> w { uint16_t(opcode | e.bits) };
    w.insert(w.end(), own.begin(), own.end());
    w.insert(w.end(), e.ext.begin(), e.ext.end());
    add(name, w);
}

void build()
{
    const int modes[] = { 2, 3, 4, 5, 6, 7, 8 };
    auto imm = [](int size) {
        std::vector<uint16_t> v { uint16_t(size == 0 ? rnd(gen) & 0xff : rnd(gen)) };
        if (size == 2) v.push_back(uint16_t(rnd(gen)));
        return v;
    };
    for (int size = 0; size < 3; size++) {
        const uint16_t sz = uint16_t(size << 6);
        const uint16_t moveSize[] = { 0x1000, 0x3000, 0x2000 };
        for (int sm : modes) for (int dm : modes) {     // MOVE <ea>,<ea>
            if (sm >= 7 && dm >= 7) continue;
            const Ea src = ea(sm, 1), dst = ea(dm, sm == 3 && dm == 3 ? 1 : 2);
            const uint16_t dstBits = uint16_t((dst.bits & 7) << 9 | (dst.bits >> 3) << 6);
            std::vector<uint16_t> w { uint16_t(moveSize[size] | dstBits | src.bits) };
            w.insert(w.end(), src.ext.begin(), src.ext.end());
            w.insert(w.end(), dst.ext.begin(), dst.ext.end());
            add("MOVE", w);
        }
        for (int m : modes) {
            addEa("ADD Dn,ea", uint16_t(0xd300 | sz), {}, ea(m, 1));
            addEa("SUB Dn,ea", uint16_t(0x9300 | sz), {}, ea(m, 1));
            addEa("AND Dn,ea", uint16_t(0xc300 | sz), {}, ea(m, 1));
            addEa("OR Dn,ea", uint16_t(0x8300 | sz), {}, ea(m, 1));
            addEa("EOR Dn,ea", uint16_t(0xb300 | sz), {}, ea(m, 1));
            addEa("ADD ea,Dn", uint16_t(0xd200 | sz), {}, ea(m, 1));
            addEa("CMP ea,Dn", uint16_t(0xb200 | sz), {}, ea(m, 1));
            addEa("ADDQ", uint16_t(0x5600 | sz), {}, ea(m, 1));
            addEa("SUBQ", uint16_t(0x5700 | sz), {}, ea(m, 1));
            addEa("ADDI", uint16_t(0x0600 | sz), imm(size), ea(m, 1));
            addEa("EORI", uint16_t(0x0a00 | sz), imm(size), ea(m, 1));
            addEa("CMPI", uint16_t(0x0c00 | sz), imm(size), ea(m, 1));
            addEa("NEG", uint16_t(0x4400 | sz), {}, ea(m, 1));
            addEa("NEGX", uint16_t(0x4000 | sz), {}, ea(m, 1));
            addEa("NOT", uint16_t(0x4600 | sz), {}, ea(m, 1));
            addEa("CLR", uint16_t(0x4200 | sz), {}, ea(m, 1));
            addEa("TST", uint16_t(0x4a00 | sz), {}, ea(m, 1));
            if (m != 3 && m != 4)
                addEa("CAS", uint16_t(0x08c0 | (size + 1) << 9), { 0x0081 }, ea(m, 1));
            else
                addEa("CAS", uint16_t(0x08c0 | (size + 1) << 9), { 0x0081 }, ea(m, 1));
        }
        add("ADDX -(A1),-(A2)", { uint16_t(0xd509 | sz) });
        add("SUBX -(A1),-(A2)", { uint16_t(0x9509 | sz) });
        add("CMPM (A1)+,(A2)+", { uint16_t(0xb509 | sz) });
    }
    for (int m : modes) {
        for (int t = 0; t < 4; t++) {
            addEa("shift right", uint16_t(0xe0c0 | t << 9), {}, ea(m, 1));
            addEa("shift left", uint16_t(0xe1c0 | t << 9), {}, ea(m, 1));
            addEa("bit Dn", uint16_t(0x0300 | t << 6), {}, ea(m, 1));
            addEa("bit #", uint16_t(0x0800 | t << 6), { uint16_t(rnd(gen) & 7) }, ea(m, 1));
        }
        addEa("Scc", uint16_t(0x50c0 | (rnd(gen) & 0xf) << 8), {}, ea(m, 1));
        addEa("TAS", 0x4ac0, {}, ea(m, 1));
        addEa("NBCD", 0x4800, {}, ea(m, 1));
        addEa("MOVE ea,CCR", 0x44c0, {}, ea(m, 1));
        addEa("MOVE CCR,ea", 0x42c0, {}, ea(m, 1));
        addEa("MULU", 0xc2c0, {}, ea(m, 1));
        addEa("MULS", 0xc3c0, {}, ea(m, 1));
        addEa("DIVU", 0x82c0, {}, ea(m, 1));
        addEa("DIVS", 0x83c0, {}, ea(m, 1));
        addEa("MULU.L", 0x4c00, { 0x1000 }, ea(m, 2));
        addEa("MULS.L 64", 0x4c00, { 0x1c02 }, ea(m, 4));
        addEa("DIVU.L", 0x4c40, { 0x1001 }, ea(m, 2));
        addEa("CMP2.W", 0x02c0, { 0x1000 }, ea(m == 3 || m == 4 ? 2 : m, 1));
        addEa("ADDA.W", 0xd0c0 | 5 << 9, {}, ea(m, 1));
        addEa("CMPA.L", 0xb1c0 | 5 << 9, {}, ea(m, 1));
        addEa("MOVEA.L", 0x2040 | 5 << 9, {}, ea(m, 1));
        if (m != 3 && m != 4) {
            for (int t = 0; t < 8; t++)
                addEa("bit field", uint16_t(0xe8c0 | t << 8),
                      { uint16_t(0x1000 | (rnd(gen) & 0x1f) << 6 | (rnd(gen) & 0x1f)) }, ea(m, 1));
            addEa("PEA", 0x4840, {}, ea(m, 1));
            addEa("LEA", 0x45c0, {}, ea(m, 1));
        }
        // A load list leaves out the registers the EA is made of (A1, A3,
        // D7) and A7: a MOVEM that overwrites its own base before a later
        // read faults is a question for a real processor, not for this gate.
        if (m != 3) {
            addEa("MOVEM.L regs,ea", 0x48c0, { uint16_t(m == 4 ? 0x3f3c : 0x3cfc) }, ea(m, 1));
            addEa("MOVEM.W regs,ea", 0x4880, { uint16_t(rnd(gen) | 1) }, ea(m, 1));
        }
        if (m != 4) {
            addEa("MOVEM.L ea,regs", 0x4cc0, { 0x757c }, ea(m, 1));
            addEa("MOVEM.W ea,regs", 0x4c80, { uint16_t((rnd(gen) & 0x757f) | 1) }, ea(m, 1));
        }
    }
    // The coprocessor: operands of every format, register lists, control registers.
    for (int m : modes) {
        for (int f = 0; f < 7; f++) {
            if (f == 3) continue;                   // packed: its own gates
            addEa("FMOVE ea,FP1", 0xf200, { uint16_t(0x4080 | f << 10) }, ea(m, 1));
            addEa("FADD ea,FP1", 0xf200, { uint16_t(0x40a2 | f << 10) }, ea(m, 1));
            addEa("FMOVE FP1,ea", 0xf200, { uint16_t(0x6080 | f << 10) }, ea(m, 1));
        }
        addEa("FTST.X ea", 0xf200, { 0x483a }, ea(m, 1));
        if (m != 3) addEa("FMOVEM FP,ea", 0xf200, { uint16_t(m == 4 ? 0xe03c : 0xf03c) }, ea(m, 1));
        if (m != 4) addEa("FMOVEM ea,FP", 0xf200, { 0xd03c }, ea(m, 1));
        addEa("FMOVE.L ea,FPCR", 0xf200, { 0x9000 }, ea(m, 1));
        addEa("FMOVE.L FPSR,ea", 0xf200, { 0xa800 }, ea(m, 1));
        addEa("FMOVEM.L ea,FPCR/FPSR/FPIAR", 0xf200, { 0x9c00 }, ea(m, 1));
        addEa("FMOVEM.L FPCR/FPSR/FPIAR,ea", 0xf200, { 0xbc00 }, ea(m, 1));
    }
    // Privileged instructions, in supervisor mode.
    const size_t firstSuper = list.size();
    for (int m : modes) {
        for (int size = 0; size < 3; size++) {
            addEa("MOVES Rn,ea", uint16_t(0x0e00 | size << 6), { 0x1800 }, ea(m, 1));
            addEa("MOVES ea,Rn", uint16_t(0x0e00 | size << 6), { 0x1000 }, ea(m, 1));
        }
        addEa("MOVE SR,ea", 0x40c0, {}, ea(m, 1));
        if (m != 3 && m != 4) addEa("FSAVE ea", 0xf300, {}, ea(m, 1));
    }
    add("FSAVE -(A1)", { 0xf321 });
    for (const uint16_t tail : { uint16_t(0x4200), uint16_t(0x4e00), uint16_t(0x4a00),
                                 uint16_t(0x0a00), uint16_t(0x6200) })
        add("PMOVE reg,(A1)", { 0xf011, tail }, false, true);
    add("PMOVE (A1),TT0", { 0xf011, 0x0800 }, false, true); list.back().cell = 0;
    add("FRESTORE (A1)", { 0xf351 }); list.back().cell = 0;
    add("FRESTORE (A1)+", { 0xf359 }); list.back().cell = 0;
    add("MOVE (A1),SR, staying supervisor", { 0x46d1 }); list.back().cell = 0x2715;
    // RTE reading its own frame: the fault frame must not land on it.
    add("RTE format 0, to supervisor", { 0x4e73 });
    list.back().stack = { 0x2711, uint16_t(kTarget >> 16), uint16_t(kTarget), 0x0000 };
    add("RTE format 0, to user mode", { 0x4e73 });
    list.back().stack = { 0x0011, uint16_t(kTarget >> 16), uint16_t(kTarget), 0x0000 };
    add("RTE format 2", { 0x4e73 });
    list.back().stack = { 0x2700, uint16_t(kTarget >> 16), uint16_t(kTarget), 0x2018, 0x0000, 0x1000 };
    add("MOVE (A1),SR, to user mode", { 0x46d1 }); list.back().cell = 0x0012;
    for (size_t i = firstSuper; i < list.size(); i++) list[i].super = true;
    add("ABCD -(A1),-(A2)", { 0xc509 });
    add("SBCD -(A1),-(A2)", { 0x8509 });
    add("PACK -(A1),-(A2)", { 0x8549, uint16_t(rnd(gen)) });
    add("UNPK -(A1),-(A2)", { 0x8589, uint16_t(rnd(gen)) });
    add("MOVEP.W D1,d16(A1)", { 0x0389, 0x0004 });
    add("MOVEP.L D1,d16(A1)", { 0x03c9, 0x0006 });
    add("MOVEP.W d16(A1),D1", { 0x0309, 0x0004 });
    add("MOVEP.L d16(A1),D1", { 0x0349, 0x0006 });
    add("CAS2.W", { 0x0cfc, 0x9081, 0xa0c2 });
    add("CAS2.L", { 0x0efc, 0x9081, 0xa0c2 });
    // The stack.
    add("MOVE.L D1,-(A7)", { 0x2f01 });
    add("MOVE.W D1,-(A7)", { 0x3f01 });
    add("MOVE.B D1,-(A7)", { 0x1f01 });
    add("MOVE.L (A7)+,D1", { 0x221f });
    add("MOVE.L (A7)+,(A1)", { 0x229f });
    add("MOVE.L (A1)+,-(A7)", { 0x2f19 });
    add("MOVEM.L regs,-(A7)", { 0x48e7, 0xfffe });
    add("MOVEM.L (A7)+,regs", { 0x4cdf, 0x3cfc });
    add("PEA (A7)", { 0x4857 });
    add("LINK A6,#-8", { 0x4e56, 0xfff8 });
    add("LINK.L A5,#-16", { 0x480d, 0xffff, 0xfff0 });
    add("UNLK A6", { 0x4e5e });
    add("JSR (A0)", { 0x4e90 }, true);
    add("JSR d16(A0)", { 0x4ea8, 0x0004 }, true);
    add("BSR.B", { 0x6140 });
    add("BSR.W", { 0x6100, 0x0040 });
    add("BSR.L", { 0x61ff, 0x0000, 0x0040 });
    add("RTS", { 0x4e75 });
    add("RTR", { 0x4e77 });
    add("RTD #8", { 0x4e74, 0x0008 });
}

} // namespace

int main(int argc, char** argv)
{
    bool verbose = false; long only = -1;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else only = std::strtol(argv[i], nullptr, 0);
    }
    build();

    struct Row { moira::CPUModel model; const char* name; bool is040; };
    static const Row rows[] = {
        { moira::CPUModel::M68030, "68030", false }, { moira::CPUModel::M68040, "68040", true },
    };
    int failures = 0;
    for (const Row& r : rows) {
        unsigned instructions = 0, faults = 0, skipped = 0, bad = 0;
        for (size_t n = 0; n < list.size(); n++) {
            if (only >= 0 && long(n) != only) continue;
            const Inst& in = list[n];
            if (in.only030 && r.is040) continue;
            std::vector<Cycle> cycles;
            const Final want = run(r.model, in, uint32_t(n), nullptr, &cycles);
            if (want.trapped || want.wedged) { skipped++; continue; }   // a divide by zero, a CHK
            instructions++;
            std::vector<Cycle> seen;
            for (const Cycle& c : cycles) {
                // 68040 stores that are no page-fault case: CAS and CAS2 probe
                // the write when they read, and a bit-field store restarts
                // with a valid WB3 besides (TODO.md).
                if (c.write && r.is040 &&
                    (in.name.rfind("CAS", 0) == 0 || in.name.rfind("bit field", 0) == 0)) continue;
                bool dup = false;
                for (const Cycle& s : seen) dup = dup || s == c;
                if (dup) continue;
                seen.push_back(c);
                const Final got = run(r.model, in, uint32_t(n), &c, nullptr);
                faults++;
                std::string why = got.trips == 1 ? differ(got, want) : "the cycle was not reached";
                if (verbose || !why.empty())
                    std::printf("  %s %s #%zu %s, /BERR on the %s of %06X: %s\n",
                                why.empty() ? "ok  " : "FAIL", r.name, n, in.name.c_str(),
                                c.write ? "write" : "read", c.addr, why.empty() ? "same" : why.c_str());
                if (!why.empty()) { bad++; failures++; }
            }
        }
        std::printf("[fault_transparency_test] %s: %u instructions, %u faulted cycles, "
                    "%u differ (%u instructions trap by themselves)\n",
                    r.name, instructions, faults, bad, skipped);
    }
    if (failures) { std::printf("[fault_transparency_test] FAIL: %d\n", failures); return 1; }
    std::printf("[fault_transparency_test] OK\n");
    return 0;
}
