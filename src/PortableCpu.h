// 15.6672 MHz 68000, independent PMU and dedicated video RAM (GPLv3).
#pragma once
#include "MoiraCpu.h"
#include "PortableMemory.h"

class PortableCpu:public pom68k::MoiraCpu<PortableCpu,PortableMemory> {
public:
    PortableCpu(PortableMemory& mem,const jit::ResolvedConfig& jitConfig);
    void hardReset();
    void runCycles(moira::i64 cycles);
    void runUntil(moira::i64 target) {if(target>clock) runCycles(target-clock);}
    void stall(int cycles);
    void flushTicks();
    void setEngine(int e) {jit_.setEnabled(e!=0);pomJitDisarm();}
    moira::i64 machineClock() const {return clock;}
    template<class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);ar(lastPeriphClock_);
        if constexpr(Ar::loading) jit_.flushAll();
    }
private:
    friend pom68k::MoiraCpu<PortableCpu,PortableMemory>;
    void catchUp() {flushTicks();}
};
