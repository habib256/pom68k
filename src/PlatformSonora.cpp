// POM68K — Sonora/VASP/RBV platform composition
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PlatformCompositionSupport.h"
#include "GuiRunnerSonora.h"

using pom68k::gui::SonoraRunnerSpec;
using pom68k::gui::runSonoraGui;

// ── Mac LC III (Phase C): Sonora + 68030 @ 25 MHz ───────────────────────
// Same GUI ↔ machine contract as LcMachine (the QuadraMachine precedent:
// per-machine thread struct, queued commands, decoded framebuffer copy).
// Resolution comes from the ACTIVE Sonora modeline, monitor pick via the
// sense buttons (2 = 512×384, 6 = 640×480) like the LC II.
template <class Mem, class Cpu, class Video>
struct SonoraStyleMachine
    : MachineHost<SonoraStyleMachine<Mem, Cpu, Video>, Mem, Cpu, MacAudioHost> {
    using Base = MachineHost<SonoraStyleMachine<Mem, Cpu, Video>, Mem, Cpu,
                             MacAudioHost>;
    using Base::mem; using Base::cpu; using Base::fb_; using Base::samp_;
    using Base::framesRun_; using Base::stPc_; using Base::stClock_;
    using Base::stFlags_;
    static constexpr bool kStereo = false;

    Video& video;
    GuiHostServices& services;
    SonoraStyleMachine(Mem& m, Cpu& c, Video& v, MacAudioHost& a,
                       GuiHostServices& hostServices)
        : Base(m, c, a, hostServices.config().diagnostics().keyTrace),
          video(v), services(hostServices) {}

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

    // ── The platform half of the host contract ─────────────────────────────
    int64_t frameCycles() const { return kFrame; }

    void emulateQuantum() {
        // Raster catch-up rides the wire slicing: each row is decoded once,
        // when the beam scans it (LLE_VS_HLE §1.1, VideoBeam.h). Same total
        // work per frame as the old whole-frame decode, correctly placed.
        auto beam = [this] { video.raster(fb_); };
        if (mem.cpuHeld()) { mem.tick(int(kFrame)); beam(); }  // Egret hold
        else runQuantumWithWire(services, mem, cpu, kFrame, beam);
        framesRun_++;
    }

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
        // `fb_` is the RASTER SURFACE — emulateQuantum() already decoded each
        // row as the beam scanned it. Catch up once more so a paused or held
        // machine still publishes a complete frame, then copy out: the
        // surface itself stays alpha-free, since the next frame overwrites
        // only the rows the beam repaints.
        video.raster(fb_, /*full=*/true);
        out.assign(fb_.begin(), fb_.end());
        for (uint32_t& px : out) px |= 0xFF000000u;
    }

    void publishStatus() {
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC() & 0x80000000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        // Machine-thread write (Cmd::Sense), so it must cross as an atomic —
        // see the Sense arm in MachineHost::applyCmds.
        stSense_.store(mem.monitorSense(), std::memory_order_relaxed);
    }

private:
    const int64_t kFrame = mem.cpuHz() / 60;   // true machine clock (33 MHz+)

    std::atomic<uint8_t> stSense_{0};   // monitor sense, machine → GUI
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

using pom68k::gui::GuiHostServices;

static int runLc3(std::vector<uint8_t> rom, const std::string& romName,
                  const std::vector<std::string>& media,
                  GuiHostServices& services,
                  SonoraModel model = SonoraModel::Lc3) {
    // The LC III Egret boards and the Cuda all-in-ones share a ROM family,
    // but their model longword, clock, monitor and battery files are profile
    // data. The descriptor keeps those identities out of the common loop.
    struct Profile {
        const char* name;
        int mhz;
        int64_t cpuHz;
        uint32_t id;
        bool cuda;
        uint8_t sense;
        const char* tag;
        pom68k::SnapMachine snap;
    };
    static const Profile kProfiles[] = {
        {"LC III", 25, SonoraMemory::kCpuHz, SonoraMemory::kIdLc3,
         false, 6, "lc3", pom68k::SnapMachine::Lc3},
        {"LC III+", 33, SonoraMemory::kCpuHzPlus, SonoraMemory::kIdLc3Plus,
         false, 6, "lc3plus", pom68k::SnapMachine::Lc3Plus},
        {"LC 520", 25, SonoraMemory::kCpuHz, SonoraMemory::kIdLc520,
         true, 6, "lc520", pom68k::SnapMachine::Lc520},
        {"LC 550", 33, SonoraMemory::kCpuHzPlus, SonoraMemory::kIdLc550,
         true, 6, "lc550", pom68k::SnapMachine::Lc550},
        {"Color Classic II", 33, SonoraMemory::kCpuHzPlus,
         SonoraMemory::kIdLc550, true, 2, "cclassic2",
         pom68k::SnapMachine::CClassic2},
    };
    const Profile& profile = kProfiles[int(model)];
    const SonoraRunnerSpec spec{
        std::string("Macintosh ") + profile.name,
        profile.tag,
        {},
        "68030 @ " + std::to_string(profile.mhz) + " MHz (Moira + PMMU)",
        profile.cuda ? MachineKind::Aio : MachineKind::Lc3,
        profile.snap,
        profile.sense,
        0.08f, 0.08f, 0.10f,
    };
    std::printf("Machine: %s (68030 @ %d MHz, Sonora%s, %s)\n",
                spec.name.c_str(), profile.mhz,
                services.config().cpu().fpu ? ", 68882" : "",
                profile.cuda ? "Cuda" : "Egret");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    SonoraMemory& mem = services.own<SonoraMemory>(services.config().core(),
        0x800000, profile.cpuHz, profile.id, profile.cuda);
    // POM68K_NOFPU models the stock bare machine; target disks issue FPU ops,
    // so the existing product default remains ON.
    SonoraCpu& cpu = services.own<SonoraCpu>(mem, services.config().jit().resolved,
        services.config().core().cpu, services.config().cpu().fpu);
    SonoraVideo& video = services.own<SonoraVideo>(mem);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runSonoraGui<Lc3Machine>(
        mem, cpu, video, audioHost, spec,
        [&] { mem.setRtcSeconds(services.hostMacSeconds()); mem.egret().factoryDefaults(); },
        romName, media, services);
}

// ── Mac IIvx / IIvi (VASP) ──────────────────────────────────────────────
// The runLc3 shell on the VASP machine (VaspMemory/VaspCpu/VaspVideo,
// Egret 341S0851 LLE): IIvx = 68030 + 68882 @ 31.3344 MHz ($A55A2015),
// IIvi = 15.6672 MHz ($A55A2016) — MAME maciivx.cpp. POM68K_IIVI picks
// the IIvi (the GUI menu sets it before the relaunch).
static int runVasp(std::vector<uint8_t> rom, const std::string& romName,
                   const std::vector<std::string>& media,
                   GuiHostServices& services, bool vi = false) {
    const SonoraRunnerSpec spec{
        vi ? "Macintosh IIvi" : "Macintosh IIvx",
        vi ? "iivi" : "iivx",
        {},
        {},
        MachineKind::Vasp,
        vi ? pom68k::SnapMachine::IIvi : pom68k::SnapMachine::IIvx,
        6,
        0.10f, 0.10f, 0.12f,
    };
    std::printf("Machine: %s (68030 @ %s MHz, VASP%s, Egret)\n",
                spec.name.c_str(), vi ? "16" : "32",
                services.config().cpu().fpu ? ", 68882" : "");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    VaspMemory& mem = services.own<VaspMemory>(services.config().core(), 0x800000,
        vi ? VaspMemory::kCpuHzVi : VaspMemory::kCpuHzVx,
        vi ? VaspMemory::kIdIIvi : VaspMemory::kIdIIvx);
    VaspCpu& cpu = services.own<VaspCpu>(
        mem, services.config().jit().resolved, services.config().core().cpu,
        services.config().cpu().fpu);
    VaspVideo& video = services.own<VaspVideo>(mem);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runSonoraGui<VaspMachine>(
        mem, cpu, video, audioHost, spec,
        [&] { mem.setRtcSeconds(services.hostMacSeconds()); mem.egret().factoryDefaults(); },
        romName, media, services);
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
                   const std::vector<std::string>& media,
                   GuiHostServices& services, bool iici = false) {
    const SonoraRunnerSpec spec{
        iici ? "Macintosh IIci" : "Macintosh IIsi",
        iici ? "iici" : "iisi",
        iici ? "hdv/iici-boot.vhd" : "hdv/iisi-boot.vhd",
        {},
        iici ? MachineKind::IIci : MachineKind::IIsi,
        iici ? pom68k::SnapMachine::IIci : pom68k::SnapMachine::IIsi,
        6,
        0.10f, 0.10f, 0.12f,
    };
    std::printf("Machine: %s (68030 @ %d MHz, RBV%s, %s)\n",
                spec.name.c_str(), iici ? 25 : 20,
                services.config().cpu().fpu ? ", 68882" : "",
                iici ? "ADB modem + RTC" : "Egret");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(), rom.size() / 1024);

    RbvMemory& mem = services.own<RbvMemory>(services.config().core(), 0x800000,
        iici ? RbvMemory::kCpuHzCi : RbvMemory::kCpuHz, iici);
    RbvCpu& cpu = services.own<RbvCpu>(
        mem, services.config().jit().resolved, services.config().core().cpu,
        services.config().cpu().fpu);
    RbvVideo& video = services.own<RbvVideo>(mem);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    mem.loadRom(rom);
    mem.setCpu(&cpu);
    return runSonoraGui<RbvMachine>(
        mem, cpu, video, audioHost, spec,
        [&, iici] {
            if (iici) mem.rtc().setSeconds(services.hostMacSeconds());
            else {
                mem.setRtcSeconds(services.hostMacSeconds());
                mem.egret().factoryDefaults();
            }
        },
        romName, media, services);
}


int pom68k::gui::composeSonora(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    if (launch.platform == pom68k::PlatformKind::Rbv)
        return runIIsi(std::move(launch.rom), launch.romName, launch.media,
                       services, launch.selected == pom68k::SnapMachine::IIci);
    if (launch.platform == pom68k::PlatformKind::Vasp)
        return runVasp(std::move(launch.rom), launch.romName, launch.media,
                       services, launch.selected == pom68k::SnapMachine::IIvi);
    const SonoraModel model =
        launch.selected == pom68k::SnapMachine::Lc3Plus ? SonoraModel::Lc3Plus :
        launch.selected == pom68k::SnapMachine::Lc520 ? SonoraModel::Lc520 :
        launch.selected == pom68k::SnapMachine::Lc550 ? SonoraModel::Lc550 :
        launch.selected == pom68k::SnapMachine::CClassic2 ?
            SonoraModel::CClassic2 : SonoraModel::Lc3;
    return runLc3(std::move(launch.rom), launch.romName, launch.media,
                  services, model);
}
