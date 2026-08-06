// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M3 shell: run the 68000 (Moira) against the Mac Plus memory map and display
// the 512×342 framebuffer in an ImGui window. Structure mirrors POMIIGS's
// main.cpp so it grows into the same shape (Ui class, audio, disks later).
// O6: a 512 KB ROM selects the Mac LC II machine (V8 + 68030); Q6: a 1 MB
// ROM selects the LC 475 / Quadra 605 machine (MEMCjr/PrimeTime + 68LC040).

#include "imgui.h"
#include "DiskBays.h"
#include "DockLayout.h"
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
#include "SonoraMemory.h"
#include "SonoraVideo.h"
#include "SonoraCpu.h"
#include "VaspMemory.h"
#include "VaspVideo.h"
#include "VaspCpu.h"
#include "RbvMemory.h"
#include "RbvVideo.h"
#include "RbvCpu.h"
#include "Cpu040.h"
#include "Q605Memory.h"
#include "CentrisMemory.h"
#include "CentrisCpu.h"
#include "Q700Memory.h"
#include "Q700Cpu.h"
#include "Q630Memory.h"
#include "Q630Cpu.h"
#include "Cpu020.h"
#include "IIfxCpu.h"
#include "IIfxMemory.h"
#include "MscCpu.h"
#include "MscMemory.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "LtoUdp.h"
#include "AtalkHub.h"
#include "SaveStateMachines.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

// ─── Windows shims (first MSVC build, 2026-08-05) ────────────────────────
// MSVC ships none of the POSIX env/time calls, and its <GL/gl.h> — dragged
// in by glfw3.h — stops at OpenGL 1.1, so the 1.2 constant GL_BGRA is
// missing from the HEADER only: the 3.x core context requested below
// supplies the format at runtime on every target. Everything here is a
// spelling difference, not a behaviour change.
#ifdef _WIN32
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#define timegm _mkgmtime
static inline void setenv(const char* k, const char* v, int) { _putenv_s(k, v); }
static inline void unsetenv(const char* k) { _putenv_s(k, ""); }
// localtime_s takes its two arguments in the opposite order to localtime_r.
static inline std::tm* localtime_r(const std::time_t* t, std::tm* out) {
    return localtime_s(out, t) == 0 ? out : nullptr;
}
#endif

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string execDir();          // defined below; used by wireLocalTalk

// ── AppleTalk: in-process stack + optional LToUDP cable ─────────────────
// Two things ride the SCC LocalTalk wire, and both can be live at once:
//   • g_atalk — the in-process AppleTalk world (node/router + AppleShare,
//     LaserWriter, MacIP), always on in the GUI unless POM68K_APPLETALK=0.
//     No external TashRouter / netatalk / macipgw needed.
//   • g_ltoudp — the LToUDP multicast cable (opt-in POM68K_LTOUDP=1), for
//     interop with real peers (Mini vMac, host netatalk). When up, guest
//     frames still cross it and the internal node is multicast alongside.
// wireLocalTalk installs the SCC onTxFrame hook once per machine; the
// hook synthesizes the LLAP CTS (needed by BOTH consumers) then fans the
// data frame out. pollLocalTalk drains the cable; the hub is ticked from
// runQuantumWithWire. injectRxFrame / the hub tick are all machine-thread
// bound.
static LtoUdp g_ltoudp;
static AtalkHub g_atalk;
static bool atalkEnabled() {
    const char* e = std::getenv("POM68K_APPLETALK");
    return !e || std::strcmp(e, "0") != 0;   // default on; "=0" disables
}
// The byte pace IS the machine clock divided by 230.4 kbit/s ÷ 8, and hubHz
// below reconstructs the clock from it — so derive it here rather than letting
// each call site pass a literal. Hardcoded 868s were feeding a 25 MHz clock to
// the 15.67 MHz IIvi, the 31.33 MHz IIvx/Mac TV and the 33.33 MHz Quadra 650,
// skewing every second-scale AppleTalk timer by up to 2x.
template <class M> static void wireLocalTalk(M& mem) {
    const int byteCycles = int(mem.cpuHz() / 28800);
    bool cable = std::getenv("POM68K_LTOUDP") && g_ltoudp.start();
    bool hub = atalkEnabled();
    if (!cable && !hub) { mem.scc().setByteCycles(byteCycles); return; }
    // Real LocalTalk is 230 kbit/s (~28 KB/s), so a multi-MB Finder copy to
    // the in-process AppleShare takes minutes — it *is* the wire speed. The
    // internal node is not a real cable, so run a LOSSLESS boosted virtual
    // wire (hub without external cable only):
    //   setWirePace   — SDLC per-byte pace ÷ boost (floor 64 cy/byte); it
    //                   must override the guest-derived 230.4 kbit/s pace,
    //                   which is why plain setByteCycles never worked here.
    //   setLosslessRx — a full Rx FIFO pauses the wire instead of dropping
    //                   (overrun → lost frame → 1-2 s ATP retransmit stall
    //                   was the copy "saccade"); throughput self-limits to
    //                   the guest's ISR drain rate, smoothly.
    //   (Scc-side)    — express-CTS gap, LLAP IDG and the Tx underrun
    //                   grace all stay at REAL pace: they are guest-code
    //                   turnaround windows, not wire properties.
    // Async serial (terminal on the modem/printer ports) is untouched: the
    // override applies in SDLC mode only. LToUDP interop keeps full real
    // timing (external peers are real wires). POM68K_ATALK_WIRE_BOOST
    // tunes it; =1 restores authentic LocalTalk speed.
    int64_t hubHz = int64_t(byteCycles) * 28800;     // real machine clock
    mem.scc().setByteCycles(byteCycles);
    if (hub && !cable) {
        int boost = 8;
        if (const char* b = std::getenv("POM68K_ATALK_WIRE_BOOST")) {
            int v = std::atoi(b);
            if (v >= 1) boost = v;
        }
        if (boost > 1) {
            mem.scc().setWirePace(std::max(byteCycles / boost, 64));
            mem.scc().setLosslessRx(true);
        }
    }
    if (hub) {
        // One-time defaults (env overridable). Share folder: POM68K_SHARE_DIR
        // or <repo root>/AppleShare (created if absent) — the repo root is
        // the exec dir's parent, so it lives next to the sources, not inside
        // the throwaway build/ tree.
        static bool once = [] {
            namespace fs = std::filesystem;
            std::error_code ec;
            const char* sd = std::getenv("POM68K_SHARE_DIR");
            fs::path dir;
            if (sd && *sd) {
                dir = sd;
            } else {
                std::string ed = execDir();               // ".../POM68K/build/"
                dir = ed.empty() ? fs::path("AppleShare")
                                 : fs::weakly_canonical(fs::path(ed) / ".." / "AppleShare", ec);
                if (dir.empty()) dir = "AppleShare";
            }
            fs::create_directories(dir, ec);
            g_atalk.setDefaultShareDir(fs::absolute(dir, ec).string());
            if (std::getenv("POM68K_APPLETALK") == nullptr)   // note only
                std::fprintf(stderr, "AppleTalk: in-process stack active "
                             "(share %s; POM68K_APPLETALK=0 disables)\n",
                             dir.string().c_str());
            return true;
        }();
        (void)once;
        g_atalk.attach(mem, hubHz, cable ? &g_ltoudp : nullptr);
    }
    mem.scc().onTxFrame = [&mem, cable, hub](int ch, const uint8_t* d, size_t n) {
        if (ch != 0) return;                         // channel B = LocalTalk
        // A directed lapRTS needs a lapCTS before the guest will send its
        // data frame — the "cable" (or the internal node) answers it
        // locally. Broadcast RTS gets no CTS; RTS/CTS never cross the wire
        // to a peer. Express: the synthesized CTS threads the sender's
        // half-duplex Rx-off window (Scc8530::injectRxFrame).
        if (n == 3 && d[2] == 0x84) {                // lapRTS
            if (d[0] != 0xFF) {
                const uint8_t cts[3] = { d[1], d[0], 0x85 };
                mem.scc().injectRxFrame(0, cts, 3, true);
            }
            return;
        }
        if (n == 3 && d[2] == 0x85) return;          // lapCTS: local only
        if (hub) g_atalk.onGuestFrame(d, n);
        if (cable) g_ltoudp.send(d, n);
    };
}
template <class M> static void pollLocalTalk(M& mem) {
    if (!g_ltoudp.active()) return;
    g_ltoudp.poll([&mem](const uint8_t* d, size_t n) {
        mem.scc().injectRxFrame(0, d, n);
        if (atalkEnabled()) g_atalk.onCableFrame(d, n);
    });
}
// One 60 Hz emulation quantum. With the wire active, slice it ~1 ms and
// service between slices: LLAP directed frames (lapRTS → lapCTS) run on
// 200 µs-class timeouts + driver retries — a 16.7 ms poll period would
// push every handshake past the retry limit; ~1 ms keeps it within a few
// retries (Mini vMac's LToUDP takes the same approach). The internal
// AppleTalk hub's timers are advanced on cumulative CPU cycles here too.
// `onSlice` runs at every slice boundary — the raster decoders hook it to
// catch the beam up mid-frame (VideoBeam.h). It is deliberately NOT allowed
// to change the slicing: the no-wire fast path still runs the quantum in one
// `runCycles`, so a machine with the network off keeps exactly the timing it
// had, and gets one whole-frame repaint per frame as before.
template <class M, class C, class F>
static void runQuantumWithWire(M& mem, C& cpu, int64_t frameCycles, F&& onSlice) {
    bool hub = atalkEnabled();
    if (!g_ltoudp.active() && !hub) {
        cpu.runCycles(frameCycles);
        onSlice();
        return;
    }
    // The hub flushes its queued replies from the tick() at each slice end,
    // so every AFP/ATP round-trip costs at least one slice of latency.
    // Finer slicing (64 vs 16) cuts that to ~260 µs — worth it for the
    // in-process server's back-to-back transactions during a file copy.
    const int kSlices = hub ? 64 : 16;
    for (int i = 0; i < kSlices; i++) {
        cpu.runCycles(frameCycles / kSlices);
        pollLocalTalk(mem);
        // Wire/bus time is MACHINE cycles: the hub was configured with the real
        // machine Hz (hubHz above), but getClock() is the Moira core clock,
        // which the i-cache boost runs cacheBoost_x faster. Feeding it here made
        // every second-scale timer (ATP retry, RTMP, AFP tickle/session death)
        // expire 4x early. Same class as the viaSync/stall boost fix.
        if (hub) g_atalk.tick(cpu.machineClock());
        onSlice();
    }
}

template <class M, class C>
static void runQuantumWithWire(M& mem, C& cpu, int64_t frameCycles) {
    runQuantumWithWire(mem, cpu, frameCycles, [] {});
}

// Host wall clock → Mac epoch (seconds since 1904-01-01, LOCAL time — the
// classic RTC keeps local wall time, there is no TZ in PRAM). GUI-only seed:
// tests never call this, so etalons stay deterministic (clock starts at 0).
// The Date & Time control panel still writes through as usual; only the
// power-on value comes from the host. 1904→1970 offset = 2 082 844 800 s.
static uint32_t hostMacSeconds() {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    // Re-read the LOCAL broken-down time as if it were UTC — that IS the
    // wall clock the RTC keeps. Equivalent to the old now + tm_gmtoff (DST
    // included, since localtime_r already folded it into the fields), and
    // it needs no tm_gmtoff, which MSVC's struct tm does not have.
    return uint32_t(uint64_t(int64_t(timegm(&lt))) + 2082844800ULL);
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

// macOS only exposes OpenGL 3.2+ core contexts through NSGL. Other hosts keep
// the long-standing 3.0 request, which is also what the Emscripten path uses.
// Return the matching GLSL preamble expected by ImGui's OpenGL3 backend.
static const char* configureGlfwOpenGl() {
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    return "#version 150";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    return "#version 130";
#endif
}

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
    float accX = 0, accY = 0;            // sub-pixel remainder
    float zoom = 2.0f;                   // host px per guest px, live
    double lastX = 0, lastY = 0;         // virtual cursor while captured

    template <typename MoveFn, typename ButtonFn>
    void frame(GLFWwindow* win, GLuint tex, ImVec2 size,
               MoveFn move, ButtonFn button) {
        ImGuiIO& io = ImGui::GetIO();
        // `size` arrives as the guest framebuffer at 2x, which was the whole
        // story while the screen lived in an auto-resizing window. Docked, the
        // node is whatever the user dragged it to, so fit inside it and keep
        // the aspect ratio — then record the zoom actually used, because the
        // pointer deltas below must divide by that and not by a hardcoded 2.
        const ImVec2 native(size.x * 0.5f, size.y * 0.5f);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (size.x > 0 && size.y > 0 && avail.x > 32 && avail.y > 32) {
            float s = avail.x / size.x;
            if (avail.y / size.y < s) s = avail.y / size.y;
            size = ImVec2(size.x * s, size.y * s);
        }
        zoom = native.x > 0 ? size.x / native.x : 2.0f;
        if (zoom < 0.05f) zoom = 0.05f;
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
            button(0, glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
            button(1, glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        } else if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            feed(io.MouseDelta.x, io.MouseDelta.y, move);
            button(0, io.MouseDown[0]);  // releases seen while still active
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
        else    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

private:
    template <typename MoveFn>
    void feed(float hx, float hy, MoveFn move) {
        accX += hx / zoom;               // host px -> guest px, live zoom
        accY += hy / zoom;
        int dx = int(accX), dy = int(accY);
        if (dx || dy) { move(dx, dy); accX -= dx; accY -= dy; }
    }
};

// ── Mechanical drive sounds ─────────────────────────────────────────────
// Two FloppySound instances (MAME sample sets, assets/floppy_samples/):
// the 3.5" set voices the Sony drives, the chunkier 5.25" set stands in
// for the SCSI hard disks (POM2 SmartPort precedent: spin loop kept
// alive by per-access step events, auto-retired when the bus goes
// idle). POM68K_DRIVE_SFX=0 starts muted; the Machine menu toggles.
// POM68K_KEY_TRACE=1 — stderr log of every GUI key event at PUSH (UI
// thread) and APPLY (machine thread). A freeze where pushes continue but
// applies stop = machine-thread wedge; both stopping = GUI-side.
static void keyTrace(const char* where, uint8_t adb, bool down) {
    static const bool on = std::getenv("POM68K_KEY_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[key] %s adb=%02X %s\n", where, adb, down ? "dn" : "up");
}

// Two host keys can share one Mac transition code (for example the two
// Command keys). Count presses per code so the DOWN transition is
// emitted only on 0->1 and the UP transition only on 1->0: without this,
// holding Left Shift, adding Right Shift and releasing Left sent down/down/up,
// so the System cleared the KeyMap bit while Shift was still physically held
// and the following characters came out lowercase.
static uint8_t g_keyHeld[128];
static bool keyDown(uint8_t code, ImGuiKey k) {
    if (!ImGui::IsKeyPressed(k, false)) return false;
    return ++g_keyHeld[code & 0x7F] == 1;
}
static bool keyUp(uint8_t code, ImGuiKey k) {
    if (!ImGui::IsKeyReleased(k)) return false;
    uint8_t& n = g_keyHeld[code & 0x7F];
    if (!n) return false;
    return --n == 0;
}

static FloppySound gFloppySfx;
static FloppySound gHddSfx;

static void initDriveSfx(MacAudioHost& host) {
    static bool inited = false;
    if (!inited) {
        inited = true;
        std::string probe = findPath("assets/floppy_samples/35_step_1_1.wav");
        const std::string dir = probe.empty()
            ? std::string("assets/floppy_samples")
            : probe.substr(0, probe.find_last_of('/'));
        const bool flOk = gFloppySfx.loadSamples(dir, FloppySound::FormFactor::FF35);
        const bool hdOk = gHddSfx.loadSamples(dir, FloppySound::FormFactor::FF525);
        if (!flOk || !hdOk)
            std::fprintf(stderr, "sfx: drive samples not found under %s "
                                 "(mechanical sounds off)\n", dir.c_str());
        gHddSfx.setVolume(0.25f);            // background whirr, not a drive
        gHddSfx.setAutoMotorOff(1500.0);     // retire after 1.5 s idle bus
        if (const char* e = std::getenv("POM68K_DRIVE_SFX"); e && e[0] == '0') {
            gFloppySfx.setMuted(true);
            gHddSfx.setMuted(true);
        }
    }
    host.attachFx(&gFloppySfx);
    host.attachFx(&gHddSfx);
}

// ── Machine profile menu ────────────────────────────────────────────────
// Main-menu-bar "Machine": pick Plus / Mac II / LC II / Quadra 605.
// Selecting another machine relaunches the process on its ROM — clean
// state, since each machine is built once at startup (ROM size alone
// selects the machine in main()).
// Appended, never inserted: the order is the Machine menu's, and an
// insertion would silently renumber every kind a running relaunch compares.
// `Duo` = platform #12 (MSC + PG&E), the first PowerBook (2026-08-06).
enum class MachineKind { Plus, Se, SeFdhd, MacClassic, MacII, IIfx, Lc, LcII, ClassicII, ColorClassic, MacTv, IIsi, IIci, Lc3, Aio, Vasp, Centris, Q700, Q630, Quadra, Duo };
static std::vector<std::string> gSwitchArgs;   // argv[1..] for the relaunch

// ── CPU engine selection (interpreter vs JIT) ───────────────────────────
// Global, like the AppleTalk window below, so the "CPU" menu appears on
// every machine without touching the ten machineMenu() call sites. The
// hooks are installed by the machines that HAVE a second engine — the four
// 68040 loops, the 68030 ones (V8, Sonora, VASP, RBV) since 2026-07-30, and
// the Mac II family + the compacts since 2026-08-06 — every loop but the
// IIfx's, which has no engine wired yet.
static bool gShowJit = false;
static std::function<void(int)> gSetCpuEngine;         // 0 = interpreter, 1 = JIT
static std::function<int()> gGetCpuEngine;
static std::function<jit::Stats::Snapshot()> gJitStats;
static const char* gJitBackend = nullptr;              // backend chosen for this host

// ── Save states (TODO § C GUI wiring) ───────────────────────────────────
// Shared plumbing embedded in each machine-thread struct: the GUI queues a
// request; the MACHINE thread performs the save/load between two quanta
// (the Cmd::CpuEngine precedent — a restore replaces the whole tree, so it
// must land between two instructions, never mid-quantum from the GUI
// thread) and posts a one-line outcome the machine window displays. The
// run function fills in the profile tag and the state-file path (tagged
// like the .pram file, so states pair with their boot volume).
struct SaveStateSlot {
    pom68k::SnapMachine kind{};        // 0 = profile not wired
    std::string path;

    void request(bool load) {
        std::lock_guard<std::mutex> l(mu_);
        pending_ |= load ? 2 : 1;
    }
    std::string message() {
        std::lock_guard<std::mutex> l(mu_);
        return message_;
    }

    // Machine-thread side, called from applyCmds() (between quanta).
    // Returns what actually happened (bit 0 = saved, bit 1 = restored) so
    // single-threaded callers (the Plus loop) can resync their frame clock
    // after a restore.
    template <class Mem, class Cpu>
    int apply(Mem& mem, Cpu& cpu) {
        int p;
        { std::lock_guard<std::mutex> l(mu_); p = pending_; pending_ = 0; }
        if (!p) return 0;
        int done = 0;
        if (kind == pom68k::SnapMachine{} || path.empty()) {
            post("Save states: profil non câblé");
            return 0;
        }
        if (p & 1) {
            std::vector<uint8_t> blob;
            pom68k::save(mem, cpu, kind, blob);
            // Atomic temp+rename, the floppy write-back convention: a crash
            // mid-write must never leave a truncated state file behind.
            const std::string tmp = path + ".tmp";
            std::FILE* f = std::fopen(tmp.c_str(), "wb");
            if (!f || std::fwrite(blob.data(), 1, blob.size(), f) != blob.size()) {
                if (f) std::fclose(f);
                std::remove(tmp.c_str());
                post("État NON sauvé: écriture impossible (" + tmp + ")");
            } else {
                std::fclose(f);
                if (std::rename(tmp.c_str(), path.c_str()) != 0) {
                    std::remove(tmp.c_str());
                    post("État NON sauvé: rename impossible (" + path + ")");
                } else {
                    post("État sauvé → " + path + " ("
                         + std::to_string((blob.size() + 512) / 1024) + " Ko)");
                    done |= 1;
                }
            }
        }
        if (p & 2) {
            std::vector<uint8_t> blob;
            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) {
                post("Aucun état à restaurer (" + path + ")");
            } else {
                std::fseek(f, 0, SEEK_END);
                long n = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                blob.resize(n > 0 ? size_t(n) : 0);
                size_t got = blob.empty() ? 0
                           : std::fread(blob.data(), 1, blob.size(), f);
                std::fclose(f);
                std::string err;
                if (got != blob.size()) {
                    post("État NON restauré: lecture tronquée (" + path + ")");
                } else if (!pom68k::load(mem, cpu, kind,
                                         blob.data(), blob.size(), err)) {
                    // A refused snapshot leaves the machine untouched — the
                    // reason (profile/ROM/RAM mismatch, corruption) is
                    // load()'s own explanation.
                    post("État NON restauré: " + err);
                } else {
                    post(err.empty() ? "État restauré ← " + path
                                     : "État restauré (" + err + ")");
                    done |= 2;
                }
            }
        }
        return done;
    }

private:
    void post(std::string m) {
        std::lock_guard<std::mutex> l(mu_);
        message_ = std::move(m);
        std::printf("SaveState: %s\n", message_.c_str());
    }
    std::mutex mu_;
    int pending_ = 0;                  // bit 0 = save, bit 1 = load
    std::string message_;
};

// The "État" row every machine window shows: Sauver / Restaurer + the last
// outcome. One helper so the ten windows stay in step.
static void saveStateUi(SaveStateSlot& slot) {
    if (ImGui::Button("Sauver l'état")) slot.request(false);
    ImGui::SameLine();
    if (ImGui::Button("Restaurer l'état")) slot.request(true);
    const std::string msg = slot.message();
    if (!msg.empty()) ImGui::TextWrapped("%s", msg.c_str());
}

// ── AppleTalk window (in-process stack visibility + toggles) ────────────
static bool gShowAtalk = false;
// A green/red status bullet. ImGui::Bullet() draws via the draw list (no
// glyph needed — the default font lacks ● ○) and ends with a SameLine, so
// the label follows on the same row.
static void dot(bool ok, const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ok ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                             : ImVec4(0.9f, 0.4f, 0.35f, 1));
    ImGui::Bullet();
    ImGui::PopStyleColor();
    ImGui::TextUnformatted(label);
}
static void appleTalkWindow() {
    if (!gShowAtalk) return;
    AtalkHub::Snapshot s = g_atalk.snapshot();
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AppleTalk", &gShowAtalk, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!s.attached || !atalkEnabled()) {
        ImGui::TextUnformatted("Pile AppleTalk interne désactivée "
                               "(POM68K_APPLETALK=0).");
        ImGui::End();
        return;
    }
    bool stackOn = s.cfg.stack;
    if (ImGui::Checkbox("Réseau AppleTalk actif", &stackOn))
        g_atalk.setService("stack", stackOn);
    ImGui::SameLine();
    ImGui::TextDisabled(s.cableUp ? "(câble LToUDP: relié)" : "(câble LToUDP: local)");

    ImGui::SeparatorText("Noeud / routeur");
    char guest[16];
    if (s.net.guestNode) std::snprintf(guest, sizeof guest, "%u", s.net.guestNode);
    else std::strcpy(guest, "aucun");
    char routerLine[80];
    std::snprintf(routerLine, sizeof routerLine,
                  "Reseau 2, noeud serveur %u, zone \"%s\"", s.node, s.zone.c_str());
    dot(s.cfg.stack, routerLine);
    ImGui::Text("Invite vu : %s   -   trames recues %ld / emises %ld",
                guest, s.net.framesIn, s.net.framesOut);
    ImGui::Text("Recherches NBP servies : %ld   -   transactions ATP : %ld",
                s.net.nbpLookups, s.net.atpReqIn);
    if (s.net.atpDupReqs || s.net.atpDupPending) {
        // Diagnostic, not just a count: the lossless wire cannot DROP a
        // reply, so a retransmit means it arrived too LATE. The lag says
        // which timer fired and the backlog/hold say whether the wire was
        // congested when it did (see AtalkStack::Stats).
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1),
                           "Retransmissions client : %ld  (dernier retard %ld ms, "
                           "max %ld ms)", s.net.atpDupReqs,
                           s.net.atpDupLagLastMs, s.net.atpDupLagMaxMs);
        if (s.net.atpDupPending)
            ImGui::TextDisabled("  dont %ld pendant le service (serveur lent, "
                                "pas le fil)", s.net.atpDupPending);
        ImGui::TextDisabled("  file d'injection %zu (max %zu)  -  attente max "
                            "%ld ms  -  POM68K_ATALK_DEBUG=1 pour le detail",
                            s.wire.backlog, s.wire.backlogMax, s.wireHoldMaxMs);
    } else {
        ImGui::TextDisabled("Retransmissions client : 0 (fil sans perte)");
    }
    if (s.wire.drops)
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1),
                           "Debordement du fil : %ld trames  (l'invite a cesse "
                           "d'ecouter assez longtemps pour saturer la file)",
                           s.wire.drops);

    ImGui::SeparatorText("Partage de fichiers (AppleShare / AFP)");
    bool afpOn = s.cfg.afp;
    if (ImGui::Checkbox("Activer AppleShare", &afpOn)) g_atalk.setService("afp", afpOn);
    dot(s.afp.registered, "Visible dans le Sélecteur (NBP AFPServer)");
    dot(s.afp.dirOk, s.afp.dirOk ? "Dossier partagé accessible en écriture"
                                 : "Dossier partagé INTROUVABLE / lecture seule");
    ImGui::Text("Nom serveur : %s", s.afp.serverName.c_str());
    ImGui::Text("Volume : %s", s.afp.volName.c_str());
    ImGui::TextWrapped("Dossier hôte : %s",
                       s.afp.dirPath.empty() ? "(non défini)" : s.afp.dirPath.c_str());
    ImGui::Text("Sessions : %d%s   ·   utilisateur : %s", s.afp.sessions,
                s.afp.volMounted ? " (volume monté)" : "",
                s.afp.lastUser.empty() ? "-" : s.afp.lastUser.c_str());
    ImGui::Text("Dernière commande : %s   ·   lu %ld o / écrit %ld o",
                s.afp.lastCmd.empty() ? "-" : s.afp.lastCmd.c_str(),
                s.afp.bytesRead, s.afp.bytesWritten);

    ImGui::SeparatorText("Imprimante (LaserWriter / PAP)");
    bool papOn = s.cfg.pap;
    if (ImGui::Checkbox("Activer l'imprimante", &papOn)) g_atalk.setService("pap", papOn);
    dot(s.pap.registered, "Visible dans le Sélecteur (NBP LaserWriter)");
    ImGui::Text("Nom : %s", s.pap.printerName.c_str());
    ImGui::Text("État : %s%s", s.pap.state.c_str(), s.pap.busy ? "  (occupée)" : "");
    ImGui::Text("Travaux imprimés : %ld   ·   dernier : %s", s.pap.jobs,
                s.pap.lastJob.empty() ? "-" : s.pap.lastJob.c_str());
    ImGui::TextDisabled("Spool → CUPS (lp) si présent, sinon %s/", s.pap.spoolDir.c_str());

    ImGui::SeparatorText("Internet (MacIP / IP-in-DDP)");
    bool ipOn = s.cfg.macip;
    if (ImGui::Checkbox("Activer la passerelle MacIP", &ipOn))
        g_atalk.setService("macip", ipOn);
    dot(s.macip.registered, "Passerelle visible (NBP IPGATEWAY)");
    bool ipWorks = s.macip.registered && s.macip.leases > 0;
    dot(ipWorks, ipWorks ? "MacIP fonctionne (bail attribué)"
                         : "MacIP en attente (aucun invité connecté)");
    ImGui::Text("Passerelle : %s   ·   DNS : %s", s.macip.gwIp.c_str(),
                s.macip.dns.c_str());
    ImGui::Text("Baux : %d   ·   dernier : %s   ·   flux UDP %d / TCP %d",
                s.macip.leases,
                s.macip.lastLease.empty() ? "-" : s.macip.lastLease.c_str(),
                s.macip.udpFlows, s.macip.tcpConns);
    ImGui::Text("IP invite -> net %ld   -   net -> invite %ld",
                s.macip.ipFromGuest, s.macip.ipToGuest);
    ImGui::TextDisabled("HTTP uniquement (TLS 2026 hors d'atteinte) — "
                        "frogfind.com, theoldnet.com");

    ImGui::End();
}

// ── JIT statistics window ("CPU → Statistiques JIT...") ─────────────────
// The point of having a second engine you can switch by hand is being able
// to SEE what it does, so this window shows the split between engines, the
// block cache, and — the interesting one — why blocks hand control back.
static void jitWindow() {
    if (!gShowJit || !gJitStats) return;
    const jit::Stats::Snapshot s = gJitStats();
    const int eng = gGetCpuEngine ? gGetCpuEngine() : 0;

    // Guest instructions per second, differentiated over the host clock.
    // The engine itself never reads a wall clock — that stays a GUI concern.
    static uint64_t lastTotal = 0;
    static double rate = 0;
    static std::chrono::steady_clock::time_point lastAt{};
    const auto now = std::chrono::steady_clock::now();
    const uint64_t total = s.instrs + s.interpInstrs;
    if (lastAt.time_since_epoch().count()) {
        const double dt = std::chrono::duration<double>(now - lastAt).count();
        if (dt >= 0.25) {
            rate = double(total - lastTotal) / dt;
            lastTotal = total; lastAt = now;
        }
    } else {
        lastTotal = total; lastAt = now;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Moteur accéléré", &gShowJit, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Moteur");
    dot(eng == 1, eng == 1 ? "Moteur accéléré actif" : "Interpréteur Moira (défaut)");
    ImGui::Text("Backend : %s", gJitBackend ? gJitBackend : "-");
    // The counters below are the ENGINE's; while the interpreter drives the
    // machine the engine sees nothing, so the rate would sit frozen at the
    // last JIT value. Say so rather than showing a stale number.
    if (eng == 1) ImGui::Text("Instructions/s : %.2f M", rate / 1e6);
    else          ImGui::TextDisabled("Instructions/s : — (compteurs du moteur accéléré, à l'arrêt)");

    ImGui::SeparatorText("Répartition");
    const double all = double(total ? total : 1);
    ImGui::Text("Par le moteur       : %llu  (%.1f %%)",
                (unsigned long long)s.instrs, 100.0 * double(s.instrs) / all);
    ImGui::Text("Par l'interpréteur  : %llu  (%.1f %%)",
                (unsigned long long)s.interpInstrs,
                100.0 * double(s.interpInstrs) / all);

    ImGui::SeparatorText("Blocs");
    ImGui::Text("Compilés %llu   ·   vivants %llu   ·   rejoués %llu",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksLive,
                (unsigned long long)s.blocksRun);
    const double reuse = s.blocksCompiled
                       ? double(s.blocksRun) / double(s.blocksCompiled) : 0.0;
    ImGui::Text("Réutilisation : %.1f rejeux par bloc compilé", reuse);
    ImGui::Text("Purges %llu   ·   invalidations %llu",
                (unsigned long long)s.flushes,
                (unsigned long long)s.invalidations);

    ImGui::SeparatorText("Fenêtre de code");
    const unsigned long long arms = (unsigned long long)s.windowArmed;
    const unsigned long long fails = (unsigned long long)s.windowFailed;
    ImGui::Text("Validations %llu   ·   refusées %llu  (%.1f %%)", arms, fails,
                arms ? 100.0 * double(fails) / double(arms) : 0.0);
    ImGui::TextDisabled("Refus = I/O, VRAM, overlay encore actif, ou page pas "
                        "encore dans l'ATC.");

    ImGui::SeparatorText("Sorties de bloc (par cause)");
    for (int i = 0; i < int(jit::Exit::Count); i++) {
        if (!s.exits[i]) continue;
        ImGui::Text("%-16s %llu", jit::exitName(jit::Exit(i)),
                    (unsigned long long)s.exits[i]);
    }
    ImGui::TextDisabled("Toute sortie se fait à une frontière d'instruction, "
                        "état invité exact.");
    ImGui::End();
}

static void machineMenu(MachineKind cur, GLFWwindow* window,
                        const std::function<void()>& extraMenus = {}) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Machine")) {
        // rom = canonical short name (a convenience symlink); sig = the CRC32
        // signature scanned for under roms/ when the short name is absent, so
        // the exact dated filename is never hardcoded.
        // Some ROMs serve two models that differ only by clock / CPU / model
        // ID (LC III vs LC III+; LC 475 vs Quadra 605). envKey/envVal tag the
        // variant; `dflt` marks the one selected when the env is unset. The
        // relaunch inherits the process env, so setenv() here is enough.
        // `group` = the machine PLATFORM (gate array / bus front end), the
        // same seven-way split DEV.md is organised around: every entry under
        // a heading shares one memory-map + I/O implementation and differs
        // only by clock / CPU / model ID / MCU. Entries stay grouped in
        // declaration order — the loop emits a SeparatorText whenever `group`
        // changes, so reordering the table reorders the menu.
        struct Profile { const char* group; const char* label; MachineKind kind;
                         const char* rom;
                         const char* sig; const char* envKey; const char* envVal;
                         bool dflt; };
        const char* kGlue = "GLUE + NuBus (Mac II)";
        const char* kOss = "OSS + IOP (IIfx)";
        const char* kV8 = "V8 / Eagle / Spice / Tinker Bell";
        const char* kRbv = "RBV (video en RAM)";
        const char* kSonora = "Sonora";
        const char* kVasp = "VASP (Sonora + peripheriques V8)";
        const char* kMemc = "MEMCjr + PrimeTime";
        const char* kDjmemc = "djMEMC + IOSB";
        const char* kSpike = "Discret 040 (Quadra 700/900/950)";
        const char* kF108 = "F108 + PrimeTime II + Valkyrie";
        const char* kMsc = "MSC + PG&E (PowerBook Duo)";
        const Profile kProfiles[] = {
            { "68000", "Macintosh Plus", MachineKind::Plus, "roms/macplus.rom", nullptr, nullptr, nullptr, true },
            { "68000", "Macintosh SE", MachineKind::Se, "roms/macse.rom", "B2E362A8", nullptr, nullptr, true },
            { "68000", "Macintosh SE FDHD", MachineKind::SeFdhd, "roms/macsefd.rom", "B306E171", nullptr, nullptr, true },
            { "68000", "Macintosh Classic", MachineKind::MacClassic, "roms/macclassic.rom", "A49F9914", nullptr, nullptr, true },
            // Tagged like its siblings: with envKey == nullptr, variantCur
            // defaulted to true, so on ANY Mac II-family machine this row
            // computed isCur and the "&& !isCur" guard swallowed the click —
            // the plain Mac II was unreachable from the Machine menu.
            { kGlue, "Macintosh II", MachineKind::MacII, "roms/macii.rom", "9779D2C4", "POM68K_MACII_MODEL", "ii", false },
            { kGlue, "Macintosh IIx", MachineKind::MacII, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "iix", true },
            { kGlue, "Macintosh IIcx", MachineKind::MacII, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "iicx", false },
            { kGlue, "Macintosh SE/30", MachineKind::MacII, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "se30", false },
            { kOss, "Macintosh IIfx (40 MHz)", MachineKind::IIfx, "roms/maciifx.rom", "4147DD77", nullptr, nullptr, true },
            { kRbv, "Macintosh IIci", MachineKind::IIci, "roms/maciici.rom", "368CADFE", nullptr, nullptr, true },
            { kRbv, "Macintosh IIsi", MachineKind::IIsi, "roms/maciisi.rom", "36B7FB6C", nullptr, nullptr, true },
            { kV8, "Macintosh LC", MachineKind::Lc, "roms/maclc.rom", "350EACF0", nullptr, nullptr, true },
            { kV8, "Macintosh LC II", MachineKind::LcII, "roms/maclcii.rom", "35C28F5F", nullptr, nullptr, true },
            { kV8, "Macintosh Classic II", MachineKind::ClassicII, "roms/classic2.rom", "3193670E", nullptr, nullptr, true },
            { kV8, "Macintosh Color Classic", MachineKind::ColorClassic, "roms/cclassic.rom", "ECD99DC0", nullptr, nullptr, true },
            { kV8, "Macintosh TV", MachineKind::MacTv, "roms/mactv.rom", "EAF1678D", nullptr, nullptr, true },
            { kSonora, "Macintosh LC III", MachineKind::Lc3, "roms/maclc3.rom", "ECBBC41C", "POM68K_LC3_PLUS", "0", true },
            { kSonora, "Macintosh LC III+ (33 MHz)", MachineKind::Lc3, "roms/maclc3.rom", "ECBBC41C", "POM68K_LC3_PLUS", "1", false },
            { kSonora, "Macintosh LC 520", MachineKind::Aio, "roms/maclc520.rom", "EDE66CBD", "POM68K_AIO_ID", "A55A0100", true },
            { kSonora, "Macintosh LC 550 (33 MHz)", MachineKind::Aio, "roms/maclc520.rom", "EDE66CBD", "POM68K_AIO_ID", "A55A0101", false },
            { kSonora, "Macintosh Color Classic II", MachineKind::Aio, "roms/maclc520.rom", "EDE66CBD", "POM68K_AIO_ID", "CC2", false },
            { kVasp, "Macintosh IIvx", MachineKind::Vasp, "roms/maciivx.rom", "4957EB49", "POM68K_IIVI", "0", true },
            { kVasp, "Macintosh IIvi (16 MHz)", MachineKind::Vasp, "roms/maciivx.rom", "4957EB49", "POM68K_IIVI", "1", false },
            { kMemc, "Macintosh LC 475", MachineKind::Quadra, "roms/quadra605.rom", "FF7439EE", "POM68K_Q605_ID", "A55A2221", true },
            { kMemc, "Macintosh LC 575 (33 MHz)", MachineKind::Quadra, "roms/quadra605.rom", "FF7439EE", "POM68K_Q605_ID", "A55A222E", false },
            { kMemc, "Quadra 605", MachineKind::Quadra, "roms/quadra605.rom", "FF7439EE", "POM68K_Q605_ID", "A55A2225", false },
            { kDjmemc, "Macintosh Centris 610 (20 MHz)", MachineKind::Centris, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "c610", false },
            { kDjmemc, "Macintosh Centris 650", MachineKind::Centris, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "c650", true },
            { kDjmemc, "Macintosh Quadra 610", MachineKind::Centris, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "q610", false },
            { kDjmemc, "Macintosh Quadra 650 (33 MHz)", MachineKind::Centris, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "q650", false },
            { kDjmemc, "Macintosh Quadra 800 (33 MHz)", MachineKind::Centris, "roms/centris650.rom", "F1A6F343", "POM68K_CENTRIS_MODEL", "q800", false },
            { kSpike, "Macintosh Quadra 700", MachineKind::Q700, "roms/quadra700.rom", "420DBFF3", "POM68K_Q700_MODEL", "q700", true },
            // The "Eclipse" towers: the same board plus the Mac IIfx's front
            // end (two Apple PIC IOPs for SCC and SWIM/ADB, the Egret in
            // place of the discrete RTC, a second 53C96 bus). The Q900
            // shares the Quadra 700's ROM — only the env tells them apart —
            // while the Q950 has its own $3DC27823 dump and is therefore
            // also reachable by dropping that ROM in.
            { kSpike, "Macintosh Quadra 900 (IOP)", MachineKind::Q700, "roms/quadra700.rom", "420DBFF3", "POM68K_Q700_MODEL", "q900", false },
            { kSpike, "Macintosh Quadra 950 (33 MHz, IOP)", MachineKind::Q700, "roms/quadra950.rom", "3DC27823", "POM68K_Q700_MODEL", "q950", false },
            { kF108, "Macintosh Quadra 630 (33 MHz)", MachineKind::Q630, "roms/quadra630.rom", "06684214", "POM68K_Q630_ID", "A55A2252", true },
            { kF108, "Macintosh LC / Performa 580", MachineKind::Q630, "roms/quadra630.rom", "06684214", "POM68K_Q630_ID", "A55A225A", false },
            // The 37th profile (2026-08-06): platform #12's first GUI
            // citizen. envKey stays null the way the IIfx's does — that is
            // only safe because MachineKind::Duo has exactly ONE row, so
            // `kindCur` alone decides `isCur`. The Mac II bug above happened
            // because FOUR rows shared one kind: add a Duo 210/250 row here
            // and it must carry an envKey/envVal pair, or both rows will
            // claim to be current and neither will be clickable.
            { kMsc, "PowerBook Duo 230 (33 MHz)", MachineKind::Duo, "roms/macduo230.rom", "ECFA989B", nullptr, nullptr, true },
        };
        const char* lastGroup = nullptr;
        for (const Profile& pr : kProfiles) {
            if (!lastGroup || std::strcmp(lastGroup, pr.group) != 0) {
                ImGui::SeparatorText(pr.group);
                lastGroup = pr.group;
            }
            bool kindCur = pr.kind == cur;
            bool variantCur = true;              // variant match within a kind
            if (pr.envKey) {
                const char* e = getenv(pr.envKey);
                variantCur = e ? (std::strstr(e, pr.envVal) != nullptr) : pr.dflt;
            }
            bool isCur = kindCur && variantCur;
            std::string path = findPath(pr.rom);
            if (path.empty() && pr.sig) path = findRomBySignature(pr.sig);
            if (ImGui::MenuItem(pr.label, nullptr, isCur,
                                isCur || !path.empty()) && !isCur) {
                if (pr.envKey) {
                    setenv(pr.envKey, pr.envVal, 1);
                    // LC 475 / LC 575 are 68LC040; the Quadra 605 keeps the FPU.
                    if (!std::strcmp(pr.envKey, "POM68K_Q605_ID")) {
                        if (std::strstr(pr.envVal, "2221") ||
                            std::strstr(pr.envVal, "222E")) setenv("POM68K_Q605_NOFPU", "1", 1);
                        else unsetenv("POM68K_Q605_NOFPU");
                    }
                }
                gSwitchArgs = { path };
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        ImGui::Separator();
        bool sfx = !gFloppySfx.isMuted();
        if (ImGui::MenuItem("Sons des lecteurs", nullptr, sfx,
                            gFloppySfx.isLoaded())) {
            gFloppySfx.setMuted(sfx);
            gHddSfx.setMuted(sfx);
        }
        ImGui::EndMenu();
    }
    // ── CPU: which execution engine drives this machine ──────────────────
    // The interpreter is the default and the reference; the JIT sits beside
    // it and is switched live, between two instructions, through the machine
    // thread's command queue (DafbMachine::Cmd::CpuEngine).
    if (ImGui::BeginMenu("CPU")) {
        const bool hasJit = bool(gSetCpuEngine);
        const int eng = hasJit ? gGetCpuEngine() : 0;
        if (ImGui::MenuItem("Interpréteur (Moira)", nullptr, eng == 0, hasJit) &&
            eng != 0) {
            gSetCpuEngine(0);
        }
        // Honest labelling (2026-07-28): the default accelerated engine is
        // NOT a JIT — it is the interpreter running behind a fetch window
        // (and, per backend, a block replayer or a code generator). Only
        // the x86-64 backend actually emits machine code. Users deserve
        // the distinction; the internal names (src/jit/, POM68K_JIT_*)
        // stay, because they name the subsystem, not the technique.
        char label[80];
        const bool codegen = gJitBackend && std::strcmp(gJitBackend, "threaded") != 0;
        std::snprintf(label, sizeof(label),
                      codegen ? "Moteur accéléré — JIT %s"
                              : "Moteur accéléré — fenêtres (%s)",
                      gJitBackend ? gJitBackend : "?");
        if (ImGui::MenuItem(label, nullptr, eng == 1, hasJit) && eng != 1) {
            gSetCpuEngine(1);
        }
        ImGui::Separator();
        ImGui::MenuItem("Statistiques du moteur...", nullptr, &gShowJit, hasJit);
        if (!hasJit)
            ImGui::TextDisabled("(interrupteur : machines 68030/68040 —\n"
                                "Mac II / compacts : interpréteur seul)");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Réseau")) {
        ImGui::MenuItem("AppleTalk...", nullptr, &gShowAtalk,
                        atalkEnabled());
        if (!atalkEnabled())
            ImGui::TextDisabled("(POM68K_APPLETALK=0)");
        ImGui::EndMenu();
    }
    pom68k::dockLayoutMenu();
    if (extraMenus) extraMenus();
    ImGui::TextDisabled("|  Delete: capture mouse");
    ImGui::EndMainMenuBar();
    // Every runner goes through machineMenu, so the docked shell is
    // installed in exactly one place. Must follow EndMainMenuBar: the
    // viewport work area only excludes the bar once it has been drawn.
    pom68k::dockLayoutFrame();
    appleTalkWindow();
    jitWindow();
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



// ── Mac II machine thread ───────────────────────────────────────────────
// Same GUI ↔ machine contract as LcMachine: queued commands, published
// framebuffer + status. Video is NuBus Toby (640×480); sound is discrete
// ASC @ $50F14000. Frame slice ≈ 60.15 Hz at 15.6672 MHz.
struct MacIiMachine {
    MacIIMemory& mem; Cpu020& cpu; MacAudioHost& audioHost;
    MacIiMachine(MacIIMemory& m, Cpu020& c, MacAudioHost& a)
        : mem(m), cpu(c), audioHost(a) {
        stEngine_.store(cpu.engine(), std::memory_order_relaxed);
    }
    ~MacIiMachine() { stop(); }

    // Engine state + JIT gauges for the CPU menu (the LcMachine contract:
    // the menu tick follows the MACHINE; the swap lands one queue trip
    // later, on the machine thread).
    int cpuEngine() const { return stEngine_.load(std::memory_order_relaxed); }
    jit::Stats::Snapshot jitStats() const {
        std::lock_guard<std::mutex> l(jitMu_);
        return jitSnap_;
    }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, CpuEngine,
                          InsertFloppy, EjectFloppy } t; int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    // Save-state requests (GUI → machine thread; see SaveStateSlot above).
    SaveStateSlot state;

    // Floppy hot-swap (GUI → machine thread; the DafbMachine contract).
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
    std::string floppyPending_;              // guarded by cmdMu_
    std::atomic<bool> floppyFlag_{false};

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
        Se30Video* sv = mem.se30();
        int hres = tv ? tv->hres() : sv ? Se30Video::W : TobyVideo::W;
        int vres = tv ? tv->vres() : sv ? Se30Video::H : TobyVideo::H;
        // `fb_` is the RASTER SURFACE — runOne() decoded each row as the
        // beam scanned it. Catch up once more so a paused machine still
        // publishes a complete frame.
        if (tv || sv) rasterBeam();
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
        {
            std::lock_guard<std::mutex> l(jitMu_);
            jitSnap_ = cpu.jit().stats().snapshot();
        }
    }

private:
    static constexpr int64_t kFrame = MacIIMemory::kCpuHz / 60;
    static constexpr size_t kTarget = 2225;

    void runOne() {
        // Raster catch-up rides the wire slicing (LLE_VS_HLE §1.1): the
        // Toby card runs its own CRTC frame clock, while the SE/30's
        // pseudo-slot video has none and rides the machine's 60 Hz one.
        runQuantumWithWire(mem, cpu, kFrame, [this] { rasterBeam(); });
        framesRun_++;
    }
    void rasterBeam() {
        if (TobyVideo* tv = mem.toby()) tv->raster(fb_);
        else if (Se30Video* sv = mem.se30())
            sv->raster(fb_, mem.framePos(), mem.frameCycles(), mem.frameCount());
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
        std::string pending;
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_);
          pending.swap(floppyPending_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.b != 0, c.a); break;
            case Cmd::Key:         keyTrace("apply", uint8_t(c.a), c.b != 0);
                               mem.keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            case Cmd::InsertFloppy:
                if (!pending.empty() && mem.insertDisk(pending))
                    floppyFlag_.store(true, std::memory_order_relaxed);
                break;
            case Cmd::EjectFloppy:
                mem.ejectDisk();
                floppyFlag_.store(false, std::memory_order_relaxed);
                break;
            // Engine swap between two runCycles() — an instruction
            // boundary (the LcMachine precedent).
            case Cmd::CpuEngine:
                cpu.setEngine(c.a);
                stEngine_.store(c.a, std::memory_order_relaxed);
                break;
        }
        cmdsApply_.clear();
        state.apply(mem, cpu);         // save/load between two quanta
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
    std::atomic<int> stEngine_{0};           // 0 = interpreter, 1 = JIT
    mutable std::mutex jitMu_;
    jit::Stats::Snapshot jitSnap_{};
    int activeHold_ = 0;
    int starve_ = 0;
    int framesRun_ = 0;
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;
    std::vector<float> samp_;
};

// ── Macintosh II: GLUE + 68020 + Toby NuBus, selected by a 256 KB ROM ───
static int runMacII(std::vector<uint8_t> rom, const std::string& romName,
                    int argc, char** argv,
                    MacIIMemory::Model model = MacIIMemory::Model::MacII) {
    const bool is030 = model != MacIIMemory::Model::MacII;
    const bool se30 = model == MacIIMemory::Model::SE30;
    const char* name = model == MacIIMemory::Model::IIx  ? "IIx"
                     : model == MacIIMemory::Model::IIcx ? "IIcx"
                     : se30 ? "SE/30" : "II";
    std::printf("Machine: Macintosh %s (%s @ 15.6672 MHz, %s%s)\n",
                name, is030 ? "68030 + PMMU" : "68020",
                se30 ? "512×342 interne" : "Toby NuBus",
                getenv("POM68K_NOFPU") ? "" : (is030 ? ", soft 68882"
                                                     : ", soft 68881"));
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static MacIIMemory mem(0x800000, model);
    static Cpu020 cpu(mem, getenv("POM68K_NOFPU") == nullptr, is030);
    static MacAudioHost audioHost;
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "FAIL: bad Mac II ROM\n");
        return 1;
    }
    if (se30) {
        if (!mem.installSe30Video())
            std::fprintf(stderr, "SE/30: se30vrom.uk6 manquante (roms/se30/) — pas de video\n");
    } else {
        mem.installTobyVideo();
    }
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.rtc().setSeconds(hostMacSeconds());      // GUI: real local date/time
    wireLocalTalk(mem);

    // Prefer Infinite Mac System 6.0.8 HD, then HD20SC / other SCSI images.
    std::string hddPath = (argc > 2) ? argv[2] : findPath("hdv/System 6.0.8 HD.dsk");
    if (hddPath.empty()) hddPath = findPath("hdv/HD20SC.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    // Battery-backed PRAM (discrete RTC XPRAM). Tagged with the machine
    // like every later profile: an untagged "<image>.pram" would let the
    // four Mac II-family boards trade XPRAM through a shared boot volume.
    // The clock is NOT in the file — a real RTC's battery kept counting,
    // so wall time comes from the host at each launch (seeded above).
    // "SE/30" would put a directory separator in the filename.
    const std::string pramTag = se30 ? "se30"
                              : model == MacIIMemory::Model::IIx  ? "iix"
                              : model == MacIIMemory::Model::IIcx ? "iicx"
                                                                  : "macii";
    static std::string pramPath =
        (hddPath.empty() ? pramTag : hddPath + "." + pramTag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    static const std::string maciiTitle =
        std::string("POM68K — Macintosh ") + name;
    GLFWwindow* window = glfwCreateWindow(1320, 1040, maciiTitle.c_str(),
                                          nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static MacIiMachine machine{mem, cpu, audioHost};
    gSetCpuEngine = [](int e) { machine.push({MacIiMachine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.state.kind = model == MacIIMemory::Model::IIx  ? pom68k::SnapMachine::IIx
                       : model == MacIIMemory::Model::IIcx ? pom68k::SnapMachine::IIcx
                       : model == MacIIMemory::Model::SE30 ? pom68k::SnapMachine::SE30
                                                           : pom68k::SnapMachine::MacII;
    machine.state.path = (hddPath.empty() ? std::string(name)
                                          : hddPath + "." + name) + ".pomss";
    machine.publish(true);

    // Optional startup floppy (POM68K_FLOPPY); the Disques window hot-swaps.
    static std::string floppyPath =
        std::getenv("POM68K_FLOPPY") ? std::getenv("POM68K_FLOPPY") : "";
    static bool floppyOk = !floppyPath.empty() && mem.insertDisk(floppyPath);
    if (floppyOk) std::printf("Floppy: %s\n", floppyPath.c_str());
    machine.setFloppyInserted(floppyOk);

    struct Ctx {
        GLFWwindow* window; MacIiMachine& m; GLuint tex;
        ScreenInput input;
        std::string romName, hddPath, floppyPath;
        std::vector<std::string> extraDisks;
        bool& floppyOk;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, floppyPath,
                   extraDisks, floppyOk};

    auto frame = [](void* arg) {
        Ctx& c = *static_cast<Ctx*>(arg);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        machineMenu(MachineKind::MacII, c.window, [&c] {
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({MacIiMachine::Cmd::HardReset}); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


                pom68k::dockLayoutScreenWindow("Macintosh II");
        ImGui::Begin("Macintosh II");
        std::vector<uint32_t> fb;
        int fw = 0, fh = 0;
        if (c.m.latchFrame(fb, fw, fh) && fw > 0 && fh > 0) {
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, fb.data());
            c.input.frame(c.window, c.tex, ImVec2(float(fw * 2), float(fh * 2)),
                    [&](int dx, int dy) { c.m.push({MacIiMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({MacIiMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Escape,0x6B},
            };
            for (const auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({MacIiMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (keyUp(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), false);
                    c.m.push({MacIiMachine::Cmd::Key, e.m0110 >> 1, 0});
                }
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
        saveStateUi(c.m.state);
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
    machine.stop();                     // join before touching machine state
    mem.savePram(pramPath);
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

// ── Mac IIfx machine thread (platform #12) ──────────────────────────────
// The MacIiMachine contract, on the OSS + dual-IOP board: input crosses as
// queued commands, the framebuffer as a decoded copy, status as relaxed
// atomics. What is IIfx-specific is what the CPU window shows — the two
// Apple PIC IOPs are processors in their own right, and "are they still
// executing?" is the first question any IIfx bug asks.
struct IIfxMachine {
    IIfxMemory& mem; IIfxCpu& cpu; MacAudioHost& audioHost;
    IIfxMachine(IIfxMemory& m, IIfxCpu& c, MacAudioHost& a)
        : mem(m), cpu(c), audioHost(a) {}
    ~IIfxMachine() { stop(); }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, InsertFloppy,
                          EjectFloppy } t; int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    SaveStateSlot state;

    // Floppy hot-swap (GUI → machine thread; the DafbMachine contract).
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
    std::string floppyPending_;              // guarded by cmdMu_
    std::atomic<bool> floppyFlag_{false};

    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay;
                    long long sccPicCycles, swimPicCycles; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 stOverlay_.load(std::memory_order_relaxed),
                 stSccPic_.load(std::memory_order_relaxed),
                 stSwimPic_.load(std::memory_order_relaxed) };
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
        if (tv) tv->raster(fb_);          // raster surface, see runOne()
        else fb_.assign(size_t(hres) * size_t(vres), 0xFFFFFFFFu);
        for (uint32_t& px : fb_) px |= 0xFF000000u;
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_ = fb_; fbW_ = hres; fbH_ = vres;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        stOverlay_.store(mem.overlay(), std::memory_order_relaxed);
        stSccPic_.store(mem.sccPic().cpu().cycleCount(), std::memory_order_relaxed);
        stSwimPic_.store(mem.swimPic().cpu().cycleCount(), std::memory_order_relaxed);
    }

private:
    // 60.15 Hz on a 40 MHz clock (the IIfx's OSS tick, not a round 60).
    static constexpr int64_t kFrame = IIfxMemory::kCpuHz * 100 / 6015;
    static constexpr size_t kTarget = 2225;

    void runOne() {
        // Raster catch-up on the Toby card's own CRTC frame clock
        // (LLE_VS_HLE §1.1) — the IIfx has no built-in video.
        runQuantumWithWire(mem, cpu, kFrame, [this] {
            if (TobyVideo* tv = mem.toby()) tv->raster(fb_);
        });
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
        std::string pending;
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_);
          pending.swap(floppyPending_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.b != 0, c.a); break;
            case Cmd::Key:         mem.keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            case Cmd::InsertFloppy:
                if (!pending.empty() && mem.insertDisk(pending))
                    floppyFlag_.store(true, std::memory_order_relaxed);
                break;
            case Cmd::EjectFloppy:
                mem.ejectDisk();
                floppyFlag_.store(false, std::memory_order_relaxed);
                break;
        }
        cmdsApply_.clear();
        state.apply(mem, cpu);         // save/load between two quanta
    }

    std::thread th_;
    std::mutex cmdMu_;
    std::vector<Cmd> cmds_, cmdsApply_;
    std::mutex fbMu_;
    std::vector<uint32_t> fbShared_;
    int fbW_ = 0, fbH_ = 0;
    std::atomic<uint32_t> stPc_{0};
    std::atomic<long long> stClock_{0};
    std::atomic<bool> stOverlay_{true};
    std::atomic<long long> stSccPic_{0}, stSwimPic_{0};
    int activeHold_ = 0;
    int starve_ = 0;
    int framesRun_ = 0;
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;
    std::vector<float> samp_;
};

// ── Macintosh IIfx: OSS + two Apple PIC IOPs, 68030 @ 40 MHz ────────────
// Selected by the 512 KB ROM whose header checksum is $4147DD77. No
// built-in video: the machine boots on the slot-9 Toby NuBus card, and
// ADB is bit-banged by the SWIM IOP's own 65C02 firmware
// (docs/IOP_BRINGUP.md).
static int runIIfx(std::vector<uint8_t> rom, const std::string& romName,
                   int argc, char** argv) {
    std::printf("Machine: Macintosh IIfx (68030 @ 40 MHz, OSS + 2 IOP 65C02, "
                "Toby NuBus%s)\n",
                getenv("POM68K_NOFPU") ? "" : ", soft 68882");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static IIfxMemory mem(0x800000);
    static IIfxCpu cpu(mem, getenv("POM68K_NOFPU") == nullptr);
    static MacAudioHost audioHost;
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "FAIL: bad Mac IIfx ROM\n");
        return 1;
    }
    mem.installTobyVideo();
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.rtc().setSeconds(hostMacSeconds());      // GUI: real local date/time
    wireLocalTalk(mem);

    // The IIfx ROM is 32-bit dirty: System 7.6 is the practical ceiling
    // (8.x needs a 32-bit-clean ROM), so prefer a 7.x image.
    std::string hddPath = (argc > 2) ? argv[2] : findPath("hdv/MacOS-7.6-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    // Battery-backed PRAM (Rtc.h). The IIfx keeps a discrete RTC despite
    // its two IOPs — the PICs carry firmware, not the PRAM. Host wall time
    // was seeded above; only the XPRAM is in the file.
    static std::string pramPath =
        (hddPath.empty() ? std::string("iifx") : hddPath + ".iifx") + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());

    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    GLFWwindow* window = glfwCreateWindow(1320, 1040, "POM68K — Macintosh IIfx",
                                          nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static IIfxMachine machine{mem, cpu, audioHost};
    machine.state.kind = pom68k::SnapMachine::IIfx;
    machine.state.path = (hddPath.empty() ? std::string("IIfx")
                                          : hddPath + ".IIfx") + ".pomss";
    machine.publish(true);

    // Optional startup floppy (POM68K_FLOPPY); the Disques window hot-swaps.
    static std::string floppyPath =
        std::getenv("POM68K_FLOPPY") ? std::getenv("POM68K_FLOPPY") : "";
    static bool floppyOk = !floppyPath.empty() && mem.insertDisk(floppyPath);
    if (floppyOk) std::printf("Floppy: %s\n", floppyPath.c_str());
    machine.setFloppyInserted(floppyOk);

    struct Ctx {
        GLFWwindow* window; IIfxMachine& m; GLuint tex;
        ScreenInput input;
        std::string romName, hddPath, floppyPath;
        std::vector<std::string> extraDisks;
        bool& floppyOk;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, floppyPath,
                   extraDisks, floppyOk};

    auto frame = [](void* arg) {
        Ctx& c = *static_cast<Ctx*>(arg);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        machineMenu(MachineKind::IIfx, c.window, [&c] {
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({IIfxMachine::Cmd::HardReset}); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


                pom68k::dockLayoutScreenWindow("Macintosh IIfx");
        ImGui::Begin("Macintosh IIfx");
        std::vector<uint32_t> fb;
        int fw = 0, fh = 0;
        if (c.m.latchFrame(fb, fw, fh) && fw > 0 && fh > 0) {
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, fb.data());
            c.input.frame(c.window, c.tex, ImVec2(float(fw * 2), float(fh * 2)),
                    [&](int dx, int dy) { c.m.push({IIfxMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({IIfxMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        }
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

        // Keyboard → ADB (the same M0110>>1 table as the Mac II / LC II).
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Escape,0x6B},
            };
            for (const auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k))
                    c.m.push({IIfxMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (keyUp(uint8_t(e.m0110), e.k))
                    c.m.push({IIfxMachine::Cmd::Key, e.m0110 >> 1, 0});
            }
        }

        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        IIfxMachine::Status st = c.m.status();
        ImGui::Text("68030 @ 40 MHz (Moira)  PC=%08X  clock=%lld",
                    st.pc, st.clock);
        ImGui::Text("overlay=%d  Toby=%dx%d",
                    st.overlay ? 1 : 0,
                    c.m.mem.toby() ? c.m.mem.toby()->hres() : 0,
                    c.m.mem.toby() ? c.m.mem.toby()->vres() : 0);
        // The two IOPs are real processors: a frozen counter here is the
        // first symptom of every IOP-side bug (SCC = serial, SWIM =
        // floppy + ADB).
        ImGui::Text("IOP 65C02  SCC=%lld cyc   SWIM=%lld cyc",
                    st.sccPicCycles, st.swimPicCycles);
        bool running = c.m.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run")) c.m.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) c.m.push({IIfxMachine::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = c.m.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Turbo", &turbo))
            c.m.turbo.store(turbo);
        saveStateUi(c.m.state);
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
    machine.stop();                     // join before touching machine state
    mem.savePram(pramPath);
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
        : mem(m), cpu(c), video(v), audioHost(a) {
        stEngine_.store(cpu.engine(), std::memory_order_relaxed);
    }
    // Any exit() while the thread runs (Xlib's default error handler exits
    // behind GLFW's back) would otherwise destroy a joinable std::thread —
    // an instant std::terminate. Joining here turns that into a clean stop.
    ~LcMachine() { stop(); }

    // Engine state + JIT gauges for the CPU menu (DafbMachine contract:
    // the menu tick follows the MACHINE; the swap lands one queue trip
    // later, on the machine thread).
    int cpuEngine() const { return stEngine_.load(std::memory_order_relaxed); }
    jit::Stats::Snapshot jitStats() const {
        std::lock_guard<std::mutex> l(jitMu_);
        return jitSnap_;
    }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, Sense,
                          CpuEngine, InsertFloppy, EjectFloppy } t;
                 int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    // Save-state requests (GUI → machine thread; see SaveStateSlot above).
    SaveStateSlot state;

    // Floppy hot-swap (GUI → machine thread; the DafbMachine contract).
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
    std::string floppyPending_;              // guarded by cmdMu_
    std::atomic<bool> floppyFlag_{false};

    // Latest decoded frame (00RRGGBB, alpha forced — see the decode note).
    // Returns false until the first publish.
    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held;
                    uint8_t config, sense; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 4) != 0,
                 stConfig_.load(std::memory_order_relaxed),
                 stSense_.load(std::memory_order_relaxed) };
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
        video.size(hres, vres);
        // `fb_` is the RASTER SURFACE: runOne() has already decoded each row
        // at the moment the beam scanned it. Catch up once more here so a
        // paused or held machine still publishes a complete frame, then copy
        // out — the surface itself must stay alpha-free, since the next
        // frame overwrites only the rows the beam repaints.
        video.raster(fb_, /*full=*/true);
        // The decoders pack 00RRGGBB — alpha 0. ImGui renders textures with
        // alpha blending on, so a 0 alpha draws fully transparent (black
        // window background); force A=$FF before the BGRA upload.
        fbPub_.assign(fb_.begin(), fb_.end());
        for (uint32_t& px : fbPub_) px |= 0xFF000000u;
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_.swap(fbPub_); fbW_ = hres; fbH_ = vres;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC() & 0x80000000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        stConfig_.store(mem.ramConfig(), std::memory_order_relaxed);
        // The sense byte is written on THIS thread (Cmd::Sense) and it is a
        // multi-field update inside V8Memory (vidSpram_/vidSpramSaved_/
        // montype_) — so it crosses to the GUI as an atomic like every other
        // machine→GUI value, never by reaching into mem from the GUI thread.
        stSense_.store(mem.monitorSense(), std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> l(jitMu_);
            jitSnap_ = cpu.jit().stats().snapshot();
        }
    }

private:
    // CPU cycles per 60 Hz slice: 640×407 dots at C15M for the V8 machines,
    // the true clock/60 for the Mac TV's C32M Tinker Bell (~2× the cycles).
    const int kFrame = mem.cpuHz() == V8Memory::kCpuHz
        ? 640 * 407 : int(mem.cpuHz() / 60);
    static constexpr size_t kTarget = 2225;    // ~100 ms of 22 257 Hz sound

    void runOne() {
        // The raster catch-up rides the wire slicing: each row is decoded
        // once, when the beam scans it (LLE_VS_HLE §1.1, gate
        // v8_raster_test). Total work per frame is unchanged — it is the
        // same rows, placed correctly instead of all at publish time.
        auto beam = [this] { video.raster(fb_); };
        if (mem.cpuHeld()) { mem.tick(kFrame); beam(); }   // Egret power-on hold
        else runQuantumWithWire(mem, cpu, kFrame, beam);
        framesRun_++;
    }
    // Drain the ASC samples produced by the last slice (22 257 Hz mono,
    // continuous — an empty FIFO repeats its stale byte) and report whether
    // they carry real sound (AC span, same gate as MacAudioHost::pushFrame).
    // The facade dispatches per model (Spice = Sonora EASC, mono L+R mix).
    bool drain() {
        samp_.clear();
        while (mem.ascAvailable() > 0)
            samp_.push_back(float(mem.ascPop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }
    void applyCmds() {
        std::string pending;
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_);
          pending.swap(floppyPending_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            // V8Memory routes to the firmware AdbLine when the Egret LLE
            // is active (POM68K_EGRET_LLE), else to the HLE's AdbBus.
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.b != 0, c.a); break;
            case Cmd::Key:         keyTrace("apply", uint8_t(c.a), c.b != 0);
                               mem.keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            case Cmd::InsertFloppy:
                if (!pending.empty() && mem.insertDisk(pending))
                    floppyFlag_.store(true, std::memory_order_relaxed);
                break;
            case Cmd::EjectFloppy:
                mem.ejectDisk();
                floppyFlag_.store(false, std::memory_order_relaxed);
                break;
            case Cmd::Sense:       mem.setMonitorSense(uint8_t(c.a)); cpu.hardReset(); break;
            // Engine swap between two runCycles() — an instruction
            // boundary (the DafbMachine precedent).
            case Cmd::CpuEngine:
                cpu.setEngine(c.a);
                stEngine_.store(c.a, std::memory_order_relaxed);
                break;
        }
        cmdsApply_.clear();
        state.apply(mem, cpu);         // save/load between two quanta
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
    std::atomic<uint8_t> stSense_{0};   // monitor sense, machine → GUI
    std::atomic<int> stEngine_{0};           // 0 = interpreter, 1 = JIT
    mutable std::mutex jitMu_;
    jit::Stats::Snapshot jitSnap_{};
    int activeHold_ = 0;           // machine frames of sound-recent state
    int starve_ = 0;               // safety against a dead DAC
    int framesRun_ = 0;            // frames emulated since the last publish
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;     // raster surface (no alpha — see publish)
    std::vector<uint32_t> fbPub_;  // alpha'd copy handed to the GUI
    std::vector<float> samp_;
};

// ── Mac LC II (O6): V8 + 68030, selected by a 512 KB ROM ────────────────
// Also drives the two V8-family siblings (Phase C, MAME maclc.cpp): the
// Macintosh LC (same board, 68020 + 2 MB soldered) and the Classic II
// (Eagle = V8 with built-in 512×342 mono video). Pre-Ui-class shape like
// the Plus loop below; the shared boilerplate folds into a Ui class with
// the backlog item.
struct V8Profile { const char* name; const char* cpu; const char* shortName;
                   MachineKind kind; };
static const V8Profile& v8Profile(V8Memory::Model model) {
    static const V8Profile kLcII{"Macintosh LC II", "68030", "lcii",
                                 MachineKind::LcII};
    static const V8Profile kLc{"Macintosh LC", "68020", "lc",
                               MachineKind::Lc};
    static const V8Profile kClas2{"Macintosh Classic II", "68030", "classic2",
                                  MachineKind::ClassicII};
    static const V8Profile kCClas{"Macintosh Color Classic", "68030", "cclassic",
                                  MachineKind::ColorClassic};
    static const V8Profile kMacTv{"Macintosh TV", "68030", "mactv",
                                  MachineKind::MacTv};
    switch (model) {
        case V8Memory::Model::Lc:           return kLc;
        case V8Memory::Model::ClassicII:    return kClas2;
        case V8Memory::Model::ColorClassic: return kCClas;
        case V8Memory::Model::MacTv:        return kMacTv;
        default:                            return kLcII;
    }
}
static int runLcII(std::vector<uint8_t> rom, const std::string& romName,
                   int argc, char** argv,
                   V8Memory::Model model = V8Memory::Model::LcII) {
    const V8Profile& prof = v8Profile(model);
    // The Mac TV's Tinker Bell runs the 68030 at C32M (31.3344 MHz), no
    // FPU; every other V8 machine is 15.6672 MHz (C32M/2).
    const int64_t cpuHz = model == V8Memory::Model::MacTv
        ? V8Memory::kCpuHzTv : V8Memory::kCpuHz;
    std::printf("Machine: %s (%s @ %.4f MHz, %s%s)\n", prof.name, prof.cpu,
                double(cpuHz) / 1e6,
                model == V8Memory::Model::ClassicII     ? "Eagle"
                : model == V8Memory::Model::ColorClassic ? "Spice"
                : model == V8Memory::Model::MacTv        ? "Tinker Bell"
                                                         : "V8",
                (getenv("POM68K_NOFPU") || model == V8Memory::Model::MacTv)
                    ? "" : ", 68882");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    // Tinker Bell caps at 8 MB (4 MB soldered + 4 MB SIMM); the others go
    // to 10 MB. The Mac TV has no FPU socket (maclc.cpp:524).
    static V8Memory mem{model == V8Memory::Model::MacTv ? 0x800000u : 0xA00000u,
                        model, cpuHz};
    // The LC II's 68882 is a PDS option; this build defaults it ON —
    // the target disks (and much LC II-era software) issue FPU ops and
    // fault with "system error 10" (Line-F) on a no-FPU machine. Set
    // POM68K_NOFPU to model a bare machine. The LC profile runs the
    // 68020 core (maclc.cpp:342).
    static Cpu030 cpu(mem, getenv("POM68K_NOFPU") == nullptr
                               && model != V8Memory::Model::MacTv,
                      model == V8Memory::Model::Lc);
    static V8Video video(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    wireLocalTalk(mem);
    // Monitor sense (resolution): the GUI defaults to 640×480 13"/14" RGB —
    // the roomiest built-in mode, and some software (Lode Runner) needs a
    // ≥640×400 screen. POM68K_MONITOR=512 forces the 512×384 12" RGB mode;
    // also switchable live from the CPU window. Only these two — the LC II
    // built-in video can't do more (512KB VRAM, V8 bandwidth); the 640×870
    // portrait needs a wider framebuffer than the V8 provides. (Tests keep
    // V8Memory's own 512×384 default; only this GUI path picks 640.)
    if (model != V8Memory::Model::ClassicII &&   // Eagle/Spice/Tinker Bell:
        model != V8Memory::Model::ColorClassic && // display built in (fixed
        model != V8Memory::Model::MacTv) {        // sense)
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

    // GISTPERSO-boot.vhd = the user's real LC II volume. Bare HFS `.dsk`
    // images get an in-memory DDM façade in ScsiDisk::open. The sibling
    // profiles look for their own image first (hdv/lc-boot.vhd /
    // hdv/classic2-boot.vhd), then share the LC II's.
    std::string hddPath = (argc > 2) ? argv[2]
        : findPath(("hdv/" + std::string(prof.shortName) + "-boot.vhd").c_str());
    if (hddPath.empty()) hddPath = findPath("hdv/GISTPERSO-boot.vhd");
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
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    // Battery-backed PRAM+clock: a cold PRAM triggers the ROM's long
    // full-RAM burn-in on every boot — persist it like a real battery.
    // The file is tagged with the machine: an untagged "<image>.pram" made
    // every profile sharing one boot volume load another machine's XPRAM
    // (loadPram validates nothing), so a Quadra's video/boot bytes landed in
    // the LC II's Egret. The later profiles already tag theirs.
    static std::string pramPath =
        (hddPath.empty() ? std::string(prof.shortName)
                         : hddPath + "." + prof.shortName) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // The battery file's clock froze while the emulator was off; a real
    // RTC keeps counting. Wall time always comes from the host (GUI only).
    mem.setRtcSeconds(hostMacSeconds());
    // First boot / stale battery file: seed the Basilisk II known-good
    // XPRAM defaults instead of an all-zero PRAM (no-op once the system
    // software's 'NuMc' signature is present)
    mem.egret().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    // Sized so the largest mode (640×480 shown at 2×) fits with the menu
    // bar and the CPU window; the smaller 512×384 leaves margin.
    const std::string winTitle = std::string("POM68K — ") + prof.name;
    GLFWwindow* window = glfwCreateWindow(1320, 1040, winTitle.c_str(), nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static LcMachine machine{mem, cpu, video, audioHost};
    gSetCpuEngine = [](int e) { machine.push({LcMachine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.state.kind = model == V8Memory::Model::Lc           ? pom68k::SnapMachine::Lc
                       : model == V8Memory::Model::ClassicII    ? pom68k::SnapMachine::ClassicII
                       : model == V8Memory::Model::ColorClassic ? pom68k::SnapMachine::ColorClassic
                       : model == V8Memory::Model::MacTv        ? pom68k::SnapMachine::MacTv
                                                                : pom68k::SnapMachine::LcII;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    machine.publish(true);              // first frame before the GUI shows

    struct Ctx {
        GLFWwindow* window; LcMachine& m; GLuint tex;
        std::vector<uint32_t> fb;                // GUI-side framebuffer copy
        std::string romName, hddPath;            // for the "Disques" relaunch
        std::string floppyPath;
        bool floppyOk = false;
        std::vector<std::string>& extraDisks;
        const V8Profile& prof; V8Memory::Model model;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, {}, false,
                   extraDisks, prof, model};
    // Optional startup floppy (POM68K_FLOPPY); the Disques window hot-swaps.
    if (const char* env = std::getenv("POM68K_FLOPPY")) {
        if (mem.insertDisk(env)) {
            ctx.floppyPath = env; ctx.floppyOk = true;
            machine.setFloppyInserted(true);
            std::printf("Floppy: %s\n", env);
        }
    }

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

        machineMenu(c.prof.kind, c.window, [&c] {
            // Disk selection lives in its own window (src/DiskBays.*), shared
            // by every platform. It used to be ten copies of an ImGui menu
            // whose MenuItems relaunched the emulator on the click — one
            // mis-aimed click cold-started the machine, and a series of them
            // walked the user through several boot volumes, leaving each one
            // dirty. A window with explicit buttons cannot do that.
            pom68k::diskBaysMenuItem();
            // One-click machine reset (= power cycle: overlay + chips + CPU;
            // the ROM rescans the SCSI bus, so hot-attached media appear).
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({LcMachine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });

        // The shared "Disques" window. Built once: the hooks capture the
        // static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({LcMachine::Cmd::HardReset}); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;      // cheap, and follows a relaunch
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow(c.prof.name);
        ImGui::Begin(c.prof.name);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({LcMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({LcMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
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
                if (keyDown(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({LcMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (keyUp(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), false);
                    c.m.push({LcMachine::Cmd::Key, e.m0110 >> 1, 0});
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 830), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        // Display-only snapshot published by the machine thread.
        LcMachine::Status st = c.m.status();
        ImGui::Text("%s @ 15.6672 MHz (Moira%s)  PC=%08X  clock=%lld",
                    c.prof.cpu,
                    c.model == V8Memory::Model::Lc ? "" : " + PMMU",
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
        if (c.model != V8Memory::Model::ClassicII &&      // built-in displays:
            c.model != V8Memory::Model::ColorClassic) {   // Eagle mono, Spice CRT
            int sense = st.sense;                // published by the machine thread
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
        }
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
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

// ── Mac LC III (Phase C): Sonora + 68030 @ 25 MHz ───────────────────────
// Same GUI ↔ machine contract as LcMachine (the QuadraMachine precedent:
// per-machine thread struct, queued commands, decoded framebuffer copy).
// Resolution comes from the ACTIVE Sonora modeline, monitor pick via the
// sense buttons (2 = 512×384, 6 = 640×480) like the LC II.
template <class Mem, class Cpu, class Video>
struct SonoraStyleMachine {
    Mem& mem; Cpu& cpu; Video& video; MacAudioHost& audioHost;
    SonoraStyleMachine(Mem& m, Cpu& c, Video& v, MacAudioHost& a)
        : mem(m), cpu(c), video(v), audioHost(a) {
        stEngine_.store(cpu.engine(), std::memory_order_relaxed);
    }
    ~SonoraStyleMachine() { stop(); }

    // Engine state + JIT gauges for the CPU menu (DafbMachine contract:
    // the menu tick follows the MACHINE; the swap lands one queue trip
    // later, on the machine thread).
    int cpuEngine() const { return stEngine_.load(std::memory_order_relaxed); }
    jit::Stats::Snapshot jitStats() const {
        std::lock_guard<std::mutex> l(jitMu_);
        return jitSnap_;
    }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, Sense,
                          CpuEngine, InsertFloppy, EjectFloppy } t;
                 int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    // Save-state requests (GUI → machine thread; see SaveStateSlot above).
    SaveStateSlot state;

    // Floppy hot-swap (GUI → machine thread; the DafbMachine contract).
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
    std::string floppyPending_;              // guarded by cmdMu_
    std::atomic<bool> floppyFlag_{false};

    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held;
                    uint8_t sense; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 4) != 0,
                 stSense_.load(std::memory_order_relaxed) };
    }

    int stepTick() {                    // LcMachine::stepTick, verbatim logic
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
        int hres, vres;
        video.size(hres, vres);
        // `fb_` is the RASTER SURFACE — runOne() already decoded each row as
        // the beam scanned it. Catch up once more so a paused or held
        // machine still publishes a complete frame, then copy out: the
        // surface itself stays alpha-free, since the next frame overwrites
        // only the rows the beam repaints.
        video.raster(fb_, /*full=*/true);
        fbPub_.assign(fb_.begin(), fb_.end());
        for (uint32_t& px : fbPub_) px |= 0xFF000000u;
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_.swap(fbPub_); fbW_ = hres; fbH_ = vres;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC() & 0x80000000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        // Machine-thread write (Cmd::Sense), so it must cross as an atomic —
        // see LcMachine::publish.
        stSense_.store(mem.monitorSense(), std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> l(jitMu_);
            jitSnap_ = cpu.jit().stats().snapshot();
        }
    }

private:
    const int64_t kFrame = mem.cpuHz() / 60;   // true machine clock (33 MHz+)
    static constexpr size_t kTarget = 2225;

    void runOne() {
        // Raster catch-up rides the wire slicing: each row is decoded once,
        // when the beam scans it (LLE_VS_HLE §1.1, VideoBeam.h). Same total
        // work per frame as the old whole-frame decode, correctly placed.
        auto beam = [this] { video.raster(fb_); };
        if (mem.cpuHeld()) { mem.tick(int(kFrame)); beam(); }  // Egret hold
        else runQuantumWithWire(mem, cpu, kFrame, beam);
        framesRun_++;
    }
    bool drain() {
        samp_.clear();
        while (mem.ascAvailable() > 0)
            samp_.push_back(float(mem.ascPop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }
    void applyCmds() {
        std::string pending;
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_);
          pending.swap(floppyPending_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.b != 0, c.a); break;
            case Cmd::Key:         mem.keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            case Cmd::InsertFloppy:
                if (!pending.empty() && mem.insertDisk(pending))
                    floppyFlag_.store(true, std::memory_order_relaxed);
                break;
            case Cmd::EjectFloppy:
                mem.ejectDisk();
                floppyFlag_.store(false, std::memory_order_relaxed);
                break;
            case Cmd::Sense:       mem.setMonitorSense(uint8_t(c.a)); cpu.hardReset(); break;
            // Engine swap between two runCycles() — an instruction
            // boundary (the DafbMachine precedent).
            case Cmd::CpuEngine:
                cpu.setEngine(c.a);
                stEngine_.store(c.a, std::memory_order_relaxed);
                break;
        }
        cmdsApply_.clear();
        state.apply(mem, cpu);         // save/load between two quanta
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
    std::atomic<uint8_t> stSense_{0};   // monitor sense, machine → GUI
    std::atomic<int> stEngine_{0};           // 0 = interpreter, 1 = JIT
    mutable std::mutex jitMu_;
    jit::Stats::Snapshot jitSnap_{};
    int activeHold_ = 0;
    int starve_ = 0;
    int framesRun_ = 0;
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;     // raster surface (no alpha — see publish)
    std::vector<uint32_t> fbPub_;  // alpha'd copy handed to the GUI
    std::vector<float> samp_;
};

using Lc3Machine = SonoraStyleMachine<SonoraMemory, SonoraCpu, SonoraVideo>;
using VaspMachine = SonoraStyleMachine<VaspMemory, VaspCpu, VaspVideo>;
using RbvMachine = SonoraStyleMachine<RbvMemory, RbvCpu, RbvVideo>;

// The four Sonora-board profiles: the LC III+ is the LC III clocked at
// 33.33 MHz with the $A55A0003 model longword (maclc3.cpp maclc3plus);
// the LC 520 / LC 550 are the all-in-one siblings on the separate EDE66CBD
// universal ROM with a CUDA MCU instead of the Egret (maclc3.cpp:379
// CUDA_V2XX 341s0060 — docs/LC520_BRINGUP.md).
enum class SonoraModel { Lc3, Lc3Plus, Lc520, Lc550, CClassic2 };
static MachineKind gSonoraKind = MachineKind::Lc3;   // for the frame lambda
static char gSonoraTitle[40] = "Macintosh LC III";

static int runLc3(std::vector<uint8_t> rom, const std::string& romName,
                  int argc, char** argv, SonoraModel model = SonoraModel::Lc3) {
    // `slug` tags this profile's battery file: the Egret LC IIIs and the Cuda
    // AIOs share one boot volume in practice, and an untagged .pram made each
    // load the other's XPRAM.
    struct Profile { const char* name; int mhz; int64_t cpuHz; uint32_t id;
                     bool cuda; uint8_t sense; const char* slug; };
    static const Profile kP[] = {
        { "LC III",  25, SonoraMemory::kCpuHz,     SonoraMemory::kIdLc3,     false, 6, "lc3" },
        { "LC III+", 33, SonoraMemory::kCpuHzPlus, SonoraMemory::kIdLc3Plus, false, 6, "lc3plus" },
        { "LC 520",  25, SonoraMemory::kCpuHz,     SonoraMemory::kIdLc520,   true,  6, "lc520" },
        { "LC 550",  33, SonoraMemory::kCpuHzPlus, SonoraMemory::kIdLc550,   true,  6, "lc550" },
        // Color Classic II / Performa 275: the LC 550 board in the CC case;
        // the built-in 512×384 Trinitron reports sense 2, which selects the
        // ROM machine-table entry with video type $4D.
        { "Color Classic II", 33, SonoraMemory::kCpuHzPlus,
          SonoraMemory::kIdLc550, true, 2, "cclassic2" },
    };
    const Profile& pr = kP[int(model)];
    gSonoraKind = pr.cuda ? MachineKind::Aio : MachineKind::Lc3;
    std::snprintf(gSonoraTitle, sizeof gSonoraTitle, "Macintosh %s", pr.name);
    std::printf("Machine: Macintosh %s (68030 @ %d MHz, Sonora%s, %s)\n",
                pr.name, pr.mhz, getenv("POM68K_NOFPU") ? "" : ", 68882",
                pr.cuda ? "Cuda" : "Egret");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static SonoraMemory mem{0x800000,            // 8 MB
        pr.cpuHz, pr.id, pr.cuda};
    // Stock LC III ships without the 68882 (maclc3.cpp:338); default it
    // ON like the LC II — the target disks issue FPU ops. POM68K_NOFPU
    // models the bare machine.
    static SonoraCpu cpu(mem, getenv("POM68K_NOFPU") == nullptr);
    static SonoraVideo video(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    wireLocalTalk(mem);
    {
        const char* m = getenv("POM68K_MONITOR");
        mem.setMonitorSense(m ? (atoi(m) < 640 ? 2 : 6) : pr.sense);
    }
    cpu.hardReset();

    std::string hddPath = (argc > 2) ? argv[2]
                                     : findPath("hdv/lc3-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/HD20SC.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");
    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    static std::string pramPath =
        (hddPath.empty() ? std::string(pr.slug) : hddPath + "." + pr.slug) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    mem.setRtcSeconds(hostMacSeconds());
    mem.egret().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    static const std::string title = std::string("POM68K — Macintosh ") + pr.name;
    GLFWwindow* window = glfwCreateWindow(1320, 1040, title.c_str(),
                                          nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static Lc3Machine machine{mem, cpu, video, audioHost};
    gSetCpuEngine = [](int e) { machine.push({Lc3Machine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.state.kind = model == SonoraModel::Lc3Plus   ? pom68k::SnapMachine::Lc3Plus
                       : model == SonoraModel::Lc520     ? pom68k::SnapMachine::Lc520
                       : model == SonoraModel::Lc550     ? pom68k::SnapMachine::Lc550
                       : model == SonoraModel::CClassic2 ? pom68k::SnapMachine::CClassic2
                                                         : pom68k::SnapMachine::Lc3;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; Lc3Machine& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath;
        std::string floppyPath;
        bool floppyOk = false;
        std::vector<std::string>& extraDisks;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, {}, false,
                   extraDisks};
    // Optional startup floppy (POM68K_FLOPPY); the Disques window hot-swaps.
    if (const char* env = std::getenv("POM68K_FLOPPY")) {
        if (mem.insertDisk(env)) {
            ctx.floppyPath = env; ctx.floppyOk = true;
            machine.setFloppyInserted(true);
            std::printf("Floppy: %s\n", env);
        }
    }

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

        machineMenu(gSonoraKind, c.window, [&c] {
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
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({Lc3Machine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({Lc3Machine::Cmd::HardReset}); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow(gSonoraTitle);
        ImGui::Begin(gSonoraTitle);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({Lc3Machine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({Lc3Machine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k))
                    c.m.push({Lc3Machine::Cmd::Key, e.m0110 >> 1, 1});
                if (keyUp(uint8_t(e.m0110), e.k))
                    c.m.push({Lc3Machine::Cmd::Key, e.m0110 >> 1, 0});
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 830), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        Lc3Machine::Status st = c.m.status();
        ImGui::Text("68030 @ 25 MHz (Moira + PMMU)  PC=%08X  clock=%lld",
                    st.pc, st.clock);
        ImGui::Text("overlay=%d  MMU=%s  held=%d",
                    st.overlay ? 1 : 0, st.mmu ? "on" : "off", st.held ? 1 : 0);
        bool running = c.m.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run")) c.m.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) c.m.push({Lc3Machine::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = c.m.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Turbo", &turbo))
            c.m.turbo.store(turbo);
        {
            int sense = st.sense;                // published by the machine thread
            ImGui::Text("Moniteur:");
            ImGui::SameLine();
            auto monoBtn = [&](const char* label, int s) {
                bool cur = sense == s;
                if (cur) ImGui::PushStyleColor(ImGuiCol_Button,
                                               ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(label) && !cur) c.m.push({Lc3Machine::Cmd::Sense, s});
                if (cur) ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            monoBtn("512x384", 2);
            monoBtn("640x480", 6);
            ImGui::TextDisabled("(redemarre le Mac)");
        }
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
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

// ── Mac IIvx / IIvi (VASP) ──────────────────────────────────────────────
// The runLc3 shell on the VASP machine (VaspMemory/VaspCpu/VaspVideo,
// Egret 341S0851 LLE): IIvx = 68030 + 68882 @ 31.3344 MHz ($A55A2015),
// IIvi = 15.6672 MHz ($A55A2016) — MAME maciivx.cpp. POM68K_IIVI picks
// the IIvi (the GUI menu sets it before the relaunch).
static int runVasp(std::vector<uint8_t> rom, const std::string& romName,
                   int argc, char** argv, bool vi = false) {
    std::printf("Machine: Macintosh %s (68030 @ %s MHz, VASP%s, Egret)\n",
                vi ? "IIvi" : "IIvx", vi ? "16" : "32",
                getenv("POM68K_NOFPU") ? "" : ", 68882");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static VaspMemory mem{0x800000,              // 8 MB
        vi ? VaspMemory::kCpuHzVi : VaspMemory::kCpuHzVx,
        vi ? VaspMemory::kIdIIvi : VaspMemory::kIdIIvx};
    static VaspCpu cpu(mem, getenv("POM68K_NOFPU") == nullptr);
    static VaspVideo video(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    wireLocalTalk(mem);
    {
        const char* m = getenv("POM68K_MONITOR");
        mem.setMonitorSense((m && atoi(m) < 640) ? 2 : 6);
    }
    cpu.hardReset();

    std::string hddPath = (argc > 2) ? argv[2]
                                     : findPath("hdv/lc3-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/HD20SC.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");
    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    static std::string pramPath =
        (hddPath.empty() ? std::string("iivx") : hddPath) + ".iivx.pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    mem.setRtcSeconds(hostMacSeconds());
    mem.egret().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    GLFWwindow* window = glfwCreateWindow(1320, 1040,
                                          vi ? "POM68K — Macintosh IIvi"
                                             : "POM68K — Macintosh IIvx",
                                          nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static VaspMachine machine{mem, cpu, video, audioHost};
    gSetCpuEngine = [](int e) { machine.push({VaspMachine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.state.kind = vi ? pom68k::SnapMachine::IIvi : pom68k::SnapMachine::IIvx;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; VaspMachine& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath;
        std::string floppyPath;
        bool floppyOk = false;
        std::vector<std::string>& extraDisks;
        bool vi;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, {}, false,
                   extraDisks, vi};
    // Optional startup floppy (POM68K_FLOPPY); the Disques window hot-swaps.
    if (const char* env = std::getenv("POM68K_FLOPPY")) {
        if (mem.insertDisk(env)) {
            ctx.floppyPath = env; ctx.floppyOk = true;
            machine.setFloppyInserted(true);
            std::printf("Floppy: %s\n", env);
        }
    }

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

        machineMenu(MachineKind::Vasp, c.window, [&c] {
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
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({VaspMachine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({VaspMachine::Cmd::HardReset}); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow(c.vi ? "Macintosh IIvi" : "Macintosh IIvx");
        ImGui::Begin(c.vi ? "Macintosh IIvi" : "Macintosh IIvx");
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({VaspMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({VaspMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k))
                    c.m.push({VaspMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (keyUp(uint8_t(e.m0110), e.k))
                    c.m.push({VaspMachine::Cmd::Key, e.m0110 >> 1, 0});
            }
        }

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(c.window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
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
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

// ── Mac IIsi (RBV) ──────────────────────────────────────────────────────
// The runLc3 shell on the RBV machine (RbvMemory/RbvCpu/RbvVideo): the
// IIsi (68030 @ 20 MHz, Egret 344S0100 LLE) or — iici=true — the IIci
// (68030 @ 25 MHz, PIC1654S ADB modem LLE + discrete 343-0042 RTC, empty
// NuBus). RAM-based video (framebuffer = start of system RAM), SWIM1,
// discrete ASC — MAME maciici.cpp + rbv.cpp. RbvCpu defaults to no i-cache
// boost: the IIsi ROM's host-paced Egret bit-bang loses the via_full pulse
// otherwise (see RbvCpu.cpp).
static int runIIsi(std::vector<uint8_t> rom, const std::string& romName,
                   int argc, char** argv, bool iici = false) {
    std::printf("Machine: Macintosh %s (68030 @ %d MHz, RBV%s, %s)\n",
                iici ? "IIci" : "IIsi", iici ? 25 : 20,
                getenv("POM68K_NOFPU") ? "" : ", 68882",
                iici ? "ADB modem + RTC" : "Egret");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static RbvMemory mem{0x800000,               // 8 MB
        iici ? RbvMemory::kCpuHzCi : RbvMemory::kCpuHz, iici};
    static RbvCpu cpu(mem, getenv("POM68K_NOFPU") == nullptr);
    static RbvVideo video(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    wireLocalTalk(mem);
    {
        const char* m = getenv("POM68K_MONITOR");
        mem.setMonitorSense((m && atoi(m) < 640) ? 2 : 6);
    }
    cpu.hardReset();

    std::string hddPath = (argc > 2) ? argv[2]
                                     : findPath(iici ? "hdv/iici-boot.vhd"
                                                     : "hdv/iisi-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/lc3-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/GISTPERSO-boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    if (hddPath.empty()) hddPath = findPath("hdv/HD20SC.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD 0: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .vhd in hdv/.\n");
    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    static std::string pramPath = (hddPath.empty() ? std::string(iici ? "iici"
                                                                       : "iisi")
                                   : hddPath) + (iici ? ".iici.pram"
                                                      : ".iisi.pram");
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // Real local time from the host: the IIci's discrete RTC, the IIsi's
    // Egret. factoryDefaults seeds a fresh XPRAM (Egret only).
    if (iici) mem.rtc().setSeconds(hostMacSeconds());
    else { mem.setRtcSeconds(hostMacSeconds()); mem.egret().factoryDefaults(); }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    GLFWwindow* window = glfwCreateWindow(1320, 1040,
                                          iici ? "POM68K — Macintosh IIci"
                                               : "POM68K — Macintosh IIsi",
                                          nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static RbvMachine machine{mem, cpu, video, audioHost};
    gSetCpuEngine = [](int e) { machine.push({RbvMachine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.state.kind = iici ? pom68k::SnapMachine::IIci : pom68k::SnapMachine::IIsi;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; RbvMachine& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath;
        std::string floppyPath;
        bool floppyOk = false;
        std::vector<std::string>& extraDisks;
        bool iici;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, {}, false,
                   extraDisks, iici};
    // Optional startup floppy (POM68K_FLOPPY); the Disques window hot-swaps.
    if (const char* env = std::getenv("POM68K_FLOPPY")) {
        if (mem.insertDisk(env)) {
            ctx.floppyPath = env; ctx.floppyOk = true;
            machine.setFloppyInserted(true);
            std::printf("Floppy: %s\n", env);
        }
    }

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

        machineMenu(c.iici ? MachineKind::IIci : MachineKind::IIsi, c.window, [&c] {
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
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({RbvMachine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({RbvMachine::Cmd::HardReset}); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow(c.iici ? "Macintosh IIci" : "Macintosh IIsi");
        ImGui::Begin(c.iici ? "Macintosh IIci" : "Macintosh IIsi");
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({RbvMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({RbvMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k))
                    c.m.push({RbvMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (keyUp(uint8_t(e.m0110), e.k))
                    c.m.push({RbvMachine::Cmd::Key, e.m0110 >> 1, 0});
            }
        }

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(c.window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
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
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

// ── Quadra 605 / LC 475 machine thread ──────────────────────────────────
// Same GUI ↔ machine contract as LcMachine (commands queued, framebuffer +
// status copied out), but the Q605 has no ASC wired in POM68K yet, so the
// pacing is the plain time-budgeted turbo — no audio-clocked path. The
// framebuffer is decoded straight from VRAM using the live screen geometry
// read from the main GDevice's PixMap (same derivation as q605_trace); the
// Mac OS 8.1 Finder comes up 1bpp 640×480, colour modes decode via the
// Antelope CLUT.
// The DAFB-video machine thread — shared by the Quadra 605 / LC 475
// (Q605Memory + Cpu040) and the Centris 610/650 (CentrisMemory + CentrisCpu).
// Both expose the same surface, so one template drives both.
template <class Mem, class Cpu>
struct DafbMachine {
    Mem& mem; Cpu& cpu; MacAudioHost& audioHost;
    DafbMachine(Mem& m, Cpu& c, MacAudioHost& a)
        : mem(m), cpu(c), audioHost(a) {
        // POM68K_CPU_ENGINE may have started us on the JIT; mirror whatever
        // the CPU actually built itself with so the menu tick is honest.
        stEngine_.store(cpu.engine(), std::memory_order_relaxed);
    }
    ~DafbMachine() { stop(); }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, InsertFloppy,
                          EjectFloppy, InsertBay, EjectBay, CpuEngine } t;
                 int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    // Save-state requests (GUI → machine thread; see SaveStateSlot above).
    SaveStateSlot state;
    void requestInsertFloppy(std::string path) {
        std::lock_guard<std::mutex> l(cmdMu_);
        floppyPending_ = std::move(path);
        cmds_.push_back({Cmd::InsertFloppy});
    }
    void requestEjectFloppy() {
        std::lock_guard<std::mutex> l(cmdMu_);
        cmds_.push_back({Cmd::EjectFloppy});
    }
    // CD-bay media in/out (same queue discipline as the floppy: the path
    // travels under cmdMu_, the machine thread applies between quanta).
    void requestInsertBay(int id, std::string path) {
        std::lock_guard<std::mutex> l(cmdMu_);
        bayPending_ = std::move(path);
        cmds_.push_back({Cmd::InsertBay, id});
    }
    void requestEjectBay(int id) {
        std::lock_guard<std::mutex> l(cmdMu_);
        cmds_.push_back({Cmd::EjectBay, id});
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

    // Which engine the machine thread is actually running (the menu's tick
    // must follow the machine, not the click — the swap happens one queue
    // round-trip later).
    int cpuEngine() const { return stEngine_.load(std::memory_order_relaxed); }
    // Lock-free copy of the JIT gauges. Published at the same ~16 ms cadence
    // as the framebuffer, which is exactly what a statistics window wants.
    jit::Stats::Snapshot jitStats() const {
        std::lock_guard<std::mutex> l(jitMu_);
        return jitSnap_;
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

    // ── Freeze probe (POM68K_FREEZE_PROBE=1) ──
    // A guest that looks frozen while the CPU keeps executing is either
    // spinning in a loop or stuck in an interrupt handler that never
    // returns. From outside the two are identical, and telling them apart
    // IS the diagnosis. Sample PC + SR at the publish rate and print the
    // distribution every ~2 s: a live guest spreads over hundreds of
    // addresses, a wedged one collapses onto one or two. SR answers the
    // rest — supervisor bit ($2000) and the interrupt mask (bits 8-10):
    // mask 7 in supervisor on a tight PC range means an interrupt handler
    // that never rearmed, which no guest-side UI action can recover from.
    void freezeProbe(uint32_t pc, uint16_t sr) {
        static const bool on = std::getenv("POM68K_FREEZE_PROBE") != nullptr;
        if (!on) return;
        probeHist_[pc]++;
        probeSr_ = sr;
        if (++probeSamples_ < 125) return;          // ~2 s at 16 ms
        std::vector<std::pair<uint32_t, int>> top(probeHist_.begin(),
                                                  probeHist_.end());
        std::sort(top.begin(), top.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::fprintf(stderr, "[freeze] %d samples, %zu distinct PC  SR=$%04X "
                     "(%s, IPL mask %u)\n", probeSamples_, probeHist_.size(),
                     probeSr_, (probeSr_ & 0x2000) ? "supervisor" : "user",
                     unsigned((probeSr_ >> 8) & 7));
        for (size_t i = 0; i < top.size() && i < 4; i++)
            std::fprintf(stderr, "[freeze]   PC=$%08X  %d× (%d%%)\n",
                         top[i].first, top[i].second,
                         top[i].second * 100 / probeSamples_);
        // Collapsed onto a handful of addresses = a real spin, not a busy
        // stretch. Dump the loop body and the register file: what the loop
        // polls (which address, which bit) is the whole answer, and a spin
        // waiting on a flag that never sets names the subsystem that owes
        // it. Disassembling live RAM is the only way here — the code is a
        // driver loaded into the system heap, absent from the ROM.
        // Dump once per *loop*, not once per run: boot legitimately spins
        // (ROM device polls) long before the failure under study, and a
        // single global one-shot is always spent on the wrong one. Two
        // dominant PCs within 64 bytes are the same loop; anything further
        // is a new one and earns its own dump, capped at 8.
        bool fresh = true;
        // Signed compare: seen - 64 wraps for a spin PC below $40, which made
        // the neighbourhood test unable to suppress a redump there.
        for (uint32_t seen : probeDumped_) {
            const int64_t d = int64_t(top[0].first) - int64_t(seen);
            if (d > -64 && d < 64) fresh = false;
        }
        if (probeHist_.size() <= 8 && fresh && probeDumped_.size() < 8) {
            probeDumped_.push_back(top[0].first);
            // Window = top[0] plus only the dominant PCs that belong to the
            // SAME loop (within 64 bytes). Spanning min..max of the whole
            // top-4 is what turns one stray far-away sample into millions of
            // disassembly lines.
            uint32_t lo = top[0].first, hi = top[0].first;
            for (size_t i = 1; i < top.size() && i < 4; i++) {
                if (top[i].first + 64 < top[0].first ||
                    top[i].first > top[0].first + 64) continue;
                lo = std::min(lo, top[i].first);
                hi = std::max(hi, top[i].first);
            }
            // Clamp, don't wrap: a spin PC below $18 sent lo to ~$FFFFFFE8,
            // so the loop below never ran and the dump was silently empty —
            // and the PC was recorded as dumped, so it was never retried.
            lo = (lo > 24 ? lo - 24 : 0) & ~1u;
            std::fprintf(stderr, "[freeze] spin loop at $%08X — disassembly:\n",
                         top[0].first);
            char line[256];
            for (uint32_t a = lo; a < hi + 24;) {
                int n = cpu.disassemble(line, a);
                std::fprintf(stderr, "[freeze]   %c $%08X  %s\n",
                             a == top[0].first ? '>' : ' ', a, line);
                a += uint32_t(n > 0 ? n : 2);
            }
            for (int i = 0; i < 8; i++)
                std::fprintf(stderr, "[freeze]   D%d=$%08X  A%d=$%08X\n", i,
                             cpu.getD(i), i, cpu.getA(i));
            std::fprintf(stderr, "[freeze]   SP=$%08X  ISP=$%08X\n",
                         cpu.getSP(), cpu.getISP());
        }
        probeHist_.clear();
        probeSamples_ = 0;
    }
    std::vector<uint32_t> probeDumped_;
    std::map<uint32_t, int> probeHist_;
    int probeSamples_ = 0;
    uint16_t probeSr_ = 0;

    void publish(bool force = false) {
        auto now = std::chrono::steady_clock::now();
        if (!force && framesRun_ == 0 &&
            now - lastPub_ < std::chrono::milliseconds(16)) return;
        lastPub_ = now; framesRun_ = 0;
        // `fb_` is the RASTER SURFACE — runOne() decoded each row as the
        // beam scanned it. Catch up once more so a paused or held machine
        // still publishes a complete frame.
        newFrameGeom();
        rasterBeam(true);
        const int w = geom_.w, h = geom_.h, depth = geom_.depth;
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
        freezeProbe(cpu.getPC(), cpu.getSR());
        stW_.store(w, std::memory_order_relaxed);
        stH_.store(h, std::memory_order_relaxed);
        stDepth_.store(depth, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> l(jitMu_);
            jitSnap_ = cpu.jit().stats().snapshot();
        }
    }

    // Decode the Q605 framebuffer (VRAM at $F9000000) into 00RRGGBB. Screen
    // base and bounds are read live from the main GDevice → PixMap. Pixel
    // depth and stride come from the DAFB hardware registers; the PixMap is
    // only a fallback while the video driver is publishing a new mode.
    // The geometry a frame is scanned with. Resolved ONCE per frame (it
    // costs a walk of the guest's GDevice → PixMap through peek8, far more
    // than the pixels themselves) and then held for every row of that
    // frame — which is also more correct than re-reading it per row: on
    // real hardware the CRTC latches its parameters for the frame.
    struct Geom {
        int w = 0, h = 0, depth = 0;
        uint32_t off = 0, stride = 0;
        bool operator!=(const Geom& o) const {
            return w != o.w || h != o.h || depth != o.depth ||
                   off != o.off || stride != o.stride;
        }
    };

    // (There is no whole-frame `decode()` here on purpose. The other eight
    // decoders keep one because tests and screenshot paths call it; this
    // one lived only inside publish(), which now goes through the raster
    // surface, so a `decode()` would be dead code. A still is
    // `newFrameGeom(); rasterBeam(true);`.)

    Geom resolveGeom() {
        int w = 0, h = 0, depth = 0;
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
            // PixMap.pixelSize is at +$20; +$1C is the low half of the vRes
            // Fixed (0 at 72 dpi), which made this fallback dead code.
            pmDepth = (pk32(pmap+0x20)>>16)&0xFFFF;
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
        uint32_t off = src & (Mem::kVramSize - 1);
        w = (pmR > pmL && pmR - pmL <= 1600) ? int(pmR - pmL) : 640;
        h = (pmB > pmT && pmB - pmT <= 1200) ? int(pmB - pmT) : 480;
        // DAFB can select 16 and 24 bpp and Valkyrie 16; dropping them to 1
        // painted 640 columns out of the first 80 bytes of a 1280-byte row.
        auto okDepth = [](uint32_t d) {
            return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 24;
        };
        uint32_t hwDepth = mem.dafbDepth();
        depth = okDepth(hwDepth) ? int(hwDepth)
                                 : (okDepth(pmDepth) ? int(pmDepth) : 1);
        uint32_t minStride = uint32_t((w * depth + 7) / 8);
        uint32_t hwStride = mem.dafbStride();
        uint32_t stride = (hwStride >= minStride && hwStride <= Mem::kVramSize)
                        ? hwStride : (pmRow >= minStride ? pmRow : minStride);
        // Guard a bogus base (before the driver publishes one): the visible
        // screen must fit within VRAM, else fall back to offset 0.
        if (uint64_t(off) + uint64_t(h) * stride > Mem::kVramSize) off = 0;

        return Geom{w, h, depth, off, stride};
    }

    // Render visible rows [y0, y1) of `g` into an existing g.w×g.h surface.
    void decodeRows(std::vector<uint32_t>& out, const Geom& g, int y0, int y1) {
        const int w = g.w, depth = g.depth;
        const uint32_t off = g.off, stride = g.stride;
        if (out.size() < size_t(g.w) * g.h) return;
        y0 = y0 < 0 ? 0 : y0;
        y1 = y1 > g.h ? g.h : y1;
        if (y0 >= y1) return;
        const uint8_t* vr = mem.vram();
        const uint8_t (*cl)[3] = mem.clut();
        auto vb = [&](uint32_t o) -> uint8_t {
            return o < Mem::kVramSize ? vr[o] : 0;
        };
        for (int y = y0; y < y1; y++) {
            uint32_t rowOff = off + uint32_t(y) * stride;
            for (int x = 0; x < w; x++) {
                uint32_t rgb;
                switch (depth) {
                    case 1: { int bit = (vb(rowOff + (x >> 3)) >> (7 - (x & 7))) & 1;
                              const uint8_t* c = cl[bit];   // CLUT, not hardcoded B/W
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    case 2: { int v = (vb(rowOff + (x >> 2)) >> (6 - 2*(x & 3))) & 3;
                              const uint8_t* c = cl[v];
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    case 4: { uint8_t bt = vb(rowOff + (x >> 1));
                              int v = (x & 1) ? (bt & 0xF) : (bt >> 4);
                              const uint8_t* c = cl[v];
                              rgb = uint32_t(c[0])<<16 | uint32_t(c[1])<<8 | c[2]; break; }
                    case 16: { uint16_t p = uint16_t(vb(rowOff + 2*x) << 8
                                                   | vb(rowOff + 2*x + 1));
                              rgb = uint32_t(((p>>10)&0x1F)<<19 | ((p>>5)&0x1F)<<11
                                           | (p&0x1F)<<3); break; }   // xRRRRRGGGGGBBBBB
                    case 24: { rgb = uint32_t(vb(rowOff + 4*x + 1))<<16
                                   | uint32_t(vb(rowOff + 4*x + 2))<<8
                                   | vb(rowOff + 4*x + 3); break; }   // xRGB
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
    // Derived, not hardcoded: this template is shared by Q605 (25 MHz),
    // Centris/Quadra 6x0-800 (20/25/33.33) and Q630 (33), so a fixed 25 MHz
    // quantum ran the 33 MHz boards ~24 % slow and the Centris 610 ~25 % fast —
    // and fed the same wrong budget to the LLE MCU seconds counter.
    const int kFrame = int(mem.cpuHz() / 60);
    static constexpr size_t kTarget = 2225;    // ~100 ms of 22 257 Hz sound
    // Advance the beam off the video cell's own frame accumulator (DAFB's
    // Swatch clock, or Valkyrie's) and decode the rows it has crossed —
    // each visible row rendered once, when it is scanned out (LLE_VS_HLE
    // §1.1, VideoBeam.h). The geometry is resolved once per frame, at the
    // wrap, because walking the guest's PixMap costs more than the pixels.
    // `full` = this is the once-per-publish call, so a machine whose CRTC
    // is not programmed yet (Valkyrie before the guest sets a mode, DAFB
    // before its first tick) still gets a picture. Without that flag the
    // fallback would run a whole-frame decode on EVERY slice — 64 of them
    // per frame throughout the POST.
    void rasterBeam(bool full = false) {
        const size_t need = size_t(geom_.w) * geom_.h;
        beam_.setGeometry(mem.frameCycles(), mem.frameActiveCycles(),
                          mem.frameTotalLines(), geom_.h);
        if (!beam_.valid() || need == 0) {
            if (full && need) decodeRows(fb_, geom_, 0, geom_.h);
            beam_.restartFrame();
            return;
        }
        beam_.setPos(mem.framePos(), mem.frameCount());
        beam_.pumpRows([&](int a, int b) { decodeRows(fb_, geom_, a, b); });
    }

    // Re-resolve the frame geometry and, if it moved, restart the frame so
    // no surface is left half-rendered under two different modes.
    void newFrameGeom() {
        const Geom g = resolveGeom();
        const size_t need = size_t(g.w) * g.h;
        if (g != geom_ || fb_.size() != need) {
            geom_ = g;
            fb_.assign(need, 0xFF000000u);
            beam_.restartFrame();
        }
    }

    void runOne() {
        newFrameGeom();
        auto beam = [this] { rasterBeam(); };
        if (mem.cpuHeld()) { mem.tick(kFrame); beam(); }
        else runQuantumWithWire(mem, cpu, kFrame, beam);
        framesRun_++;
        // POM68K_KEY_TRACE heartbeat: proves the machine thread and the
        // guest are still advancing (~1 s of emulated time per line).
        static const bool hb = std::getenv("POM68K_KEY_TRACE") != nullptr;
        if (hb) {
            static long n = 0;
            static uint32_t lastPc = 0, stable = 0;
            static bool dumped = false;
            if (++n % 60 == 0) {
                const uint32_t pc = cpu.getPC();
                std::fprintf(stderr, "[hb] frames=%ld clock=%lld pc=%08X\n",
                             n, (long long)cpu.getClock(), pc);
                // Same 64-byte window for 5 consecutive beats (~5 s) in RAM:
                // dump the spin loop once so it can be disassembled.
                if (pc < 0x40000000 && (pc >> 6) == (lastPc >> 6)) {
                    if (++stable == 5 && !dumped) {
                        dumped = true;
                        const uint32_t base = (pc & ~15u) - 16;
                        std::fprintf(stderr, "[hb] spin dump @%08X:", base);
                        for (uint32_t i = 0; i < 48; i++)
                            std::fprintf(stderr, "%s%02X", (i % 16) ? " " :
                                         "\n[hb]   ", mem.peek8(base + i));
                        std::fprintf(stderr, "\n");
                    }
                } else { stable = 0; }
                lastPc = pc;
            }
        }
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
        // Take the pending path out UNDER the lock: the GUI thread reassigns
        // floppyPending_ (under cmdMu_) while this thread was reading it by
        // const& through the milliseconds of file I/O in insertDisk() — a
        // use-after-free on the second pick from the Disques menu.
        std::string pending, bayPending;
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_);
          pending.swap(floppyPending_); bayPending.swap(bayPending_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            // Q605Memory routes to the firmware AdbLine when the Cuda LLE
            // is active (POM68K_CUDA_LLE), else to the Egret HLE's AdbBus.
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.b != 0, c.a); break;
            case Cmd::Key:         keyTrace("apply", uint8_t(c.a), c.b != 0);
                               mem.keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            case Cmd::InsertFloppy:
                if (!pending.empty() && mem.insertDisk(pending))
                    floppyFlag_.store(true, std::memory_order_relaxed);
                break;
            case Cmd::EjectFloppy:
                mem.ejectDisk();
                floppyFlag_.store(false, std::memory_order_relaxed);
                break;
            // Only Q605Memory carries the CD-bay hooks so far; the other
            // 040 memories compile this template too, hence the requires.
            case Cmd::InsertBay:
                if constexpr (requires { mem.insertBayMedia(1, bayPending); }) {
                    if (!bayPending.empty()) mem.insertBayMedia(c.a, bayPending);
                }
                break;
            case Cmd::EjectBay:
                if constexpr (requires { mem.ejectBayMedia(1); })
                    mem.ejectBayMedia(c.a);
                break;
            // Switching execution engines. applyCmds() is the first
            // statement of stepTick(), i.e. strictly between two calls to
            // runCycles() — so the swap always lands on an instruction
            // boundary and needs no lock of its own.
            case Cmd::CpuEngine:
                cpu.setEngine(c.a);
                stEngine_.store(c.a, std::memory_order_relaxed);
                break;
        }
        cmdsApply_.clear();
        state.apply(mem, cpu);         // save/load between two quanta
    }

    std::thread th_;
    std::mutex cmdMu_;
    std::vector<Cmd> cmds_, cmdsApply_;
    std::string floppyPending_;
    std::string bayPending_;                 // CD-bay path, guarded by cmdMu_
    std::atomic<bool> floppyFlag_{false};
    std::mutex fbMu_;
    std::vector<uint32_t> fbShared_;
    int fbW_ = 0, fbH_ = 0;
    std::atomic<uint32_t> stPc_{0};
    std::atomic<long long> stClock_{0};
    std::atomic<uint8_t> stFlags_{0};
    std::atomic<int> stW_{0}, stH_{0}, stDepth_{0};
    std::atomic<int> stEngine_{0};           // 0 = interpreter, 1 = JIT
    mutable std::mutex jitMu_;
    jit::Stats::Snapshot jitSnap_{};
    int framesRun_ = 0;
    int activeHold_ = 0;           // machine frames of sound-recent state
    int starve_ = 0;               // safety against a dead DAC
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;     // raster surface (alpha already forced)
    Geom geom_;                    // geometry the current frame is scanned with
    VideoBeam beam_;               // not serialized: pure cache
    std::vector<float> samp_;
};

using QuadraMachine  = DafbMachine<Q605Memory, Cpu040>;
using CentrisMachine = DafbMachine<CentrisMemory, CentrisCpu>;
using Q700Machine    = DafbMachine<Q700Memory, Q700Cpu>;
using Q630Machine    = DafbMachine<Q630Memory, Q630Cpu>;

// ── LC 475 / Quadra 605 (Q6): MEMCjr/PrimeTime + 68LC040, selected by a
// 1 MB ROM. Structure mirrors runLcII; the Q605 has no ASC yet (silent).
static int runQuadra(std::vector<uint8_t> rom, const std::string& romName,
                     int argc, char** argv) {
    // Same FF7439EE ROM, three model identities (MAME macquadra605.cpp):
    // LC 475 ($A55A2221, 68LC040 @ 25), Quadra 605 ($A55A2225, 68040+FPU @ 25)
    // and LC/Performa 575 "Optimus" ($A55A222E, 68LC040 @ 33). POM68K_Q605_ID
    // / POM68K_Q605_NOFPU select (the GUI menu sets them before relaunch);
    // default = LC 475.
    {
        const char* id = getenv("POM68K_Q605_ID");
        const bool q605 = id && strstr(id, "2225");
        const bool lc575 = id && (strstr(id, "222e") || strstr(id, "222E"));
        std::printf("Machine: %s (68%s040 @ %d MHz, MEMCjr+PrimeTime)\n",
                    q605 ? "Quadra 605" : lc575 ? "Macintosh LC 575"
                                                : "Macintosh LC 475",
                    (q605 || !getenv("POM68K_Q605_NOFPU")) ? "0" : "LC",
                    lc575 ? 33 : 25);
    }
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static Q605Memory mem;
    static Cpu040 cpu(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    cpu.hardReset();
    wireLocalTalk(mem);

    // Boot volume: argv[2], else Mac OS 8.1. Bare HFS `.dsk` images get an
    // in-memory DDM façade in ScsiDisk::open (same as LC II / Plus).
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
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    // Battery-backed PRAM+clock (Cuda XPRAM) — persist it like the LC II so a
    // cold PRAM doesn't retrigger the ROM's full-RAM burn-in every boot.
    static std::string pramPath =
        (hddPath.empty() ? std::string("quadra605") : hddPath + ".q605") + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // Same as LC II: the file's clock froze while powered off — wall time
    // comes from the host at every launch (GUI only).
    mem.setRtcSeconds(hostMacSeconds());
    mem.cuda().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    // 640×480 shown at 2× fits with the menu bar and the CPU window.
    GLFWwindow* window = glfwCreateWindow(1320, 1080, "POM68K — Quadra 605", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static QuadraMachine machine{mem, cpu, audioHost};
    {
        const char* qid = getenv("POM68K_Q605_ID");
        machine.state.kind = qid && strstr(qid, "2225") ? pom68k::SnapMachine::Q605
                           : qid && (strstr(qid, "222e") || strstr(qid, "222E"))
                                 ? pom68k::SnapMachine::Lc575
                                 : pom68k::SnapMachine::Lc475;
    }
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    // The "CPU" menu is global; only machines that HAVE a second engine
    // install its hooks. `machine` is static, so a captureless lambda can
    // reach it and the hooks stay valid for the process lifetime.
    gSetCpuEngine = [](int e) { machine.push({QuadraMachine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
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
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({QuadraMachine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({QuadraMachine::Cmd::HardReset}); };
                // CD bays: media in/out of a drive that exists on the bus
                // (attached at boot, or the reserved "cdbay"), no reboot.
                h.bayIsCd  = [](int id) { return mem.bayIsCdrom(id); };
                h.insertBay = [&c](int id, const std::string& d) {
                    if (!mem.bayIsCdrom(id)) return false;
                    c.m.requestInsertBay(id, d);
                    return true;
                };
                h.ejectBay = [&c](int id) { c.m.requestEjectBay(id); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow("Quadra 605");
        ImGui::Begin("Quadra 605");
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({QuadraMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({QuadraMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({QuadraMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (keyUp(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), false);
                    c.m.push({QuadraMachine::Cmd::Key, e.m0110 >> 1, 0});
                }
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
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

static int runCentris(std::vector<uint8_t> rom, const std::string& romName,
                     int argc, char** argv) {
    // Shared F1A6F343/F1ACAD13 ROM on the djMEMC + IOSB machine (MAME
    // macquadra800.cpp), five models by POM68K_CENTRIS_MODEL (GUI menu sets
    // it before relaunch; default = Centris 650): Centris 650 (68LC040 @
    // 25 MHz, ID $46) / Centris 610 (20 MHz, $40) / Quadra 650 (full 68040 @
    // 33 MHz, $52) / Quadra 610 (25 MHz, $44) / Quadra 800 (full 68040 @
    // 33 MHz, $12 — same board plus SONIC Ethernet and three NuBus slots,
    // neither of which the boot path binds). POM68K_CENTRIS610 = legacy
    // alias for c610.
    std::string cmodel = getenv("POM68K_CENTRIS_MODEL")
                       ? getenv("POM68K_CENTRIS_MODEL")
                       : (getenv("POM68K_CENTRIS610") ? "c610" : "c650");
    const bool c610 = cmodel == "c610", q650 = cmodel == "q650",
               q610 = cmodel == "q610", q800 = cmodel == "q800";
    // The Machine menu relaunches through execv(), which inherits the
    // environment, and CentrisCpu keys on mere presence of this variable — so
    // it must be cleared for the 68LC040 models or a Quadra->Centris switch
    // silently boots a full 68040 (mirrors the POM68K_Q605_NOFPU handling).
    if (q650 || q610 || q800) setenv("POM68K_CENTRIS_FPU", "1", 1);  // full 68040
    else                      unsetenv("POM68K_CENTRIS_FPU");
    struct CInfo { const char* name; int mhz; int64_t hz; uint8_t pins; };
    const CInfo cinfo =
          q800 ? CInfo{"Quadra 800", 33, CentrisMemory::kCpuHzQ650, CentrisMemory::kIdQuadra800}
        : q650 ? CInfo{"Quadra 650", 33, CentrisMemory::kCpuHzQ650, CentrisMemory::kIdQuadra650}
        : q610 ? CInfo{"Quadra 610", 25, CentrisMemory::kCpuHzQ610, CentrisMemory::kIdQuadra610}
        : c610 ? CInfo{"Centris 610", 20, CentrisMemory::kCpuHz610, CentrisMemory::kIdCentris610}
               : CInfo{"Centris 650", 25, CentrisMemory::kCpuHz650, CentrisMemory::kIdCentris650};
    std::printf("Machine: Macintosh %s (68%s040 @ %d MHz, djMEMC+IOSB)\n",
                cinfo.name, (q650 || q610 || q800) ? "0" : "LC", cinfo.mhz);
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static CentrisMemory mem(36u << 20, cinfo.hz, cinfo.pins);
    static CentrisCpu cpu(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    cpu.hardReset();
    wireLocalTalk(mem);

    // Boot volume: argv[2], else Mac OS 8.1. Bare HFS `.dsk` images get an
    // in-memory DDM façade in ScsiDisk::open (same as LC II / Plus).
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
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    // Battery-backed PRAM+clock (discrete RTC XPRAM) — persist it so a cold
    // PRAM doesn't retrigger the ROM's full-RAM burn-in every boot.
    static std::string pramPath =
        (hddPath.empty() ? std::string("centris") : hddPath) + ".centris.pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // Discrete RTC: the file's clock froze while powered off — wall time
    // comes from the host at every launch (GUI only).
    mem.rtc().setSeconds(hostMacSeconds());

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    // 640×480 shown at 2× fits with the menu bar and the CPU window.
    GLFWwindow* window = glfwCreateWindow(1320, 1080, "POM68K — Quadra 605", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static CentrisMachine machine{mem, cpu, audioHost};
    machine.state.kind = q800 ? pom68k::SnapMachine::Quadra800
                       : q650 ? pom68k::SnapMachine::Quadra650
                       : q610 ? pom68k::SnapMachine::Quadra610
                       : c610 ? pom68k::SnapMachine::Centris610
                              : pom68k::SnapMachine::Centris650;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    // The "CPU" menu is global; only machines that HAVE a second engine
    // install its hooks. `machine` is static, so a captureless lambda can
    // reach it and the hooks stay valid for the process lifetime.
    gSetCpuEngine = [](int e) { machine.push({CentrisMachine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.setFloppyInserted(floppyOk);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; CentrisMachine& m; GLuint tex;
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

        machineMenu(MachineKind::Centris, c.window, [&c] {
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
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({CentrisMachine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({CentrisMachine::Cmd::HardReset}); };
                // CD bays: media in/out of a drive that exists on the bus
                // (attached at boot, or the reserved "cdbay"), no reboot.
                h.bayIsCd  = [](int id) { return mem.bayIsCdrom(id); };
                h.insertBay = [&c](int id, const std::string& d) {
                    if (!mem.bayIsCdrom(id)) return false;
                    c.m.requestInsertBay(id, d);
                    return true;
                };
                h.ejectBay = [&c](int id) { c.m.requestEjectBay(id); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow("Quadra 605");
        ImGui::Begin("Quadra 605");
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({CentrisMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({CentrisMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({CentrisMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (keyUp(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), false);
                    c.m.push({CentrisMachine::Cmd::Key, e.m0110 >> 1, 0});
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        CentrisMachine::Status st = c.m.status();
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
        if (ImGui::Button("Reset")) c.m.push({CentrisMachine::Cmd::HardReset});
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
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

static int runQ700(std::vector<uint8_t> rom, const std::string& romName,
                     int argc, char** argv) {
    // Macintosh Quadra 700 ("Spike", MAME macquadra700.cpp): the first
    // Quadra — a full 68040 @ 25 MHz on discrete chips. Mac II VIA1/VIA2 +
    // 343-0042 RTC + PIC1654S ADB in front, Quadra DAFB/53C96/SWIM1/EASC
    // behind, SCSI through DAFB's own TurboSCSI cell. $420DBFF3 ROM.
    //
    // The same board carries the "Eclipse"/"Zydeco" towers (Quadra 900/950,
    // docs/IOP_BRINGUP.md § M7): the Mac IIfx front end grafted on — two
    // Apple PIC IOPs, the Egret instead of the discrete RTC, a second 53C96
    // bus. POM68K_Q700_MODEL picks the variant (the GUI menu sets it before
    // relaunch); the Q950 ROM forces its own model, because a $3DC27823 dump
    // IS a Quadra 950 whatever the environment inherited from the last run.
    const uint32_t romCk = rom.size() >= 4
        ? uint32_t(rom[0]) << 24 | uint32_t(rom[1]) << 16
          | uint32_t(rom[2]) << 8 | rom[3]
        : 0u;
    std::string qmodel = getenv("POM68K_Q700_MODEL") ? getenv("POM68K_Q700_MODEL")
                                                     : "q700";
    if (romCk == 0x3DC27823) qmodel = "q950";
    else if (qmodel == "q950") qmodel = "q700";   // Zydeco ROM absent
    const bool q900 = qmodel == "q900", q950 = qmodel == "q950";
    const auto qkind = q950 ? Q700Memory::Model::Q950
                     : q900 ? Q700Memory::Model::Q900
                            : Q700Memory::Model::Spike;
    const int64_t qhz = q950 ? Q700Memory::kCpuHzQ950 : Q700Memory::kCpuHz;
    const char* qname = q950 ? "Quadra 950" : q900 ? "Quadra 900" : "Quadra 700";
    std::printf("Machine: Macintosh %s (68040 @ %lld MHz, %s)\n", qname,
                (long long)(qhz / 1000000),
                q900 || q950 ? "discret + IOP Apple PIC" : "discret");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static Q700Memory mem(32u << 20, qhz, qkind);
    static Q700Cpu cpu(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    cpu.hardReset();
    wireLocalTalk(mem);

    // Boot volume: argv[2], else Mac OS 8.1. Bare HFS `.dsk` images get an
    // in-memory DDM façade in ScsiDisk::open (same as LC II / Plus).
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
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    // Battery-backed PRAM+clock — persist it so a cold PRAM doesn't
    // retrigger the ROM's full-RAM burn-in every boot. The suffix carries
    // the PROFILE, not the family: on the Spike this store is the discrete
    // RTC's XPRAM and on the towers it is the Egret's, so one file must
    // never serve two of them (the save-state path is derived from it).
    static std::string pramSuffix =
        std::string(".") + (q950 ? "q950" : q900 ? "q900" : "q700") + ".pram";
    static std::string pramPath =
        (hddPath.empty() ? std::string(qname) : hddPath) + pramSuffix;
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // The file's clock froze while powered off — wall time comes from the
    // host at every launch (GUI only). On the Eclipse there is no discrete
    // RTC in the loop: the Egret keeps time and runs its own second counter.
    if (mem.eclipse()) mem.egret().setSeconds(hostMacSeconds());
    else               mem.rtc().setSeconds(hostMacSeconds());

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    // 640×480 shown at 2× fits with the menu bar and the CPU window.
    static std::string winTitle = std::string("POM68K — ") + qname;
    GLFWwindow* window = glfwCreateWindow(1320, 1080, winTitle.c_str(), nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static Q700Machine machine{mem, cpu, audioHost};
    machine.state.kind = q950 ? pom68k::SnapMachine::Quadra950
                       : q900 ? pom68k::SnapMachine::Quadra900
                              : pom68k::SnapMachine::Q700;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    // The "CPU" menu is global; only machines that HAVE a second engine
    // install its hooks. `machine` is static, so a captureless lambda can
    // reach it and the hooks stay valid for the process lifetime.
    gSetCpuEngine = [](int e) { machine.push({Q700Machine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.setFloppyInserted(floppyOk);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; Q700Machine& m; GLuint tex;
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

        machineMenu(MachineKind::Q700, c.window, [&c] {
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
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({Q700Machine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({Q700Machine::Cmd::HardReset}); };
                // CD bays: media in/out of a drive that exists on the bus
                // (attached at boot, or the reserved "cdbay"), no reboot.
                h.bayIsCd  = [](int id) { return mem.bayIsCdrom(id); };
                h.insertBay = [&c](int id, const std::string& d) {
                    if (!mem.bayIsCdrom(id)) return false;
                    c.m.requestInsertBay(id, d);
                    return true;
                };
                h.ejectBay = [&c](int id) { c.m.requestEjectBay(id); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow("Quadra 605");
        ImGui::Begin("Quadra 605");
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({Q700Machine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({Q700Machine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({Q700Machine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (keyUp(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), false);
                    c.m.push({Q700Machine::Cmd::Key, e.m0110 >> 1, 0});
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        Q700Machine::Status st = c.m.status();
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
        if (ImGui::Button("Reset")) c.m.push({Q700Machine::Cmd::HardReset});
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
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

static int runQ630(std::vector<uint8_t> rom, const std::string& romName,
                     int argc, char** argv) {
    // Macintosh Quadra 630 / LC 580 ("Show and Tell", MAME macquadra630.cpp):
    // the last 68k desktop board — F108 memory controller + PrimeTime II I/O
    // + the fixed-mode Valkyrie framebuffer + Cuda 341S0060, 68040 @ 33 MHz.
    // POM68K_Q630_ID picks the identity ($A55A2252 = Quadra 630 by default,
    // $A55A225A = LC/Performa 580). $06684214 / $064DC91D ROMs.
    std::printf("Machine: Macintosh Quadra 630 (68040 @ 33 MHz, F108 + Valkyrie)\n");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static Q630Memory mem(32u << 20);
    static Q630Cpu cpu(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    cpu.hardReset();
    wireLocalTalk(mem);

    // Boot volume: argv[2], else Mac OS 8.1. Bare HFS `.dsk` images get an
    // in-memory DDM façade in ScsiDisk::open (same as LC II / Plus).
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
        // "cdbay" (the Disques window's reserved bay) = an empty CD drive
        // on the bus; a CD image = the same drive with the disc already in.
        // Both make the bay hot-swappable forever (DiskBays.h contract).
        if (std::string(argv[i]) == "cdbay") {
            if (mem.attachCdromEmpty(id)) {
                extraDisks.push_back("cdbay");
                std::printf("SCSI CD %d: <vide>\n", id);
            }
            continue;
        }
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    // Battery-backed PRAM+clock (discrete RTC XPRAM) — persist it so a cold
    // PRAM doesn't retrigger the ROM's full-RAM burn-in every boot.
    static std::string pramPath =
        (hddPath.empty() ? std::string("quadra630") : hddPath) + ".q630.pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // The Cuda holds the clock on this board (no discrete RTC): its saved
    // seconds froze while powered off, so re-seed from the host (GUI only).
    mem.setRtcSeconds(hostMacSeconds());

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    // 640×480 shown at 2× fits with the menu bar and the CPU window.
    GLFWwindow* window = glfwCreateWindow(1320, 1080, "POM68K — Quadra 630", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static Q630Machine machine{mem, cpu, audioHost};
    {
        const char* qid = getenv("POM68K_Q630_ID");
        machine.state.kind = qid && (strstr(qid, "225a") || strstr(qid, "225A"))
                                 ? pom68k::SnapMachine::Lc580
                                 : pom68k::SnapMachine::Q630;
    }
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    // The "CPU" menu is global; only machines that HAVE a second engine
    // install its hooks. `machine` is static, so a captureless lambda can
    // reach it and the hooks stay valid for the process lifetime.
    gSetCpuEngine = [](int e) { machine.push({Q630Machine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.setFloppyInserted(floppyOk);
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; Q630Machine& m; GLuint tex;
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

        machineMenu(MachineKind::Q630, c.window, [&c] {
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
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({Q630Machine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({Q630Machine::Cmd::HardReset}); };
                // CD bays: media in/out of a drive that exists on the bus
                // (attached at boot, or the reserved "cdbay"), no reboot.
                h.bayIsCd  = [](int id) { return mem.bayIsCdrom(id); };
                h.insertBay = [&c](int id, const std::string& d) {
                    if (!mem.bayIsCdrom(id)) return false;
                    c.m.requestInsertBay(id, d);
                    return true;
                };
                h.ejectBay = [&c](int id) { c.m.requestEjectBay(id); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.m.floppyInserted(); };
                h.insertFloppy = [&c](const std::string& d) {
                    c.m.requestInsertFloppy(d); c.floppyPath = d; c.floppyOk = true;
                };
                h.ejectFloppy = [&c] {
                    c.m.requestEjectFloppy(); c.floppyPath.clear(); c.floppyOk = false;
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }


        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow("Quadra 630");
        ImGui::Begin("Quadra 630");
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({Q630Machine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({Q630Machine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Keypad0,0xA4},{ImGuiKey_Keypad1,0xA6},{ImGuiKey_Keypad2,0xA8},
                {ImGuiKey_Keypad3,0xAA},{ImGuiKey_Keypad4,0xAC},{ImGuiKey_Keypad5,0xAE},
                {ImGuiKey_Keypad6,0xB0},{ImGuiKey_Keypad7,0xB2},{ImGuiKey_Keypad8,0xB6},
                {ImGuiKey_Keypad9,0xB8},
            };
            for (auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({Q630Machine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (keyUp(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), false);
                    c.m.push({Q630Machine::Cmd::Key, e.m0110 >> 1, 0});
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        Q630Machine::Status st = c.m.status();
        ImGui::Text("68040 @ 33 MHz (Moira + 040 MMU)  PC=%08X  clock=%lld",
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
        if (ImGui::Button("Reset")) c.m.push({Q630Machine::Cmd::HardReset});
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
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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

// ── PowerBook Duo machine thread ────────────────────────────────────────
// The IIfxMachine shape (no Video class — MscMemory decodes its own GSC
// panel) plus the SonoraStyleMachine engine hooks, since MscCpu carries a
// jit::Engine. Two things are specific to this platform:
//   - the PG&E boots FIRST and holds the 68030 in reset (msc.cpp:151,
//     port E bit 2). runOne() therefore has to tick the peripherals with
//     the CPU stopped, exactly like the Egret hold on the V8/Sonora
//     boards — tests/duo230_boot_etalon.cpp:52 does the same warm-up;
//   - there is no floppy drive at all (the Duo's is in the dock), so no
//     insert/eject plumbing and no drive-sound wiring — MscMemory has
//     neither API.
struct MscMachine {
    MscMemory& mem; MscCpu& cpu; MacAudioHost& audioHost;
    MscMachine(MscMemory& m, MscCpu& c, MacAudioHost& a)
        : mem(m), cpu(c), audioHost(a) {
        stEngine_.store(cpu.engine(), std::memory_order_relaxed);
    }
    ~MscMachine() { stop(); }

    // Engine state + JIT gauges for the CPU menu (DafbMachine contract:
    // the menu tick follows the MACHINE; the swap lands one queue trip
    // later, on the machine thread).
    int cpuEngine() const { return stEngine_.load(std::memory_order_relaxed); }
    jit::Stats::Snapshot jitStats() const {
        std::lock_guard<std::mutex> l(jitMu_);
        return jitSnap_;
    }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset,
                          CpuEngine } t; int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    // Save-state requests (GUI → machine thread; see SaveStateSlot above).
    SaveStateSlot state;

    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held;
                    uint8_t gscMode; };
    Status status() const {
        const uint8_t f = stFlags_.load(std::memory_order_relaxed);
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (f & 1) != 0, (f & 2) != 0, (f & 4) != 0,
                 stGsc_.load(std::memory_order_relaxed) };
    }

    int stepTick() {                    // LcMachine::stepTick, verbatim logic
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
        // Fixed-mode LCD: 640x400, one GSC layout per frame, no beam and no
        // mode change — decodeScreen() reads VRAM whole (MscMemory.h:152).
        mem.decodeScreen(fb_);
        for (uint32_t& px : fb_) px |= 0xFF000000u;
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_ = fb_;
            fbW_ = MscMemory::kScreenW; fbH_ = MscMemory::kScreenH;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC() & 0x80000000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        stGsc_.store(mem.gscReg(4), std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> l(jitMu_);
            jitSnap_ = cpu.jit().stats().snapshot();
        }
    }

private:
    const int64_t kFrame = mem.cpuHz() / 60;   // 33 MHz on the Duo 230
    static constexpr size_t kTarget = 2225;

    void runOne() {
        // PG&E hold: the PMU releases the 68030 through its port E, so
        // until it does the peripherals must still be clocked or the MCU
        // never gets there (duo230_boot_etalon.cpp:52). Same shape as the
        // Egret power-on hold on the V8/Sonora boards.
        if (mem.cpuHeld()) mem.tick(int(kFrame));
        else runQuantumWithWire(mem, cpu, kFrame);
        framesRun_++;
    }
    bool drain() {
        samp_.clear();
        while (mem.ascAvailable() > 0)
            samp_.push_back(float(mem.ascPop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }
    void applyCmds() {
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            // The trackball has ONE button and MscMemory::mouseButton takes
            // no index (PgePmu → AdbBus), so the right button is dropped
            // rather than mapped onto the left.
            case Cmd::MouseButton: if (c.a == 0) mem.mouseButton(c.b != 0); break;
            case Cmd::Key:         mem.keyEvent(uint8_t(c.a), c.b != 0); break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            // Engine swap between two runCycles() — an instruction
            // boundary (the DafbMachine precedent).
            case Cmd::CpuEngine:
                cpu.setEngine(c.a);
                stEngine_.store(c.a, std::memory_order_relaxed);
                break;
        }
        cmdsApply_.clear();
        state.apply(mem, cpu);         // save/load between two quanta
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
    std::atomic<uint8_t> stGsc_{0};          // GSC reg 4 = panel depth mode
    std::atomic<int> stEngine_{0};           // 0 = interpreter, 1 = JIT
    mutable std::mutex jitMu_;
    jit::Stats::Snapshot jitSnap_{};
    int activeHold_ = 0;
    int starve_ = 0;
    int framesRun_ = 0;
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;
    std::vector<float> samp_;
};

// ── PowerBook Duo 230: MSC + PG&E, 68030 @ 33 MHz ───────────────────────
// The 37th profile and platform #12's first GUI citizen (2026-08-06;
// docs/DUO_BRINGUP.md, gate tests/duo230_boot_etalon.cpp). Selected by the
// 1 MB ROM whose header checksum is $ECFA989B — the dump shared by the
// Duo 210 / 230 / 250; only the 230 is wired (kCpuHz230 / kIdDuo230).
// No floppy drive, no NuBus, no discrete RTC: the PG&E power manager runs
// its own 68HC05 and owns the clock, the PRAM and the whole input path.
static int runDuo(std::vector<uint8_t> rom, const std::string& romName,
                  int argc, char** argv) {
    std::printf("Machine: PowerBook Duo 230 (68030 @ 33 MHz, MSC + PG&E, "
                "GSC 640x400)\n");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    // Built exactly as the gate builds it (duo230_boot_etalon.cpp:43-56).
    static MscMemory mem(8u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    static MscCpu cpu(mem);
    static MacAudioHost audioHost;
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "FAIL: bad PowerBook Duo ROM\n");
        return 1;
    }
    // Without roms/pge/pge_boot.bin there is no PMU: the CPU runs free and
    // the ROM stalls at the power-manager handshake. Say so once, loudly,
    // instead of letting the user watch a dead panel.
    if (!mem.pgeActive())
        std::fprintf(stderr, "PG&E firmware missing (roms/pge/pge_boot.bin) — "
                             "the ROM will stall at the PMU handshake.\n");
    mem.setCpu(&cpu);
    cpu.hardReset();
    wireLocalTalk(mem);

    // Boot volume: argv[2], else the gate's 7.5.5 image (7.5.x is what
    // uploads the BORG v2 PMU firmware mid-boot — docs/DUO_BRINGUP.md).
    std::string hddPath = (argc > 2) ? argv[2] : findPath("hdv/System 7.5.5 HD.dsk");
    if (hddPath.empty()) hddPath = findPath("hdv/boot.vhd");
    static bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (write-back)\n", hddPath.c_str());
    else std::fprintf(stderr, "No SCSI image — drop a .dsk/.vhd in hdv/.\n");

    // Secondary volumes (argv[3..] → SCSI IDs 1..6). No "cdbay" reservation
    // and no live bay swap: MscMemory has attachCdrom() but neither
    // attachCdromEmpty() nor bayIsCdrom(), so the Disques window stages
    // those changes and applies them on the reboot it offers.
    static std::vector<std::string> extraDisks;
    for (int i = 3; i < argc && extraDisks.size() < 6; i++) {
        if (argv[i] == hddPath) continue;
        int id = int(extraDisks.size()) + 1;
        if (pom68k::diskBaysPathIsCd(argv[i])) {
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

    // Battery-backed PRAM. The Duo keeps it in the PG&E's own RAM+SRAM, not
    // in a discrete Rtc or an Egret/Cuda (MscMemory.cpp:138) — but the file
    // plays the same role, so it is tagged and paired with the boot volume
    // like everywhere else. The PMU also holds the clock, and its saved
    // seconds froze while powered off: re-seed from the host afterwards,
    // the ordering MscMemory.cpp:132 asks main.cpp for.
    static std::string pramPath =
        (hddPath.empty() ? std::string("duo230") : hddPath) + ".duo230.pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    mem.setRtcSeconds(hostMacSeconds());

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    // 640x400 shown at 2x, plus the menu bar and the CPU window.
    GLFWwindow* window = glfwCreateWindow(1320, 1000, "POM68K — PowerBook Duo 230",
                                          nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Drive sounds are wired to the FLOPPY/HDD hooks the other platforms
    // expose; MscMemory has no attachDriveSounds() (no floppy at all), so
    // the Duo runs silent-mechanically. The Machine menu toggle still
    // shows, greyed by gFloppySfx.isLoaded() as everywhere else.
    initDriveSfx(audioHost);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static MscMachine machine{mem, cpu, audioHost};
    machine.state.kind = pom68k::SnapMachine::Duo230;
    machine.state.path = pramPath.substr(0, pramPath.size() - 5) + ".pomss";
    // The "CPU" menu is global; only machines that HAVE a second engine
    // install its hooks. MscCpu owns a jit::Engine (MscCpu.h:24), so the
    // Duo gets the live interpreter/accelerated switch like the other 030s.
    gSetCpuEngine = [](int e) { machine.push({MscMachine::Cmd::CpuEngine, e}); };
    gGetCpuEngine = [] { return machine.cpuEngine(); };
    gJitStats     = [] { return machine.jitStats(); };
    gJitBackend   = cpu.jit().backendName();
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; MscMachine& m; GLuint tex;
        ScreenInput input;
        std::string romName, hddPath;
        std::vector<std::string>& extraDisks;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, extraDisks};

    auto frame = [](void* arg) {
        Ctx& c = *static_cast<Ctx*>(arg);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

#ifdef __EMSCRIPTEN__
        c.m.stepTick();
#endif

        machineMenu(MachineKind::Duo, c.window, [&c] {
            // Disk selection lives in its own window
            // (src/DiskBays.*) -- see the note in runLcII.
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({MscMachine::Cmd::HardReset});
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) c.m.state.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) c.m.state.request(true);
            {
                const std::string ssMsg = c.m.state.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). Built once: the hooks
        // capture the static Ctx, which outlives every frame. No floppy hooks
        // and no live bay hooks — see the argv loop above.
        {
            static pom68k::DiskBaysHost host = [&c] {
                pom68k::DiskBaysHost h;
                h.extras = &c.extraDisks;
                h.hardReset = [&c] { c.m.push({MscMachine::Cmd::HardReset}); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    gSwitchArgs = { c.romName, boot };
                    for (const std::string& e : extras)
                        if (e != boot) gSwitchArgs.push_back(e);
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = false;   // the Duo's floppy is in the dock
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            pom68k::diskBaysWindow(host);
        }

        pom68k::dockLayoutScreenWindow("PowerBook Duo 230");
        ImGui::Begin("PowerBook Duo 230");
        std::vector<uint32_t> fb;
        int fw = 0, fh = 0;
        if (c.m.latchFrame(fb, fw, fh) && fw > 0 && fh > 0) {
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, fb.data());
            c.input.frame(c.window, c.tex, ImVec2(float(fw * 2), float(fh * 2)),
                    [&](int dx, int dy) { c.m.push({MscMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        c.m.push({MscMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        }
        ImGuiIO& io = ImGui::GetIO();
        ImGui::End();

        // Keyboard → ADB key codes (= M0110 transition code >> 1); the same
        // table as every other ADB machine — the Duo's matrix keyboard is
        // scanned by the PG&E and presented to the guest as an ADB device.
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
                {ImGuiKey_Backspace,0x67},{ImGuiKey_LeftSuper,0x6F},{ImGuiKey_RightSuper,0x6F},
                {ImGuiKey_LeftCtrl,0x6D},{ImGuiKey_LeftShift,0x71},{ImGuiKey_RightShift,0xF7},
                {ImGuiKey_CapsLock,0x73},{ImGuiKey_LeftAlt,0x75},{ImGuiKey_RightAlt,0xF9},
                {ImGuiKey_RightCtrl,0xFB},
                {ImGuiKey_LeftArrow,0x76},{ImGuiKey_RightArrow,0x78},
                {ImGuiKey_DownArrow,0x7A},{ImGuiKey_UpArrow,0x7C},
                {ImGuiKey_Escape,0x6B},
            };
            for (const auto& e : kKeys) {
                if (keyDown(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({MscMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (keyUp(uint8_t(e.m0110), e.k)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), false);
                    c.m.push({MscMachine::Cmd::Key, e.m0110 >> 1, 0});
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(20, 870), ImGuiCond_FirstUseEver);
        ImGui::Begin("CPU", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        MscMachine::Status st = c.m.status();
        ImGui::Text("68030 @ 33 MHz (Moira + PMMU)  PC=%08X  clock=%lld",
                    st.pc, st.clock);
        // `held` is the first thing to look at on this platform: a Duo that
        // never leaves the hold has a PG&E that never released port E bit 2.
        ImGui::Text("overlay=%d  MMU=%s  PG&E hold=%d  GSC mode=$%02X",
                    st.overlay ? 1 : 0, st.mmu ? "on" : "off",
                    st.held ? 1 : 0, st.gscMode);
        bool running = c.m.running.load(std::memory_order_relaxed);
        if (ImGui::Button(running ? "Pause" : "Run")) c.m.running.store(!running);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) c.m.push({MscMachine::Cmd::HardReset});
        ImGui::SameLine();
        bool turbo = c.m.turbo.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Turbo", &turbo)) c.m.turbo.store(turbo);
        saveStateUi(c.m.state);
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
    machine.stop();                     // join before touching machine state
    mem.savePram(pramPath);
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
#ifndef POM68K_VERSION_STRING
#define POM68K_VERSION_STRING "dev"
#endif
    // Release smoke gates run this on display-less CI runners: it must
    // print and exit before glfwInit or any window is created.
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("POM68K %s — Macintosh 68k emulator "
                        "(37 profiles, Mac Plus to Quadra 950)\n",
                        POM68K_VERSION_STRING);
            return 0;
        }
    }
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
        // Default machine: Mac LC II. Prefer its canonical short name, then a
        // CRC scan so a stock LC II dump boots without needing a symlink.
        rom = findResource("roms/maclcii.rom", matched);
        if (rom.empty()) {
            std::string p = findRomBySignature("35C28F5F");
            if (!p.empty()) { rom = readFile(p); matched = p; }
        }
        // Fall back to the other short names, then the remaining CRC signatures.
        if (rom.empty()) rom = findResource("roms/macplus.rom", matched);
        if (rom.empty()) rom = findResource("roms/macii.rom", matched);
        if (rom.empty()) rom = findResource("roms/quadra605.rom", matched);
        for (const char* sig : { "9779D2C4", "FF7439EE" }) {
            if (!rom.empty()) break;
            std::string p = findRomBySignature(sig);
            if (!p.empty()) { rom = readFile(p); matched = p; }
        }
    }

    // ROM size selects the machine: 1 MB = LC 475/Quadra 605 (Q6) — or the
    // Color Classic when the header checksum says $ECD99DC0 (Spice is a
    // V8-family machine with a 1 MB ROM) —
    // 512 KB = V8 family (checksum picks LC / LC II / Classic II),
    // 256 KB = Macintosh II, 128 KB = Mac Plus.
    if (rom.size() == Q605Memory::kRomSize) {
        const uint32_t ck = uint32_t(rom[0]) << 24 | uint32_t(rom[1]) << 16
                          | uint32_t(rom[2]) << 8 | rom[3];
        if (ck == 0xECD99DC0)
            return runLcII(std::move(rom), matched, argc, argv,
                           V8Memory::Model::ColorClassic);
        // $EAF1678D = Macintosh TV (Tinker Bell — a Spice/V8 derivative on
        // its OWN 1 MB ROM, not the EDE66CBD AIO ROM; 68030 @ 31.3344 MHz,
        // no FPU, Cuda MCU, built-in 640×480).
        if (ck == 0xEAF1678D)
            return runLcII(std::move(rom), matched, argc, argv,
                           V8Memory::Model::MacTv);
        // $ECBBC41C / $EC904829 = Mac LC III (Sonora), newer/older ROM. The
        // same ROM serves the LC III+ (33 MHz, $A55A0003) — POM68K_LC3_PLUS
        // picks it (the GUI menu sets it before relaunch).
        if (ck == 0xECBBC41C || ck == 0xEC904829) {
            const char* p = getenv("POM68K_LC3_PLUS");
            return runLc3(std::move(rom), matched, argc, argv,
                          p && atoi(p) != 0 ? SonoraModel::Lc3Plus
                                            : SonoraModel::Lc3);
        }
        // $4957EB49 = Mac IIvx / IIvi / Performa 600 (VASP) — POM68K_IIVI
        // picks the 16 MHz IIvi (the GUI menu sets it before relaunch).
        if (ck == 0x4957EB49) {
            const char* p = getenv("POM68K_IIVI");
            return runVasp(std::move(rom), matched, argc, argv,
                           p && atoi(p) != 0);
        }
        // $EDE66CBD = the all-in-one family universal ROM (LC 520 / LC 550 /
        // CC II / Mac TV / Performa 275,550,560) — POM68K_AIO_ID picks the
        // model longword (the GUI menu sets it before relaunch).
        if (ck == 0xEDE66CBD) {
            const char* p = getenv("POM68K_AIO_ID");
            SonoraModel m = SonoraModel::Lc520;
            if (p && strstr(p, "CC2"))       m = SonoraModel::CClassic2;
            else if (p && strstr(p, "0101")) m = SonoraModel::Lc550;
            return runLc3(std::move(rom), matched, argc, argv, m);
        }
        // $F1A6F343 / $F1ACAD13 = Centris/Quadra 610/650/800 (djMEMC + IOSB)
        // — POM68K_CENTRIS610 picks the 610 (the GUI menu sets it).
        if (ck == 0xF1A6F343 || ck == 0xF1ACAD13)
            return runCentris(std::move(rom), matched, argc, argv);
        // $420DBFF3 = Quadra 700 / 900 (+ PB140/170) — the two share a dump,
        // so POM68K_Q700_MODEL is the only thing that tells them apart;
        // $3DC27823 = the Quadra 950's own ROM, which pins its model by
        // itself. All three are the same board (docs/IOP_BRINGUP.md § M7).
        if (ck == 0x420DBFF3 || ck == 0x3DC27823)
            return runQ700(std::move(rom), matched, argc, argv);
        // $06684214 = Quadra 630 / LC 630 / Performa 630, $064DC91D = the
        // later LC 580 ROM — both the F108 + Valkyrie board.
        if (ck == 0x06684214 || ck == 0x064DC91D)
            return runQ630(std::move(rom), matched, argc, argv);
        // $ECFA989B = PowerBook Duo 210 / 230 / 250 (MSC + PG&E) — the only
        // PowerBook ROM wired today, and the only one of the 1 MB dumps that
        // is not a desktop. The 230 is the gated model; the 210 (25 MHz) and
        // the 250 share this checksum and would need an env tag here, the
        // way POM68K_Q700_MODEL splits the $420DBFF3 dump.
        if (ck == 0xECFA989B)
            return runDuo(std::move(rom), matched, argc, argv);
        return runQuadra(std::move(rom), matched, argc, argv);
    }
    // Compact 68000 family (mac128.cpp macse/macsefd/macclasc): the Plus map
    // with a bigger ROM and ADB instead of the M0110 — same MacMemory, a
    // different Model. Checked BEFORE the Mac II / V8 size branches, which
    // otherwise claim these 256 KB / 512 KB dumps.
    if (rom.size() >= MacIIMemory::kRomSize) {
        const uint32_t ck = uint32_t(rom[0]) << 24 | uint32_t(rom[1]) << 16
                          | uint32_t(rom[2]) << 8 | rom[3];
        if (ck == 0xB2E362A8)      mem.setModel(MacMemory::Model::SE);
        else if (ck == 0xB306E171) mem.setModel(MacMemory::Model::SEFDHD);
        else if (ck == 0xA49F9914) mem.setModel(MacMemory::Model::Classic);
    }
    if (mem.model() == MacMemory::Model::Plus && rom.size() == V8Memory::kRomSize) {
        // The header checksum (first 4 bytes, big-endian) is the model ID:
        // $350EACF0 = LC, $3193670E = Classic II, $35C28F5F = LC II. Any
        // other 512 KB dump gets the LC II profile (the reference V8).
        const uint32_t ck = uint32_t(rom[0]) << 24 | uint32_t(rom[1]) << 16
                          | uint32_t(rom[2]) << 8 | rom[3];
        // $4147DD77 = Mac IIfx — platform #12 (OSS + two Apple PIC IOPs),
        // the only 512 KB ROM that is not an RBV or V8 machine.
        if (ck == 0x4147DD77)
            return runIIfx(std::move(rom), matched, argc, argv);
        // $36B7FB6C = Mac IIsi, $368CADFE = Mac IIci — both RBV machines on
        // a 512 KB ROM (maciici.cpp); the IIci swaps the Egret for the ADB
        // modem + discrete RTC.
        if (ck == 0x36B7FB6C)
            return runIIsi(std::move(rom), matched, argc, argv);
        if (ck == 0x368CADFE)
            return runIIsi(std::move(rom), matched, argc, argv, /*iici=*/true);
        V8Memory::Model model = V8Memory::Model::LcII;
        if (ck == 0x350EACF0) model = V8Memory::Model::Lc;
        else if (ck == 0x3193670E) model = V8Memory::Model::ClassicII;
        return runLcII(std::move(rom), matched, argc, argv, model);
    }
    if (mem.model() == MacMemory::Model::Plus && rom.size() == MacIIMemory::kRomSize) {
        // $97221136 = the mac2fdhd ROM shared by the Mac II FDHD and the
        // 68030 IIx / IIcx. Default it to the IIx (the distinct 030 machine);
        // POM68K_MACII_MODEL=iicx / fdhd picks the siblings. The 800K Mac II
        // ROMs ($9779D2C4 / $97851DB6) stay the plain 68020 Mac II.
        const uint32_t ck = uint32_t(rom[0]) << 24 | uint32_t(rom[1]) << 16
                          | uint32_t(rom[2]) << 8 | rom[3];
        MacIIMemory::Model model = MacIIMemory::Model::MacII;
        if (ck == 0x97221136) {
            const char* m = getenv("POM68K_MACII_MODEL");
            if (m && !std::strcmp(m, "iicx")) model = MacIIMemory::Model::IIcx;
            else if (m && !std::strcmp(m, "se30")) model = MacIIMemory::Model::SE30;
            else if (m && !std::strcmp(m, "fdhd")) model = MacIIMemory::Model::MacII;
            else model = MacIIMemory::Model::IIx;
        }
        return runMacII(std::move(rom), matched, argc, argv, model);
    }

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
    mem.rtc().setSeconds(hostMacSeconds());      // GUI: real local date/time
    wireLocalTalk(mem);

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

    // The compact 68000 siblings run this very machine (Plus map + ADB).
    const MacMemory::Model compactModel = mem.model();
    static const char* machineName =
        compactModel == MacMemory::Model::SE      ? "Macintosh SE" :
        compactModel == MacMemory::Model::SEFDHD  ? "Macintosh SE FDHD" :
        compactModel == MacMemory::Model::Classic ? "Macintosh Classic" : "Macintosh Plus";
    static const MachineKind compactKind =
        compactModel == MacMemory::Model::SE      ? MachineKind::Se :
        compactModel == MacMemory::Model::SEFDHD  ? MachineKind::SeFdhd :
        compactModel == MacMemory::Model::Classic ? MachineKind::MacClassic : MachineKind::Plus;

    // Battery-backed PRAM (Rtc.h). The compacts were the last platform
    // family without it: the Control Panel's settings — and the ROM's
    // startup-disk choice — died with the process. Tagged per model like
    // every other profile, since the four boards share one boot volume.
    // The clock is not in the file; host wall time was seeded above.
    static const char* compactTag =
        compactModel == MacMemory::Model::SE      ? "se" :
        compactModel == MacMemory::Model::SEFDHD  ? "sefdhd" :
        compactModel == MacMemory::Model::Classic ? "classic" : "plus";
    static std::string pramPath =
        (hddPath.empty() ? std::string(compactTag)
                         : hddPath + "." + compactTag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    const std::string windowTitle = std::string("POM68K — ") + machineName;

    // Save states (single-threaded machine: applied inline between frames).
    static SaveStateSlot ssSlot;
    ssSlot.kind =
        compactModel == MacMemory::Model::SE      ? pom68k::SnapMachine::SE :
        compactModel == MacMemory::Model::SEFDHD  ? pom68k::SnapMachine::SEFDHD :
        compactModel == MacMemory::Model::Classic ? pom68k::SnapMachine::Classic
                                                  : pom68k::SnapMachine::Plus;
    {
        const char* tag =
            compactModel == MacMemory::Model::SE      ? "se" :
            compactModel == MacMemory::Model::SEFDHD  ? "sefdhd" :
            compactModel == MacMemory::Model::Classic ? "classic" : "plus";
        ssSlot.path = (hddPath.empty() ? std::string(tag)
                                       : hddPath + "." + tag) + ".pomss";
    }

    // ── Window / ImGui ───────────────────────────────────────────────────
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    const char* glslVersion = configureGlfwOpenGl();
    GLFWwindow* window = glfwCreateWindow(1100, 800, windowTitle.c_str(), nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Windows move only by their title bar — so dragging inside the Mac
    // screen (Finder drag-and-drop) doesn't drag the host window.
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    pom68k::dockLayoutInit();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    pom68k::diskBaysInstallDrop(window);

    static GLuint screenTex = 0;
    glGenTextures(1, &screenTex);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    struct Ctx {
        GLFWwindow* window; MacMemory& mem; Cpu68k& cpu; MacVideo& video;
        MacAudio& audio; MacAudioHost& audioHost;
        GLuint tex; bool running; bool turbo; MacFrameClock clock;
        std::string romName, hddPath, floppyPath;
    };
    static Ctx ctx{window, mem, cpu, video, audio, audioHost, screenTex, true, !demoMode, {},
                   matched, hddPath, diskOk ? diskPath : std::string()};
    ctx.clock.resync(cpu);
    // The compacts run the machine INLINE on the GUI thread, so the engine
    // swap needs no command queue: the menu is drawn after this frame's
    // quantum has already finished, which is the instruction boundary the
    // threaded machines reach through Cmd::CpuEngine.
    gSetCpuEngine = [](int e) { ctx.cpu.setEngine(e); };
    gGetCpuEngine = [] { return ctx.cpu.engine(); };
    gJitStats     = [] { return ctx.cpu.jit().stats().snapshot(); };
    gJitBackend   = cpu.jit().backendName();

    auto frame = [](void* p) {
        Ctx& c = *static_cast<Ctx*>(p);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        if (ssSlot.apply(c.mem, c.cpu) & 2)
            c.clock.resync(c.cpu);              // restored: re-derive frameBase
        if (c.running) {
            int n = c.turbo ? 8 : 1;            // turbo: 8 machine frames per host frame
            std::vector<float> samp;
            for (int i = 0; i < n; i++) {
                // Raster catch-up 16× per display period: each row is
                // decoded once, when the beam scans it (LLE_VS_HLE §1.1).
                c.clock.runFrame(c.cpu, c.mem, [&c] { c.video.raster(c.mem); });
                pollLocalTalk(c.mem);
                // machineClock(), not getClock(): identical on the 68000
                // (no i-cache boost) but the boosted machines expire every
                // second-scale AppleTalk timer 4x early on the raw clock,
                // and the two tick sites must not disagree about which
                // clock the hub runs on (see the other site above).
                if (atalkEnabled()) g_atalk.tick(c.cpu.machineClock());
                samp.clear();
                c.audio.renderFrame(c.mem, samp);   // 370 PWM samples
                c.audioHost.pushFrame(samp, 0);     // plays only non-silent frames
            }
        }

        // `fb_` inside MacVideo is the raster surface — the frame loop
        // already decoded each row as the beam scanned it. One more
        // catch-up so a paused machine still shows a complete frame.
        const uint32_t* fb = c.video.raster(c.mem);
        glBindTexture(GL_TEXTURE_2D, c.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, c.video.width(), c.video.height(),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, fb);

        machineMenu(compactKind, c.window, [&c] {
            // Disk selection lives in its own window (src/DiskBays.*).
            pom68k::diskBaysMenuItem();
            if (ImGui::MenuItem("Redémarrer")) c.cpu.hardReset();
            ImGui::Separator();
            if (ImGui::MenuItem("Sauver l'état")) ssSlot.request(false);
            if (ImGui::MenuItem("Restaurer l'état")) ssSlot.request(true);
            {
                const std::string ssMsg = ssSlot.message();
                if (!ssMsg.empty()) ImGui::TextDisabled("%s", ssMsg.c_str());
            }
        });
        // The shared "Disques" window (src/DiskBays.*). The compact machine
        // runs single-threaded, so the hooks call straight into mem between
        // two emulated frames — no command queue needed.
        {
            static pom68k::DiskBaysHost host = [] {
                pom68k::DiskBaysHost h;
                Ctx& c = ctx;
                h.hardReset = [&c] { c.cpu.hardReset(); };
                h.relaunch  = [&c](const std::string& boot,
                                   const std::vector<std::string>& extras) {
                    (void)extras;                    // one SCSI bay on argv
                    // Compact CLI order: argv[2] = floppy, argv[3] = SCSI 0.
                    // An empty argv[2] skips the insert cleanly.
                    gSwitchArgs = { c.romName, c.floppyPath, boot };
                    glfwSetWindowShouldClose(c.window, GLFW_TRUE);
                };
                h.hasFloppyDrive = true;
                h.floppyInserted = [&c] { return c.mem.internalDrive().hasDisk(); };
                h.insertFloppy = [&c](const std::string& d) {
                    if (c.mem.insertDisk(d)) c.floppyPath = d;
                };
                h.ejectFloppy = [&c] {
                    c.mem.ejectDisk(); c.floppyPath.clear();
                };
                return h;
            }();
            host.romName  = c.romName;
            host.bootPath = c.hddPath;
            host.floppyPath = c.floppyPath;
            pom68k::diskBaysWindow(host);
        }

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
                pom68k::dockLayoutScreenWindow(machineName);
        ImGui::Begin(machineName);
        // Mouse → quadrature: hover/drag on the screen, or Delete-key capture
        static ScreenInput input;
        input.frame(c.window, c.tex,
                    ImVec2(float(c.video.width() * 2), float(c.video.height() * 2)),
                    [&](int dx, int dy) {
                        if (c.mem.isAdb()) c.mem.adbMouseMove(dx, dy);
                        else c.mem.mouse().move(dx, dy);
                    },
                    [&](int button, bool down) {
                        if (c.mem.isAdb()) c.mem.adbMouseButton(down, button);
                        else if (button == 0) c.mem.mouse().setButton(down);
                    });
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
                if (c.mem.isAdb()) {          // ADB raw code = M0110 code >> 1
                    if (keyDown(e.code, e.k)) c.mem.keyEvent(uint8_t(e.code >> 1), true);
                    if (keyUp(e.code, e.k)) c.mem.keyEvent(uint8_t(e.code >> 1), false);
                    continue;
                }
                if (keyDown(e.code, e.k)) c.mem.keyboard().enqueue(e.code);
                if (keyUp(e.code, e.k)) c.mem.keyboard().enqueue(uint8_t(e.code | 0x80));
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
    mem.savePram(pramPath);
    mem.internalDrive().flushToFile();   // persist floppy writes on exit
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
