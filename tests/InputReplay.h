// POM68K — replay side of the input journal (src/InputJournal.h).
//
// Drives a machine through a recorded session: apply every event whose
// machine-clock stamp has been reached, then emulate one quantum, until the
// journal's end clock. Events were recorded at quantum boundaries, so as
// long as the replayed machine starts from the journal's snapshot and the
// quantum reproduces the session's (same frameCycles, same held-CPU
// handling, no external wire), each event lands at the SAME boundary and
// the run is bit-deterministic — that is what `input_journal_test` gates.
//
// The application arms mirror MachineHost::applyCmds() one for one,
// including the single-button fold for platforms whose mouseButton takes no
// ADB address. StateRestore stops the replay: a mid-session GUI restore
// invalidates everything after it.

#pragma once

#include "InputJournal.h"

#include <cstdint>

// Per-run applier: carries the host-button fold state that
// MachineHost::applyCmds() keeps per machine.
template <class Mem, class Cpu>
struct InputReplayer {
    bool hostBtn[2] = {false, false};

    // Returns false on an event replay cannot honour.
    bool apply(Mem& mem, Cpu& cpu, const pom68k::InputEvent& e) {
        using ET = pom68k::InputEventType;
        switch (ET(e.type)) {
        case ET::MouseMove:
            mem.mouseMove(e.a, e.b);
            return true;
        case ET::MouseButton:
            if constexpr (requires { mem.mouseButton(true, 0); }) {
                mem.mouseButton(e.b != 0, e.a);
            } else {
                if (e.a >= 0 && e.a < 2) hostBtn[e.a] = (e.b != 0);
                mem.mouseButton(hostBtn[0] || hostBtn[1]);
            }
            return true;
        case ET::Key:
            mem.keyEvent(uint8_t(e.a), e.b != 0);
            return true;
        case ET::HardReset:
            cpu.hardReset();
            return true;
        case ET::CpuEngine:
            cpu.setEngine(e.a);
            return true;
        case ET::InsertFloppy:
            if constexpr (requires { mem.insertDisk(e.path); })
                if (!e.path.empty()) mem.insertDisk(e.path);
            return true;
        case ET::EjectFloppy:
            if constexpr (requires { mem.ejectDisk(); }) mem.ejectDisk();
            return true;
        case ET::InsertBay:
            if constexpr (requires { mem.insertBayMedia(1, e.path); })
                if (!e.path.empty()) mem.insertBayMedia(e.a, e.path);
            return true;
        case ET::EjectBay:
            if constexpr (requires { mem.ejectBayMedia(1); })
                mem.ejectBayMedia(e.a);
            return true;
        case ET::Sense:
            if constexpr (requires { mem.setMonitorSense(uint8_t(0)); }) {
                mem.setMonitorSense(uint8_t(e.a));
                cpu.hardReset();
            }
            return true;
        case ET::StateRestore:
            return false;
        }
        return false;
    }
};

// `quantum()` emulates one frame the way the recorded session did and
// returns the machine clock. `maxQuanta` bounds a machine that stops
// advancing its clock (a held CPU that never releases). Returns true when
// the whole journal replayed to its end clock.
template <class Mem, class Cpu, class Quantum>
bool replayJournal(Mem& mem, Cpu& cpu, const pom68k::InputJournal& j,
                   Quantum&& quantum, long long maxQuanta = 1'000'000'000) {
    InputReplayer<Mem, Cpu> rep;
    size_t i = 0;
    long long clk = cpu.machineClock();
    auto applyDue = [&](long long now) {
        while (i < j.events.size() && j.events[i].clk <= now) {
            if (!rep.apply(mem, cpu, j.events[i])) return false;
            ++i;
        }
        return true;
    };
    long long stalls = 0;
    for (long long n = 0; clk < j.endClk && n < maxQuanta; ++n) {
        if (!applyDue(clk)) return false;
        const long long before = clk;
        clk = quantum();
        if (clk == before) { if (++stalls > 1'000'000) return false; }
        else stalls = 0;
    }
    if (!applyDue(clk)) return false;
    return clk >= j.endClk && i == j.events.size();
}
