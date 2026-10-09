// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── DebugSession: the debugger at the machine ownership boundary ──
// The GUI posts typed commands and reads immutable snapshots; the machine
// thread owns every mutation. Two places serve the commands:
//
//   atBoundary()  between two quanta (MachineHost::stepTick). A Pause stops
//                 HERE: every quantum ends on an instruction boundary, so
//                 this is an architectural stop that needs no CPU support
//                 and also covers a CPU sitting in STOP or a held bus.
//                 Breakpoint edits made while the machine runs land here,
//                 at most one quantum after they were posted.
//   onCpuStop()   inside a quantum, from the CPU's stop hook, after an
//                 instruction retired and before the next one starts. The
//                 machine thread waits here, serving inspection and
//                 breakpoint commands, until Continue/Step (or shutdown).
//                 It does not unwind the quantum: the platform's frame loop
//                 (vblank edges, beam slices) resumes exactly where the CPU
//                 stopped, so a stop is invisible to guest timing.
//
// Watchpoints and exception stops are noticed mid-instruction by the CPU
// adapter, which only arms a soft stop there; the stop itself is delivered
// through onCpuStop() at the end of that instruction (or exception entry),
// so every stop the user sees is at an instruction boundary.
//
// Acknowledgement: a command is applied when `Snapshot::acked` reaches its
// id. "Paused" means `stopped` in a snapshot whose `acked` covers the
// Pause — the CPU is then at an instruction boundary, not merely flagged.
//
// Edits (SetRegister, WriteMemory) are applied only while stopped, at
// either place, and refused with a message otherwise or when a Continue or
// Step earlier in the same batch has already released the stop. Their
// semantics belong to the Target (DebugTypes.h).
//
// What waits while a stop holds the machine inside a quantum: reset, state
// save/load, engine swaps and input (MachineHost applies them between
// quanta). The snapshot's `inQuantum` says so to the user.
//
// Gate: tests/debug_session_test.cpp.

#pragma once
#include "DebugTypes.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace pom68k::dbg {

class Session {
public:
    // ── GUI side (any thread) ───────────────────────────────────────────
    std::uint64_t post(Command c);
    std::shared_ptr<const Snapshot> snapshot() const;
    // Set by the machine thread when Continue/Step must run a machine the
    // dashboard had paused; MachineHost clears its own pause on it.
    bool takeResumeRequest() {
        return resumeRequest_.exchange(false, std::memory_order_acq_rel);
    }

    // The session identity a history export carries (profile, ROM,
    // media…), supplied once by the runner before the machine starts.
    void setIdentity(std::vector<std::pair<std::string, std::string>> notes) {
        std::lock_guard<std::mutex> l(mu_);
        identity_ = std::move(notes);
    }

    // Builds without a second thread (Emscripten) cannot hold the machine
    // inside a quantum: breakpoints and steps are refused there.
    void setBlockingAvailable(bool on) { blockingAvailable_ = on; }

    // Releases a stop held inside a quantum and refuses further holds.
    // Called before the machine thread is joined.
    void shutdown();

    // ── Machine thread ──────────────────────────────────────────────────
    // True: hold this tick (the machine is stopped at the boundary).
    bool atBoundary(Target& target);
    // Blocks until the GUI resumes; returns at once after shutdown().
    // `reason` is Step, Breakpoint, Watchpoint or Exception; `detail`
    // describes the last two.
    void onCpuStop(Target& target, StopReason reason, std::uint32_t pc,
                   const StopDetail& detail = {});

private:
    struct Applied {
        bool changed = false, resume = false, step = false;
        Command::Kind run = Command::Kind::Pause;   // StepOver/StepOut, or none
    };
    Applied apply(Target& target, std::deque<Command>& batch, bool inQuantum);
    void applyEdit(Target& target, const Command& c);
    void publish(Target& target);

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Command> queue_;              // under mu_
    std::uint64_t nextId_ = 1;               // under mu_
    bool shutdown_ = false;                  // under mu_
    std::vector<std::pair<std::string, std::string>> identity_;  // under mu_
    std::shared_ptr<const Snapshot> snap_ =  // under mu_
        std::make_shared<const Snapshot>();
    std::atomic<bool> resumeRequest_{false};
    bool blockingAvailable_ = true;

    // Machine-thread state.
    bool stopped_ = false;
    bool resumePending_ = false;             // a Continue/Step earlier in this batch
    bool inQuantum_ = false;
    StopReason reason_ = StopReason::None;
    StopDetail detail_;
    std::uint64_t acked_ = 0;
    std::uint64_t generation_ = 0;
    std::string message_;
    MemoryView view_;                        // what the GUI asked to watch
    std::uint32_t viewLength_ = 0;
    bool disasmFollowPc_ = true;
    std::uint32_t disasmAddr_ = 0;
    // Last published CPU position, so a reset or a state load applied while
    // stopped at the boundary republishes the registers.
    std::int64_t lastClock_ = -1;
    std::uint32_t lastPc_ = 0;
    // A soft stop and a breakpoint can match the same instruction: Moira
    // evaluates both after one retirement. One stop, not two.
    std::int64_t lastStopClock_ = -1;
    std::uint32_t lastStopPc_ = 0;
};

} // namespace pom68k::dbg
