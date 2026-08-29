// POM68K — Toby/NuBus GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"
#include "GuiTobyStatus.h"

namespace pom68k::gui {

struct TobyRunnerSpec {
    std::string windowName;
    std::string screenName;
    std::string cpuLine;
    std::string pramTag;
    std::string snapshotTag;
    std::vector<std::string> defaultHdds;
    MachineKind kind;
    SnapMachine snap;
    bool traceKeys;
};

// mem/cpu/audioHost are caller-owned session objects whose ROM and video card have
// already been installed. readStatus adapts board atomics to a render-neutral
// value; this template owns every process/UI/media lifecycle around it.
template <class MachineT, class Mem, class Cpu, class AudioHost,
          class SeedRtc, class ReadStatus, class Services>
int runTobyGui(Mem& mem, Cpu& cpu, AudioHost& audioHost,
               const TobyRunnerSpec& spec, SeedRtc&& seedRtc,
               ReadStatus&& readStatus, const std::string& romName,
               const std::vector<std::string>& media, Services& services) {
    cpu.hardReset();
    seedRtc();
    services.wireNetwork(mem);

    std::string hddPath = !media.empty() ? media.front() : std::string();
    if (hddPath.empty()) {
        for (const std::string& candidate : spec.defaultHdds) {
            hddPath = services.locate(candidate);
            if (!hddPath.empty()) break;
        }
    }
    bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr,
                      "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    std::string pramPath =
        (hddPath.empty() ? spec.pramTag : hddPath + "." + spec.pramTag) +
        ".pram";
    if (mem.loadPram(pramPath))
        std::printf("PRAM: %s\n", pramPath.c_str());

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

    const std::string windowTitle =
        std::string("POM68K — ") + spec.windowName;
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
        mem, cpu, audioHost, services);
    services.shell().bindCpuMenu(machine, cpu);
    machine.state.kind = spec.snap;
    machine.state.path =
        (hddPath.empty() ? spec.snapshotTag
                         : hddPath + "." + spec.snapshotTag) + ".pomss";
    services.armInputRecording(machine, spec.snapshotTag, romName, media);
    machine.publish(true);

    std::string floppyPath = services.config().devices().startupFloppy
        .value_or(std::string());
    bool floppyOk =
        !floppyPath.empty() && mem.insertDisk(floppyPath);
    if (floppyOk) std::printf("Floppy: %s\n", floppyPath.c_str());
    machine.setFloppyInserted(floppyOk, floppyPath);

    using StatusReader = std::decay_t<ReadStatus>;
    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint texture;
        ScreenInput input;
        std::string romName, hddPath, floppyPath;
        std::vector<std::string> extraDisks;
        bool floppyOk;
        TobyRunnerSpec spec;
        StatusReader readStatus;
        Services& services;
        DiskBaysHost diskHost;
        AdbKeyboard keyboard;
    };
    Ctx& ctx = services.template own<Ctx>(
        window, machine, screenTex, ScreenInput{}, romName, hddPath,
        floppyPath, std::move(extraDisks), floppyOk, spec,
        std::forward<ReadStatus>(readStatus), services, DiskBaysHost{},
        AdbKeyboard{});
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

    auto frame = [](void* opaque) {
        Ctx& c = *static_cast<Ctx*>(opaque);
        MachineT& machine = c.machine;
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        c.services.shell().drawMachineMenu(c.spec.snap, c.window, [] {
            diskBaysMenuItem();
        });
        {
            DiskBaysHost& host = c.diskHost;
            host.romName = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.machine.floppyPath();
            diskBaysWindow(host);
        }

        dockLayoutScreenWindow(c.spec.screenName.c_str());
        ImGui::Begin(c.spec.screenName.c_str());
        std::vector<uint32_t> framebuffer;
        int frameWidth = 0, frameHeight = 0;
        if (machine.latchFrame(framebuffer, frameWidth, frameHeight) &&
            frameWidth > 0 && frameHeight > 0) {
            glBindTexture(GL_TEXTURE_2D, c.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                         frameWidth, frameHeight, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, framebuffer.data());
            c.input.frame(
                c.window, c.texture,
                ImVec2(float(frameWidth * 2), float(frameHeight * 2)),
                [&](int dx, int dy) {
                    machine.push({MachineT::Cmd::MouseMove, dx, dy});
                },
                [&](int button, bool down) {
                    machine.push({MachineT::Cmd::MouseButton, button,
                                  down ? 1 : 0});
                });
        }
        ImGui::End();

        c.keyboard.frameLegacy(machine, [&c](uint8_t adb, bool down) {
            if (c.spec.traceKeys) c.services.traceKey(adb, down);
        });

        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const TobyStatusView status = c.readStatus(machine);
        drawTobyStatus(c.spec.cpuLine, status);
        bool running = machine.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run"))
            machine.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            machine.push({MachineT::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = machine.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Avance rapide", &turbo)) machine.turbo.store(turbo);
        c.services.shell().drawSaveState(machine.state);
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
    audioHost.stop();
    ui->close();
    services.shell().noteWindowClosed();
    return services.processRelaunch();
#endif
    return 0;
}

} // namespace pom68k::gui
