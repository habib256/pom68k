// Slot-E register, raster, capacity and snapshot contracts without Apple assets.
#include "Pb030Memory.h"
#include <cstdio>

namespace {
int failures=0;
void check(bool ok,const char* label) {std::printf("%s %s\n",ok?"ok":"FAIL",label);failures+=!ok;}
std::vector<uint8_t> save(auto& object) {std::vector<uint8_t> out;sav::Writer ar(out);ar(object);return out;}
bool load(auto& object,const std::vector<uint8_t>& bytes) {sav::Reader ar(bytes.data(),bytes.size());ar(object);return ar.ok() && !ar.remaining();}
}
int main() {
    Pb030ExternalVideo video;
    check(video.read(4)==0x70,"no monitor is the startup default");
    video.setMonitor(true);check(video.read(4)==0x60,"13-inch RGB sense code is 6");
    video.write(4,4);check(video.read(4)==0x20,"sense outputs drive individual pins low");video.write(4,0);
    std::vector<uint8_t> vram(0x80000,0);
    video.write(0x10,0x43);video.memoryWrite(vram,0,0x42);
    check(video.memoryRead(vram,0)==0xff && vram[0]==0,"PrimaryInit cannot find an absent second VRAM bank");
    video.write(0x10,0xfd);video.memoryWrite(vram,0,0x42);
    check(video.memoryRead(vram,0)==0x42,"the 512 KB bank is available after the ROM clears the capacity bit");
    auto color=[&](uint8_t pen,uint8_t r,uint8_t g,uint8_t b) {
        video.dacWrite(0,pen);video.dacWrite(1,r);video.dacWrite(1,g);video.dacWrite(1,b);
    };
    color(0,0x12,0x34,0x56);color(1,0x87,0x65,0x43);
    vram.assign(vram.size(),0);vram[1]=1;vram[640]=1;video.write(8,3);
    std::vector<uint32_t> pixels;video.decode(vram,pixels);
    check(pixels.size()==640*480 && pixels[0]==0x123456 && pixels[1]==0x876543 &&
        pixels[639]==0x123456 && pixels[640]==0x876543,"8-bit scanout uses the DAC and 640-byte rows");
    color(0x7f,0,0,0);color(0xff,255,255,255);video.write(8,0);vram[0]=0x80;video.decode(vram,pixels);
    check(pixels[0]==0xffffff && pixels[1]==0,"one-bit packing selects the high DAC indices");
    video.write(8,4);video.decode(vram,pixels);
    check(std::all_of(pixels.begin(),pixels.end(),[](auto p){return p==0;}),"the 512 KB output does not claim a 16-bit mode");
    video.reset();video.slotWrite(0x12,0xc0);video.slotWrite(0x13,0x82);
    const int toBlank=int((25000000LL*864*480+30240000-1)/30240000);
    video.tick(toBlank,25000000);
    check(video.blank() && video.irq() && video.slotRead(2)==0x3f && video.slotRead(3)==0x82,
        "raster VBL reaches the enabled slot-E interrupt");
    video.write(0x14,0);check(!video.irq() && video.blank(),"IRQ acknowledgement preserves the physical blanking phase");
    auto state=save(video);video.tick(123456,25000000);auto later=save(video);
    Pb030ExternalVideo fresh;
    check(load(fresh,state),"a fresh controller accepts a snapshot");fresh.tick(123456,25000000);
    check(save(fresh)==later,"restored raster phase and IRQ continue exactly");
    Pb030ExternalVideo split;load(split,state);for(int i=0;i<123456;++i) split.tick(1,25000000);
    check(save(split)==later,"raster timing is independent of host tick partitioning");
    fresh.dacWrite(0,255);fresh.dacWrite(1,11);state=save(fresh);
    fresh.dacWrite(1,22);fresh.dacWrite(1,33);later=save(fresh);
    load(split,state);split.dacWrite(1,22);split.dacWrite(1,33);
    check(save(split)==later,"a snapshot resumes the in-flight DAC component and index wrap");

    const auto config=pom68k::defaultCoreConfig();Pb030Memory mem(config,8u<<20,25000000,true);
    mem.read8(0x40000000);mem.write8(0x1234,0x56);mem.setMonitorSense(6);
    mem.write8(0xfeefe010,0x43);mem.write8(0xfe100000,0x99);
    check(mem.read8(0xfe0fe010)==0x43 && mem.read8(0xfe100000)==0xff,
        "the ROM's 32-bit control alias governs the real VRAM probe");
    mem.write8(0xfe0fe010,0xfd);mem.write8(0xfe100000,0x99);
    check(mem.read8(0xfef00000)==0x99,"the slot-E framebuffer alias reads the same physical bank");
    auto extension=save(mem.externalVideo());state=save(mem);
    state.resize(state.size()-extension.size());mem.write8(0x1234,0);
    check(load(mem,state) && !mem.externalMonitor() && mem.peek8(0x1234)==0x56,
        "older Niagra snapshots without the optional suffix restore with no monitor");
    mem.setMonitorSense(6);mem.write8(0xfeefe010,0xfd);mem.write8(0xfeefe008,3);
    mem.decodeDisplay(pixels);
    check(pixels.size()==640*880 && mem.displayHeight()==880,"published scanout stacks RGB below the LCD like the guest desktop");
    Pb030Memory colorMem(config,8u<<20,33000000,true,480);colorMem.setMonitorSense(6);
    colorMem.decodeDisplay(pixels);
    check(colorMem.displayWidth()==1280 && colorMem.displayHeight()==480 && pixels.size()==1280*480,
        "a 480-line LCD places the equally tall RGB screen to its right");
    Pb030Memory jaws(config);jaws.setMonitorSense(6);
    check(!jaws.externalMonitor() && jaws.displayHeight()==400,"Jaws does not acquire an unsupported video connector");
    return failures?1:0;
}
