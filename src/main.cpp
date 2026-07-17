// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M3 shell: run the 68000 (Moira) against the Mac Plus memory map and display
// the 512×342 framebuffer in an ImGui window. Structure mirrors POMIIGS's
// main.cpp so it grows into the same shape (Ui class, audio, disks later).
// O6: a 512 KB ROM selects the Mac LC II machine (V8 + 68030) instead.

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include "MacAudio.h"
#include "MacAudioHost.h"
#include "DemoRom.h"
#include "Cpu030.h"
#include "V8Memory.h"
#include "V8Video.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string execDir() {
#ifdef __linux__
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) { buf[n] = 0; std::string p(buf); auto s = p.find_last_of('/'); if (s != std::string::npos) return p.substr(0, s + 1); }
#endif
    return {};
}

static std::vector<uint8_t> findResource(const std::string& rel, std::string& matched) {
    std::string ed = execDir();
    for (const std::string& base : { std::string(), ed, ed + "../" }) {
        std::string p = base + rel;
        auto data = readFile(p);
        if (!data.empty()) { matched = p; return data; }
    }
    matched = rel;
    return {};
}

// Resolve a path (CWD / exec dir / parent) without reading the file.
static std::string findPath(const std::string& rel) {
    std::string ed = execDir();
    for (const std::string& base : { std::string(), ed, ed + "../" }) {
        std::ifstream f(base + rel, std::ios::binary);
        if (f) return base + rel;
    }
    return {};
}

static void glfwErrorCallback(int e, const char* d) { std::fprintf(stderr, "GLFW error %d: %s\n", e, d); }

// ── Emulated-screen input (shared by the Plus and LC II loops) ──────────
// The screen is an InvisibleButton (the image is drawn over it): a drag
// STARTED on the Mac screen owns the mouse until release, so Finder
// drag-and-drop keeps tracking when the pointer leaves the item and the
// ImGui window never moves from a drag inside it (only its title bar
// moves it — ConfigWindowsMoveFromTitleBarOnly). The Delete key toggles
// a hard capture: GLFW disabled cursor (raw deltas, no window edges),
// ImGui mouse off so clicks can't leak into widgets; Delete releases.
struct ScreenInput {
    bool captured = false;
    float accX = 0, accY = 0;            // sub-pixel remainder (2x zoom)
    double lastX = 0, lastY = 0;         // virtual cursor while captured

    template <typename MoveFn, typename ButtonFn>
    void frame(GLFWwindow* win, GLuint tex, ImVec2 size,
               MoveFn move, ButtonFn button) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("screen", size);
        ImGui::GetWindowDrawList()->AddImage(
            ImTextureID(intptr_t(tex)), p, ImVec2(p.x + size.x, p.y + size.y));

        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            setCaptured(win, !captured);

        if (captured) {                  // raw deltas from the virtual cursor
            double x, y;
            glfwGetCursorPos(win, &x, &y);
            feed(float(x - lastX), float(y - lastY), move);
            lastX = x; lastY = y;
            button(glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        } else if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            feed(io.MouseDelta.x, io.MouseDelta.y, move);
            button(io.MouseDown[0]);     // release seen while still active
        }
    }

    void setCaptured(GLFWwindow* win, bool on) {
        captured = on;
        glfwSetInputMode(win, GLFW_CURSOR,
                         on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (on) glfwGetCursorPos(win, &lastX, &lastY);
        ImGuiIO& io = ImGui::GetIO();
        if (on) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        else    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

private:
    template <typename MoveFn>
    void feed(float hx, float hy, MoveFn move) {
        accX += hx / 2.0f;               // screen shown at 2x
        accY += hy / 2.0f;
        int dx = int(accX), dy = int(accY);
        if (dx || dy) { move(dx, dy); accX -= dx; accY -= dy; }
    }
};

// ── Machine profile menu ────────────────────────────────────────────────
// Main-menu-bar "Machine": pick the Mac Plus or Mac LC II profile.
// Selecting the other machine relaunches the process on its ROM — clean
// state, since each machine is built once at startup.
static std::string gSwitchRom;

static void machineMenu(bool isLcII, GLFWwindow* window) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Machine")) {
        struct Profile { const char* label; bool cur; const char* rom[2]; };
        const Profile kProfiles[] = {
            { "Macintosh Plus", !isLcII, { "roms/macplus.rom", nullptr } },
            { "Macintosh LC II", isLcII, { "roms/maclcii.rom",
              "docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM" } },
        };
        for (const Profile& pr : kProfiles) {
            std::string path = findPath(pr.rom[0]);
            if (path.empty() && pr.rom[1]) path = findPath(pr.rom[1]);
            if (ImGui::MenuItem(pr.label, nullptr, pr.cur,
                                pr.cur || !path.empty()) && !pr.cur) {
                gSwitchRom = path;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("|  Delete: capture mouse");
    ImGui::EndMainMenuBar();
}

// Relaunch on the ROM the menu picked (no-op when none was).
static void relaunchIfSwitched(char* argv0) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (gSwitchRom.empty()) return;
    char* args[] = { argv0, const_cast<char*>(gSwitchRom.c_str()), nullptr };
    ::execv("/proc/self/exe", args);
    std::fprintf(stderr, "relaunch failed — start manually: %s \"%s\"\n",
                 argv0, gSwitchRom.c_str());
#else
    (void)argv0;
#endif
}

// ── Mac LC II (O6): V8 + 68030, selected by a 512 KB ROM ────────────────
// Pre-Ui-class shape like the Plus loop below; the shared boilerplate
// folds into a Ui class with the backlog item.
static int runLcII(std::vector<uint8_t> rom, const std::string& romName,
                   int argc, char** argv) {
    std::printf("Machine: Macintosh LC II (68030 @ 15.6672 MHz, V8%s)\n",
                getenv("POM68K_NOFPU") ? "" : ", 68882");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static V8Memory mem;
    // The LC II's 68882 is a PDS option; this build defaults it ON —
    // the target disks (and much LC II-era software) issue FPU ops and
    // fault with "system error 10" (Line-F) on a no-FPU machine. Set
    // POM68K_NOFPU to model a bare LC II.
    static Cpu030 cpu(mem, getenv("POM68K_NOFPU") == nullptr);
    static V8Video video(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    // Monitor sense (resolution): the GUI defaults to 640×480 13"/14" RGB —
    // the roomiest built-in mode, and some software (Lode Runner) needs a
    // ≥640×400 screen. POM68K_MONITOR=512 forces the 512×384 12" RGB mode;
    // also switchable live from the CPU window. Only these two — the LC II
    // built-in video can't do more (512KB VRAM, V8 bandwidth); the 640×870
    // portrait needs a wider framebuffer than the V8 provides. (Tests keep
    // V8Memory's own 512×384 default; only this GUI path picks 640.)
    {
        const char* m = getenv("POM68K_MONITOR");
        mem.setMonitorSense((m && atoi(m) < 640) ? 2 : 6);
    }
    // Diagnostic Line-F logger: POM68K_FPU_LOG=<file> makes the CPU single
    // -step and dump the instruction ring + full register set on the first
    // Line-F (the SimCity-2000 "coprocesseur absent" crash). Slower but
    // playable; leave unset for normal use.
    if (const char* lg = getenv("POM68K_FPU_LOG")) {
        cpu.enableFpuLog(lg);
        std::printf("Line-F log: %s (CPU single-steps — slower)\n", lg);
    }
    cpu.hardReset();

    // GISTPERSO-boot.vhd = the user's real LC II volume wrapped with a
    // DDM + $6A driver entry (tools/wrap_hfs.py) — bare HFS images are
    // not bootable and the ROM shows the blinking ? forever.
    std::string hddPath = (argc > 2) ? argv[2] : findPath("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/HD20SC.vhd");
    // Write-back ON in the GUI: the machine is a daily driver — saves made
    // inside the emulated Mac must survive the session. Tests attach
    // read-only so reference images are never modified.
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");

    // Battery-backed PRAM+clock: a cold PRAM triggers the ROM's long
    // full-RAM burn-in on every boot — persist it like a real battery.
    static std::string pramPath =
        (hddPath.empty() ? std::string("lcii") : hddPath) + ".pram";
    if (mem.egret().loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // First boot / stale battery file: seed the Basilisk II known-good
    // XPRAM defaults instead of an all-zero PRAM (no-op once the system
    // software's 'NuMc' signature is present)
    mem.egret().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // Sized so the largest mode (640×480 shown at 2×) fits with the menu
    // bar and the CPU window; the smaller 512×384 leaves margin.
    GLFWwindow* window = glfwCreateWindow(1320, 1040, "POM68K — Macintosh LC II", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    struct Ctx {
        GLFWwindow* window; V8Memory& mem; Cpu030& cpu; V8Video& video;
        MacAudioHost& audioHost; GLuint tex; bool running; bool turbo;
        std::vector<uint32_t> fb; std::vector<float> samp;
    };
    static Ctx ctx{window, mem, cpu, video, audioHost, screenTex, true, true, {}, {}};

    auto frame = [](void* p) {
        Ctx& c = *static_cast<Ctx*>(p);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        if (c.running) {
            // 512×384 frame = 640×407 dots at the CPU clock (60.15 Hz).
            static constexpr int kFrame = 640 * 407;
            auto runOne = [&c] {
                if (c.mem.cpuHeld()) c.mem.tick(kFrame);   // Egret power-on hold
                else c.cpu.runCycles(kFrame);
            };
            // Drain the ASC samples produced by the last slice (22 257 Hz
            // mono, continuous — an empty FIFO repeats its stale byte) and
            // report whether they carry real sound (AC span, same gate as
            // MacAudioHost::pushFrame).
            auto drain = [&c] {
                c.samp.clear();
                while (c.mem.asc().available() > 0)
                    c.samp.push_back(float(c.mem.asc().pop()) / 32768.0f);
                float lo = 1.f, hi = -1.f;
                for (float v : c.samp) { if (v < lo) lo = v; if (v > hi) hi = v; }
                return !c.samp.empty() && hi - lo >= 0.02f;
            };
            // Audio-clocked pacing (TODO § sound tempo wobble): while the
            // guest streams sound, the emulation speed IS the tempo, so it
            // must track the host DAC, not the host CPU. When sound was
            // heard recently (activeHold), each GUI tick emulates just
            // enough frames to keep the host ring near ~100 ms — the DAC's
            // 22 254 Hz consumption paces the machine at real time and
            // absorbs the vsync-60.00 vs frame-60.15 drift with no
            // resampler. Silence between notes is pushed too (pushRaw): it
            // is part of the musical timeline. When no sound plays, the
            // old time-budgeted turbo runs (fast boot/Finder, gated push
            // keeps the ring free of silence).
            static int activeHold = 0;   // GUI frames of sound-recent state
            static int starve = 0;       // safety against a dead DAC
            const size_t kTarget = 2225;                    // ~100 ms queued
            if (activeHold > 0 && c.audioHost.started()) {
                int n = 0;
                while (c.audioHost.buffered() < kTarget && n < 8) {
                    runOne();
                    if (drain()) activeHold = 90; else activeHold--;
                    c.audioHost.pushRaw(c.samp, 0);
                    n++;
                }
                // Ring at target: real time says "no frame due yet" (a
                // >60 fps GUI tick, or the 60.15/60.00 drift catching up).
                // Skip emulation this tick — unless the DAC stopped
                // consuming entirely (unplugged device): then force one
                // frame every 10 ticks so the machine never freezes.
                if (n == 0 && ++starve > 10) {
                    runOne();
                    if (drain()) activeHold = 90; else activeHold--;
                    starve = 0;
                } else if (n > 0) starve = 0;
            } else {
                // Time-budgeted turbo: the 68030 core runs ~1.4× real time
                // on a good host core, so a fixed ×8 would spend ~100 ms
                // per GUI frame and drop the UI (and the mouse) to ~10
                // fps. Run one frame slice, then let turbo keep emulating
                // only while the 16.6 ms vsync budget has room.
                auto slice0 = std::chrono::steady_clock::now();
                int n = 0;
                do {
                    runOne();
                } while (c.turbo && ++n < 8 &&
                         std::chrono::steady_clock::now() - slice0 <
                             std::chrono::milliseconds(10));
                if (drain()) {
                    activeHold = 90;                        // sound starts:
                    c.audioHost.pushFrame(c.samp, 0);       // switch to pacing
                }
            }
        }

        int hres, vres;
        V8Video::resolution(c.mem.monitorSense(), hres, vres);
        c.video.decode(c.fb);
        // decode() packs 00RRGGBB — alpha 0. ImGui renders textures with
        // alpha blending on, so a 0 alpha draws fully transparent (black
        // window background); force A=$FF before the BGRA upload.
        for (uint32_t& px : c.fb) px |= 0xFF000000u;
        glBindTexture(GL_TEXTURE_2D, c.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hres, vres, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, c.fb.data());

        machineMenu(true, c.window);

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin("Macintosh LC II", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.mem.adb().mouseMove(dx, dy); },
                    [&](bool down) { c.mem.adb().mouseButton(down); });
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

        // Keyboard → ADB key codes (= M0110 transition code >> 1, same
        // physical layout; DEV.md § Input key table)
        if (!io.WantTextInput) {
            static const struct { ImGuiKey k; uint8_t m0110; } kKeys[] = {
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_LeftShift,0x71},
                {ImGuiKey_RightShift,0x71},{ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},
                // Arrow keys (ADB raw $3B-$3E → m0110 = code<<1) — games like
                // Lode Runner drive the character with these; absent before.
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                // Numeric keypad (ADB raw $52-$5C → m0110 = code<<1) — some
                // games use it to move instead of the arrows.
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (ImGui::IsKeyPressed(e.k, false)) c.mem.adb().keyEvent(uint8_t(e.m0110 >> 1), true);
                if (ImGui::IsKeyReleased(e.k)) c.mem.adb().keyEvent(uint8_t(e.m0110 >> 1), false);
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 830), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("68030 @ 15.6672 MHz (Moira + PMMU)  PC=%08X  clock=%lld",
                    c.cpu.getPC(), (long long)c.cpu.getClock());
        ImGui::Text("overlay=%d  config=$%02X  MMU=%s  held=%d",
                    c.mem.overlay() ? 1 : 0, c.mem.ramConfig(),
                    (c.cpu.getTC() & 0x80000000) ? "on" : "off",
                    c.mem.cpuHeld() ? 1 : 0);
        if (ImGui::Button(c.running ? "Pause" : "Run")) c.running = !c.running;
        ImGui::SameLine();
        if (ImGui::Button("Reset")) c.cpu.hardReset();
        ImGui::SameLine();
        ImGui::Checkbox("Turbo", &c.turbo);      // as fast as the host allows (≤×8)

        // Monitor sense = the ID resistors on a real Mac's video connector;
        // the ROM reads it at reset to pick the resolution. Switching it is
        // like plugging in a different monitor, so it takes a Mac reset. The
        // LC II's built-in V8 video only drives these two color modes (512KB
        // VRAM + V8 bandwidth); depth is per-monitor, so a fresh mode may
        // come up B&W until you set "256 couleurs" in Moniteurs + restart.
        int sense = c.mem.monitorSense();
        ImGui::Text("Moniteur:");
        ImGui::SameLine();
        auto monoBtn = [&](const char* label, int s) {
            bool cur = sense == s;
            if (cur) ImGui::PushStyleColor(ImGuiCol_Button,
                                           ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label) && !cur) { c.mem.setMonitorSense(uint8_t(s)); c.cpu.hardReset(); }
            if (cur) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        monoBtn("512x384", 2);
        monoBtn("640x480", 6);
        ImGui::TextDisabled("(redemarre le Mac)");
        ImGui::End();

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(c.window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(c.window);
    };

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frame, &ctx, 0, 1);
#else
    while (!glfwWindowShouldClose(window)) frame(&ctx);
    mem.egret().savePram(pramPath);
    audioHost.stop();
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    relaunchIfSwitched(argv[0]);         // menu picked the other machine
#endif
    return 0;
}

int main(int argc, char** argv) {
    std::printf("POM68K — Macintosh 68k emulator (Mac Plus)\n");

    // ── Emulator (static: outlives main() under Emscripten) ─────────────
    static MacMemory mem;
    static Cpu68k cpu(mem);
    static MacVideo video;
    static MacAudio audio;
    static MacAudioHost audioHost;

    std::string matched;
    std::vector<uint8_t> rom;
    if (argc > 1) { rom = readFile(argv[1]); matched = argv[1]; }
    else {
        rom = findResource("roms/macplus.rom", matched);
        if (rom.empty()) rom = findResource("roms/maclcii.rom", matched);
    }

    // 512 KB ROM = Mac LC II (O6); 128 KB = Mac Plus
    if (rom.size() == V8Memory::kRomSize) return runLcII(std::move(rom), matched, argc, argv);

    static bool demoMode = rom.empty() || !mem.loadRom(rom);
    if (demoMode) {
        mem.installRom(kDemoRom, kDemoRomSize);
        std::printf("No Mac Plus ROM — running built-in 68000 demo. "
                    "Drop macplus.rom (128K) in roms/ for the real thing.\n");
    } else {
        std::printf("Loaded ROM: %s (%zu KB)\n", matched.c_str(), rom.size() / 1024);
    }
    mem.setCpu(&cpu);
    cpu.hardReset();

    // Floppy: argv[2], else probe disks35/ (CWD, exec dir, and its parent —
    // same resolution as the ROM, so it works whatever the launch directory).
    std::string diskPath = (argc > 2) ? argv[2] : findPath("disks35/Disk605.dsk");
    static bool diskOk = !diskPath.empty() && mem.insertDisk(diskPath);
    if (diskOk) std::printf("Floppy: %s\n", diskPath.c_str());

    // SCSI hard disk: argv[3], else probe hdv/HD20SC.vhd (exec-relative).
    std::string hddPath = (argc > 3) ? argv[3] : findPath("hdv/HD20SC.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (%u blocks, write-back)\n", hddPath.c_str(), mem.scsiDisk().blocks());
    if (!diskOk && !hddOk && !demoMode)
        std::fprintf(stderr, "No boot media — drop a .dsk in disks35/ or a .vhd in "
                     "hdv/ (looked relative to CWD and the executable).\n");

    // ── Window / ImGui ───────────────────────────────────────────────────
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1100, 800, "POM68K — Macintosh Plus", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Windows move only by their title bar — so dragging inside the Mac
    // screen (Finder drag-and-drop) doesn't drag the host window.
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    struct Ctx {
        GLFWwindow* window; MacMemory& mem; Cpu68k& cpu; MacVideo& video;
        MacAudio& audio; MacAudioHost& audioHost;
        GLuint tex; bool running; bool turbo; MacFrameClock clock;
    };
    static Ctx ctx{window, mem, cpu, video, audio, audioHost, screenTex, true, !demoMode, {}};
    ctx.clock.resync(cpu);

    auto frame = [](void* p) {
        Ctx& c = *static_cast<Ctx*>(p);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        if (c.running) {
            int n = c.turbo ? 8 : 1;            // turbo: 8 machine frames per host frame
            std::vector<float> samp;
            for (int i = 0; i < n; i++) {
                c.clock.runFrame(c.cpu, c.mem);
                samp.clear();
                c.audio.renderFrame(c.mem, samp);   // 370 PWM samples
                c.audioHost.pushFrame(samp, 0);     // plays only non-silent frames
            }
        }

        const uint32_t* fb = c.video.render(c.mem);
        glBindTexture(GL_TEXTURE_2D, c.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, c.video.width(), c.video.height(),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, fb);

        machineMenu(false, c.window);

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin("Macintosh Plus", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        // Mouse → quadrature: hover/drag on the screen, or Delete-key capture
        static ScreenInput input;
        input.frame(c.window, c.tex,
                    ImVec2(float(c.video.width() * 2), float(c.video.height() * 2)),
                    [&](int dx, int dy) { c.mem.mouse().move(dx, dy); },
                    [&](bool down) { c.mem.mouse().setButton(down); });
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

        // Keyboard → M0110 transition codes (DEV.md § Input key table)
        if (!io.WantTextInput) {
            static const struct { ImGuiKey k; uint8_t code; } kKeys[] = {
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_LeftShift,0x71},
                {ImGuiKey_RightShift,0x71},{ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},
            };
            for (auto& e : kKeys) {
                if (ImGui::IsKeyPressed(e.k, false)) c.mem.keyboard().enqueue(e.code);
                if (ImGui::IsKeyReleased(e.k)) c.mem.keyboard().enqueue(uint8_t(e.code | 0x80));
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 740), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("68000 @ 7.8336 MHz (Moira, cycle-exact)  PC=%06X  clock=%lld",
                    c.cpu.getPC(), (long long)c.cpu.getClock());
        ImGui::Text("overlay=%d  demo=%d  floppy=%s", c.mem.overlay() ? 1 : 0,
                    demoMode ? 1 : 0, diskOk ? "inserted" : "none");
        if (ImGui::Button(c.running ? "Pause" : "Run")) c.running = !c.running;
        ImGui::SameLine();
        if (ImGui::Button("Reset")) { c.cpu.hardReset(); c.clock.resync(c.cpu); }
        ImGui::SameLine();
        ImGui::Checkbox("Turbo x8", &c.turbo);  // the 4 MB RAM test takes ~45 s at 1x
        ImGui::End();

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(c.window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(c.window);
    };

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frame, &ctx, 0, 1);
#else
    while (!glfwWindowShouldClose(window)) frame(&ctx);
    audioHost.stop();
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    relaunchIfSwitched(argv[0]);         // menu picked the other machine
#endif
    return 0;
}
