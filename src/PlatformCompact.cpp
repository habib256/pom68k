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
// One row per compact profile — four parallel ternary chains before this.
struct CompactProfile {
    pom68k::SnapMachine snapshot;
    MacMemory::Model model;
    MachineKind kind;
    const char* name;
    const char* tag;      // PRAM / save-state / input-journal file stem
};
static constexpr CompactProfile kCompactProfiles[] = {
    {pom68k::SnapMachine::Plus, MacMemory::Model::Plus,
     MachineKind::Plus, "Macintosh Plus", "plus"},
    {pom68k::SnapMachine::Mac128K, MacMemory::Model::Mac128,
     MachineKind::Mac128, "Macintosh 128K", "mac128k"},
    {pom68k::SnapMachine::Mac512K, MacMemory::Model::Mac512,
     MachineKind::Mac512, "Macintosh 512K", "mac512k"},
    {pom68k::SnapMachine::SE, MacMemory::Model::SE,
     MachineKind::Se, "Macintosh SE", "se"},
    {pom68k::SnapMachine::SEFDHD, MacMemory::Model::SEFDHD,
     MachineKind::SeFdhd, "Macintosh SE FDHD", "sefdhd"},
    {pom68k::SnapMachine::Classic, MacMemory::Model::Classic,
     MachineKind::MacClassic, "Macintosh Classic", "classic"},
};
static const CompactProfile& compactProfile(pom68k::SnapMachine selected) {
    for (const CompactProfile& profile : kCompactProfiles)
        if (profile.snapshot == selected) return profile;
    return kCompactProfiles[0];              // the Plus: this map's default
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
    const CompactProfile& profile = compactProfile(selected);
    mem.setModel(profile.model);

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
    services.wireNetwork(mem);

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
        (hddPath.empty() ? std::string(profile.tag)
                         : hddPath + "." + profile.tag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    machine.state.kind = profile.snapshot;
    machine.state.setPath((hddPath.empty() ? std::string(profile.tag)
                                           : hddPath + "." + profile.tag) +
                          ".pomss");
    services.armInputRecording(machine, profile.tag, matched, media);
    machine.setFloppyInserted(diskOk, diskOk ? diskPath : std::string());
    return pom68k::gui::runCompactGui(
        machine, mem, cpu, audioHost, services,
        {matched, hddPath, diskOk ? diskPath : std::string(),
         std::move(mounted.extraDisks), pramPath,
         std::string("POM68K — ") + profile.name, profile.name, profile.kind,
         demoMode, MacVideo::kWidth, MacVideo::kHeight});
}

int pom68k::gui::composeCompact(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    return runCompact(std::move(launch.rom), launch.romName, launch.media,
                      services, launch.selected);
}
