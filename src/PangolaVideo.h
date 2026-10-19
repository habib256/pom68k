// POM68K — Pangola / WD90C26 LCD graphics path (GPLv3).
// References and BSD-3-Clause attribution: docs/PB030_BRINGUP.md.
#pragma once
#include "SaveState.h"
#include <array>
#include <vector>
#include <functional>
#include <cstdint>

class PangolaVideo {
public:
    void configure(int height) {height_=height;vram_.resize(0x80000);}
    void reset();
    bool loadRom(const std::vector<uint8_t>& bytes);
    bool hasRom() const {return rom_.size()==0x8000;}
    uint8_t romRead(uint32_t a) const {return hasRom()?rom_[a&0x7fff]:0xff;}
    uint8_t memoryRead(uint32_t a) const;
    void memoryWrite(uint32_t a,uint8_t v);
    uint8_t controlRead(uint32_t a) const {return uint8_t(pangola_>>((1-(a&1))*8));}
    void controlWrite(uint32_t a,uint8_t v) {
        const int shift=(1-(a&1))*8;pangola_=uint16_t((pangola_&~(0xff<<shift))|(v<<shift));
    }
    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port,uint8_t v);
    void modeSetup(uint8_t v);
    void tick(int cycles,int64_t cpuHz);
    void decode(bool blank,std::vector<uint32_t>& out) const;
    int height() const {return height_;}
    uint8_t crtc(unsigned r) const {return crtc_[r&255];}
    uint8_t seq(unsigned r) const {return seq_[r&255];}
    const std::vector<uint8_t>& vram() const {return vram_;}
    std::function<void(uint16_t,bool,uint8_t)> onIo;
    template<class Ar> void visit(Ar& ar) {
        uint64_t identity=romIdentity_;ar(identity);
        if constexpr(Ar::loading) if(identity!=romIdentity_) {ar.fail("Color video ROM differs");return;}
        ar.blob(vram_);ar(crtc_,seq_,gfx_,attr_,dac_,pangola_,crtcIndex_,seqIndex_,gfxIndex_,attrAddress_,
            attrData_,misc_,feature_,mask_,readIndex_,writeIndex_,readComponent_,writeComponent_,dacRead_,
            setup_,enabled_,pixelAcc_,beam_);
    }
private:
    int height_=0;
    std::vector<uint8_t> vram_,rom_;
    uint64_t romIdentity_=0;
    std::array<uint8_t,256> crtc_{},seq_{},gfx_{};
    std::array<uint8_t,32> attr_{};
    std::array<std::array<uint8_t,3>,256> dac_{};
    uint16_t pangola_=0;
    uint8_t crtcIndex_=0,seqIndex_=0,gfxIndex_=0,attrAddress_=0,misc_=0,feature_=0,mask_=0xff,
        readIndex_=0,writeIndex_=0,readComponent_=0,writeComponent_=0,setup_=0;
    bool attrData_=false,dacRead_=false,enabled_=false;
    int64_t pixelAcc_=0;
    uint64_t beam_=0;
    uint8_t crtcData() const;
    void crtcWrite(uint8_t v);
    uint32_t horizontalTotal() const {return (uint32_t(crtc_[0])+5)*8;}
    uint32_t verticalTotal() const {return uint32_t(crtc_[6])|((crtc_[7]&1)<<8)|((crtc_[7]&0x20)<<4);}
};
