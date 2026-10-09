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
// Edits follow the same rule in the other direction: a byte is written only
// through `dataSpan(phys, len, true)` — RAM or framebuffer — and only if
// every byte of the edit qualifies. The written bytes are a physical poke
// after a read translation: page write protection does not refuse them,
// as it would not refuse a debugger's DMA. Any write drops every
// translated block (JitEngine::flushAll); the i-cache timing overlay holds
// tags only, so no stale instruction can survive it.
//
// Known limit: with the optional architectural 68040 data cache
// (POM68K_040_DCACHE=1) a dirty line is newer than RAM; the view shows RAM,
// and writes are refused because the line would later overwrite them.
//
// Gate: tests/debug_inspection_test.cpp.

#pragma once
#include "DebugSession.h"
#include "Mmu030Peek.h"
#include "Mmu040Peek.h"
#include "MoiraDebugSeam.h"

#include <cstdio>
#include <string>
#include <vector>

namespace pom68k::dbg {

template <class Cpu, class Mem>
class CpuTarget final : public Target, public StopHook {
public:
    CpuTarget(Cpu& cpu, Mem& mem, Session& session)
        : cpu_(cpu), mem_(mem), session_(session) {}
    // Breakpoints are Moira's own list and outlive the host session; the
    // watch and catch lists are this adapter's, so their guards go with
    // it — otherwise CHECK_WP/CHECK_CP would keep the CPU on the
    // interpreter with no stop anyone can see or remove. The host destroys
    // the adapter after joining its machine thread.
    ~CpuTarget() {
        cpu_.debugger.watchpoints.removeAll();
        cpu_.debugger.catchpoints.removeAll();
    }

    // ── StopHook (machine thread, inside execute()) ─────────────────────
    void cpuStopped(bool soft, moira::u32 pc) override {
        StopReason reason = soft ? StopReason::Step : StopReason::Breakpoint;
        StopDetail detail;
        if (soft) {
            stepArmed_ = false;
            if (pending_ != StopReason::None) {
                reason = pending_;
                detail = pendingDetail_;
            } else if (run_ != Run::None && !runDone()) {
                // Not there yet: one more instruction, silently.
                cpu_.debugger.stepInto();
                return;
            }
            pending_ = StopReason::None;
        }
        // Any stop ends a run-until step; a breakpoint that interrupts one
        // finds its soft stop re-armed for the next boundary.
        if (!soft && run_ != Run::None) dropSoftStop();
        run_ = Run::None;
        session_.onCpuStop(*this, reason, pc, detail);
    }

    // Mid-instruction: decide, record, and arm a soft stop; never block.
    // The first match of an instruction wins.
    void cpuAccess(moira::u32 addr, int bytes, bool write, bool program) override {
        if (program || pending_ != StopReason::None) return;
        const std::uint64_t lo = addr, hi = lo + std::uint64_t(bytes);
        const auto want = std::uint8_t(write ? Access::Write : Access::Read);
        for (const Watchpoint& w : watches_) {
            if (!(std::uint8_t(w.access) & want)) continue;
            if (lo >= std::uint64_t(w.addr) + w.length || w.addr >= hi) continue;
            pendingDetail_ = {};
            pendingDetail_.accessAddr = addr;
            pendingDetail_.accessSize = std::uint8_t(bytes);
            pendingDetail_.accessWrite = write;
            pendingDetail_.instructionPc = cpu_.getPC0();
            arm(StopReason::Watchpoint);
            return;
        }
    }
    void cpuException(moira::u8 vector) override {
        if (pending_ != StopReason::None) return;
        StopDetail d;
        d.vector = vector;
        bool framed = false;
        for (const Catch& c : catches_) {
            if (c.vector != vector) continue;
            if (!framed) { readFrame(d); framed = true; }
            if (c.trap && !trapMatches(c.trap, d.trapWord)) continue;
            pendingDetail_ = d;
            arm(StopReason::Exception);
            return;
        }
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

    bool setRegister(Reg reg, std::uint32_t v, std::string& why) override {
        const bool has010 = model() != moira::Model::M68000;
        const bool has020 = has010 && model() != moira::Model::M68010;
        const int r = int(reg);
        if (r < 8) { cpu_.setD(r, v); return true; }
        if (r < 16) { cpu_.setA(r - 8, v); return true; }
        switch (reg) {
        case Reg::PC: {
            if (v & 1) { why = "PC impair refusé"; return false; }
            std::uint8_t b[4];
            ByteState st[4];
            readMemory(Space::Logical, v, b, st, 4);
            for (ByteState x : st)
                if (x != ByteState::Ok) {
                    why = "PC refusé : aucune mémoire lisible à cette adresse";
                    return false;
                }
            cpu_.debugSetPc(v, moira::u16(b[0] << 8 | b[1]),
                            moira::u16(b[2] << 8 | b[3]));
            return true;
        }
        case Reg::SR:  cpu_.debugSetSr(moira::u16(v)); return true;
        case Reg::USP: cpu_.setUSP(v); return true;
        case Reg::ISP: cpu_.setISP(v); return true;
        case Reg::MSP: if (!has020) break; cpu_.setMSP(v); return true;
        case Reg::VBR: if (!has010) break; cpu_.setVBR(v); return true;
        case Reg::SFC: if (!has010) break; cpu_.setSFC(v); return true;
        case Reg::DFC: if (!has010) break; cpu_.setDFC(v); return true;
        default: break;
        }
        why = std::string("Registre absent du ") + modelName();
        return false;
    }

    bool writeMemory(Space space, std::uint32_t addr, const std::uint8_t* data,
                     std::size_t n, std::string& why) override {
        if (cpu_.pomCache040Armed()) {
            why = "Écriture refusée : cache de données 68040 actif";
            return false;
        }
        std::vector<std::uint8_t*> dst(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t a = addr + std::uint32_t(i);
            std::uint32_t phys = a;
            char at[48];
            if (space == Space::Logical && !translate(a, phys)) {
                std::snprintf(at, sizeof at, "$%08X non traduite", a);
                why = std::string("Écriture refusée : ") + at;
                return false;
            }
            std::uint32_t len = 0;
            dst[i] = mem_.dataSpan(physMask(phys), len, true);
            if (!dst[i] || !len) {
                std::snprintf(at, sizeof at, "$%08X n'est pas de la RAM", a);
                why = std::string("Écriture refusée : ") + at;
                return false;
            }
        }
        for (std::size_t i = 0; i < n; ++i) *dst[i] = data[i];
        if (n) cpu_.jit().flushAll();
        return true;
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
    bool addWatchpoint(const Watchpoint& w, std::string& why) override {
        Watchpoint m = w;
        m.addr = physMask(w.addr);
        if (!m.length || m.length > kMaxWatchLength ||
            std::uint64_t(m.addr) + m.length > 0x100000000ull) {
            why = "Surveillance : 1 à 16 octets";
            return false;
        }
        if (!(std::uint8_t(m.access) & 3) || std::uint8_t(m.access) > 3) {
            why = "Surveillance : lecture, écriture ou les deux";
            return false;
        }
        for (const Watchpoint& x : watches_) if (x == m) return true;
        if (watches_.size() >= kMaxWatchpoints) {
            why = "Trop de surveillances";
            return false;
        }
        watches_.push_back(m);
        rebuildWatchGuards();
        return true;
    }
    void removeWatchpoint(std::uint32_t addr) override {
        std::erase_if(watches_, [&](const Watchpoint& w) {
            return w.addr == physMask(addr);
        });
        rebuildWatchGuards();
    }
    void clearWatchpoints() override {
        watches_.clear();
        rebuildWatchGuards();
    }
    std::vector<Watchpoint> watchpoints() const override { return watches_; }

    bool addCatch(const Catch& c, std::string& why) override {
        if (c.vector < 2) {
            why = "Exception : vecteur 2 à 255";
            return false;
        }
        if (c.trap && (c.vector != 10 || (c.trap & 0xF000) != 0xA000)) {
            why = "Filtre de trap : un mot $Axxx sur le vecteur 10";
            return false;
        }
        for (const Catch& x : catches_) if (x == c) return true;
        if (catches_.size() >= kMaxCatches) {
            why = "Trop d'arrêts sur exception";
            return false;
        }
        catches_.push_back(c);
        rebuildCatchGuards();
        return true;
    }
    void removeCatch(const Catch& c) override {
        std::erase(catches_, c);
        rebuildCatchGuards();
    }
    void clearCatches() override {
        catches_.clear();
        rebuildCatchGuards();
    }
    std::vector<Catch> catches() const override { return catches_; }

    void armStep() override {
        stepArmed_ = true;
        cpu_.debugger.stepInto();
    }
    void armStepOver() override {
        std::uint16_t op = 0;
        if (!opcodeAt(cpu_.getPC(), op) || !isCall(op)) {
            armStep();
            return;
        }
        startRun(Run::Over);
        target_ = cpu_.getPC() + disassemble(cpu_.getPC()).length;
    }
    void armStepOut() override {
        startRun(Run::Out);
        returning_ = atReturn();
    }
    void cancelRun() override {
        if (run_ != Run::None) dropSoftStop();
        run_ = Run::None;
    }

    bool stopsArmed() const override {
        return stepArmed_ || run_ != Run::None ||
               cpu_.debugger.breakpoints.elements() != 0 ||
               !watches_.empty() || !catches_.empty();
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

    // Moira's guards match one address each, so a watched range is one
    // guard per byte: they only make Moira call cpuAccess(); the range,
    // the direction and the instruction-stream filter are decided there.
    void rebuildWatchGuards() {
        auto& g = cpu_.debugger.watchpoints;
        g.removeAll();
        for (const Watchpoint& w : watches_)
            for (std::uint32_t i = 0; i < w.length; ++i)
                if (!g.isSetAt(w.addr + i)) g.setAt(w.addr + i);
    }
    void rebuildCatchGuards() {
        auto& g = cpu_.debugger.catchpoints;
        g.removeAll();
        for (const Catch& c : catches_)
            if (!g.isSetAt(c.vector)) g.setAt(c.vector);
    }
    void arm(StopReason r) {
        pending_ = r;
        cpu_.debugger.stepInto();
    }

    // The handler has been entered: the frame is on the active (super-
    // visor) stack. Every 68k frame has SR at SP and the PC at SP+2,
    // except the 68000's group-0 frame (bus/address error), whose PC
    // follows the access address and instruction register (SP+10).
    void readFrame(StopDetail& d) {
        const bool group0 = model() == moira::Model::M68000 &&
                            (d.vector == 2 || d.vector == 3);
        std::uint8_t b[4];
        ByteState st[4];
        readMemory(Space::Logical, cpu_.getA(7) + (group0 ? 10 : 2), b, st, 4);
        if (st[0] != ByteState::Ok || st[3] != ByteState::Ok) return;
        d.stackedPc = std::uint32_t(b[0]) << 24 | b[1] << 16 | b[2] << 8 | b[3];
        if (d.vector != 10) return;
        readMemory(Space::Logical, d.stackedPc, b, st, 2);
        if (st[0] == ByteState::Ok && st[1] == ByteState::Ok)
            d.trapWord = std::uint16_t(b[0] << 8 | b[1]);
    }
    static bool trapMatches(std::uint16_t want, std::uint16_t got) {
        const std::uint16_t mask = (want & 0x0800) ? 0xFBFF : 0xF8FF;
        return (got & mask) == (want & mask);
    }

    // ── Run-until steps ─────────────────────────────────────────────────
    enum class Run : std::uint8_t { None, Over, Out };
    // The stack a run is judged on: 0 USP, 1 ISP, 2 MSP.
    int stackKind() const {
        const std::uint16_t sr = cpu_.getSR();
        return !(sr & 0x2000) ? 0 : (sr & 0x1000) ? 2 : 1;
    }
    // Moira has no "cancel": softstopMatches() is the call that consumes
    // an armed step-into soft stop and recomputes CHECK_BP from the
    // breakpoint list. Left armed, it would fire at the next boundary —
    // or, once the last breakpoint is removed, lie dormant and fire the
    // next time one is set — as a step nobody asked for.
    void dropSoftStop() { cpu_.debugger.softstopMatches(cpu_.getPC()); }
    void startRun(Run r) {
        run_ = r;
        stack0_ = stackKind();
        sp0_ = cpu_.getA(7);
        cpu_.debugger.stepInto();
    }
    bool opcodeAt(std::uint32_t pc, std::uint16_t& op) {
        std::uint8_t b[2];
        ByteState st[2];
        readMemory(Space::Logical, pc, b, st, 2);
        if (st[0] != ByteState::Ok || st[1] != ByteState::Ok) return false;
        op = std::uint16_t(b[0] << 8 | b[1]);
        return true;
    }
    static bool isCall(std::uint16_t op) {
        return (op & 0xFF00) == 0x6100 ||          // BSR (.S/.W/.L)
               (op & 0xFFC0) == 0x4E80 ||          // JSR
               (op & 0xFFF0) == 0x4E40 ||          // TRAP #n
               (op & 0xF000) == 0xA000 ||          // A-line: Toolbox/OS trap
               (op & 0xF000) == 0xF000;            // F-line: FPU or its emulation
    }
    static bool isReturn(std::uint16_t op) {
        return op == 0x4E75 || op == 0x4E74 ||     // RTS, RTD
               op == 0x4E77 || op == 0x4E73;       // RTR, RTE
    }
    // The next instruction is a return of the frame the run started in.
    bool atReturn() {
        std::uint16_t op = 0;
        return stackKind() == stack0_ && cpu_.getA(7) >= sp0_ &&
               opcodeAt(cpu_.getPC(), op) && isReturn(op);
    }
    // At an instruction boundary: has the run arrived?
    bool runDone() {
        if (run_ == Run::Out) {
            if (returning_) return true;       // the return just retired
            returning_ = atReturn();
            return false;
        }
        if (stackKind() != stack0_) return false;
        const std::uint32_t sp = cpu_.getA(7);
        return sp > sp0_ || (sp == sp0_ && cpu_.getPC() == target_);
    }

    Cpu& cpu_;
    Mem& mem_;
    Session& session_;
    bool stepArmed_ = false;
    Run run_ = Run::None;
    bool returning_ = false;
    int stack0_ = 0;
    std::uint32_t sp0_ = 0, target_ = 0;
    std::vector<Watchpoint> watches_;
    std::vector<Catch> catches_;
    StopReason pending_ = StopReason::None;
    StopDetail pendingDetail_;
};

} // namespace pom68k::dbg
