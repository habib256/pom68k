#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <map>
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
static uint16_t r16(MacIIMemory& m,uint32_t a){return uint16_t((m.peek8(a)<<8)|m.peek8(a+1));}
static uint32_t r32(MacIIMemory& m,uint32_t a){
  return (uint32_t(m.peek8(a))<<24)|(uint32_t(m.peek8(a+1))<<16)|(uint32_t(m.peek8(a+2))<<8)|m.peek8(a+3);}
int main(int argc, char** argv) {
  const char* disk = argc>1?argv[1]:"hdv/HD20SC.vhd";
  long frames = argc>2?std::atol(argv[2]):8000;
  auto rom=find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
  auto img=find(disk);
  std::ifstream rin(rom,std::ios::binary);
  std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)),{});
  MacIIMemory mem; mem.loadRom(rd); mem.installTobyVideo();
  Cpu020 cpu(mem,true); mem.setCpu(&cpu); cpu.hardReset();
  mem.attachScsi(img); ensureBootDriverType(mem.scsiDisk().image());

  std::map<uint32_t,long> lbaHits;
  std::map<uint8_t,long> cmdHits;
  long logged=0;
  mem.scsi().onCommand = [&](const std::vector<uint8_t>& cdb) {
    if (cdb.empty()) return;
    cmdHits[cdb[0]]++;
    uint32_t lba=0; int n=1;
    if (cdb[0]==0x08 && cdb.size()>=6) { // READ(6)
      lba=((cdb[1]&0x1F)<<16)|(cdb[2]<<8)|cdb[3];
      n=cdb[4]?cdb[4]:256;
      lbaHits[lba]++;
    } else if (cdb[0]==0x28 && cdb.size()>=10) {
      lba=(cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
      n=(cdb[7]<<8)|cdb[8];
      lbaHits[lba]++;
    }
    if (logged++ < 20)
      std::printf("CMD#%ld $%02X LBA=%u n=%d PC=$%08X\n",
        mem.scsi().commands, cdb[0], lba, n, cpu.getPC());
  };

  const int64_t kFrame=800*525;
  for (long f=0;f<frames&&!cpu.isHalted();f++) cpu.runCycles(kFrame);

  std::printf("\nend PC=$%08X SCSI=%ld dma=%ld $DA6=%04X BootDrive=%04X\n",
    cpu.getPC(), mem.scsi().commands, mem.scsi().dmaBytes, r16(mem,0xDA6), r16(mem,0x210));
  std::printf("MemTop=$%08X BufPtr=$%08X SysZone=$%08X\n",
    r32(mem,0x108), r32(mem,0x10C), r32(mem,0x2A6));
  std::printf("cmds:");
  for (auto& kv: cmdHits) std::printf(" $%02X=%ld", kv.first, kv.second);
  std::printf("\n");
  // top LBAs
  std::vector<std::pair<long,uint32_t>> v;
  for (auto& kv: lbaHits) v.push_back({kv.second, kv.first});
  std::sort(v.rbegin(), v.rend());
  std::printf("top LBAs:\n");
  for (int i=0;i<15&&i<(int)v.size();i++)
    std::printf("  LBA %6u x%ld\n", v[i].second, v[i].first);
  std::printf("unique LBAs=%zu\n", lbaHits.size());
}
