// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// DebugSession — see DebugSession.h for the threading contract.

#include "DebugSession.h"

#include <algorithm>
#include <utility>

namespace pom68k::dbg {

namespace {
// Disassembly lines per snapshot when the GUI has not asked otherwise.
constexpr int kDisasmLines = 24;
// The GUI posts a handful of commands per click; anything beyond this is a
// runaway caller, refused rather than queued without bound.
constexpr std::size_t kMaxQueued = 1024;
} // namespace

std::uint64_t Session::post(Command c) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (queue_.size() >= kMaxQueued) return 0;
        c.id = nextId_++;
        queue_.push_back(c);
    }
    cv_.notify_all();
    return c.id;
}

std::shared_ptr<const Snapshot> Session::snapshot() const {
    std::lock_guard<std::mutex> l(mu_);
    return snap_;
}

void Session::shutdown() {
    {
        std::lock_guard<std::mutex> l(mu_);
        shutdown_ = true;
    }
    cv_.notify_all();
}

Session::Applied Session::apply(Target& target, std::deque<Command>& batch,
                                bool inQuantum) {
    Applied a;
    if (!batch.empty()) message_.clear();   // the message answers the last batch
    for (const Command& c : batch) {
        a.changed = true;
        acked_ = std::max(acked_, c.id);
        switch (c.kind) {
        case Command::Kind::Pause:
            if (!stopped_) {
                stopped_ = true;
                inQuantum_ = inQuantum;
                reason_ = StopReason::Pause;
            }
            break;
        case Command::Kind::Continue:
            if (stopped_) a.resume = resumePending_ = true;
            break;
        case Command::Kind::Step:
            if (!blockingAvailable_)
                message_ = "Pas à pas indisponible dans cette version";
            else if (!stopped_)
                message_ = "Pas à pas : arrêter d'abord la machine";
            else
                a.step = a.resume = resumePending_ = true;
            break;
        case Command::Kind::AddBreakpoint:
            if (!blockingAvailable_)
                message_ = "Points d'arrêt indisponibles dans cette version";
            else if (target.breakpoints().size() >= kMaxBreakpoints)
                message_ = "Trop de points d'arrêt";
            else
                target.addBreakpoint(c.addr);
            break;
        case Command::Kind::RemoveBreakpoint:
            target.removeBreakpoint(c.addr);
            break;
        case Command::Kind::ClearBreakpoints:
            target.clearBreakpoints();
            break;
        case Command::Kind::ViewMemory:
            view_.space = c.space;
            view_.addr = c.addr;
            viewLength_ = std::min(c.length, kMaxMemoryBytes);
            if (c.length > kMaxMemoryBytes)
                message_ = "Fenêtre mémoire limitée à 4096 octets";
            break;
        case Command::Kind::ViewDisasm:
            disasmFollowPc_ = c.followPc;
            disasmAddr_ = c.addr;
            break;
        case Command::Kind::AddWatchpoint:
        case Command::Kind::AddCatch: {
            std::string why;
            if (!blockingAvailable_)
                message_ = "Arrêts indisponibles dans cette version";
            else if (!(c.kind == Command::Kind::AddWatchpoint
                           ? target.addWatchpoint(c.watch, why)
                           : target.addCatch(c.catchpoint, why)))
                message_ = why;
            break;
        }
        case Command::Kind::RemoveWatchpoint:
            target.removeWatchpoint(c.addr);
            break;
        case Command::Kind::ClearWatchpoints:
            target.clearWatchpoints();
            break;
        case Command::Kind::RemoveCatch:
            target.removeCatch(c.catchpoint);
            break;
        case Command::Kind::ClearCatches:
            target.clearCatches();
            break;
        case Command::Kind::SetRegister:
        case Command::Kind::WriteMemory:
            applyEdit(target, c);
            break;
        }
    }
    // Breakpoint edits recompute Moira's CHECK_BP from the list alone, which
    // would drop a soft stop armed before them: arm the step last.
    if (a.step) target.armStep();
    resumePending_ = false;
    if (a.resume) {
        stopped_ = false;
        inQuantum_ = false;
        reason_ = StopReason::None;
        resumeRequest_.store(true, std::memory_order_release);
    }
    return a;
}

// Edits are defined only at a stop: a running machine has no instruction
// boundary the GUI could have meant. A stop that a Continue in the same
// batch released no longer counts (`resume` is applied after the loop, so
// test the edit against the order the commands were posted in).
void Session::applyEdit(Target& target, const Command& c) {
    std::string why;
    if (!stopped_ || resumePending_) {
        message_ = "Modification refusée : arrêter d'abord la machine";
        return;
    }
    const bool ok = c.kind == Command::Kind::SetRegister
        ? (c.reg < Reg::Count && target.setRegister(c.reg, c.value, why))
        : (c.data.size() <= kMaxEditBytes &&
           target.writeMemory(c.space, c.addr, c.data.data(), c.data.size(), why));
    if (!ok) message_ = why.empty() ? "Modification refusée" : why;
}

bool Session::atBoundary(Target& target) {
    std::deque<Command> batch;
    {
        std::lock_guard<std::mutex> l(mu_);
        batch.swap(queue_);
    }
    const Applied a = apply(target, batch, false);
    // A reset or a state load applied while stopped moves the CPU without a
    // command of ours: the snapshot must follow it.
    const bool moved = stopped_ &&
        (target.clock() != lastClock_ || target.pc() != lastPc_);
    if (a.changed || moved) publish(target);
    return stopped_;
}

void Session::onCpuStop(Target& target, StopReason reason, std::uint32_t pc,
                        const StopDetail& detail) {
    if (!blockingAvailable_) return;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (shutdown_) return;
    }
    const std::int64_t clock = target.clock();
    if (reason == StopReason::Breakpoint && clock == lastStopClock_ &&
        pc == lastStopPc_)
        return;
    lastStopClock_ = clock;
    lastStopPc_ = pc;
    stopped_ = true;
    inQuantum_ = true;
    reason_ = reason;
    detail_ = detail;
    publish(target);
    for (;;) {
        std::deque<Command> batch;
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait(l, [this] { return shutdown_ || !queue_.empty(); });
            if (shutdown_) {
                stopped_ = false;
                inQuantum_ = false;
                reason_ = StopReason::None;
                return;
            }
            batch.swap(queue_);
        }
        const Applied a = apply(target, batch, true);
        if (a.changed) publish(target);
        if (a.resume) return;
    }
}

void Session::publish(Target& target) {
    Snapshot s;
    s.generation = ++generation_;
    s.acked = acked_;
    s.available = blockingAvailable_;
    s.stopped = stopped_;
    s.inQuantum = stopped_ && inQuantum_;
    s.reason = stopped_ ? reason_ : StopReason::None;
    if (s.reason == StopReason::Watchpoint || s.reason == StopReason::Exception)
        s.detail = detail_;
    s.message = message_;
    target.capture(s);
    s.breakpoints = target.breakpoints();
    std::sort(s.breakpoints.begin(), s.breakpoints.end());
    s.watchpoints = target.watchpoints();
    s.catches = target.catches();

    // Both windows are read at an instruction boundary on the machine
    // thread, running or not: between quanta nothing else is executing.
    std::uint32_t at = disasmFollowPc_ ? s.regs.pc : disasmAddr_;
    for (int i = 0; i < kDisasmLines; ++i) {
        DisasmLine line = target.disassemble(at);
        line.breakpoint = std::binary_search(s.breakpoints.begin(),
                                             s.breakpoints.end(), at);
        at += line.length;
        s.disasm.push_back(std::move(line));
    }
    if (viewLength_) {
        s.memory.space = view_.space;
        s.memory.addr = view_.addr;
        s.memory.bytes.resize(viewLength_);
        s.memory.state.resize(viewLength_);
        target.readMemory(view_.space, view_.addr, s.memory.bytes.data(),
                          s.memory.state.data(), viewLength_);
    }
    lastClock_ = target.clock();
    lastPc_ = target.pc();

    auto shared = std::make_shared<const Snapshot>(std::move(s));
    std::lock_guard<std::mutex> l(mu_);
    snap_ = std::move(shared);
}

} // namespace pom68k::dbg
