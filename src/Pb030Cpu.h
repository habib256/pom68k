// POM68K — 68030 on the Jaws PowerBook board (GPLv3).
#pragma once
#include "MoiraCpu.h"
#include "Pb030Memory.h"

class Pb030Cpu:public pom68k::MoiraCpu<Pb030Cpu,Pb030Memory> {
public:
    Pb030Cpu(Pb030Memory& mem,const jit::ResolvedConfig& jit,
             const pom68k::CoreCpuConfig& config,bool fpu=false);
    void hardReset();
    void runCycles(moira::i64 cycles);
    void stall(int cycles);
    void flushTicks();
    void setEngine(int e) { jit_.setEnabled(e!=0);pomJitDisarm(); }
    moira::i64 machineClock() const { return machineBase_+(clock-clockBase_)/boost_; }
    template<class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);ar(lastPeriphClock_,remainder_,machineBase_,clockBase_,boost_);
        if constexpr(Ar::loading) jit_.flushAll();
    }
private:
    friend pom68k::MoiraCpu<Pb030Cpu,Pb030Memory>;
    int nominalBoost_=4,boost_=4;
    bool floppyGate_=true;
    moira::i64 remainder_=0,machineBase_=0,clockBase_=0;
    void catchUp();
    void didChangeCACR(moira::u32 value) override;
    void checkCoprocessorBus(moira::u32 a) const;
    moira::u8 read8(moira::u32 a) const override;
    moira::u16 read16(moira::u32 a) const override;
    void write8(moira::u32 a,moira::u8 v) const override;
    void write16(moira::u32 a,moira::u16 v) const override;
};
