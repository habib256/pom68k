// POM68K — V8/Eagle/Spice/Tinker Bell platform composition
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PlatformCompositionSupport.h"
#include "GuiRunnerV8.h"

using pom68k::gui::V8RunnerSpec;
using pom68k::gui::runV8Gui;

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
struct LcMachine
    : MachineHost<LcMachine, V8Memory, Cpu030, MacAudioHost> {
    using Base = MachineHost<LcMachine, V8Memory, Cpu030, MacAudioHost>;
    static constexpr bool kStereo = false;

    V8Video& video;
    GuiHostServices& services;
    LcMachine(V8Memory& m, Cpu030& c, V8Video& v, MacAudioHost& a,
              GuiHostServices& hostServices)
        : Base(m, c, a, hostServices.config().diagnostics().keyTrace),
          video(v), services(hostServices) {}

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

    // ── The platform half of the host contract ─────────────────────────────
    int64_t frameCycles() const { return kFrame; }

    void emulateQuantum() {
        // The raster catch-up rides the wire slicing: each row is decoded
        // once, when the beam scans it (LLE_VS_HLE §1.1, gate
        // v8_raster_test). Total work per frame is unchanged — it is the
        // same rows, placed correctly instead of all at publish time.
        auto beam = [this] { video.raster(fb_); };
        if (mem.cpuHeld()) { mem.tick(kFrame); beam(); }   // Egret power-on hold
        else runQuantumWithWire(services, mem, cpu, kFrame, beam);
        framesRun_++;
    }

    // Drain the ASC samples produced by the last slice (22 257 Hz mono,
    // continuous — an empty FIFO repeats its stale byte) and report whether
    // they carry real sound (AC span, same gate as MacAudioHost::pushFrame).
    // The facade dispatches per model (Spice = Sonora EASC, mono L+R mix).
    bool drainAudio() {
        samp_.clear();
        while (mem.ascAvailable() > 0)
            samp_.push_back(float(mem.ascPop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }

    void renderFrame(std::vector<uint32_t>& out, int& w, int& h) {
        video.size(w, h);
        // `fb_` is the RASTER SURFACE: emulateQuantum() already decoded each
        // row at the moment the beam scanned it. Catch up once more here so a
        // paused or held machine still publishes a complete frame, then copy
        // out — the surface itself must stay alpha-free, since the next frame
        // overwrites only the rows the beam repaints.
        video.raster(fb_, /*full=*/true);
        // The decoders pack 00RRGGBB — alpha 0. ImGui renders textures with
        // alpha blending on, so a 0 alpha draws fully transparent (black
        // window background); force A=$FF before the BGRA upload.
        out.assign(fb_.begin(), fb_.end());
        for (uint32_t& px : out) px |= 0xFF000000u;
    }

    void publishStatus() {
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC() & 0x80000000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        stConfig_.store(mem.ramConfig(), std::memory_order_relaxed);
        stSense_.store(mem.monitorSense(), std::memory_order_relaxed);
    }

private:
    // CPU cycles per 60 Hz slice: 640×407 dots at C15M for the V8 machines,
    // the true clock/60 for the Mac TV's C32M Tinker Bell (~2× the cycles).
    const int kFrame = mem.cpuHz() == V8Memory::kCpuHz
        ? 640 * 407 : int(mem.cpuHz() / 60);

    std::atomic<uint8_t> stConfig_{0};
    std::atomic<uint8_t> stSense_{0};   // monitor sense, machine → GUI
};

// ── Mac LC II (O6): V8 + 68030, selected by a 512 KB ROM ────────────────
// Also drives the two V8-family siblings (Phase C, MAME maclc.cpp): the
// Macintosh LC (same board, 68020 + 2 MB soldered) and the Classic II
// (Eagle = V8 with built-in 512×342 mono video). Their board-facing host
// contract stays here; GuiShell.h owns the shared process/UI lifecycle.
// ── V8/Eagle/Spice/Tinker Bell composition wrapper ──────────────────────
static int runLcII(std::vector<uint8_t> rom, const std::string& romName,
                   const std::vector<std::string>& media,
                   GuiHostServices& services,
                   V8Memory::Model model = V8Memory::Model::LcII) {
    const bool lc = model == V8Memory::Model::Lc;
    const bool classic = model == V8Memory::Model::ClassicII;
    const bool colorClassic = model == V8Memory::Model::ColorClassic;
    const bool macTv = model == V8Memory::Model::MacTv;
    const char* name = lc ? "Macintosh LC"
        : classic ? "Macintosh Classic II"
        : colorClassic ? "Macintosh Color Classic"
        : macTv ? "Macintosh TV" : "Macintosh LC II";
    const char* tag = lc ? "lc" : classic ? "classic2"
        : colorClassic ? "cclassic" : macTv ? "mactv" : "lcii";
    const int64_t cpuHz = macTv ? V8Memory::kCpuHzTv : V8Memory::kCpuHz;
    const MachineKind kind = lc ? MachineKind::Lc
        : classic ? MachineKind::ClassicII
        : colorClassic ? MachineKind::ColorClassic
        : macTv ? MachineKind::MacTv : MachineKind::LcII;
    const pom68k::SnapMachine snap = lc ? pom68k::SnapMachine::Lc
        : classic ? pom68k::SnapMachine::ClassicII
        : colorClassic ? pom68k::SnapMachine::ColorClassic
        : macTv ? pom68k::SnapMachine::MacTv : pom68k::SnapMachine::LcII;
    const V8RunnerSpec spec{
        name,
        lc ? "68020" : "68030",
        tag,
        double(cpuHz) / 1e6,
        kind,
        snap,
        !lc,
        !classic && !colorClassic && !macTv,
        !classic && !colorClassic,
    };

    std::printf("Machine: %s (%s @ %.4f MHz, %s%s)\n",
                spec.name.c_str(), spec.cpu.c_str(), spec.cpuMhz,
                classic ? "Eagle" : colorClassic ? "Spice"
                : macTv ? "Tinker Bell" : "V8",
                (!services.config().cpu().fpu || macTv) ? "" : ", 68882");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(),
                rom.size() / 1024);

    // Tinker Bell caps at 8 MB and has no FPU socket; the other profiles go
    // to 10 MB. The LC selects the 68020-compatible core contract.
    V8Memory& mem = services.own<V8Memory>(services.config().core(),
        macTv ? 0x800000u : 0xA00000u, model, cpuHz);
    Cpu030& cpu = services.own<Cpu030>(mem, services.config().jit().resolved,
        services.config().core().cpu,
        services.config().cpu().fpu && !macTv, lc);
    V8Video& video = services.own<V8Video>(mem);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runV8Gui<LcMachine>(
        mem, cpu, video, audioHost, spec,
        [&] {
            if (const auto& log = services.config().diagnostics().fpuLog) {
                cpu.enableFpuLog(log->c_str());
                std::printf("Line-F log: %s (CPU single-steps — slower)\n",
                            log->c_str());
            }
        },
        [&] {
            mem.setRtcSeconds(services.hostMacSeconds());
            mem.egret().factoryDefaults();
        },
        romName, media, services);
}


int pom68k::gui::composeV8(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    const V8Memory::Model model =
        launch.selected == pom68k::SnapMachine::Lc ? V8Memory::Model::Lc :
        launch.selected == pom68k::SnapMachine::ClassicII ?
            V8Memory::Model::ClassicII :
        launch.selected == pom68k::SnapMachine::ColorClassic ?
            V8Memory::Model::ColorClassic :
        launch.selected == pom68k::SnapMachine::MacTv ? V8Memory::Model::MacTv :
                                                        V8Memory::Model::LcII;
    return runLcII(std::move(launch.rom), launch.romName, launch.media,
                   services, model);
}
