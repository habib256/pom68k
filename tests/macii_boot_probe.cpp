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
  for (long f=0;f<800&&!cpu.isHalted();f++) {
    cpu.runCycles(kFrame);
    if (f%50==0 || (f>350 && f<550 && f%10==0)) {
      uint8_t r640=mem.read8(0x640000);
      uint8_t r100=mem.read8(0x10000);
      std::printf("f=%ld ov=%d hmmu=%d PC=$%08X r64=%02X r10=%02X cmds=%ld VIA1PA=%02X\n",
        f, mem.overlay(), mem.hmmu24(), cpu.getPC(), r640, r100, mem.scsi().commands,
        mem.via1().portA());
    }
  }
}
