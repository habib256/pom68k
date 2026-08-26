// POM68K — V8/Eagle/Spice/Tinker Bell GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"

namespace pom68k::gui {

struct V8RunnerSpec {
    std::string name;
    std::string cpu;
    std::string pramTag;
    double cpuMhz;
    MachineKind kind;
    SnapMachine snap;
    bool pmmu;
    bool setInitialMonitor;
    bool showMonitorControls;
};

template <class MachineT, class Mem, class Cpu, class Video, class AudioHost,
          class ConfigureCpu, class SeedRtc, class Services>
int runV8Gui(Mem& mem, Cpu& cpu, Video& video, AudioHost& audioHost,
             const V8RunnerSpec& spec, ConfigureCpu&& configureCpu,
             SeedRtc&& seedRtc, const std::string& romName,
             const std::vector<std::string>& media, Services& services) {
    services.wireNetwork(mem);
    if (spec.setInitialMonitor)
        mem.setMonitorSense(services.config().devices().monitorWidth &&
                                    *services.config().devices().monitorWidth < 640
                                ? 2
                                : 6);
    configureCpu();
    cpu.hardReset();

    std::string hddPath = !media.empty() ? media.front()
        : services.locate("hdv/" + spec.pramTag + "-boot.vhd");
    if (hddPath.empty())
        hddPath = services.locate("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/HD20SC.vhd");
    bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk)
        std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else
        std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");

    std::vector<std::string> extraDisks;
    for (std::size_t i = 1; i < media.size() && extraDisks.size() < 6; ++i) {
        const std::string& argument = media[i];
        if (argument == hddPath) continue;
        const int id = int(extraDisks.size()) + 1;
        if (argument == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (diskBaysPathIsCd(argument)) {
            if (mem.attachCdrom(argument, id)) {
                extraDisks.push_back(argument);
                std::printf("SCSI CD %d: %s\n", id, argument.c_str());
            } else {
                std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id,
                             argument.c_str());
            }
            continue;
        }
        if (mem.attachScsi(argument, true, id)) {
            extraDisks.push_back(argument);
            std::printf("SCSI HD %d: %s (write-back)\n", id,
                        argument.c_str());
        } else {
            std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id,
                         argument.c_str());
        }
    }
    ensureCdDrive(mem, extraDisks, services.config().core().storage.cdBay);

    std::string pramPath =
        (hddPath.empty() ? spec.pramTag
                         : hddPath + "." + spec.pramTag) + ".pram";
    if (mem.loadPram(pramPath))
        std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    const std::string windowTitle = std::string("POM68K — ") + spec.name;
    auto* ui = services.shell().openWindow(1320, 1040, windowTitle);
    if (!ui) return 1;
    GLFWwindow* window = ui->window();
    const GLuint screenTex = ui->texture();

    services.prepareDriveSounds(mem, audioHost);
    mem.internalDrive().setWriteBack(
        services.config().devices().floppyWriteBack);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");

    MachineT& machine = services.template own<MachineT>(
        mem, cpu, video, audioHost, services);
    services.shell().bindCpuMenu(machine, cpu);
    machine.state.kind = spec.snap;
    machine.state.path =
        pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint texture;
        std::vector<uint32_t> framebuffer;
        std::string romName, hddPath, floppyPath;
        bool floppyOk = false;
        std::vector<std::string> extraDisks;
        V8RunnerSpec spec;
        Services& services;
        DiskBaysHost diskHost;
        ScreenInput input;
        AdbKeyboard keyboard;
    };
    Ctx& ctx = services.template own<Ctx>(
        window, machine, screenTex, std::vector<uint32_t>{}, romName,
        hddPath, std::string{}, false, std::move(extraDisks), spec, services,
        DiskBaysHost{}, ScreenInput{}, AdbKeyboard{});
    ctx.diskHost = [&ctx] {
        DiskBaysHost h;
        h.extras = &ctx.extraDisks;
        h.hardReset = [&ctx] {
            ctx.machine.push({MachineT::Cmd::HardReset});
        };
        h.relaunch = [&ctx](const std::string& boot,
                            const std::vector<std::string>& extras) {
            ctx.services.requestRelaunch(
                ctx.window, ctx.romName, boot, extras);
        };
        h.bayIsCd = [&ctx](int id) {
            return ctx.machine.bayIsCdrom(id);
        };
        h.insertBay = [&ctx](int id, const std::string& disk) {
            if (!ctx.machine.bayIsCdrom(id)) return false;
            ctx.machine.requestInsertBay(id, disk);
            return true;
        };
        h.ejectBay = [&ctx](int id) {
            ctx.machine.requestEjectBay(id);
        };
        h.hasFloppyDrive = true;
        h.floppyInserted = [&ctx] {
            return ctx.machine.floppyInserted();
        };
        h.insertFloppy = [&ctx](const std::string& disk) {
            ctx.machine.requestInsertFloppy(disk);
            ctx.floppyPath = disk;
            ctx.floppyOk = true;
        };
        h.ejectFloppy = [&ctx] {
            ctx.machine.requestEjectFloppy();
            ctx.floppyPath.clear();
            ctx.floppyOk = false;
        };
        return h;
    }();

    if (const auto& floppy = services.config().devices().startupFloppy) {
        if (mem.insertDisk(*floppy)) {
            ctx.floppyPath = *floppy;
            ctx.floppyOk = true;
            machine.setFloppyInserted(true, *floppy);
            std::printf("Floppy: %s\n", floppy->c_str());
        }
    }

    auto frame = [](void* opaque) {
        Ctx& c = *static_cast<Ctx*>(opaque);
        MachineT& machine = c.machine;
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

#ifdef __EMSCRIPTEN__
        machine.stepTick();
#endif

        int hres = 0, vres = 0;
        if (machine.latchFrame(c.framebuffer, hres, vres)) {
            glBindTexture(GL_TEXTURE_2D, c.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hres, vres, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, c.framebuffer.data());
        }

        c.services.shell().drawMachineMenu(c.spec.snap, c.window, [&c] {
            diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.machine.push({MachineT::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état"))
                c.machine.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état"))
                c.machine.state.request(true);
            const std::string message = c.machine.state.message();
            if (!message.empty())
                ImGui::TextDisabled("%s", message.c_str());
        });

        {
            DiskBaysHost& host = c.diskHost;
            host.romName = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = machine.floppyPath();
            diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        dockLayoutScreenWindow(c.spec.name.c_str());
        ImGui::Begin(c.spec.name.c_str());
        c.input.frame(c.window, c.texture,
                    ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) {
                        machine.push({MachineT::Cmd::MouseMove, dx, dy});
                    },
                    [&](int button, bool down) {
                        machine.push({MachineT::Cmd::MouseButton, button,
                                      down ? 1 : 0});
                    });
        ImGui::End();

        c.keyboard.frame(machine, [&c](uint8_t adb, bool down) {
            c.services.traceKey(adb, down);
        });

        ImGui::SetNextWindowPos(ImVec2(20, 830), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const auto status = machine.status();
        ImGui::Text("%s @ %.4f MHz (Moira%s)  PC=%08X  clock=%lld",
                    c.spec.cpu.c_str(), c.spec.cpuMhz,
                    c.spec.pmmu ? " + PMMU" : "",
                    status.pc, status.clock);
        ImGui::Text("overlay=%d  config=$%02X  MMU=%s  held=%d",
                    status.overlay ? 1 : 0, status.config,
                    status.mmu ? "on" : "off", status.held ? 1 : 0);
        bool running = machine.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run"))
            machine.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            machine.push({MachineT::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = machine.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Avance rapide", &turbo)) machine.turbo.store(turbo);

        if (c.spec.showMonitorControls) {
            const int sense = status.sense;
            ImGui::Text("Moniteur:");
            ImGui::SameLine();
            auto monitorButton = [&](const char* label, int value) {
                const bool current = sense == value;
                if (current)
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(label) && !current)
                    machine.push({MachineT::Cmd::Sense, value});
                if (current) ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            monitorButton("512x384", 2);
            monitorButton("640x480", 6);
            ImGui::TextDisabled("(redemarre le Mac)");
        }
        ImGui::End();

        c.services.shell().runSmokeFrame(c.window, machine.state);
        ImGui::Render();
        int width, height;
        glfwGetFramebufferSize(c.window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(c.window);
    };

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frame, &ctx, 0, 1);
#else
    machine.start();
    while (!glfwWindowShouldClose(window)) frame(&ctx);
    machine.stop();
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();
    audioHost.stop();
    ui->close();
    services.shell().noteWindowClosed();
    return services.processRelaunch();
#endif
    return 0;
}


// The Duo is deliberately not forced into the floppy-capable desktop
// contract. PG&E owns RTC/PRAM/input, and MscMemory has neither an internal
// floppy nor an empty/live CD target. This specification keeps its identity
// data while the template preserves those negative capabilities.

} // namespace pom68k::gui
