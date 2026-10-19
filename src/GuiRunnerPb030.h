// POM68K — Jaws PowerBook GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiFloppyBays.h"
#include "GuiAgentAutostart.h"
#include "GuiShellCommon.h"

namespace pom68k::gui {

struct Pb030RunnerSpec {
    const char* name;
    const char* pramTag;
    const char* defaultHdd;
    const char* cpuLine;
    MachineKind kind;
    SnapMachine snap;
};

template <class MachineT, class Mem, class Cpu, class AudioHost,
          class SeedRtc, class Services>
int runPb030Gui(Mem& mem, Cpu& cpu, AudioHost& audioHost,
              const Pb030RunnerSpec& spec, SeedRtc&& seedRtc,
              const std::string& romName,
              const std::vector<std::string>& media,
              Services& services) {
    cpu.hardReset();
    services.wireNetwork(mem, cpu);

    std::string hddPath = !media.empty() ? media.front()
        : services.locate(spec.defaultHdd);
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) installAgentStartupItem(mem.scsiDisk(), services);
    if (hddOk) std::printf("%s HD: %s (write-back)\n", "SCSI", hddPath.c_str());
    else std::fprintf(stderr,
                      "No startup image — drop a .dsk/.vhd in hdv/.\n");

    std::vector<std::string> extraDisks;
    for (std::size_t i = 1; i < media.size() && extraDisks.size() < 6; ++i) {
        const std::string& argument = media[i];
        if (argument == hddPath) continue;
        if (argument == kEmptyBayToken) {           // a gap that keeps its id
            extraDisks.emplace_back();
            continue;
        }
        const int id = int(extraDisks.size()) + 1;
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
    const auto startupFloppy = services.config().devices().startupFloppy.value_or("");
    const bool floppyInserted = !startupFloppy.empty() && mem.insertDisk(startupFloppy);
    mem.internalDrive().setWriteBack(services.config().devices().floppyWriteBack);

    std::string pramPath =
        (hddPath.empty() ? std::string(spec.pramTag) : hddPath) + "." +
        spec.pramTag + ".pram";
    if (mem.loadPram(pramPath))
        std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    const std::string title = std::string("POM68K — ") + spec.name;
    auto* ui = services.shell().openWindow(1320, 1000, title);
    if (!ui) return 1;
    GLFWwindow* window = ui->window();
    const GLuint screenTex = ui->texture();

    services.prepareDriveSounds(mem, audioHost);
    services.attachCdAudio(mem, 0);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");

    MachineT& machine = services.template own<MachineT>(
        mem, cpu, audioHost, services);
    machine.setFloppyInserted(floppyInserted, startupFloppy);
    machine.state.kind = spec.snap;
    machine.state.setPath(pramPath.substr(0, pramPath.size() - 5) + ".pomss");
    services.armInputRecording(machine, spec.pramTag, romName, media);
    services.shell().bindCpuMenu(machine, cpu);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint texture;
        ScreenInput input;
        std::string romName, hddPath;
        std::vector<std::string> extraDisks;
        Pb030RunnerSpec spec;
        Services& services;
        DiskBaysHost diskHost;
        AdbKeyboard keyboard;
    };
    Ctx& ctx = services.template own<Ctx>(
        window, machine, screenTex, ScreenInput{}, romName, hddPath,
        std::move(extraDisks), spec, services, DiskBaysHost{}, AdbKeyboard{});
    ctx.diskHost = diskBaysHostFor<MachineT>(ctx.machine, &ctx.extraDisks,
                                             {false, false});
    bindFloppyBays(ctx.diskHost, ctx.machine);
    ctx.diskHost.relaunch = [&ctx](const std::string& boot,
                                   const std::vector<std::string>& extras) {
        ctx.services.requestRelaunch(ctx.window, ctx.romName, boot, extras);
    };
    services.shell().bindMachineControls(machine, [&ctx] {
        const auto status = ctx.machine.status();
        ImGui::Text("%s  PC=%08X  clock=%lld", ctx.spec.cpuLine,
                    status.pc, status.clock);
        ImGui::Text("overlay=%d  MMU=%s  PMU hold=%d  LCD 640x400",
                    status.overlay ? 1 : 0,
                    status.mmu ? "on" : "off",
                    status.held ? 1 : 0);
    });

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

        c.services.shell().drawMachineMenu(c.spec.snap, c.window);

        refreshFloppyBays(c.diskHost, c.machine);
        {
            DiskBaysHost& host = c.diskHost;
            host.romName = c.romName;
            host.bootPath = c.hddPath;
            diskBaysWindow(host);
        }

        screenWindowBegin(c.services.shell().display(), c.spec.name);
        std::vector<uint32_t> framebuffer;
        int frameWidth = 0, frameHeight = 0;
        if (machine.latchFrame(framebuffer, frameWidth, frameHeight) &&
            uploadFrameTexture(GlTextureHost{}, c.texture, framebuffer,
                               frameWidth, frameHeight)) {
            c.input.frame(c.services.shell().display(), GlfwScreenHost{c.window}, c.texture,
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
            c.services.traceKey(adb, down);
        });

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



} // namespace pom68k::gui
