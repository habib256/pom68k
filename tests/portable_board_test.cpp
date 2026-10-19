#include "PortableCpu.h"
#include <algorithm>
#include <cstdio>

namespace {
int failures=0;
void check(bool ok,const char* text) {std::printf("%s %s\n",ok?"ok":"FAIL",text);if(!ok) ++failures;}
std::vector<uint8_t> firmware() {
    std::vector<uint8_t> b(M50753::kRomSize,0xea);
    const uint8_t code[]={0xa2,0xbf,0x9a,0x3c,0x72,0xe9,0x3c,0x72,0xe8,
        0x3c,4,0xeb,0x3c,4,0xea,0xe6,0x40,0x80,0xfc};
    std::copy(std::begin(code),std::end(code),b.begin());b[0x17fe]=0;b[0x17ff]=0xe8;return b;
}
std::vector<uint8_t> rom() {
    std::vector<uint8_t> b(PortableMemory::kRomSize,0);
    b[1]=0x20;b[5]=0x90;b[6]=1;b[0x100]=0x60;b[0x101]=0xfe;return b;
}
void bus() {
    auto config=pom68k::defaultCoreConfig();PortableMemory m(config);
    check(m.loadRom(rom()) && !m.loadRom(std::vector<uint8_t>(4)),"ROM size contract");
    check(m.peek8(0x900100)==0x60 && m.overlay(),"ROM debugger peek preserves overlay");
    m.write16(0,0x1234);check(m.read16(0)==0x0020,"low overlay protects ROM from writes");
    check(m.read16(0x9c0100)==0x60fe && !m.overlay(),"high ROM mirror read removes overlay");
    m.write16(0,0x1234);check(m.read16(0)==0x1234 && m.read16(0x01000000)==0x1234,
          "big-endian RAM uses the 68000's 24-bit address bus");
    PortableMemory small(config,4u<<20);
    check(m.read16(0xfc0000)==0 && small.read16(0xfc0000)==0xffff,
          "PDS expansion strap follows the installed RAM size");
    m.write8(0xfa8000,0x81);check(m.peek8(0xfa8000)==0x81 && m.read16(0xfa8000)==0x8100,
          "VRAM occupies its own 32 KB window");
    check(m.read16(0xf70800)==0 && m.read16(0xf70600)==0,"VIA registers have a 512-byte stride");
    m.write8(0xf70400,0x21);m.write8(0xf70000,0x21);
    check(m.pmu().request() && m.swim().iwm().sel(),"VIA PB0/PB5 drive PMREQ and floppy head select");
    m.write8(0xf70000,0);check(!m.pmu().request() && !m.swim().iwm().sel(),"VIA clears request and head select");
    check(m.read16(0xf70000)==0x8484,"VIA word reads replicate one device byte");
    check(m.read8(0xfb0800)==0,"Portable exposes the discrete ASC version");
    m.write8(0xf90001,0x5a);
    check(m.read8(0xf90000)==0x5a,"SCSI writes on the odd byte lane reach the data latch");
    m.write16(0xf90000,0x1234);
    check(m.read16(0xf90000)==0x3400,"SCSI word stores use the lower byte and reads the upper byte");
    m.write8(0xf90281,0x69);
    check(m.read8(0xf90000)==0x69,"NCR register mirror does not accidentally select pseudo-DMA");
    uint32_t span=1;check(!m.codeSpan(0xf70000,span) && !span,"instruction windows exclude I/O");
    bool fault=false;try {m.read8(0xf90260);} catch(const moira::MmuBusError&) {fault=true;}
    check(fault,"SCSI pseudo-DMA without DRQ faults instead of transferring a fake byte");
    m.pmu().loadFirmware(firmware());m.pmu().reset();m.tick(10000);
    std::vector<uint32_t> pixels;m.decode(pixels);
    check(pixels.size()==640*400 && pixels[0]==0 && pixels[1]==0xffffff && pixels[7]==0,
          "LCD renders 640x400 independent monochrome VRAM");
    // Pulse SYS_RST low, then release it. Only reset assertion re-arms ROM.
    m.read8(0x900000);m.consumeReset();m.pmu().mcu().write(0xe8,0x70);
    check(m.overlay() && m.cpuHeld(),"PMU reset assertion rearms the ROM overlay");
    m.pmu().mcu().write(0xe8,0x72);
    check(!m.cpuHeld() && m.consumeReset() && !m.consumeReset(),"PMU reset release is consumed once at a CPU boundary");
}
void cpuAndState() {
    auto config=pom68k::defaultCoreConfig();PortableMemory m(config);
    m.loadRom(rom());PortableCpu cpu(m,jit::defaultResolvedConfig());m.setCpu(&cpu);cpu.hardReset();
    const auto before=cpu.machineClock();cpu.runCycles(1000);
    check(cpu.machineClock()-before==1000 && m.cpuHeld() && !cpu.isHalted(),
          "held CPU advances devices in machine time without executing guest code");
    m.pmu().loadFirmware(firmware());cpu.hardReset();cpu.runCycles(10000);
    check(!m.cpuHeld() && !m.overlay() && cpu.getPC0()==0x900100 && !cpu.isHalted(),
          "PMU releases the real 68000 reset sequence into the high ROM");
    const auto viaBefore=cpu.machineClock();m.write16(0xf70600,0);
    check(cpu.machineClock()-viaBefore==50,"one VIA word store pays one 50-cycle bus wait");
    m.write8(0x1234,0x56);std::vector<uint8_t> state;sav::Writer writer(state);writer(m,cpu);
    cpu.runCycles(1000);std::vector<uint8_t> expected;sav::Writer after(expected);after(m,cpu);
    sav::Reader reader(state.data(),state.size());reader(m,cpu);cpu.runCycles(1000);
    std::vector<uint8_t> actual;sav::Writer replay(actual);replay(m,cpu);
    check(reader.ok() && !reader.remaining() && actual==expected,
          "RAM, CPU, VIA, PMU and clock phases resume byte-identically");
}
}
int main() {bus();cpuAndState();return failures?1:0;}
