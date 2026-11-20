// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// HostProcess — run one helper program to completion, with a deadline and a
// stop request, and no shell in between.
//
// The ROM downloader is its only client: HTTPS goes through the system
// `curl` so POM68K grows no TLS dependency. The arguments are handed over
// as an argv vector (posix_spawnp / CreateProcessW), never through a shell,
// so a URL cannot become a command. The program is looked up on the search
// path by the OS call itself; nothing here reads the process environment.
// Its standard streams are the null device: the caller learns the outcome
// from the exit status and the files the program wrote.
//
// Blocking: call it from a worker thread. `stop` is polled every 30 ms
// while the child runs — that poll is how a GUI that is closing gets its
// worker back without waiting out the child's own timeout.
//
// Emscripten has no processes: `hostProcessesAvailable()` is false and
// `runHostProcess` reports "not available" without trying.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace pom68k::host {

struct ProcessOutcome {
    bool started = false;    // false: the program could not be launched
    bool timedOut = false;   // killed at the deadline
    bool stopped = false;    // killed because `stop` returned true
    int exitCode = -1;       // valid when started && !timedOut && !stopped
    std::string error;       // set when !started (or the wait itself failed)

    bool succeeded() const { return started && !timedOut && !stopped && exitCode == 0; }
};

bool hostProcessesAvailable();

ProcessOutcome runHostProcess(const std::string& program,
                              const std::vector<std::string>& args,
                              int timeoutMs,
                              const std::function<bool()>& stop = {});

// The Windows command line `args` becomes (MSVC CRT quoting rules), exposed
// so the gate can pin the quoting on every host.
std::string windowsCommandLine(const std::string& program,
                               const std::vector<std::string>& args);

} // namespace pom68k::host
