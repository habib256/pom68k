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
void PortableCpu::runCycles(moira::i64 cycles) {
    const auto end=clock+cycles;
    while(clock<end && !isHalted()) {
        if(mem_.consumeReset()) {jit_.flushAll();reset();}
        const auto chunk=std::min<moira::i64>(end-clock,256);
        if(chunk<=0) break;
        if(mem_.cpuHeld()) stall(int(chunk));
        else if(jit_.enabled()) jit_.executeUntil(clock+chunk);
        else executeUntil(clock+chunk);
        flushTicks();
    }
}
void PortableCpu::stall(int cycles) {if(cycles>0) {clock+=cycles;flushTicks();}}
void PortableCpu::flushTicks() {
    const auto delta=clock-lastPeriphClock_;
    if(delta<=0) return;
    lastPeriphClock_=clock;mem_.tick(int(delta));
}
