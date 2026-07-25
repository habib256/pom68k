// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AfpServer: in-process AppleShare (ASP + AFP 2.1 subset) ──
// Replaces the external netatalk afpd for the common case: one guest,
// one shared host folder, guest/cleartext login. Rides AtalkStack's ATP
// engine: an ASP listener (NBP "name:AFPServer@zone", GetStatus +
// OpenSession) and a session socket (Command/Write/Tickle/Close).
//
// The AFP vocabulary covers what System 6-8 Finders actually issue for
// browse/copy both ways: Login/Logout, GetSrvrInfo/Parms, OpenVol/
// GetVolParms, Enumerate, GetFileDirParms, Set(File|Dir|FileDir)Parms,
// OpenFork/Read/Write/SetForkParms/CloseFork, CreateFile/CreateDir/
// Delete/Rename/MoveAndRename, ByteRangeLock (grant-all), Desktop DB
// stubs (OpenDT + NoItem answers). Resource forks + Finder info live in
// netatalk-style `.AppleDouble/<name>` sidecars (AppleDouble v2,
// FinderInfo + resource entries), so a folder previously served by the
// external afpd keeps its metadata.
//
// Wire layouts pinned against extern/netatalk2 (libatalk/asp/*.c,
// etc/afpd/{enumerate,file,directory,volume,fork}.c) and Inside
// AppleTalk ch.11/13. Gate: tests/afp_server_test.cpp.

#pragma once
#include "AtalkStack.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class AfpServer {
public:
    explicit AfpServer(AtalkStack& st) : st_(st) {}

    // dirPath: the host folder served as volume volName. Safe to call
    // again (rebinds NBP under the new name).
    void configure(const std::string& serverName, const std::string& volName,
                   const std::string& dirPath);
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void tick(int64_t now);

    struct Status {
        bool enabled = false;
        bool registered = false;     // NBP AFPServer entity live
        std::string serverName, volName, dirPath;
        bool dirOk = false;          // folder exists and is writable
        int sessions = 0;
        bool volMounted = false;     // a session has the volume open
        std::string lastUser;        // last FPLogin identity
        std::string lastCmd;
        long cmdCount = 0;
        long bytesRead = 0, bytesWritten = 0;
        int64_t lastActivity = -1;   // emuCycles
    };
    Status status() const;

private:
    struct Fork {
        uint32_t id = 0;             // CNID of the file
        bool resource = false;
        bool writable = false;
    };
    struct Session {
        AtalkStack::Addr wss;        // client workstation session socket
        uint16_t seq = 0;
        int64_t lastHeard = 0;
        int64_t nextTickle = 0;
        bool volOpen = false;
        std::map<uint16_t, Fork> forks;
        uint16_t nextFork = 1;
    };

    void slsHandler(std::shared_ptr<AtalkStack::AtpTxn> t);
    void sssHandler(std::shared_ptr<AtalkStack::AtpTxn> t);
    void dispatchAfp(Session& s, std::shared_ptr<AtalkStack::AtpTxn> t,
                     const uint8_t* cmd, size_t n);
    void handleWrite(Session& s, uint8_t sid, uint16_t seq,
                     std::shared_ptr<AtalkStack::AtpTxn> t,
                     const uint8_t* cmd, size_t n);
    void aspReply(std::shared_ptr<AtalkStack::AtpTxn> t, int32_t result,
                  const std::vector<uint8_t>& data);
    void buildStatusBlock();

    // catalog / filesystem backend (definitions in AfpServer.cpp)
    std::string pathForId(uint32_t id) const;
    uint32_t idForPath(const std::string& rel);
    void dropId(const std::string& rel);
    int resolvePath(uint32_t dirId, const uint8_t* p, size_t n, size_t& used,
                    std::string& outRel);

    AtalkStack& st_;
    bool enabled_ = false;
    bool configured_ = false;
    std::string serverName_ = "POM68K";
    std::string volName_ = "Partage";
    std::string dir_;                // host folder ('' = unset)
    std::vector<uint8_t> statusBlock_;

    std::map<uint8_t, Session> sessions_;
    uint8_t nextSid_ = 1;
    std::map<uint32_t, std::string> idToPath_;   // CNID → volume-relative
    std::map<std::string, uint32_t> pathToId_;
    uint32_t nextId_ = 16;

    // GUI-facing counters
    mutable Status stat_;
};
