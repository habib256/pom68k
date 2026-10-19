// POM68K — Niagra/Ponti and GSC panel 5 (GPLv3).
// Register layout and grayscale conversion: MAME macpwrbk030.cpp / gsc.cpp,
// R. Belmont (BSD-3-Clause); attribution in docs/PB030_BRINGUP.md.
#pragma once
#include "SaveState.h"
#include <array>
#include <vector>
#include <cstdint>

class Pb030Niagra {
public:
    void reset() {gsc_.fill(0);ponti_.fill(0);}
    uint8_t gscRead(uint32_t a) const {return a==1?5:gsc_[a&31];}
    void gscWrite(uint32_t a,uint8_t v) {gsc_[a&31]=v;}
    uint8_t controlRead(uint32_t a,int64_t hz) const {
        if(a&3) return 0;
        switch((a-0x80000)>>12) {
        case 0x16:return ponti_[(a>>10)&3];
        case 0x34:return 0xff; // FPU access-detect register, not FPU presence
        case 0x36:return hz==25000000?2:hz==33000000?3:0;
        default:return 0;
        }
    }
    void controlWrite(uint32_t a,uint8_t v) {
        if((a&3) || ((a-0x80000)>>12)!=0x16) return;
        auto& latch=ponti_[(a>>10)&3];
        if(((a>>10)&3)==1) {
            if((v&4) && !(latch&4)) latch&=~8;
            latch=uint8_t((latch&8)|(v&~8));
        } else latch=v;
    }
    void soundWritten() {ponti_[1]|=8;}
    void decode(const std::vector<uint8_t>& vram,bool blank,std::vector<uint32_t>& out) const {
        out.resize(640*400);
        const unsigned mode=gsc_[4]&3;
        for(unsigned p=0;p<640*400;++p) {
            unsigned pen=15;
            if(!blank && (gsc_[4]&0x20)) {
                if(mode==0) pen=(vram[p>>3]>>(7-(p&7)))&1?15:0;
                else if(mode==1) pen=((vram[p>>2]>>(6-2*(p&3)))&3)<<2;
                else if(mode==2) pen=(vram[(p>>1)&0x1ffff]>>(p&1?0:4))&15;
            }
            const uint32_t gray=((15-pen)<<4)|15;
            out[p]=gray*0x010101;
        }
    }
    template<class Ar> void visit(Ar& ar) {ar(gsc_,ponti_);}
private:
    std::array<uint8_t,32> gsc_{};
    std::array<uint8_t,4> ponti_{};
};
