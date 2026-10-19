// POM68K — bounded CPU/PMU co-stepping; machine-time contract (GPLv3).
#include "Pb030Cpu.h"
#include <algorithm>

Pb030Cpu::Pb030Cpu(Pb030Memory& mem,const jit::ResolvedConfig& jit,
                   const pom68k::CoreCpuConfig& config,bool fpu)
    :MoiraCpu(mem,jit::kGuest68030,jit) {
    setModel(moira::Model::M68030);
    setFPUModel(fpu?moira::FPUModel::M68882:moira::FPUModel::NONE);
    nominalBoost_=config.cacheBoost.value_or(4);boost_=nominalBoost_;
    floppyGate_=config.floppyBoostGate.value_or(true);
    armIcacheOverlay(config.icacheMiss.value_or(4));
}

void Pb030Cpu::hardReset() {
    mem_.reset();lastPeriphClock_=clock;remainder_=0;
    pomIcache.reset();jit_.flushAll();reset();
}
void Pb030Cpu::didChangeCACR(moira::u32 v) {
    pomInvalidateIcache030(v);
    // Conservative during board bring-up: no store-inventory admission yet.
    if(v&12) jit_.flushAll();
}
void Pb030Cpu::runCycles(moira::i64 cycles) {
    const auto end=machineClock()+cycles;
    while(machineClock()<end && !isHalted()) {
        // Reset is driven by the independently powered PMU. Never reset
        // that MCU or the device graph in response to its host reset pin.
        if(mem_.consumeReset()) { jit_.flushAll();reset(); }
        const auto chunk=std::min<moira::i64>(end-machineClock(),256);
        if(mem_.cpuHeld()) stall(int(chunk));
        else {
            const auto target=clock+chunk*boost_;
            if(jit_.enabled()) jit_.executeUntil(target);else executeUntil(target);
        }
        flushTicks();
    }
}
void Pb030Cpu::stall(int cycles) {
    if(cycles>0) { clock+=moira::i64(cycles)*boost_;flushTicks(); }
}
void Pb030Cpu::catchUp() {
    // At most eight machine cycles between deliveries. VIA accesses also
    // settle the PMU before sampling a handshake line.
    if(clock-lastPeriphClock_>=8*boost_) flushTicks();
}
void Pb030Cpu::flushTicks() {
    const auto delta=clock-lastPeriphClock_;
    if(delta<=0) return;
    lastPeriphClock_=clock;remainder_+=delta;
    const int machine=int(remainder_/boost_);remainder_-=moira::i64(machine)*boost_;
    if(machine) mem_.tick(machine);
    const int want=floppyGate_ && mem_.floppyStreaming()?1:nominalBoost_;
    if(want!=boost_) {
        machineBase_+=(clock-clockBase_)/boost_;clockBase_=clock;
        remainder_=remainder_*want/boost_;boost_=want;
    }
}

// Like the LC II, this ROM tests the external FPU's response register
// using MOVES/SFC=7. An empty socket must fault in CPU space, not expose RAM.
void Pb030Cpu::checkCoprocessorBus(moira::u32 a) const {
    if((a&0xffffe000u)==0x22000u && getFPUModel()==moira::FPUModel::NONE && readFC()==7)
        const_cast<Pb030Cpu*>(this)->extBusError();
}
moira::u8 Pb030Cpu::read8(moira::u32 a) const {checkCoprocessorBus(a);return mem_.read8(a);}
moira::u16 Pb030Cpu::read16(moira::u32 a) const {checkCoprocessorBus(a);return mem_.read16(a);}
void Pb030Cpu::write8(moira::u32 a,moira::u8 v) const {checkCoprocessorBus(a);mem_.write8(a,v);}
void Pb030Cpu::write16(moira::u32 a,moira::u16 v) const {checkCoprocessorBus(a);mem_.write16(a,v);}
