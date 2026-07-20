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
int main(int argc, char** argv) {
  bool patch = argc>1 && std::string(argv[1])=="--patch6a";
  auto rom=find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
  auto img=find("hdv/HD20SC.vhd");
  std::ifstream rin(rom,std::ios::binary);
  std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)),{});
  MacIIMemory mem; mem.loadRom(rd); mem.installTobyVideo();
  Cpu020 cpu(mem,true); mem.setCpu(&cpu); cpu.hardReset();
  mem.attachScsi(img);
  if (patch) {
    auto& d=mem.scsiDisk().image();
    int count=(d[0x10]<<8)|d[0x11];
    bool has=false;
    for (int i=0;i<count;i++){int e=0x12+i*8; if(((d[e+6]<<8)|d[e+7])==0x6A) has=true;}
    if (!has && count>=1) {
      int src=0x12,dst=0x12+count*8;
      for(int k=0;k<8;k++) d[dst+k]=d[src+k];
      d[dst+6]=0; d[dst+7]=0x6A;
      d[0x10]=0; d[0x11]=uint8_t(count+1);
    }
  }
  std::printf("patch6a=%d ddCount=%d\n", patch, mem.scsiDisk().image()[0x11]);
  std::map<uint32_t,long> lba;
  mem.scsi().onCommand=[&](const std::vector<uint8_t>& cdb){
    if (cdb.size()>=6 && cdb[0]==0x08) {
      uint32_t a=((cdb[1]&0x1F)<<16)|(cdb[2]<<8)|cdb[3];
      lba[a]++;
      if (mem.scsi().commands<=20)
        std::printf("CMD#%ld LBA=%u n=%u\n", mem.scsi().commands,a,cdb[4]?cdb[4]:256);
    }
  };
  const int64_t kFrame=800*525;
  for (long f=0;f<3000&&!cpu.isHalted();f++) cpu.runCycles(kFrame);
  TobyVideo* tv=mem.toby();
  std::vector<uint32_t> fb; tv->decode(fb);
  int W=tv->hres(),H=tv->vres();
  auto br=[&](int x0,int x1,int y0,int y1){long b=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)if((fb[y*W+x]&0xFF)<0x80)b++;return double(b)/(x1-x0)/(y1-y0);};
  std::printf("cmds=%ld dma=%ld unique=%zu menu=%.2f desk=%.2f PC=$%08X\n",
    mem.scsi().commands,mem.scsi().dmaBytes,lba.size(),br(0,W,2,20),br(W/2,W,40,H-40),cpu.getPC());
  std::vector<std::pair<long,uint32_t>> v;
  for (auto& kv:lba) v.push_back({kv.second,kv.first});
  std::sort(v.rbegin(),v.rend());
  for (int i=0;i<12&&i<(int)v.size();i++) std::printf("  LBA %u x%ld\n",v[i].second,v[i].first);
}
