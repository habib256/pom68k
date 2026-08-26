// POM68K — PowerBook Duo platform composition
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PlatformCompositionSupport.h"
#include "GuiRunnerDuo.h"

using pom68k::gui::DuoRunnerSpec;
using pom68k::gui::runDuoGui;

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
struct MscMachine
    : MachineHost<MscMachine, MscMemory, MscCpu, MacAudioHost> {
    using Base = MachineHost<MscMachine, MscMemory, MscCpu, MacAudioHost>;
    GuiHostServices& services;
    MscMachine(MscMemory& m, MscCpu& c, MacAudioHost& a,
               GuiHostServices& hostServices)
        : Base(m, c, a, hostServices.config().diagnostics().keyTrace),
          services(hostServices) {}
    static constexpr bool kStereo = false;

    struct Status { uint32_t pc; long long clock; bool overlay, mmu, held;
                    uint8_t gscMode; };
    Status status() const {
        const uint8_t f = stFlags_.load(std::memory_order_relaxed);
        return { stPc_.load(std::memory_order_relaxed),
                 stClock_.load(std::memory_order_relaxed),
                 (f & 1) != 0, (f & 2) != 0, (f & 4) != 0,
                 stGsc_.load(std::memory_order_relaxed) };
    }

    // ── The platform half of the host contract ─────────────────────────────
    int64_t frameCycles() const { return kFrame; }

    void emulateQuantum() {
        // PG&E hold: the PMU releases the 68030 through its port E, so until
        // it does the peripherals must still be clocked or the MCU never gets
        // there (duo230_boot_etalon.cpp:52). Same shape as the Egret power-on
        // hold on the V8/Sonora boards.
        if (mem.cpuHeld()) mem.tick(int(kFrame));
        else runQuantumWithWire(services, mem, cpu, kFrame);
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
        // Fixed-mode LCD: 640x400, one GSC layout per frame, no beam and no
        // mode change — decodeScreen() reads VRAM whole (MscMemory.h:152), so
        // it can write the published buffer directly.
        mem.decodeScreen(out);
        for (uint32_t& px : out) px |= 0xFF000000u;
        w = MscMemory::kScreenW; h = MscMemory::kScreenH;
    }

    void publishStatus() {
        stFlags_.store(uint8_t((mem.overlay() ? 1 : 0) |
                               ((cpu.getTC() & 0x80000000) ? 2 : 0) |
                               (mem.cpuHeld() ? 4 : 0)),
                       std::memory_order_relaxed);
        stGsc_.store(mem.gscReg(4), std::memory_order_relaxed);
    }

private:
    const int64_t kFrame = mem.cpuHz() / 60;   // 33 MHz on the Duo 230

    std::atomic<uint8_t> stGsc_{0};            // GSC reg 4 = panel depth mode
};

// ── PowerBook Duo 230: MSC + PG&E, 68030 @ 33 MHz ───────────────────────
// The 37th profile and platform #12's first GUI citizen (2026-08-06;
// docs/DUO_BRINGUP.md, gate tests/duo230_boot_etalon.cpp). Selected by the
// 1 MB ROM whose header checksum is $ECFA989B — the dump shared by the
// Duo 210 / 230 / 250; only the 230 is wired (kCpuHz230 / kIdDuo230).
// No floppy drive, no NuBus, no discrete RTC: the PG&E power manager runs
// its own 68HC05 and owns the clock, the PRAM and the whole input path.
static int runDuo(std::vector<uint8_t> rom, const std::string& romName,
                  const std::vector<std::string>& media,
                  GuiHostServices& services) {
    std::printf("Machine: PowerBook Duo 230 (68030 @ 33 MHz, MSC + PG&E, "
                "GSC 640x400)\n");
    std::printf("Loaded ROM: %s (%zu KB)\n", romName.c_str(),
                rom.size() / 1024);

    // Built exactly as the gate builds it (duo230_boot_etalon.cpp:43-56).
    MscMemory& mem = services.own<MscMemory>(services.config().core(), 8u << 20,
        MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    MscCpu& cpu = services.own<MscCpu>(mem, services.config().jit().resolved,
        services.config().core().cpu, false);
    MacAudioHost& audioHost = services.own<MacAudioHost>(
        services.config().devices().audio);
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "FAIL: bad PowerBook Duo ROM\n");
        return 1;
    }
    if (!mem.pgeActive())
        std::fprintf(stderr,
                     "PG&E firmware missing (roms/pge/pge_boot.bin) — "
                     "the ROM will stall at the PMU handshake.\n");
    mem.setCpu(&cpu);

    const DuoRunnerSpec spec{
        "PowerBook Duo 230",
        "duo230",
        "hdv/System 7.5.5 HD.dsk",
        "68030 @ 33 MHz (Moira + PMMU)",
        MachineKind::Duo,
        pom68k::SnapMachine::Duo230,
    };
    return runDuoGui<MscMachine>(
        mem, cpu, audioHost, spec,
        [&] { mem.setRtcSeconds(services.hostMacSeconds()); },
        romName, media, services);
}


int pom68k::gui::composeDuo(
    pom68k::gui::PlatformLaunch launch, GuiHostServices& services) {
    return runDuo(std::move(launch.rom), launch.romName, launch.media, services);
}
