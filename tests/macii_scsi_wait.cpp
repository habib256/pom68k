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
static void ensureBootDriverType(std::vector<uint8_t>& img) {
  if (img.size()<512||img[0]!='E'||img[1]!='R') return;
  int count=(img[0x10]<<8)|img[0x11];
  for (int i=0;i<count&&0x12+i*8+8<=512;i++) {
    int e=0x12+i*8;
    if (((img[e+6]<<8)|img[e+7])==0x6A) return;
  }
  if (count>=1&&0x12+count*8+8<=512) {
    int src=0x12,dst=0x12+count*8;
    for (int k=0;k<8;k++) img[dst+k]=img[src+k];
    img[dst+6]=0; img[dst+7]=0x6A;
    img[0x10]=uint8_t((count+1)>>8); img[0x11]=uint8_t(count+1);
  }
}
static uint32_t r32(MacIIMemory& m, uint32_t a) {
  return (uint32_t(m.peek8(a))<<24)|(uint32_t(m.peek8(a+1))<<16)|
         (uint32_t(m.peek8(a+2))<<8)|m.peek8(a+3);
}
int main() {
  auto rom=find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
  auto img=find("hdv/GISTPERSO-boot.vhd");
  std::ifstream rin(rom,std::ios::binary);
  std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)),{});
  MacIIMemory mem; mem.loadRom(rd); mem.installTobyVideo();
  Cpu020 cpu(mem,true); mem.setCpu(&cpu); cpu.hardReset();
  mem.attachScsi(img); ensureBootDriverType(mem.scsiDisk().image());
  const int64_t kFrame=800*525;
  bool logged=false;
  for (long f=0;f<8000&&!cpu.isHalted();f++) {
    cpu.runCycles(kFrame);
    uint32_t pc=cpu.getPC();
    if (!logged && (pc==0x40826CC0||pc==0x40826CC6)) {
      uint32_t a3=cpu.getA(3);
      std::printf("stuck PC=$%08X A3=$%08X D1=%08X D5=%08X\n",
        pc,a3,cpu.getD(1),cpu.getD(5));
      std::printf("  $40(A3)=%02X $10(A3)=%02X $30(A3)=%02X $00(A3)=%02X $70(A3)=%02X\n",
        mem.peek8(a3+0x40), mem.peek8(a3+0x10), mem.peek8(a3+0x30),
        mem.peek8(a3), mem.peek8(a3+0x70));
      std::printf("  SCSI cmds=%ld drq=%d irq=%d\n",
        mem.scsi().commands, mem.scsi().drqActive(), mem.scsi().irqAsserted());
      // dump a3 structure first 0x80
      std::printf("  A3[0..7F]:");
      for (int i=0;i<0x80;i++) {
        if (i%16==0) std::printf("\n  %02X:", i);
        std::printf(" %02X", mem.peek8(a3+i));
      }
      std::printf("\n");
      logged=true;
      break;
    }
  }
  if (!logged) std::printf("never hit wait; PC=$%08X SCSI=%ld\n",
    cpu.getPC(), mem.scsi().commands);
}
