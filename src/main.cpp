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

    // Floppy: argv[2], else first image in disks35/
    std::string diskPath = (argc > 2) ? argv[2] : "";
    if (diskPath.empty())
        for (const char* p : { "disks35/Disk605.dsk", "../disks35/Disk605.dsk" })
            if (std::ifstream(p, std::ios::binary)) { diskPath = p; break; }
    if (!diskPath.empty() && mem.insertDisk(diskPath))
        std::printf("Floppy: %s\n", diskPath.c_str());

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
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(20, 740), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("68000 @ 7.8336 MHz (Moira, cycle-exact)  PC=%06X  clock=%lld",
                    c.cpu.getPC(), (long long)c.cpu.getClock());
        ImGui::Text("overlay=%d  demo=%d", c.mem.overlay() ? 1 : 0, demoMode ? 1 : 0);
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
