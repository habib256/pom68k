// The real host queue applies monitor changes; session capture follows publication.
#pragma once
#include "GuiPb030ExternalDisplay.h"
#include "Pb030Cpu.h"
#include "GuiSessionMenu.h"
#include "MachineHost.h"

namespace pb030externalgui {
struct Audio {
    bool started() const {return false;}
    size_t buffered() const {return 0;} size_t targetBuffered() const {return 0;}
    void pushRaw(std::vector<float>&,int) {} void pushFrame(std::vector<float>&,int) {}
};
struct Machine:MachineHost<Machine,Pb030Memory,Pb030Cpu,Audio> {
    using Base=MachineHost<Machine,Pb030Memory,Pb030Cpu,Audio>;
    using Base::Base;
    static constexpr bool kStereo=false;
    std::atomic<bool> connected{false};
    struct Status {bool external;};
    Status status() const {return {connected.load()};}
    int64_t frameCycles() const {return 1000;}
    void emulateQuantum() {++framesRun_;}
    bool drainAudio() {return false;}
    void renderFrame(std::vector<uint32_t>& out,int& w,int& h) {mem.decodeDisplay(out);w=mem.displayWidth();h=mem.displayHeight();}
    void publishStatus() {connected.store(mem.externalMonitor());}
};
inline bool check(headless::Context& ui) {
    using namespace pom68k::gui;
    using namespace pom68k::app;
    const auto core=pom68k::defaultCoreConfig();Pb030Memory mem(core,8u<<20,25000000,true);
    Pb030Cpu cpu(mem,jit::defaultResolvedConfig(),core.cpu,false);mem.setCpu(&cpu);
    Audio audio;Machine machine(mem,cpu,audio);
    GuiSessionState state;char name[]="pom68k";char* argv[]={name};
    const auto config=RuntimeConfig::parse(1,argv,pom68k::StartupSnapshot{{{"POM68K_MONITOR","640"}}});
    bindSessionFile(state,config,"/tmp");state.sessionFile.profile=pom68k::SnapMachine::Pb160;
    struct Services {
        const RuntimeConfig& value;GuiSessionFileState& file;
        const RuntimeConfig& config() const {return value;}
        void bindMonitorCapture(std::function<int()> get) {file.monitorWidth=std::move(get);}
    } services{config,state.sessionFile};
    bool ok=preparePb030ExternalDisplay(mem,cpu,services) && mem.externalMonitor();
    machine.publish(true);bindPb030ExternalDisplay(machine,services);
    auto captured=[&](){for(const auto& entry:state.sessionFile.capture()) if(entry.key=="monitor") return entry.value;return std::string();};
    auto draw=[&](){ImGui::Begin("PowerBook external display test");drawPb030ExternalDisplay(machine);ImGui::End();};
    ui.frame(draw);ui.frame(draw);
    std::printf("external GUI initial: connected=%d captured=%s ready=%d\n",mem.externalMonitor(),captured().c_str(),ok);
    ok=ui.click("Disconnected",draw) && mem.externalMonitor() && captured()=="640" && ok;
    machine.stepTick();machine.publish(true);
    std::printf("external GUI disconnected: connected=%d captured=%s queued=%d\n",mem.externalMonitor(),captured().c_str(),ok);
    ok=!mem.externalMonitor() && captured()=="0" && ok;
    ui.frame(draw);ok=ui.click("13-inch RGB (640x480)",draw) && !mem.externalMonitor() && ok;
    machine.stepTick();machine.publish(true);
    std::printf("external GUI connected: connected=%d captured=%s queued=%d\n",mem.externalMonitor(),captured().c_str(),ok);
    ok=mem.externalMonitor() && captured()=="640" && ok;
    std::vector<uint32_t> pixels;int w=0,h=0;machine.renderFrame(pixels,w,h);
    ok=w==640 && h==880 && pixels.size()==640*880 && ok;
    machine.stop();state.sessionFile.monitorWidth={};
    return ok;
}
}
