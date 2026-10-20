// PowerBook 160/165/180 external video, slot E (GPLv3).
// Apple PB160/180 Developer Note pp.21-24; E33B2724 ROM $9B82E-$9CB82.
// MAME macpwrbk030.cpp ext_video_* names the register/depth layout.
#pragma once
#include "SaveState.h"
#include <array>
#include <vector>
#include <algorithm>
#include <functional>

class Pb030ExternalVideo {
public:
    void setMonitor(bool connected) {sense_=connected?6:7;}
    bool connected() const {return sense_==6;}
    void reset() {
        registers_.fill(0);palette_.fill({0,0,0});index_=component_=readIndex_=readComponent_=0;
        slotMask_=mainMask_=0;pending_=false;pixelAcc_=0;beam_=0;
    }
    uint8_t peek(uint32_t a) const {
        a&=255;
        return a==4?uint8_t((sense_&~registers_[4])<<4):registers_[a];
    }
    uint8_t read(uint32_t a) {
        const auto v=peek(a);if(onIo) onIo(a&255,false,v);
        return v;
    }
    void write(uint32_t a,uint8_t v) {
        a&=255;if(onIo) onIo(a,true,v);
        registers_[a]=v;
        if(a==0x14) pending_=false;
    }
    uint8_t dacRead(uint32_t a) {
        switch(a&3) {
        case 0:return index_;
        case 1:{const auto v=palette_[readIndex_][readComponent_];
            if(++readComponent_==3) {readComponent_=0;++readIndex_;}return v;}
        default:return 0;
        }
    }
    void dacWrite(uint32_t a,uint8_t v) {
        if((a&3)==0) {index_=readIndex_=v;component_=readComponent_=0;}
        if((a&3)==1) {
            palette_[index_][component_]=v;
            if(++component_==3) {component_=0;++index_;}
        }
    }
    uint8_t slotRead(uint32_t a) const {
        switch(a&255) {
        case 2:return blank()?0x3f:0x7f;
        case 3:return irq()?0x82:0;
        case 0x12:return slotMask_;
        case 0x13:return mainMask_;
        default:return 0;
        }
    }
    void slotWrite(uint32_t a,uint8_t v) {
        auto mask=[&](uint8_t& latch){if(v&0x80) latch|=v&0x7f;else latch&=uint8_t(~v);};
        if((a&255)==0x12) mask(slotMask_);
        if((a&255)==0x13) mask(mainMask_);
    }
    bool irq() const {return connected() && pending_ && (slotMask_&0x40) && (mainMask_&2);}
    bool blank() const {return beam_>=864u*480;}
    void tick(int cycles,int64_t hz) {
        if(!connected()) return;
        pixelAcc_+=int64_t(cycles)*30240000;
        const auto clocks=uint64_t(pixelAcc_/hz);pixelAcc_%=hz;
        constexpr uint64_t total=864u*525,start=864u*480;
        const auto until=beam_<start?start-beam_:total-beam_+start;
        if(clocks>=until) pending_=true;
        beam_=(beam_+clocks)%total;
    }
    // PrimaryInit tests the second bank with control $43 and drops bit 1
    // when it is absent. The supported boards have 512 KB, not a 1 MB bank.
    uint8_t memoryRead(const std::vector<uint8_t>& vram,uint32_t a) const {
        return (registers_[0x10]&2)?0xff:vram[a&0x7ffff];
    }
    void memoryWrite(std::vector<uint8_t>& vram,uint32_t a,uint8_t v) {
        if(!(registers_[0x10]&2)) vram[a&0x7ffff]=v;
    }
    void decode(const std::vector<uint8_t>& vram,std::vector<uint32_t>& out) const {
        out.assign(640*480,0);
        if(!connected() || !(registers_[0x10]&0x80)) return;
        const auto mode=registers_[8];if(mode>3) return;
        const unsigned bits=1u<<mode,row=640*bits/8;
        for(unsigned p=0;p<640*480;++p) {
            const unsigned a=(p/640)*row+(p%640)*bits/8;
            const unsigned pen=(vram[a&0x7ffff]>>(8-bits-((p%640)*bits&7)))&((1u<<bits)-1);
            // Sub-byte modes occupy the high bits of each DAC index.
            const auto& rgb=palette_[(pen<<(8-bits))|((1u<<(8-bits))-1)];
            out[p]=(uint32_t(rgb[0])<<16)|(uint32_t(rgb[1])<<8)|rgb[2];
        }
    }
    const auto& registers() const {return registers_;}
    std::function<void(uint32_t,bool,uint8_t)> onIo;
    template<class Ar> void visit(Ar& ar) {
        ar(registers_,palette_,sense_,index_,component_,readIndex_,readComponent_,
            slotMask_,mainMask_,pending_,pixelAcc_,beam_);
        if constexpr(Ar::loading) {
            if((sense_!=6 && sense_!=7) || component_>=3 || readComponent_>=3 || beam_>=864u*525)
                ar.fail("Invalid PowerBook external video state");
        }
    }
private:
    std::array<uint8_t,256> registers_{};
    std::array<std::array<uint8_t,3>,256> palette_{};
    uint8_t sense_=7,index_=0,component_=0,readIndex_=0,readComponent_=0,slotMask_=0,mainMask_=0;
    bool pending_=false;
    int64_t pixelAcc_=0;
    uint64_t beam_=0;
};
