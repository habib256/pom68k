// POM68K — PowerBook Duo GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"

namespace pom68k::gui {

struct DuoRunnerSpec {
    const char* name;
    const char* pramTag;
    const char* defaultHdd;
    const char* cpuLine;
    MachineKind kind;
    SnapMachine snap;
};

template <class MachineT, class Mem, class Cpu, class AudioHost,
          class SeedRtc, class Services>
int runDuoGui(Mem& mem, Cpu& cpu, AudioHost& audioHost,
              const DuoRunnerSpec& spec, SeedRtc&& seedRtc,
              const std::string& romName,
              const std::vector<std::string>& media,
              Services& services) {
    cpu.hardReset();
    services.wireNetwork(mem);

    std::string hddPath = !media.empty() ? media.front()
        : services.locate(spec.defaultHdd);
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr,
                      "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    std::vector<std::string> extraDisks;
    for (std::size_t i = 1; i < media.size() && extraDisks.size() < 6; ++i) {
        const std::string& argument = media[i];
        if (argument == hddPath) continue;
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

    // Preserve the historical empty-boot name: duo230.duo230.pram rather
    // than the single-tag shape used by the desktop families.
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

    // MscMemory intentionally has no attachDriveSounds(): there is no
    // internal floppy. The shared audio host still owns ASC output; the
    // session owns the drive-sound menu state even though no sources attach.
    services.prepareAudioHost(audioHost);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");

    MachineT& machine = services.template own<MachineT>(
        mem, cpu, audioHost, services);
    machine.state.kind = spec.snap;
    machine.state.path =
        pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    services.shell().bindCpuMenu(machine, cpu);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint texture;
        ScreenInput input;
        std::string romName, hddPath;
        std::vector<std::string> extraDisks;
        DuoRunnerSpec spec;
        Services& services;
        DiskBaysHost diskHost;
        AdbKeyboard keyboard;
    };
    Ctx& ctx = services.template own<Ctx>(
        window, machine, screenTex, ScreenInput{}, romName, hddPath,
        std::move(extraDisks), spec, services, DiskBaysHost{}, AdbKeyboard{});
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
        h.hasFloppyDrive = false;
        h.supportsEmptyCdDrive = false;
        return h;
    }();

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

        // No floppy hooks and no live bay hooks: the Disques window stages
        // CD changes for a relaunch, exactly as the old Duo body did.
        {
            DiskBaysHost& host = c.diskHost;
            host.romName = c.romName;
            host.bootPath = c.hddPath;
            diskBaysWindow(host);
        }

        dockLayoutScreenWindow(c.spec.name);
        ImGui::Begin(c.spec.name);
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
            c.services.traceKey(adb, down);
        });

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const auto status = machine.status();
        ImGui::Text("%s  PC=%08X  clock=%lld", c.spec.cpuLine,
                    status.pc, status.clock);
        ImGui::Text("overlay=%d  MMU=%s  PG&E hold=%d  GSC mode=$%02X",
                    status.overlay ? 1 : 0,
                    status.mmu ? "on" : "off",
                    status.held ? 1 : 0, status.gscMode);
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
