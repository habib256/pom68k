// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── DebugCpuTarget: one CPU wrapper + memory map, seen by the debugger ──
// The adapter between a DebugSession and a machine. It reads registers
// through Moira's const getters, edits Moira's own breakpoint list, and
// inspects memory under one rule: a byte is read only if the map hands out
// a host pointer for it — `dataSpan(phys, len, false)`, the JIT data-TLB
// contract that already refuses every window whose read has a side effect
// (VIA IFR clears, SCC RR0 latches, IWM state lines, a boot overlay that a
// ROM-window read would drop). Everything else is reported NotMemory.
//
// Logical addresses are translated without touching MMU state:
//   - 68000/68010/EC020: 24 address lines, identity otherwise;
//   - 68020 (no 68851 on these boards): identity;
//   - 68030: TT0/TT1, then the PMMU tables (Mmu030Peek.h);
//   - 68040/LC040: DTT0/DTT1, then the tables (Mmu040Peek.h).
// Descriptors themselves are fetched by the same span rule.
//
// Known limit: with the optional architectural 68040 data cache
// (POM68K_040_DCACHE=1) a dirty line is newer than RAM; the view shows RAM.
//
// Gate: tests/debug_inspection_test.cpp.

#pragma once
#include "DebugSession.h"
#include "Mmu030Peek.h"
#include "Mmu040Peek.h"
#include "MoiraDebugSeam.h"

#include <cstdio>

namespace pom68k::dbg {

template <class Cpu, class Mem>
class CpuTarget final : public Target, public StopHook {
public:
    CpuTarget(Cpu& cpu, Mem& mem, Session& session)
        : cpu_(cpu), mem_(mem), session_(session) {}

    // ── StopHook (machine thread, inside execute()) ─────────────────────
    void cpuStopped(bool soft, moira::u32 pc) override {
        if (soft) stepArmed_ = false;
        session_.onCpuStop(*this, soft, pc);
    }

    // ── Target ──────────────────────────────────────────────────────────
    std::int64_t clock() const override { return cpu_.getClock(); }
    std::uint32_t pc() const override { return cpu_.getPC(); }

    void capture(Snapshot& s) const override {
        Registers& r = s.regs;
        for (int n = 0; n < 8; ++n) { r.d[n] = cpu_.getD(n); r.a[n] = cpu_.getA(n); }
        r.pc = cpu_.getPC();
        r.sr = cpu_.getSR();
        r.usp = cpu_.getUSP(); r.isp = cpu_.getISP(); r.msp = cpu_.getMSP();
        r.vbr = cpu_.getVBR(); r.sfc = cpu_.getSFC(); r.dfc = cpu_.getDFC();
        r.cacr = cpu_.getCACR();
        r.tc = cpu_.getTC(); r.tt0 = cpu_.getTT0(); r.tt1 = cpu_.getTT1();
        r.crp = cpu_.getCRP(); r.srp = cpu_.getSRP();
        r.tc040 = cpu_.getTC040(); r.urp040 = cpu_.getURP040();
        r.srp040 = cpu_.getSRP040();
        r.dtt0 = cpu_.getDTT0(); r.dtt1 = cpu_.getDTT1();
        s.model = modelName();
        s.coreClock = cpu_.getClock();
        s.machineClock = cpu_.machineClock();
        s.supervisor = (r.sr & 0x2000) != 0;
        s.mmuEnabled = isMmu030() ? (r.tc & 0x80000000u) != 0
                     : isMmu040() ? (r.tc040 & 0x8000u) != 0 : false;
        s.requestedEngine = cpu_.engine();
        s.effectiveEngine = stopsArmed() ? 0 : s.requestedEngine;
    }

    void readMemory(Space space, std::uint32_t addr, std::uint8_t* out,
                    ByteState* state, std::size_t n) override {
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t a = addr + std::uint32_t(i);
            std::uint32_t phys = a;
            if (space == Space::Logical && !translate(a, phys)) {
                out[i] = 0;
                state[i] = ByteState::Untranslated;
                continue;
            }
            state[i] = readPhys(physMask(phys), out[i])
                ? ByteState::Ok : ByteState::NotMemory;
            if (state[i] != ByteState::Ok) out[i] = 0;
        }
    }

    DisasmLine disassemble(std::uint32_t addr) override {
        DisasmLine line;
        line.addr = addr;
        Fetch f{this, true};
        char text[128] = {};
        const int n = cpu_.disassembleWith(text, addr, &Fetch::word, &f);
        line.readable = f.readable;
        if (!f.readable || n <= 0) {
            line.length = 2;
            line.text = "??";
        } else {
            line.length = std::uint8_t(n);
            line.text = text;
        }
        return line;
    }

    bool addBreakpoint(std::uint32_t pc) override {
        cpu_.debugger.breakpoints.setAt(pc);
        return true;
    }
    void removeBreakpoint(std::uint32_t pc) override {
        cpu_.debugger.breakpoints.removeAt(pc);
    }
    void clearBreakpoints() override { cpu_.debugger.breakpoints.removeAll(); }
    std::vector<std::uint32_t> breakpoints() const override {
        std::vector<std::uint32_t> v;
        const auto& list = cpu_.debugger.breakpoints;
        for (long i = 0; i < list.elements(); ++i)
            if (auto a = list.guardAddr(i)) v.push_back(*a);
        return v;
    }
    void armStep() override {
        stepArmed_ = true;
        cpu_.debugger.stepInto();
    }
    bool stopsArmed() const override {
        return stepArmed_ || cpu_.debugger.breakpoints.elements() != 0;
    }

private:
    struct Fetch {
        CpuTarget* self;
        bool readable;
        static moira::u16 word(void* ctx, moira::u32 addr) {
            auto* f = static_cast<Fetch*>(ctx);
            std::uint8_t b[2];
            ByteState st[2];
            f->self->readMemory(Space::Logical, addr, b, st, 2);
            if (st[0] != ByteState::Ok || st[1] != ByteState::Ok)
                f->readable = false;
            return moira::u16(b[0] << 8 | b[1]);
        }
    };

    moira::Model model() const { return cpu_.getModel(); }
    bool isMmu030() const { return model() == moira::Model::M68030; }
    bool isMmu040() const {
        return model() == moira::Model::M68040 ||
               model() == moira::Model::M68LC040;
    }
    bool narrowBus() const {
        return model() == moira::Model::M68000 ||
               model() == moira::Model::M68010 ||
               model() == moira::Model::M68EC020;
    }
    std::uint32_t physMask(std::uint32_t a) const {
        return narrowBus() ? (a & 0x00FFFFFFu) : a;
    }
    const char* modelName() const {
        switch (model()) {
        case moira::Model::M68000:   return "68000";
        case moira::Model::M68010:   return "68010";
        case moira::Model::M68EC020: return "68EC020";
        case moira::Model::M68020:   return "68020";
        case moira::Model::M68EC030: return "68EC030";
        case moira::Model::M68030:   return "68030";
        case moira::Model::M68EC040: return "68EC040";
        case moira::Model::M68LC040: return "68LC040";
        case moira::Model::M68040:   return "68040";
        }
        return "68k";
    }

    bool readPhys(std::uint32_t phys, std::uint8_t& out) {
        std::uint32_t len = 0;
        const std::uint8_t* p = mem_.dataSpan(phys, len, false);
        if (!p || !len) return false;
        out = *p;
        return true;
    }
    std::uint8_t peekDescriptor(std::uint32_t phys) {
        std::uint8_t b = 0;
        return readPhys(phys, b) ? b : 0;   // I/O reads as an invalid table
    }

    bool translate(std::uint32_t laddr, std::uint32_t& phys) {
        const bool super = (cpu_.getSR() & 0x2000) != 0;
        auto peek = [this](std::uint32_t a) { return peekDescriptor(a); };
        if (isMmu030()) {
            const int fc = super ? 5 : 1;               // data space
            if (mmu030peek::transparentRead(cpu_.getTT0(), laddr, fc) ||
                mmu030peek::transparentRead(cpu_.getTT1(), laddr, fc)) {
                phys = laddr;
                return true;
            }
            return mmu030peek::translate(cpu_.getTC(), cpu_.getCRP(),
                                         cpu_.getSRP(), laddr, fc, peek,
                                         &phys);
        }
        if (isMmu040()) {
            mmu040peek::Registers r;
            r.tc = cpu_.getTC040(); r.urp = cpu_.getURP040();
            r.srp = cpu_.getSRP040();
            r.dtt0 = cpu_.getDTT0(); r.dtt1 = cpu_.getDTT1();
            return mmu040peek::translate(r, laddr, super, peek, &phys);
        }
        phys = laddr;
        return true;
    }

    Cpu& cpu_;
    Mem& mem_;
    Session& session_;
    bool stepArmed_ = false;
};

} // namespace pom68k::dbg
