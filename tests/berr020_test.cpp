// A software-completed format-$B data cycle must not repeat after RTE.
// MC68020/MC68EC020 User's Manual (1992), §§6.2.2-6.2.3.
#include "Moira.h"
#include "../src/SaveState.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace {
constexpr uint32_t operand = 0x40000, code = 0x1000, handler = 0x3000;
class Cpu : public moira::Moira {
public:
    std::vector<moira::u8> mem = std::vector<moira::u8>(1 << 20);
    uint32_t fault = ~0u;
    bool writeFault = false, programFault = false;
    mutable unsigned faults = 0, operandReads = 0, operandWrites = 0;
    explicit Cpu(moira::Model model) { setModel(model); }
    void word(uint32_t a, uint16_t v) { mem[a] = uint8_t(v >> 8); mem[a + 1] = uint8_t(v); }
    void lng(uint32_t a, uint32_t v) { word(a, uint16_t(v >> 16)); word(a + 2, uint16_t(v)); }
    uint16_t word(uint32_t a) const { return uint16_t(mem[a] << 8 | mem[a + 1]); }
    uint32_t lng(uint32_t a) const { return uint32_t(word(a)) << 16 | word(a + 2); }
    void observe(uint32_t a, bool write) const {
        if (a >= operand && a < operand + 4)
            ++(write ? operandWrites : operandReads);
        if (a == fault && write == writeFault && (programFault || (readFC() & 1))) {
            ++faults; const_cast<Cpu*>(this)->extBusError();
        }
    }
private:
    moira::u8 read8(moira::u32 a) const override {
        observe(a, false); return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override {
        observe(a, false); return word(a & 0xfffff);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        observe(a, true); const_cast<Cpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        observe(a, true); const_cast<Cpu*>(this)->word(a & 0xfffff, v);
    }
};
int failures = 0;
unsigned cases = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("  FAIL %s\n", what); }
}

void run(moira::Model model, unsigned bytes, bool write, unsigned mode,
         bool completed, bool restore, bool second = false, bool user = false) {
    ++cases;
    Cpu c(model);
    c.lng(0, 0x80000); c.lng(4, code); c.lng(8, handler);
    const uint16_t size = bytes == 1 ? 0x1000 : bytes == 2 ? 0x3000 : 0x2000;
    const uint16_t op = uint16_t(size | (write ? 0x0080 + mode * 0x40 : 0x0010 + mode * 8));
    c.word(code, op); c.word(code + 2, 0x4e71); c.word(code + 4, 0x4e71);
    // BCLR #0,10(SP), clearing DF in the stacked SSW; then RTE.
    c.word(handler, 0x08af); c.word(handler + 2, 0); c.word(handler + 4, 10);
    c.word(handler + 6, 0x4e73);
    c.lng(operand, 0x11223344);
    c.reset(); c.setSR(user ? 0x001f : 0x271f); c.setD(0, 0xa1b2c3d4);
    c.setA(0, operand + (mode == 2 ? bytes : 0));
    const bool reverse = write && mode == 2 && bytes == 4;
    c.fault = operand + (bytes == 4 && (reverse != second) ? 2 : 0);
    c.writeFault = write;
    c.execute();
    std::printf("020 model=%u bytes=%u write=%u mode=%u completed=%u restore=%u second=%u user=%u\n",
                unsigned(model), bytes, write, mode, completed, restore, second, user);
    const auto sp = c.getSP();
    check(c.getPC0() == handler && !c.isHalted(), "data fault enters handler");
    check(c.word(sp + 6) == 0xb008 && c.lng(sp + 2) == code,
          "format $B names the faulted instruction");
    check(c.lng(sp + 16) == c.fault, "frame names the failing bus access");
    check(!(c.word(sp + 10) & 0x0200), "020 SSW needs no oracle-private bit-9 marker");
    const uint16_t sizeCode = bytes == 1 ? 0x10 : bytes == 2 || second ? 0x20 : 0;
    check((c.word(sp + 10) & 0x0077) == (sizeCode | (write ? 0 : 0x40) | (user ? 1 : 5)),
          "SSW preserves transfer size, direction and address space");
    if (write) {
        const uint32_t mask = second || bytes == 2 ? 0xffff : bytes == 1 ? 0xff : ~0u;
        const uint32_t output = second && reverse ? 0xa1b2 : 0xa1b2c3d4;
        check((c.lng(sp + 24) & mask) == (output & mask),
              "output buffer contains the pending transfer at the faulted size");
    }
    const uint32_t input = 0x89abcdef;
    c.lng(sp + 44, input);
    if (completed) c.execute(); // BCLR; otherwise keep DF set and skip to RTE
    else {
        c.setPC(handler + 6); c.setPC0(handler + 6);
        c.setIRD(0x4e73); c.setIRC(0x4e71);
    }
    c.execute(); // RTE
    check(c.getPC0() == code, "RTE returns to the faulted instruction");
    std::unique_ptr<Cpu> loaded;
    if (restore) {
        std::vector<moira::u8> saved;
        sav::Writer save(saved); c.pomVisitState(save);
        loaded = std::make_unique<Cpu>(model);
        loaded->mem = c.mem;
        loaded->fault = c.fault; loaded->writeFault = c.writeFault;
        sav::Reader load(saved.data(), saved.size()); loaded->pomVisitState(load);
        check(load.ok() && load.remaining() == 0, "retry latch survives save/load into a fresh CPU");
    }
    Cpu& retry = loaded ? *loaded : c;
    const unsigned trips = retry.faults;
    const unsigned transfers = write ? retry.operandWrites : retry.operandReads;
    retry.execute(); // retry
    if (completed) {
        check(retry.getPC0() == code + 2 && !retry.isHalted(), "software completion permits progress");
        check(retry.faults == trips, "software-completed cycle is never repeated");
        if (!second)
            check((write ? retry.operandWrites : retry.operandReads) == transfers,
                  "no data bus callback for the completed operand");
        // Earlier successful beats may still replay in this instruction-restart
        // model. This gate pins the faulted cycle, not a full continuation.
        const uint32_t mask = bytes == 1 ? 0xff : bytes == 2 ? 0xffff : ~0u;
        if (!write)
            check(retry.getD(0) == (second ? 0x11220000 | (input & 0xffff) :
                               (0xa1b2c3d4 & ~mask) | (input & mask)),
                  "read consumes the right-justified data input buffer");
        check(retry.getA(0) == operand + (mode == 1 ? bytes : 0),
              "address update happens once across the fault/RTE");
        check(retry.getCCR() == ((!write && second) ? 0x10 : 0x18),
              "completed MOVE has the architectural XNZVC result");
    } else {
        check(retry.getPC0() == handler && retry.faults == trips + 1,
              "DF still set reruns and faults the same operand");
    }
}
void aliasAndReset(moira::Model model, bool resetPending) {
    ++cases;
    Cpu c(model);
    c.lng(0, 0x80000); c.lng(4, code); c.lng(8, handler);
    c.word(code, 0x3010); c.word(code + 2, 0x4e71); c.word(code + 4, 0x4e71);
    c.word(handler, 0x08af); c.word(handler + 2, 0); c.word(handler + 4, 10);
    c.word(handler + 6, 0x4e73);
    c.reset(); c.setSR(0x2700); c.setA(0, code + 2);
    c.fault = code + 2;
    c.execute();
    const auto sp = c.getSP();
    check(c.getPC0() == handler, "alias: data-space fault reached handler");
    c.lng(sp + 44, 0x1234);
    c.execute(); c.execute();
    check(c.getPC0() == code && c.faults == 1,
          "alias: RTE program prefetch does not consume the pending data result");
    c.setD(0, 0);
    if (resetPending) {
        c.fault = ~0u; c.reset(); c.setSR(0x2700); c.setA(0, code + 2);
    }
    c.execute();
    check(c.getPC0() == code + 2 && c.getD(0) == (resetPending ? 0x4e71u : 0x1234u),
          "alias: pending result is consumed once, or cancelled by reset");
    c.fault = ~0u; c.setPC(code); c.setPC0(code);
    c.setIRD(0x3010); c.setIRC(0x4e71);
    c.execute();
    check(c.getD(0) == 0x4e71, "alias: a later execution reads the real operand");
}

void cpuSpace(moira::Model model, bool write) {
    ++cases;
    Cpu c(model);
    c.lng(0, 0x80000); c.lng(4, code); c.lng(8, handler);
    c.word(code, 0x0e50); c.word(code + 2, write ? 0x0800 : 0);
    c.word(code + 4, 0x4e71); c.word(code + 6, 0x4e71);
    c.word(handler, 0x08af); c.word(handler + 2, 0); c.word(handler + 4, 10);
    c.word(handler + 6, 0x4e73);
    c.reset(); c.setSR(0x2700); c.setA(0, operand);
    c.setD(0, 0xdeadbeef); c.setSFC(7); c.setDFC(7);
    c.fault = operand; c.writeFault = write;
    c.execute();
    const auto sp = c.getSP();
    check(c.getPC0() == handler && (c.word(sp + 10) & 7) == 7,
          "MOVES: CPU-space function code survives the bus fault");
    c.lng(sp + 44, 0x1234);
    c.execute(); c.execute(); c.execute();
    check(c.getPC0() == code + 4 && c.faults == 1,
          "MOVES: software-completed FC=7 cycle is not repeated");
    if (!write) check(c.getD(0) == 0xdead1234, "MOVES: FC=7 read uses the saved input");
}
void lockedFault(moira::Model model, unsigned kind) {
    ++cases;
    Cpu c(model);
    c.lng(0, 0x80000); c.lng(4, code); c.lng(8, handler);
    c.word(code, kind == 0 ? 0x4ad0 : kind == 1 ? 0x0cd0 : 0x0cfc);
    c.word(code + 2, kind == 2 ? 0x8040 : 0x0040);
    c.word(code + 4, kind == 2 ? 0x90c2 : 0x4e71);
    c.word(code + 6, 0x4e71); c.word(handler, 0x4e73);
    c.reset(); c.setSR(0x2700); c.setA(0, operand); c.setA(1, operand + 4);
    c.fault = operand;
    c.execute();
    const auto sp = c.getSP();
    check(c.getPC0() == handler && (c.word(sp + 10) & 0x0180) == 0x0180,
          "TAS/CAS/CAS2: frame identifies a locked RMW data fault");
    c.execute(); c.execute(); // DF still set: RTE then restart
    check(c.getPC0() == handler && c.faults == 2,
          "locked fault with DF set reruns the operation");
}
void lockedPrefetch(moira::Model model) {
    ++cases;
    Cpu c(model);
    c.lng(0, 0x80000); c.lng(4, code); c.lng(8, handler);
    c.word(code, 0x4ad0); c.word(code + 2, 0x4e71); c.word(code + 4, 0x4e71);
    c.word(handler, 0x4e71);
    c.reset(); c.setSR(0x2700); c.setA(0, operand);
    c.fault = code + 4; c.programFault = true;
    c.execute();
    const auto ssw = c.word(c.getSP() + 10);
    check(c.getPC0() == handler && !(ssw & 0x0180) && (ssw & 7) == 6,
          "TAS prefetch fault is an instruction cycle, not locked data");
}
void redirectedPc(moira::Model model) {
    ++cases;
    Cpu c(model);
    c.lng(0, 0x80000); c.lng(4, code); c.lng(8, handler);
    c.word(code, 0x3010); c.word(code + 2, 0x3010); c.word(code + 4, 0x4e71);
    c.word(handler, 0x08af); c.word(handler + 2, 0); c.word(handler + 4, 10);
    c.word(handler + 6, 0x4e73);
    c.reset(); c.setSR(0x2700); c.setA(0, operand); c.fault = operand;
    c.execute();
    const auto sp = c.getSP();
    c.lng(sp + 2, code + 2); c.lng(sp + 44, 0x1234);
    c.execute(); c.execute(); c.execute();
    check(c.getPC0() == handler && c.faults == 2,
          "redirected PC: identical next opcode must issue a new data access");
}

void nestedFaults(moira::Model model) {
    ++cases;
    Cpu c(model);
    c.lng(0, 0x80000); c.lng(4, code); c.lng(8, handler);
    c.word(code, 0x3010); c.word(code + 2, 0x4e71); c.word(code + 4, 0x4e71);
    c.word(handler, 0x3211); // MOVE.W (A1),D1 faults in the outer handler
    for (uint32_t at : {handler + 2, handler + 0x100}) {
        c.word(at, 0x08af); c.word(at + 2, 0); c.word(at + 4, 10); c.word(at + 6, 0x4e73);
    }
    c.reset(); c.setSR(0x2700); c.setA(0, operand); c.setA(1, operand + 8);
    c.fault = operand; c.execute();
    c.lng(c.getSP() + 44, 0x1111);
    c.lng(8, handler + 0x100); c.fault = operand + 8;
    c.execute(); // nested data fault
    check(c.getPC0() == handler + 0x100 && c.faults == 2, "nested handler entered");
    c.lng(c.getSP() + 44, 0x2222);
    c.execute(); c.execute(); c.execute(); // inner BCLR/RTE, retry outer MOVE
    check(c.getD(1) == 0x2222 && c.getPC0() == handler + 2,
          "nested retry uses its own frame input");
    c.fault = operand;
    c.execute(); c.execute(); c.execute(); // outer BCLR/RTE, retry original MOVE
    check(c.getD(0) == 0x1111 && c.getPC0() == code + 2 && c.faults == 2,
          "outer retry survives a nested frame and has no extra device access");
}
} // namespace

int main() {
    for (auto model : {moira::Model::M68EC020, moira::Model::M68020})
        for (unsigned bytes : {1u, 2u, 4u})
            for (bool write : {false, true})
                for (unsigned mode = 0; mode < 3; ++mode)
                    for (bool completed : {false, true})
                        for (bool restore : {false, true})
                            for (bool second : {false, true}) {
                                if (second && bytes != 4) continue;
                                for (bool user : {false, true})
                                    run(model, bytes, write, mode, completed, restore, second, user);
                            }
    for (auto model : {moira::Model::M68EC020, moira::Model::M68020}) {
        aliasAndReset(model, false); aliasAndReset(model, true);
        cpuSpace(model, false); cpuSpace(model, true);
        for (unsigned kind = 0; kind < 3; ++kind) lockedFault(model, kind);
        lockedPrefetch(model);
        redirectedPc(model); nestedFaults(model);
    }
    std::printf("berr020_test: %u executed sequences, %d failure(s)\n", cases, failures);
    return failures ? 1 : 0;
}
