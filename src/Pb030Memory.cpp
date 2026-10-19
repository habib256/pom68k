// POM68K — PowerBook 140/170 memory and peripheral graph (GPLv3).
#include "Pb030Memory.h"
#include "Pb030Cpu.h"
#include <algorithm>

Pb030Memory::Pb030Memory(const pom68k::CoreConfig& config,uint32_t ram,int64_t hz,bool niagra)
    :ram_(ram,0),rom_(kRomSize,0xff),vram_(niagra?0x20000:kVramSize,0),
     extVram_(niagra?0x80000:0,0),niagra_(niagra),cpuHz_(hz),pmu_(hz),asc_(hz) {
    via_.configureTrace(config.peripherals.adbLleTrace);
    rtc_.configure(config.peripherals.appleTalkPram,config.peripherals.rtcTrace);
    rtc_.factoryDefaults();
    scc_.configureTrace(config.peripherals.sccTrace);
    drive_.configureFluxJitter(config.storage.fluxJitterPercent);
    lle_=config.firmware.registry;
    pom68k::configureScsiBus(scsi_,disks_,dayna_,config);
    pmu_.onResetRelease=[this](){resetPending_=resetOverlayPending_=true;};
    pmu_.onInterruptPin=[this](bool high){via_.extCb1Int(high);};
    pmu_.adbLine().configure(config.peripherals.adbKeyboardHandlerId,config.peripherals.adbLleTrace);
    pvia_.onPortBRead=[this](){return uint8_t((pmu_.ack()?2:0)|(pmu_.request()?4:0));};
    pvia_.onPortB=[this](uint8_t v){pmu_.setRequest((v&4)!=0);};
    pvia_.onConfigRead=[this](){return pmu_.data();};
    pvia_.onPortA=[this](uint8_t v){pmu_.setData(v);};
    asc_.onIrq=[this](bool high){pvia_.ascIrq(high);updateIrq();};
    reset();
}
bool Pb030Memory::loadRom(const std::vector<uint8_t>& rom) {
    if(rom.size()!=kRomSize) return false;
    rom_=rom;return true;
}
bool Pb030Memory::attachScsi(const std::string& path,bool writeBack,int id) {
    if(id<0 || id>6 || !disks_[id].open(path,writeBack)) return false;
    scsi_.attach(&disks_[id],id);return true;
}
void Pb030Memory::reset() {
    overlay_=true;resetPending_=resetOverlayPending_=false;
    viaAcc_=tickAcc_=c15Acc_=secAcc_=0;niagraVideo_.reset();
    via_.reset();via_.setInA(0x93);via_.setInB(8|rtc_.dataBit());
    pvia_.reset();rtc_.reset();asc_.reset();scsi_.reset();scc_.reset();
    scc_.setClocks(cpuHz_,7833600);scc_.setAbortIdle(true);
    swim_.reset();swim_.attachDrive(&drive_,nullptr);swim_.configureSuperDrive(true);
    drive_.setSpinClockHz(15667200);
    pmu_.reset();mapChanged();updateIrq();
}
void Pb030Memory::busError() const {
    if(cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}
int Pb030Memory::iplLevel() const {
    if(scc_.irqAsserted()) return 4;
    if(pvia_.irqAsserted()) return 2;
    return via_.irqAsserted()?1:0;
}
void Pb030Memory::updateIrq() { if(cpu_) cpu_->updateIpl(); }
void Pb030Memory::mapChanged() {
    if(guard_) guard_->invalidate();
    if(cpu_) cpu_->pomJitDtlbFlush();
}
void Pb030Memory::viaSync() {
    if(!cpu_) return;
    const auto now=cpu_->machineClock(),cycle=now*kViaHz/cpuHz_;
    const auto end=(cycle*2+3)*cpuHz_/(2*kViaHz)+1;
    if(end>now) cpu_->stall(int(end-now));
}
uint8_t Pb030Memory::viaAccess(uint32_t a,bool write,uint8_t v) {
    if(cpu_) cpu_->flushTicks();
    viaSync();
    const int reg=(a>>9)&15;
    if(write) {
        via_.write(reg,v);
        swim_.setSel((via_.portA()&0x20)!=0);
        rtc_.setLines(!(via_.portB()&4),(via_.portB()&2)!=0,(via_.portB()&1)!=0);
    } else via_.setInB(uint8_t(8|rtc_.dataBit()));
    const uint8_t value=write?0:via_.read(reg);
    updateIrq();return value;
}
void Pb030Memory::scsiLines() {
    pvia_.scsiDrq(scsi_.drqActive());pvia_.scsiIrq(scsi_.irqAsserted());updateIrq();
}
uint8_t Pb030Memory::dmaRead() {
    if(!scsi_.drqActive()) busError();
    const auto value=scsi_.dmaRead();scsiLines();return value;
}
void Pb030Memory::dmaWrite(uint8_t v) {
    if(!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);scsiLines();
}
const uint8_t* Pb030Memory::codeSpan(uint32_t a,uint32_t& len) const {
    len=0;if(overlay_) return nullptr;
    if(a<ram_.size()) {len=uint32_t(ram_.size()-a);return ram_.data()+a;}
    if(a>=0x40000000 && a<0x50000000) {
        const auto offset=a&(kRomSize-1);len=kRomSize-offset;return rom_.data()+offset;
    }
    return nullptr;
}
uint8_t* Pb030Memory::dataSpan(uint32_t a,uint32_t& len,bool write) {
    if(!write) return const_cast<uint8_t*>(codeSpan(a,len));
    len=0;if(overlay_ || a>=ram_.size()) return nullptr;
    len=uint32_t(ram_.size()-a);return ram_.data()+a;
}
uint8_t Pb030Memory::peek8(uint32_t a) const {
    if(a<0x40000000) {
        if(overlay_ && a<kRomSize) return rom_[a];
        return a<ram_.size()?ram_[a]:0xff;
    }
    if(a<0x50000000) return rom_[a&(kRomSize-1)];
    if(niagra_) {
        if(a>=0x60000000 && a<0x70000000) return vram_[a&0x1ffff];
        if(a>=0xfe100000 && a<0xfe180000) return extVram_[a&0x7ffff];
        if(a>=0xfe0fe000 && a<0xfe0fe100) return (a&255)==4?0x70:0;
    } else if(a>=0xfee00000 && a<0xfee10000) return vram_[a&(kVramSize-1)];
    return 0;
}
uint8_t Pb030Memory::read8(uint32_t a) {
    if(a<0x40000000) return peek8(a);
    if(a<0x50000000) {
        if(overlay_) { overlay_=false;mapChanged(); }
        return rom_[a&(kRomSize-1)];
    }
    if(niagra_) {
        if(a>=0x60000000 && a<0x70000000) return vram_[a&0x1ffff];
        if(a>=0xfe100000 && a<0xfe180000) return extVram_[a&0x7ffff];
        if(a>=0xfe0fe000 && a<0xfe0fe100) return (a&255)==4?0x70:0;
    } else if(a>=0xfee00000 && a<0xfee10000) return vram_[a&(kVramSize-1)];
    if(a<0x52000000 && (!niagra_ || (a&0xfff00000)==0x50f00000)) return ioRead(a&0xfffff);
    return 0;
}
void Pb030Memory::write8(uint32_t a,uint8_t v) {
    if(a<0x40000000) {
        if(a<ram_.size() && !(overlay_ && a<kRomSize)) {
            if(guard_) guard_->note(a,1);
            ram_[a]=v;
        }
        return;
    }
    if(a<0x50000000) return;
    if(niagra_) {
        if(a>=0x60000000 && a<0x70000000) {vram_[a&0x1ffff]=v;return;}
        if(a>=0xfe100000 && a<0xfe180000) {extVram_[a&0x7ffff]=v;return;}
    } else if(a>=0xfee00000 && a<0xfee10000) {vram_[a&(kVramSize-1)]=v;return;}
    if(a<0x52000000 && (!niagra_ || (a&0xfff00000)==0x50f00000)) ioWrite(a&0xfffff,v);
}
uint16_t Pb030Memory::read16(uint32_t a) {
    if(a>=0x50000000 && a<0x52000000 && (!niagra_ || (a&0xfff00000)==0x50f00000)) {
        const auto low=a&0xfffff;
        if(low<0x2000) {const uint16_t v=viaAccess(low,false);return uint16_t(v*0x101);}
        if(low>=0x4000 && low<0x6000) {const uint16_t v=ioRead(low);return uint16_t(v*0x101);}
        if((low>=0x10000 && low<0x12000) || (low>=0x16000 && low<0x18000))
            return uint16_t(ioRead(low))<<8;
    }
    const uint16_t hi=read8(a);return uint16_t((hi<<8)|read8(a+1));
}
void Pb030Memory::write16(uint32_t a,uint16_t v) {
    if(a>=0x50000000 && a<0x52000000 && (!niagra_ || (a&0xfff00000)==0x50f00000)) {
        const auto low=a&0xfffff;
        if(low<0x2000) {viaAccess(low,true,uint8_t(v));viaAccess(low,true,uint8_t(v>>8));return;}
        if((low>=0x4000 && low<0x6000) || (low>=0x10000 && low<0x12000) ||
           (low>=0x16000 && low<0x18000)) {ioWrite(low,uint8_t(v>>8));return;}
    }
    write8(a,uint8_t(v>>8));write8(a+1,uint8_t(v));
}
uint8_t Pb030Memory::ioRead(uint32_t a) {
    if(a<0x2000) return viaAccess(a,false);
    if(cpu_) cpu_->flushTicks();
    if(a<0x4000) {viaSync();const auto v=pvia_.read(a-0x2000);updateIrq();return v;}
    if(a<0x6000) {
        const int ch=(a>>1)&1;
        const auto v=(a&4)?scc_.readData(ch):scc_.readCtl(ch);updateIrq();return v;
    }
    if(a<0x8000 || (a>=0x12060 && a<0x12064)) return dmaRead();
    if(a>=0x10000 && a<0x12000) {
        if(a&1) return 0;
        const int reg=(a>>4)&7;
        if(reg==6 && (a&0xfff)>=0x260) return dmaRead();
        const auto v=scsi_.read(reg);scsiLines();return v;
    }
    if(a>=0x14000 && a<0x16000) return asc_.read(a-0x14000);
    if(a>=0x16000 && a<0x18000) {
        if(a&1) return 0;
        if(cpu_) cpu_->stall(5);
        return swim_.read((a>>9)&15);
    }
    if(niagra_ && a>=0x20000 && a<0x22000) return niagraVideo_.gscRead(a-0x20000);
    if(a>=0x24000 && a<0x28000) busError();
    if(a>=0x80000 && a<0xc0000) {
        if(niagra_) return niagraVideo_.controlRead(a,cpuHz_);
        if(((a-0x80000)>>12)==0x34 && !(a&3)) return cpuHz_==25000000?1:0;
    }
    return 0;
}
void Pb030Memory::ioWrite(uint32_t a,uint8_t v) {
    if(a<0x2000) {viaAccess(a,true,v);return;}
    if(cpu_) cpu_->flushTicks();
    if(a<0x4000) {viaSync();pvia_.write(a-0x2000,v);updateIrq();return;}
    if(a<0x6000) {
        const int ch=(a>>1)&1;
        if(a&4) scc_.writeData(ch,v);else scc_.writeCtl(ch,v);updateIrq();return;
    }
    if(a<0x8000 || (a>=0x12060 && a<0x12064)) {dmaWrite(v);return;}
    if(a>=0x10000 && a<0x12000) {
        if(a&1) return;
        const int reg=(a>>4)&7;
        if(reg==0 && (a&0xfff)>=0x200) {dmaWrite(v);return;}
        scsi_.write(reg,v);scsiLines();return;
    }
    if(a>=0x14000 && a<0x16000) {asc_.write(a-0x14000,v);if(niagra_) niagraVideo_.soundWritten();return;}
    if(a>=0x16000 && a<0x18000) {
        if(a&1) return;
        if(cpu_) cpu_->stall(5);
        swim_.write((a>>9)&15,v);return;
    }
    if(niagra_ && a>=0x20000 && a<0x22000) {niagraVideo_.gscWrite(a-0x20000,v);return;}
    if(niagra_ && a>=0x80000 && a<0xc0000) niagraVideo_.controlWrite(a,v);
    if(a>=0x80000 && a<0xc0000 && ((a-0x80000)>>12)==0x20) {
        // CPU-only wake reset preserves the RAM reset vectors (MAME jaws_w).
        resetPending_=true;
    }
}
void Pb030Memory::tick(int cycles) {
    cdPump_.advance(disks_,cycles,cpuHz_);
    viaAcc_+=int64_t(cycles)*kViaHz;
    const int via=int(viaAcc_/cpuHz_);viaAcc_-=int64_t(via)*cpuHz_;
    if(via) via_.tick(via);
    tickAcc_+=int64_t(cycles)*1203;
    while(tickAcc_>=cpuHz_*20) {tickAcc_-=cpuHz_*20;via_.raiseCa1();pmu_.vblank();}
    secAcc_+=cycles;
    while(secAcc_>=cpuHz_) {secAcc_-=cpuHz_;rtc_.tickSecond();via_.raiseCa2();}
    c15Acc_+=int64_t(cycles)*15667200;
    const int c15=int(c15Acc_/cpuHz_);c15Acc_-=int64_t(c15)*cpuHz_;
    if(c15) {swim_.tick(c15);drive_.tick(c15);}
    asc_.tick(cycles);scc_.tick(cycles);pmu_.tick(cycles);updateIrq();
}
void Pb030Memory::decode(std::vector<uint32_t>& frame) const {
    if(niagra_) {niagraVideo_.decode(vram_,pmu_.displayBlank(),frame);return;}
    frame.resize(640*400);
    if(pmu_.displayBlank()) {std::fill(frame.begin(),frame.end(),0);return;}
    for(unsigned p=0;p<640*400;++p)
        frame[p]=(vram_[p>>3]&(0x80>>(p&7)))?0:0xffffff;
}
