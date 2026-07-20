#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <vector>
int main(int argc, char** argv) {
  std::ifstream rin(argv[1], std::ios::binary);
  std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rin)), {});
  MacIIMemory mem;
  mem.loadRom(rom); mem.installTobyVideo();
  Cpu020 cpu(mem, true); mem.setCpu(&cpu); cpu.hardReset();
  mem.attachScsi(argv[2]);
  const int64_t kFrame = 800*525;
  for (long f=0; f<12000 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
  auto p8=[&](uint32_t a){ return mem.peek8(a); };
  auto p16=[&](uint32_t a){ return uint16_t(p8(a)<<8|p8(a+1)); };
  auto p32=[&](uint32_t a){ return uint32_t(p8(a)<<24|p8(a+1)<<16|p8(a+2)<<8|p8(a+3)); };
  uint32_t pc=cpu.getPC();
  std::printf("PC=$%08X SCSI=%ld clk=%lld halted=%d\n",
    pc, mem.scsi().commands, (long long)cpu.getClock(), cpu.isHalted()?1:0);
  for (int i=0;i<8;i++) std::printf("D%d=$%08X ", i, (unsigned)cpu.getD(i));
  std::printf("\n");
  for (int i=0;i<8;i++) std::printf("A%d=$%08X ", i, (unsigned)cpu.getA(i));
  std::printf("\n");
  std::printf("VIA1 IFR/IER=%02X/%02X VIA2=%02X/%02X\n",
    mem.via1().ifrRaw(), mem.via1().ierRaw(),
    mem.via2().ifrRaw(), mem.via2().ierRaw());
  std::printf("bytes@PC:");
  for (int i=0;i<48;i++) std::printf(" %02X", p8(pc+i));
  std::printf("\n");
  std::printf("MemTop=$%08X SysZone=$%08X ApplZone=$%08X CurrentA5=$%08X\n",
    p32(0x108), p32(0x2A6), p32(0x2AA), p32(0x904));
  std::printf("BootDrive=%d HWCfgFlags=$%04X\n", (int16_t)p16(0x210), p16(0xB22));
  std::printf("op=$%04X\n", p16(pc));
  // stack top
  uint32_t sp=cpu.getA(7);
  std::printf("SP=$%08X stack:", sp);
  for (int i=0;i<16;i++) std::printf(" %08X", p32(sp+i*4));
  std::printf("\n");
  return 0;
}
