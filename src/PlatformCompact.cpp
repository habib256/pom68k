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
               hostServices.config().diagnostics().keyTrace),
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
        clock.runFrame(cpu, mem, [this] { video.raster(mem); });
        services.pollNetwork(mem);
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
    const MacMemory::Model model =
        selected == pom68k::SnapMachine::SE ? MacMemory::Model::SE :
        selected == pom68k::SnapMachine::SEFDHD ? MacMemory::Model::SEFDHD :
        selected == pom68k::SnapMachine::Classic ? MacMemory::Model::Classic :
                                                   MacMemory::Model::Plus;
    mem.setModel(model);

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

    // First media argument: floppy, else probe disks35/ (CWD, exec dir, parent —
    // same resolution as the ROM, so it works whatever the launch directory).
    std::string diskPath = !media.empty()
        ? media[0] : services.locate("disks35/Disk605.dsk");
    const bool diskOk = !diskPath.empty() && mem.insertDisk(diskPath);
    if (diskOk) std::printf("Floppy: %s\n", diskPath.c_str());

    // Second media argument: SCSI disk, else probe hdv/HD20SC.vhd.
    std::string hddPath = media.size() > 1
        ? media[1] : services.locate("hdv/HD20SC.vhd");
    const bool hddOk = !hddPath.empty() && mem.attachScsi(hddPath, true);
    if (hddOk) std::printf("SCSI HD: %s (%u blocks, write-back)\n", hddPath.c_str(), mem.scsiDisk().blocks());
    if (!diskOk && !hddOk && !demoMode)
        std::fprintf(stderr, "No boot media — drop a .dsk in disks35/ or a .vhd in "
                     "hdv/ (looked relative to CWD and the executable).\n");

    // The compact 68000 siblings run this very machine (Plus map + ADB).
    const MacMemory::Model compactModel = mem.model();
    const char* machineName =
        compactModel == MacMemory::Model::SE      ? "Macintosh SE" :
        compactModel == MacMemory::Model::SEFDHD  ? "Macintosh SE FDHD" :
        compactModel == MacMemory::Model::Classic ? "Macintosh Classic" : "Macintosh Plus";
    const MachineKind compactKind =
        compactModel == MacMemory::Model::SE      ? MachineKind::Se :
        compactModel == MacMemory::Model::SEFDHD  ? MachineKind::SeFdhd :
        compactModel == MacMemory::Model::Classic ? MachineKind::MacClassic : MachineKind::Plus;

    // Battery-backed PRAM (Rtc.h). The compacts were the last platform
    // family without it: the Control Panel's settings — and the ROM's
    // startup-disk choice — died with the process. Tagged per model like
    // every other profile, since the four boards share one boot volume.
    // The clock is not in the file; host wall time was seeded above.
    const char* compactTag =
        compactModel == MacMemory::Model::SE      ? "se" :
        compactModel == MacMemory::Model::SEFDHD  ? "sefdhd" :
        compactModel == MacMemory::Model::Classic ? "classic" : "plus";
    const std::string pramPath =
        (hddPath.empty() ? std::string(compactTag)
                         : hddPath + "." + compactTag) + ".pram";
    if (mem.loadPram(pramPath)) std::printf("PRAM: %s\n", pramPath.c_str());
    machine.state.kind =
        compactModel == MacMemory::Model::SE      ? pom68k::SnapMachine::SE :
        compactModel == MacMemory::Model::SEFDHD  ? pom68k::SnapMachine::SEFDHD :
        compactModel == MacMemory::Model::Classic ? pom68k::SnapMachine::Classic
                                                  : pom68k::SnapMachine::Plus;
    machine.state.path = (hddPath.empty() ? std::string(compactTag)
                                           : hddPath + "." + compactTag) +
                         ".pomss";
    services.armInputRecording(machine, compactTag, matched, media);
    machine.setFloppyInserted(diskOk, diskOk ? diskPath : std::string());
    return pom68k::gui::runCompactGui(
        machine, mem, cpu, audioHost, services,
        {matched, hddPath, diskOk ? diskPath : std::string(), pramPath,
         std::string("POM68K — ") + machineName, machineName, compactKind,
         demoMode, MacVideo::kWidth, MacVideo::kHeight});
}

int pom68k::gui::composeCompact(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    return runCompact(std::move(launch.rom), launch.romName, launch.media,
                      services, launch.selected);
}
