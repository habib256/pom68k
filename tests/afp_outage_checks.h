// Delayed WriteContinue responses must not outlive their session or fork.
#pragma once

void outageWriteChecks(const std::string& parent) {
    for (bool resource : {false, true}) for (int stop = 0; stop < 8; ++stop) {
        const fs::path dir = fs::path(parent) /
            ("Outage" + std::to_string(resource) + std::to_string(stop));
        CHECK(afplive::seed(dir), "seed outage fixture with both forks");
        Wire wire;
        auto server = std::make_unique<AfpServer>(wire.st);
        server->configure("POM68K", "Partage", dir.string());
        server->setEnabled(true);
        const auto openTid = g_tid++;
        wire.atpReq(47, 200, 129, openTid, {4, 200, 1, 0});
        const auto packets = wire.atpResps(openTid, 200);
        CHECK(!packets.empty() && packets[0].size() >= 4, "outage session opens");
        if (packets.empty() || packets[0].size() < 4) continue;
        const auto sid = packets[0][1];
        uint16_t seq = 0;
        std::vector<uint8_t> reply, login{18};
        putP(login, "AFPVersion 2.1"); putP(login, "No User Authent");
        CHECK(aspCmd(wire, sid, seq++, login, reply) == 0, "outage login");
        std::vector<uint8_t> cmd{26, uint8_t(resource ? 0x80 : 0)};
        put16v(cmd, 1); put32v(cmd, 2); put16v(cmd, 0); put16v(cmd, 3);
        cmd.push_back(2); putP(cmd, "BONJOUR.txt");
        CHECK(aspCmd(wire, sid, seq++, cmd, reply) == 0 && reply.size() >= 4,
              "outage opens writable fork");
        if (reply.size() < 4) continue;
        const auto ref = get16(reply.data() + 2);
        std::vector<uint8_t> req{6, sid, uint8_t(seq >> 8), uint8_t(seq), 33, 0};
        ++seq;
        put16v(req, ref); put32v(req, 0); put32v(req, 4);
        wire.clear();
        const auto writeTid = g_tid++;
        wire.atpReq(47, 201, 130, writeTid, req);
        int continuation = -1;
        for (const auto& frame : wire.out)
            if (frame.dstNode == 47 && frame.dstSock == 200 && frame.ddpType == 3 &&
                frame.pay.size() >= 8 && (frame.pay[0] & 0xc0) == 0x40 && frame.pay[4] == 7)
                continuation = get16(frame.pay.data() + 2);
        CHECK(continuation >= 0, "outage interrupts an outstanding WriteContinue");
        const auto bytesBefore = server->status().bytesWritten;
        if (stop == 0) {
            const auto previousSocket = server->status().listeningSocket;
            server->setEnabled(false);
            server->setEnabled(true);
            const auto currentSocket = server->status().listeningSocket;
            CHECK(currentSocket != previousSocket && currentSocket != 130 && currentSocket != 131,
                  "restart changes the listener without colliding with ASP/PAP sockets");
            std::vector<uint8_t> lookup{0x21, 0x4a, 0, 2, 47, 200, 0};
            putP(lookup, "POM68K"); putP(lookup, "AFPServer"); putP(lookup, "*");
            wire.clear(); wire.sendDdp(47, 200, 2, 2, lookup);
            bool advertised = false;
            for (const auto& frame : wire.out)
                if (frame.ddpType == 2 && frame.pay.size() >= 8 && (frame.pay[0] >> 4) == 3)
                    advertised = frame.pay[5] == currentSocket;
            CHECK(advertised, "NBP advertises the restarted listener's fresh socket");
        } else if (stop == 1) {
            CHECK(aspCmd(wire, sid, seq++, {}, reply, 1) == 0, "close interrupted session");
        } else if (stop == 2) {
            std::vector<uint8_t> close{4, 0}; put16v(close, ref);
            CHECK(aspCmd(wire, sid, seq++, close, reply) == 0, "close interrupted fork");
        } else if (stop == 3) {
            // Expire ASP without expiring the pending ATP request first.
            server->tick(wire.now + 121 * wire.hz);
        } else if (stop == 4) {
            CHECK(aspCmd(wire, sid, seq++, {2, 0, 0, 1}, reply) == 0,
                  "close interrupted volume");
        } else if (stop == 5) {
            CHECK(aspCmd(wire, sid, seq++, {20, 0}, reply) == 0,
                  "logout during interrupted write");
        } else if (stop == 6) {
            server->configure("POM68K", "Partage", dir.string());
        } else {
            server.reset();
            // The transport survives the server. Even a new server on the
            // same sockets and volume cannot inherit the old continuation.
            server = std::make_unique<AfpServer>(wire.st);
            server->configure("POM68K", "Partage", dir.string());
            server->setEnabled(true);
            const auto reopenedTid = g_tid++;
            wire.atpReq(47, 200, 129, reopenedTid, {4, 200, 1, 0});
            const auto reopened = wire.atpResps(reopenedTid, 200);
            CHECK(!reopened.empty() && reopened[0].size() >= 4 && reopened[0][1] == sid,
                  "replacement server reuses the old numeric session id");
            CHECK(aspCmd(wire, sid, 0, login, reply) == 0 &&
                  aspCmd(wire, sid, 1, cmd, reply) == 0 && reply.size() >= 4 &&
                  get16(reply.data() + 2) == ref,
                  "replacement server reuses the old numeric fork reference");
        }
        if (continuation >= 0)
            wire.atpRespond(47, 200, 130, uint16_t(continuation),
                            {{0, 0, 0, 0, 'L', 'A', 'T', 'E'}});
        CHECK(afplive::exactCopy(dir / "BONJOUR.txt"),
              "late response preserves both committed forks after interruption");
        CHECK(server->status().bytesWritten == bytesBefore,
              "late response is not counted as a successful write");
        CHECK(wire.aspResult(writeTid, 201, reply) == -1072,
              "interrupted write reports closed ASP session");
        if (stop == 0 || stop == 1 || stop == 3 || stop == 6) {
            const auto staleTid = g_tid++;
            wire.clear();
            wire.atpReq(47, 201, 130, staleTid, {2, sid, 0, 42, 16, 0});
            CHECK(wire.atpResps(staleTid, 201).empty(),
                  "unknown session stays silent so the guest timeout can expire");
            wire.clear();
            wire.atpReq(47, 200, server->status().listeningSocket, g_tid++,
                        {5, sid, 0, 0}, false, 0);
            std::vector<uint8_t> close;
            const int closeTid = wire.findTReq(47, 200, close);
            CHECK(closeTid >= 0 && close == std::vector<uint8_t>({1, sid, 0, 0}),
                  "unknown session tickle receives a real ASP CloseSess on its WSS");
            if (closeTid >= 0)
                wire.atpRespond(47, 200, 130, uint16_t(closeTid), {{0, 0, 0, 0}});
            CHECK(aspCmd(wire, sid, 0, {}, reply, 1) == 0,
                  "closing an already-gone session still acknowledges local teardown");
        }
    }
}
