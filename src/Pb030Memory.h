// POM68K — Jaws/Misc GLU board, PowerBook 140/170 (GPLv3).
// Map/pins: MAME macpwrbk030.cpp; milestone contract: docs/PB030_BRINGUP.md.
#pragma once
#include "CoreConfig.h"
#include "Pb030Pmu.h"
#include "Via6522.h"
#include "PseudoVia.h"
#include "Rtc.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Swim1.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include "jit/JitGuard.h"

class Pb030Cpu;
class Pb030Memory {
public:
    static constexpr uint32_t kRomSize=0x100000,kVramSize=0x8000;
    static constexpr int64_t kCpuHz=15667200,kViaHz=1566720;
    explicit Pb030Memory(const pom68k::CoreConfig& config,uint32_t ram=0x800000,
                         int64_t cpuHz=kCpuHz);
    bool loadRom(const std::vector<uint8_t>& rom);
    void reset();
    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a);
    void write8(uint32_t a,uint8_t v);
    void write16(uint32_t a,uint16_t v);
    uint8_t peek8(uint32_t a) const;
    void tick(int cycles);
    int64_t cpuHz() const { return cpuHz_; }
    void setCpu(Pb030Cpu* cpu) { cpu_=cpu; }
    bool cpuHeld() const { return pmu_.cpuHeld(); }
    bool overlay() const { return overlay_; }
    bool consumeReset() { bool v=resetPending_;resetPending_=false;return v; }
    int iplLevel() const;
    void updateIrq();
    void mapChanged();
    const uint8_t* codeSpan(uint32_t a,uint32_t& len) const;
    uint8_t* dataSpan(uint32_t a,uint32_t& len,bool write);
    void setJitGuard(jit::CodeGuard* g) { guard_=g; }
    uint32_t ramBytes() const { return uint32_t(ram_.size()); }
    bool floppyStreaming() const { return drive_.motorOn(); }
    Pb030Pmu& pmu() { return pmu_; }
    Via6522& via1() { return via_; }
    PseudoVia& pseudoVia() { return pvia_; }
    Rtc& rtc() { return rtc_; }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk(int id=0) { return disks_[id]; }
    bool attachScsi(const std::string& path,bool writeBack=false,int id=0);
    Swim1& swim() { return swim_; }
    SonyDrive& internalDrive() { return drive_; }
    Scc8530& scc() { return scc_; }
    const std::vector<uint8_t>& vram() const { return vram_; }
    void decode(std::vector<uint32_t>& frame) const;
    template<class Ar> void visit(Ar& ar) {
        ar.blob(ram_);ar.blob(vram_);
        ar(overlay_,resetPending_,viaAcc_,tickAcc_,c15Acc_,secAcc_,
           via_,pvia_,rtc_,asc_,scsi_,swim_,drive_,scc_,pmu_);
        for(auto& d:disks_) ar(d);
        if constexpr(Ar::loading) { mapChanged();updateIrq(); }
    }
private:
    std::vector<uint8_t> ram_,rom_,vram_;
    int64_t cpuHz_;
    Pb030Pmu pmu_;
    Via6522 via_;
    PseudoVia pvia_{PseudoVia::Flavour::Pb030};
    Rtc rtc_;
    AscEasc asc_;
    Ncr5380 scsi_;
    ScsiDisk disks_[7];
    Swim1 swim_;
    SonyDrive drive_;
    Scc8530 scc_;
    Pb030Cpu* cpu_=nullptr;
    jit::CodeGuard* guard_=nullptr;
    bool overlay_=true,resetPending_=false;
    int64_t viaAcc_=0,tickAcc_=0,c15Acc_=0,secAcc_=0;
    void busError() const;
    void viaSync();
    uint8_t viaAccess(uint32_t a,bool write,uint8_t v=0);
    uint8_t ioRead(uint32_t a);
    void ioWrite(uint32_t a,uint8_t v);
    uint8_t dmaRead();
    void dmaWrite(uint8_t v);
    void scsiLines();
};
