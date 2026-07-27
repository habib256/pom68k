// POM68K — gate `atalk_stack_test`: the in-process AppleTalk node
// (AtalkStack) at the LLAP/DDP level. Pins: ENQ defence of the server
// node ID, the RTMP beacon + RTMP Request/Response, ZIP GetNetInfo and
// GetZoneList/GetMyZone over ATP, NBP LkUp answering + BrRq→LkUp relay,
// AEP echo, and the ATP responder's exactly-once cache (a retransmitted
// XO TReq is answered from the cache, not re-executed).

#include "atalk_test_util.h"

int main() {
    Wire w;                                        // node 128, net 2, "POM68K"

    // ── ENQ defence ──
    {
        const uint8_t enq[3] = { 128, 128, 0x81 };  // guest probes OUR ID
        w.st.onGuestFrame(enq, 3);
        bool ack = false;
        for (auto& g : w.out)
            if (g.ddpType == 0x82 && g.dstNode == 128) ack = true;
        CHECK(ack, "ENQ for the stack's node ID draws a lapACK");
        const uint8_t enq2[3] = { 47, 47, 0x81 };   // someone else's probe
        w.clear();
        w.st.onGuestFrame(enq2, 3);
        CHECK(w.out.empty(), "foreign ENQ stays unanswered");
    }

    // ── RTMP beacon + request ──
    {
        w.clear();
        w.run(11);                                  // beacon period: 10 s
        bool beacon = false;
        for (auto& g : w.out)
            if (g.ddpType == 1 && g.dstSock == 1 && g.dstNode == 0xFF
                && g.pay.size() >= 7 && get16(g.pay.data()) == 2)
                beacon = true;
        CHECK(beacon, "RTMP Data broadcast advertises net 2");

        w.clear();
        w.sendDdp(47, 1, 1, 5, { 1 });              // RTMP Request
        bool resp = false;
        for (auto& g : w.out)
            if (g.ddpType == 1 && g.dstNode == 47 && g.pay.size() == 4
                && get16(g.pay.data()) == 2 && g.pay[3] == 128)
                resp = true;
        CHECK(resp, "RTMP Request answered with net/node header");
    }

    // ── ZIP GetNetInfo (DDP) ──
    {
        // Real GetNetInfo header: [5][zh_zero][firstNet16][lastNet16][len][zone].
        // The old fixture emitted [5][len][zone], the same wrong layout the
        // parser assumed, so it could not catch the offset bug.
        w.clear();
        std::vector<uint8_t> gni = { 5, 0, 0, 0, 0, 0 };
        putP(gni, "POM68K");
        w.sendDdp(47, 6, 6, 6, gni, 0xFF);
        bool ok = false;
        for (auto& g : w.out)
            if (g.ddpType == 6 && g.pay.size() > 8 && g.pay[0] == 6) {
                CHECK(!(g.pay[1] & 0x80), "requested zone is valid");
                CHECK(get16(g.pay.data() + 2) == 2, "GNI net range start");
                ok = true;
            }
        CHECK(ok, "GetNetInfo answered");
    }

    // ── ZIP GetNetInfo with a WRONG zone: must come back ZoneInvalid and
    //    carry the default zone so the guest can re-learn it. This is the
    //    case the old fixture's layout made unreachable.
    {
        w.clear();
        std::vector<uint8_t> gni = { 5, 0, 0, 0, 0, 0 };
        putP(gni, "OtherZone");
        w.sendDdp(47, 6, 6, 6, gni, 0xFF);
        bool ok = false;
        for (auto& g : w.out)
            if (g.ddpType == 6 && g.pay.size() > 8 && g.pay[0] == 6) {
                CHECK(g.pay[1] & 0x80, "stale zone flagged ZoneInvalid");
                ok = true;
            }
        CHECK(ok, "GetNetInfo answered for a stale zone");
    }

    // ── ZIP GetZoneList (ATP) ──
    {
        w.clear();
        w.atpReq(47, 100, 6, 0x1001, { 8, 0, 0, 1 });
        auto r = w.atpResps(0x1001, 100);
        CHECK(r.size() == 1 && r[0].size() > 4, "GetZoneList: one packet");
        if (r.size() == 1 && r[0].size() > 4) {
            CHECK(r[0][0] == 1, "zone list: last flag");
            CHECK(get16(r[0].data() + 2) == 1, "zone list: one zone");
            CHECK(r[0][4] == 6 && !std::memcmp(r[0].data() + 5, "POM68K", 6),
                  "zone list carries POM68K");
        }
    }

    // ── NBP: registry answers LkUp, relays BrRq ──
    {
        w.st.nbpRegister("Serveur", "AFPServer", 129);
        w.clear();
        std::vector<uint8_t> lk = { uint8_t(0x2 << 4 | 1), 0x42 };
        put16v(lk, 2); lk.push_back(47); lk.push_back(100); lk.push_back(0);
        putP(lk, "="); putP(lk, "AFPServer"); putP(lk, "*");
        w.sendDdp(47, 100, 2, 2, lk, 0xFF);
        bool reply = false;
        for (auto& g : w.out)
            if (g.ddpType == 2 && g.dstNode == 47 && (g.pay[0] >> 4) == 3) {
                CHECK(g.pay[1] == 0x42, "LkUpReply echoes the NBP id");
                CHECK(g.pay.size() > 15 && g.pay[7] == 7
                      && !std::memcmp(g.pay.data() + 8, "Serveur", 7),
                      "LkUpReply names the registered entity");
                reply = true;
            }
        CHECK(reply, "NBP LkUp answered from the registry");

        // Solo (no cable): BrRq must be answered directly but NOT relayed
        // as a broadcast — the relay + reply would collide in the guest's
        // Rx FIFO (empty-Chooser bug).
        w.clear();
        lk[0] = uint8_t(0x1 << 4 | 1);              // BrRq to the router
        w.sendDdp(47, 100, 2, 2, lk);
        bool relayed = false, answered = false;
        for (auto& g : w.out) {
            if (g.ddpType != 2) continue;
            if (g.dstNode == 0xFF && (g.pay[0] >> 4) == 2) relayed = true;
            if (g.dstNode == 47 && (g.pay[0] >> 4) == 3) answered = true;
        }
        CHECK(!relayed, "BrRq NOT relayed when no external cable (avoids FIFO collision)");
        CHECK(answered, "BrRq answered from the registry");

        // With the bridge relay on (cable up), the segment DOES see the LkUp.
        w.st.setBridgeRelay(true);
        w.clear();
        w.sendDdp(47, 100, 2, 2, lk);
        bool relayed2 = false;
        for (auto& g : w.out)
            if (g.ddpType == 2 && g.dstNode == 0xFF && (g.pay[0] >> 4) == 2)
                relayed2 = true;
        CHECK(relayed2, "BrRq relayed to the segment when the cable is up");
        w.st.setBridgeRelay(false);
    }

    // ── AEP echo ──
    {
        w.clear();
        w.sendDdp(47, 100, 4, 4, { 1, 0xDE, 0xAD });
        bool echo = false;
        for (auto& g : w.out)
            if (g.ddpType == 4 && g.pay.size() == 3 && g.pay[0] == 2
                && g.pay[1] == 0xDE)
                echo = true;
        CHECK(echo, "AEP request echoed");
    }

    // ── ATP XO exactly-once ──
    {
        int calls = 0;
        w.st.bindAtp(140, [&](std::shared_ptr<AtalkStack::AtpTxn> t) {
            calls++;
            t->respond({ { 0, 0, 0, 0, 'o', 'k' } });
        });
        w.clear();
        w.atpReq(47, 101, 140, 0x2002, { 9, 9, 9, 9 });
        w.atpReq(47, 101, 140, 0x2002, { 9, 9, 9, 9 });   // retransmit
        CHECK(calls == 1, "XO retransmit served from cache, handler ran once");
        auto r = w.atpResps(0x2002, 101);
        CHECK(!r.empty() && r[0].size() == 6 && r[0][4] == 'o',
              "XO response delivered");
        // TRel drops the cache → a THIRD send re-executes
        std::vector<uint8_t> rel = { 0xC0, 0, 0x20, 0x02, 0, 0, 0, 0 };
        w.sendDdp(47, 101, 140, 3, rel);
        w.atpReq(47, 101, 140, 0x2002, { 9, 9, 9, 9 });
        CHECK(calls == 2, "after TRel the transaction re-executes");
    }

    // ── stats surface for the GUI ──
    CHECK(w.st.stats().guestNode == 47, "guest node tracked");
    CHECK(w.st.stats().framesIn > 0 && w.st.stats().framesOut > 0,
          "traffic counters run");

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("atalk_stack_test OK\n");
    return 0;
}
