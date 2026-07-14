// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M3 shell: run the 68000 (Moira) against the Mac Plus memory map and display
// the 512×342 framebuffer in an ImGui window. Structure mirrors POMIIGS's
// main.cpp so it grows into the same shape (Ui class, audio, disks later).

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include "DemoRom.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdint>
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

int main(int argc, char** argv) {
    std::printf("POM68K — Macintosh 68k emulator (Mac Plus)\n");

    // ── Emulator (static: outlives main() under Emscripten) ─────────────
    static MacMemory mem;
    static Cpu68k cpu(mem);
    static MacVideo video;

    std::string matched;
    std::vector<uint8_t> rom;
    if (argc > 1) { rom = readFile(argv[1]); matched = argv[1]; }
    else rom = findResource("roms/macplus.rom", matched);
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
    else if (!demoMode)
        std::fprintf(stderr, "No floppy found — drop a .dsk in disks35/ (looked "
                     "relative to CWD and the executable). Booting to the ?-disk.\n");

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
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    struct Ctx {
        GLFWwindow* window; MacMemory& mem; Cpu68k& cpu; MacVideo& video;
        GLuint tex; bool running; bool turbo; MacFrameClock clock;
    };
    static Ctx ctx{window, mem, cpu, video, screenTex, true, !demoMode, {}};
    ctx.clock.resync(cpu);

    auto frame = [](void* p) {
        Ctx& c = *static_cast<Ctx*>(p);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        if (c.running) {
            int n = c.turbo ? 8 : 1;            // turbo: 8 machine frames per host frame
            for (int i = 0; i < n; i++) c.clock.runFrame(c.cpu, c.mem);
        }

        const uint32_t* fb = c.video.render(c.mem);
        glBindTexture(GL_TEXTURE_2D, c.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, c.video.width(), c.video.height(),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, fb);

        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::Begin("Macintosh Plus", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Image(ImTextureID(intptr_t(c.tex)),
                     ImVec2(float(c.video.width() * 2), float(c.video.height() * 2)));
        // Mouse → quadrature while hovering the Mac screen (2x scaled)
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemHovered()) {
            static float accX = 0, accY = 0;
            accX += io.MouseDelta.x / 2.0f;
            accY += io.MouseDelta.y / 2.0f;
            int dx = int(accX), dy = int(accY);
            if (dx || dy) { c.mem.mouse().move(dx, dy); accX -= dx; accY -= dy; }
            c.mem.mouse().setButton(io.MouseDown[0]);
        }
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
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
    return 0;
}
