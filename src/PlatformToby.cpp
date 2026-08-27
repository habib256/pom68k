// POM68K — Toby/NuBus platform composition
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PlatformCompositionSupport.h"

// This composer's own family — see the header's note on the fan-in.
#include "Cpu020.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "IIfxCpu.h"
#include "IIfxMemory.h"

#include "GuiRunnerToby.h"

using pom68k::gui::TobyRunnerSpec;
using pom68k::gui::TobyStatusView;
using pom68k::gui::runTobyGui;

// ── Mac II machine thread ───────────────────────────────────────────────
// Same GUI ↔ machine contract as LcMachine: queued commands, published
// framebuffer + status. Video is NuBus Toby (640×480); sound is discrete
// ASC @ $50F14000. Frame slice ≈ 60.15 Hz at 15.6672 MHz.
struct MacIiMachine
    : MachineHost<MacIiMachine, MacIIMemory, Cpu020, MacAudioHost> {
    using Base = MachineHost<MacIiMachine, MacIIMemory, Cpu020, MacAudioHost>;
    GuiHostServices& services;
    MacIiMachine(MacIIMemory& m, Cpu020& c, MacAudioHost& a,
                 GuiHostServices& hostServices)
        : Base(m, c, a, hostServices.config().diagnostics().keyTrace),
          services(hostServices) {}
    static constexpr bool kStereo = false;

    struct Status { uint32_t pc; long long clock; bool overlay, hmmu24; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (stFlags_.load(std::memory_order_relaxed) & 1) != 0,
                 (stFlags_.load(std::memory_order_relaxed) & 2) != 0 };
    }

    // ── The platform half of the host contract ─────────────────────────────
    int64_t frameCycles() const { return kFrame; }

    void emulateQuantum() {
        // Raster catch-up rides the wire slicing (LLE_VS_HLE §1.1): the Toby
        // card runs its own CRTC frame clock, while the SE/30's pseudo-slot
        // video has none and rides the machine's 60 Hz one.
        runQuantumWithWire(services, mem, cpu, kFrame,
                           [this] { rasterBeam(); });
        framesRun_++;
    }

    bool drainAudio() {
        samp_.clear();
        while (mem.asc().available() > 0)
            samp_.push_back(float(mem.asc().pop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }

    void renderFrame(std::vector<uint32_t>& out, int& w, int& h) {
        TobyVideo* tv = mem.toby();
        Se30Video* sv = mem.se30();
        w = tv ? tv->hres() : sv ? Se30Video::W : TobyVideo::W;
        h = tv ? tv->vres() : sv ? Se30Video::H : TobyVideo::H;
        // `fb_` is the RASTER SURFACE — emulateQuantum() decoded each row as
        // the beam scanned it. Catch up once more so a paused machine still
        // publishes a complete frame.
        if (tv || sv) rasterBeam();
        else fb_.assign(size_t(w) * size_t(h), 0xFFFFFFFFu);
        out.assign(fb_.begin(), fb_.end());
        // The decoders pack 00RRGGBB — alpha 0. ImGui blends, so a 0 alpha
        // draws fully transparent; force A=$FF before the BGRA upload.
        for (uint32_t& px : out) px |= 0xFF000000u;
    }

    void publishStatus() {
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               (mem.hmmu24() ? 2 : 0)),
                       std::memory_order_relaxed);
    }

private:
    static constexpr int64_t kFrame = MacIIMemory::kCpuHz / 60;

    void rasterBeam() {
        if (TobyVideo* tv = mem.toby()) tv->raster(fb_);
        else if (Se30Video* sv = mem.se30())
            sv->raster(fb_, mem.framePos(), mem.frameCycles(), mem.frameCount());
    }
};

// ── Macintosh II: GLUE + 68020 + Toby NuBus, selected by a 256 KB ROM ───
// ── Mac IIfx machine thread (platform #12) ──────────────────────────────
// The MacIiMachine contract, on the OSS + dual-IOP board: input crosses as
// queued commands, the framebuffer as a decoded copy, status as relaxed
// atomics. What is IIfx-specific is what the CPU window shows — the two
// Apple PIC IOPs are processors in their own right, and "are they still
// executing?" is the first question any IIfx bug asks.
struct IIfxMachine
    : MachineHost<IIfxMachine, IIfxMemory, IIfxCpu, MacAudioHost> {
    using Base = MachineHost<IIfxMachine, IIfxMemory, IIfxCpu, MacAudioHost>;
    GuiHostServices& services;
    IIfxMachine(IIfxMemory& m, IIfxCpu& c, MacAudioHost& a,
                GuiHostServices& hostServices)
        : Base(m, c, a, hostServices.config().diagnostics().keyTrace),
          services(hostServices) {}
    static constexpr bool kStereo = false;

    struct Status { uint32_t pc; long long clock; bool overlay;
                    long long sccPicCycles, swimPicCycles; };
    Status status() const {
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 stOverlay_.load(std::memory_order_relaxed),
                 stSccPic_.load(std::memory_order_relaxed),
                 stSwimPic_.load(std::memory_order_relaxed) };
    }

    // ── The platform half of the host contract ─────────────────────────────
    int64_t frameCycles() const { return kFrame; }

    void emulateQuantum() {
        // Raster catch-up on the Toby card's own CRTC frame clock
        // (LLE_VS_HLE §1.1) — the IIfx has no built-in video.
        runQuantumWithWire(services, mem, cpu, kFrame, [this] {
            if (TobyVideo* tv = mem.toby()) tv->raster(fb_);
        });
        framesRun_++;
    }

    bool drainAudio() {
        samp_.clear();
        while (mem.asc().available() > 0)
            samp_.push_back(float(mem.asc().pop()) / 32768.0f);
        float lo = 1.f, hi = -1.f;
        for (float v : samp_) { if (v < lo) lo = v; if (v > hi) hi = v; }
        return !samp_.empty() && hi - lo >= 0.02f;
    }

    void renderFrame(std::vector<uint32_t>& out, int& w, int& h) {
        TobyVideo* tv = mem.toby();
        w = tv ? tv->hres() : TobyVideo::W;
        h = tv ? tv->vres() : TobyVideo::H;
        if (tv) tv->raster(fb_);          // raster surface, see emulateQuantum()
        else fb_.assign(size_t(w) * size_t(h), 0xFFFFFFFFu);
        out.assign(fb_.begin(), fb_.end());
        for (uint32_t& px : out) px |= 0xFF000000u;
    }

    // The two IOPs' cycle counters are the visible sign that their firmware is
    // running at all — the machine window shows them, and a frozen counter is
    // the first thing to look at when ADB goes quiet.
    void publishStatus() {
        stOverlay_.store(mem.overlay(), std::memory_order_relaxed);
        stSccPic_.store(mem.sccPic().cpu().cycleCount(), std::memory_order_relaxed);
        stSwimPic_.store(mem.swimPic().cpu().cycleCount(), std::memory_order_relaxed);
    }

private:
    // 60.15 Hz on a 40 MHz clock (the IIfx's OSS tick, not a round 60).
    static constexpr int64_t kFrame = IIfxMemory::kCpuHz * 100 / 6015;

    std::atomic<bool> stOverlay_{true};
    std::atomic<long long> stSccPic_{0}, stSwimPic_{0};
};

// ── Macintosh IIfx: OSS + two Apple PIC IOPs, 68030 @ 40 MHz ────────────
// Selected by the 512 KB ROM whose header checksum is $4147DD77. No
// built-in video: the machine boots on the slot-9 Toby NuBus card, and
// ADB is bit-banged by the SWIM IOP's own 65C02 firmware
// (docs/IOP_BRINGUP.md).
// ── Macintosh II family: GLUE + 68020/68030 + Toby or SE/30 video ───────
static int runMacII(std::vector<uint8_t> rom, const std::string& romName,
                    const std::vector<std::string>& media,
                    GuiHostServices& services,
                    MacIIMemory::Model model = MacIIMemory::Model::MacII) {
    const bool is030 = model != MacIIMemory::Model::MacII;
    const bool se30 = model == MacIIMemory::Model::SE30;
    const char* name = model == MacIIMemory::Model::IIx  ? "IIx"
                     : model == MacIIMemory::Model::IIcx ? "IIcx"
                     : se30 ? "SE/30" : "II";
    std::printf("Machine: Macintosh %s (%s @ 15.6672 MHz, %s%s)\n",
                name, is030 ? "68030 + PMMU" : "68020",
                se30 ? "512×342 interne" : "Toby NuBus",
                !services.config().cpu().fpu
                    ? ""
                    : (is030 ? ", soft 68882" : ", soft 68881"));
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(),
                rom.size() / 1024);

    MacIIMemory& mem = services.own<MacIIMemory>(services.config().core(), 0x800000, model);
    Cpu020& cpu = services.own<Cpu020>(
        mem, services.config().jit().resolved, services.config().core().cpu,
        services.config().cpu().fpu, is030);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "FAIL: bad Mac II ROM\n");
        return 1;
    }
    if (se30) {
        if (!mem.installSe30Video())
            std::fprintf(stderr,
                         "SE/30: se30vrom.uk6 manquante (roms/se30/) — "
                         "pas de video\n");
    } else {
        mem.installTobyVideo();
    }
    mem.setCpu(&cpu);

    const std::string pramTag = se30 ? "se30"
        : model == MacIIMemory::Model::IIx  ? "iix"
        : model == MacIIMemory::Model::IIcx ? "iicx" : "macii";
    const pom68k::SnapMachine snap = model == MacIIMemory::Model::IIx
        ? pom68k::SnapMachine::IIx
        : model == MacIIMemory::Model::IIcx ? pom68k::SnapMachine::IIcx
        : se30 ? pom68k::SnapMachine::SE30 : pom68k::SnapMachine::MacII;
    const TobyRunnerSpec spec{
        std::string("Macintosh ") + name,
        "Macintosh II",
        is030 ? "68030 @ 15.6672 MHz (Moira)"
              : "68020 @ 15.6672 MHz (Moira)",
        pramTag,
        pramTag,
        {"hdv/System 6.0.8 HD.dsk", "hdv/HD20SC.vhd",
         "hdv/GISTPERSO-boot.vhd", "hdv/boot.vhd"},
        MachineKind::MacII,
        snap,
        true,
    };
    return runTobyGui<MacIiMachine>(
        mem, cpu, audioHost, spec,
        [&] { mem.rtc().setSeconds(services.hostMacSeconds()); },
        [](MacIiMachine& machine) {
            const MacIiMachine::Status st = machine.status();
            return TobyStatusView{
                st.pc, st.clock, st.overlay, st.hmmu24,
                machine.mem.toby() ? machine.mem.toby()->hres() : 0,
                machine.mem.toby() ? machine.mem.toby()->vres() : 0};
        },
        romName, media, services);
}

// ── Macintosh IIfx: OSS + two Apple PIC IOPs, 68030 @ 40 MHz ────────────
// ADB remains bit-banged by the SWIM IOP's own 65C02 firmware; the shared
// runner changes only the host lifecycle around that board contract.
static int runIIfx(std::vector<uint8_t> rom, const std::string& romName,
                   const std::vector<std::string>& media,
                   GuiHostServices& services) {
    std::printf("Machine: Macintosh IIfx (68030 @ 40 MHz, OSS + 2 IOP 65C02, "
                "Toby NuBus%s)\n",
                services.config().cpu().fpu ? ", soft 68882" : "");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(),
                rom.size() / 1024);

    IIfxMemory& mem = services.own<IIfxMemory>(services.config().core(), 0x800000);
    IIfxCpu& cpu = services.own<IIfxCpu>(
        mem, services.config().jit().resolved, services.config().cpu().fpu);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "FAIL: bad Mac IIfx ROM\n");
        return 1;
    }
    mem.installTobyVideo();
    mem.setCpu(&cpu);

    const TobyRunnerSpec spec{
        "Macintosh IIfx",
        "Macintosh IIfx",
        "68030 @ 40 MHz (Moira)",
        "iifx",
        "IIfx",
        {"hdv/MacOS-7.6-boot.vhd", "hdv/GISTPERSO-boot.vhd",
         "hdv/boot.vhd"},
        MachineKind::IIfx,
        pom68k::SnapMachine::IIfx,
        false,
    };
    return runTobyGui<IIfxMachine>(
        mem, cpu, audioHost, spec,
        [&] { mem.rtc().setSeconds(services.hostMacSeconds()); },
        [](IIfxMachine& machine) {
            const IIfxMachine::Status st = machine.status();
            return TobyStatusView{
                st.pc, st.clock, st.overlay, false,
                machine.mem.toby() ? machine.mem.toby()->hres() : 0,
                machine.mem.toby() ? machine.mem.toby()->vres() : 0,
                true, st.sccPicCycles, st.swimPicCycles};
        },
        romName, media, services);
}


int pom68k::gui::composeToby(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    if (launch.platform == pom68k::PlatformKind::Oss)
        return runIIfx(std::move(launch.rom), launch.romName, launch.media,
                       services);
    const MacIIMemory::Model model =
        launch.selected == pom68k::SnapMachine::IIx ? MacIIMemory::Model::IIx :
        launch.selected == pom68k::SnapMachine::IIcx ? MacIIMemory::Model::IIcx :
        launch.selected == pom68k::SnapMachine::SE30 ? MacIIMemory::Model::SE30 :
                                                       MacIIMemory::Model::MacII;
    return runMacII(std::move(launch.rom), launch.romName, launch.media,
                    services, model);
}
