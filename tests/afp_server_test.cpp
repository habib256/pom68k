// POM68K — gate `afp_server_test`: the in-process AppleShare server over
// forged ASP/ATP frames — the same bytes a System 7 Chooser + Finder
// put on the wire. Pins: GetStatus block, OpenSession, FPLogin,
// FPOpenVol/FPGetSrvrParms, FPEnumerate, FPOpenFork + FPRead (EOF rule:
// short read returns the data WITH kFPEOFErr), the ASP SPWrite →
// server-initiated WriteContinue → FPWrite path (both ATP roles), the
// resource fork landing in a netatalk-style .AppleDouble sidecar,
// FPCreateDir, and the server tickle keep-alive.

#include "AfpServer.h"
#include "atalk_test_util.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
uint16_t g_tid = 0x3000;

// One ASP command round-trip: returns the AFP result, fills reply.
int32_t aspCmd(Wire& w, uint8_t sid, uint16_t seq,
               const std::vector<uint8_t>& cmd, std::vector<uint8_t>& reply,
               uint8_t func = 2) {
    uint16_t tid = g_tid++;
    std::vector<uint8_t> req = { func, sid, uint8_t(seq >> 8), uint8_t(seq) };
    req.insert(req.end(), cmd.begin(), cmd.end());
    w.clear();
    w.atpReq(47, 201, 130, tid, req);
    return w.aspResult(tid, 201, reply);
}
} // namespace

int main() {
    std::string dir = "/tmp/pom68k_afp_test_" + std::to_string(::getpid());
    fs::create_directories(dir + "/SubDir");
    { std::ofstream f(dir + "/a.txt"); f << "Bonjour"; }

    Wire w;
    AfpServer afp(w.st);
    afp.configure("POM68K", "Partage", dir);
    afp.setEnabled(true);
    CHECK(afp.status().dirOk, "shared folder detected writable");

    // ── GetStatus ──
    {
        w.clear();
        w.atpReq(47, 100, 129, 0x2F00, { 3, 0, 0, 0 });
        std::vector<uint8_t> d;
        CHECK(w.aspResult(0x2F00, 100, d) == 0, "GetStatus answers");
        CHECK(d.size() > 16 && d[10] == 6 && !std::memcmp(d.data() + 11, "POM68K", 6),
              "status block carries the server name");
    }

    // ── OpenSession ──
    uint8_t sid = 0;
    {
        w.clear();
        w.atpReq(47, 200, 129, 0x2F01, { 4, 200, 1, 0 });
        auto r = w.atpResps(0x2F01, 200);
        CHECK(r.size() == 1 && r[0].size() >= 4, "OpenSess reply");
        if (!r.empty() && r[0].size() >= 4) {
            CHECK(r[0][0] == 130, "OpenSess names the session socket");
            sid = r[0][1];
            CHECK(sid != 0, "session id allocated");
        }
    }
    CHECK(afp.status().sessions == 1, "one live session");

    std::vector<uint8_t> d;
    uint16_t seq = 0;

    // ── FPLogin (guest) ──
    {
        std::vector<uint8_t> cmd = { 18 };
        putP(cmd, "AFPVersion 2.1");
        putP(cmd, "No User Authent");
        CHECK(aspCmd(w, sid, seq++, cmd, d) == 0, "guest FPLogin accepted");
    }

    // ── FPOpenVol ──
    uint16_t vid = 0;
    {
        std::vector<uint8_t> cmd = { 24, 0 };
        put16v(cmd, 0x0120);                       // VID | NAME
        putP(cmd, "Partage");
        CHECK(aspCmd(w, sid, seq++, cmd, d) == 0, "FPOpenVol succeeds");
        CHECK(d.size() >= 4, "OpenVol reply has params");
        if (d.size() >= 4) {
            vid = get16(d.data() + 2);             // first param bit: VID
            CHECK(vid == 1, "volume id is 1");
        }
    }
    CHECK(afp.status().volMounted, "volume flagged mounted");

    // ── FPGetSrvrParms ──
    {
        CHECK(aspCmd(w, sid, seq++, { 16, 0 }, d) == 0, "FPGetSrvrParms");
        CHECK(d.size() >= 6 && d[4] == 1, "one volume listed");
        CHECK(d.size() > 6 && d[6] == 7 && !std::memcmp(d.data() + 7, "Partage", 7),
              "volume name in the list");
    }

    // ── FPGetFileDirParms on the root: its name must be the volume name
    //    (the Finder labels the desktop icon from this) ──
    {
        std::vector<uint8_t> cmd = { 34, 0 };
        put16v(cmd, vid);
        put32v(cmd, 2);                            // root DID
        put16v(cmd, 0);                            // file bitmap (n/a: it's a dir)
        put16v(cmd, 0x0040);                       // dir bitmap: LNAME
        cmd.push_back(2); cmd.push_back(0);        // long-name path, empty = root
        CHECK(aspCmd(w, sid, seq++, cmd, d) == 0, "FPGetFileDirParms(root)");
        // reply: fbm(2) dbm(2) isFileDir(2) then params; LNAME = offset16 → pstr
        CHECK(d.size() > 8 && (d[4] & 0x80), "root reported as a directory");
        const uint8_t* pr = d.data() + 6;
        uint16_t noff = get16(pr);
        std::string rootName(reinterpret_cast<const char*>(pr + noff + 1), pr[noff]);
        CHECK(rootName == "Partage", "root long-name == volume name (desktop label)");
    }

    // ── FPEnumerate on the root ──
    {
        std::vector<uint8_t> cmd = { 9, 0 };
        put16v(cmd, vid);
        put32v(cmd, 2);                            // root DID
        put16v(cmd, 0x0140);                       // files: LNAME | FNUM
        put16v(cmd, 0x0140);                       // dirs: LNAME | DID
        put16v(cmd, 32);                           // req count
        put16v(cmd, 1);                            // start index
        put16v(cmd, 2000);                         // max reply
        cmd.push_back(2); cmd.push_back(0);        // long-name path, empty
        CHECK(aspCmd(w, sid, seq++, cmd, d) == 0, "FPEnumerate root");
        CHECK(d.size() > 6 && get16(d.data() + 4) == 2, "two entries (a.txt, SubDir)");
        bool sawFile = false, sawDir = false;
        size_t off = 6;
        for (int i = 0; i < 2 && off + 2 < d.size(); i++) {
            uint8_t len = d[off], isDir = d[off + 1];
            const uint8_t* p = d.data() + off + 2;
            uint16_t nameOff = get16(p);
            std::string nm(reinterpret_cast<const char*>(p + nameOff + 1),
                           p[nameOff]);
            if (isDir & 0x80) { sawDir = nm == "SubDir"; }
            else { sawFile = nm == "a.txt"; }
            off += len;
        }
        CHECK(sawFile, "a.txt enumerated with its long name");
        CHECK(sawDir, "SubDir enumerated as a directory");
    }

    // ── FPOpenFork + FPRead ──
    uint16_t ref = 0;
    {
        std::vector<uint8_t> cmd = { 26, 0 };
        put16v(cmd, vid);
        put32v(cmd, 2);
        put16v(cmd, 0);                            // no params wanted
        put16v(cmd, 0x0003);                       // read | write
        cmd.push_back(2); putP(cmd, "a.txt");
        CHECK(aspCmd(w, sid, seq++, cmd, d) == 0, "FPOpenFork(data)");
        CHECK(d.size() >= 4, "fork reply");
        ref = get16(d.data() + 2);

        std::vector<uint8_t> rd = { 27, 0 };
        put16v(rd, ref);
        put32v(rd, 0);
        put32v(rd, 100);
        rd.push_back(0); rd.push_back(0);
        int32_t res = aspCmd(w, sid, seq++, rd, d);
        CHECK(res == -5009, "short read returns kFPEOFErr");
        CHECK(d.size() == 7 && !std::memcmp(d.data(), "Bonjour", 7),
              "FPRead delivers the file body");
    }

    // ── FPWrite via ASP SPWrite + WriteContinue ──
    {
        uint16_t tid = g_tid++;
        std::vector<uint8_t> req = { 6, sid, uint8_t(seq >> 8), uint8_t(seq) };
        req.push_back(33); req.push_back(0);       // FPWrite, at offset
        put16v(req, ref);
        put32v(req, 0);
        put32v(req, 5);
        w.clear();
        w.atpReq(47, 201, 130, tid, req);
        // the server must now ask for the data on the WSS (socket 200)
        int wtid = -1;
        std::vector<uint8_t> wreq;
        for (auto& g : w.out)
            if (g.dstNode == 47 && g.dstSock == 200 && g.ddpType == 3
                && g.pay.size() >= 8 && (g.pay[0] & 0xC0) == 0x40
                && g.pay[4] == 7)                  // ASP WriteContinue
                { wtid = get16(g.pay.data() + 2); wreq.assign(g.pay.begin() + 4, g.pay.end()); }
        CHECK(wtid >= 0, "server issued WriteContinue to the WSS");
        if (wtid >= 0) {
            CHECK(wreq.size() >= 6 && get16(wreq.data() + 4) >= 5,
                  "WriteContinue advertises buffer space");
            w.atpRespond(47, 200, 130, uint16_t(wtid),
                         { { 0, 0, 0, 0, 'S', 'a', 'l', 'u', 't' } });
            std::vector<uint8_t> wr;
            CHECK(w.aspResult(tid, 201, wr) == 0, "FPWrite completed");
            CHECK(wr.size() == 4 && get32(wr.data()) == 5, "new offset = 5");
        }
        seq++;
        std::ifstream f(dir + "/a.txt");
        std::string s((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
        CHECK(s == "Salutur", "write landed over the old bytes");
    }

    // The 16 MiB ceiling belongs to resource forks only. A data write that
    // crosses it must remain legal (and sparse on the host), otherwise Finder
    // copies of ordinary files fail part-way through with paramErr.
    {
        constexpr uint32_t kAt = 16u * 1024u * 1024u - 1u;
        uint16_t tid = g_tid++;
        std::vector<uint8_t> req = { 6, sid, uint8_t(seq >> 8), uint8_t(seq) };
        req.push_back(33); req.push_back(0);
        put16v(req, ref);
        put32v(req, kAt);
        put32v(req, 2);
        w.clear();
        w.atpReq(47, 201, 130, tid, req);
        int wtid = -1;
        for (auto& g : w.out)
            if (g.dstNode == 47 && g.dstSock == 200 && g.ddpType == 3
                && g.pay.size() >= 8 && (g.pay[0] & 0xC0) == 0x40 && g.pay[4] == 7)
                wtid = get16(g.pay.data() + 2);
        CHECK(wtid >= 0, "large data-fork write pulls a WriteContinue");
        if (wtid >= 0) {
            w.atpRespond(47, 200, 130, uint16_t(wtid),
                         { { 0, 0, 0, 0, 'X', 'Y' } });
            std::vector<uint8_t> wr;
            CHECK(w.aspResult(tid, 201, wr) == 0,
                  "data fork may cross the resource-fork 16 MiB ceiling");
        }
        seq++;
        CHECK(fs::file_size(dir + "/a.txt") == uint64_t(kAt) + 2,
              "large data-fork write reached the host file");
    }

    // ── resource fork → .AppleDouble sidecar ──
    {
        std::vector<uint8_t> cmd = { 26, uint8_t(0x80) };
        put16v(cmd, vid);
        put32v(cmd, 2);
        put16v(cmd, 0);
        put16v(cmd, 0x0003);
        cmd.push_back(2); putP(cmd, "a.txt");
        CHECK(aspCmd(w, sid, seq++, cmd, d) == 0, "FPOpenFork(resource)");
        uint16_t rref = get16(d.data() + 2);

        uint16_t tid = g_tid++;
        std::vector<uint8_t> req = { 6, sid, uint8_t(seq >> 8), uint8_t(seq) };
        req.push_back(33); req.push_back(0);
        put16v(req, rref);
        put32v(req, 0);
        put32v(req, 4);
        w.clear();
        w.atpReq(47, 201, 130, tid, req);
        int wtid = -1;
        for (auto& g : w.out)
            if (g.dstNode == 47 && g.dstSock == 200 && g.ddpType == 3
                && g.pay.size() >= 8 && (g.pay[0] & 0xC0) == 0x40 && g.pay[4] == 7)
                wtid = get16(g.pay.data() + 2);
        CHECK(wtid >= 0, "resource write pulls a WriteContinue");
        if (wtid >= 0) {
            w.atpRespond(47, 200, 130, uint16_t(wtid),
                         { { 0, 0, 0, 0, 'R', 'S', 'R', 'C' } });
            std::vector<uint8_t> wr;
            CHECK(w.aspResult(tid, 201, wr) == 0, "resource FPWrite completed");
        }
        seq++;
        CHECK(fs::exists(dir + "/.AppleDouble/a.txt"),
              "AppleDouble sidecar created");
    }

    // ── FPCreateDir ──
    {
        std::vector<uint8_t> cmd = { 6, 0 };
        put16v(cmd, vid);
        put32v(cmd, 2);
        cmd.push_back(2); putP(cmd, "Nouveau");
        CHECK(aspCmd(w, sid, seq++, cmd, d) == 0, "FPCreateDir");
        CHECK(d.size() == 4 && get32(d.data()) >= 16, "new DID returned");
        CHECK(fs::is_directory(dir + "/Nouveau"), "directory exists on host");
    }

    // ── server tickle keeps the session fed ──
    {
        w.clear();
        for (int i = 0; i < 350; i++) {            // 35 s > tickle period
            w.now += w.hz / 10;
            w.st.tick(w.now);
            afp.tick(w.now);
        }
        bool tickled = false;
        for (auto& g : w.out)
            if (g.dstNode == 47 && g.dstSock == 200 && g.ddpType == 3
                && g.pay.size() >= 8 && g.pay[4] == 5)
                tickled = true;
        CHECK(tickled, "server sends SPTickle to the workstation");
        CHECK(afp.status().sessions == 1, "session survives (client silence < 2 min)");
    }

    fs::remove_all(dir);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("afp_server_test OK\n");
    return 0;
}
