// Portable/PB100 product composition, shared parallel-PMU GUI lifecycle (GPLv3).
#include "PlatformCompositionSupport.h"
#include "PortableCpu.h"
#include "GuiRunnerPb030.h"

namespace {
struct PortableMachine:MachineHost<PortableMachine,PortableMemory,PortableCpu,MacAudioHost> {
    using Base=MachineHost<PortableMachine,PortableMemory,PortableCpu,MacAudioHost>;
    GuiHostServices& services;
    PortableMachine(PortableMemory& m,PortableCpu& c,MacAudioHost& a,GuiHostServices& s)
        :Base(m,c,a,s.config().diagnostics().keyTrace,s.config().devices().turbo),services(s) {}
    static constexpr bool kStereo=false;
    struct Status {uint32_t pc;long long clock;bool overlay,mmu,held;};
    Status status() const {
        const auto f=stFlags_.load(std::memory_order_relaxed);
        return {stPc_.load(std::memory_order_relaxed),stClock_.load(std::memory_order_relaxed),bool(f&1),false,bool(f&4)};
    }
    int64_t frameCycles() const {return PortableMemory::kCpuHz/60;}
    void emulateQuantum() {runQuantumWithWire(services,mem,cpu,frameCycles());++framesRun_;}
    bool drainAudio() {
        samp_.clear();float lo=1,hi=-1;
        while(mem.asc().available()) {const float v=mem.asc().pop()/32768.f;samp_.push_back(v);lo=std::min(lo,v);hi=std::max(hi,v);}
        return !samp_.empty() && hi-lo>=0.02f;
    }
    void renderFrame(std::vector<uint32_t>& out,int& w,int& h) {
        mem.decode(out);for(auto& p:out) p|=0xff000000;w=640;h=400;
    }
    void publishStatus() {stFlags_.store(uint8_t((mem.overlay()?1:0)|(mem.cpuHeld()?4:0)),std::memory_order_relaxed);}
};
}

int pom68k::gui::composePortable(PlatformLaunch launch,GuiHostServices& services) {
    const auto* profile=pom68k::machineProfile(launch.selected);
    if(!profile || profile->platform!=PlatformKind::Portable) return 1;
    const auto& config=services.config();
    auto& mem=services.own<PortableMemory>(config.core());
    if(!mem.loadRom(launch.rom) || !mem.selectPmuFirmware(config.core().firmware)) return 1;
    auto& cpu=services.own<PortableCpu>(mem,config.jit().resolved);mem.setCpu(&cpu);
    auto& audio=services.own<MacAudioHost>(config.devices().audio);
    std::printf("Machine: %s (68000 @ 15.6672 MHz + M50753)\n",profile->label);
    const Pb030RunnerSpec spec{profile->label,profile->slug,"hdv/System 7.1 HD.dsk",
        "68000 @ 15.6672 MHz",MachineKind::Portable,profile->snapshot};
    return runPb030Gui<PortableMachine>(mem,cpu,audio,spec,
        [&](){mem.seedRtc(services.hostMacSeconds());},launch.romName,launch.media,services);
}
