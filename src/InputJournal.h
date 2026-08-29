// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The input journal: a session's user input as (machine clock, command)
// pairs, in a versioned text file, so the SAME session can be replayed
// bit-deterministically and timed. The write side taps
// MachineHost::applyCmds() — the one seam every GUI input crosses — at the
// moment a command is applied, which is always a quantum boundary; replay
// applies each event at the first quantum boundary whose machine clock has
// reached the recorded stamp, which by induction is the SAME boundary
// (`lcii_savestate_etalon` is the determinism proof this rides on:
// frame-boundary injection at identical machine times reproduces the run).
//
// A journal is paired with the snapshot taken when recording was armed
// (`<journal>.pomss`), so replay is restore-then-inject and never depends on
// host wall clock — the RTC seed the GUI applied at launch is INSIDE the
// snapshot. What replay cannot reproduce is traffic from outside the
// process: a session recorded with the LToUDP cable or the AppleTalk hub
// active carries a `network 1` note so a replay harness can refuse loudly
// instead of diverging silently.
//
// Format (line-based, ASCII, one record per line):
//   # POM68K input journal v1
//   <key> <value…>            free notes (profile, cpuhz, snapshot, media…)
//   start <clk>               machine clock when recording began
//   ev <clk> <name> <a> <b> [<path…>]
//   end <clk>                 machine clock when recording stopped
// A file without `end` is a session that did not stop cleanly; the reader
// keeps it usable (complete = false, endClk = last event's clock).

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace pom68k {

// The first ten values MIRROR MachineHost::Cmd::T in order — the recording
// tap casts the enum straight through. `machinehost_test` pins the pairing
// by name, so a re-order there fails a gate instead of silently breaking
// every recorded journal.
enum class InputEventType : int {
    MouseMove, MouseButton, Key, HardReset, CpuEngine,
    InsertFloppy, EjectFloppy, InsertBay, EjectBay, Sense,
    StateRestore,   // journal marker (GUI restored a state) — never queued
};

inline constexpr const char* kInputEventNames[] = {
    "mousemove", "mousebutton", "key", "hardreset", "cpuengine",
    "insertfloppy", "ejectfloppy", "insertbay", "ejectbay", "sense",
    "staterestore",
};
inline constexpr int kInputEventTypeCount =
    int(sizeof kInputEventNames / sizeof kInputEventNames[0]);

inline const char* inputEventName(int type) {
    return (type >= 0 && type < kInputEventTypeCount)
        ? kInputEventNames[type] : "unknown";
}
inline int inputEventTypeFromName(const std::string& name) {
    for (int i = 0; i < kInputEventTypeCount; i++)
        if (name == kInputEventNames[i]) return i;
    return -1;
}

struct InputEvent {
    long long clk = 0;
    int type = 0;
    int a = 0, b = 0;
    std::string path;   // media commands only
};

// Streaming writer. Every event line is flushed so a session that crashes
// still leaves a readable journal (it merely lacks `end`). The writer is
// driven from the machine thread by MachineHost and must outlive the host's
// stop(); ownership stays with whoever armed the recording.
class InputJournalWriter {
public:
    InputJournalWriter() = default;
    ~InputJournalWriter() { if (f_) std::fclose(f_); }
    InputJournalWriter(const InputJournalWriter&) = delete;
    InputJournalWriter& operator=(const InputJournalWriter&) = delete;

    bool open(const std::string& path) {
        if (f_) return false;
        f_ = std::fopen(path.c_str(), "wb");
        if (!f_) return false;
        path_ = path;
        std::fprintf(f_, "# POM68K input journal v1\n");
        return true;
    }
    bool isOpen() const { return f_ != nullptr; }
    const std::string& path() const { return path_; }

    // Header notes — write them before begin() so readers see them first.
    void note(const std::string& key, const std::string& value) {
        if (f_) std::fprintf(f_, "%s %s\n", key.c_str(), value.c_str());
    }
    void begin(long long clk) {
        if (!f_) return;
        std::fprintf(f_, "start %lld\n", clk);
        std::fflush(f_);
    }
    void event(long long clk, int type, int a, int b,
               const std::string& path) {
        if (!f_) return;
        if (path.empty())
            std::fprintf(f_, "ev %lld %s %d %d\n", clk,
                         inputEventName(type), a, b);
        else
            std::fprintf(f_, "ev %lld %s %d %d %s\n", clk,
                         inputEventName(type), a, b, path.c_str());
        std::fflush(f_);
    }
    void finish(long long clk) {
        if (!f_) return;
        std::fprintf(f_, "end %lld\n", clk);
        std::fclose(f_);
        f_ = nullptr;
    }
    // Abandon a recording that failed to arm (e.g. its snapshot could not
    // be written): close AND remove the file, so no header-only journal is
    // left behind to confuse a replay harness.
    void abort() {
        if (!f_) return;
        std::fclose(f_);
        f_ = nullptr;
        std::remove(path_.c_str());
    }

private:
    std::FILE* f_ = nullptr;
    std::string path_;
};

struct InputJournal {
    std::vector<std::pair<std::string, std::string>> notes;
    std::vector<InputEvent> events;
    long long startClk = 0;
    long long endClk = 0;
    bool complete = false;   // true only when the file carries `end`

    // First value for a key, or "" — media notes may repeat, read `notes`
    // directly for those.
    std::string note(const std::string& key) const {
        for (const auto& kv : notes)
            if (kv.first == key) return kv.second;
        return {};
    }
};

inline bool loadInputJournal(const std::string& path, InputJournal& out,
                             std::string& err) {
    out = InputJournal{};
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    char line[4096];
    bool sawHeader = false, sawStart = false;
    long long lastClk = 0;
    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        err = path + ":" + std::to_string(lineNo) + ": " + why;
        std::fclose(f);
        return false;
    };
    while (std::fgets(line, sizeof line, f)) {
        lineNo++;
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        if (s.empty()) continue;
        if (!sawHeader) {
            if (s != "# POM68K input journal v1")
                return fail("not a POM68K input journal (bad first line)");
            sawHeader = true;
            continue;
        }
        const size_t sp = s.find(' ');
        const std::string head = s.substr(0, sp);
        const std::string rest = sp == std::string::npos ? "" : s.substr(sp + 1);
        if (head == "ev") {
            if (!sawStart) return fail("event before start");
            // ev <clk> <name> <a> <b> [<path…>]
            char name[64] = {};
            long long clk = 0; int a = 0, b = 0, consumed = 0;
            if (std::sscanf(rest.c_str(), "%lld %63s %d %d%n",
                            &clk, name, &a, &b, &consumed) != 4)
                return fail("malformed event line");
            const int type = inputEventTypeFromName(name);
            if (type < 0) return fail(std::string("unknown event: ") + name);
            if (clk < lastClk) return fail("event clock goes backwards");
            lastClk = clk;
            std::string p = rest.substr(size_t(consumed));
            if (!p.empty() && p.front() == ' ') p.erase(0, 1);
            out.events.push_back({clk, type, a, b, std::move(p)});
        } else if (head == "start") {
            if (sawStart) return fail("duplicate start");
            out.startClk = std::strtoll(rest.c_str(), nullptr, 10);
            lastClk = out.startClk;
            sawStart = true;
        } else if (head == "end") {
            if (!sawStart) return fail("end before start");
            out.endClk = std::strtoll(rest.c_str(), nullptr, 10);
            out.complete = true;
        } else {
            if (sawStart) return fail("note after start: " + head);
            out.notes.emplace_back(head, rest);
        }
    }
    std::fclose(f);
    if (!sawHeader) { err = path + ": empty file"; return false; }
    if (!sawStart) { err = path + ": no start record"; return false; }
    if (!out.complete)
        out.endClk = out.events.empty() ? out.startClk
                                        : out.events.back().clk;
    return true;
}

} // namespace pom68k
