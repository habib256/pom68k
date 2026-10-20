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
#include "DaynaPortBus.h"
#include "CdAudioPump.h"
#include "Pb030Niagra.h"
#include "PangolaVideo.h"
#include "Pb030ExternalVideo.h"

class Pb030Cpu;
class Pb030Memory {
public:
    static constexpr uint32_t kRomSize=0x100000,kVramSize=0x8000;
    static constexpr int64_t kCpuHz=15667200,kViaHz=1566720;
    explicit Pb030Memory(const pom68k::CoreConfig& coreConfig,uint32_t ram=0x800000,
                         int64_t cpuHz=kCpuHz,bool niagra=false,int colorHeight=0);
    int screenHeight() const {return colorHeight_?colorHeight_:400;}
    PangolaVideo& colorVideo() {return color_;}
    Pb030ExternalVideo& externalVideo() {return externalVideo_;}
    uint8_t monitorSense() const {return externalVideo_.connected()?6:7;}
    void setMonitorSense(uint8_t sense) {externalVideo_.setMonitor(niagra_ && sense==6);}
    bool externalMonitor() const {return niagra_ && externalVideo_.connected();}
    void prepareExternalPower() {
        if(!externalMonitor()) return;
        // An attached screen requires the continuously powered PMU's
        // charger measurement before PrimaryInit. Run firmware, not a RAM seed.
        pmu_.reset();
        for(int f=0;f<600;++f) {pmu_.vblank();pmu_.tick(int(cpuHz_/60));}
    }
    void decodeExternal(std::vector<uint32_t>& out) const {externalVideo_.decode(extVram_,out);}
    int displayWidth() const {return externalMonitor() && screenHeight()==480?1280:640;}
    int displayHeight() const {return externalMonitor() && screenHeight()!=480?screenHeight()+480:screenHeight();}
    void decodeDisplay(std::vector<uint32_t>& out) const {
        decode(out);
        if(!externalMonitor()) return;
        std::vector<uint32_t> external;decodeExternal(external);
        if(screenHeight()!=480) {out.insert(out.end(),external.begin(),external.end());return;}
        const auto lcd=std::move(out);out.resize(1280*480);
        for(int y=0;y<480;++y) {
            std::copy_n(lcd.begin()+y*640,640,out.begin()+y*1280);
            std::copy_n(external.begin()+y*640,640,out.begin()+y*1280+640);
        }
    }
    bool isNiagra() const { return niagra_; }
    bool loadRom(const std::vector<uint8_t>& rom);
    bool selectColorFirmware(const pom68k::CoreFirmwareConfig& config);
    bool selectPmuFirmware(const pom68k::CoreFirmwareConfig& config);
    bool loadPram(const std::string& path);
    void savePram(const std::string& path);
    pom68k::lle::Registry& lleRegistry() const { return *lle_; }
    uint32_t romChecksum() const {
        return (uint32_t(rom_[0])<<24)|(uint32_t(rom_[1])<<16)|(uint32_t(rom_[2])<<8)|rom_[3];
    }
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
    bool consumeReset() {
        const bool v=resetPending_;resetPending_=false;
        if(v && resetOverlayPending_) {overlay_=true;mapChanged();}
        resetOverlayPending_=false;return v;
    }
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
    AscEasc& asc() { return asc_; }
    DaynaPort& daynaPort() { return dayna_; }
    void keyEvent(uint8_t key,bool down) { pmu_.keyEvent(key,down); }
    void mouseMove(int dx,int dy) { pmu_.mouseMove(dx,dy); }
    void mouseButton(bool down,int button=0) { pmu_.mouseButton(down,button); }
    bool externalFloppyPort() const { return false; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void ejectDisk() { drive_.eject(); }
    void attachDriveSounds(FloppySoundSink* floppy,FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);for(auto& d:disks_) d.setSoundSink(hdd);
    }
    void attachCdAudioSink(CdAudioSink* cd) { for(auto& d:disks_) d.setCdAudioSink(cd); }
    bool attachCdrom(const std::string& path,int id=3) {
        if(id<0 || id>6 || !disks_[id].openCdrom(path)) return false;
        scsi_.attach(&disks_[id],id);return true;
    }
    bool attachCdromEmpty(int id) {
        if(id<1 || id>6) return false;
        disks_[id].attachCdromEmpty();
        scsi_.attach(&disks_[id],id);return true;
    }
    bool bayIsCdrom(int id) const { return id>=1 && id<7 && disks_[id].cdrom() && disks_[id].present(); }
    bool insertBayMedia(int id,const std::string& path) { return bayIsCdrom(id) && disks_[id].openCdrom(path); }
    void ejectBayMedia(int id) { if(bayIsCdrom(id)) disks_[id].eject(); }
    bool detachScsi(int id) {
        if(id<1 || id>6 || disks_[id].cdrom() || !disks_[id].present() || !scsi_.detach(id)) return false;
        disks_[id].close();return true;
    }
    void debugDevices(std::vector<pom68k::dev::Snapshot>& out) const;
    const std::vector<uint8_t>& vram() const { return vram_; }
    void decode(std::vector<uint32_t>& frame) const;
    template<class Ar> void visit(Ar& ar) {
        ar.blob(ram_);ar.blob(vram_);
        ar(overlay_,resetPending_,resetOverlayPending_,viaAcc_,tickAcc_,c15Acc_,secAcc_,
           via_,pvia_,rtc_,asc_,scsi_,swim_,drive_,scc_,pmu_,dayna_);
        if(niagra_) {ar(niagraVideo_);ar.blob(extVram_);}
        if(colorHeight_) ar(color_);
        for(auto& d:disks_) ar(d);
        // Optional suffix: older Niagra/Pangola containers end after disks.
        if(niagra_) {
            if constexpr(Ar::loading) {
                if(ar.remaining()) ar(externalVideo_);
                else {externalVideo_.setMonitor(false);externalVideo_.reset();}
            } else ar(externalVideo_);
        }
        if constexpr(Ar::loading) { mapChanged();updateIrq(); }
    }
private:
    std::vector<uint8_t> ram_,rom_,vram_,extVram_;
    bool niagra_;
    int colorHeight_;
    PangolaVideo color_;
    Pb030Niagra niagraVideo_;
    Pb030ExternalVideo externalVideo_;
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
    DaynaPort dayna_;
    CdAudioPump cdPump_;
    pom68k::lle::Registry* lle_;
    Pb030Cpu* cpu_=nullptr;
    jit::CodeGuard* guard_=nullptr;
    bool overlay_=true,resetPending_=false,resetOverlayPending_=false;
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
