// POM68K — Sonora/VASP/RBV GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"

namespace pom68k::gui {

struct SonoraRunnerSpec {
    /// Human name, including "Macintosh". Used by GLFW, the screen window
    /// (and therefore its dock-layout key), and the Machine menu.
    std::string name;
    /// File suffix: "<boot image>.<tag>.pram", and ".pomss" beside it.
    std::string pramTag;
    /// First boot-volume candidate before the common fallbacks.
    std::string defaultHdd;
    /// Optional CPU-panel first line. Empty preserves platforms whose copied
    /// runner never exposed that panel.
    std::string cpuLine;
    MachineKind kind;
    SnapMachine snap;
    uint8_t monitorSense;
    float clearR, clearG, clearB;
};

/// mem/cpu/video are session-owned objects already carrying the ROM. The
/// descriptor captures every GUI-visible variation; seedRtc initializes the
/// board-specific battery clock after PRAM has been loaded.
template <class MachineT, class Mem, class Cpu, class Video, class AudioHost,
          class SeedRtc, class Services>
int runSonoraGui(Mem& mem, Cpu& cpu, Video& video,
                 AudioHost& audioHost, const SonoraRunnerSpec& spec,
                 SeedRtc&& seedRtc, const std::string& romName,
                 const std::vector<std::string>& media, Services& services) {
    services.wireNetwork(mem);
    mem.setMonitorSense(services.config().devices().monitorWidth
        ? (*services.config().devices().monitorWidth < 640 ? 2 : 6)
        : spec.monitorSense);
    cpu.hardReset();

    std::string hddPath = !media.empty() ? media.front()
        : (spec.defaultHdd.empty() ? std::string() : services.locate(spec.defaultHdd));
    if (hddPath.empty()) hddPath = services.locate("hdv/lc3-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/HD20SC.vhd");
    bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");

    std::vector<std::string> extraDisks;
    for (std::size_t i = 1; i < media.size() && extraDisks.size() < 6; ++i) {
        const std::string& argument = media[i];
        if (argument == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        // "cdbay" reserves an empty CD drive on the bus; a CD image creates
        // the same hot-swappable bay with media already inserted.
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
            } else std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id,
                                argument.c_str());
            continue;
        }
        if (mem.attachScsi(argument, true, id)) {
            extraDisks.push_back(argument);
            std::printf("SCSI HD %d: %s (write-back)\n", id,
                        argument.c_str());
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id,
                            argument.c_str());
    }
    ensureCdDrive(mem, extraDisks, services.config().core().storage.cdBay);

    std::string pramPath =
        (hddPath.empty() ? spec.pramTag : hddPath + "." + spec.pramTag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    const std::string winTitle = std::string("POM68K — ") + spec.name;
    auto* ui = services.shell().openWindow(1320, 1040, winTitle);
    if (!ui) return 1;
    GLFWwindow* window = ui->window();
    const GLuint screenTex = ui->texture();

    services.prepareDriveSounds(mem, audioHost);
    // GUI floppies persist committed writes back to the image file on eject
    // and exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable write-back.
    mem.internalDrive().setWriteBack(
        services.config().devices().floppyWriteBack);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    MachineT& machine = services.template own<MachineT>(
        mem, cpu, video, audioHost, services);
    services.shell().bindCpuMenu(machine, cpu);
    machine.state.kind = spec.snap;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    services.armInputRecording(machine, spec.pramTag, romName, media);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; MachineT& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath, floppyPath;
        bool floppyOk = false;
        std::vector<std::string> extraDisks;
        SonoraRunnerSpec spec;
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
            ctx.m.push({MachineT::Cmd::HardReset});
        };
        h.relaunch = [&ctx](const std::string& boot,
                            const std::vector<std::string>& extras) {
            ctx.services.requestRelaunch(
                ctx.window, ctx.romName, boot, extras);
        };
        h.bayIsCd = [&ctx](int id) { return ctx.m.bayIsCdrom(id); };
        h.insertBay = [&ctx](int id, const std::string& disk) {
            if (!ctx.m.bayIsCdrom(id)) return false;
            ctx.m.requestInsertBay(id, disk);
            return true;
        };
        h.ejectBay = [&ctx](int id) { ctx.m.requestEjectBay(id); };
        h.hasFloppyDrive = true;
        h.floppyInserted = [&ctx] { return ctx.m.floppyInserted(); };
        h.insertFloppy = [&ctx](const std::string& disk) {
            ctx.m.requestInsertFloppy(disk);
            ctx.floppyPath = disk;
            ctx.floppyOk = true;
        };
        h.ejectFloppy = [&ctx] {
            ctx.m.requestEjectFloppy();
            ctx.floppyPath.clear();
            ctx.floppyOk = false;
        };
        return h;
    }();

    // Optional startup floppy; the Disques window can hot-swap it later.
    if (const auto& floppy = services.config().devices().startupFloppy) {
        if (mem.insertDisk(*floppy)) {
            ctx.floppyPath = *floppy;
            ctx.floppyOk = true;
            machine.setFloppyInserted(true, *floppy);
            std::printf("Floppy: %s\n", floppy->c_str());
        }
    }

    auto frame = [](void* p) {
        Ctx& c = *static_cast<Ctx*>(p);
        Services& services = c.services;
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

#ifdef __EMSCRIPTEN__
        c.m.stepTick();
#endif

        int hres = 0, vres = 0;
        if (c.m.latchFrame(c.fb, hres, vres)) {
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hres, vres, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, c.fb.data());
        }

        services.shell().drawMachineMenu(c.spec.snap, c.window, [&c] {
            diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({MachineT::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            const std::string ssMsg = c.m.state.message();
            if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
        });

        // Hooks are owned by the runner context and cross mutations through
        // MachineHost commands.
        {
            DiskBaysHost& host = c.diskHost;
            host.romName = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.m.floppyPath();
            diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        dockLayoutScreenWindow(c.spec.name.c_str());
        ImGui::Begin(c.spec.name.c_str());
        c.input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) {
                        c.m.push({MachineT::Cmd::MouseMove, dx, dy});
                    },
                    [&](int button, bool down) {
                        c.m.push({MachineT::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        ImGui::End();

        // Preserve the original Sonora behaviour: transitions were not
        // included in POM68K_KEY_TRACE on these three platforms.
        c.keyboard.frame(c.m, [](uint8_t, bool) {});

        // The LC III copy exposed this panel; the VASP/RBV copies did not.
        // An empty descriptor field preserves that visible distinction.
        if (!c.spec.cpuLine.empty()) {
            ImGui::SetNextWindowPos(ImVec2(20, 830), ImGuiCond_FirstUseEver);
            ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            const auto st = c.m.status();
            ImGui::Text("%s  PC=%08X  clock=%lld", c.spec.cpuLine.c_str(),
                        st.pc, st.clock);
            ImGui::Text("overlay=%d  MMU=%s  held=%d",
                        st.overlay ? 1 : 0, st.mmu ? "on" : "off",
                        st.held ? 1 : 0);
            bool running = c.m.running.load(std::memory_order_relaxed);
            if (ImGui::Button(running ? "Pause" : "Run"))
                c.m.running.store(!running);
            ImGui::SameLine();
            if (ImGui::Button("Reset"))
                c.m.push({MachineT::Cmd::HardReset});
            ImGui::SameLine();
            bool turbo = c.m.turbo.load(std::memory_order_relaxed);
            if (ImGui::Checkbox("Avance rapide", &turbo)) c.m.turbo.store(turbo);

            int sense = st.sense;
            ImGui::Text("Moniteur:");
            ImGui::SameLine();
            auto monoBtn = [&](const char* label, int s) {
                bool current = sense == s;
                if (current)
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(label) && !current)
                    c.m.push({MachineT::Cmd::Sense, s});
                if (current) ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            monoBtn("512x384", 2);
            monoBtn("640x480", 6);
            ImGui::TextDisabled("(redemarre le Mac)");
            ImGui::End();
        }

        services.shell().runSmokeFrame(c.window, c.m.state);
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(c.window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(c.spec.clearR, c.spec.clearG, c.spec.clearB, 1.0f);
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


// Mac II-family and IIfx boards differ below the host boundary, but their
// Toby/NuBus GUI lifecycle is the same.  The descriptor retains the few
// user-visible historical differences, including IIfx's capitalized
// snapshot suffix and the SE/30's internal-video screen identity.

} // namespace pom68k::gui
