// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate for the GUI ↔ machine-thread contract (`src/MachineHost.h`).
//
// This contract had six copies in `main.cpp` and **zero gates**, for a
// structural reason: `main.cpp` is the only translation unit outside
// `pom68k_core`, so nothing living there can be linked by a test. That is why
// the contract was extracted before it was unified — a defect in the queue
// discipline, the publish cadence or the thread teardown was previously
// fixable in one of six places and observable in none.
//
// Real `Q605Memory` + `Cpu040` (the save-state slot instantiates
// `pom68k::save/load`, so fakes would not compile, and real types are what the
// GUI actually runs). The audio host IS a fake, because it is a template
// parameter and because both pacing branches have to be reachable on demand —
// the audio-clocked one never runs under a silent test otherwise.
//
// No ROM, no disk image: nothing here boots. What is gated is ordering,
// visibility and teardown.

#include "Cpu040.h"
#include "GuiSpeedGauge.h"
#include "MachineHost.h"
#include "Q605Memory.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) gFails++;
}

namespace {

// Counts what the host asked of it, and lets the test choose whether the
// machine is "audible" and whether the ring is full.
struct FakeAudio {
    bool startedFlag = false;
    size_t bufferedCount = 0;
    int raw = 0, frame = 0, rawStereo = 0, frameStereo = 0;

    bool started() const { return startedFlag; }
    size_t buffered() const { return bufferedCount; }
    void pushRaw(std::vector<float>&, int) { raw++; }
    void pushFrame(std::vector<float>&, int) { frame++; }
    void pushRawStereo(std::vector<float>&, int) { rawStereo++; }
    void pushFrameStereo(std::vector<float>&, int) { frameStereo++; }
};

// A platform that emulates nothing: the contract under test is the host's,
// and a quantum that really ran the 68040 would make the timing branches
// depend on how fast this machine is.
template <bool Stereo>
struct TestMachine
    : MachineHost<TestMachine<Stereo>, Q605Memory, Cpu040, FakeAudio> {
    using Base = MachineHost<TestMachine<Stereo>, Q605Memory, Cpu040, FakeAudio>;
    using Base::Base;
    using typename Base::Cmd;

    static constexpr bool kStereo = Stereo;

    int quanta = 0, renders = 0, statusPublishes = 0;
    bool audible = false;
    int frameW = 64, frameH = 32;

    int64_t frameCycles() const { return 1000; }
    void emulateQuantum() { quanta++; this->framesRun_++; }
    bool drainAudio() { return audible; }
    void renderFrame(std::vector<uint32_t>& fb, int& w, int& h) {
        renders++;
        w = frameW; h = frameH;
        fb.assign(size_t(w) * h, 0xFF112233u);
    }
    void publishStatus() { statusPublishes++; }

    // The pacing state is protected in the host; the test drives it directly
    // to reach the audio-clocked branch without a real sound device.
    void forceAudioClocked() { this->activeHold_ = 90; }
    int engineAtomic() const { return this->cpuEngine(); }
};

using MonoMachine = TestMachine<false>;
using StereoMachine = TestMachine<true>;

}  // namespace

int main() {
    {
        pom68k::RealtimeGauge gauge;
        auto r = gauge.observe(1000, 1000, 10.0);
        check(!r.updated && r.ratio == 0.0,
              "the GUI speed gauge arms without inventing a first sample");
        r = gauge.observe(1250, 1000, 10.25);
        check(!r.updated && r.ratio == 0.0,
              "the GUI speed gauge waits for a half-second window");
        r = gauge.observe(2000, 1000, 11.0);
        check(r.updated && r.ratio == 1.0,
              "the GUI speed gauge reports exact real-time progress");
        r = gauge.observe(100, 1000, 11.1);
        check(!r.updated && r.ratio == 0.0,
              "a machine-clock reset re-arms the GUI speed gauge");
    }

    static Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    static Cpu040 cpu(mem, jit::defaultResolvedConfig(),
                      pom68k::defaultCoreConfig().cpu,
                      pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    FakeAudio audio;

    {
        MonoMachine m(mem, cpu, audio);
        check(!m.turbo.load(std::memory_order_relaxed),
              "GUI fast-forward starts disabled so double-click timing is preserved");

        // ── The framebuffer is not visible before the first publish ────────
        std::vector<uint32_t> out;
        int w = 0, h = 0;
        check(!m.latchFrame(out, w, h),
              "latchFrame refuses before the first frame exists");

        m.publish(true);
        check(m.renders == 1, "publish(force) renders exactly one frame");
        check(m.machineHz() == mem.cpuHz(),
              "the GUI speed gauge uses the platform's real machine rate");
        check(m.machineClock() == cpu.machineClock(),
              "publish exposes real machine cycles, not boosted core cycles");
        check(m.latchFrame(out, w, h), "latchFrame succeeds after publish");
        check(w == 64 && h == 32 && out.size() == 64 * 32,
              "the latched frame carries the platform's geometry");
        check(out[0] == 0xFF112233u, "the latched frame carries its pixels");

        // A latch is a COPY: the GUI must be able to hold it while the
        // machine thread renders the next one.
        m.frameW = 16; m.frameH = 8;
        m.publish(true);
        check(out.size() == 64 * 32 && out[0] == 0xFF112233u,
              "an already-latched frame is not mutated by the next publish");
        std::vector<uint32_t> out2;
        int w2 = 0, h2 = 0;
        check(m.latchFrame(out2, w2, h2) && w2 == 16 && h2 == 8,
              "the next latch sees the new geometry");
        m.frameW = 64; m.frameH = 32;

        // ── publish() is rate-limited unless a frame was emulated ─────────
        const int before = m.renders;
        m.publish();
        check(m.renders == before,
              "publish() inside the 16 ms window with no frame run is a no-op");
        m.emulateQuantum();
        m.publish();
        check(m.renders == before + 1,
              "a frame emulated since the last publish forces one through");

        // ── The engine tick follows the MACHINE, not the click ────────────
        // This is the invariant the CPU menu is built on: between the click
        // and the machine thread applying it, the tick must still read the
        // engine that is actually running.
        check(m.cpuEngine() == 1, "a 68040 starts on the accelerated engine");
        m.push({MonoMachine::Cmd::CpuEngine, 0});
        check(m.cpuEngine() == 1,
              "cpuEngine() does NOT follow the click before the queue drains");
        m.stepTick();
        check(m.cpuEngine() == 0, "cpuEngine() follows the machine after apply");
        check(cpu.engine() == 0, "the CPU really swapped to the reference engine");

        // ── The queue is FIFO ─────────────────────────────────────────────
        // Two conflicting commands in one batch: the LAST one must win, or a
        // rapid click pair would leave the machine on the wrong engine.
        m.push({MonoMachine::Cmd::CpuEngine, 0});
        m.push({MonoMachine::Cmd::CpuEngine, 1});
        m.push({MonoMachine::Cmd::CpuEngine, 0});
        m.stepTick();
        check(m.cpuEngine() == 0 && cpu.engine() == 0,
              "a batch of conflicting commands applies in order, last wins");

        // ── Floppy flag round trip ────────────────────────────────────────
        m.setFloppyInserted(true);
        check(m.floppyInserted(), "setFloppyInserted is visible to the GUI");
        m.requestEjectFloppy();
        m.stepTick();
        check(!m.floppyInserted(), "EjectFloppy clears the flag on apply");
        // An insert whose path does not exist must NOT claim a disk.
        m.requestInsertFloppy("hdv/definitely-not-here.dsk");
        m.stepTick();
        check(!m.floppyInserted(),
              "InsertFloppy of a missing path leaves the flag clear");

        // Payload belongs to the command, not to the batch. The former
        // single floppyPending_ slot made both commands below use the second
        // path, so the valid first insert vanished entirely.
        const std::string fifoDisk = "/tmp/pom68k_machinehost_fifo.dsk";
        {
            std::ofstream f(fifoDisk, std::ios::binary | std::ios::trunc);
            f.seekp(409600 - 1); f.put('\0');
        }
        m.requestInsertFloppy(fifoDisk);
        m.requestInsertFloppy("/tmp/pom68k_machinehost_missing.dsk");
        m.stepTick();
        check(m.floppyInserted() && mem.internalDrive().hasDisk(),
              "each batched floppy command keeps its own path");
        check(m.floppyPath() == fifoDisk,
              "failed later insert keeps the successful media path");
        m.requestEjectFloppy();
        m.stepTick();
        check(m.floppyPath().empty(), "eject clears the published media path");

        // ── The GUEST ejects, and the GUI has to hear about it ───────────
        // The command queue is one direction only. A Finder "Ranger" reaches
        // `SonyDrive::eject()` without passing through it, and until
        // 2026-08-15 nothing published that: the Disques window kept naming
        // a disk the drive no longer had, with an « Éjecter » button for it.
        // The drive is the authority; `publish()` takes its answer.
        m.requestInsertFloppy(fifoDisk);
        m.stepTick();
        check(m.floppyInserted() && m.floppyPath() == fifoDisk,
              "a disk is in the drive and named");
        mem.internalDrive().eject();                 // the guest's own eject
        m.stepTick();
        check(!m.floppyInserted(),
              "a guest-side eject clears the published floppy flag");
        check(m.floppyPath().empty(),
              "a guest-side eject clears the published media path");

        // ── Bay commands are accepted and stay in their lane ──────────────
        // Q605Memory carries insertBayMedia/ejectBayMedia, so these arms are
        // compiled in here; a platform without them compiles the same
        // template with no arm at all (the `requires` in applyCmds()).
        // The disk is inserted for real rather than with `setFloppyInserted`:
        // since publish() mirrors the drive, a flag set behind the drive's
        // back is corrected on the next tick — which is the point of the
        // change, and would make a fake here fail for the right reason.
        m.requestInsertFloppy(fifoDisk);
        m.requestInsertBay(3, "hdv/definitely-not-here.iso");
        m.requestEjectBay(3);
        m.stepTick();
        check(m.floppyInserted(),
              "a bay command does not disturb the floppy flag");
        m.requestEjectFloppy();
        m.stepTick();
        std::remove(fifoDisk.c_str());

        // ── Pausing still publishes ──────────────────────────────────────
        // A paused machine must keep the window painted, or « pause » looks
        // like a freeze.
        m.running.store(false);
        const int r0 = m.renders, q0 = m.quanta;
        const int sleepUs = m.stepTick();
        check(m.quanta == q0, "a paused machine emulates nothing");
        check(m.renders == r0 + 1 || sleepUs == 5000,
              "a paused machine still services the queue and sleeps");
        m.running.store(true);

        // ── The time-budgeted branch runs quanta ─────────────────────────
        const int q1 = m.quanta;
        const int pacedSleep = m.stepTick();
        check(m.quanta == q1 + 1 && pacedSleep > 0,
              "default pacing runs one guest frame and requests a real-time sleep");

        const int q2 = m.quanta;
        m.turbo.store(true);
        m.stepTick();
        check(m.quanta > q2, "the fast-forward branch emulates at least one quantum");
        check(audio.frame == 0 && audio.raw == 0,
              "a silent machine pushes no audio");

        // ── The audio-clocked branch ─────────────────────────────────────
        m.audible = true;
        m.stepTick();
        check(audio.frame == 1,
              "the first audible drain pushes a frame and arms audio pacing");
        audio.startedFlag = true;
        audio.bufferedCount = 0;
        m.forceAudioClocked();
        const int rawBefore = audio.raw;
        m.stepTick();
        check(audio.raw > rawBefore,
              "with the device started, pacing switches to pushRaw()");
        check(audio.rawStereo == 0 && audio.frameStereo == 0,
              "a mono platform never reaches the stereo entry points");
    }

    // ── Stereo platforms take the other entry points ─────────────────────
    {
        FakeAudio stereoAudio;
        StereoMachine s(mem, cpu, stereoAudio);
        s.audible = true;
        s.stepTick();
        check(stereoAudio.frameStereo == 1 && stereoAudio.frame == 0,
              "a stereo platform uses pushFrameStereo()");
        stereoAudio.startedFlag = true;
        s.forceAudioClocked();
        s.stepTick();
        check(stereoAudio.rawStereo > 0 && stereoAudio.raw == 0,
              "a stereo platform uses pushRawStereo()");
    }

    // ── The save-state slot is wired and refuses politely ─────────────────
    {
        MonoMachine m(mem, cpu, audio);
        m.state.request(false);                    // save, profile not wired
        m.stepTick();
        check(m.state.message().find("non câblé") != std::string::npos,
              "an unwired save-state profile is refused, not crashed into");
    }

    // ── Thread lifecycle ─────────────────────────────────────────────────
    // The destructor must join. A joinable std::thread destroyed unjoined
    // calls std::terminate, which is how this class of bug announces itself.
    {
        MonoMachine m(mem, cpu, audio);
        m.start();
        // Let the thread take at least one slice.
        for (int i = 0; i < 200 && m.quanta == 0; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(m.quanta > 0, "the machine thread runs quanta of its own accord");
        m.stop();
        check(true, "stop() joins without hanging");
        m.stop();
        check(true, "stop() is idempotent");
    }
    check(true, "destruction after an explicit stop() does not terminate");

    {
        MonoMachine m(mem, cpu, audio);
        m.start();
    }
    check(true, "destruction WITHOUT an explicit stop() joins the thread");

    std::printf("%s\n", gFails ? "FAILED" : "PASS");
    return gFails ? 1 : 0;
}
