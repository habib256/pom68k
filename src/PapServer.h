// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── PapServer: in-process LaserWriter (PAP print service) ──
// Replaces the external netatalk papd for the common case: one NBP
// "name:LaserWriter@zone" entity, one job at a time. The guest's
// LaserWriter driver opens a PAP connection; the server pulls the
// PostScript with SendData credits (PAP is pull-based), answers the
// driver's `%%?Begin…Query` lines with the "unknown" reply `*` (the
// driver then downloads its own proc sets, like a printer with no
// spooler smarts), and on EOF spools the job: `lp` (CUPS) when the
// host has it, else a timestamped .ps file in the spool folder.
//
// Wire layout pinned against extern/netatalk2 etc/papd/{main,session}.c
// and include/atalk/pap.h; Inside AppleTalk ch.10.
// Gate: tests/pap_server_test.cpp.

#pragma once
#include "AtalkStack.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class PapServer {
public:
    explicit PapServer(AtalkStack& st) : st_(st) {}

    void configure(const std::string& printerName, const std::string& spoolDir);
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void tick(int64_t now);

    struct Status {
        bool enabled = false;
        bool registered = false;
        std::string printerName, spoolDir;
        bool busy = false;               // job in progress
        std::string state;               // "idle" / "receiving job" / …
        long jobs = 0;                   // completed jobs
        long bytesReceived = 0;          // current/last job size
        std::string lastJob;             // where the last job went
        int64_t lastActivity = -1;
    };
    Status status() const;

    // Test hook: force the file spool path even when `lp` exists.
    void setSpoolToFileOnly(bool v) { fileOnly_ = v; }

private:
    void papHandler(std::shared_ptr<AtalkStack::AtpTxn> t);
    void issueRead();
    void scanQueries(size_t from);
    void flushClientRead();
    void releaseClientRead();       // answer + drop a deferred kRead (teardown)
    void finishJob();

    AtalkStack& st_;
    bool enabled_ = false;
    bool fileOnly_ = false;
    std::string name_ = "POM68K Printer";
    std::string spoolDir_ = "run/print";

    // single active connection
    bool open_ = false;
    uint8_t connId_ = 0;
    AtalkStack::Addr client_;            // client's responding socket
    uint16_t seq_ = 0;                   // our SendData sequence
    bool readInFlight_ = false;
    bool eofSeen_ = false;
    std::vector<uint8_t> job_;           // PostScript received
    size_t scanned_ = 0;                 // query-scan progress into job_
    std::deque<uint8_t> answers_;        // bytes owed to the client's reads
    std::shared_ptr<AtalkStack::AtpTxn> pendingClientRead_;
    int64_t lastHeard_ = 0;
    int64_t nextTickle_ = 0;

    mutable Status stat_;
};
