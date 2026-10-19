// POM68K — M50753 peripherals; see M50753.h for provenance.
#include "M50753.h"
#include <algorithm>

M50753::M50753(uint32_t oscillatorHz):oscillatorHz_(oscillatorHz) {
    cpu_.read8=[this](uint16_t a){return read(a);};
    cpu_.write8=[this](uint16_t a,uint8_t v){write(a,v);};
    rom_.fill(0xff);
}

bool M50753::loadRom(const std::vector<uint8_t>& rom) {
    if(rom.size()!=kRomSize) return false;
    std::copy(rom.begin(),rom.end(),rom_.begin());
    return true;
}

void M50753::reset() {
    ports_.fill(0); ddr_.fill(0); prescale_.fill(0);
    count_.fill(0); latch_.fill(0); phase_.fill(0); started_.fill(false);
    intCtrl_=timerCtrl_=adControl_=0; adcLeft_=0; pwm_=false;
    cpu_.reset();
    irqs();
    for(unsigned p=0;p<5;++p) sendPort(p);
}

int M50753::portIndex(uint16_t a) {
    if(a>=0xe0 && a<=0xe5) return (a-0xe0)/2;
    if(a>=0xe8 && a<=0xeb) return 3+(a-0xe8)/2;
    return -1;
}

void M50753::sendPort(unsigned p) {
    if(writePort) writePort(p,uint8_t((ports_[p]&ddr_[p])|(pullups_[p]&~ddr_[p])));
}

void M50753::setPullups(unsigned p,uint8_t bits) {
    if(p<5) { pullups_[p]=p==4 ? bits&15:bits; sendPort(p); }
}

uint8_t M50753::read(uint16_t a) {
    if(a<ram_.size()) return ram_[a];
    if(a>=0xe800) return rom_[a-0xe800];
    const int p=portIndex(a);
    if(p>=0) {
        if(a&1) return ddr_[p];
        const uint8_t in=readPort ? readPort(unsigned(p)):0;
        const uint8_t v=uint8_t((in&~ddr_[p])|(ports_[p]&ddr_[p]));
        return p==4 ? v&15:v;
    }
    switch(a) {
    case 0xee:return readInput ? readInput():0;
    case 0xef:intCtrl_&=~0x20;irqs();return readAnalog ? readAnalog(adControl_&7):0;
    case 0xf2:return 0;
    case 0xf3:return adControl_;
    case 0xf5:return pwm_ ? 1:0;
    case 0xf9:return prescale_[0];
    case 0xfa:return count_[0]; case 0xfb:return count_[1];
    case 0xfc:return prescale_[1]; case 0xfd:return count_[2];
    case 0xfe:return intCtrl_; case 0xff:return timerCtrl_;
    default:return 0xff;
    }
}

void M50753::write(uint16_t a,uint8_t v) {
    if(a<ram_.size()) { ram_[a]=v;return; }
    const int p=portIndex(a);
    if(p>=0) {
        if(p==4) v&=15;
        if(a&1) ddr_[p]=v; else ports_[p]=v;
        sendPort(unsigned(p));return;
    }
    switch(a) {
    case 0xf2:
        // CPU clock = oscillator / 4; conversion is 72 microseconds.
        adcLeft_=int((uint64_t(oscillatorHz_)*72+3999999)/4000000);break;
    case 0xf3:adControl_=v&15;break;
    case 0xf5:pwm_=(v&1)!=0;break;
    case 0xf9:prescale_[0]=v;phase_[0]=phase_[1]=0;started_[0]=started_[1]=true;break;
    case 0xfa:count_[0]=latch_[0]=v;break;
    case 0xfb:count_[1]=latch_[1]=v;break;
    case 0xfc:prescale_[1]=v;phase_[2]=0;started_[2]=true;break;
    case 0xfd:count_[2]=latch_[2]=v;break;
    case 0xfe:intCtrl_=v&(intCtrl_|uint8_t(~0x82));irqs();break;
    case 0xff:timerCtrl_=v&(timerCtrl_|uint8_t(~0x80));irqs();break;
    default:break;
    }
}

void M50753::setInterrupt(unsigned input,bool asserted) {
    if(input>1) return;
    const uint8_t bit=input==0 ? 0x80:0x02;
    intCtrl_=asserted ? intCtrl_|bit:intCtrl_&~bit;
    irqs();
}

void M50753::irqs() {
    cpu_.setInterrupt(0,(intCtrl_&0xc0)==0xc0);
    cpu_.setInterrupt(1,(timerCtrl_&0xc0)==0xc0);
    cpu_.setInterrupt(2,(intCtrl_&0x30)==0x30);
    cpu_.setInterrupt(3,(intCtrl_&0x0c)==0x0c);
    cpu_.setInterrupt(4,(intCtrl_&0x03)==0x03);
}

void M50753::tick(int cycles) {
    for(unsigned t=0;t<3;++t) {
        if(!started_[t] || (t==2 && (timerCtrl_&0x2c))) continue;
        const int period=4*(int(prescale_[t==2 ? 1:0])+2);
        phase_[t]+=cycles;
        while(phase_[t]>=period) {
            phase_[t]-=period;
            if(--count_[t]==0) {
                count_[t]=latch_[t];
                if(t==2) timerCtrl_|=0x80;
                else intCtrl_|=t==0 ? 0x20:0x08;
                irqs();
            }
        }
    }
    if(adcLeft_>0) {
        adcLeft_-=cycles;
        if(adcLeft_<=0) {
            adcLeft_=0;
            // MAME's existing M50753 model enables the ADC request with bit 2.
            if(adControl_&4) { intCtrl_|=0x20;irqs(); }
        }
    }
}

int M50753::step() { const int c=cpu_.step();tick(c);return c; }
int M50753::run(int cycles) {
    int elapsed=0;
    while(elapsed<cycles) elapsed+=step();
    return elapsed;
}
