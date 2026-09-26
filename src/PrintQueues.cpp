// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// PrintQueues — see PrintQueues.h. The lpstat shapes parsed below are CUPS
// 2.x under LC_ALL=C (systemv/lpstat.c show_printers / show_accepting):
//   printer Q is idle.  enabled since <date>
//   printer Q now printing Q-12.  enabled since <date>
//   printer Q disabled since <date> -
//   	<reason>
//   Q accepting requests since <date>
//   Q not accepting requests since <date> -
//   system default destination: Q   |   no system default destination

#include "PrintQueues.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <csignal>
#include <pthread.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> lines(std::string_view out) {
    std::vector<std::string_view> v;
    while (!out.empty()) {
        const size_t nl = out.find('\n');
        v.push_back(out.substr(0, nl));
        if (nl == std::string_view::npos) break;
        out.remove_prefix(nl + 1);
    }
    return v;
}

// The indented line after `line`, if any: lpstat's reason text.
std::string reasonAfter(const std::vector<std::string_view>& ls, size_t line) {
    if (line + 1 < ls.size() && !ls[line + 1].empty() &&
        (ls[line + 1].front() == '\t' || ls[line + 1].front() == ' '))
        return std::string(trim(ls[line + 1]));
    return {};
}

std::string shellQuote(std::string_view s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') q += "'\\''";
        else q += c;
    }
    return q + "'";
}

struct Capture { bool ok = false; std::string out; };

Capture capture(const std::string& command) {
    Capture c;
#ifndef _WIN32
    FILE* p = ::popen(("LC_ALL=C " + command + " 2>/dev/null").c_str(), "r");
    if (!p) return c;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) c.out.append(buf, n);
    c.ok = ::pclose(p) == 0;
#else
    (void)command;
#endif
    return c;
}

} // namespace

PrintDestination PrintDestination::parse(std::string_view text) {
    PrintDestination d;
    text = trim(text);
    if (text == kFile) d.kind = Kind::File;
    else if (!text.empty()) { d.kind = Kind::CupsQueue; d.queue = std::string(text); }
    return d;
}

std::string PrintDestination::format() const {
    switch (kind) {
    case Kind::File: return std::string(kFile);
    case Kind::CupsQueue: return queue;
    case Kind::CupsDefault: break;
    }
    return {};
}

std::vector<std::string> lpstat::parseList(std::string_view out) {
    std::vector<std::string> v;
    for (std::string_view l : lines(out))
        if (!trim(l).empty() && l.front() != '\t' && l.front() != ' ')
            v.emplace_back(trim(l));
    return v;
}

std::optional<std::string> lpstat::parseDefault(std::string_view out) {
    constexpr std::string_view kTag = "system default destination: ";
    for (std::string_view l : lines(out))
        if (l.starts_with(kTag)) {
            std::string_view q = trim(l.substr(kTag.size()));
            if (!q.empty()) return std::string(q);
        }
    return std::nullopt;
}

QueueState lpstat::parseState(std::string_view queue, std::string_view printers,
                              std::string_view accepting) {
    QueueState s;
    const std::string head = "printer " + std::string(queue) + " ";
    const auto pl = lines(printers);
    for (size_t i = 0; i < pl.size(); ++i) {
        if (!pl[i].starts_with(head)) continue;
        std::string_view rest = pl[i].substr(head.size());
        if (rest.starts_with("disabled")) {
            s.phase = QueueState::Phase::Stopped;
            s.reason = reasonAfter(pl, i);
        } else if (rest.starts_with("now printing")) {
            s.phase = QueueState::Phase::Processing;
        } else {
            s.phase = QueueState::Phase::Idle;
        }
        break;
    }
    const std::string yes = std::string(queue) + " accepting requests";
    const std::string no = std::string(queue) + " not accepting requests";
    const auto al = lines(accepting);
    for (size_t i = 0; i < al.size(); ++i) {
        if (al[i].starts_with(yes)) { s.accepting = true; break; }
        if (al[i].starts_with(no)) {
            s.accepting = false;
            if (s.reason.empty()) s.reason = reasonAfter(al, i);
            break;
        }
    }
    return s;
}

std::string papStatusLine(std::string_view queue, const QueueState& st) {
    const std::string q(queue);
    std::string line;
    if (st.phase == QueueState::Phase::Unknown)
        return "status: busy; info: \"" + q + "\" appears to be offline.";
    if (!st.accepting && st.phase != QueueState::Phase::Processing)
        line = "status: busy; info: \"" + q + "\" is rejecting jobs; ";
    else if (st.phase == QueueState::Phase::Stopped)
        line = "status: idle; info: \"" + q + "\" is stopped, accepting jobs ; ";
    else if (st.phase == QueueState::Phase::Processing)
        line = "status: busy; info: \"" + q + "\" is processing a job ; ";
    else
        line = "status: idle; info: \"" + q + "\" is ready ; ";
    if (!st.reason.empty()) line += st.reason;
    if (line.size() > 255) line.resize(255);          // a Pascal string
    return line;
}

std::vector<std::string> splitPrintOptions(std::string_view options) {
    std::vector<std::string> v;
    size_t i = 0;
    while (i < options.size()) {
        while (i < options.size() && (options[i] == ' ' || options[i] == '\t')) ++i;
        size_t j = i;
        while (j < options.size() && options[j] != ' ' && options[j] != '\t') ++j;
        if (j > i) v.emplace_back(options.substr(i, j - i));
        i = j;
    }
    return v;
}

std::string writeSpoolFile(const std::string& dir, long jobNumber,
                           const std::vector<uint8_t>& job) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string path;
    int i = 1;
    do {
        path = dir + "/job_" + std::to_string(jobNumber) + "_" +
               std::to_string(i++) + ".ps";
    } while (fs::exists(path, ec) && i < 1000);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(job.data()), std::streamsize(job.size()));
    return out ? path : std::string();
}

// ── PrintSpooler ──

PrintSpooler::~PrintSpooler() {
    {
        std::lock_guard<std::mutex> l(mu_);
        stop_ = true;                    // the loop still prints what is queued
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void PrintSpooler::setCommands(Commands c) {
    std::lock_guard<std::mutex> l(mu_);
    cmd_ = std::move(c);
    pollNow_ = true;
    cv_.notify_all();
}

void PrintSpooler::setDestination(PrintDestination d, std::string options) {
    std::lock_guard<std::mutex> l(mu_);
    if (d == dest_ && options == options_) return;
    dest_ = std::move(d);
    options_ = std::move(options);
    // Until the next poll says otherwise, the view names what a job would
    // go to — a queue named on the relaunch line is not "offline" yet.
    view_.state = {};
    view_.activeQueue = dest_.kind == PrintDestination::Kind::CupsQueue
                            ? dest_.queue
                        : dest_.kind == PrintDestination::Kind::File ? std::string()
                                                                     : view_.defaultQueue;
    pollNow_ = true;
    cv_.notify_all();
}

void PrintSpooler::start() {
    std::lock_guard<std::mutex> l(mu_);
    if (running_) return;
    running_ = true;
    pollNow_ = true;
    worker_ = std::thread([this] { run(); });
}

PrintSpooler::View PrintSpooler::view() const {
    std::lock_guard<std::mutex> l(mu_);
    return view_;
}

void PrintSpooler::submit(long serial, std::vector<uint8_t> job, std::string fallbackDir) {
    start();
    std::lock_guard<std::mutex> l(mu_);
    jobs_.push_back({ serial, std::move(job), std::move(fallbackDir) });
    view_.pending++;
    cv_.notify_all();
}

void PrintSpooler::drain() {
    std::unique_lock<std::mutex> l(mu_);
    idle_.wait(l, [&] { return !running_ || (jobs_.empty() && view_.pending == 0); });
}

void PrintSpooler::run() {
#ifndef _WIN32
    // Writing to an `lp` that exited (no CUPS: /bin/sh answers 127 before
    // reading) raises SIGPIPE on THIS thread, whose default disposition
    // kills the emulator. Blocked here, the write fails with EPIPE instead
    // and pclose's status drives the file fallback.
    sigset_t pipe;
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe, nullptr);
#endif
    std::unique_lock<std::mutex> l(mu_);
    for (;;) {
        if (!jobs_.empty()) {
            Job job = std::move(jobs_.front());
            jobs_.erase(jobs_.begin());
            printLocked(l, std::move(job));
            continue;
        }
        if (stop_) break;
        if (pollNow_) { pollNow_ = false; pollLocked(l); continue; }
        idle_.notify_all();
        if (!cv_.wait_for(l, std::chrono::seconds(5),
                          [&] { return stop_ || pollNow_ || !jobs_.empty(); }))
            pollNow_ = true;             // the periodic refresh
    }
    running_ = false;
    idle_.notify_all();
}

// Runs the lpstat queries with the lock released; publishes one coherent
// view. Host wall time paces this: it is the host's queue being watched,
// not a device of the machine.
void PrintSpooler::pollLocked(std::unique_lock<std::mutex>& l) {
    const Commands cmd = cmd_;
    const PrintDestination dest = dest_;
    l.unlock();
    View v;
    const std::string lpstat = shellQuote(cmd.lpstat);
    const Capture list = capture(lpstat + " -e");
    v.cups = list.ok;
    v.queues = lpstat::parseList(list.out);
    v.defaultQueue = lpstat::parseDefault(capture(lpstat + " -d").out).value_or("");
    v.activeQueue = dest.kind == PrintDestination::Kind::CupsQueue ? dest.queue
                    : dest.kind == PrintDestination::Kind::File     ? std::string()
                                                                    : v.defaultQueue;
    if (!v.activeQueue.empty()) {
        const std::string q = shellQuote(v.activeQueue);
        v.state = lpstat::parseState(v.activeQueue,
                                     capture(lpstat + " -p " + q).out,
                                     capture(lpstat + " -a " + q).out);
    }
    v.polled = true;
    l.lock();
    if (!(dest == dest_)) { pollNow_ = true; return; }   // changed meanwhile
    v.pending = view_.pending;
    v.lastSerial = view_.lastSerial;
    v.lastResult = view_.lastResult;
    view_ = std::move(v);
}

void PrintSpooler::printLocked(std::unique_lock<std::mutex>& l, Job job) {
    const Commands cmd = cmd_;
    const PrintDestination dest = dest_;
    const std::string options = options_;
    l.unlock();
    std::string result;
#ifndef _WIN32
    if (dest.kind != PrintDestination::Kind::File) {
        std::string line = "LC_ALL=C " + shellQuote(cmd.lp);
        if (dest.kind == PrintDestination::Kind::CupsQueue)
            line += " -d " + shellQuote(dest.queue);
        for (const std::string& o : splitPrintOptions(options))
            line += " -o " + shellQuote(o);
        line += " -s -- - >/dev/null 2>&1";
        if (FILE* lp = ::popen(line.c_str(), "w")) {
            const size_t w = std::fwrite(job.data.data(), 1, job.data.size(), lp);
            if (::pclose(lp) == 0 && w == job.data.size())
                result = "CUPS (" + (dest.kind == PrintDestination::Kind::CupsQueue
                                         ? dest.queue : std::string("default")) + ")";
        }
    }
#endif
    if (result.empty()) result = writeSpoolFile(job.dir, job.serial, job.data);
    l.lock();
    view_.pending--;
    if (job.serial >= view_.lastSerial) {
        view_.lastSerial = job.serial;
        view_.lastResult = result;
    }
    pollNow_ = true;                     // the queue now holds the job
}
