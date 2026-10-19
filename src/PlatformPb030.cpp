// POM68K — PowerBook Jaws/M50753 product composition (GPLv3).
#include "PlatformCompositionSupport.h"
#include "Pb030Cpu.h"
#include "Pb030Profile.h"
#include "GuiRunnerPb030.h"
namespace {
struct Pb030Machine:MachineHost<Pb030Machine,Pb030Memory,Pb030Cpu,MacAudioHost> {
    using Base=MachineHost<Pb030Machine,Pb030Memory,Pb030Cpu,MacAudioHost>;
    GuiHostServices& services;
    Pb030Machine(Pb030Memory& m,Pb030Cpu& c,MacAudioHost& a,GuiHostServices& s)
        :Base(m,c,a,s.config().diagnostics().keyTrace,s.config().devices().turbo),services(s) {}
    static constexpr bool kStereo=true;
    struct Status {uint32_t pc;long long clock;bool overlay,mmu,held;};
    Status status() const {
        const auto f=stFlags_.load(std::memory_order_relaxed);
        return {stPc_.load(std::memory_order_relaxed),stClock_.load(std::memory_order_relaxed),
            bool(f&1),bool(f&2),bool(f&4)};
    }
    int64_t frameCycles() const {return mem.cpuHz()/60;}
    void emulateQuantum() {runQuantumWithWire(services,mem,cpu,frameCycles());++framesRun_;}
    bool drainAudio() {
        samp_.clear();int16_t l=0,r=0;bool audible=false;
        while(mem.asc().popStereo(l,r)) {
            samp_.push_back(l/32768.f);samp_.push_back(r/32768.f);
            audible=audible || l>300 || l< -300 || r>300 || r< -300;
        }
        return audible;
    }
    void renderFrame(std::vector<uint32_t>& out,int& w,int& h) {
        mem.decode(out);for(auto& p:out) p|=0xff000000;w=640;h=400;
    }
    void publishStatus() {
        stFlags_.store(uint8_t((mem.overlay()?1:0)|((cpu.getTC()&0x80000000)?2:0)|
            (mem.cpuHeld()?4:0)),std::memory_order_relaxed);
    }
};
}

int pom68k::gui::composePb030(PlatformLaunch launch,GuiHostServices& services) {
    const auto* profile=pom68k::machineProfile(launch.selected);
    if(!profile || (profile->platform!=PlatformKind::Jaws && profile->platform!=PlatformKind::Niagra)) return 1;
    const auto board=pb030Profile(profile->snapshot);
    const auto& config=services.config();
    auto& mem=services.own<Pb030Memory>(config.core(),8u<<20,board.cpuHz,profile->platform==PlatformKind::Niagra);
    if(!mem.loadRom(launch.rom) || !mem.selectPmuFirmware(config.core().firmware)) return 1;
    auto& cpu=services.own<Pb030Cpu>(mem,config.jit().resolved,config.core().cpu,board.fpu);
    mem.setCpu(&cpu);
    auto& audio=services.own<MacAudioHost>(config.devices().audio);
    std::printf("Machine: %s (%s, %s + M50753)\n",profile->label,board.fpu?"68030 + 68882":"68030",mem.isNiagra()?"Niagra":"Jaws");
    const Pb030RunnerSpec spec{profile->label,profile->slug,"hdv/System 7.5.5 HD.dsk",
        board.cpuLine,MachineKind::PowerBook,profile->snapshot};
    return runPb030Gui<Pb030Machine>(mem,cpu,audio,spec,
        [&](){mem.rtc().setSeconds(services.hostMacSeconds());},launch.romName,launch.media,services);
}
