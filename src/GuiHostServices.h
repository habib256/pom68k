// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Process/host boundary for one GUI session: network wiring, host audio,
// relaunch and qualification diagnostics. Windowing, menus and rendering live
// in GuiShell; concrete machine construction lives in PlatformComposers.

#pragma once

#include "FirmwareManifest.h"
#include "GuiSessionObjects.h"
#include "GuiSessionState.h"
#include "LleSession.h"
#include "MacAudioHost.h"
#include "MachineFactory.h"
#include "RuntimeConfig.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace pom68k::gui {

class GuiShell;

class GuiHostServices {
public:
    GuiHostServices(GuiSessionState& state, GuiSessionObjects& objects,
                    GuiShell& shell, const app::RuntimeConfig& config);
    ~GuiHostServices();

    GuiHostServices(const GuiHostServices&) = delete;
    GuiHostServices& operator=(const GuiHostServices&) = delete;

    const app::RuntimeConfig& config() const noexcept { return config_; }
    GuiShell& shell() const noexcept { return shell_; }

    template <class T, class... Args>
    T& own(Args&&... args) {
        return objects_.make<T>(std::forward<Args>(args)...);
    }

    std::string locate(const std::string& relative) const {
        return app::MachineFactory::findPath(relative);
    }
    std::uint32_t hostMacSeconds() const;

    template <class Mem>
    void wireNetwork(Mem& mem) {
        const int byteCycles = int(mem.cpuHz() / 28800);
        const bool cable = state_.network.ltoUdpEnabled &&
                           state_.network.ltoudp.start();
        const bool hub = state_.network.appleTalkEnabled;
        if (!cable && !hub) {
            mem.scc().setByteCycles(byteCycles);
            return;
        }

        const std::int64_t hubHz = std::int64_t(byteCycles) * 28800;
        mem.scc().setByteCycles(byteCycles);
        if (hub && !cable && state_.network.appleTalkWireBoost > 1) {
            mem.scc().setWirePace(
                std::max(byteCycles / state_.network.appleTalkWireBoost, 64));
            mem.scc().setLosslessRx(true);
        }
        if (hub) {
            configureAppleTalk();
            state_.network.atalk.attach(
                mem, hubHz, cable ? &state_.network.ltoudp : nullptr);
        }
        mem.scc().onTxFrame =
            [this, &mem, cable, hub](int channel, const std::uint8_t* data,
                                     std::size_t size) {
                if (channel != 0) return;
                if (size == 3 && data[2] == 0x84) {
                    if (data[0] != 0xFF) {
                        const std::uint8_t cts[3] = {
                            data[1], data[0], 0x85};
                        mem.scc().injectRxFrame(0, cts, 3, true);
                    }
                    return;
                }
                if (size == 3 && data[2] == 0x85) return;
                if (hub) state_.network.atalk.onGuestFrame(data, size);
                if (cable) state_.network.ltoudp.send(data, size);
            };
    }

    template <class Mem>
    void pollNetwork(Mem& mem) {
        if (!state_.network.ltoudp.active()) return;
        state_.network.ltoudp.poll([this, &mem](const std::uint8_t* data,
                                       std::size_t size) {
            mem.scc().injectRxFrame(0, data, size);
            if (state_.network.appleTalkEnabled)
                state_.network.atalk.onCableFrame(data, size);
        });
    }

    template <class Mem, class Cpu, class OnSlice>
    void runNetworkQuantum(Mem& mem, Cpu& cpu, std::int64_t frameCycles,
                           OnSlice&& onSlice) {
        const bool hub = state_.network.appleTalkEnabled;
        if (!state_.network.ltoudp.active() && !hub) {
            cpu.runCycles(frameCycles);
            onSlice();
            return;
        }
        const int slices = hub ? 64 : 16;
        for (int i = 0; i < slices; ++i) {
            cpu.runCycles(frameCycles / slices);
            pollNetwork(mem);
            if (hub) state_.network.atalk.tick(cpu.machineClock());
            onSlice();
        }
    }

    template <class Mem, class Cpu>
    void runNetworkQuantum(Mem& mem, Cpu& cpu, std::int64_t frameCycles) {
        runNetworkQuantum(mem, cpu, frameCycles, [] {});
    }

    bool networkEnabled() const noexcept {
        return state_.network.appleTalkEnabled;
    }

    void tickNetwork(std::int64_t machineClock) {
        if (state_.network.appleTalkEnabled)
            state_.network.atalk.tick(machineClock);
    }

    template <class AudioHost>
    void prepareAudioHost(AudioHost& audioHost) {
        initializeDriveSounds(audioHost);
    }

    template <class Mem, class AudioHost>
    void prepareDriveSounds(Mem& mem, AudioHost& audioHost) {
        initializeDriveSounds(audioHost);
        mem.attachDriveSounds(&state_.audio.floppySfx, &state_.audio.hddSfx);
    }

    template <class Cpu>
    bool qualify(const char* machine, const char* firmware,
                 bool firmwareActive, const Cpu& cpu) const {
        if (!config_.fullLleAarch64()) return true;
        const bool native = cpu.engine() == 1 &&
            std::strcmp(cpu.jit().backendName(), "aarch64") == 0;
        std::string assetError;
        const bool assetValid = firmware::verify(
            firmware,
            config_.core().firmware.root.value_or(std::string()), assetError);
        const std::uint32_t hle = lle::activeHleModules();
        if (!firmwareActive || !assetValid || !native || hle != 0) {
            std::string why;
            if (!firmwareActive)
                why = std::string("firmware/transport requis inactif: ") +
                      firmware;
            if (!assetValid) {
                if (!why.empty()) why += " ; ";
                why += assetError;
            }
            if (!native) {
                if (!why.empty()) why += " ; ";
                why += "backend AArch64 natif inactif";
            }
            if (hle) {
                if (!why.empty()) why += " ; ";
                why += "module(s) HLE actif(s), masque=" +
                       std::to_string(hle);
            }
            std::fprintf(stderr,
                "Mode LLE AArch64 complet: REFUSÉ pour %s — %s. "
                "La session n'est pas qualifiée LLE et ne sera pas démarrée.\n",
                machine, why.c_str());
            lle::setQualified(false);
            return false;
        }
        lle::setQualified(true);
        std::printf(
            "Mode LLE AArch64 complet: QUALIFIÉ — %s, %s, backend aarch64\n",
            machine, firmware);
        return true;
    }

    bool checkOnly() const noexcept { return config_.fullLleCheckOnly(); }

    void requestRelaunch(GLFWwindow* window, const std::string& romName,
                         const std::string& boot,
                         const std::vector<std::string>& extras);
    int processRelaunch() const;
    void traceKey(std::uint8_t adb, bool down) const;

private:
    void configureAppleTalk();
    void initializeDriveSounds(MacAudioHost& audioHost);

    GuiSessionState& state_;
    GuiSessionObjects& objects_;
    GuiShell& shell_;
    const app::RuntimeConfig& config_;
};

} // namespace pom68k::gui
