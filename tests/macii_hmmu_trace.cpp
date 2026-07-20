#include "MacIIMemory.h"
#include "Cpu020.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>
static std::string find(const char* rel) {
  for (const std::string b : {"", "../"})
    if (std::ifstream(b + rel, std::ios::binary)) return b + rel;
  return {};
}
int main() {
  auto rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
  std::ifstream rin(rom, std::ios::binary);
  std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)), {});
  MacIIMemory mem;
  mem.loadRom(rd);
  mem.installTobyVideo();
  Cpu020 cpu(mem, true);
  mem.setCpu(&cpu);
  cpu.hardReset();
  std::printf("reset PC=$%08X\n", cpu.getPC());
  bool lastHmmu = false;
  int first400 = 0;
  std::map<uint32_t, long> hits;
  for (int i = 0; i < 3'000'000 && !cpu.isHalted(); i++) {
    bool h = mem.hmmu24();
    uint32_t pc = cpu.getPC();
    if (h != lastHmmu) {
      std::printf("[%lld] HMMU %s PC=$%08X\n",
                  (long long)cpu.getClock(), h ? "ON" : "OFF", pc);
      lastHmmu = h;
    }
    if ((pc & 0xFF0F0000u) == 0x40000000u && first400++ < 8)
      std::printf("[%lld] PC in $4000xxxx form $%08X hmmu=%d\n",
                  (long long)cpu.getClock(), pc, h);
    if (h) hits[pc]++;
    cpu.execute();
  }
  std::printf("done PC=$%08X hmmu=%d\n", cpu.getPC(), mem.hmmu24());
  std::vector<std::pair<long, uint32_t>> v;
  for (auto& kv : hits) v.push_back({kv.second, kv.first});
  std::sort(v.rbegin(), v.rend());
  std::printf("top PCs while HMMU on:\n");
  for (int i = 0; i < 15 && i < (int)v.size(); i++)
    std::printf("  %8ld  $%08X\n", v[i].first, v[i].second);
}
