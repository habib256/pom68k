// Jaws/Misc GLU bus, pin and clock contracts, without private assets.
#include "Pb030Cpu.h"
#include <cstdio>

namespace {
int failures=0;
void check(bool ok,const char* label) {std::printf("%s %s\n",ok?"ok":"FAIL",label);if(!ok) ++failures;}
std::vector<uint8_t> firmware() {
    std::vector<uint8_t> bytes(M50753::kRomSize,0xea);
    // Put stack below GPIO, release host reset/halt, enable display, loop.
    const uint8_t code[]={0xa2,0xbf,0x9a,0x3c,0x63,0xe9,0x3c,0x63,0xe8,
        0x3c,4,0xeb,0x3c,4,0xea,0xe6,0x40,0x80,0xfc};
    std::copy(std::begin(code),std::end(code),bytes.begin());
    bytes[0x17fe]=0;bytes[0x17ff]=0xe8;return bytes;
}
void testClockBridge() {
    Pb030Pmu one(15667200),split(15667200);
    const auto fw=firmware();one.loadFirmware(fw);split.loadFirmware(fw);
    one.reset();split.reset();one.tick(100001);
    for(int i=0;i<100001;++i) split.tick(1);
    std::vector<uint8_t> a,b;sav::Writer wa(a),wb(b);wa(one);wb(split);
    check(a==b && !one.cpuHeld(),"PMU instruction debt and ADB phase are invariant under host tick partitioning");
    int resets=0;one.onResetRelease=[&](){++resets;};
    sav::Reader reader(a.data(),a.size());reader(one);
    check(reader.ok() && !resets,"restoring PMU output levels does not create a host reset edge");
}
void testBus() {
    const auto core=pom68k::defaultCoreConfig();Pb030Memory mem(core);
    std::vector<uint8_t> rom(Pb030Memory::kRomSize);
    rom[0]=0x12;rom[1]=0x34;mem.loadRom(rom);
    check(mem.peek8(0x40000000)==0x12 && mem.overlay(),"ROM peek does not remove the boot overlay");
    mem.write8(0,0x56);
    check(mem.read16(0)==0x1234,"overlay protects low ROM from writes");
    check(mem.read16(0x4ff00000)==0x1234 && !mem.overlay(),"first ROM mirror read exposes RAM");
    mem.write16(0,0xabcd);check(mem.read16(0)==0xabcd,"RAM word stores use big-endian order");
    check(mem.read8(0x50000200)==0x93 && mem.read8(0x51f00200)==0x93,
          "VIA1 ID appears through Jaws I/O mirrors including bit 24");
    mem.write8(0x50000600,0x20);mem.write8(0x50000200,0x20);
    check(mem.swim().iwm().sel(),"VIA1 PA5 drives floppy head select on Jaws");
    mem.write8(0x50000200,0);
    check(!mem.swim().iwm().sel(),"VIA1 PA5 clear returns the drive to side zero");
    mem.write8(0xfee08000,0x81);
    check(mem.read8(0xfee00000)==0x81 && mem.read8(0xfee08000)==0x81,"DDC VRAM aliases its upper 32 KB");
    check(mem.read8(0x50014800)==0xb0,"sound window exposes the discrete EASC version");
    bool fault=false;try {mem.read8(0x51026000);} catch(const moira::MmuBusError&) {fault=true;}
    check(fault,"foreign decoder probe raises bus error rather than exposing an MSC pseudo-VIA");
    uint32_t length=1;
    check(mem.codeSpan(0x50000000,length)==nullptr && !length,"JIT cannot turn I/O into plain memory");
    mem.pmu().loadFirmware(firmware());mem.pmu().reset();mem.tick(50000);
    check(!mem.cpuHeld(),"synthetic M50753 firmware releases the host through real GPIO");
    std::vector<uint32_t> pixels;mem.decode(pixels);
    check(pixels.size()==640*400 && pixels[0]==0 && pixels[1]==0xffffff && pixels[7]==0,
          "DDC renderer maps the mirrored logical bytes to 640x400 monochrome pixels");
    mem.pseudoVia().write(0,0x04);
    check((mem.pseudoVia().read(0)&4)!=0,"pseudo-VIA port B reads back the host's PMREQ bit");
    mem.pseudoVia().write(0,0);
    check(!(mem.pseudoVia().read(0)&4),"PMREQ deassert/assert writes reach the independent MCU");
    mem.consumeReset();mem.read8(0x40000000);const auto pmuClock=mem.pmu().mcu().cpu().cycleCount();
    mem.write8(0x500a0000,0);
    check(!mem.overlay() && mem.consumeReset() && !mem.overlay() && !mem.consumeReset() &&
          mem.pmu().mcu().cpu().cycleCount()==pmuClock,
          "Jaws wake reset preserves RAM vectors and defers CPU reset without resetting its PMU");
}
void testPseudoVia() {
    PseudoVia pv(PseudoVia::Flavour::Pb030);pv.reset();
    int a=0,b=0;pv.onPortA=[&](uint8_t v){a=v;};pv.onPortB=[&](uint8_t v){b=v;};
    pv.write(0x200,0x12);pv.write(0,0x34);
    check(a==0x12 && b==0x34,"Misc GLU uses 6522-spaced port registers");
    pv.write(14*512,0xff);
    check(pv.read(14*512)==0x7f,"Misc GLU IER has no IIci FF-to-1F quirk");
    pv.write(13*512,0x7f);pv.ascIrq(true);
    check(pv.irqAsserted() && (pv.read(13*512)&0x90)==0x90,"EASC rising edge raises the enabled IFR");
    pv.write(13*512,0x10);
    check(!pv.irqAsserted() && !(pv.read(13*512)&0x10),"Misc GLU acknowledges EASC even while its line remains high");
    pv.ascIrq(true);check(!(pv.read(13*512)&0x10),"held EASC line does not invent a second edge");
    pv.ascIrq(false);pv.ascIrq(true);check(pv.irqAsserted(),"new EASC edge retriggers after acknowledgement");
}
void testCpuHoldAndClock() {
    auto core=pom68k::defaultCoreConfig();Pb030Memory mem(core);
    Pb030Cpu cpu(mem,jit::defaultResolvedConfig(),core.cpu);mem.setCpu(&cpu);
    cpu.hardReset();const auto before=cpu.machineClock();cpu.runCycles(1000);
    check(mem.cpuHeld() && cpu.machineClock()-before==1000 && !cpu.isHalted(),
          "absent PMU firmware holds execution while machine time continues");
    const auto nominal=cpu.machineClock();mem.internalDrive().setMotor(true);cpu.stall(100);
    check(cpu.machineClock()-nominal==100,"floppy gate enters unboosted operation without a clock jump");
    const auto streaming=cpu.machineClock();mem.internalDrive().setMotor(false);cpu.stall(100);
    check(cpu.machineClock()-streaming==100,"floppy gate restores nominal throughput without a clock jump");
}
}
int main() {testClockBridge();testBus();testPseudoVia();testCpuHoldAndClock();return failures?1:0;}
