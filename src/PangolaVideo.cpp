// POM68K — WD90C26 indexed registers and Pangola packed pixel bridge (GPLv3).
#include "PangolaVideo.h"
#include <algorithm>

bool PangolaVideo::loadRom(const std::vector<uint8_t>& bytes) {
    if(bytes.size()!=0x8000) return false;
    rom_=bytes;romIdentity_=14695981039346656037ull;
    for(auto b:rom_) {romIdentity_^=b;romIdentity_*=1099511628211ull;}
    return true;
}
void PangolaVideo::reset() {
    crtc_.fill(0);seq_.fill(0);gfx_.fill(0);attr_.fill(0);dac_.fill({0,0,0});
    pangola_=0;crtcIndex_=seqIndex_=gfxIndex_=attrAddress_=misc_=feature_=0;
    attrData_=dacRead_=false;readIndex_=writeIndex_=readComponent_=writeComponent_=setup_=0;
    mask_=0xff;enabled_=false;pixelAcc_=0;beam_=0;
}
uint8_t PangolaVideo::memoryRead(uint32_t a) const {
    const unsigned shift=(pangola_>>5)&3,bits=8>>shift,mask=(1u<<bits)-1;
    uint8_t value=0;
    for(unsigned p=0;p<(1u<<shift);++p)
        value=uint8_t((unsigned(value)<<bits)|(vram_[((a<<shift)+p)&0x7ffff]&mask));
    return value;
}
void PangolaVideo::memoryWrite(uint32_t a,uint8_t v) {
    const unsigned shift=(pangola_>>5)&3,bits=8>>shift,mask=(1u<<bits)-1;
    for(unsigned p=0;p<(1u<<shift);++p)
        vram_[((a<<shift)+p)&0x7ffff]=uint8_t((v>>(8-bits*(p+1)))&mask);
}
uint8_t PangolaVideo::crtcData() const {
    if(crtcIndex_>=0x2a && crtcIndex_<=0x30 && (crtc_[0x29]&0x88)!=0x80) return 0xff;
    return crtc_[crtcIndex_];
}
void PangolaVideo::crtcWrite(uint8_t v) {
    if(crtcIndex_>=0x2a && crtcIndex_<=0x30 && (crtc_[0x29]&7)!=5) return;
    if(crtcIndex_<7 && (crtc_[0x11]&0x80)) return;
    crtc_[crtcIndex_]=v;
}
void PangolaVideo::modeSetup(uint8_t v) {
    setup_=v;
    // VGA setup latches the board straps: CNF15 separates 165c and 180c.
    if(v&0x10) crtc_[0x2a]=uint8_t((height_==400?0x80:0)|0x70);
    enabled_=(v&0x10)!=0;
}
uint8_t PangolaVideo::ioRead(uint16_t p) {
    uint8_t v=0xff;
    const bool mono=p>=0x3b0 && p<=0x3bf,color=p>=0x3d0 && p<=0x3df;
    if((mono && (misc_&1)) || (color && !(misc_&1))) return 0xff;
    switch(p) {
    case 0x3b4:case 0x3d4:v=crtcIndex_;break;
    case 0x3b5:case 0x3d5:v=crtcData();break;
    case 0x3ba:case 0x3da: {
        attrData_=false;
        const auto ht=horizontalTotal();const auto line=beam_/ht;
        const auto vd=uint32_t(crtc_[0x12])|((crtc_[7]&2)<<7)|((crtc_[7]&0x40)<<3);
        const bool vb=line>vd;
        v=uint8_t((vb || beam_%ht>=(unsigned(crtc_[1])+1)*8?1:0)|(vb?8:0));break;
    }
    case 0x3c0:v=attrAddress_;break;
    case 0x3c1:v=attr_[attrAddress_&31];break;
    case 0x3c2:v=uint8_t(0x60|(((crtc_[0x2a]>>(4+((3-(misc_>>2))&3)))&1)<<4));break;
    case 0x3c3:v=uint8_t(enabled_);break;
    case 0x3c4:v=seqIndex_;break;
    case 0x3c5:v=seq_[(seq_[6]&0x58)==0x48?seqIndex_:seqIndex_&7];break;
    case 0x3c6:v=mask_;break;
    case 0x3c7:v=dacRead_?3:0;break;
    case 0x3c8:v=writeIndex_;break;
    case 0x3c9:v=dac_[readIndex_][readComponent_];if(++readComponent_==3) {readComponent_=0;++readIndex_;}break;
    case 0x3ca:v=feature_;break;
    case 0x3cc:v=misc_;break;
    case 0x3ce:v=gfxIndex_;break;
    case 0x3cf:v=gfxIndex_==0x0b?uint8_t(0xc0|(gfx_[0x0b]&0x3f)):gfx_[gfxIndex_];break;
    default:break;
    }
    if(onIo) onIo(p,false,v);
    return v;
}
void PangolaVideo::ioWrite(uint16_t p,uint8_t v) {
    if(onIo) onIo(p,true,v);
    const bool mono=p>=0x3b0 && p<=0x3bf,color=p>=0x3d0 && p<=0x3df;
    if((mono && (misc_&1)) || (color && !(misc_&1))) return;
    switch(p) {
    case 0x3b4:case 0x3d4:crtcIndex_=v;break;
    case 0x3b5:case 0x3d5:crtcWrite(v);break;
    case 0x3ba:case 0x3da:feature_=v;break;
    case 0x3c0:if(attrData_) attr_[attrAddress_&31]=v;else attrAddress_=v;attrData_=!attrData_;break;
    case 0x3c2:misc_=v;break;
    case 0x3c3:enabled_=(v&1)!=0;break;
    case 0x3c4:seqIndex_=v;break;
    case 0x3c5:seq_[(seq_[6]&0x58)==0x48?seqIndex_:seqIndex_&7]=v;break;
    case 0x3c6:mask_=v;break;
    case 0x3c7:readIndex_=v;readComponent_=0;dacRead_=true;break;
    case 0x3c8:writeIndex_=v;writeComponent_=0;dacRead_=false;break;
    case 0x3c9:dac_[writeIndex_][writeComponent_]=v;if(++writeComponent_==3) {writeComponent_=0;++writeIndex_;}break;
    case 0x3ce:gfxIndex_=v;break;
    case 0x3cf:if(gfxIndex_<9 || gfxIndex_>14 || (gfx_[15]&7)==5) gfx_[gfxIndex_]=v;break;
    default:break;
    }
}
void PangolaVideo::tick(int cycles,int64_t cpuHz) {
    pixelAcc_+=int64_t(cycles)*((misc_&0x0c)==4?28322000:25175000);
    const auto clocks=pixelAcc_/cpuHz;pixelAcc_%=cpuHz;
    const auto total=uint64_t(horizontalTotal())*(verticalTotal()+2);
    beam_=(beam_+uint64_t(clocks))%total;
}
void PangolaVideo::decode(bool blank,std::vector<uint32_t>& out) const {
    out.assign(size_t(640*height_),0);
    if(blank || (seq_[1]&0x20) || !(crtc_[0x17]&0x80)) return;
    const unsigned pitch=unsigned(crtc_[0x13])<<((crtc_[0x14]&0x40)?3:(crtc_[0x17]&0x40)?1:2);
    const unsigned start=unsigned((crtc_[0x0c]<<8)|crtc_[0x0d])<<((seq_[4]&8)?0:2);
    for(int y=0;y<height_;++y) for(unsigned x=0;x<640;++x) {
        const auto& rgb=dac_[vram_[(start+unsigned(y)*pitch+x)&0x7ffff]&mask_];
        auto expand=[](uint8_t c){c&=63;return uint32_t((c<<2)|(c>>4));};
        out[size_t(y)*640+x]=(expand(rgb[0])<<16)|(expand(rgb[1])<<8)|expand(rgb[2]);
    }
}
