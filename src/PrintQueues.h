// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── PrintQueues: the host half of the in-process LaserWriter ──
// Where a finished PAP job goes (a CUPS queue, the CUPS default, or a .ps
// file) and what the printer tells the Mac about itself. CUPS is reached
// through its command-line clients, as the spooling always was: `lpstat`
// for the queue list and state, `lp -d` for the job. Both run on the
// spooler's OWN thread — a slow or wedged cupsd must never stall the
// machine thread, which answers PAP under the hub's lock — and the machine
// thread only ever reads the cached view.
//
// The status wording is netatalk papd's (extern/netatalk2 etc/papd/
// print_cups.c `cups_status_msg`), and so is the rule that a queue which
// rejects jobs, or cannot be found, answers OpenConn busy.
// Gates: tests/print_queues_test.cpp (parsers, fake lp/lpstat end to end),
// tests/pap_server_test.cpp.

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// "" = the CUPS default destination, "#file" = the spool folder only,
// anything else = that CUPS queue. '#' is one of the characters CUPS
// refuses in a queue name (cupsd validate_name), so the sentinel cannot
// collide with a real queue.
struct PrintDestination {
    enum class Kind { CupsDefault, CupsQueue, File };
    Kind kind = Kind::CupsDefault;
    std::string queue;                   // Kind::CupsQueue only

    static constexpr std::string_view kFile = "#file";
    static PrintDestination parse(std::string_view text);
    std::string format() const;
    bool operator==(const PrintDestination&) const = default;
};

struct QueueState {
    enum class Phase { Unknown, Idle, Processing, Stopped };
    Phase phase = Phase::Unknown;        // Unknown = lpstat did not know it
    bool accepting = false;
    std::string reason;                  // lpstat's indented reason line
};

// Pure parsers of `LC_ALL=C lpstat` output.
namespace lpstat {
std::vector<std::string> parseList(std::string_view out);          // -e
std::optional<std::string> parseDefault(std::string_view out);     // -d
QueueState parseState(std::string_view queue, std::string_view printers,
                      std::string_view accepting);                  // -p Q / -a Q
} // namespace lpstat

// The PAP status line papd would send for this queue.
std::string papStatusLine(std::string_view queue, const QueueState& state);
// "a=b c=d" → the `lp -o` arguments, one per whitespace-separated word.
std::vector<std::string> splitPrintOptions(std::string_view options);
// Write a job as <dir>/job_<n>_<i>.ps (first free i); the path, or "" on error.
std::string writeSpoolFile(const std::string& dir, long jobNumber,
                           const std::vector<uint8_t>& job);

class PrintSpooler {
public:
    // The CUPS clients, by name or path. A gate points them at scripts so
    // that no test ever prints on the host's real printer.
    struct Commands { std::string lp = "lp", lpstat = "lpstat"; };

    PrintSpooler() = default;
    ~PrintSpooler();
    PrintSpooler(const PrintSpooler&) = delete;
    PrintSpooler& operator=(const PrintSpooler&) = delete;

    void setCommands(Commands c);
    void setDestination(PrintDestination d, std::string options);
    // Start polling (idempotent): the queue list, the default and the
    // destination's state, now and then every few seconds.
    void start();

    struct View {
        bool polled = false;             // one poll has completed
        bool cups = false;               // lpstat answered at all
        std::vector<std::string> queues;
        std::string defaultQueue;        // "" = none
        std::string activeQueue;         // what a job would go to ("" = file)
        QueueState state;                // activeQueue's
        long pending = 0;                // jobs handed over, not yet done
        long lastSerial = 0;             // job serial of lastResult
        std::string lastResult;          // "CUPS (Q)" or the fallback file
    };
    View view() const;

    // Hand a finished job over. It goes to the destination's queue with
    // `lp -d`; when that fails it is written to fallbackDir instead.
    void submit(long serial, std::vector<uint8_t> job, std::string fallbackDir);
    // Block until every submitted job is done (gates only).
    void drain();

private:
    struct Job { long serial; std::vector<uint8_t> data; std::string dir; };
    void run();
    void pollLocked(std::unique_lock<std::mutex>& l);
    void printLocked(std::unique_lock<std::mutex>& l, Job job);

    mutable std::mutex mu_;
    std::condition_variable cv_, idle_;
    std::thread worker_;
    bool running_ = false, stop_ = false, pollNow_ = false;
    Commands cmd_;
    PrintDestination dest_;
    std::string options_;
    std::vector<Job> jobs_;
    View view_;
};
