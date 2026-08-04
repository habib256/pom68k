// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// PapServer — see PapServer.h. PAP packet = 4 ATP user bytes
// [connID, function, b2, b3] + data; OpenConn carries the client's
// responding socket + flow quantum in data[0..1], the reply mirrors
// [our socket, quantum 8, result16] + status pascal string
// (netatalk etc/papd/main.c PAP_OPEN handling).

#include "PapServer.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
constexpr uint8_t kPapSock = 131;
constexpr uint8_t kOpen = 1, kOpenReply = 2, kRead = 3, kData = 4,
                  kTickle = 5, kClose = 6, kCloseReply = 7,
                  kSendStatus = 8, kStatus = 9;
constexpr size_t kMaxData = 512;

void statusStr(std::vector<uint8_t>& v, const std::string& s) {
    v.push_back(uint8_t(std::min<size_t>(s.size(), 255)));
    v.insert(v.end(), s.begin(), s.begin() + std::min<size_t>(s.size(), 255));
}
} // namespace

void PapServer::configure(const std::string& printerName,
                          const std::string& spoolDir) {
    bool was = enabled_;
    if (was) setEnabled(false);
    if (!printerName.empty()) name_ = printerName;
    if (!spoolDir.empty()) spoolDir_ = spoolDir;
    if (was) setEnabled(true);
}

void PapServer::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    if (on) {
        st_.bindAtp(kPapSock, [this](std::shared_ptr<AtalkStack::AtpTxn> t) {
            papHandler(std::move(t));
        });
        st_.nbpRegister(name_, "LaserWriter", kPapSock);
        stat_.state = "idle";
    } else {
        st_.nbpUnregister(name_, "LaserWriter");
        open_ = false;
        releaseClientRead();
    }
}

// Dropping a deferred kRead without answering it leaves the entry in
// AtalkStack::pendingTxns_, which then silently swallows every later TReq
// reusing that (client, tid) — the guest's tid counter wraps, so the socket
// wedges for good. Every teardown path must make the transaction terminal.
void PapServer::releaseClientRead() {
    if (!pendingClientRead_) return;
    auto t = pendingClientRead_;
    pendingClientRead_.reset();
    t->respond({ { connId_, kData, 1, 0 } });        // EOF, no data
}

PapServer::Status PapServer::status() const {
    stat_.enabled = enabled_;
    stat_.registered = enabled_;
    stat_.printerName = name_;
    stat_.spoolDir = spoolDir_;
    stat_.busy = open_;
    if (!enabled_) stat_.state = "off";
    return stat_;
}

void PapServer::tick(int64_t now) {
    if (!enabled_ || !open_) return;
    if (now - lastHeard_ > 120 * st_.cpuHz()) {     // PAP: 2 min conn timer
        open_ = false;
        releaseClientRead();
        stat_.state = "idle";
        return;
    }
    if (now >= nextTickle_) {
        std::vector<uint8_t> req = { connId_, kTickle, 0, 0 };
        st_.atpRequest(client_, kPapSock, std::move(req), 0, false, {});
        nextTickle_ = now + 60 * st_.cpuHz();       // tickle every 60 s
    }
}

void PapServer::papHandler(std::shared_ptr<AtalkStack::AtpTxn> t) {
    if (!enabled_ || t->req.size() < 4) return;
    uint8_t cid = t->req[0], func = t->req[1];
    stat_.lastActivity = st_.now();
    switch (func) {

    case kOpen: {
        // data[0] = client responding socket, data[1] = client quantum
        if (t->req.size() < 6) return;
        bool busy = open_ && cid != connId_;
        std::vector<uint8_t> pkt = { cid, kOpenReply,
                                     uint8_t(busy ? 0xFF : 0),
                                     uint8_t(busy ? 0xFF : 0) };
        pkt.push_back(kPapSock);                    // our responding socket
        pkt.push_back(8);                           // our flow quantum
        pkt.push_back(busy ? 0xFF : 0);
        pkt.push_back(busy ? 0xFF : 0);
        statusStr(pkt, busy ? "status: busy" : "status: idle");
        t->respond({ std::move(pkt) });
        if (busy) return;

        open_ = true;
        connId_ = cid;
        client_ = t->src;
        client_.sock = t->req[4];
        seq_ = 0;
        readInFlight_ = false;
        eofSeen_ = false;
        job_.clear();
        scanned_ = 0;
        answers_.clear();
        releaseClientRead();            // kOpen re-init: retire the old txn
        lastHeard_ = st_.now();
        nextTickle_ = st_.now() + 60 * st_.cpuHz();
        stat_.state = "receiving job";
        stat_.bytesReceived = 0;
        issueRead();                                // start pulling the job
        return;
    }

    case kRead: {
        // The client reading from the "printer": the driver expects its
        // query answers on this channel. Serve queued answers, EOF when
        // the job is over and nothing is owed.
        if (!open_ || cid != connId_) return;
        lastHeard_ = st_.now();
        pendingClientRead_ = t;
        flushClientRead();
        return;
    }

    case kSendStatus: {
        std::vector<uint8_t> pkt = { 0, kStatus, 0, 0, 0, 0, 0, 0 };
        statusStr(pkt, open_ ? "status: busy; source AppleTalk"
                             : "status: idle");
        t->respond({ std::move(pkt) });
        return;
    }

    case kClose: {
        t->respond({ { cid, kCloseReply, 0, 0 } });
        if (open_ && cid == connId_) {
            if (!job_.empty() && !eofSeen_) finishJob();  // client bailed late
            open_ = false;
            releaseClientRead();
            stat_.state = "idle";
        }
        return;
    }

    case kTickle:
        if (open_ && cid == connId_) lastHeard_ = st_.now();
        return;

    default:
        return;
    }
}

void PapServer::issueRead() {
    if (!open_ || readInFlight_ || eofSeen_) return;
    readInFlight_ = true;
    seq_++;
    std::vector<uint8_t> req = { connId_, kRead, uint8_t(seq_ >> 8), uint8_t(seq_) };
    st_.atpRequest(client_, kPapSock, std::move(req), 8, true,
        [this](bool ok, std::vector<std::vector<uint8_t>>& pkts) {
            readInFlight_ = false;
            if (!open_) return;
            if (!ok) {                              // client vanished
                open_ = false;
                releaseClientRead();
                stat_.state = "idle";
                return;
            }
            lastHeard_ = st_.now();
            bool eof = false;
            for (auto& p : pkts) {
                if (p.size() >= 3 && p[2]) eof = true;
                if (p.size() > 4) job_.insert(job_.end(), p.begin() + 4, p.end());
            }
            stat_.bytesReceived = long(job_.size());
            scanQueries(scanned_);
            flushClientRead();
            if (eof) {
                eofSeen_ = true;
                finishJob();
                flushClientRead();                  // EOF the reader too
            } else {
                issueRead();
            }
        });
}

// The LaserWriter driver interrogates its printer with PostScript query
// jobs (`%%?BeginXxxQuery` … `%%?EndXxxQuery`). A spooler that answers
// `*` ("unknown") makes the driver self-sufficient — it downloads its
// own proc sets. That is all papd does for unconfigured queries too.
void PapServer::scanQueries(size_t from) {
    static const char kTag[] = "%%?Begin";
    while (from < job_.size()) {
        size_t eol = from;
        while (eol < job_.size() && job_[eol] != '\n' && job_[eol] != '\r') eol++;
        if (eol >= job_.size()) break;              // incomplete line: wait
        size_t len = eol - from;
        if (len > 8 && !std::memcmp(job_.data() + from, kTag, 8)) {
            const char* ans = "*\r";
            answers_.insert(answers_.end(), ans, ans + 2);
        }
        from = eol + 1;
        scanned_ = from;
    }
}

void PapServer::flushClientRead() {
    if (!pendingClientRead_) return;
    if (answers_.empty() && !eofSeen_) return;      // nothing owed yet: defer
    auto t = pendingClientRead_;
    pendingClientRead_.reset();
    std::vector<std::vector<uint8_t>> pkts;
    while (!answers_.empty() && pkts.size() < 8) {
        size_t chunk = std::min(answers_.size(), kMaxData);
        std::vector<uint8_t> p = { connId_, kData, 0, 0 };
        p.insert(p.end(), answers_.begin(), answers_.begin() + long(chunk));
        answers_.erase(answers_.begin(), answers_.begin() + long(chunk));
        pkts.push_back(std::move(p));
    }
    if (pkts.empty()) pkts.push_back({ connId_, kData, 1, 0 });   // pure EOF
    else if (eofSeen_ && answers_.empty()) pkts.back()[2] = 1;
    t->respond(std::move(pkts));
}

void PapServer::finishJob() {
    if (job_.empty()) { stat_.state = "idle"; return; }
    stat_.jobs++;
    bool spooled = false;
#ifndef _WIN32
    if (!fileOnly_) {
        // CUPS last mile: hand the PostScript to lp(1) when available.
        // popen() succeeds even with no CUPS installed — /bin/sh exits 127 and
        // the read end is gone before we write, so fwrite raises SIGPIPE, whose
        // default disposition KILLS the emulator mid-print. Ignore it for the
        // duration and let pclose's status drive the file fallback.
        struct sigaction ign {}, prev {};
        ign.sa_handler = SIG_IGN;
        // macOS exposes sigemptyset as a function-like macro, so qualifying
        // it with the global namespace makes AppleClang expand invalid code.
        sigemptyset(&ign.sa_mask);
        ::sigaction(SIGPIPE, &ign, &prev);
        FILE* lp = ::popen("lp -s -- - >/dev/null 2>&1", "w");
        if (lp) {
            size_t w = std::fwrite(job_.data(), 1, job_.size(), lp);
            int rc = ::pclose(lp);
            if (w == job_.size() && rc == 0) {
                spooled = true;
                stat_.lastJob = "CUPS (lp)";
            }
        }
        ::sigaction(SIGPIPE, &prev, nullptr);
    }
#endif
    if (!spooled) {
        std::error_code ec;
        fs::create_directories(spoolDir_, ec);
        int i = 1;
        std::string path;
        do {
            path = spoolDir_ + "/job_" + std::to_string(stat_.jobs) + "_"
                 + std::to_string(i++) + ".ps";
        } while (fs::exists(path, ec) && i < 1000);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(job_.data()),
                  std::streamsize(job_.size()));
        stat_.lastJob = path;
    }
    job_.clear();
    scanned_ = 0;
    stat_.state = "idle";
}
