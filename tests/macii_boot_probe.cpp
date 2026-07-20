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
int main(int argc, char** argv) {
  long frames = argc>1 ? std::atol(argv[1]) : 30000;
  auto rom=find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
  auto img=find("hdv/GISTPERSO-boot.vhd");
  std::ifstream rin(rom,std::ios::binary);
  std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)),{});
  MacIIMemory mem; mem.loadRom(rd); mem.installTobyVideo();
  Cpu020 cpu(mem,true); mem.setCpu(&cpu); cpu.hardReset();
  mem.attachScsi(img); ensureBootDriverType(mem.scsiDisk().image());
  const int64_t kFrame=800*525;
  for (long f=0;f<frames&&!cpu.isHalted();f++) cpu.runCycles(kFrame);
  TobyVideo* tv=mem.toby();
  std::vector<uint32_t> fb; tv->decode(fb);
  int W=tv->hres(), H=tv->vres();
  auto blackRatio=[&](int x0,int x1,int y0,int y1){
    long black=0; for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++)
      if ((fb[y*W+x]&0xFF)<0x80) black++;
    return double(black)/double(x1-x0)/double(y1-y0);
  };
  double menu=blackRatio(0,W,2,20);
  double desk=blackRatio(W/2,W,40,H-40);
  std::printf("frames=%ld PC=$%08X SCSI=%ld menu=%.2f desk=%.2f %dx%d\n",
    frames, cpu.getPC(), mem.scsi().commands, menu, desk, W, H);
  // write PPM
  std::ofstream out("macii_boot.ppm");
  out << "P6\n" << W << " " << H << "\n255\n";
  for (int i=0;i<W*H;i++) {
    uint32_t p=fb[i];
    out.put(char((p>>16)&0xFF)); out.put(char((p>>8)&0xFF)); out.put(char(p&0xFF));
  }
  std::printf("wrote macii_boot.ppm\n");
}
