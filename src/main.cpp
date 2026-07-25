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
#include "Cpu020.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "LtoUdp.h"
#include "AtalkHub.h"

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
template <class M> static void wireLocalTalk(M& mem, int byteCycles) {
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
template <class M, class C>
static void runQuantumWithWire(M& mem, C& cpu, int64_t frameCycles) {
    bool hub = atalkEnabled();
    if (!g_ltoudp.active() && !hub) { cpu.runCycles(frameCycles); return; }
    // The hub flushes its queued replies from the tick() at each slice end,
    // so every AFP/ATP round-trip costs at least one slice of latency.
    // Finer slicing (64 vs 16) cuts that to ~260 µs — worth it for the
    // in-process server's back-to-back transactions during a file copy.
    const int kSlices = hub ? 64 : 16;
    for (int i = 0; i < kSlices; i++) {
        cpu.runCycles(frameCycles / kSlices);
        pollLocalTalk(mem);
        if (hub) g_atalk.tick(cpu.getClock());
    }
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
    return uint32_t(uint64_t(now) + uint64_t(int64_t(lt.tm_gmtoff)) + 2082844800ULL);
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
enum class MachineKind { Plus, MacII, Lc, LcII, ClassicII, ColorClassic, MacTv, IIsi, IIci, Lc3, Aio, Vasp, Centris, Q700, Quadra };
static std::vector<std::string> gSwitchArgs;   // argv[1..] for the relaunch

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
    if (s.net.atpDupReqs)
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1),
                           "Retransmissions client : %ld (fil trop rapide ? "
                           "baisser POM68K_ATALK_WIRE_BOOST)", s.net.atpDupReqs);
    else
        ImGui::TextDisabled("Retransmissions client : 0 (fil sans perte)");

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
        const char* kV8 = "V8 / Eagle / Spice / Tinker Bell";
        const char* kRbv = "RBV (video en RAM)";
        const char* kSonora = "Sonora";
        const char* kVasp = "VASP (Sonora + peripheriques V8)";
        const char* kMemc = "MEMCjr + PrimeTime";
        const char* kDjmemc = "djMEMC + IOSB";
        const char* kSpike = "Discret 040 (Quadra 700)";
        const Profile kProfiles[] = {
            { "68000", "Macintosh Plus", MachineKind::Plus, "roms/macplus.rom", nullptr, nullptr, nullptr, true },
            { kGlue, "Macintosh II", MachineKind::MacII, "roms/macii.rom", "9779D2C4", nullptr, nullptr, true },
            { kGlue, "Macintosh IIx", MachineKind::MacII, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "iix", true },
            { kGlue, "Macintosh IIcx", MachineKind::MacII, "roms/mac2fdhd.rom", "97221136", "POM68K_MACII_MODEL", "iicx", false },
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
            { kSpike, "Macintosh Quadra 700", MachineKind::Q700, "roms/quadra700.rom", "420DBFF3", nullptr, nullptr, true },
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
    if (ImGui::BeginMenu("Réseau")) {
        ImGui::MenuItem("AppleTalk...", nullptr, &gShowAtalk,
                        atalkEnabled());
        if (!atalkEnabled())
            ImGui::TextDisabled("(POM68K_APPLETALK=0)");
        ImGui::EndMenu();
    }
    if (extraMenus) extraMenus();
    ImGui::TextDisabled("|  Delete: capture mouse");
    ImGui::EndMainMenuBar();
    appleTalkWindow();
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
        runQuantumWithWire(mem, cpu, kFrame);
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
            case Cmd::Key:         keyTrace("apply", uint8_t(c.a), c.b != 0);
                               mem.keyEvent(uint8_t(c.a), c.b != 0); break;
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
                    int argc, char** argv,
                    MacIIMemory::Model model = MacIIMemory::Model::MacII) {
    const bool is030 = model != MacIIMemory::Model::MacII;
    const char* name = model == MacIIMemory::Model::IIx  ? "IIx"
                     : model == MacIIMemory::Model::IIcx ? "IIcx" : "II";
    std::printf("Machine: Macintosh %s (%s @ 15.6672 MHz, Toby NuBus%s)\n",
                name, is030 ? "68030 + PMMU" : "68020",
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
    mem.installTobyVideo();
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.rtc().setSeconds(hostMacSeconds());      // GUI: real local date/time
    wireLocalTalk(mem, 544);                     // 230.4 kbit/s @ 15.6672 MHz

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
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

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
            // Same filename shows up in both sections — scope the ImGui IDs.
            ImGui::PushID("boot");
            for (const std::string& d : listDiskImages(c.hddPath)) {
                bool cur = (d == c.hddPath);
                std::string name = std::filesystem::path(d).filename().string();
                if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                    relaunch(d, c.extraDisks);
            }
            ImGui::PopID();
            ImGui::Separator();
            ImGui::TextDisabled("Volumes secondaires");
            ImGui::PushID("secondary");
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
            ImGui::PopID();
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
                if (ImGui::IsKeyPressed(e.k, false)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({MacIiMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (ImGui::IsKeyReleased(e.k)) {
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
        if (mem.model() == V8Memory::Model::ClassicII) { hres = 512; vres = 342; }
        else V8Video::resolution(mem.monitorSense(), hres, vres);
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
    // CPU cycles per 60 Hz slice: 640×407 dots at C15M for the V8 machines,
    // the true clock/60 for the Mac TV's C32M Tinker Bell (~2× the cycles).
    const int kFrame = mem.cpuHz() == V8Memory::kCpuHz
        ? 640 * 407 : int(mem.cpuHz() / 60);
    static constexpr size_t kTarget = 2225;    // ~100 ms of 22 257 Hz sound

    void runOne() {
        if (mem.cpuHeld()) mem.tick(kFrame);   // Egret power-on hold
        else runQuantumWithWire(mem, cpu, kFrame);
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
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            // V8Memory routes to the firmware AdbLine when the Egret LLE
            // is active (POM68K_EGRET_LLE), else to the HLE's AdbBus.
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.a != 0); break;
            case Cmd::Key:         keyTrace("apply", uint8_t(c.a), c.b != 0);
                               mem.keyEvent(uint8_t(c.a), c.b != 0); break;
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
    wireLocalTalk(mem, 544);                     // 230.4 kbit/s @ 15.6672 MHz
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
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

    // Battery-backed PRAM+clock: a cold PRAM triggers the ROM's long
    // full-RAM burn-in on every boot — persist it like a real battery.
    static std::string pramPath =
        (hddPath.empty() ? std::string(prof.shortName) : hddPath) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // The battery file's clock froze while the emulator was off; a real
    // RTC keeps counting. Wall time always comes from the host (GUI only).
    mem.egret().setSeconds(hostMacSeconds());
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
    const std::string winTitle = std::string("POM68K — ") + prof.name;
    GLFWwindow* window = glfwCreateWindow(1320, 1040, winTitle.c_str(), nullptr, nullptr);
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

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static LcMachine machine{mem, cpu, video, audioHost};
    machine.publish(true);              // first frame before the GUI shows

    struct Ctx {
        GLFWwindow* window; LcMachine& m; GLuint tex;
        std::vector<uint32_t> fb;                // GUI-side framebuffer copy
        std::string romName, hddPath;            // for the "Disques" relaunch
        std::vector<std::string>& extraDisks;
        const V8Profile& prof; V8Memory::Model model;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, extraDisks,
                   prof, model};

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
                // Same filename shows up in both sections — scope the IDs.
                ImGui::PushID("boot");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                ImGui::PushID("secondary");
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
                ImGui::PopID();
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
        ImGui::Begin(c.prof.name, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
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
                if (ImGui::IsKeyPressed(e.k, false)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({LcMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (ImGui::IsKeyReleased(e.k)) {
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
            int sense = c.m.mem.monitorSense();  // byte read; only the GUI changes it
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
        : mem(m), cpu(c), video(v), audioHost(a) {}
    ~SonoraStyleMachine() { stop(); }

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    struct Cmd { enum T { MouseMove, MouseButton, Key, HardReset, Sense } t; int a = 0, b = 0; };
    void push(Cmd c) { std::lock_guard<std::mutex> l(cmdMu_); cmds_.push_back(c); }

    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 4) != 0 };
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
        video.decode(fb_);
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
    }

private:
    const int64_t kFrame = mem.cpuHz() / 60;   // true machine clock (33 MHz+)
    static constexpr size_t kTarget = 2225;

    void runOne() {
        if (mem.cpuHeld()) mem.tick(int(kFrame));  // Egret power-on hold
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
            case Cmd::MouseButton: mem.mouseButton(c.a != 0); break;
            case Cmd::Key:         mem.keyEvent(uint8_t(c.a), c.b != 0); break;
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
    int activeHold_ = 0;
    int starve_ = 0;
    int framesRun_ = 0;
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<uint32_t> fb_;
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
    struct Profile { const char* name; int mhz; int64_t cpuHz; uint32_t id;
                     bool cuda; uint8_t sense; };
    static const Profile kP[] = {
        { "LC III",  25, SonoraMemory::kCpuHz,     SonoraMemory::kIdLc3,     false, 6 },
        { "LC III+", 33, SonoraMemory::kCpuHzPlus, SonoraMemory::kIdLc3Plus, false, 6 },
        { "LC 520",  25, SonoraMemory::kCpuHz,     SonoraMemory::kIdLc520,   true,  6 },
        { "LC 550",  33, SonoraMemory::kCpuHzPlus, SonoraMemory::kIdLc550,   true,  6 },
        // Color Classic II / Performa 275: the LC 550 board in the CC case;
        // the built-in 512×384 Trinitron reports sense 2, which selects the
        // ROM machine-table entry with video type $4D.
        { "Color Classic II", 33, SonoraMemory::kCpuHzPlus,
          SonoraMemory::kIdLc550, true, 2 },
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
    wireLocalTalk(mem, 868);                     // 230.4 kbit/s @ 25 MHz
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
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

    static std::string pramPath =
        (hddPath.empty() ? std::string("lc3") : hddPath) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    mem.egret().setSeconds(hostMacSeconds());
    mem.egret().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
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
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

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
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; Lc3Machine& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath;
        std::vector<std::string>& extraDisks;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, extraDisks};

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
            if (ImGui::BeginMenu("Disques")) {
                const auto disks = listDiskImages(c.hddPath);
                ImGui::TextDisabled("Démarrage (SCSI 0)");
                ImGui::PushID("boot");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                ImGui::PushID("secondary");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Changer un disque relance l'émulateur");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({Lc3Machine::Cmd::HardReset});
        });

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin(gSonoraTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({Lc3Machine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({Lc3Machine::Cmd::MouseButton, down ? 1 : 0}); });
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
                    c.m.push({Lc3Machine::Cmd::Key, e.m0110 >> 1, 1});
                if (ImGui::IsKeyReleased(e.k))
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
            int sense = c.m.mem.monitorSense();
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
    wireLocalTalk(mem, 868);                     // 230.4 kbit/s
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
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

    static std::string pramPath =
        (hddPath.empty() ? std::string("iivx") : hddPath) + ".iivx.pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    mem.egret().setSeconds(hostMacSeconds());
    mem.egret().factoryDefaults();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
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
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

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
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; VaspMachine& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath;
        std::vector<std::string>& extraDisks;
        bool vi;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath,
                   extraDisks, vi};

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
            if (ImGui::BeginMenu("Disques")) {
                const auto disks = listDiskImages(c.hddPath);
                ImGui::TextDisabled("Démarrage (SCSI 0)");
                ImGui::PushID("boot");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                ImGui::PushID("secondary");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Changer un disque relance l'émulateur");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({VaspMachine::Cmd::HardReset});
        });

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin(c.vi ? "Macintosh IIvi" : "Macintosh IIvx", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({VaspMachine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({VaspMachine::Cmd::MouseButton, down ? 1 : 0}); });
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
                    c.m.push({VaspMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (ImGui::IsKeyReleased(e.k))
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
    wireLocalTalk(mem, iici ? 868 : 694);        // 230.4 kbit/s @ 25/20 MHz
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
    else { mem.egret().setSeconds(hostMacSeconds()); mem.egret().factoryDefaults(); }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
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
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

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
    machine.publish(true);

    struct Ctx {
        GLFWwindow* window; RbvMachine& m; GLuint tex;
        std::vector<uint32_t> fb;
        std::string romName, hddPath;
        std::vector<std::string>& extraDisks;
        bool iici;
    };
    static Ctx ctx{window, machine, screenTex, {}, romName, hddPath, extraDisks,
                   iici};

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
            if (ImGui::BeginMenu("Disques")) {
                const auto disks = listDiskImages(c.hddPath);
                ImGui::TextDisabled("Démarrage (SCSI 0)");
                ImGui::PushID("boot");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                ImGui::PushID("secondary");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Changer un disque relance l'émulateur");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({RbvMachine::Cmd::HardReset});
        });

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin(c.iici ? "Macintosh IIci" : "Macintosh IIsi", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({RbvMachine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({RbvMachine::Cmd::MouseButton, down ? 1 : 0}); });
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
                    c.m.push({RbvMachine::Cmd::Key, e.m0110 >> 1, 1});
                if (ImGui::IsKeyReleased(e.k))
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
        : mem(m), cpu(c), audioHost(a) {}
    ~DafbMachine() { stop(); }

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
        uint32_t off = src & (Mem::kVramSize - 1);
        w = (pmR > pmL && pmR - pmL <= 1600) ? int(pmR - pmL) : 640;
        h = (pmB > pmT && pmB - pmT <= 1200) ? int(pmB - pmT) : 480;
        uint32_t hwDepth = mem.dafbDepth();
        depth = (hwDepth == 1 || hwDepth == 2 || hwDepth == 4 || hwDepth == 8)
              ? int(hwDepth)
              : ((pmDepth == 1 || pmDepth == 2 || pmDepth == 4 || pmDepth == 8)
                    ? int(pmDepth) : 1);
        uint32_t minStride = uint32_t((w * depth + 7) / 8);
        uint32_t hwStride = mem.dafbStride();
        uint32_t stride = (hwStride >= minStride && hwStride <= Mem::kVramSize)
                        ? hwStride : (pmRow >= minStride ? pmRow : minStride);
        // Guard a bogus base (before the driver publishes one): the visible
        // screen must fit within VRAM, else fall back to offset 0.
        if (uint64_t(off) + uint64_t(h) * stride > Mem::kVramSize) off = 0;

        const uint8_t* vr = mem.vram();
        const uint8_t (*cl)[3] = mem.clut();
        auto vb = [&](uint32_t o) -> uint8_t {
            return o < Mem::kVramSize ? vr[o] : 0;
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
        else runQuantumWithWire(mem, cpu, kFrame);
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
        { std::lock_guard<std::mutex> l(cmdMu_); cmdsApply_.swap(cmds_); }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            // Q605Memory routes to the firmware AdbLine when the Cuda LLE
            // is active (POM68K_CUDA_LLE), else to the Egret HLE's AdbBus.
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            case Cmd::MouseButton: mem.mouseButton(c.a != 0); break;
            case Cmd::Key:         keyTrace("apply", uint8_t(c.a), c.b != 0);
                               mem.keyEvent(uint8_t(c.a), c.b != 0); break;
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

using QuadraMachine  = DafbMachine<Q605Memory, Cpu040>;
using CentrisMachine = DafbMachine<CentrisMemory, CentrisCpu>;
using Q700Machine    = DafbMachine<Q700Memory, Q700Cpu>;

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
    wireLocalTalk(mem, 868);                     // 230.4 kbit/s @ 25 MHz

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
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

    // Battery-backed PRAM+clock (Cuda XPRAM) — persist it like the LC II so a
    // cold PRAM doesn't retrigger the ROM's full-RAM burn-in every boot.
    static std::string pramPath =
        (hddPath.empty() ? std::string("quadra605") : hddPath) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // Same as LC II: the file's clock froze while powered off — wall time
    // comes from the host at every launch (GUI only).
    mem.cuda().setSeconds(hostMacSeconds());
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

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
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
                // Same filename shows up in several sections — scope the IDs.
                ImGui::PushID("boot");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                ImGui::PushID("secondary");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Floppy (SWIM2)");
                ImGui::PushID("floppy");
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
                ImGui::PopID();
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
                if (ImGui::IsKeyPressed(e.k, false)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({QuadraMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (ImGui::IsKeyReleased(e.k)) {
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
    if (q650 || q610 || q800) setenv("POM68K_CENTRIS_FPU", "1", 1);  // full 68040
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
    wireLocalTalk(mem, 868);                     // 230.4 kbit/s @ 25 MHz

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

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static CentrisMachine machine{mem, cpu, audioHost};
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
            if (ImGui::BeginMenu("Disques")) {
                const auto disks = listDiskImages(c.hddPath);
                ImGui::TextDisabled("Démarrage (SCSI 0)");
                // Same filename shows up in several sections — scope the IDs.
                ImGui::PushID("boot");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                ImGui::PushID("secondary");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Floppy (SWIM2)");
                ImGui::PushID("floppy");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Changer un disque SCSI relance l'émulateur");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({CentrisMachine::Cmd::HardReset});
        });

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin("Quadra 605", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({CentrisMachine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({CentrisMachine::Cmd::MouseButton, down ? 1 : 0}); });
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
                if (ImGui::IsKeyPressed(e.k, false)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({CentrisMachine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (ImGui::IsKeyReleased(e.k)) {
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
    std::printf("Machine: Macintosh Quadra 700 (68040 @ 25 MHz, discrete)\n");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    static Q700Memory mem(32u << 20, Q700Memory::kCpuHz);
    static Q700Cpu cpu(mem);
    static MacAudioHost audioHost;
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    cpu.hardReset();
    wireLocalTalk(mem, 868);                     // 230.4 kbit/s @ 25 MHz

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
        if (mem.attachScsi(argv[i], true, id)) {
            extraDisks.push_back(argv[i]);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argv[i]);
        } else std::fprintf(stderr, "SCSI HD %d: %s FAILED\n", id, argv[i]);
    }

    // Battery-backed PRAM+clock (discrete RTC XPRAM) — persist it so a cold
    // PRAM doesn't retrigger the ROM's full-RAM burn-in every boot.
    static std::string pramPath =
        (hddPath.empty() ? std::string("quadra700") : hddPath) + ".q700.pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    // Discrete RTC: the file's clock froze while powered off — wall time
    // comes from the host at every launch (GUI only).
    mem.rtc().setSeconds(hostMacSeconds());

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // 640×480 shown at 2× fits with the menu bar and the CPU window.
    GLFWwindow* window = glfwCreateWindow(1320, 1080, "POM68K — Quadra 700", nullptr, nullptr);
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

    initDriveSfx(audioHost);
    mem.attachDriveSounds(&gFloppySfx, &gHddSfx);
    // GUI floppies persist committed writes back to the image file on
    // eject and on exit (opt-out: POM68K_FLOPPY_RO=1); tests never enable.
    mem.internalDrive().setWriteBack(std::getenv("POM68K_FLOPPY_RO") == nullptr);
    if (!audioHost.start()) std::fprintf(stderr, "audio: no output device (silent)\n");

    static Q700Machine machine{mem, cpu, audioHost};
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
            if (ImGui::BeginMenu("Disques")) {
                const auto disks = listDiskImages(c.hddPath);
                ImGui::TextDisabled("Démarrage (SCSI 0)");
                // Same filename shows up in several sections — scope the IDs.
                ImGui::PushID("boot");
                for (const std::string& d : disks) {
                    bool cur = samePath(d, c.hddPath);
                    std::string name = fs::path(d).filename().string();
                    if (ImGui::MenuItem(name.c_str(), nullptr, cur) && !cur)
                        relaunch(d, c.extraDisks);
                }
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Secondaires (SCSI 1-6)");
                ImGui::PushID("secondary");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Floppy (SWIM2)");
                ImGui::PushID("floppy");
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
                ImGui::PopID();
                ImGui::Separator();
                ImGui::TextDisabled("Changer un disque SCSI relance l'émulateur");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Redémarrer"))
                c.m.push({Q700Machine::Cmd::HardReset});
        });

        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        ImGui::Begin("Quadra 605", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        static ScreenInput input;
        input.frame(c.window, c.tex, ImVec2(float(hres * 2), float(vres * 2)),
                    [&](int dx, int dy) { c.m.push({Q700Machine::Cmd::MouseMove, dx, dy}); },
                    [&](bool down) { c.m.push({Q700Machine::Cmd::MouseButton, down ? 1 : 0}); });
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
                if (ImGui::IsKeyPressed(e.k, false)) {
                    keyTrace("push", uint8_t(e.m0110 >> 1), true);
                    c.m.push({Q700Machine::Cmd::Key, e.m0110 >> 1, 1});
                }
                if (ImGui::IsKeyReleased(e.k)) {
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
        // $420DBFF3 = Quadra 700 / 900 (+ PB140/170); POM68K supports the 700.
        if (ck == 0x420DBFF3)
            return runQ700(std::move(rom), matched, argc, argv);
        return runQuadra(std::move(rom), matched, argc, argv);
    }
    if (rom.size() == V8Memory::kRomSize) {
        // The header checksum (first 4 bytes, big-endian) is the model ID:
        // $350EACF0 = LC, $3193670E = Classic II, $35C28F5F = LC II. Any
        // other 512 KB dump gets the LC II profile (the reference V8).
        const uint32_t ck = uint32_t(rom[0]) << 24 | uint32_t(rom[1]) << 16
                          | uint32_t(rom[2]) << 8 | rom[3];
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
    if (rom.size() == MacIIMemory::kRomSize) {
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
    wireLocalTalk(mem, 272);                     // 230.4 kbit/s @ 7.8336 MHz

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
                pollLocalTalk(c.mem);
                if (atalkEnabled()) g_atalk.tick(c.cpu.getClock());
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
