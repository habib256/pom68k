// POM68K — DAFB-family GUI runner
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"

namespace pom68k::gui {

struct DafbRunnerSpec {
    std::string name;
    std::string pramTag;
    std::string cpuLine;
    std::string lleFirmware;
    MachineKind kind;
    SnapMachine snap;
};

// Services is the explicit boundary back to the executable.  The runner
// needs qualify/checkOnly, sessionState, wireNetwork, locate, openWindow,
// prepareDriveSounds, bindCpuMenu, drawMachineMenu, requestRelaunch, traceKey
// and processRelaunch.
template <class MachineT, class Mem, class Cpu, class AudioHost,
          class SeedRtc, class Services>
int runDafbGui(Mem& mem, Cpu& cpu, AudioHost& audioHost,
               const DafbRunnerSpec& spec, bool lleActive,
               SeedRtc&& seedRtc, const std::string& romName,
               const std::vector<std::string>& media, Services& services) {
    if (!services.qualify(spec.name.c_str(), spec.lleFirmware.c_str(),
                          lleActive, cpu))
        return 2;
    if (services.checkOnly()) return 0;
    cpu.hardReset();
    services.wireNetwork(mem);

    std::string hddPath = !media.empty()
        ? media.front() : services.locate("hdv/MacOS-8.1-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");

    std::string floppyPath = services.config().devices().startupFloppy
        .value_or(std::string());
    if (floppyPath.empty() && !hddOk)
        floppyPath = services.locate("disks35/Disk605.dsk");
    if (floppyPath.empty() && !hddOk)
        floppyPath = services.locate("disks35/quadra.img");
    bool floppyOk = !floppyPath.empty() && mem.insertDisk(floppyPath);
    if (floppyOk) std::printf("Floppy: %s\n", floppyPath.c_str());

    std::vector<std::string> extraDisks;
    for (std::size_t i = 1; i < media.size() && extraDisks.size() < 6; ++i) {
        if (media[i] == hddPath) continue;
        const std::string& arg = media[i];
        auto ext = std::filesystem::path(arg).extension().string();
        for (char& ch : ext) ch = char(std::tolower(ch));
        if (ext == ".dsk" || ext == ".image") {
            if (mem.insertDisk(arg)) {
                floppyPath = arg;
                floppyOk = true;
                std::printf("Floppy: %s\n", arg.c_str());
            }
            continue;
        }
        const int id = int(extraDisks.size()) + 1;
        if (arg == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (diskBaysPathIsCd(arg)) {
            if (mem.attachCdrom(arg, id)) {
                extraDisks.push_back(arg);
                std::printf("SCSI CD %d: %s\n", id, arg.c_str());
            } else {
                std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id,
                             arg.c_str());
            }
            continue;
        }
        if (mem.attachScsi(arg, true, id)) {
            extraDisks.push_back(arg);
            std::printf("SCSI HD %d: %s (write-back)\n", id, arg.c_str());
        } else {
            std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, arg.c_str());
        }
    }
    ensureCdDrive(mem, extraDisks, services.config().core().storage.cdBay);

    std::string pramPath =
        (hddPath.empty() ? spec.pramTag
                         : hddPath + "." + spec.pramTag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    const std::string winTitle = "POM68K — " + spec.name;
    auto* ui = services.shell().openWindow(1320, 1080, winTitle);
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
    machine.state.kind = spec.snap;
    machine.state.setPath(pramPath.substr(0, pramPath.size() - 5) + ".pomss");
    services.armInputRecording(machine, spec.pramTag, romName, media);
    services.shell().bindCpuMenu(machine, cpu);
    machine.setFloppyInserted(floppyOk, floppyPath);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint texture;
        std::vector<uint32_t> framebuffer;
        std::string romName;
        std::string hddPath;
        std::string floppyPath;
        std::vector<std::string> extraDisks;
        bool floppyOk;
        DafbRunnerSpec spec;
        Services& services;
        DiskBaysHost diskHost;
        ScreenInput input;
        AdbKeyboard keyboard;
    };
    Ctx& ctx = services.template own<Ctx>(
        window, machine, screenTex, std::vector<uint32_t>{}, romName,
        hddPath, floppyPath, std::move(extraDisks), floppyOk, spec, services,
        DiskBaysHost{}, ScreenInput{}, AdbKeyboard{});
    ctx.diskHost = [&ctx] {
        DiskBaysHost value;
        value.extras = &ctx.extraDisks;
        value.hardReset = [&ctx] {
            ctx.machine.push({MachineT::Cmd::HardReset});
        };
        value.bayIsCd = [&ctx](int id) {
            return ctx.machine.bayIsCdrom(id);
        };
        value.insertBay = [&ctx](int id, const std::string& disk) {
            if (!ctx.machine.bayIsCdrom(id)) return false;
            ctx.machine.requestInsertBay(id, disk);
            return true;
        };
        value.ejectBay = [&ctx](int id) {
            ctx.machine.requestEjectBay(id);
        };
        value.relaunch = [&ctx](const std::string& boot,
                                const std::vector<std::string>& extras) {
            ctx.services.requestRelaunch(
                ctx.window, ctx.romName, boot, extras);
        };
        value.hasFloppyDrive = true;
        value.floppyInserted = [&ctx] {
            return ctx.machine.floppyInserted();
        };
        value.insertFloppy = [&ctx](const std::string& disk) {
            ctx.machine.requestInsertFloppy(disk);
            ctx.floppyPath = disk;
            ctx.floppyOk = true;
        };
        value.ejectFloppy = [&ctx] {
            ctx.machine.requestEjectFloppy();
            ctx.floppyPath.clear();
            ctx.floppyOk = false;
        };
        return value;
    }();

    auto frame = [](void* opaque) {
        Ctx& context = *static_cast<Ctx*>(opaque);
        MachineT& machine = context.machine;
        Services& services = context.services;
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

#ifdef __EMSCRIPTEN__
        machine.stepTick();
#endif

        int hres = 0;
        int vres = 0;
        if (machine.latchFrame(context.framebuffer, hres, vres)) {
            glBindTexture(GL_TEXTURE_2D, context.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hres, vres, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE,
                         context.framebuffer.data());
        }

        services.shell().drawMachineMenu(context.spec.snap, context.window, [&] {
            diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                machine.push({MachineT::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état"))
                machine.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état"))
                machine.state.request(true);
            const std::string message = machine.state.message();
            if (!message.empty())
                ImGui::TextDisabled("%s", message.c_str());
            recordingMenuItems(machine);
        });

        {
            DiskBaysHost& host = context.diskHost;
            host.romName = context.romName;
            host.bootPath = context.hddPath;
            host.floppyPath = machine.floppyPath();
            diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        dockLayoutScreenWindow(context.spec.name.c_str());
        ImGui::Begin(context.spec.name.c_str());
        context.input.frame(
            context.window, context.texture,
            ImVec2(float(hres * 2), float(vres * 2)),
            [&](int dx, int dy) {
                machine.push({MachineT::Cmd::MouseMove, dx, dy});
            },
            [&](int button, bool down) {
                machine.push(
                    {MachineT::Cmd::MouseButton, button, down ? 1 : 0});
            });
        ImGui::End();

        context.keyboard.frame(machine, [&](uint8_t adb, bool down) {
            services.traceKey(adb, down);
        });

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const auto status = machine.status();
        ImGui::Text("%s  PC=%08X  clock=%lld", context.spec.cpuLine.c_str(),
                    status.pc, status.clock);
        ImGui::Text("overlay=%d  %dx%d @ %d bpp  MMU=%s  held=%d",
                    status.overlay ? 1 : 0, status.w, status.h, status.depth,
                    status.mmu ? "on" : "off", status.held ? 1 : 0);
        const std::string liveFloppy = machine.floppyPath();
        ImGui::Text("floppy=%s", machine.floppyInserted()
                    ? (liveFloppy.empty() ? "inserted" : liveFloppy.c_str())
                    : "none");
        const bool running =
            machine.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run"))
            machine.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            machine.push({MachineT::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = machine.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Avance rapide", &turbo))
            machine.turbo.store(turbo);
        ImGui::End();

        services.shell().runSmokeFrame(context.window, machine.state);
        ImGui::Render();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(context.window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(context.window);
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


// The three SonoraStyleMachine platforms share the complete GUI contract.
// A wrapper still owns model decoding and constructs its memory/CPU/video;
// everything visible to the runner is data.

} // namespace pom68k::gui
