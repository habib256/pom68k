// POM68K — compact 68000 GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"

namespace pom68k::gui {

struct CompactRunnerSpec {
    std::string romName;
    std::string hddPath;
    std::string floppyPath;
    std::string pramPath;
    std::string windowTitle;
    std::string machineName;
    MachineKind kind;
    bool demoMode;
    int initialWidth;
    int initialHeight;
};

template <class MachineT, class Mem, class Cpu, class AudioHost,
          class Services>
int runCompactGui(MachineT& machine, Mem& mem, Cpu& cpu,
                  AudioHost& audioHost, Services& services,
                  const CompactRunnerSpec& spec) {
    auto* ui = services.shell().openWindow(1100, 800, spec.windowTitle);
    if (!ui) return 1;
    GLFWwindow* window = ui->window();
    const GLuint screenTex = ui->texture();

    services.prepareDriveSounds(mem, audioHost);
    mem.internalDrive().setWriteBack(
        services.config().devices().floppyWriteBack);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");
    services.shell().bindCpuMenu(machine, cpu);

    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint tex;
        CompactRunnerSpec spec;
        Services& services;
        pom68k::DiskBaysHost diskHost;
        ScreenInput input;
        CompactKeyboard keyboard;
    };
    Ctx& ctx = services.template own<Ctx>(
        window, machine, screenTex, spec, services,
        pom68k::DiskBaysHost{}, ScreenInput{}, CompactKeyboard{});
    ctx.diskHost = [&ctx] {
        pom68k::DiskBaysHost host;
        host.hardReset = [&ctx] {
            ctx.machine.push({MachineT::Cmd::HardReset});
        };
        host.relaunch = [&ctx](const std::string& boot,
                               const std::vector<std::string>& extras) {
            (void)extras;
            ctx.services.requestRelaunch(
                ctx.window, ctx.spec.romName, ctx.spec.floppyPath, {boot});
        };
        host.hasFloppyDrive = true;
        host.floppyInserted = [&ctx] {
            return ctx.machine.floppyInserted();
        };
        host.insertFloppy = [&ctx](const std::string& disk) {
            ctx.machine.requestInsertFloppy(disk);
            ctx.spec.floppyPath = disk;
        };
        host.ejectFloppy = [&ctx] {
            ctx.machine.requestEjectFloppy();
            ctx.spec.floppyPath.clear();
        };
        return host;
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

        int frameWidth = c.spec.initialWidth;
        int frameHeight = c.spec.initialHeight;
        std::vector<std::uint32_t> framebuffer;
        if (machine.latchFrame(framebuffer, frameWidth, frameHeight)) {
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frameWidth, frameHeight,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer.data());
        }

        c.services.shell().drawMachineMenu(
            c.machine.state.kind, c.window, [&c] {
            pom68k::diskBaysMenuItem();
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
            recordingMenuItems(c.machine);
        });
        {
            pom68k::DiskBaysHost& host = c.diskHost;
            host.romName = c.spec.romName;
            host.bootPath = c.spec.hddPath;
            const std::string liveFloppy = machine.floppyPath();
            if (!machine.floppyInserted()) c.spec.floppyPath.clear();
            else if (!liveFloppy.empty()) c.spec.floppyPath = liveFloppy;
            host.floppyPath = c.spec.floppyPath;
            pom68k::diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        pom68k::dockLayoutScreenWindow(c.spec.machineName.c_str());
        ImGui::Begin(c.spec.machineName.c_str());
        c.input.frame(c.window, c.tex,
                      ImVec2(float(frameWidth * 2), float(frameHeight * 2)),
                      [&](int dx, int dy) {
                          machine.push(
                              {MachineT::Cmd::MouseMove, dx, dy});
                      },
                      [&](int button, bool down) {
                          machine.push({MachineT::Cmd::MouseButton, button,
                                        down ? 1 : 0});
                      });
        ImGui::End();

        c.keyboard.frame(machine, [&c](std::uint8_t code, bool down) {
            c.services.traceKey(code, down);
        });

        ImGui::SetNextWindowPos(ImVec2(20, 740), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const auto status = machine.status();
        ImGui::Text(
            "68000 @ 7.8336 MHz (Moira, cycle-exact)  PC=%06X  clock=%lld",
            status.pc, status.clock);
        ImGui::Text("overlay=%d  demo=%d  floppy=%s",
                    status.overlay ? 1 : 0, c.spec.demoMode ? 1 : 0,
                    machine.floppyInserted() ? "inserted" : "none");
        bool running = machine.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run"))
            machine.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            machine.push({MachineT::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = machine.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Avance rapide x8", &turbo))
            machine.turbo.store(turbo);
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
    mem.savePram(spec.pramPath);
    mem.internalDrive().flushToFile();
    audioHost.stop();
    ui->close();
    services.shell().noteWindowClosed();
    return services.processRelaunch();
#endif
    return 0;
}

} // namespace pom68k::gui
