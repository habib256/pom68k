// POM68K — gate `pap_server_test`: the in-process LaserWriter over forged
// PAP/ATP frames. Pins: NBP LaserWriter registration, OpenConn reply
// (socket + quantum + status string), the pull model (the server issues
// SendData credits and the client streams PostScript back), the `*`
// answer to `%%?Begin…Query` lines on the client's read channel, the
// EOF → spool-to-file path, and CloseConn.

#include "PapServer.h"
#include "atalk_test_util.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    std::string spool = "/tmp/pom68k_pap_test_" + std::to_string(::getpid());
    Wire w;
    PapServer pap(w.st);
    pap.configure("POM68K Printer", spool);
    pap.setSpoolToFileOnly(true);
    pap.setEnabled(true);

    // ── the Chooser finds the printer ──
    {
        std::vector<uint8_t> lk = { uint8_t(0x2 << 4 | 1), 0x11 };
        put16v(lk, 2); lk.push_back(47); lk.push_back(100); lk.push_back(0);
        putP(lk, "="); putP(lk, "LaserWriter"); putP(lk, "*");
        w.sendDdp(47, 100, 2, 2, lk, 0xFF);
        bool found = false;
        for (auto& g : w.out)
            if (g.ddpType == 2 && g.dstNode == 47 && (g.pay[0] >> 4) == 3)
                found = true;
        CHECK(found, "NBP finds the LaserWriter");
    }

    // ── OpenConn ──
    {
        w.clear();
        w.atpReq(47, 180, 131, 0x4000, { 1, 1, 0, 0, 180, 8, 0, 0 });
        auto r = w.atpResps(0x4000, 180);
        CHECK(!r.empty() && r[0].size() > 8, "OpenConn reply");
        if (!r.empty() && r[0].size() > 8) {
            CHECK(r[0][1] == 2, "reply function = OpenConnReply");
            CHECK(r[0][2] == 0 && r[0][3] == 0, "printer not busy");
            CHECK(r[0][4] == 131, "server responding socket");
            CHECK(r[0][5] == 8, "flow quantum 8");
        }
        CHECK(pap.status().busy, "connection marks the printer busy");
    }

    // ── the server pulls the job (SendData), we stream PostScript ──
    {
        std::vector<uint8_t> rq;
        int tid = w.findTReq(47, 180, rq);
        CHECK(tid >= 0 && rq.size() >= 4 && rq[1] == 3,
              "server issued SendData after open");
        const char* part1 = "%!PS-Adobe-3.0\n%%?BeginProcSetQuery: foo 1 0\n";
        const char* part2 = "%%EndProcSetQuery\nshowpage\n";
        w.clear();
        std::vector<uint8_t> p1 = { 1, 4, 0, 0 };
        p1.insert(p1.end(), part1, part1 + std::strlen(part1));
        std::vector<uint8_t> p2 = { 1, 4, 1, 0 };            // EOF flag
        p2.insert(p2.end(), part2, part2 + std::strlen(part2));
        w.atpRespond(47, 180, 131, uint16_t(tid), { p1, p2 });
        CHECK(pap.status().jobs == 1, "job spooled at EOF");
        CHECK(!pap.status().busy || pap.status().state == "idle",
              "printer back to idle state");
        bool ok = false;
        for (auto& e : fs::directory_iterator(spool)) {
            std::ifstream f(e.path(), std::ios::binary);
            std::string s((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
            if (s.find("showpage") != std::string::npos) ok = true;
        }
        CHECK(ok, "spool file holds the PostScript");
    }

    // ── the driver reads its query answer ──
    {
        w.clear();
        w.atpReq(47, 180, 131, 0x4001, { 1, 3, 0, 1 });
        auto r = w.atpResps(0x4001, 180);
        CHECK(!r.empty(), "client read answered");
        std::string data;
        bool eof = false;
        for (auto& p : r) {
            if (p.size() >= 3 && p[2]) eof = true;
            if (p.size() > 4) data.append(p.begin() + 4, p.end());
        }
        CHECK(data.find('*') != std::string::npos,
              "query answered with the unknown reply '*'");
        CHECK(eof, "read channel closed after the job");
    }

    // ── CloseConn ──
    {
        w.clear();
        w.atpReq(47, 180, 131, 0x4002, { 1, 6, 0, 0 });
        auto r = w.atpResps(0x4002, 180);
        CHECK(!r.empty() && r[0].size() >= 2 && r[0][1] == 7,
              "CloseConnReply sent");
        CHECK(!pap.status().busy, "connection torn down");
    }

    // ── SendStatus without a connection ──
    {
        w.clear();
        w.atpReq(47, 100, 131, 0x4003, { 0, 8, 0, 0 });
        auto r = w.atpResps(0x4003, 100);
        CHECK(!r.empty() && r[0].size() > 9 && r[0][1] == 9,
              "SendStatus answered");
        std::string s(r[0].begin() + 9, r[0].end());
        CHECK(s.find("idle") != std::string::npos, "status says idle");
    }

    fs::remove_all(spool);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("pap_server_test OK\n");
    return 0;
}
