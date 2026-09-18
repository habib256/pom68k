// POM68K — compact 68000 GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"
#include "GuiRunnerCompactStorage.h"

namespace pom68k::gui {

struct CompactRunnerSpec {
    std::string romName;
    std::string hddPath;
    std::string floppyPath;
    std::vector<std::string> extraDisks;
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
    configureFloppyWriteBack(
        mem, services.config().devices().floppyWriteBack);
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
    ctx.diskHost = compactDiskBaysHost<MachineT>(ctx);
    services.shell().bindMachineControls(machine, [&ctx] {
        const auto status = ctx.machine.status();
        ImGui::Text(
            "68000 @ 7.8336 MHz (Moira, cycle-exact)  PC=%06X  clock=%lld",
            status.pc, status.clock);
        ImGui::Text("overlay=%d  demo=%d  floppy=%s",
                    status.overlay ? 1 : 0, ctx.spec.demoMode ? 1 : 0,
                    ctx.machine.floppyInserted() ? "inserted" : "none");
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

        int frameWidth = c.spec.initialWidth;
        int frameHeight = c.spec.initialHeight;
        std::vector<std::uint32_t> framebuffer;
        if (machine.latchFrame(framebuffer, frameWidth, frameHeight))
            uploadFrameTexture(GlTextureHost{}, c.tex, framebuffer,
                               frameWidth, frameHeight);

        c.services.shell().drawMachineMenu(c.machine.state.kind, c.window);
        {
            pom68k::DiskBaysHost& host = c.diskHost;
            host.romName = c.spec.romName;
            host.bootPath = c.spec.hddPath;
            const std::string liveFloppy = machine.floppyPath();
            if (!machine.floppyInserted()) c.spec.floppyPath.clear();
            else if (!liveFloppy.empty()) c.spec.floppyPath = liveFloppy;
            host.floppyPath = c.spec.floppyPath;
            host.externalFloppyPath = machine.floppyPath(1);
            pom68k::diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        screenWindowBegin(c.services.shell().display(), c.spec.machineName.c_str());
        c.input.frame(c.services.shell().display(), GlfwScreenHost{c.window}, c.tex,
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
    flushFloppyDrives(mem);
    audioHost.stop();
    ui->close();
    services.shell().noteWindowClosed();
    return services.processRelaunch();
#endif
    return 0;
}

} // namespace pom68k::gui
