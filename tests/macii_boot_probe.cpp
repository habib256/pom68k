#include "MacIIMemory.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
static std::string find(const char* rel) {
  for (const std::string b:{"","../"}) if (std::ifstream(b+rel,std::ios::binary)) return b+rel;
  return {};
}
static long gBusErr=0;
int main() {
  auto rom=find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
  auto img=find("hdv/HD20SC.vhd");
  std::ifstream rin(rom,std::ios::binary);
  std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)),{});
  MacIIMemory mem; mem.loadRom(rd); mem.installTobyVideo();
  Cpu020 cpu(mem,true); mem.setCpu(&cpu); cpu.hardReset();
  mem.attachScsi(img);
  for (int i=1;i<7;i++) mem.scsi().attach(nullptr, i);

  // wrap: count DMA reads per DATA_IN session via onAccess? use onCommand + dma counter
  long prevDma=0; int n96=0;
  mem.scsi().onCommand=[&](const std::vector<uint8_t>& cdb){
    if (cdb.size()<10||cdb[0]!=0x28) return;
    uint32_t a=(uint32_t(cdb[2])<<24)|(uint32_t(cdb[3])<<16)|(uint32_t(cdb[4])<<8)|cdb[5];
    uint32_t n=(uint32_t(cdb[7])<<8)|cdb[8];
    if (a!=96) { prevDma=mem.scsi().dmaBytes; return; }
    if (n96<5) {
      auto be16=[&](uint32_t x){return uint16_t((mem.read8(x)<<8)|mem.read8(x+1));};
      std::printf("LBA96 #%d n=%u dmaDelta=%ld $B24=%u $DA6=%u $C04=%08X $C08=%08X\n",
        n96,n,mem.scsi().dmaBytes-prevDma, be16(0xB24), be16(0xDA6),
        (mem.read8(0xC04)<<24)|(mem.read8(0xC05)<<16)|(mem.read8(0xC06)<<8)|mem.read8(0xC07),
        (mem.read8(0xC08)<<24)|(mem.read8(0xC09)<<16)|(mem.read8(0xC0A)<<8)|mem.read8(0xC0B));
    }
    n96++; prevDma=mem.scsi().dmaBytes;
  };
  const int64_t kFrame=800*525;
  auto be16=[&](uint32_t a){return uint16_t((mem.read8(a)<<8)|mem.read8(a+1));};
  for (long f=0;f<400&&!cpu.isHalted();f++) cpu.runCycles(kFrame);
  std::printf("timings $B24=%u $DA6=%u $D00=%u BootDrive=%d n96=%d cmds=%ld\n",
    be16(0xB24),be16(0xDA6),be16(0xD00),int16_t(be16(0x210)),n96,mem.scsi().commands);
}
