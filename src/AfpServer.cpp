// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// AfpServer — see AfpServer.h. Layouts pinned against extern/netatalk2:
// asp_cmdreply.c (reply = 4-byte header per ATP packet, result in the
// first), asp_getsess.c (OpenSess reply user bytes [SSS, sid, err16]),
// asp_write.c (server-initiated WriteContinue), etc/afpd/enumerate.c
// (entry = len, isDir, params, even-padded), file.c/directory.c
// (ascending-bit params, LNAME as offset16 + trailing pascal string),
// volume.c (volume params), Inside AppleTalk ch.11/13.

#include "AfpServer.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
constexpr uint8_t kSls = 129;         // session listening socket (NBP'd)
constexpr uint8_t kSss = 130;         // server session socket

// ASP functions (asp.h)
constexpr uint8_t kAspClose = 1, kAspCmd = 2, kAspStat = 3, kAspOpen = 4,
                  kAspTickle = 5, kAspWrite = 6, kAspWrtCont = 7;

// AFP results (afp.h)
constexpr int32_t kErrAccess = -5000, kErrBitmap = -5004, kErrDirNotEmpty = -5007,
                  kErrEof = -5009, kErrNoItem = -5012, kErrMisc = -5014,
                  kErrExist = -5017, kErrNoObj = -5018, kErrParam = -5019,
                  kErrNoDir = -5029, kErrNoOp = -5024, kErrBadType = -5025;

constexpr uint32_t kRootId = 2;
constexpr uint16_t kVolId = 1;
constexpr int64_t kAfpEpoch = 946684800;      // 2000-01-01 (AFP date zero)

void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x >> 8); v.push_back(uint8_t(x)); }
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void pstr(std::vector<uint8_t>& v, const std::string& s, size_t maxLen = 31) {
    size_t l = std::min(s.size(), maxLen);
    v.push_back(uint8_t(l));
    v.insert(v.end(), s.begin(), s.begin() + l);
}
uint32_t afpDate(int64_t unixSecs) { return uint32_t(unixSecs - kAfpEpoch); }

// Mac ':' is the path separator and cannot appear in names; unix '/'
// cannot either — netatalk's classic swap keeps both sides legal.
std::string macToUnix(const std::string& s) {
    std::string r = s;
    for (char& c : r) if (c == '/') c = ':';
    return r;
}
std::string unixToMac(const std::string& s) {
    std::string r = s;
    for (char& c : r) if (c == ':') c = '/';
    return r;
}

// ── AppleDouble v2 sidecars (netatalk .AppleDouble/<name> layout) ──
struct AdMeta {
    uint8_t finder[32] = {};
    bool hasFinder = false;
    uint32_t cdate = 0, bdate = 0;    // AFP dates; 0 = unset
    std::vector<uint8_t> rsrc;
};

std::string adPath(const std::string& host) {
    fs::path p(host);
    return (p.parent_path() / ".AppleDouble" / p.filename()).string();
}

bool adRead(const std::string& host, AdMeta& m) {
    std::ifstream in(adPath(host), std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    if (b.size() < 26 || rd32(b.data()) != 0x00051607u) return false;
    uint16_t count = rd16(b.data() + 24);
    for (uint16_t i = 0; i < count; i++) {
        size_t e = 26 + size_t(i) * 12;
        if (e + 12 > b.size()) break;
        uint32_t id = rd32(b.data() + e), off = rd32(b.data() + e + 4),
                 len = rd32(b.data() + e + 8);
        if (off > b.size() || len > b.size() - off) continue;
        if (id == 9 && len >= 16) {              // Finder info
            std::memcpy(m.finder, b.data() + off, std::min<size_t>(len, 32));
            m.hasFinder = true;
        } else if (id == 8 && len >= 16) {       // dates: create/mod/backup/access
            m.cdate = rd32(b.data() + off);
            m.bdate = rd32(b.data() + off + 8);
        } else if (id == 2) {                    // resource fork
            m.rsrc.assign(b.begin() + off, b.begin() + off + len);
        }
    }
    return true;
}

void adWrite(const std::string& host, const AdMeta& m) {
    std::error_code ec;
    fs::create_directories(fs::path(adPath(host)).parent_path(), ec);
    std::vector<uint8_t> b;
    put32(b, 0x00051607u);                       // magic
    put32(b, 0x00020000u);                       // version 2
    b.resize(b.size() + 16, 0);                  // filler
    put16(b, 3);                                 // entries
    uint32_t off = 26 + 3 * 12;
    put32(b, 9);  put32(b, off);      put32(b, 32);            // Finder info
    put32(b, 8);  put32(b, off + 32); put32(b, 16);            // dates
    put32(b, 2);  put32(b, off + 48); put32(b, uint32_t(m.rsrc.size()));
    b.insert(b.end(), m.finder, m.finder + 32);
    put32(b, m.cdate); put32(b, m.cdate); put32(b, m.bdate); put32(b, 0);
    b.insert(b.end(), m.rsrc.begin(), m.rsrc.end());
    std::ofstream out(adPath(host), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

const char* afpCmdName(uint8_t c) {
    switch (c) {
    case 1: return "ByteRangeLock"; case 2: return "CloseVol";
    case 4: return "CloseFork"; case 6: return "CreateDir";
    case 7: return "CreateFile"; case 8: return "Delete";
    case 9: return "Enumerate"; case 10: return "Flush";
    case 11: return "FlushFork"; case 14: return "GetForkParms";
    case 15: return "GetSrvrInfo"; case 16: return "GetSrvrParms";
    case 17: return "GetVolParms"; case 18: return "Login";
    case 20: return "Logout"; case 23: return "MoveAndRename";
    case 24: return "OpenVol"; case 26: return "OpenFork";
    case 27: return "Read"; case 28: return "Rename";
    case 29: return "SetDirParms"; case 30: return "SetFileParms";
    case 31: return "SetForkParms"; case 32: return "SetVolParms";
    case 33: return "Write"; case 34: return "GetFileDirParms";
    case 35: return "SetFileDirParms"; case 37: return "GetUserInfo";
    case 48: return "OpenDT"; case 49: return "CloseDT";
    case 51: return "GetIcon"; case 52: return "GetIconInfo";
    case 53: return "AddAPPL"; case 55: return "GetAPPL";
    case 56: return "AddComment"; case 58: return "GetComment";
    default: return "?";
    }
}
} // namespace

// ── lifecycle ───────────────────────────────────────────────────────────

void AfpServer::configure(const std::string& serverName,
                          const std::string& volName,
                          const std::string& dirPath) {
    bool was = enabled_;
    if (was) setEnabled(false);
    serverName_ = serverName.empty() ? "POM68K" : serverName;
    volName_ = volName.empty() ? "Partage" : volName;
    dir_ = dirPath;
    idToPath_ = { { kRootId, "" } };
    pathToId_ = { { "", kRootId } };
    nextId_ = 16;
    buildStatusBlock();
    configured_ = true;
    if (was) setEnabled(true);
}

void AfpServer::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    if (on) {
        if (!configured_) configure(serverName_, volName_, dir_);
        st_.bindAtp(kSls, [this](std::shared_ptr<AtalkStack::AtpTxn> t) { slsHandler(std::move(t)); });
        st_.bindAtp(kSss, [this](std::shared_ptr<AtalkStack::AtpTxn> t) { sssHandler(std::move(t)); });
        st_.nbpRegister(serverName_, "AFPServer", kSls);
    } else {
        st_.nbpUnregister(serverName_, "AFPServer");
        sessions_.clear();
    }
}

void AfpServer::tick(int64_t now) {
    if (!enabled_) return;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        Session& s = it->second;
        if (now - s.lastHeard > 120 * st_.cpuHz()) {   // 4 missed tickles × 30 s
            it = sessions_.erase(it);
            continue;
        }
        if (now >= s.nextTickle) {
            // Keep the client's session timer fed (SPTickle, no response).
            std::vector<uint8_t> req = { kAspTickle, it->first, 0, 0 };
            st_.atpRequest(s.wss, kSss, std::move(req), 0, false, {});
            s.nextTickle = now + 30 * st_.cpuHz();
        }
        ++it;
    }
}

AfpServer::Status AfpServer::status() const {
    stat_.enabled = enabled_;
    stat_.registered = enabled_;
    stat_.serverName = serverName_;
    stat_.volName = volName_;
    stat_.dirPath = dir_;
    std::error_code ec;
    stat_.dirOk = !dir_.empty() && fs::is_directory(dir_, ec)
#ifndef _WIN32
                  && ::access(dir_.c_str(), W_OK | X_OK) == 0
#endif
        ;
    stat_.sessions = int(sessions_.size());
    stat_.volMounted = false;
    for (const auto& [sid, s] : sessions_)
        if (s.volOpen) stat_.volMounted = true;
    return stat_;
}

// ── GetStatus / GetSrvrInfo block ──────────────────────────────────────

void AfpServer::buildStatusBlock() {
    std::vector<uint8_t>& b = statusBlock_;
    b.clear();
    b.resize(10, 0);                       // 4 offsets + flags, patched below
    pstr(b, serverName_);
    if (b.size() & 1) b.push_back(0);
    uint16_t machOff = uint16_t(b.size());
    pstr(b, "Macintosh");                  // machine type
    uint16_t versOff = uint16_t(b.size());
    b.push_back(3);
    pstr(b, "AFPVersion 1.1");
    pstr(b, "AFPVersion 2.0");
    pstr(b, "AFPVersion 2.1");
    uint16_t uamOff = uint16_t(b.size());
    b.push_back(2);
    pstr(b, "No User Authent");
    pstr(b, "Cleartxt Passwrd");
    b[0] = machOff >> 8; b[1] = uint8_t(machOff);
    b[2] = versOff >> 8; b[3] = uint8_t(versOff);
    b[4] = uamOff >> 8;  b[5] = uint8_t(uamOff);
    // icon offset (6-7) = none, flags (8-9) = 0
}

// ── catalog (CNIDs are per-run, root = 2) ───────────────────────────────

std::string AfpServer::pathForId(uint32_t id) const {
    auto it = idToPath_.find(id);
    return it == idToPath_.end() ? std::string("\x01") : it->second;  // \x01 = invalid marker
}

uint32_t AfpServer::idForPath(const std::string& rel) {
    auto it = pathToId_.find(rel);
    if (it != pathToId_.end()) return it->second;
    uint32_t id = nextId_++;
    pathToId_[rel] = id;
    idToPath_[id] = rel;
    return id;
}

void AfpServer::dropId(const std::string& rel) {
    std::string prefix = rel + "/";
    for (auto it = pathToId_.begin(); it != pathToId_.end();) {
        if (it->first == rel || it->first.rfind(prefix, 0) == 0) {
            idToPath_.erase(it->second);
            it = pathToId_.erase(it);
        } else ++it;
    }
}

// AFP pathname: type byte (1 short / 2 long) + pascal string where NUL
// separates elements and an empty element means "up one directory".
int AfpServer::resolvePath(uint32_t dirId, const uint8_t* p, size_t n,
                           size_t& used, std::string& outRel) {
    if (n < 2) return int(kErrParam);
    uint8_t type = p[0];
    if (type != 1 && type != 2) return int(kErrParam);
    size_t len = p[1];
    if (2 + len > n) return int(kErrParam);
    used = 2 + len;
    std::string rel = pathForId(dirId);
    if (rel == "\x01") return int(kErrNoDir);
    size_t i = 0;
    const uint8_t* s = p + 2;
    bool lastSep = true;
    while (i < len) {
        if (s[i] == 0) {
            if (!lastSep) { lastSep = true; i++; continue; }
            // consecutive NUL = parent hop
            size_t cut = rel.find_last_of('/');
            rel = cut == std::string::npos ? "" : rel.substr(0, cut);
            i++;
            continue;
        }
        size_t j = i;
        while (j < len && s[j] != 0) j++;
        std::string mac(reinterpret_cast<const char*>(s + i), j - i);
        std::string ux = macToUnix(mac);
        if (ux == "." || ux == ".." || ux.empty()) return int(kErrParam);
        rel = rel.empty() ? ux : rel + "/" + ux;
        i = j;
        lastSep = false;
    }
    outRel = rel;
    return 0;
}

// ── ASP listener ────────────────────────────────────────────────────────

void AfpServer::slsHandler(std::shared_ptr<AtalkStack::AtpTxn> t) {
    if (!enabled_ || t->req.size() < 4) return;
    switch (t->req[0]) {
    case kAspStat: {
        std::vector<uint8_t> pkt = { 0, 0, 0, 0 };
        pkt.insert(pkt.end(), statusBlock_.begin(), statusBlock_.end());
        t->respond({ std::move(pkt) });
        return;
    }
    case kAspOpen: {
        uint8_t wss = t->req[1];
        uint8_t sid = nextSid_++;
        if (!nextSid_) nextSid_ = 1;
        Session s;
        s.wss = t->src;
        s.wss.sock = wss;
        s.lastHeard = st_.now();
        s.nextTickle = st_.now() + 30 * st_.cpuHz();
        sessions_[sid] = std::move(s);
        stat_.lastActivity = st_.now();
        t->respond({ { kSss, sid, 0, 0 } });   // [SSS, sid, no error]
        return;
    }
    default:
        return;
    }
}

void AfpServer::aspReply(std::shared_ptr<AtalkStack::AtpTxn> t, int32_t result,
                         const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> pkts;
    size_t off = 0;
    do {
        std::vector<uint8_t> pkt;
        if (pkts.empty()) put32(pkt, uint32_t(result));
        else pkt.resize(4, 0);
        size_t chunk = std::min<size_t>(578, data.size() - off);
        pkt.insert(pkt.end(), data.begin() + off, data.begin() + off + chunk);
        off += chunk;
        pkts.push_back(std::move(pkt));
    } while (off < data.size() && pkts.size() < 8);
    t->respond(std::move(pkts));
}

void AfpServer::sssHandler(std::shared_ptr<AtalkStack::AtpTxn> t) {
    if (!enabled_ || t->req.size() < 4) return;
    uint8_t func = t->req[0], sid = t->req[1];
    uint16_t seq = rd16(t->req.data() + 2);
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) {
        if (func == kAspCmd || func == kAspWrite)
            aspReply(t, -1072, {});        // aspSessClosed
        return;
    }
    Session& s = it->second;
    s.lastHeard = st_.now();
    stat_.lastActivity = st_.now();
    switch (func) {
    case kAspTickle:
        return;                            // timer already refreshed
    case kAspClose:
        t->respond({ { 0, 0, 0, 0 } });
        sessions_.erase(it);
        return;
    case kAspCmd:
        s.seq = seq;
        dispatchAfp(s, t, t->req.data() + 4, t->req.size() - 4);
        return;
    case kAspWrite:
        s.seq = seq;
        handleWrite(s, sid, seq, t, t->req.data() + 4, t->req.size() - 4);
        return;
    default:
        return;
    }
}

// ── parameter packers ───────────────────────────────────────────────────

namespace {
struct NodeInfo {
    std::string rel, macName, host;
    bool isDir = false;
    uint64_t dsize = 0;
    int64_t mtime = 0;
    AdMeta ad;
    bool hasAd = false;
};

bool statNode(const std::string& dir, const std::string& rel, NodeInfo& n) {
    n.rel = rel;
    n.host = rel.empty() ? dir : dir + "/" + rel;
    size_t cut = rel.find_last_of('/');
    n.macName = unixToMac(cut == std::string::npos ? rel : rel.substr(cut + 1));
    std::error_code ec;
    fs::file_status fst = fs::status(n.host, ec);
    if (ec || !fs::exists(fst)) return false;
    n.isDir = fs::is_directory(fst);
    if (!n.isDir) n.dsize = fs::file_size(n.host, ec);
    struct stat sb {};
    if (::stat(n.host.c_str(), &sb) == 0) n.mtime = sb.st_mtime;
    n.hasAd = adRead(n.host, n.ad);
    return true;
}

size_t dirOffspring(const std::string& host) {
    size_t c = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(host, ec))
        if (e.path().filename().string()[0] != '.') c++;
    return c;
}
} // namespace

// Params packed in ascending bit order; LNAME is an offset16 (from the
// params-block start) to a trailing pascal string — file.c getmetadata.
static std::vector<uint8_t> packParams(const NodeInfo& n, uint32_t nodeId,
                                       uint32_t parentId, uint16_t bitmap,
                                       bool asDir) {
    std::vector<uint8_t> v;
    int nameFix = -1;
    for (int bit = 0; bit < 16; bit++) {
        if (!(bitmap & (1u << bit))) continue;
        switch (bit) {
        case 0: put16(v, 0); break;                      // attributes
        case 1: put32(v, parentId); break;
        case 2: put32(v, n.hasAd && n.ad.cdate ? n.ad.cdate
                                               : afpDate(n.mtime)); break;
        case 3: put32(v, afpDate(n.mtime)); break;
        case 4: put32(v, n.hasAd ? n.ad.bdate : 0); break;
        case 5: {                                        // Finder info
            const uint8_t* f = n.hasAd ? n.ad.finder : nullptr;
            for (int i = 0; i < 32; i++) v.push_back(f ? f[i] : 0);
            break;
        }
        case 6: nameFix = int(v.size()); put16(v, 0); break;   // long name
        case 7: put16(v, 0); break;                      // short name: none
        case 8: put32(v, nodeId); break;                 // FNUM / DID
        case 9:
            if (asDir) put16(v, uint16_t(dirOffspring(n.host)));
            else put32(v, uint32_t(std::min<uint64_t>(n.dsize, 0xFFFFFFFF)));
            break;
        case 10:
            if (asDir) put32(v, 0);                      // owner ID
            else put32(v, uint32_t(n.ad.rsrc.size()));   // resource length
            break;
        case 11: if (asDir) put32(v, 0); break;          // group ID
        case 12:
            if (asDir) { v.push_back(0x87); v.push_back(0x07);
                         v.push_back(0x07); v.push_back(0x07); }  // all rights
            break;
        default: break;
        }
    }
    if (nameFix >= 0) {
        uint16_t off = uint16_t(v.size());
        v[nameFix] = off >> 8;
        v[nameFix + 1] = uint8_t(off);
        pstr(v, n.macName.empty() ? "" : n.macName);
    }
    return v;
}

static std::vector<uint8_t> packVolParams(uint16_t bitmap,
                                          const std::string& dir,
                                          const std::string& volName) {
    uint32_t bfree = 0x40000000, btotal = 0x7FFFFFFF;
#ifndef _WIN32
    struct statvfs vf {};
    if (::statvfs(dir.c_str(), &vf) == 0) {
        uint64_t f = uint64_t(vf.f_bavail) * vf.f_frsize;
        uint64_t tt = uint64_t(vf.f_blocks) * vf.f_frsize;
        bfree = uint32_t(std::min<uint64_t>(f, 0x7FFFFFFF));
        btotal = uint32_t(std::min<uint64_t>(tt, 0x7FFFFFFF));
    }
#endif
    std::vector<uint8_t> v;
    int nameFix = -1;
    for (int bit = 0; bit < 16; bit++) {
        if (!(bitmap & (1u << bit))) continue;
        switch (bit) {
        case 0: put16(v, 0); break;                      // attributes
        case 1: put16(v, 2); break;                      // signature: fixed DIDs
        case 2: put32(v, afpDate(0)); break;             // creation date
        case 3: put32(v, afpDate(0)); break;
        case 4: put32(v, 0); break;                      // backup date
        case 5: put16(v, kVolId); break;
        case 6: put32(v, bfree); break;
        case 7: put32(v, btotal); break;
        case 8: nameFix = int(v.size()); put16(v, 0); break;
        case 9: put32(v, 0); put32(v, bfree); break;     // ext free (8 B)
        case 10: put32(v, 0); put32(v, btotal); break;
        case 11: put32(v, 512); break;                   // block size
        default: break;
        }
    }
    if (nameFix >= 0) {
        uint16_t off = uint16_t(v.size());
        v[nameFix] = off >> 8;
        v[nameFix + 1] = uint8_t(off);
        pstr(v, volName, 27);
    }
    return v;
}

// ── the AFP command vocabulary ──────────────────────────────────────────

void AfpServer::dispatchAfp(Session& s, std::shared_ptr<AtalkStack::AtpTxn> t,
                            const uint8_t* c, size_t n) {
    if (!n) return;
    stat_.cmdCount++;
    stat_.lastCmd = afpCmdName(c[0]);
    auto err = [&](int32_t e) { aspReply(t, e, {}); };
    auto ok = [&](const std::vector<uint8_t>& d = {}) { aspReply(t, 0, d); };

    switch (c[0]) {

    case 15:                                             // FPGetSrvrInfo
        ok(statusBlock_);
        return;

    case 18: {                                           // FPLogin
        size_t off = 1;
        auto rdP = [&](std::string& out) -> bool {
            if (off >= n) return false;
            size_t l = c[off++];
            if (off + l > n) return false;
            out.assign(reinterpret_cast<const char*>(c + off), l);
            off += l;
            return true;
        };
        std::string vers, uam, user;
        if (!rdP(vers) || !rdP(uam)) { err(kErrParam); return; }
        if (vers.rfind("AFPVersion", 0) != 0 && vers.rfind("AFP", 0) != 0) {
            err(-5003); return;                          // BadVersNum
        }
        rdP(user);                                       // absent for guest
        stat_.lastUser = user.empty() ? "Guest" : user;
        ok();
        return;
    }
    case 20: ok(); return;                               // FPLogout
    case 37: {                                           // FPGetUserInfo
        if (n < 8) { err(kErrParam); return; }
        uint16_t bm = rd16(c + 6);
        std::vector<uint8_t> d;
        put16(d, bm);
        if (bm & 1) put32(d, 100);                       // user ID
        if (bm & 2) put32(d, 100);                       // primary group
        ok(d);
        return;
    }

    case 16: {                                           // FPGetSrvrParms
        std::vector<uint8_t> d;
        put32(d, afpDate(std::time(nullptr)));
        d.push_back(1);                                  // one volume
        d.push_back(0);                                  // flags: no password
        pstr(d, volName_, 27);
        ok(d);
        return;
    }
    case 24: {                                           // FPOpenVol
        if (n < 4) { err(kErrParam); return; }
        uint16_t bm = rd16(c + 2);
        s.volOpen = true;
        std::vector<uint8_t> d;
        put16(d, bm);
        auto p = packVolParams(bm, dir_, volName_);
        d.insert(d.end(), p.begin(), p.end());
        ok(d);
        return;
    }
    case 17: {                                           // FPGetVolParms
        if (n < 6) { err(kErrParam); return; }
        uint16_t bm = rd16(c + 4);
        std::vector<uint8_t> d;
        put16(d, bm);
        auto p = packVolParams(bm, dir_, volName_);
        d.insert(d.end(), p.begin(), p.end());
        ok(d);
        return;
    }
    case 2: s.volOpen = false; ok(); return;             // FPCloseVol
    case 32: ok(); return;                               // FPSetVolParms
    case 10: ok(); return;                               // FPFlush

    case 34: {                                           // FPGetFileDirParms
        if (n < 12) { err(kErrParam); return; }
        uint32_t did = rd32(c + 4);
        uint16_t fbm = rd16(c + 8), dbm = rd16(c + 10);
        size_t used = 0;
        std::string rel;
        int e = resolvePath(did, c + 12, n - 12, used, rel);
        if (e) { err(e); return; }
        NodeInfo ni;
        if (!statNode(dir_, rel, ni)) { err(kErrNoObj); return; }
        // The volume root's name IS the volume name — the Finder labels the
        // desktop icon from the root dir's DIRPBIT_LNAME (statNode leaves it
        // empty for rel="").
        if (rel.empty()) ni.macName = volName_;
        uint32_t id = rel.empty() ? kRootId : idForPath(rel);
        size_t cut = rel.find_last_of('/');
        uint32_t parent = rel.empty() ? 1
                        : idForPath(cut == std::string::npos ? "" : rel.substr(0, cut));
        std::vector<uint8_t> d;
        put16(d, fbm);
        put16(d, dbm);
        d.push_back(ni.isDir ? 0x80 : 0x00);
        d.push_back(0);
        auto p = packParams(ni, id, parent, ni.isDir ? dbm : fbm, ni.isDir);
        d.insert(d.end(), p.begin(), p.end());
        ok(d);
        return;
    }

    case 9: {                                            // FPEnumerate
        if (n < 18) { err(kErrParam); return; }
        uint32_t did = rd32(c + 4);
        uint16_t fbm = rd16(c + 8), dbm = rd16(c + 10);
        uint16_t reqcnt = rd16(c + 12), sindex = rd16(c + 14),
                 maxsz = rd16(c + 16);
        size_t used = 0;
        std::string rel;
        int e = resolvePath(did, c + 18, n - 18, used, rel);
        if (e) { err(e); return; }
        if (!sindex || (!fbm && !dbm)) { err(kErrBitmap); return; }
        std::string host = rel.empty() ? dir_ : dir_ + "/" + rel;
        std::error_code ec;
        if (!fs::is_directory(host, ec)) { err(kErrNoDir); return; }
        uint32_t dirIdNum = rel.empty() ? kRootId : idForPath(rel);

        std::vector<std::string> names;
        for (auto& de : fs::directory_iterator(host, ec)) {
            std::string nm = de.path().filename().string();
            if (!nm.empty() && nm[0] != '.') names.push_back(nm);
        }
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        });

        std::vector<uint8_t> d;
        put16(d, fbm);
        put16(d, dbm);
        put16(d, 0);                                     // patched count
        size_t cap = std::min<size_t>(maxsz ? maxsz : 4000, 4000);
        uint16_t act = 0;
        for (size_t i = sindex - 1; i < names.size() && act < (reqcnt ? reqcnt : 0xFFFF); i++) {
            std::string crel = rel.empty() ? names[i] : rel + "/" + names[i];
            NodeInfo ni;
            if (!statNode(dir_, crel, ni)) continue;
            if (ni.isDir && !dbm) continue;
            if (!ni.isDir && !fbm) continue;
            size_t cut2 = crel.find_last_of('/');
            (void)cut2;
            auto p = packParams(ni, idForPath(crel), dirIdNum,
                                ni.isDir ? dbm : fbm, ni.isDir);
            size_t esz = p.size() + 2;
            if (esz & 1) esz++;
            if (d.size() + esz > cap + 6) break;
            d.push_back(uint8_t(esz));
            d.push_back(ni.isDir ? 0x80 : 0x00);
            d.insert(d.end(), p.begin(), p.end());
            if ((p.size() + 2) & 1) d.push_back(0);
            act++;
        }
        if (!act) { err(kErrNoObj); return; }
        d[4] = act >> 8;
        d[5] = uint8_t(act);
        ok(d);
        return;
    }

    case 6: {                                            // FPCreateDir
        if (n < 8) { err(kErrParam); return; }
        size_t used = 0;
        std::string rel;
        int e = resolvePath(rd32(c + 4), c + 8, n - 8, used, rel);
        if (e) { err(e); return; }
        std::error_code ec;
        std::string host = dir_ + "/" + rel;
        if (fs::exists(host, ec)) { err(kErrExist); return; }
        if (!fs::create_directory(host, ec)) { err(kErrAccess); return; }
        std::vector<uint8_t> d;
        put32(d, idForPath(rel));
        stat_.bytesWritten += 0;
        ok(d);
        return;
    }

    case 7: {                                            // FPCreateFile
        if (n < 8) { err(kErrParam); return; }
        bool hard = (c[1] & 0x80) != 0;
        size_t used = 0;
        std::string rel;
        int e = resolvePath(rd32(c + 4), c + 8, n - 8, used, rel);
        if (e) { err(e); return; }
        std::string host = dir_ + "/" + rel;
        std::error_code ec;
        if (fs::exists(host, ec) && !hard) { err(kErrExist); return; }
        std::ofstream f(host, std::ios::binary | std::ios::trunc);
        if (!f) { err(kErrAccess); return; }
        idForPath(rel);
        ok();
        return;
    }

    case 8: {                                            // FPDelete
        if (n < 8) { err(kErrParam); return; }
        size_t used = 0;
        std::string rel;
        int e = resolvePath(rd32(c + 4), c + 8, n - 8, used, rel);
        if (e) { err(e); return; }
        if (rel.empty()) { err(kErrAccess); return; }
        std::string host = dir_ + "/" + rel;
        std::error_code ec;
        if (fs::is_directory(host, ec)) {
            // Only the AppleDouble sidecar dir may remain inside.
            fs::remove_all(host + "/.AppleDouble", ec);
            if (dirOffspring(host)) { err(kErrDirNotEmpty); return; }
            fs::remove(host, ec);
            if (ec) { err(kErrAccess); return; }
        } else if (fs::exists(host, ec)) {
            fs::remove(host, ec);
            fs::remove(adPath(host), ec);
        } else { err(kErrNoObj); return; }
        dropId(rel);
        ok();
        return;
    }

    case 28: {                                           // FPRename
        if (n < 8) { err(kErrParam); return; }
        size_t u1 = 0, u2 = 0;
        std::string rel, newRel;
        int e = resolvePath(rd32(c + 4), c + 8, n - 8, u1, rel);
        if (e) { err(e); return; }
        e = resolvePath(rd32(c + 4), c + 8 + u1, n - 8 - u1, u2, newRel);
        if (e) { err(e); return; }
        // new name is relative to the same parent
        size_t cut = rel.find_last_of('/');
        std::string parent = cut == std::string::npos ? "" : rel.substr(0, cut);
        size_t ncut = newRel.find_last_of('/');
        std::string leaf = ncut == std::string::npos ? newRel : newRel.substr(ncut + 1);
        std::string dst = parent.empty() ? leaf : parent + "/" + leaf;
        std::error_code ec;
        uint32_t keep = idForPath(rel);
        fs::rename(dir_ + "/" + rel, dir_ + "/" + dst, ec);
        if (ec) { err(kErrNoObj); return; }
        fs::rename(adPath(dir_ + "/" + rel), adPath(dir_ + "/" + dst), ec);
        dropId(rel);
        pathToId_[dst] = keep;
        idToPath_[keep] = dst;
        ok();
        return;
    }

    case 23: {                                           // FPMoveAndRename
        if (n < 12) { err(kErrParam); return; }
        uint32_t sdid = rd32(c + 4), ddid = rd32(c + 8);
        size_t u1 = 0, u2 = 0, u3 = 0;
        std::string src, dstDir, newName;
        int e = resolvePath(sdid, c + 12, n - 12, u1, src);
        if (e) { err(e); return; }
        e = resolvePath(ddid, c + 12 + u1, n - 12 - u1, u2, dstDir);
        if (e) { err(e); return; }
        std::string leaf;
        if (n > 12 + u1 + u2 + 2 && c[12 + u1 + u2 + 1]) {
            std::string nn;
            e = resolvePath(ddid, c + 12 + u1 + u2, n - 12 - u1 - u2, u3, nn);
            if (e) { err(e); return; }
            size_t ncut = nn.find_last_of('/');
            leaf = ncut == std::string::npos ? nn : nn.substr(ncut + 1);
        } else {
            size_t scut = src.find_last_of('/');
            leaf = scut == std::string::npos ? src : src.substr(scut + 1);
        }
        std::string dst = dstDir.empty() ? leaf : dstDir + "/" + leaf;
        std::error_code ec;
        uint32_t keep = idForPath(src);
        fs::rename(dir_ + "/" + src, dir_ + "/" + dst, ec);
        if (ec) { err(kErrNoObj); return; }
        fs::rename(adPath(dir_ + "/" + src), adPath(dir_ + "/" + dst), ec);
        dropId(src);
        pathToId_[dst] = keep;
        idToPath_[keep] = dst;
        ok();
        return;
    }

    case 29: case 30: case 35: {                         // FPSet*Parms
        if (n < 12) { err(kErrParam); return; }
        uint32_t did = rd32(c + 4);
        uint16_t bm = rd16(c + 8);
        size_t used = 0;
        std::string rel;
        int e = resolvePath(did, c + 10, n - 10, used, rel);
        if (e) { err(e); return; }
        size_t off = 10 + used;
        if (off & 1) off++;                              // params are even-aligned
        NodeInfo ni;
        if (!statNode(dir_, rel, ni)) { err(kErrNoObj); return; }
        AdMeta m = ni.ad;
        bool dirty = false;
        for (int bit = 0; bit < 16 && off < n; bit++) {
            if (!(bm & (1u << bit))) continue;
            switch (bit) {
            case 0: off += 2; break;                     // attributes
            case 2: if (off + 4 <= n) { m.cdate = rd32(c + off); dirty = true; } off += 4; break;
            case 3: off += 4; break;                     // mod date: host owns it
            case 4: if (off + 4 <= n) { m.bdate = rd32(c + off); dirty = true; } off += 4; break;
            case 5:
                if (off + 32 <= n) {
                    std::memcpy(m.finder, c + off, 32);
                    m.hasFinder = true;
                    dirty = true;
                }
                off += 32;
                break;
            default: off = n; break;                     // unsupported: stop
            }
        }
        if (dirty && !ni.rel.empty()) adWrite(ni.host, m);
        ok();
        return;
    }

    case 26: {                                           // FPOpenFork
        if (n < 12) { err(kErrParam); return; }
        bool rsrc = (c[1] & 0x80) != 0;
        uint32_t did = rd32(c + 4);
        uint16_t bm = rd16(c + 8), access = rd16(c + 10);
        size_t used = 0;
        std::string rel;
        int e = resolvePath(did, c + 12, n - 12, used, rel);
        if (e) { err(e); return; }
        NodeInfo ni;
        if (!statNode(dir_, rel, ni)) { err(kErrNoObj); return; }
        if (ni.isDir) { err(kErrBadType); return; }
        Fork f;
        f.id = idForPath(rel);
        f.resource = rsrc;
        f.writable = (access & 0x02) != 0;
        uint16_t ref = s.nextFork++;
        s.forks[ref] = f;
        std::vector<uint8_t> d;
        put16(d, bm);
        put16(d, ref);
        size_t cut = rel.find_last_of('/');
        uint32_t parent = idForPath(cut == std::string::npos ? "" : rel.substr(0, cut));
        auto p = packParams(ni, f.id, parent, bm, false);
        d.insert(d.end(), p.begin(), p.end());
        ok(d);
        return;
    }

    case 14: {                                           // FPGetForkParms
        if (n < 6) { err(kErrParam); return; }
        uint16_t ref = rd16(c + 2), bm = rd16(c + 4);
        auto fit = s.forks.find(ref);
        if (fit == s.forks.end()) { err(kErrParam); return; }
        NodeInfo ni;
        if (!statNode(dir_, pathForId(fit->second.id), ni)) { err(kErrNoObj); return; }
        std::vector<uint8_t> d;
        put16(d, bm);
        auto p = packParams(ni, fit->second.id, kRootId, bm, false);
        d.insert(d.end(), p.begin(), p.end());
        ok(d);
        return;
    }

    case 31: {                                           // FPSetForkParms (EOF)
        if (n < 10) { err(kErrParam); return; }
        uint16_t ref = rd16(c + 2), bm = rd16(c + 4);
        uint32_t len = rd32(c + 6);
        auto fit = s.forks.find(ref);
        if (fit == s.forks.end()) { err(kErrParam); return; }
        std::string host = dir_ + "/" + pathForId(fit->second.id);
        if (bm & (1u << 9)) {                            // data fork length
            std::error_code ec;
            fs::resize_file(host, len, ec);
            if (ec) { err(kErrMisc); return; }
        } else if (bm & (1u << 10)) {                    // resource fork length
            AdMeta m;
            adRead(host, m);
            m.rsrc.resize(len, 0);
            adWrite(host, m);
        }
        ok();
        return;
    }

    case 27: {                                           // FPRead
        if (n < 14) { err(kErrParam); return; }
        uint16_t ref = rd16(c + 2);
        uint32_t offR = rd32(c + 4), req = rd32(c + 8);
        uint8_t nlMask = c[12], nlChar = c[13];
        auto fit = s.forks.find(ref);
        if (fit == s.forks.end()) { err(kErrParam); return; }
        std::string host = dir_ + "/" + pathForId(fit->second.id);
        std::vector<uint8_t> d;
        size_t want = std::min<uint32_t>(req, 8 * 578);
        bool eof = false;
        if (!fit->second.resource) {
            std::ifstream in(host, std::ios::binary);
            if (!in) { err(kErrNoObj); return; }
            in.seekg(std::streamoff(offR));
            d.resize(want);
            in.read(reinterpret_cast<char*>(d.data()), std::streamsize(want));
            d.resize(size_t(in.gcount()));
            eof = d.size() < want;
        } else {
            AdMeta m;
            adRead(host, m);
            if (offR < m.rsrc.size()) {
                size_t avail = std::min(want, m.rsrc.size() - offR);
                d.assign(m.rsrc.begin() + offR, m.rsrc.begin() + offR + avail);
            }
            eof = d.size() < want;
        }
        if (nlMask) {
            for (size_t i = 0; i < d.size(); i++)
                if ((d[i] & nlMask) == (nlChar & nlMask)) {
                    d.resize(i + 1);
                    eof = false;
                    break;
                }
        }
        stat_.bytesRead += long(d.size());
        aspReply(t, eof ? kErrEof : 0, d);
        return;
    }

    case 4: {                                            // FPCloseFork
        if (n < 4) { err(kErrParam); return; }
        s.forks.erase(rd16(c + 2));
        ok();
        return;
    }
    case 11: ok(); return;                               // FPFlushFork

    case 1: {                                            // FPByteRangeLock
        if (n < 12) { err(kErrParam); return; }
        std::vector<uint8_t> d;
        put32(d, rd32(c + 4));                           // grant at asked start
        ok(d);
        return;
    }

    case 48: {                                           // FPOpenDT
        std::vector<uint8_t> d;
        put16(d, 1);
        ok(d);
        return;
    }
    case 49: ok(); return;                               // FPCloseDT
    case 53: case 54: case 56: case 57: case 192: ok(); return;   // Add/Rmv APPL-comment-icon
    case 51: case 52: case 55: case 58: err(kErrNoItem); return;  // Get icon/APPL/comment

    default:
        err(kErrNoOp);
        return;
    }
}

// ── ASP SPWrite: pull the payload with WriteContinue, then apply ───────

void AfpServer::handleWrite(Session& s, uint8_t sid, uint16_t seq,
                            std::shared_ptr<AtalkStack::AtpTxn> t,
                            const uint8_t* c, size_t n) {
    if (n < 12 || c[0] != 33) { aspReply(t, kErrParam, {}); return; }
    stat_.cmdCount++;
    stat_.lastCmd = "Write";
    bool fromEof = (c[1] & 0x80) != 0;
    uint16_t ref = rd16(c + 2);
    uint32_t offW = rd32(c + 4), req = rd32(c + 8);
    auto fit = s.forks.find(ref);
    if (fit == s.forks.end() || !fit->second.writable) {
        aspReply(t, kErrAccess, {});
        return;
    }
    Fork fork = fit->second;
    uint16_t avail = uint16_t(std::min<uint32_t>(req, 8 * 578));
    std::vector<uint8_t> wc = { kAspWrtCont, sid, uint8_t(seq >> 8), uint8_t(seq),
                                uint8_t(avail >> 8), uint8_t(avail) };
    std::string host = dir_ + "/" + pathForId(fork.id);
    st_.atpRequest(s.wss, kSss, std::move(wc), 8, true,
        [this, t, fork, host, offW, fromEof](bool okFlag,
                                             std::vector<std::vector<uint8_t>>& pkts) {
            if (!okFlag) { aspReply(t, kErrMisc, {}); return; }
            std::vector<uint8_t> data;
            for (auto& p : pkts)
                if (p.size() > 4) data.insert(data.end(), p.begin() + 4, p.end());
            uint64_t at = offW;
            if (!fork.resource) {
                std::fstream f(host, std::ios::binary | std::ios::in | std::ios::out);
                if (!f) { aspReply(t, kErrAccess, {}); return; }
                if (fromEof) {
                    f.seekp(0, std::ios::end);
                    at = uint64_t(f.tellp()) + offW;
                }
                f.seekp(std::streamoff(at));
                f.write(reinterpret_cast<const char*>(data.data()),
                        std::streamsize(data.size()));
                if (!f) { aspReply(t, kErrMisc, {}); return; }
            } else {
                AdMeta m;
                adRead(host, m);
                if (fromEof) at = m.rsrc.size() + offW;
                if (m.rsrc.size() < at + data.size()) m.rsrc.resize(at + data.size(), 0);
                std::copy(data.begin(), data.end(), m.rsrc.begin() + long(at));
                adWrite(host, m);
            }
            stat_.bytesWritten += long(data.size());
            std::vector<uint8_t> d;
            put32(d, uint32_t(at + data.size()));        // new offset
            aspReply(t, 0, d);
        });
}
