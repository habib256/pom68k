#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
static std::string find(const char* rel) {
  for (const std::string b:{"","../"}) if (std::ifstream(b+rel,std::ios::binary)) return b+rel;
  return {};
}
int main() {
  auto rom=find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
  auto img=find("hdv/HD20SC.vhd");
  std::ifstream rin(rom,std::ios::binary);
  std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)),{});
  MacIIMemory mem; mem.loadRom(rd); mem.installTobyVideo();
  Cpu020 cpu(mem,true); mem.setCpu(&cpu); cpu.hardReset();
  mem.attachScsi(img);
  const int64_t kFrame=800*525;
  auto be16=[&](uint32_t a){return uint16_t((mem.read8(a)<<8)|mem.read8(a+1));};
  long lastOk=0;
  for (long f=0;f<20000&&!cpu.isHalted();f++) {
    try {
      cpu.runCycles(kFrame);
    } catch (const std::exception& e) {
      std::printf("exception f=%ld PC=$%08X what=%s\n", f, cpu.getPC(), e.what());
      return 1;
    }
    lastOk=f;
    if (f%2000==0)
      std::printf("f=%ld PC=$%08X BootDrive=%d cmds=%ld halted=%d\n",
        f,cpu.getPC(),int16_t(be16(0x210)),mem.scsi().commands,cpu.isHalted());
  }
  TobyVideo* tv=mem.toby();
  std::vector<uint32_t> fb; tv->decode(fb);
  int W=tv->hres(),H=tv->vres();
  auto br=[&](int x0,int x1,int y0,int y1){long b=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)if((fb[y*W+x]&0xFF)<0x80)b++;return double(b)/(x1-x0)/(y1-y0);};
  std::printf("DONE f=%ld halted=%d PC=$%08X menu=%.2f desk=%.2f cmds=%ld\n",
    lastOk,cpu.isHalted(),cpu.getPC(),br(0,W,2,20),br(W/2,W,40,H-40),mem.scsi().commands);
}
