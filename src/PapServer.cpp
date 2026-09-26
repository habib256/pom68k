// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// PapServer — see PapServer.h. PAP packet = 4 ATP user bytes
// [connID, function, b2, b3] + data; OpenConn carries the client's
// responding socket + flow quantum in data[0..1], the reply mirrors
// [our socket, quantum 8, result16] + status pascal string
// (netatalk etc/papd/main.c PAP_OPEN handling).

#include "PapServer.h"

#include <cstring>

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

void PapServer::setDestination(const PrintDestination& d, const std::string& options) {
    dest_ = d;
    options_ = options;
    spooler_.setDestination(d, options);
    // A CUPS destination is watched from the moment it is chosen, so the
    // first OpenConn already knows whether the queue accepts jobs.
    if (enabled_ && d.kind != PrintDestination::Kind::File) spooler_.start();
}

// papd's getstatus(): our own connection first (the job being received is
// the printer's business), then the destination queue's live state. A
// file destination — or no CUPS at all behind the default, where jobs
// fall back to files — is a printer that is simply idle.
std::string PapServer::statusLine() const {
    if (open_) return "status: busy; source: AppleTalk";
    if (dest_.kind == PrintDestination::Kind::File) return "status: idle";
    const PrintSpooler::View v = spooler_.view();
    if (!v.polled || v.activeQueue.empty() ||
        (dest_.kind == PrintDestination::Kind::CupsDefault && !v.cups))
        return "status: idle";
    return papStatusLine(v.activeQueue, v.state);
}

// papd answers OpenConn busy when CUPS is not accepting jobs for the queue
// (etc/papd/main.c PAP_OPEN, cups_get_printer_status() == 0) — rejecting,
// or not found. Before the first poll nothing is known and nothing refused.
bool PapServer::refusesJobs() const {
    if (dest_.kind == PrintDestination::Kind::File) return false;
    const PrintSpooler::View v = spooler_.view();
    if (!v.polled || v.activeQueue.empty() ||
        (dest_.kind == PrintDestination::Kind::CupsDefault && !v.cups))
        return false;
    return v.state.phase == QueueState::Phase::Unknown ||
           (!v.state.accepting && v.state.phase != QueueState::Phase::Processing);
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
        if (dest_.kind != PrintDestination::Kind::File) spooler_.start();
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
    stat_.destination = dest_;
    stat_.printOptions = options_;
    stat_.statusLine = statusLine();
    stat_.host = spooler_.view();
    // The last job's fate: a CUPS hand-over resolves on the spooler thread.
    if (stat_.host.lastSerial > lastFileSerial_) stat_.lastJob = stat_.host.lastResult;
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
        bool busy = (open_ && cid != connId_) || (!open_ && refusesJobs());
        std::vector<uint8_t> pkt = { cid, kOpenReply,
                                     uint8_t(busy ? 0xFF : 0),
                                     uint8_t(busy ? 0xFF : 0) };
        pkt.push_back(kPapSock);                    // our responding socket
        pkt.push_back(8);                           // our flow quantum
        pkt.push_back(busy ? 0xFF : 0);
        pkt.push_back(busy ? 0xFF : 0);
        statusStr(pkt, (open_ && cid != connId_) ? "status: busy" : statusLine());
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
        statusStr(pkt, statusLine());
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
    if (dest_.kind == PrintDestination::Kind::File) {
        stat_.lastJob = writeSpoolFile(spoolDir_, stat_.jobs, job_);
        lastFileSerial_ = stat_.jobs;
    } else {
        // CUPS last mile, off the machine thread; a refusal lands in the
        // spool folder instead, and status() reports which.
        stat_.lastJob = "CUPS (pending)";
        spooler_.submit(stat_.jobs, std::move(job_), spoolDir_);
    }
    job_.clear();
    scanned_ = 0;
    stat_.state = "idle";
}
