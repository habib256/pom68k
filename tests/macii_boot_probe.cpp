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
  int lastMode=-1;
  for (long f=0;f<2000&&!cpu.isHalted();f++) {
    cpu.runCycles(kFrame);
    TobyVideo* tv=mem.toby();
    if (tv->mode()!=lastMode || f%200==0) {
      lastMode=tv->mode();
      std::printf("f=%ld mode=%d %dx%d cmds=%ld PC=$%08X SR=$%04X\n",
        f,tv->mode(),tv->hres(),tv->vres(),mem.scsi().commands,cpu.getPC(),cpu.getSR());
    }
  }
  uint32_t pc=cpu.getPC();
  std::printf("FINAL PC=$%08X words:", pc);
  for(int i=0;i<8;i++){
    uint16_t w=(mem.read8(pc+i*2)<<8)|mem.read8(pc+i*2+1);
    std::printf(" %04X", w);
  }
  std::printf("\n");
}
