// Macintosh Portable / PowerBook 100 board (GPLv3).
// MAME macprtb.cpp map and pins; notice in docs/PORTABLE_BRINGUP.md.
#pragma once
#include "CoreConfig.h"
#include "PortablePmu.h"
#include "Via6522.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Swim1.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include "DaynaPortBus.h"
#include "jit/JitGuard.h"
#include "CdAudioPump.h"

class PortableCpu;
class PortableMemory {
public:
    static constexpr int64_t kCpuHz=15667200;
    static constexpr uint32_t kRomSize=0x40000,kVramSize=0x8000;
    explicit PortableMemory(const pom68k::CoreConfig& coreConfig,uint32_t ram=8u<<20);
    bool loadRom(const std::vector<uint8_t>& bytes);
    bool attachScsi(const std::string& path,bool writeBack=false,int id=0);
    bool selectPmuFirmware(const pom68k::CoreFirmwareConfig& config);
    bool loadPram(const std::string& path);
    void savePram(const std::string& path);
    void seedRtc(uint32_t seconds) {rtcSeed_=seconds;rtcSeedPending_=true;}
    uint32_t romChecksum() const {
        return (uint32_t(rom_[0])<<24)|(uint32_t(rom_[1])<<16)|(uint32_t(rom_[2])<<8)|rom_[3];
    }
    pom68k::lle::Registry& lleRegistry() const {return *lle_;}
    int64_t cpuHz() const {return kCpuHz;}
    int screenHeight() const {return 400;}
    void reset();
    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a);
    void write8(uint32_t a,uint8_t v);
    void write16(uint32_t a,uint16_t v);
    uint8_t peek8(uint32_t a) const;
    void tick(int cycles);
    void setCpu(PortableCpu* cpu) {cpu_=cpu;}
    bool cpuHeld() const {return pmu_.cpuHeld();}
    bool consumeReset() {bool result=resetPending_;resetPending_=false;return result;}
    bool overlay() const {return overlay_;}
    int iplLevel() const;
    void updateIrq();
    void mapChanged();
    const uint8_t* codeSpan(uint32_t a,uint32_t& len) const;
    uint8_t* dataSpan(uint32_t a,uint32_t& len,bool write);
    void setJitGuard(jit::CodeGuard* guard) {guard_=guard;}
    uint32_t ramBytes() const {return uint32_t(ram_.size());}
    PortablePmu& pmu() {return pmu_;}
    Via6522& via1() {return via_;}
    Ncr5380& scsi() {return scsi_;}
    ScsiDisk& scsiDisk(int id=0) {return disks_[id];}
    Swim1& swim() {return swim_;}
    SonyDrive& internalDrive() {return drive_;}
    Scc8530& scc() {return scc_;}
    AscV8& asc() {return asc_;}
    DaynaPort& daynaPort() {return dayna_;}
    static constexpr bool externalFloppyPort() {return false;}
    bool insertDisk(const std::string& path) {return drive_.insert(path);}
    void debugDevices(std::vector<pom68k::dev::Snapshot>& out) const;
    void attachDriveSounds(FloppySoundSink* floppy,FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);for(auto& d:disks_) d.setSoundSink(hdd);
    }
    void attachCdAudioSink(CdAudioSink* cd) {for(auto& d:disks_) d.setCdAudioSink(cd);}
    bool attachCdrom(const std::string& path,int id=3) {
        if(id<0 || id>6 || !disks_[id].openCdrom(path)) return false;
        scsi_.attach(&disks_[id],id);return true;
    }
    bool attachCdromEmpty(int id) {
        if(id<1 || id>6) return false;
        disks_[id].attachCdromEmpty();scsi_.attach(&disks_[id],id);return true;
    }
    bool bayIsCdrom(int id) const {return id>=1 && id<7 && disks_[id].cdrom() && disks_[id].present();}
    bool insertBayMedia(int id,const std::string& path) {return bayIsCdrom(id) && disks_[id].openCdrom(path);}
    void ejectBayMedia(int id) {if(bayIsCdrom(id)) disks_[id].eject();}
    bool detachScsi(int id) {
        if(id<1 || id>6 || disks_[id].cdrom() || !disks_[id].present() || !scsi_.detach(id)) return false;
        disks_[id].close();return true;
    }
    void keyEvent(uint8_t key,bool down) {pmu_.keyEvent(key,down);}
    void mouseMove(int dx,int dy) {pmu_.mouseMove(dx,dy);}
    void mouseButton(bool down,int button=0) {pmu_.mouseButton(down,button);}
    void decode(std::vector<uint32_t>& pixels) const;
    template<class Ar> void visit(Ar& ar) {
        ar.blob(ram_);ar.blob(vram_);
        ar(overlay_,resetPending_,viaAcc_,frameAcc_,pulseLeft_,ca1_,ca2_,cb1_,rtcSeed_,rtcSeedPending_,
           via_,asc_,scsi_,swim_,drive_,scc_,pmu_,dayna_);
        for(auto& disk:disks_) ar(disk);
        if constexpr(Ar::loading) {mapChanged();updateIrq();}
    }
private:
    std::vector<uint8_t> ram_,rom_,vram_;
    PortablePmu pmu_{kCpuHz};
    Via6522 via_;
    AscV8 asc_{0};
    Ncr5380 scsi_;
    ScsiDisk disks_[7];
    Swim1 swim_;
    SonyDrive drive_;
    Scc8530 scc_;
    DaynaPort dayna_;
    CdAudioPump cdPump_;
    pom68k::lle::Registry* lle_;
    PortableCpu* cpu_=nullptr;
    jit::CodeGuard* guard_=nullptr;
    bool overlay_=true,resetPending_=false,ca1_=false,ca2_=false,cb1_=true;
    int64_t viaAcc_=0,frameAcc_=0;
    int pulseLeft_=0;
    uint32_t rtcSeed_=0;
    bool rtcSeedPending_=false;
    void ca1(bool high);
    void ca2(bool high);
    void refreshVia();
    void scsiLines();
    void busError();
    uint8_t ioRead(uint32_t a);
    void ioWrite(uint32_t a,uint8_t v,bool sync=true);
    void ioSync(uint32_t a);
};
