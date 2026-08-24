// POM68K — shared GUI runners
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// DAFB, Sonora, Toby/NuBus, V8 and Duo machines each share a complete
// host-side lifecycle. This header owns those lifecycles while the executable
// supplies the few process services that still live in main.cpp (asset lookup,
// menus, networking, engine hooks, snapshot UI and relaunch).

#pragma once

#include "DiskBays.h"
#include "DockLayout.h"
#include "MachineCatalog.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#endif

namespace pom68k::gui {

// An emulated screen is an InvisibleButton with the image drawn over it.
// A drag started on the Mac screen owns the mouse until release.  The middle
// mouse button, Ctrl+Alt+G, or Delete toggles hard GLFW cursor capture.
struct ScreenInput {
    bool captured = false;
    bool midWas = false;
    bool grabWas = false;
    float accX = 0;
    float accY = 0;
    float zoom = 2.0f;
    double lastX = 0;
    double lastY = 0;

    template <typename MoveFn, typename ButtonFn>
    void frame(GLFWwindow* win, GLuint tex, ImVec2 size,
               MoveFn move, ButtonFn button) {
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 native(size.x * 0.5f, size.y * 0.5f);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (size.x > 0 && size.y > 0 && avail.x > 32 && avail.y > 32) {
            float scale = avail.x / size.x;
            if (avail.y / size.y < scale) scale = avail.y / size.y;
            size = ImVec2(size.x * scale, size.y * scale);
        }
        zoom = native.x > 0 ? size.x / native.x : 2.0f;
        if (zoom < 0.05f) zoom = 0.05f;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("screen", size);
        ImGui::GetWindowDrawList()->AddImage(
            ImTextureID(intptr_t(tex)), pos,
            ImVec2(pos.x + size.x, pos.y + size.y));

        // GLFW polling remains active while ImGui mouse input is disabled,
        // so every capture shortcut can also release an existing capture.
        const bool mid = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE)
                             == GLFW_PRESS;
        const bool midEdge = mid && !midWas;
        midWas = mid;
        const bool ctrl = glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                       || glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const bool alt = glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                      || glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        const bool grab = ctrl && alt &&
                          glfwGetKey(win, GLFW_KEY_G) == GLFW_PRESS;
        const bool grabEdge = grab && !grabWas;
        grabWas = grab;
        if ((midEdge || grabEdge) && (captured || ImGui::IsItemHovered()))
            setCaptured(win, !captured);
        else if (!io.WantTextInput &&
                 ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            setCaptured(win, !captured);

        if (captured) {
            double x = 0;
            double y = 0;
            glfwGetCursorPos(win, &x, &y);
            feed(float(x - lastX), float(y - lastY), move);
            lastX = x;
            lastY = y;
            button(0, glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT)
                          == GLFW_PRESS);
            button(1, glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT)
                          == GLFW_PRESS);
        } else if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            feed(io.MouseDelta.x, io.MouseDelta.y, move);
            button(0, io.MouseDown[0]);
            button(1, io.MouseDown[1]);
        }
    }

    void setCaptured(GLFWwindow* win, bool on) {
        captured = on;
        glfwSetInputMode(win, GLFW_CURSOR,
                         on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (on) glfwGetCursorPos(win, &lastX, &lastY);
        ImGuiIO& io = ImGui::GetIO();
        if (on) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

private:
    template <typename MoveFn>
    void feed(float hostX, float hostY, MoveFn move) {
        accX += hostX / zoom;
        accY += hostY / zoom;
        const int dx = int(accX);
        const int dy = int(accY);
        if (dx || dy) {
            move(dx, dy);
            accX -= dx;
            accY -= dy;
        }
    }
};

// Two host keys may share one Mac transition code.  Reference counts keep a
// modifier down until its last physical host key is released.
class AdbKeyboard {
public:
    template <class MachineT, class TraceFn>
    void frame(MachineT& machine, TraceFn&& trace) {
        if (ImGui::GetIO().WantTextInput) return;
        dispatch(machine, trace, kKeys);
        dispatch(machine, trace, kKeypad);
    }

    // The original Mac II/IIfx and Duo tables predate keypad forwarding but
    // include Escape. Keep that exact host-key surface while still sharing
    // the transition/refcount implementation with the later runners.
    template <class MachineT, class TraceFn>
    void frameLegacy(MachineT& machine, TraceFn&& trace) {
        if (ImGui::GetIO().WantTextInput) return;
        dispatch(machine, trace, kKeys);
        static constexpr Key escape[] = {{ImGuiKey_Escape, 0x6B}};
        dispatch(machine, trace, escape);
    }

private:
    struct Key {
        ImGuiKey key;
        uint8_t m0110;
    };

    template <class MachineT, class TraceFn, size_t N>
    void dispatch(MachineT& machine, TraceFn&& trace,
                  const Key (&keys)[N]) {
        for (const Key& entry : keys) {
            if (keyDown(entry.m0110, entry.key)) {
                trace(uint8_t(entry.m0110 >> 1), true);
                machine.push({MachineT::Cmd::Key, entry.m0110 >> 1, 1});
            }
            if (keyUp(entry.m0110, entry.key)) {
                trace(uint8_t(entry.m0110 >> 1), false);
                machine.push({MachineT::Cmd::Key, entry.m0110 >> 1, 0});
            }
        }
    }

    inline static constexpr Key kKeys[] = {
        {ImGuiKey_A,0x01},{ImGuiKey_S,0x03},{ImGuiKey_D,0x05},{ImGuiKey_F,0x07},
        {ImGuiKey_H,0x09},{ImGuiKey_G,0x0B},{ImGuiKey_Z,0x0D},{ImGuiKey_X,0x0F},
        {ImGuiKey_C,0x11},{ImGuiKey_V,0x13},{ImGuiKey_B,0x17},{ImGuiKey_Q,0x19},
        {ImGuiKey_W,0x1B},{ImGuiKey_E,0x1D},{ImGuiKey_R,0x1F},{ImGuiKey_Y,0x21},
        {ImGuiKey_T,0x23},{ImGuiKey_1,0x25},{ImGuiKey_2,0x27},{ImGuiKey_3,0x29},
        {ImGuiKey_4,0x2B},{ImGuiKey_6,0x2D},{ImGuiKey_5,0x2F},{ImGuiKey_Equal,0x31},
        {ImGuiKey_9,0x33},{ImGuiKey_7,0x35},{ImGuiKey_Minus,0x37},{ImGuiKey_8,0x39},
        {ImGuiKey_0,0x3B},{ImGuiKey_RightBracket,0x3D},{ImGuiKey_O,0x3F},
        {ImGuiKey_U,0x41},{ImGuiKey_LeftBracket,0x43},{ImGuiKey_I,0x45},
        {ImGuiKey_P,0x47},{ImGuiKey_Enter,0x49},{ImGuiKey_L,0x4B},{ImGuiKey_J,0x4D},
        {ImGuiKey_Apostrophe,0x4F},{ImGuiKey_K,0x51},{ImGuiKey_Semicolon,0x53},
        {ImGuiKey_Backslash,0x55},{ImGuiKey_Comma,0x57},{ImGuiKey_Slash,0x59},
        {ImGuiKey_N,0x5B},{ImGuiKey_M,0x5D},{ImGuiKey_Period,0x5F},
        {ImGuiKey_Tab,0x61},{ImGuiKey_Space,0x63},{ImGuiKey_GraveAccent,0x65},
        {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},
        {ImGuiKey_RightSuper,0x6F},{ImGuiKey_LeftCtrl,0x6D},
        {ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
        {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},
        {ImGuiKey_RightAlt,0xF9},{ImGuiKey_RightCtrl,0xFB},
        {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
        {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
    };

    inline static constexpr Key kKeypad[] = {
        {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},
        {ImGuiKey_Keypad2,0xA8},{ImGuiKey_Keypad3,0xAA},
        {ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
        {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},
        {ImGuiKey_Keypad8,0xB6},{ImGuiKey_Keypad9,0xB8},
    };

    bool keyDown(uint8_t code, ImGuiKey key) {
        if (!ImGui::IsKeyPressed(key, false)) return false;
        return ++held_[code & 0x7F] == 1;
    }

    bool keyUp(uint8_t code, ImGuiKey key) {
        if (!ImGui::IsKeyReleased(key)) return false;
        uint8_t& count = held_[code & 0x7F];
        if (!count) return false;
        return --count == 0;
    }

    uint8_t held_[128]{};
};

struct DafbRunnerSpec {
    const char* name;
    const char* pramTag;
    const char* cpuLine;
    const char* lleFirmware;
    MachineKind kind;
    SnapMachine snap;
};

// Services is the explicit boundary back to the executable.  The runner
// needs qualify/checkOnly, wireNetwork, locate, installGlfwErrorCallback,
// configureOpenGl, prepareDriveSounds, bindCpuMenu, drawMachineMenu,
// requestRelaunch, traceKey and processRelaunch.
template <class MachineT, class Mem, class Cpu, class AudioHost,
          class SeedRtc, class Services>
int runDafbGui(Mem& mem, Cpu& cpu, AudioHost& audioHost,
               const DafbRunnerSpec& spec, bool lleActive,
               SeedRtc&& seedRtc, const std::string& romName,
               int argc, char** argv, Services& services) {
    if (!services.qualify(spec.name, spec.lleFirmware, lleActive, cpu))
        return 2;
    if (services.checkOnly()) return 0;
    cpu.hardReset();
    services.wireNetwork(mem);

    std::string hddPath =
        (argc > 2) ? argv[2] : services.locate("hdv/MacOS-8.1-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");

    std::string floppyPath;
    if (const char* env = std::getenv("POM68K_FLOPPY")) floppyPath = env;
    if (floppyPath.empty() && !hddOk)
        floppyPath = services.locate("disks35/Disk605.dsk");
    if (floppyPath.empty() && !hddOk)
        floppyPath = services.locate("disks35/quadra.img");
    static bool floppyOk = !floppyPath.empty() && mem.insertDisk(floppyPath);
    if (floppyOk) std::printf("Floppy: %s\n", floppyPath.c_str());

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        std::string arg = argv[i];
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
    ensureCdDrive(mem, extraDisks);

    static std::string pramPath =
        (hddPath.empty() ? std::string(spec.pramTag)
                         : hddPath + "." + spec.pramTag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    services.installGlfwErrorCallback();
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    const char* glslVersion = services.configureOpenGl();
    static std::string winTitle = std::string("POM68K — ") + spec.name;
    GLFWwindow* window =
        glfwCreateWindow(1320, 1080, winTitle.c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    services.prepareDriveSounds(mem, audioHost);
    mem.internalDrive().setWriteBack(
        std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");

    static MachineT machine{mem, cpu, audioHost};
    machine.state.kind = spec.snap;
    machine.state.path =
        pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    services.bindCpuMenu(machine, cpu);
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
        std::vector<std::string>& extraDisks;
        bool& floppyOk;
        DafbRunnerSpec spec;
        Services& services;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath,
                   floppyPath, extraDisks, floppyOk, spec, services};

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

        services.drawMachineMenu(context.spec.kind, context.window, [&] {
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
        });

        {
            static DiskBaysHost host = [&] {
                DiskBaysHost value;
                value.extras = &context.extraDisks;
                value.hardReset = [&] {
                    machine.push({MachineT::Cmd::HardReset});
                };
                value.bayIsCd = [&](int id) {
                    return machine.bayIsCdrom(id);
                };
                value.insertBay = [&](int id, const std::string& disk) {
                    if (!machine.bayIsCdrom(id)) return false;
                    machine.requestInsertBay(id, disk);
                    return true;
                };
                value.ejectBay = [&](int id) {
                    machine.requestEjectBay(id);
                };
                value.relaunch =
                    [&](const std::string& boot,
                        const std::vector<std::string>& extras) {
                        services.requestRelaunch(
                            context.window, context.romName, boot, extras);
                    };
                value.hasFloppyDrive = true;
                value.floppyInserted = [&] {
                    return machine.floppyInserted();
                };
                value.insertFloppy = [&](const std::string& disk) {
                    machine.requestInsertFloppy(disk);
                    context.floppyPath = disk;
                    context.floppyOk = true;
                };
                value.ejectFloppy = [&] {
                    machine.requestEjectFloppy();
                    context.floppyPath.clear();
                    context.floppyOk = false;
                };
                return value;
            }();
            host.romName = context.romName;
            host.bootPath = context.hddPath;
            host.floppyPath = machine.floppyPath();
            diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        dockLayoutScreenWindow(context.spec.name);
        ImGui::Begin(context.spec.name);
        static ScreenInput input;
        input.frame(
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

        static AdbKeyboard keyboard;
        keyboard.frame(machine, [&](uint8_t adb, bool down) {
            services.traceKey(adb, down);
        });

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const auto status = machine.status();
        ImGui::Text("%s  PC=%08X  clock=%lld", context.spec.cpuLine,
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
        if (ImGui::Checkbox("Turbo", &turbo))
            machine.turbo.store(turbo);
        ImGui::End();

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
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    services.processRelaunch(argv[0]);
#endif
    return 0;
}


// The three SonoraStyleMachine platforms share the complete GUI contract.
// A wrapper still owns model decoding and constructs its memory/CPU/video;
// everything visible to the runner is data.
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

/// mem/cpu/video are the caller's statics, already carrying the ROM. The
/// descriptor captures every GUI-visible variation; seedRtc initializes the
/// board-specific battery clock after PRAM has been loaded.
template <class MachineT, class Mem, class Cpu, class Video, class AudioHost,
          class SeedRtc, class Services>
int runSonoraGui(Mem& mem, Cpu& cpu, Video& video,
                 AudioHost& audioHost, const SonoraRunnerSpec& spec,
                 SeedRtc&& seedRtc, const std::string& romName,
                 int argc, char** argv, Services& services) {
    services.wireNetwork(mem);
    {
        const char* m = std::getenv("POM68K_MONITOR");
        mem.setMonitorSense(m ? (std::atoi(m) < 640 ? 2 : 6) : spec.monitorSense);
    }
    cpu.hardReset();

    std::string hddPath = (argc > 2) ? argv[2]
        : (spec.defaultHdd.empty() ? std::string() : services.locate(spec.defaultHdd));
    if (hddPath.empty()) hddPath = services.locate("hdv/lc3-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/HD20SC.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        // "cdbay" reserves an empty CD drive on the bus; a CD image creates
        // the same hot-swappable bay with media already inserted.
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (diskBaysPathIsCd(argv[i])) {
            if (mem.attachCdrom(argv[i], id)) {
                extraDisks.push_back(argv[i]);
                std::printf("SCSI CD %d: %s\n", id, argv[i]);
            } else std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id, argv[i]);
            continue;
        }
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }
    ensureCdDrive(mem, extraDisks);

    static std::string pramPath =
        (hddPath.empty() ? spec.pramTag : hddPath + "." + spec.pramTag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    services.installGlfwErrorCallback();
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = services.configureOpenGl();
    static const std::string winTitle = std::string("POM68K — ") + spec.name;
    GLFWwindow* window = glfwCreateWindow(1320, 1040, winTitle.c_str(),
                                          nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    services.prepareDriveSounds(mem, audioHost);
    // GUI floppies persist committed writes back to the image file on eject
    // and exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable write-back.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static MachineT machine{mem, cpu, video, audioHost};
    services.bindCpuMenu(machine, cpu);
    machine.state.kind = spec.snap;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; MachineT& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath, floppyPath;
        bool floppyOk = false;
        std::vector<std::string>& extraDisks;
        SonoraRunnerSpec spec;
        Services& services;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, {}, false,
                   extraDisks, spec, services};

    // Optional startup floppy; the Disques window can hot-swap it later.
    if (const char* env = std::getenv("POM68K_FLOPPY")) {
        if (mem.insertDisk(env)) {
            ctx.floppyPath = env;
            ctx.floppyOk = true;
            machine.setFloppyInserted(true, env);
            std::printf("Floppy: %s\n", env);
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

        services.drawMachineMenu(c.spec.kind, c.window, [&c] {
            diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({MachineT::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            const std::string ssMsg = c.m.state.message();
            if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
        });

        // Built once: all hooks capture the static context, which outlives
        // every frame and crosses mutations through MachineHost commands.
        {
            static DiskBaysHost host = [&c] {
                DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({MachineT::Cmd::HardReset}); };
                h.relaunch = [&c](const std::string& boot,
                                  const std::vector<std::string>& extras) {
                    c.services.requestRelaunch(
                        c.window, c.romName, boot, extras);
                };
                h.bayIsCd = [&c](int id) { return c.m.bayIsCdrom(id); };
                h.insertBay = [&c](int id, const std::string& d) {
                    if (!c.m.bayIsCdrom(id)) return false;
                    c.m.requestInsertBay(id, d);
                    return true;
                };
                h.ejectBay = [&c](int id) { c.m.requestEjectBay(id); };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d);
                    c.floppyPath = d;
                    c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy();
                    c.floppyPath.clear();
                    c.floppyOk = false;
                };
                return h;
            }();
            host.romName = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.m.floppyPath();
            diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        dockLayoutScreenWindow(c.spec.name.c_str());
        ImGui::Begin(c.spec.name.c_str());
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) {
                        c.m.push({MachineT::Cmd::MouseMove, dx, dy});
                    },
                    [&](int button, bool down) {
                        c.m.push({MachineT::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        ImGui::End();

        // Preserve the original Sonora behaviour: transitions were not
        // included in POM68K_KEY_TRACE on these three platforms.
        static AdbKeyboard keyboard;
        keyboard.frame(c.m, [](uint8_t, bool) {});

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
            if (ImGui::Checkbox("Turbo", &turbo)) c.m.turbo.store(turbo);

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
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    services.processRelaunch(argv[0]);
#endif
    return 0;
}


// Mac II-family and IIfx boards differ below the host boundary, but their
// Toby/NuBus GUI lifecycle is the same.  The descriptor retains the few
// user-visible historical differences, including IIfx's capitalized
// snapshot suffix and the SE/30's internal-video screen identity.
struct TobyRunnerSpec {
    std::string windowName;
    std::string screenName;
    std::string pramTag;
    std::string snapshotTag;
    std::vector<std::string> defaultHdds;
    MachineKind kind;
    SnapMachine snap;
    bool traceKeys;
};

// mem/cpu/audioHost are caller-owned statics whose ROM and video card have
// already been installed.  drawStatus owns only the board-specific rows of
// the CPU panel; this template owns all process/UI/media lifecycle around it.
template <class MachineT, class Mem, class Cpu, class AudioHost,
          class SeedRtc, class DrawStatus, class Services>
int runTobyGui(Mem& mem, Cpu& cpu, AudioHost& audioHost,
               const TobyRunnerSpec& spec, SeedRtc&& seedRtc,
               DrawStatus&& drawStatus, const std::string& romName,
               int argc, char** argv, Services& services) {
    cpu.hardReset();
    seedRtc();
    services.wireNetwork(mem);

    std::string hddPath = argc > 2 ? argv[2] : std::string();
    if (hddPath.empty()) {
        for (const std::string& candidate : spec.defaultHdds) {
            hddPath = services.locate(candidate);
            if (!hddPath.empty()) break;
        }
    }
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr,
                      "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    static std::string pramPath =
        (hddPath.empty() ? spec.pramTag : hddPath + "." + spec.pramTag) +
        ".pram";
    if (mem.loadPram(pramPath))
        std::printf("PRAM: %s\n", pramPath.c_str());

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        const int id = int(extraDisks.size()) + 1;
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (diskBaysPathIsCd(argv[i])) {
            if (mem.attachCdrom(argv[i], id)) {
                extraDisks.push_back(argv[i]);
                std::printf("SCSI CD %d: %s\n", id, argv[i]);
            } else {
                std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id,
                             argv[i]);
            }
            continue;
        }
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else {
            std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
        }
    }
    ensureCdDrive(mem, extraDisks);

    services.installGlfwErrorCallback();
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    const char* glslVersion = services.configureOpenGl();
    static const std::string windowTitle =
        std::string("POM68K — ") + spec.windowName;
    GLFWwindow* window = glfwCreateWindow(1320, 1040, windowTitle.c_str(),
                                          nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    services.prepareDriveSounds(mem, audioHost);
    mem.internalDrive().setWriteBack(
        std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");

    static MachineT machine{mem, cpu, audioHost};
    services.bindCpuMenu(machine, cpu);
    machine.state.kind = spec.snap;
    machine.state.path =
        (hddPath.empty() ? spec.snapshotTag
                         : hddPath + "." + spec.snapshotTag) + ".pomss";
    machine.publish(true);

    std::string floppyPath =
        std::getenv("POM68K_FLOPPY") ? std::getenv("POM68K_FLOPPY") : "";
    static bool floppyOk =
        !floppyPath.empty() && mem.insertDisk(floppyPath);
    if (floppyOk) std::printf("Floppy: %s\n", floppyPath.c_str());
    machine.setFloppyInserted(floppyOk, floppyPath);

    using StatusDrawer = std::decay_t<DrawStatus>;
    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint texture;
        ScreenInput input;
        std::string romName, hddPath, floppyPath;
        std::vector<std::string> extraDisks;
        bool& floppyOk;
        TobyRunnerSpec spec;
        StatusDrawer drawStatus;
        Services& services;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath,
                   floppyPath, extraDisks, floppyOk, spec,
                   std::forward<DrawStatus>(drawStatus), services};

    auto frame = [](void* opaque) {
        Ctx& c = *static_cast<Ctx*>(opaque);
        MachineT& machine = c.machine;
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        c.services.drawMachineMenu(c.spec.kind, c.window, [] {
            diskBaysMenuItem();
        });
        {
            static DiskBaysHost host = [&c] {
                DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] {
                    c.machine.push({MachineT::Cmd::HardReset});
                };
                h.relaunch = [&c](const std::string& boot,
                                  const std::vector<std::string>& extras) {
                    c.services.requestRelaunch(
                        c.window, c.romName, boot, extras);
                };
                h.bayIsCd = [&c](int id) {
                    return c.machine.bayIsCdrom(id);
                };
                h.insertBay = [&c](int id, const std::string& disk) {
                    if (!c.machine.bayIsCdrom(id)) return false;
                    c.machine.requestInsertBay(id, disk);
                    return true;
                };
                h.ejectBay = [&c](int id) {
                    c.machine.requestEjectBay(id);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] {
                    return c.machine.floppyInserted();
                };
                h.insertFloppy = [&c](const std::string& disk) {
                    c.machine.requestInsertFloppy(disk);
                    c.floppyPath = disk;
                    c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.machine.requestEjectFloppy();
                    c.floppyPath.clear();
                    c.floppyOk = false;
                };
                return h;
            }();
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

        static AdbKeyboard keyboard;
        keyboard.frameLegacy(machine, [&c](uint8_t adb, bool down) {
            if (c.spec.traceKeys) c.services.traceKey(adb, down);
        });

        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        c.drawStatus(machine);
        bool running = machine.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run"))
            machine.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            machine.push({MachineT::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = machine.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Turbo", &turbo)) machine.turbo.store(turbo);
        c.services.drawSaveState(machine.state);
        ImGui::End();

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
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    services.processRelaunch(argv[0]);
#endif
    return 0;
}


// The V8/Eagle/Spice/Tinker Bell profiles share one board facade and one
// lifecycle.  Two booleans are deliberately separate: fixed-display models
// skip the launch-time sense override, while the historical Mac TV panel
// still exposes the sense buttons even though its initial sense is fixed.
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
             int argc, char** argv, Services& services) {
    services.wireNetwork(mem);
    if (spec.setInitialMonitor) {
        const char* monitor = std::getenv("POM68K_MONITOR");
        mem.setMonitorSense(
            monitor && std::atoi(monitor) < 640 ? 2 : 6);
    }
    configureCpu();
    cpu.hardReset();

    std::string hddPath = argc > 2 ? argv[2]
        : services.locate("hdv/" + spec.pramTag + "-boot.vhd");
    if (hddPath.empty())
        hddPath = services.locate("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = services.locate("hdv/HD20SC.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk)
        std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else
        std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        const int id = int(extraDisks.size()) + 1;
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (diskBaysPathIsCd(argv[i])) {
            if (mem.attachCdrom(argv[i], id)) {
                extraDisks.push_back(argv[i]);
                std::printf("SCSI CD %d: %s\n", id, argv[i]);
            } else {
                std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id,
                             argv[i]);
            }
            continue;
        }
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else {
            std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
        }
    }
    ensureCdDrive(mem, extraDisks);

    static std::string pramPath =
        (hddPath.empty() ? spec.pramTag
                         : hddPath + "." + spec.pramTag) + ".pram";
    if (mem.loadPram(pramPath))
        std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    services.installGlfwErrorCallback();
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    const char* glslVersion = services.configureOpenGl();
    const std::string windowTitle = std::string("POM68K — ") + spec.name;
    GLFWwindow* window = glfwCreateWindow(1320, 1040, windowTitle.c_str(),
                                          nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    services.prepareDriveSounds(mem, audioHost);
    mem.internalDrive().setWriteBack(
        std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");

    static MachineT machine{mem, cpu, video, audioHost};
    services.bindCpuMenu(machine, cpu);
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
        std::vector<std::string>& extraDisks;
        V8RunnerSpec spec;
        Services& services;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, {},
                   false, extraDisks, spec, services};

    if (const char* floppy = std::getenv("POM68K_FLOPPY")) {
        if (mem.insertDisk(floppy)) {
            ctx.floppyPath = floppy;
            ctx.floppyOk = true;
            machine.setFloppyInserted(true, floppy);
            std::printf("Floppy: %s\n", floppy);
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

        c.services.drawMachineMenu(c.spec.kind, c.window, [&c] {
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
            static DiskBaysHost host = [&c] {
                DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] {
                    c.machine.push({MachineT::Cmd::HardReset});
                };
                h.relaunch = [&c](const std::string& boot,
                                  const std::vector<std::string>& extras) {
                    c.services.requestRelaunch(
                        c.window, c.romName, boot, extras);
                };
                h.bayIsCd = [&c](int id) {
                    return c.machine.bayIsCdrom(id);
                };
                h.insertBay = [&c](int id, const std::string& disk) {
                    if (!c.machine.bayIsCdrom(id)) return false;
                    c.machine.requestInsertBay(id, disk);
                    return true;
                };
                h.ejectBay = [&c](int id) {
                    c.machine.requestEjectBay(id);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] {
                    return c.machine.floppyInserted();
                };
                h.insertFloppy = [&c](const std::string& disk) {
                    c.machine.requestInsertFloppy(disk);
                    c.floppyPath = disk;
                    c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.machine.requestEjectFloppy();
                    c.floppyPath.clear();
                    c.floppyOk = false;
                };
                return h;
            }();
            host.romName = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = machine.floppyPath();
            diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        dockLayoutScreenWindow(c.spec.name.c_str());
        ImGui::Begin(c.spec.name.c_str());
        static ScreenInput input;
        input.frame(c.window, c.texture,
                    ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) {
                        machine.push({MachineT::Cmd::MouseMove, dx, dy});
                    },
                    [&](int button, bool down) {
                        machine.push({MachineT::Cmd::MouseButton, button,
                                      down ? 1 : 0});
                    });
        ImGui::End();

        static AdbKeyboard keyboard;
        keyboard.frame(machine, [&c](uint8_t adb, bool down) {
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
        if (ImGui::Checkbox("Turbo", &turbo)) machine.turbo.store(turbo);

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
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    services.processRelaunch(argv[0]);
#endif
    return 0;
}


// The Duo is deliberately not forced into the floppy-capable desktop
// contract. PG&E owns RTC/PRAM/input, and MscMemory has neither an internal
// floppy nor an empty/live CD target. This specification keeps its identity
// data while the template preserves those negative capabilities.
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
              const std::string& romName, int argc, char** argv,
              Services& services) {
    cpu.hardReset();
    services.wireNetwork(mem);

    std::string hddPath = argc > 2 ? argv[2]
        : services.locate(spec.defaultHdd);
    if (hddPath.empty()) hddPath = services.locate("hdv/boot.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr,
                      "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        const int id = int(extraDisks.size()) + 1;
        if (diskBaysPathIsCd(argv[i])) {
            if (mem.attachCdrom(argv[i], id)) {
                extraDisks.push_back(argv[i]);
                std::printf("SCSI CD %d: %s\n", id, argv[i]);
            } else {
                std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id,
                             argv[i]);
            }
            continue;
        }
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else {
            std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
        }
    }
    ensureCdDrive(mem, extraDisks);

    // Preserve the historical empty-boot name: duo230.duo230.pram rather
    // than the single-tag shape used by the desktop families.
    static std::string pramPath =
        (hddPath.empty() ? std::string(spec.pramTag) : hddPath) + "." +
        spec.pramTag + ".pram";
    if (mem.loadPram(pramPath))
        std::printf("PRAM: %s\n", pramPath.c_str());
    seedRtc();

    services.installGlfwErrorCallback();
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    const char* glslVersion = services.configureOpenGl();
    const std::string title = std::string("POM68K — ") + spec.name;
    GLFWwindow* window = glfwCreateWindow(1320, 1000, title.c_str(),
                                          nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // MscMemory intentionally has no attachDriveSounds(): there is no
    // internal floppy. The shared audio host still owns ASC output and the
    // process-global drive-sound menu state.
    services.prepareAudioHost(audioHost);
    if (!audioHost.start())
        std::fprintf(stderr, "audio: no output device (silent)\n");

    static MachineT machine{mem, cpu, audioHost};
    machine.state.kind = spec.snap;
    machine.state.path =
        pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    services.bindCpuMenu(machine, cpu);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window;
        MachineT& machine;
        GLuint texture;
        ScreenInput input;
        std::string romName, hddPath;
        std::vector<std::string>& extraDisks;
        DuoRunnerSpec spec;
        Services& services;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath,
                   extraDisks, spec, services};

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

        c.services.drawMachineMenu(c.spec.kind, c.window, [&c] {
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
            static DiskBaysHost host = [&c] {
                DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] {
                    c.machine.push({MachineT::Cmd::HardReset});
                };
                h.relaunch = [&c](const std::string& boot,
                                  const std::vector<std::string>& extras) {
                    c.services.requestRelaunch(
                        c.window, c.romName, boot, extras);
                };
                h.hasFloppyDrive = false;
                h.supportsEmptyCdDrive = false;
                return h;
            }();
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

        static AdbKeyboard keyboard;
        keyboard.frameLegacy(machine, [&c](uint8_t adb, bool down) {
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
        if (ImGui::Checkbox("Turbo", &turbo)) machine.turbo.store(turbo);
        c.services.drawSaveState(machine.state);
        ImGui::End();

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
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    services.processRelaunch(argv[0]);
#endif
    return 0;
}


} // namespace pom68k::gui
