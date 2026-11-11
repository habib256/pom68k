#include "PortableCpu.h"
#include <algorithm>

PortableCpu::PortableCpu(PortableMemory& mem,const jit::ResolvedConfig& config)
    :MoiraCpu(mem,jit::kGuest68000,config) {
    setModel(moira::Model::M68000);
}
void PortableCpu::hardReset() {
    flushTicks();mem_.reset();lastPeriphClock_=clock;jit_.flushAll();
    // The PMU holds RESET. Fetching vectors now would drop the overlay
    // before its eventual release; the first real reset runs at that edge.
    flags=moira::State::CHECK_IRQ;
}
// The Power Manager owns /RESET, and what it does must not depend on how
// the host sliced the run: its lines are read before EVERY instruction, and
// while it holds the CPU the board advances exactly to its next event
// (PortableMemory::cyclesToNextEvent) — never in a lump that would let the
// M50753 run several instructions before the VIA sees the first. Lumps of up
// to 256 cycles counted from the host call left the PowerBook 100 on a
// different screen under instruction stepping than under frame stepping
// (CHANGELOG 2026-11-11 (thirteenth)). The engine runs one instruction per
// call for the same reason; this board's JIT is a fetch window, not blocks.
// Gate: tests/portable_slicing_etalon.cpp.
void PortableCpu::runCycles(moira::i64 cycles) {
    const auto end=clock+cycles;
    while(clock<end && !isHalted()) {
        if(mem_.consumeReset()) {jit_.flushAll();reset();}
        if(mem_.cpuHeld()) {
            stall(int(std::min<moira::i64>(end-clock,mem_.cyclesToNextEvent())));
            continue;
        }
        if(jit_.enabled()) jit_.executeUntil(clock+1);
        else execute();
        flushTicks();
    }
}
void PortableCpu::stall(int cycles) {if(cycles>0) {clock+=cycles;flushTicks();}}
void PortableCpu::flushTicks() {
    const auto delta=clock-lastPeriphClock_;
    if(delta<=0) return;
    lastPeriphClock_=clock;mem_.tick(int(delta));
}
