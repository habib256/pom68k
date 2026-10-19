// MAME-derived Portable/PB100 board; notice in docs/PORTABLE_BRINGUP.md.
#include "PortableMemory.h"
#include "PortableCpu.h"
#include <algorithm>
#include <stdexcept>

PortableMemory::PortableMemory(const pom68k::CoreConfig& config,uint32_t ram)
    :ram_(ram,0),rom_(kRomSize,0xff),vram_(kVramSize,0) {
    if(ram<(1u<<20) || ram>(9u<<20) || (ram&0xfffff))
        throw std::invalid_argument("Portable RAM must be 1 to 9 whole megabytes");
    via_.configureTrace(config.peripherals.adbLleTrace);
    scc_.configureTrace(config.peripherals.sccTrace);
    drive_.configureFluxJitter(config.storage.fluxJitterPercent);
    pom68k::configureScsiBus(scsi_,disks_,dayna_,config);
    lle_=config.firmware.registry;
    pmu_.adbLine().configure(config.peripherals.adbKeyboardHandlerId,config.peripherals.adbLleTrace);
    pmu_.onResetPin=[this](bool high){
        if(!high) {overlay_=true;mapChanged();}
        else {
            resetPending_=true;
            if(rtcSeedPending_) {
                for(unsigned i=0;i<4;++i) pmu_.mcu().write(uint16_t(0x25+i),uint8_t(rtcSeed_>>(24-8*i)));
                rtcSeedPending_=false;
            }
        }
    };
    pmu_.onSecondPin=[this](bool high){ca2(high);};
    pmu_.onInterruptPin=[this](bool high){
        if(cb1_!=high && high==bool(via_.read(Via6522::PCR)&0x10)) via_.raiseCb1();
        cb1_=high;
    };
    asc_.onIrq=[this](bool){updateIrq();};
    reset();
}
bool PortableMemory::loadRom(const std::vector<uint8_t>& bytes) {
    if(bytes.size()!=kRomSize) return false;
    rom_=bytes;mapChanged();return true;
}
bool PortableMemory::attachScsi(const std::string& path,bool writeBack,int id) {
    if(id<0 || id>6 || !disks_[id].open(path,writeBack)) return false;
    scsi_.attach(&disks_[id],id);return true;
}
void PortableMemory::reset() {
    overlay_=true;resetPending_=false;viaAcc_=frameAcc_=0;pulseLeft_=0;ca1_=ca2_=false;cb1_=true;
    via_.reset();asc_.reset();scsi_.reset();scc_.reset();scc_.setClocks(kCpuHz,kCpuHz/2);
    scc_.setAbortIdle(true);swim_.reset();swim_.attachDrive(&drive_,nullptr);
    swim_.configureSuperDrive(true);drive_.setSpinClockHz(kCpuHz);
    pmu_.reset();refreshVia();mapChanged();updateIrq();
}
int PortableMemory::iplLevel() const {
    if(scc_.irqAsserted() || asc_.irqAsserted()) return 2;
    return via_.irqAsserted()?1:0;
}
void PortableMemory::updateIrq() {if(cpu_) cpu_->updateIpl();}
void PortableMemory::mapChanged() {
    if(guard_) guard_->invalidate();
    if(cpu_) cpu_->pomJitDtlbFlush();
}
void PortableMemory::ca1(bool high) {
    if(ca1_!=high && high==bool(via_.read(Via6522::PCR)&1)) via_.raiseCa1();
    ca1_=high;
}
void PortableMemory::ca2(bool high) {
    const auto pcr=via_.read(Via6522::PCR);
    if(!(pcr&8) && ca2_!=high && high==bool(pcr&4)) via_.raiseCa2();
    ca2_=high;
}
void PortableMemory::refreshVia() {
    via_.setInA(pmu_.data());
    via_.setInB(uint8_t(0x84|(pmu_.ack()?2:0)|(pmu_.request()?1:0)));
}
void PortableMemory::scsiLines() {
    via_.setCb2(scsi_.irqAsserted());ca1(scsi_.drqActive());updateIrq();
}
void PortableMemory::busError() {
    if(cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}
void PortableMemory::ioSync(uint32_t a) {
    if(!cpu_) return;
    cpu_->flushTicks();
    // macprtb.cpp charges 50 68000 cycles per VIA access and five per SWIM.
    if((a>>16)==0xf7) cpu_->stall(50);
    if((a>>16)==0xf6) cpu_->stall(5);
}
uint8_t PortableMemory::peek8(uint32_t a) const {
    a&=0xffffff;
    if(overlay_ && a<kRomSize) return rom_[a];
    if(a<ram_.size()) return ram_[a];
    if(a>=0x900000 && a<0xa00000) return rom_[a&(kRomSize-1)];
    if(a>=0xfa8000 && a<0xfb0000) return vram_[a-0xfa8000];
    return 0;
}
uint8_t PortableMemory::read8(uint32_t a) {
    a&=0xffffff;
    if(a>=0x900000 && a<0xa00000) {
        if(overlay_) {overlay_=false;mapChanged();}
        return rom_[a&(kRomSize-1)];
    }
    if(a>=0xf60000) return ioRead(a);
    return peek8(a);
}
uint16_t PortableMemory::read16(uint32_t a) {
    a&=0xffffff;
    const auto bank=a>>16;
    if(bank==0xf6 || bank==0xf7 || bank==0xfd) return uint16_t(ioRead(a))*0x101;
    if(bank==0xf9) return uint16_t(ioRead(a))<<8;
    const auto high=read8(a);return uint16_t((uint16_t(high)<<8)|read8(a+1));
}
void PortableMemory::write8(uint32_t a,uint8_t v) {
    a&=0xffffff;
    if(a<ram_.size() && !(overlay_ && a<kRomSize)) {
        if(guard_) guard_->note(a,1);
        ram_[a]=v;return;
    }
    if(a>=0xf60000) ioWrite(a,v);
}
void PortableMemory::write16(uint32_t a,uint16_t v) {
    a&=0xffffff;
    switch(a>>16) {
    case 0xf7:ioWrite(a,uint8_t(v));ioWrite(a,uint8_t(v>>8),false);return;
    case 0xf6:ioWrite(a,uint8_t(v));return;
    case 0xf9:ioWrite(a|1,uint8_t(v));return;
    case 0xfd:ioWrite(a,uint8_t(v>>8));return;
    default:write8(a,uint8_t(v>>8));write8(a+1,uint8_t(v));return;
    }
}
uint8_t PortableMemory::ioRead(uint32_t a) {
    ioSync(a);
    switch(a>>16) {
    case 0xf6:return swim_.read((a>>9)&15);
    case 0xf7:{refreshVia();const auto v=via_.read((a>>9)&15);updateIrq();return v;}
    case 0xf9:{
        if(a&1) return 0;
        const unsigned raw=(a>>4)&15,reg=raw&7;
        const bool dma=raw==6 && (a&0xffff)>=0x260;
        if(dma && !scsi_.drqActive()) busError();
        const auto v=dma?scsi_.dmaRead():scsi_.read(reg);scsiLines();return v;
    }
    case 0xfa:return a>=0xfa8000?vram_[a-0xfa8000]:0;
    case 0xfb:return asc_.read(a&0xffff);
    case 0xfc:return ram_.size()<(6u<<20)?0xff:0;
    case 0xfd:{const int channel=(a>>1)&1;
        const auto v=(a&4)?scc_.readData(channel):scc_.readCtl(channel);updateIrq();return v;}
    default:return 0;
    }
}
void PortableMemory::ioWrite(uint32_t a,uint8_t v,bool sync) {
    if(sync) ioSync(a);
    switch(a>>16) {
    case 0xf6:swim_.write((a>>9)&15,v);break;
    case 0xf7:
        via_.write((a>>9)&15,v);
        pmu_.setData(via_.portA());pmu_.setRequest((via_.portB()&1)!=0);
        swim_.setSel((via_.portB()&0x20)!=0);updateIrq();break;
    case 0xf9:{
        if(!(a&1)) break; // NCR input latch is on the lower write byte lane.
        const unsigned raw=(a>>4)&15,reg=raw&7;
        if(raw==0 && (a&0xffff)>=0x200) {
            if(!scsi_.drqActive()) busError();
            scsi_.dmaWrite(v);
        } else scsi_.write(reg,v);
        scsiLines();break;
    }
    case 0xfa:if(a>=0xfa8000) vram_[a-0xfa8000]=v;break;
    case 0xfb:asc_.write(a&0xffff,v);break;
    case 0xfd:{const int channel=(a>>1)&1;
        if(a&4) scc_.writeData(channel,v);else scc_.writeCtl(channel,v);updateIrq();break;}
    default:break;
    }
}
const uint8_t* PortableMemory::codeSpan(uint32_t a,uint32_t& len) const {
    len=0;if(a>0xffffff || overlay_) return nullptr;
    if(a<ram_.size()) {len=uint32_t(ram_.size()-a);return ram_.data()+a;}
    if(a>=0x900000 && a<0xa00000) {
        const auto off=a&(kRomSize-1);len=kRomSize-off;return rom_.data()+off;
    }
    return nullptr;
}
uint8_t* PortableMemory::dataSpan(uint32_t a,uint32_t& len,bool write) {
    if(!write) return const_cast<uint8_t*>(codeSpan(a,len));
    len=0;if(overlay_ || a>=ram_.size()) return nullptr;
    len=uint32_t(ram_.size()-a);return ram_.data()+a;
}
void PortableMemory::tick(int cycles) {
    cdPump_.advance(disks_,cycles,kCpuHz);
    viaAcc_+=cycles;const int via=int(viaAcc_/20);viaAcc_%=20;
    if(via) via_.tick(via);
    if(pulseLeft_>0) {pulseLeft_-=cycles;if(pulseLeft_<=0) {pulseLeft_=0;ca1(false);}}
    frameAcc_+=int64_t(cycles)*1203;
    while(frameAcc_>=kCpuHz*20) {
        frameAcc_-=kCpuHz*20;ca1(true);pmu_.vblank();pulseLeft_=497;
    }
    swim_.tick(cycles);drive_.tick(cycles);asc_.tick(cycles);scc_.tick(cycles);pmu_.tick(cycles);
    updateIrq();
}
void PortableMemory::decode(std::vector<uint32_t>& pixels) const {
    pixels.resize(640*400);
    if(pmu_.displayBlank()) {std::fill(pixels.begin(),pixels.end(),0);return;}
    for(unsigned p=0;p<640*400;++p) pixels[p]=(vram_[p>>3]&(0x80>>(p&7)))?0:0xffffff;
}
