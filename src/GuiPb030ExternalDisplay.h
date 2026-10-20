// External PowerBook display controls and startup; GUI reads published state.
#pragma once
#include "imgui.h"
#include <cstdio>

namespace pom68k::gui {
template<class Mem,class Cpu,class Services>
bool preparePb030ExternalDisplay(Mem& mem,Cpu& cpu,Services& services) {
    if constexpr(requires {mem.prepareExternalPower();}) {
        if(!mem.isNiagra()) return true;
        const auto width=services.config().devices().monitorWidth.value_or(0);
        if(width!=0 && width!=640) {
            std::fprintf(stderr,"PowerBook external monitor: choose 640 (13-inch RGB) or 0 (disconnected).\n");
            return false;
        }
        mem.setMonitorSense(width==640?6:7);
        if(mem.externalMonitor()) {mem.prepareExternalPower();cpu.hardReset();}
    }
    return true;
}

template<class Machine,class Services>
void bindPb030ExternalDisplay(Machine& machine,Services& services) {
    if constexpr(requires {machine.status().external;}) {
        if(machine.mem.isNiagra()) services.bindMonitorCapture([&machine] {
            return machine.status().external?640:0;
        });
    }
}

template<class Machine>
void drawPb030ExternalDisplay(Machine& machine) {
    if constexpr(requires {machine.status().external;}) {
        if(!machine.mem.isNiagra()) return;
        const bool connected=machine.status().external;
        ImGui::TextUnformatted("External monitor:");ImGui::SameLine();
        if(ImGui::RadioButton("Disconnected",!connected) && connected)
            machine.push({Machine::Cmd::Sense,7});
        ImGui::SameLine();
        if(ImGui::RadioButton("13-inch RGB (640x480)",connected) && !connected)
            machine.push({Machine::Cmd::Sense,6});
        ImGui::TextDisabled("Changing the monitor restarts the Mac.");
    }
}
} // namespace pom68k::gui
