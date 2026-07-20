// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M3 shell: run the 68000 (Moira) against the Mac Plus memory map and display
// the 512×342 framebuffer in an ImGui window. Structure mirrors POMIIGS's
// main.cpp so it grows into the same shape (Ui class, audio, disks later).
// O6: a 512 KB ROM selects the Mac LC II machine (V8 + 68030); Q6: a 1 MB
// ROM selects the LC 475 / Quadra 605 machine (MEMCjr/PrimeTime + 68LC040).

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
#include "Cpu040.h"
#include "Q605Memory.h"
#include "Cpu020.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
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

// Locate a ROM by a stable signature substring in its filename — the CRC32
// hex that Apple ROM dumps are named with (e.g. "35C28F5F" = Mac LC II,
// "FF7439EE" = Quadra 605). Scanning the roms/ tree for the signature avoids
// hardcoding the exact dated filename and subdirectory, and disambiguates
// same-size ROMs (a 512K IIfx dump ≠ the LC II). Returns "" if none match.
static std::string findRomBySignature(const std::string& sig) {
    namespace fs = std::filesystem;
    // Cached per signature: the menu resolves this every frame, and the roms/
    // tree doesn't change during a session — never rescan on the hot path.
    static std::map<std::string, std::string> cache;
    auto hit = cache.find(sig);
    if (hit != cache.end()) return hit->second;

    std::string want = sig;
    for (char& c : want) c = char(toupper(c));
    std::string ed = execDir();
    std::string found;
    for (const std::string& base : { std::string("roms"), ed + "roms", ed + "../roms" }) {
        std::error_code ec;
        if (!fs::is_directory(base, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(base, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string name = it->path().filename().string();
            for (char& c : name) c = char(toupper(c));
            if (name.find(want) != std::string::npos) { found = it->path().string(); break; }
        }
        if (!found.empty()) break;
    }
    cache[sig] = found;
    return found;
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
// Main-menu-bar "Machine": pick Plus / Mac II / LC II / Quadra 605.
// Selecting another machine relaunches the process on its ROM — clean
// state, since each machine is built once at startup (ROM size alone
// selects the machine in main()).
enum class MachineKind { Plus, MacII, LcII, Quadra };
static std::vector<std::string> gSwitchArgs;   // argv[1..] for the relaunch

static void machineMenu(MachineKind cur, GLFWwindow* window,
                        const std::function<void()>& extraMenus = {}) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Machine")) {
        // rom = canonical short name (a convenience symlink); sig = the CRC32
        // signature scanned for under roms/ when the short name is absent, so
        // the exact dated filename is never hardcoded.
        struct Profile { const char* label; MachineKind kind; const char* rom; const char* sig; };
        const Profile kProfiles[] = {
            { "Macintosh Plus", MachineKind::Plus, "roms/macplus.rom", nullptr },
            { "Macintosh II", MachineKind::MacII, "roms/macii.rom", "9779D2C4" },
            { "Macintosh LC II", MachineKind::LcII, "roms/maclcii.rom", "35C28F5F" },
            { "Quadra 605 / LC 475", MachineKind::Quadra, "roms/quadra605.rom", "FF7439EE" },
        };
        for (const Profile& pr : kProfiles) {
            bool isCur = pr.kind == cur;
            std::string path = findPath(pr.rom);
            if (path.empty() && pr.sig) path = findRomBySignature(pr.sig);
            if (ImGui::MenuItem(pr.label, nullptr, isCur,
                                isCur || !path.empty()) && !isCur) {
                gSwitchArgs = { path };
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        ImGui::EndMenu();
    }
    if (extraMenus) extraMenus();
    ImGui::TextDisabled("|  Delete: capture mouse");
    ImGui::EndMainMenuBar();
}

// Relaunch on the argument list the menu picked (no-op when none was).
static void relaunchIfSwitched(char* argv0) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (gSwitchArgs.empty()) return;
    std::vector<char*> args = { argv0 };
    for (const std::string& a : gSwitchArgs)
        args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    ::execv("/proc/self/exe", args.data());
    std::fprintf(stderr, "relaunch failed — start manually: %s \"%s...\"\n",
                 argv0, gSwitchArgs[0].c_str());
#else
    (void)argv0;
#endif
}

// List floppy images under disks35/ (raw .dsk / .img SuperDrive media).
static std::vector<std::string> listFloppyImages() {
    namespace fs = std::filesystem;
    std::string dir = findPath("disks35");
    std::vector<std::string> out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        for (char& ch : ext) ch = char(tolower(ch));
        if (ext == ".dsk" || ext == ".img" || ext == ".image")
            out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// List the disk images next to the current one (or under hdv/) — the pool
// the "Disques" menu offers. Sorted for a stable menu order.
static std::vector<std::string> listDiskImages(const std::string& nearPath) {
    namespace fs = std::filesystem;
    std::string dir = nearPath.empty()
        ? findPath("hdv") : fs::path(nearPath).parent_path().string();
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir.empty() ? "hdv" : dir, ec)) {
        std::string ext = e.path().extension().string();
        for (char& ch : ext) ch = char(tolower(ch));
        if (ext == ".vhd" || ext == ".hda" || ext == ".img")
            out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ── Mac II machine thread ───────────────────────────────────────────────
// Same GUI ↔ machine contract as LcMachine: queued commands, published
// framebuffer + status. Video is NuBus Toby (640×480); sound is discrete
// ASC @ $50F14000. Frame slice ≈ 60.15 Hz at 15.6672 MHz.
struct MacIiMachine {
    MacIIMemory& mem; Cpu020& cpu; MacAudioHost& audioHost;
    MacIiMachine(MacIIMemory& m, Cpu020& c, MacAudioHost& a)
        : mem(m), cpu(c), audioHost(a) {}
    ~MacIiMachine() { stop(); }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset } t; int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay, hmmu24; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0 };
    }

    int stepTick() {
        applyCmds();
        if (!running.load(std::memory_order_relaxed)) { publish(); return 5000; }
        int sleepUs = 0;
        if (activeHold_ > 0 && audioHost.started()) {
            int n = 0;
            while (audioHost.buffered() < kTarget && n < 8) {
                runOne();
                if (drain()) activeHold_ = 90; else activeHold_--;
                audioHost.pushRaw(samp_, 0);
                n++;
            }
            if (n == 0) {
                if (++starve_ > 80) {
                    runOne();
                    if (drain()) activeHold_ = 90; else activeHold_--;
                    starve_ = 0;
                }
                sleepUs = 2000;
            } else starve_ = 0;
        } else {
            auto t0 = std::chrono::steady_clock::now();
            int n = 0;
            do {
                runOne();
            } while (turbo.load(std::memory_order_relaxed) && ++n < 8 &&
                     std::chrono::steady_clock::now() - t0 <
                         std::chrono::milliseconds(10));
            if (drain()) {
                activeHold_ = 90;
                audioHost.pushFrame(samp_, 0);
            }
            if (!turbo.load(std::memory_order_relaxed)) {
                auto spent = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
                sleepUs = int(std::max<long long>(0, 16625 - spent));
            }
        }
        publish();
        return sleepUs;
    }

    void start() {
#ifndef __EMSCRIPTEN__
        th_ = std::thread([this] {
            while (!quit.load(std::memory_order_relaxed)) {
                int us = stepTick();
                if (us > 0) std::this_thread::sleep_for(std::chrono::microseconds(us));
            }
        });
#endif
    }
    void stop() {
#ifndef __EMSCRIPTEN__
        quit.store(true);
        if (th_.joinable()) th_.join();
#endif
    }

    void publish(bool force = false) {
        auto now = std::chrono::steady_clock::now();
        if (!force && framesRun_ == 0 &&
            now - lastPub_ < std::chrono::milliseconds(16)) return;
        lastPub_ = now; framesRun_ = 0;
        TobyVideo* tv = mem.toby();
        int hres = tv ? tv->hres() : TobyVideo::W;
        int vres = tv ? tv->vres() : TobyVideo::H;
        if (tv) tv->decode(fb_);
        else fb_.assign(size_t(hres) * size_t(vres), 0xFFFFFFFFu);
        for (uint32_t& px : fb_) px |= 0xFF000000u;
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_ = fb_; fbW_ = hres; fbH_ = vres;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               (mem.hmmu24() ? 2 : 0)),
                       std::memory_order_relaxed);
    }

private:
    static constexpr int64_t kFrame = MacIIMemory::kCpuHz / 60;
    static constexpr size_t kTarget = 2225;

    void runOne() {
        cpu.runCycles(kFrame);
        framesRun_++;
    }
    bool drain() {
        samp_.clear();
        while (mem.asc().available() > 0)
            samp_.push_back(float(mem.asc().pop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }
    void applyCmds() {
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.a != 0); break;
            case Cmd::Key:         mem.keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
        }
        cmdsApply_.clear();
    }

    std::thread th_;
    std::mutex cmdMu_;
    std::vector<Cmd> cmds_, cmdsApply_;
    std::mutex fbMu_;
    std::vector<uint32_t> fbShared_;
    int fbW_ = 0, fbH_ = 0;
    std::atomic<uint32_t> stPc_{0};
    std::atomic<long long> stClock_{0};
    std::atomic<uint8_t> stFlags_{0};
    int activeHold_ = 0;
    int starve_ = 0;
    int framesRun_ = 0;
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;
    std::vector<float> samp_;
};

// ── Macintosh II: GLUE + 68020 + Toby NuBus, selected by a 256 KB ROM ───
static int runMacII(std::vector<uint8_t> rom, const std::string& romName,
                    int argc, char** argv) {
    std::printf("Machine: Macintosh II (68020 @ 15.6672 MHz, Toby NuBus%s)\n",
                getenv("POM68K_NOFPU") ? "" : ", soft 68881");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static MacIIMemory mem;
    static Cpu020 cpu(mem, getenv("POM68K_NOFPU") == nullptr);
    static MacAudioHost audioHost;
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "FAIL: bad Mac II ROM\n");
        return 1;
    }
    mem.installTobyVideo();
    mem.setCpu(&cpu);
    cpu.hardReset();

    // Prefer Infinite Mac System 6.0.8 HD, then HD20SC / other SCSI images.
    std::string hddPath = (argc > 2) ? argv[2] : findPath("hdv/System 6.0.8 HD.dsk");
    if (hddPath.empty()) hddPath = findPath("hdv/HD20SC.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1320, 1040, "POM68K — Macintosh II", nullptr, nullptr);
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

    static MacIiMachine machine{mem, cpu, audioHost};
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; MacIiMachine& m; GLuint tex;
        ScreenInput input;
        std::string romName, hddPath;
        std::vector<std::string> extraDisks;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, extraDisks};

    auto frame = [](void* arg) {
        Ctx& c = *static_cast<Ctx*>(arg);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        machineMenu(MachineKind::MacII, c.window, [&c] {
            if (!ImGui::BeginMenu("Disques")) return;
            auto relaunch = [&c](const std::string& boot,
                                 const std::vector<std::string>& extras) {
                gSwitchArgs = { c.romName, boot };
                for (const std::string& e : extras)
                    if (e != boot) gSwitchArgs.push_back(e);
                glfwSetWindowShouldClose(c.window, GLFW_TRUE);
            };
            ImGui::TextDisabled("Boot SCSI");
            for (const std::string& d : listDiskImages(c.hddPath)) {
                bool cur = (d == c.hddPath);
                std::string name = std::filesystem::path(d).filename().string();
                if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                    relaunch(d, c.extraDisks);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Volumes secondaires");
            for (const std::string& d : listDiskImages(c.hddPath)) {
                if (d == c.hddPath) continue;
                bool on = std::find(c.extraDisks.begin(), c.extraDisks.end(), d)
                          != c.extraDisks.end();
                std::string name = std::filesystem::path(d).filename().string();
                if (ImGui::MenuItem(name.c_str(), nullptr, on)) {
                    std::vector<std::string> extras = c.extraDisks;
                    if (on) extras.erase(std::remove(extras.begin(), extras.end(), d),
                                         extras.end());
                    else extras.push_back(d);
                    relaunch(c.hddPath, extras);
                }
            }
            ImGui::EndMenu();
        });

        ImGui::Begin("Macintosh II", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        std::vector<uint32_t> fb;
        int fw = 0, fh = 0;
        if (c.m.latchFrame(fb, fw, fh) && fw > 0 && fh > 0) {
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, fb.data());
            c.input.frame(c.window, c.tex, ImVec2(float(fw * 2), float(fh * 2)),
                    [&](int dx, int dy) { c.m.push({MacIiMachine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({MacIiMachine::Cmd::MouseButton, down ? 1 : 0}); });
        }
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

        // Keyboard → ADB (same M0110>>1 table as LC II / Quadra).
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
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Escape,0x6B},
            };
            for (const auto& e : kKeys) {
                if (ImGui::IsKeyPressed(e.k, false))
                    c.m.push({MacIiMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (ImGui::IsKeyReleased(e.k))
                    c.m.push({MacIiMachine::Cmd::Key, e.m0110 >> 1, 0});
            }
        }

        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        MacIiMachine::Status st = c.m.status();
        ImGui::Text("68020 @ 15.6672 MHz (Moira)  PC=%08X  clock=%lld",
                    st.pc, st.clock);
        ImGui::Text("overlay=%d  HMMU24=%d  Toby=%dx%d",
                    st.overlay ? 1 : 0, st.hmmu24 ? 1 : 0,
                    c.m.mem.toby() ? c.m.mem.toby()->hres() : 0,
                    c.m.mem.toby() ? c.m.mem.toby()->vres() : 0);
        bool running = c.m.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run")) c.m.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) c.m.push({MacIiMachine::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = c.m.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Turbo", &turbo))
            c.m.turbo.store(turbo);
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
    machine.start();
    while (!glfwWindowShouldClose(window)) frame(&ctx);
    machine.stop();
    audioHost.stop();
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    relaunchIfSwitched(argv[0]);
#endif
    return 0;
}

// ── LC II machine thread ────────────────────────────────────────────────
// Runs the emulation + audio-clocked pacing OFF the vsync'd ImGui thread
// (TODO § Performance): a slow GPU frame or a compositor stall no longer
// steals emulation time, and the pacer sleeps on its own schedule instead
// of piggybacking on vsync. GUI ↔ machine contract:
//   - input and machine controls cross as queued commands (cmdMu_) applied
//     between frame slices — the ADB/CPU objects are only ever touched here;
//   - the framebuffer crosses as a decoded copy (fbMu_);
//   - the CPU-window status line crosses as relaxed atomics (display only);
//   - the ASC audio ring keeps its SPSC discipline (the producer just moved
//     from the GUI thread to this one).
// Under Emscripten there is no thread: the GUI frame calls stepTick()
// inline — one code path, two drivers.
struct LcMachine {
    V8Memory& mem; Cpu030& cpu; V8Video& video; MacAudioHost& audioHost;
    LcMachine(V8Memory& m, Cpu030& c, V8Video& v, MacAudioHost& a)
        : mem(m), cpu(c), video(v), audioHost(a) {}
    // Any exit() while the thread runs (Xlib's default error handler exits
    // behind GLFW's back) would otherwise destroy a joinable std::thread —
    // an instant std::terminate. Joining here turns that into a clean stop.
    ~LcMachine() { stop(); }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, Sense } t; int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    // Latest decoded frame (00RRGGBB, alpha forced — see the decode note).
    // Returns false until the first publish.
    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held; uint8_t config; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 4) != 0,
                 stConfig_.load(std::memory_order_relaxed) };
    }

    // One pacing tick (the former GUI-frame emulation block, verbatim
    // logic). Returns how long the caller may sleep (µs) before the next
    // tick — 0 = come straight back.
    int stepTick() {
        applyCmds();
        if (!running.load(std::memory_order_relaxed)) { publish(); return 5000; }
        int sleepUs = 0;
        // Audio-clocked pacing (TODO § sound tempo wobble): while the guest
        // streams sound, the emulation speed IS the tempo, so it must track
        // the host DAC, not the host CPU. When sound was heard recently
        // (activeHold_), each tick emulates just enough frames to keep the
        // host ring near ~100 ms — the DAC's 22 254 Hz consumption paces the
        // machine at real time and absorbs the 60.15 vs wall-clock drift
        // with no resampler. Silence between notes is pushed too (pushRaw):
        // it is part of the musical timeline. When no sound plays, the
        // time-budgeted turbo runs (fast boot/Finder; gated push keeps the
        // ring free of silence).
        if (activeHold_ > 0 && audioHost.started()) {
            int n = 0;
            while (audioHost.buffered() < kTarget && n < 8) {
                runOne();
                if (drain()) activeHold_ = 90; else activeHold_--;
                audioHost.pushRaw(samp_, 0);
                n++;
            }
            if (n == 0) {
                // Ring at target: real time says "no frame due yet". Sleep a
                // hair and let the DAC drain — unless it stopped consuming
                // entirely (unplugged device): after ~160 ms of that, force
                // a frame so the machine never freezes.
                if (++starve_ > 80) {
                    runOne();
                    if (drain()) activeHold_ = 90; else activeHold_--;
                    starve_ = 0;
                }
                sleepUs = 2000;
            } else starve_ = 0;
        } else {
            // Time-budgeted turbo: emulate in ≤10 ms bursts so commands and
            // the published frame stay fresh; between bursts the GUI thread
            // runs undisturbed on its own core. Without turbo, pace one
            // frame per 60.15 Hz period.
            auto t0 = std::chrono::steady_clock::now();
            int n = 0;
            do {
                runOne();
            } while (turbo.load(std::memory_order_relaxed) && ++n < 8 &&
                     std::chrono::steady_clock::now() - t0 <
                         std::chrono::milliseconds(10));
            if (drain()) {
                activeHold_ = 90;                       // sound starts:
                audioHost.pushFrame(samp_, 0);          // switch to pacing
            }
            if (!turbo.load(std::memory_order_relaxed)) {
                auto spent = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
                sleepUs = int(std::max<long long>(0, 16625 - spent));
            }
        }
        publish();
        return sleepUs;
    }

    void start() {
#ifndef __EMSCRIPTEN__
        th_ = std::thread([this] {
            while (!quit.load(std::memory_order_relaxed)) {
                int us = stepTick();
                if (us > 0) std::this_thread::sleep_for(std::chrono::microseconds(us));
            }
        });
#endif
    }
    void stop() {
#ifndef __EMSCRIPTEN__
        quit.store(true);
        if (th_.joinable()) th_.join();
#endif
    }

    // Decode + hand over the frame and the status snapshot. Throttled: a
    // tick that emulated nothing (audio ring full, pause) publishes at most
    // ~60 Hz, so the 640×480 decode isn't re-run 500×/s during the pacing
    // sleeps.
    void publish(bool force = false) {
        auto now = std::chrono::steady_clock::now();
        if (!force && framesRun_ == 0 &&
            now - lastPub_ < std::chrono::milliseconds(16)) return;
        lastPub_ = now; framesRun_ = 0;
        int hres, vres;
        V8Video::resolution(mem.monitorSense(), hres, vres);
        video.decode(fb_);
        // decode() packs 00RRGGBB — alpha 0. ImGui renders textures with
        // alpha blending on, so a 0 alpha draws fully transparent (black
        // window background); force A=$FF before the BGRA upload.
        for (uint32_t& px : fb_) px |= 0xFF000000u;
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_ = fb_; fbW_ = hres; fbH_ = vres;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC() & 0x80000000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        stConfig_.store(mem.ramConfig(), std::memory_order_relaxed);
    }

private:
    static constexpr int kFrame = 640 * 407;   // dots per 60.15 Hz frame
    static constexpr size_t kTarget = 2225;    // ~100 ms of 22 257 Hz sound

    void runOne() {
        if (mem.cpuHeld()) mem.tick(kFrame);   // Egret power-on hold
        else cpu.runCycles(kFrame);
        framesRun_++;
    }
    // Drain the ASC samples produced by the last slice (22 257 Hz mono,
    // continuous — an empty FIFO repeats its stale byte) and report whether
    // they carry real sound (AC span, same gate as MacAudioHost::pushFrame).
    bool drain() {
        samp_.clear();
        while (mem.asc().available() > 0)
            samp_.push_back(float(mem.asc().pop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }
    void applyCmds() {
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.adb().mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.adb().mouseButton(c.a != 0); break;
            case Cmd::Key:         mem.adb().keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            case Cmd::Sense:       mem.setMonitorSense(uint8_t(c.a)); cpu.hardReset(); break;
        }
        cmdsApply_.clear();
    }

    std::thread th_;
    std::mutex cmdMu_;
    std::vector<Cmd> cmds_, cmdsApply_;
    std::mutex fbMu_;
    std::vector<uint32_t> fbShared_;
    int fbW_ = 0, fbH_ = 0;
    std::atomic<uint32_t> stPc_{0};
    std::atomic<long long> stClock_{0};
    std::atomic<uint8_t> stFlags_{0};
    std::atomic<uint8_t> stConfig_{0};
    int activeHold_ = 0;           // machine frames of sound-recent state
    int starve_ = 0;               // safety against a dead DAC
    int framesRun_ = 0;            // frames emulated since the last publish
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;
    std::vector<float> samp_;
};

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
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");
    // Secondary volumes (argv[3..] → SCSI IDs 1..6): mounted by the
    // System's boot-time bus scan. The "Disques" menu edits this list by
    // relaunching with new arguments (clean PRAM + machine state).
    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;            // never double-attach
        int id = int(extraDisks.size()) + 1;
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

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

    static LcMachine machine{mem, cpu, video, audioHost};
    machine.publish(true);              // first frame before the GUI shows

    struct Ctx {
        GLFWwindow* window; LcMachine& m; GLuint tex;
        std::vector<uint32_t> fb;                // GUI-side framebuffer copy
        std::string romName, hddPath;            // for the "Disques" relaunch
        std::vector<std::string>& extraDisks;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, extraDisks};

    auto frame = [](void* p) {
        Ctx& c = *static_cast<Ctx*>(p);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

#ifdef __EMSCRIPTEN__
        c.m.stepTick();                 // no thread: emulate inline per frame
#endif

        int hres = 0, vres = 0;
        if (c.m.latchFrame(c.fb, hres, vres)) {
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hres, vres, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, c.fb.data());
        }

        machineMenu(MachineKind::LcII, c.window, [&c] {
            // ── "Disques" menu: pick the boot + secondary SCSI volumes.
            // Any change relaunches the emulator with the new argv list —
            // the ROM only scans the SCSI bus at boot, and the .pram file
            // follows the boot disk (same mechanism as the machine switch).
            namespace fs = std::filesystem;
            auto samePath = [](const std::string& a, const std::string& b) {
                std::error_code ec;
                return a == b || fs::equivalent(a, b, ec);
            };
            auto relaunch = [&c](const std::string& boot,
                                 const std::vector<std::string>& extras) {
                gSwitchArgs = { c.romName, boot };
                for (const std::string& e : extras)
                    if (e != boot) gSwitchArgs.push_back(e);
                glfwSetWindowShouldClose(c.window, GLFW_TRUE);
            };
            if (ImGui::BeginMenu("Disques")) {
                const auto disks = listDiskImages(c.hddPath);
                ImGui::TextDisabled("Démarrage (SCSI 0)");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                for (const std::string& d : disks) {
                    if (samePath(d, c.hddPath)) continue;
                    bool on = false;
                    for (const std::string& e : c.extraDisks)
                        if (samePath(d, e)) { on = true; break; }
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, on)) {
                        std::vector<std::string> extras;
                        for (const std::string& e : c.extraDisks)
                            if (!samePath(d, e)) extras.push_back(e);
                        if (!on) extras.push_back(d);
                        relaunch(c.hddPath, extras);
                    }
                }
                ImGui::Separator();
                ImGui::TextDisabled("Changer un disque relance l'émulateur");
                ImGui::EndMenu();
            }
            // One-click machine reset (= power cycle: overlay + chips + CPU;
            // the ROM rescans the SCSI bus, so hot-attached media appear).
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({LcMachine::Cmd::HardReset});
        });

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin("Macintosh LC II", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({LcMachine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({LcMachine::Cmd::MouseButton, down ? 1 : 0}); });
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
                if (ImGui::IsKeyPressed(e.k, false))
                    c.m.push({LcMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (ImGui::IsKeyReleased(e.k))
                    c.m.push({LcMachine::Cmd::Key, e.m0110 >> 1, 0});
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 830), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        // Display-only snapshot published by the machine thread.
        LcMachine::Status st = c.m.status();
        ImGui::Text("68030 @ 15.6672 MHz (Moira + PMMU)  PC=%08X  clock=%lld",
                    st.pc, st.clock);
        ImGui::Text("overlay=%d  config=$%02X  MMU=%s  held=%d",
                    st.overlay ? 1 : 0, st.config,
                    st.mmu ? "on" : "off", st.held ? 1 : 0);
        bool running = c.m.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run")) c.m.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) c.m.push({LcMachine::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = c.m.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Turbo", &turbo))    // as fast as the host allows
            c.m.turbo.store(turbo);

        // Monitor sense = the ID resistors on a real Mac's video connector;
        // the ROM reads it at reset to pick the resolution. Switching it is
        // like plugging in a different monitor, so it takes a Mac reset. The
        // LC II's built-in V8 video only drives these two color modes (512KB
        // VRAM + V8 bandwidth); depth is per-monitor, so a fresh mode may
        // come up B&W until you set "256 couleurs" in Moniteurs + restart.
        int sense = c.m.mem.monitorSense();      // byte read; only the GUI changes it
        ImGui::Text("Moniteur:");
        ImGui::SameLine();
        auto monoBtn = [&](const char* label, int s) {
            bool cur = sense == s;
            if (cur) ImGui::PushStyleColor(ImGuiCol_Button,
                                           ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label) && !cur) c.m.push({LcMachine::Cmd::Sense, s});
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
    machine.start();                    // emulation runs on its own core
    while (!glfwWindowShouldClose(window)) frame(&ctx);
    machine.stop();                     // join before touching machine state
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

// ── Quadra 605 / LC 475 machine thread ──────────────────────────────────
// Same GUI ↔ machine contract as LcMachine (commands queued, framebuffer +
// status copied out), but the Q605 has no ASC wired in POM68K yet, so the
// pacing is the plain time-budgeted turbo — no audio-clocked path. The
// framebuffer is decoded straight from VRAM using the live screen geometry
// read from the main GDevice's PixMap (same derivation as q605_trace); the
// Mac OS 8.1 Finder comes up 1bpp 640×480, colour modes decode via the
// Antelope CLUT.
struct QuadraMachine {
    Q605Memory& mem; Cpu040& cpu; MacAudioHost& audioHost;
    QuadraMachine(Q605Memory& m, Cpu040& c, MacAudioHost& a)
        : mem(m), cpu(c), audioHost(a) {}
    ~QuadraMachine() { stop(); }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, InsertFloppy, EjectFloppy } t;
                 int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }
    void requestInsertFloppy(std::string path) {
        std::lock_guard<std::mutex> l(cmdMu_);
        floppyPending_ = std::move(path);
        cmds_.push_back({Cmd::InsertFloppy});
    }
    void requestEjectFloppy() {
        std::lock_guard<std::mutex> l(cmdMu_);
        cmds_.push_back({Cmd::EjectFloppy});
    }
    bool floppyInserted() const {
        return floppyFlag_.load(std::memory_order_relaxed);
    }
    void setFloppyInserted(bool on) {
        floppyFlag_.store(on, std::memory_order_relaxed);
    }

    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held; int w, h, depth; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 4) != 0,
                 stW_.load(std::memory_order_relaxed),
                 stH_.load(std::memory_order_relaxed),
                 stDepth_.load(std::memory_order_relaxed) };
    }

    // Same audio-clocked pacing as LcMachine: while the guest streams sound
    // the emulation speed IS the tempo, so it tracks the host DAC via the ASC
    // ring; otherwise a time-budgeted turbo runs (fast boot/Finder).
    int stepTick() {
        applyCmds();
        if (!running.load(std::memory_order_relaxed)) { publish(); return 5000; }
        int sleepUs = 0;
        if (activeHold_ > 0 && audioHost.started()) {
            int n = 0;
            while (audioHost.buffered() < kTarget && n < 8) {
                runOne();
                if (drain()) activeHold_ = 90; else activeHold_--;
                audioHost.pushRawStereo(samp_, 0);
                n++;
            }
            if (n == 0) {
                if (++starve_ > 80) {
                    runOne();
                    if (drain()) activeHold_ = 90; else activeHold_--;
                    starve_ = 0;
                }
                sleepUs = 2000;
            } else starve_ = 0;
        } else {
            // Time-budgeted turbo: emulate in ≤10 ms bursts so commands and the
            // published frame stay fresh; without turbo, pace ~60 Hz.
            auto t0 = std::chrono::steady_clock::now();
            int n = 0;
            do {
                runOne();
            } while (turbo.load(std::memory_order_relaxed) && ++n < 8 &&
                     std::chrono::steady_clock::now() - t0 <
                         std::chrono::milliseconds(10));
            if (drain()) {
                activeHold_ = 90;                       // sound starts:
                audioHost.pushFrameStereo(samp_, 0);    // switch to pacing
            }
            if (!turbo.load(std::memory_order_relaxed)) {
                auto spent = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
                sleepUs = int(std::max<long long>(0, 16625 - spent));
            }
        }
        publish();
        return sleepUs;
    }

    void start() {
#ifndef __EMSCRIPTEN__
        th_ = std::thread([this] {
            while (!quit.load(std::memory_order_relaxed)) {
                int us = stepTick();
                if (us > 0) std::this_thread::sleep_for(std::chrono::microseconds(us));
            }
        });
#endif
    }
    void stop() {
#ifndef __EMSCRIPTEN__
        quit.store(true);
        if (th_.joinable()) th_.join();
#endif
    }

    void publish(bool force = false) {
        auto now = std::chrono::steady_clock::now();
        if (!force && framesRun_ == 0 &&
            now - lastPub_ < std::chrono::milliseconds(16)) return;
        lastPub_ = now; framesRun_ = 0;
        int w = 0, h = 0, depth = 0;
        decode(fb_, w, h, depth);
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_ = fb_; fbW_ = w; fbH_ = h;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC040() & 0x8000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        stW_.store(w, std::memory_order_relaxed);
        stH_.store(h, std::memory_order_relaxed);
        stDepth_.store(depth, std::memory_order_relaxed);
    }

    // Decode the Q605 framebuffer (VRAM at $F9000000) into 00RRGGBB. Screen
    // base and bounds are read live from the main GDevice → PixMap. Pixel
    // depth and stride come from the DAFB hardware registers; the PixMap is
    // only a fallback while the video driver is publishing a new mode.
    void decode(std::vector<uint32_t>& out, int& w, int& h, int& depth) {
        auto pk32 = [&](uint32_t a) {
            return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a+1)) << 16 |
                   uint32_t(mem.peek8(a+2)) << 8 | mem.peek8(a+3);
        };
        uint32_t scrnBase = pk32(0x0824);
        uint32_t mainDevH = pk32(0x08A4);
        uint32_t mainDev  = mainDevH ? pk32(mainDevH) : 0;
        uint32_t pmapH    = mainDev ? pk32(mainDev + 0x16) : 0;
        uint32_t pmap     = pmapH ? pk32(pmapH) : 0;
        uint32_t pmBase = 0, pmRow = 0, pmDepth = 0, pmT = 0, pmL = 0, pmB = 0, pmR = 0;
        if (pmap) {
            pmBase = pk32(pmap + 0x00);
            pmRow  = (pk32(pmap + 0x04) >> 16) & 0x3FFF;
            pmT = (pk32(pmap+0x06)>>16)&0xFFFF; pmL = pk32(pmap+0x06)&0xFFFF;
            pmB = (pk32(pmap+0x0A)>>16)&0xFFFF; pmR = pk32(pmap+0x0A)&0xFFFF;
            pmDepth = (pk32(pmap+0x1C)>>16)&0xFFFF;
        }
        // The framebuffer pointer is either the physical VRAM window
        // ($F9000000 + off) or a MMU/alias logical view of it ($5190xxxx —
        // Mac OS 8.1 runs the Quadra 32-bit clean and hands QuickDraw a
        // logical base). The aperture is VRAM-size aligned, so the low
        // log2(kVramSize) bits are the byte offset into VRAM either way —
        // masking works for both forms and skips the leading offscreen band
        // (the "same"/"diff" scratch at VRAM 0 the ROM leaves before the
        // visible screen, which otherwise paints a stray white strip on top).
        uint32_t src = pmBase ? pmBase : scrnBase;
        uint32_t off = src & (Q605Memory::kVramSize - 1);
        w = (pmR > pmL && pmR - pmL <= 1600) ? int(pmR - pmL) : 640;
        h = (pmB > pmT && pmB - pmT <= 1200) ? int(pmB - pmT) : 480;
        uint32_t hwDepth = mem.dafbDepth();
        depth = (hwDepth == 1 || hwDepth == 2 || hwDepth == 4 || hwDepth == 8)
              ? int(hwDepth)
              : ((pmDepth == 1 || pmDepth == 2 || pmDepth == 4 || pmDepth == 8)
                    ? int(pmDepth) : 1);
        uint32_t minStride = uint32_t((w * depth + 7) / 8);
        uint32_t hwStride = mem.dafbStride();
        uint32_t stride = (hwStride >= minStride && hwStride <= Q605Memory::kVramSize)
                        ? hwStride : (pmRow >= minStride ? pmRow : minStride);
        // Guard a bogus base (before the driver publishes one): the visible
        // screen must fit within VRAM, else fall back to offset 0.
        if (uint64_t(off) + uint64_t(h) * stride > Q605Memory::kVramSize) off = 0;

        const uint8_t* vr = mem.vram();
        const uint8_t (*cl)[3] = mem.clut();
        auto vb = [&](uint32_t o) -> uint8_t {
            return o < Q605Memory::kVramSize ? vr[o] : 0;
        };
        out.assign(size_t(w) * h, 0xFF000000u);
        for (int y = 0; y < h; y++) {
            uint32_t rowOff = off + uint32_t(y) * stride;
            for (int x = 0; x < w; x++) {
                uint32_t rgb;
                switch (depth) {
                    case 1: { int bit = (vb(rowOff + (x >> 3)) >> (7 - (x & 7))) & 1;
                              rgb = bit ? 0x000000u : 0xFFFFFFu; break; }
                    case 2: { int v = (vb(rowOff + (x >> 2)) >> (6 - 2*(x & 3))) & 3;
                              const uint8_t* c = cl[v];
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    case 4: { uint8_t bt = vb(rowOff + (x >> 1));
                              int v = (x & 1) ? (bt & 0xF) : (bt >> 4);
                              const uint8_t* c = cl[v];
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    default: { const uint8_t* c = cl[vb(rowOff + x)];   // 8 bpp
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                }
                out[size_t(y) * w + x] = 0xFF000000u | rgb;
            }
        }
    }

private:
    // One 60 Hz emulation quantum (25 MHz / 60 ≈ 416 667 cycles). During the
    // Cuda power-on hold the CPU is parked, so just tick the peripherals.
    static constexpr int kFrame = 416667;
    static constexpr size_t kTarget = 2225;    // ~100 ms of 22 257 Hz sound
    void runOne() {
        if (mem.cpuHeld()) mem.tick(kFrame);
        else cpu.runCycles(kFrame);
        framesRun_++;
    }
    // Drain interleaved IOSB ASC stereo frames and report real AC content.
    bool drain() {
        samp_.clear();
        int16_t left, right;
        while (mem.asc().popStereo(left, right)) {
            samp_.push_back(float(left) / 32768.0f);
            samp_.push_back(float(right) / 32768.0f);
        }
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }
    void applyCmds() {
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.adb().mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.adb().mouseButton(c.a != 0); break;
            case Cmd::Key:         mem.adb().keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            case Cmd::InsertFloppy:
                if (!floppyPending_.empty() && mem.insertDisk(floppyPending_))
                    floppyFlag_.store(true, std::memory_order_relaxed);
                floppyPending_.clear();
                break;
            case Cmd::EjectFloppy:
                mem.ejectDisk();
                floppyFlag_.store(false, std::memory_order_relaxed);
                break;
        }
        cmdsApply_.clear();
    }

    std::thread th_;
    std::mutex cmdMu_;
    std::vector<Cmd> cmds_, cmdsApply_;
    std::string floppyPending_;
    std::atomic<bool> floppyFlag_{false};
    std::mutex fbMu_;
    std::vector<uint32_t> fbShared_;
    int fbW_ = 0, fbH_ = 0;
    std::atomic<uint32_t> stPc_{0};
    std::atomic<long long> stClock_{0};
    std::atomic<uint8_t> stFlags_{0};
    std::atomic<int> stW_{0}, stH_{0}, stDepth_{0};
    int framesRun_ = 0;
    int activeHold_ = 0;           // machine frames of sound-recent state
    int starve_ = 0;               // safety against a dead DAC
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;
    std::vector<float> samp_;
};

// ── LC 475 / Quadra 605 (Q6): MEMCjr/PrimeTime + 68LC040, selected by a
// 1 MB ROM. Structure mirrors runLcII; the Q605 has no ASC yet (silent).
static int runQuadra(std::vector<uint8_t> rom, const std::string& romName,
                     int argc, char** argv) {
    std::printf("Machine: LC 475 / Quadra 605 (68LC040 @ 25 MHz, MEMCjr+PrimeTime)\n");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static Q605Memory mem;
    static Cpu040 cpu(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    cpu.hardReset();

    // Boot volume: argv[2], else the Mac OS 8.1 image the Q6 trace boots to
    // the Finder. As on the LC II, the ROM only boots wrapped images (DDM +
    // $6A driver entry — tools/wrap_hfs.py); a bare HFS image blinks the ?.
    std::string hddPath = (argc > 2) ? argv[2] : findPath("hdv/MacOS-8.1-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");
    // Optional SuperDrive floppy (SWIM2): POM68K_FLOPPY, else disks35/ if present.
    // SCSI remains the default boot path; a floppy is just media presence for the GUI.
    std::string floppyPath;
    if (const char* env = std::getenv("POM68K_FLOPPY")) floppyPath = env;
    if (floppyPath.empty()) floppyPath = findPath("disks35/Disk605.dsk");
    if (floppyPath.empty()) floppyPath = findPath("disks35/quadra.img");
    static bool floppyOk = !floppyPath.empty() && mem.insertDisk(floppyPath);
    if (floppyOk) std::printf("Floppy: %s\n", floppyPath.c_str());
    // Secondary volumes (argv[3..] → SCSI IDs 1..6).
    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        // Treat .dsk / raw SuperDrive images as floppy inserts, not SCSI.
        std::string arg = argv[i];
        auto ext = std::filesystem::path(arg).extension().string();
        for (char& c : ext) c = char(std::tolower(c));
        if (ext == ".dsk" || ext == ".image") {
            if (mem.insertDisk(arg)) {
                floppyPath = arg;
                floppyOk = true;
                std::printf("Floppy: %s\n", arg.c_str());
            }
            continue;
        }
        int id = int(extraDisks.size()) + 1;
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

    // Battery-backed PRAM+clock (Cuda XPRAM) — persist it like the LC II so a
    // cold PRAM doesn't retrigger the ROM's full-RAM burn-in every boot.
    static std::string pramPath =
        (hddPath.empty() ? std::string("quadra605") : hddPath) + ".pram";
    if (mem.cuda().loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    mem.cuda().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // 640×480 shown at 2× fits with the menu bar and the CPU window.
    GLFWwindow* window = glfwCreateWindow(1320, 1080, "POM68K — Quadra 605", nullptr, nullptr);
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

    static QuadraMachine machine{mem, cpu, audioHost};
    machine.setFloppyInserted(floppyOk);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; QuadraMachine& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath, floppyPath;
        std::vector<std::string>& extraDisks;
        bool& floppyOk;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, floppyPath,
                   extraDisks, floppyOk};

    auto frame = [](void* p) {
        Ctx& c = *static_cast<Ctx*>(p);
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

        machineMenu(MachineKind::Quadra, c.window, [&c] {
            namespace fs = std::filesystem;
            auto samePath = [](const std::string& a, const std::string& b) {
                std::error_code ec;
                return a == b || fs::equivalent(a, b, ec);
            };
            auto relaunch = [&c](const std::string& boot,
                                 const std::vector<std::string>& extras) {
                gSwitchArgs = { c.romName, boot };
                for (const std::string& e : extras)
                    if (e != boot) gSwitchArgs.push_back(e);
                glfwSetWindowShouldClose(c.window, GLFW_TRUE);
            };
            if (ImGui::BeginMenu("Disques")) {
                const auto disks = listDiskImages(c.hddPath);
                ImGui::TextDisabled("Démarrage (SCSI 0)");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                for (const std::string& d : disks) {
                    if (samePath(d, c.hddPath)) continue;
                    bool on = false;
                    for (const std::string& e : c.extraDisks)
                        if (samePath(d, e)) { on = true; break; }
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, on)) {
                        std::vector<std::string> extras;
                        for (const std::string& e : c.extraDisks)
                            if (!samePath(d, e)) extras.push_back(e);
                        if (!on) extras.push_back(d);
                        relaunch(c.hddPath, extras);
                    }
                }
                ImGui::Separator();
                ImGui::TextDisabled("Floppy (SWIM2)");
                if (ImGui::MenuItem("Éjecter", nullptr, false, c.m.floppyInserted())) {
                    c.m.requestEjectFloppy();
                    c.floppyOk = false;
                    c.floppyPath.clear();
                }
                for (const std::string& d : listFloppyImages()) {
                    bool cur = !c.floppyPath.empty() && samePath(d, c.floppyPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur) {
                        c.m.requestInsertFloppy(d);
                        c.floppyPath = d;
                        c.floppyOk = true;
                    }
                }
                ImGui::Separator();
                ImGui::TextDisabled("Changer un disque SCSI relance l'émulateur");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({QuadraMachine::Cmd::HardReset});
        });

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin("Quadra 605", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({QuadraMachine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({QuadraMachine::Cmd::MouseButton, down ? 1 : 0}); });
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

        // Keyboard → ADB key codes (= M0110 transition code >> 1); same table
        // as the LC II loop.
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
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (ImGui::IsKeyPressed(e.k, false))
                    c.m.push({QuadraMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (ImGui::IsKeyReleased(e.k))
                    c.m.push({QuadraMachine::Cmd::Key, e.m0110 >> 1, 0});
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        QuadraMachine::Status st = c.m.status();
        ImGui::Text("68LC040 @ 25 MHz (Moira + 040 MMU)  PC=%08X  clock=%lld",
                    st.pc, st.clock);
        ImGui::Text("overlay=%d  %dx%d @ %d bpp  MMU=%s  held=%d",
                    st.overlay ? 1 : 0, st.w, st.h, st.depth,
                    st.mmu ? "on" : "off", st.held ? 1 : 0);
        ImGui::Text("floppy=%s", c.m.floppyInserted()
                    ? (c.floppyPath.empty() ? "inserted" : c.floppyPath.c_str())
                    : "none");
        bool running = c.m.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run")) c.m.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) c.m.push({QuadraMachine::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = c.m.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Turbo", &turbo)) c.m.turbo.store(turbo);
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
    machine.start();
    while (!glfwWindowShouldClose(window)) frame(&ctx);
    machine.stop();
    mem.cuda().savePram(pramPath);
    audioHost.stop();
    glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    relaunchIfSwitched(argv[0]);
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
        if (rom.empty()) rom = findResource("roms/macii.rom", matched);
        if (rom.empty()) rom = findResource("roms/quadra605.rom", matched);
        // No canonical short name found — scan roms/ by CRC signature so a
        // stock Mac II / LC II / Quadra dump boots without needing a symlink.
        for (const char* sig : { "9779D2C4", "35C28F5F", "FF7439EE" }) {
            if (!rom.empty()) break;
            std::string p = findRomBySignature(sig);
            if (!p.empty()) { rom = readFile(p); matched = p; }
        }
    }

    // ROM size alone selects the machine: 1 MB = LC 475/Quadra 605 (Q6),
    // 512 KB = Mac LC II (O6), 256 KB = Macintosh II, 128 KB = Mac Plus.
    if (rom.size() == Q605Memory::kRomSize) return runQuadra(std::move(rom), matched, argc, argv);
    if (rom.size() == V8Memory::kRomSize) return runLcII(std::move(rom), matched, argc, argv);
    if (rom.size() == MacIIMemory::kRomSize) return runMacII(std::move(rom), matched, argc, argv);

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

        machineMenu(MachineKind::Plus, c.window, [&c] {
            if (ImGui::MenuItem("Redémarrer")) c.cpu.hardReset();
        });

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
