// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The GUI ↔ machine-thread contract, once.
//
// Six copies of this existed in `main.cpp` — `MacIiMachine`, `IIfxMachine`,
// `LcMachine`, `SonoraStyleMachine`, `DafbMachine`, `MscMachine`. Measured
// before extracting (2026-08-09, tree at d3bbd81): of the members they share,
// `push`, `requestInsertFloppy`, `requestEjectFloppy`, `floppyInserted`,
// `setFloppyInserted`, `latchFrame`, `cpuEngine`, `jitStats`, `start` and
// `stop` were **byte-identical across all six**, and `stepTick` was 92-100 %
// identical. Only `publish()` genuinely differed (58-65 %), because it is where
// each platform's framebuffer geometry is read.
//
// So the split is not a judgement call: everything above stays here, and each
// platform supplies the frame half plus its own quantum. The cost of a machine
// stops including a copy of the concurrency contract, which is what actually
// mattered — a defect in the queue discipline, the publish cadence or the
// thread teardown used to be fixable in one of six places.
//
// **CRTP, not a base class with virtuals.** `extern/moira` and every `*Memory`
// deliberately keep the bus path free of virtual dispatch; a host that called
// `emulateQuantum()` through a vtable 60 times a second would be harmless, but
// the same reflex applied one layer down is not, and the project's rule is
// worth keeping mechanical rather than remembered. Every `self()` call below
// resolves at compile time.
//
// Gated by `tests/machinehost_test.cpp` — the queue, the double buffer, the
// engine-swap round trip and the thread teardown, which is the part a compiler
// cannot check and the part `main.cpp` could never be tested for at all (it is
// the one translation unit outside `pom68k_core`).
//
// ── What a platform must supply ────────────────────────────────────────────
//   int64_t frameCycles() const        cycles in one emulated frame
//   void    emulateQuantum()           run one frame; bump framesRun_
//   bool    drainAudio()               pull samples into samp_; true = audible
//   void    renderFrame(std::vector<uint32_t>& out, int& w, int& h)
//                                      fill `out` with the frame to publish
//                                      (`fb_` stays the platform's own
//                                      persistent raster surface)
//   void    publishStatus()            store this platform's status atomics
//   static constexpr bool kStereo      which MacAudioHost entry points to use
// and optionally:
//   void    applyPlatformCmd(const Cmd&)   commands beyond the common set

#pragma once

#include "SaveStateSlot.h"
#include "jit/JitStats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// POM68K_KEY_TRACE=1 — stderr log of every GUI key event at PUSH (UI thread)
// and APPLY (machine thread). A freeze where pushes continue but applies stop
// is a machine-thread wedge; both stopping is GUI-side. Lives here because
// both ends of that comparison are the host contract.
inline void keyTrace(const char* where, uint8_t adb, bool down) {
    static const bool on = std::getenv("POM68K_KEY_TRACE") != nullptr;
    if (on)
        std::fprintf(stderr, "[key] %s adb=%02X %s\n", where, adb,
                     down ? "dn" : "up");
}

template <class Derived, class Mem, class Cpu, class AudioHost>
class MachineHost {
public:
    MachineHost(Mem& m, Cpu& c, AudioHost& a) : mem(m), cpu(c), audioHost(a) {
        // POM68K_CPU_ENGINE may have started us on the JIT; mirror whatever
        // the CPU actually built itself with so the menu tick is honest.
        stEngine_.store(cpu.engine(), std::memory_order_relaxed);
    }
    // Not copyable: it owns a thread and two mutexes.
    MachineHost(const MachineHost&) = delete;
    MachineHost& operator=(const MachineHost&) = delete;
    ~MachineHost() { stop(); }

    Mem& mem;
    Cpu& cpu;
    AudioHost& audioHost;

    std::atomic<bool> running{true}, turbo{true}, quit{false};

    // The union of what the platforms queue. A platform that has no CD bay
    // simply never pushes InsertBay/EjectBay; an unhandled command reaching
    // applyPlatformCmd() is a no-op by default.
    struct Cmd {
        enum T { MouseMove, MouseButton, Key, HardReset, CpuEngine,
                 InsertFloppy, EjectFloppy, InsertBay, EjectBay, Sense } t;
        int a = 0, b = 0;
    };

    void push(Cmd c) {
        std::lock_guard<std::mutex> l(cmdMu_);
        cmds_.push_back(c);
    }

    // Save-state requests (GUI → machine thread). The machine thread performs
    // them between two quanta: a restore replaces the whole tree, so it must
    // land on an instruction boundary, never mid-quantum from the GUI thread.
    SaveStateSlot state;

    // ── Floppy hot-swap (GUI → machine thread) ─────────────────────────────
    // The path travels under cmdMu_ with the command, so the machine thread
    // never reads a std::string the GUI thread is still assigning.
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

    // CD-bay media in/out — same queue discipline as the floppy.
    void requestInsertBay(int id, std::string path) {
        std::lock_guard<std::mutex> l(cmdMu_);
        bayPending_ = std::move(path);
        cmds_.push_back({Cmd::InsertBay, id});
    }
    void requestEjectBay(int id) {
        std::lock_guard<std::mutex> l(cmdMu_);
        cmds_.push_back({Cmd::EjectBay, id});
    }

    // ── Framebuffer handoff ────────────────────────────────────────────────
    // The GUI copies out under fbMu_; the machine thread writes fbShared_ at
    // the publish cadence. Returns false until the first frame exists.
    bool latchFrame(std::vector<uint32_t>& out, int& w, int& h) {
        std::lock_guard<std::mutex> l(fbMu_);
        if (fbShared_.empty()) return false;
        out = fbShared_; w = fbW_; h = fbH_;
        return true;
    }

    // Which engine the machine thread is ACTUALLY running. The menu tick must
    // follow the machine, not the click — the swap happens one queue
    // round-trip later, and a tick that led it would lie during the gap.
    int cpuEngine() const { return stEngine_.load(std::memory_order_relaxed); }

    // Lock-free-enough copy of the JIT gauges, republished at the same ~16 ms
    // cadence as the framebuffer — which is exactly what a stats window wants.
    jit::Stats::Snapshot jitStats() const {
        std::lock_guard<std::mutex> l(jitMu_);
        return jitSnap_;
    }

    // ── The thread ─────────────────────────────────────────────────────────
    void start() {
#ifndef __EMSCRIPTEN__
        th_ = std::thread([this] {
            while (!quit.load(std::memory_order_relaxed)) {
                int us = stepTick();
                if (us > 0)
                    std::this_thread::sleep_for(std::chrono::microseconds(us));
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

    // One scheduling slice. Returns how long the caller should sleep, in µs.
    // Emscripten drives this directly from its own frame callback, which is
    // why the pacing lives here and not inside start()'s lambda.
    int stepTick() {
        applyCmds();
        if (!running.load(std::memory_order_relaxed)) { publish(); return 5000; }
        int sleepUs = 0;
        if (activeHold_ > 0 && audioHost.started()) {
            // Audio-clocked: the sound buffer is the pacer, so emulate until
            // it is topped up rather than against the wall clock.
            int n = 0;
            while (audioHost.buffered() < kTarget && n < 8) {
                self()->emulateQuantum();
                if (self()->drainAudio()) activeHold_ = 90; else activeHold_--;
                pushAudioRaw();
                n++;
            }
            if (n == 0) {
                // Buffer full and staying full: the guest may have gone quiet
                // without the hold expiring. Force a quantum occasionally so a
                // silent-but-running machine still makes progress.
                if (++starve_ > 80) {
                    self()->emulateQuantum();
                    if (self()->drainAudio()) activeHold_ = 90; else activeHold_--;
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
                self()->emulateQuantum();
            } while (turbo.load(std::memory_order_relaxed) && ++n < 8 &&
                     std::chrono::steady_clock::now() - t0 <
                         std::chrono::milliseconds(10));
            if (self()->drainAudio()) {
                activeHold_ = 90;                  // sound starts: switch to
                pushAudioFrame();                  // audio-clocked pacing
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

    // Publish at most every 16 ms unless a frame was emulated since the last
    // one (`force` is for the single-threaded Emscripten path and for tests).
    void publish(bool force = false) {
        auto now = std::chrono::steady_clock::now();
        if (!force && framesRun_ == 0 &&
            now - lastPub_ < std::chrono::milliseconds(16)) return;
        lastPub_ = now; framesRun_ = 0;
        // The platform fills `fbPub_` with the frame to publish, which is NOT
        // necessarily `fb_`: on the row-granular decoders `fb_` is a raster
        // surface that persists across frames (only the rows the beam scanned
        // are repainted), so the host must never take it. Publishing by swap
        // keeps that distinction free — `fbPub_` comes back holding the
        // previous frame's storage, which the next renderFrame() overwrites
        // whole.
        int w = fbW_, h = fbH_;
        self()->renderFrame(fbPub_, w, h);
        {
            std::lock_guard<std::mutex> l(fbMu_);
            fbShared_.swap(fbPub_); fbW_ = w; fbH_ = h;
        }
        stPc_.store(cpu.getPC(), std::memory_order_relaxed);
        stClock_.store(cpu.getClock(), std::memory_order_relaxed);
        self()->publishStatus();
        {
            std::lock_guard<std::mutex> l(jitMu_);
            jitSnap_ = cpu.jit().stats().snapshot();
        }
    }

protected:
    Derived* self() { return static_cast<Derived*>(this); }
    const Derived* self() const { return static_cast<const Derived*>(this); }

    // ~100 ms of 22 257 Hz sound. The same on every platform — the figure is a
    // property of the host audio ring, not of the guest.
    static constexpr size_t kTarget = 2225;

    void pushAudioRaw() {
        if constexpr (Derived::kStereo) audioHost.pushRawStereo(samp_, 0);
        else                            audioHost.pushRaw(samp_, 0);
    }
    void pushAudioFrame() {
        if constexpr (Derived::kStereo) audioHost.pushFrameStereo(samp_, 0);
        else                            audioHost.pushFrame(samp_, 0);
    }

    // Everything the GUI queued, applied between two quanta. Every arm is
    // identical on all six platforms because every `*Memory` exposes the same
    // input surface; the media hooks that only some of them carry are selected
    // by `requires` rather than by a per-platform override, so a machine that
    // has no CD bay compiles the same template and simply has no arm.
    void applyCmds() {
        // Take the pending paths out UNDER the lock: the GUI thread reassigns
        // floppyPending_ (under cmdMu_) while this thread would otherwise be
        // reading it by const& through the milliseconds of file I/O inside
        // insertDisk() — a use-after-free on the second pick from the Disques
        // menu. Found once, in one of the six copies.
        std::string pending, bayPending;
        {
            std::lock_guard<std::mutex> l(cmdMu_);
            cmdsApply_.swap(cmds_);
            pending.swap(floppyPending_);
            bayPending.swap(bayPending_);
        }
        for (const Cmd& c : cmdsApply_) switch (c.t) {
            case Cmd::MouseMove:   mem.mouseMove(c.a, c.b); break;
            // The Duo's PMU takes the button alone; every other platform also
            // takes the ADB address it should be reported on.
            case Cmd::MouseButton:
                if constexpr (requires { mem.mouseButton(true, 0); })
                    mem.mouseButton(c.b != 0, c.a);
                else
                    mem.mouseButton(c.b != 0);
                break;
            case Cmd::Key:
                keyTrace("apply", uint8_t(c.a), c.b != 0);
                mem.keyEvent(uint8_t(c.a), c.b != 0);
                break;
            case Cmd::HardReset:   cpu.hardReset(); break;
            // The Duo 230 has no floppy drive at all, so it has no insertDisk;
            // the GUI simply never offers it the menu entry.
            case Cmd::InsertFloppy:
                if constexpr (requires { mem.insertDisk(pending); }) {
                    if (!pending.empty() && mem.insertDisk(pending))
                        floppyFlag_.store(true, std::memory_order_relaxed);
                }
                break;
            case Cmd::EjectFloppy:
                if constexpr (requires { mem.ejectDisk(); }) {
                    mem.ejectDisk();
                    floppyFlag_.store(false, std::memory_order_relaxed);
                }
                break;
            case Cmd::InsertBay:
                if constexpr (requires { mem.insertBayMedia(1, bayPending); }) {
                    if (!bayPending.empty()) mem.insertBayMedia(c.a, bayPending);
                }
                break;
            case Cmd::EjectBay:
                if constexpr (requires { mem.ejectBayMedia(1); })
                    mem.ejectBayMedia(c.a);
                break;
            // Monitor sense (V8/Sonora/VASP/RBV): a multi-field update inside
            // the memory object, so it is applied on THIS thread and crosses
            // back to the GUI as an atomic — the GUI never reaches into mem.
            // The ROM only reads the sense lines at reset, hence the reset.
            case Cmd::Sense:
                if constexpr (requires { mem.setMonitorSense(uint8_t(0)); }) {
                    mem.setMonitorSense(uint8_t(c.a));
                    cpu.hardReset();
                }
                break;
            // Engine swap. applyCmds() is the first statement of stepTick(),
            // i.e. strictly between two calls to runCycles() — so the swap
            // always lands on an instruction boundary and needs no lock.
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
    std::string floppyPending_, bayPending_;      // guarded by cmdMu_
    std::atomic<bool> floppyFlag_{false};

    std::mutex fbMu_;
    std::vector<uint32_t> fbShared_;
    int fbW_ = 0, fbH_ = 0;
    std::vector<uint32_t> fb_;      // the platform's persistent raster surface
    std::vector<uint32_t> fbPub_;   // what publish() hands to the GUI

    std::atomic<uint32_t> stPc_{0};
    std::atomic<long long> stClock_{0};
    std::atomic<uint8_t> stFlags_{0};
    std::atomic<int> stEngine_{0};                // 0 = interpreter, 1 = JIT

    mutable std::mutex jitMu_;
    jit::Stats::Snapshot jitSnap_{};

    int activeHold_ = 0;
    int starve_ = 0;
    int framesRun_ = 0;
    std::chrono::steady_clock::time_point lastPub_{};
    std::vector<float> samp_;
};
