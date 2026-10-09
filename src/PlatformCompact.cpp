// POM68K — compact 68000 platform composition
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PlatformCompositionSupport.h"

// This composer's own family — see the header's note on the fan-in.
#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include "MacAudio.h"
#include "MacAudioHost.h"
#include "DemoRom.h"

#include "CompactMedia.h"
#include "GuiRunnerCompact.h"

// ── 68000 compact host ──────────────────────────────────────────────────
// Native builds drive this through MachineHost::start(); Emscripten calls
// the exact same stepTick() from the GUI callback. Only the driver differs.
struct CompactMachine
    : MachineHost<CompactMachine, MacMemory, Cpu68k, MacAudioHost> {
    using Base = MachineHost<CompactMachine, MacMemory, Cpu68k, MacAudioHost>;
    static constexpr bool kStereo = false;

    MacVideo& video;
    MacAudio& audio;
    GuiHostServices& services;
    MacFrameClock clock;

    CompactMachine(MacMemory& m, Cpu68k& c, MacVideo& v, MacAudio& sound,
                   MacAudioHost& host, GuiHostServices& hostServices)
        : Base(m, c, host,
               hostServices.config().diagnostics().keyTrace,
               hostServices.config().devices().turbo),
          video(v), audio(sound), services(hostServices) {}

    struct Status { uint32_t pc; long long clock; bool overlay; };
    Status status() const {
        return {stPc_.load(std::memory_order_relaxed),
                stClock_.load(std::memory_order_relaxed),
                (stFlags_.load(std::memory_order_relaxed) & 1) != 0};
    }

    int64_t frameCycles() const { return kCyclesPerFrame; }
    void afterHardReset() { clock.resync(cpu); }
    void afterRestore() { clock.resync(cpu); }

    void emulateQuantum() {
        // MacFrameClock already subdivides the frame for the beam. Poll host
        // wires at those same safe boundaries so serial Rx is not delayed by
        // a full 16.6 ms frame (and no timing boundary moves).
        clock.runFrame(cpu, mem, [this] {
            video.raster(mem);
            services.pollNetwork(mem);
        }, services.serialActive() ? 64 : 16);
        services.tickNetwork(cpu.machineClock());
        framesRun_++;
    }

    bool drainAudio() {
        samp_.clear();
        audio.renderFrame(mem, samp_);
        float lo = 1.f, hi = -1.f;
        for (float value : samp_) {
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        }
        return !samp_.empty() && hi - lo >= 0.02f;
    }

    void renderFrame(std::vector<uint32_t>& out, int& w, int& h) {
        const uint32_t* pixels = video.raster(mem);
        w = video.width();
        h = video.height();
        out.assign(pixels, pixels + size_t(w) * size_t(h));
    }

    void publishStatus() {
        stFlags_.store(mem.overlay() ? 1 : 0, std::memory_order_relaxed);
    }
};
// Board variants only; names, runner and file stems belong to the catalogue.
static MacMemory::Model compactModel(pom68k::SnapMachine selected) {
    using S = pom68k::SnapMachine;
    using M = MacMemory::Model;
    switch (selected) {
        case S::Mac128K: return M::Mac128;
        case S::Mac512K: return M::Mac512;
        case S::Mac512Ke: return M::Mac512e;
        case S::SE: return M::SE;
        case S::SEFDHD: return M::SEFDHD;
        case S::Classic: return M::Classic;
        default: return M::Plus;
    }
}

// Compact composition. Native sessions use CompactMachine's worker thread;
// Emscripten drives the same MachineHost::stepTick() from its frame callback.
static int runCompact(std::vector<uint8_t> rom, const std::string& matched,
                      const std::vector<std::string>& media,
                      GuiHostServices& services, pom68k::SnapMachine selected) {
    MacMemory& mem = services.own<MacMemory>(services.config().core(), MacMemory::Model::Plus);
    Cpu68k& cpu = services.own<Cpu68k>(
        mem, services.config().jit().resolved);
    MacVideo& video = services.own<MacVideo>();
    MacAudio& audio = services.own<MacAudio>();
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    const auto& profile = *pom68k::machineProfile(selected);
    mem.setModel(compactModel(selected));

    const bool demoMode = rom.empty() || !mem.loadRom(rom);
    if (demoMode) {
        mem.installRom(kDemoRom, kDemoRomSize);
        std::printf("No Mac Plus ROM — running built-in 68000 demo. "
                    "Drop macplus.rom (128K) in roms/ for the real thing.\n");
    } else {
        std::printf("Loaded ROM: %s (%zu KB)\n", matched.c_str(), rom.size() / 1024);
    }
    mem.setCpu(&cpu);
    CompactMachine& machine = services.own<CompactMachine>(
        mem, cpu, video, audio, audioHost, services);
    cpu.hardReset();
    machine.afterHardReset();
    mem.rtc().setSeconds(services.hostMacSeconds());
    services.wireNetwork(mem, cpu);

    pom68k::gui::CompactMountedMedia mounted =
        pom68k::gui::mountCompactMedia(mem, media, services, demoMode);
    const bool diskOk = mounted.floppyOk;
    const std::string& diskPath = mounted.floppyPath;
    const std::string& hddPath = mounted.hddPath;

    // Battery-backed PRAM (Rtc.h). The compacts were the last platform
    // family without it: the Control Panel's settings — and the ROM's
    // startup-disk choice — died with the process. Tagged per model like
    // every other profile, since the four boards share one boot volume.
    // The clock is not in the file; host wall time was seeded above.
    const std::string pramPath =
        (hddPath.empty() ? std::string(profile.slug)
                         : hddPath + "." + profile.slug) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    machine.state.kind = profile.snapshot;
    machine.state.setPath((hddPath.empty() ? std::string(profile.slug)
                                           : hddPath + "." + profile.slug) +
                          ".pomss");
    services.armInputRecording(machine, profile.slug, matched, media);
    machine.setFloppyInserted(diskOk, diskOk ? diskPath : std::string());
    return pom68k::gui::runCompactGui(
        machine, mem, cpu, audioHost, services,
        {matched, hddPath, diskOk ? diskPath : std::string(),
         std::move(mounted.extraDisks), pramPath,
         std::string("POM68K — ") + profile.label, profile.label, profile.kind,
         demoMode, MacVideo::kWidth, MacVideo::kHeight});
}

int pom68k::gui::composeCompact(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    return runCompact(std::move(launch.rom), launch.romName, launch.media,
                      services, launch.selected);
}
