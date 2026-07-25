// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AtalkStack: the in-process AppleTalk node ──
// One virtual LLAP node living directly on the Scc8530 wire (the same
// attachment point as the LtoUdp cable), carrying everything the external
// TashRouter + netatalk host stack used to provide:
//
//   LLAP    — node presence (lapENQ defence), data frame RX/TX. RTS/CTS
//             stay the cable's business (main.cpp synthesizes the CTS).
//   DDP     — short + long headers; replies mirror the requester's form.
//   Router  — single-segment router-lite: periodic RTMP Data, RTMP
//             Request/Response, ZIP GetNetInfo + GetZoneList/GetMyZone
//             (one zone), NBP BrRq → LkUp conversion. The guest learns
//             its network number and the zone exactly as it did from
//             TashRouter.
//   NBP     — registry for the in-process services (AFPServer,
//             LaserWriter, IPGATEWAY) + LkUp answering.
//   AEP     — echo responder (socket 4).
//   ATP     — transaction engine, both roles: responder (XO cache,
//             release timer, deferred replies for ASP FPWrite) and
//             requester (retries, bitmap fill; ASP tickle / WriteContinue
//             / PAP SendData are server-initiated requests).
//
// Buffers use the netatalk convention: an ATP request/response buffer is
// the 4 ATP user bytes followed by the data (extern/netatalk2 libatalk).
// All timing is emuCycles (cpuHz passed at configure) — no wall clock.
//
// Sources: Inside AppleTalk 2nd ed. (LLAP ch.1, DDP ch.4, RTMP ch.5,
// NBP ch.7, ZIP ch.8, ATP ch.9), extern/netatalk2 include/atalk/*.h,
// extern/tashrouter behaviour, docs/APPLETALK.md.
// Gate: tests/atalk_stack_test.cpp.

#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class AtalkStack {
public:
    struct Addr {
        uint16_t net = 0;
        uint8_t node = 0;
        uint8_t sock = 0;
        bool longHdr = false;        // requester used a long DDP header —
                                     // mirror it in the reply
        bool operator<(const Addr& o) const {
            return (uint32_t(net) << 16 | node << 8 | sock)
                 < (uint32_t(o.net) << 16 | o.node << 8 | o.sock);
        }
    };

    // net/node: this node's address (router + all services live on it —
    // like a host running atalkd/afpd/papd/macipgw on one machine).
    // Node 128 = first server-range ID (LLAP: 128-254 are servers).
    void configure(uint16_t net, uint8_t node, const std::string& zone,
                   int64_t cpuHz);
    uint16_t net() const { return net_; }
    uint8_t node() const { return node_; }
    const std::string& zone() const { return zone_; }
    int64_t cpuHz() const { return cpuHz_; }
    int64_t now() const { return now_; }

    // ── wire side ──
    // Frames leaving this node (LLAP, no FCS) — wire to Scc8530::
    // injectRxFrame (and the LToUDP cable when active).
    std::function<void(const uint8_t*, size_t)> sendFrame;
    // A frame the guest transmitted (Scc8530::onTxFrame payload).
    void onGuestFrame(const uint8_t* d, size_t n);
    // Advance timers (RTMP beacon, ATP retry/release). Call once per
    // emulation slice with the running cycle counter.
    void tick(int64_t nowCycles);

    // ── DDP ──
    using DdpHandler = std::function<void(const Addr& src, uint8_t ddpType,
                                          const uint8_t* p, size_t n)>;
    void bindDdp(uint8_t sock, DdpHandler h) { ddpHandlers_[sock] = std::move(h); }
    void sendDdp(const Addr& dst, uint8_t srcSock, uint8_t ddpType,
                 const uint8_t* p, size_t n);

    // ── NBP registry ──
    void nbpRegister(const std::string& obj, const std::string& type,
                     uint8_t sock);
    void nbpUnregister(const std::string& obj, const std::string& type);
    // Rebroadcast a BrRq onto the segment as a LkUp so EXTERNAL peers
    // (LToUDP netatalk/TashRouter) can answer too. Off by default: when
    // the internal registry is the only responder, the relay broadcast
    // and our own LkUpReply would arrive back-to-back and collide in the
    // guest's Rx FIFO — the classic empty-Chooser bug (APPLETALK.md §2.4).
    // The hub turns it on only while the LToUDP cable is up.
    void setBridgeRelay(bool on) { bridgeRelay_ = on; }

    // ── ATP ──
    // A transaction the responder may complete now or later (ASP WRITE
    // waits for a WriteContinue round-trip before its reply exists).
    class AtpTxn {
    public:
        // pkts: ≤8 response packets, each = 4 user bytes + ≤578 data.
        void respond(std::vector<std::vector<uint8_t>> pkts);
        Addr src;                    // requester (net/node/sock + hdr form)
        std::vector<uint8_t> req;    // 4 user bytes + data
    private:
        friend class AtalkStack;
        AtalkStack* st_ = nullptr;
        uint16_t tid_ = 0;
        uint8_t bitmap_ = 0;
        uint8_t sock_ = 0;           // our responding socket
        bool xo_ = false;
        bool done_ = false;
    };
    using AtpHandler = std::function<void(std::shared_ptr<AtpTxn>)>;
    void bindAtp(uint8_t sock, AtpHandler h) { atpHandlers_[sock] = std::move(h); }
    // Requester: req = 4 user bytes + data. maxResp 0 = fire-and-forget
    // (ASP/PAP tickles). done(ok, pkts) with pkts in netatalk convention.
    void atpRequest(const Addr& dst, uint8_t srcSock,
                    std::vector<uint8_t> req, int maxResp, bool xo,
                    std::function<void(bool, std::vector<std::vector<uint8_t>>&)> done);

    // ── observability (the GUI window reads this) ──
    struct Stats {
        long framesIn = 0, framesOut = 0;   // LLAP data frames
        long ddpIn = 0, ddpOut = 0;
        long nbpLookups = 0;                // LkUp/BrRq answered or relayed
        long atpReqIn = 0;                  // transactions served
        long atpDupReqs = 0;                // XO-cache hits = the client
                                            // RETRANSMITTED after we had
                                            // already answered — the
                                            // copy-stall health indicator:
                                            // 0 = clean
        long atpDupPending = 0;             // retransmits that arrived while
                                            // the ORIGINAL transaction was
                                            // still being served (no reply
                                            // sent yet) = server too slow,
                                            // not a wire loss
        long atpDupLagLastMs = 0;           // guest-clock delay between our
        long atpDupLagMaxMs = 0;            // reply and the retransmit that
                                            // asked for it again. ~1-2 s =
                                            // the client's ATP timer fired
                                            // (reply lost or never played);
                                            // tens of ms = the guest gave up
                                            // early / the reply was mangled
        uint8_t guestNode = 0;              // last node heard on the wire
        int64_t lastGuestCycles = -1;       // emuCycles of that frame
        long enqSeen = 0;                   // guest address probes observed
    };
    const Stats& stats() const { return stats_; }

private:
    struct NbpEntry { std::string obj, type; uint8_t sock; };
    struct PendingReq {              // requester side
        uint16_t tid;
        Addr dst;
        uint8_t srcSock;
        std::vector<uint8_t> req;
        uint8_t need = 0;            // bitmap of packets still missing
        bool xo = false;
        int tries = 0;
        int64_t nextRetry = 0;
        std::vector<std::vector<uint8_t>> resp;
        int respCount = -1;          // set when EOM seen
        std::function<void(bool, std::vector<std::vector<uint8_t>>&)> done;
    };
    struct XoEntry {                 // responder XO cache
        std::vector<std::vector<uint8_t>> pkts;
        Addr src;
        uint8_t sock;
        int64_t expires;
        int64_t sentAt = 0;          // emuCycles the reply was handed to the
                                     // wire — a retransmit's lag is measured
                                     // from here (late vs. lost diagnosis)
    };

    void handleDdp(const Addr& src, uint8_t dstSock, uint8_t type,
                   const uint8_t* p, size_t n);
    void handleNbp(const Addr& src, const uint8_t* p, size_t n);
    void handleZipDdp(const Addr& src, const uint8_t* p, size_t n);
    void handleRtmpReq(const Addr& src, const uint8_t* p, size_t n);
    void handleAtp(const Addr& src, uint8_t dstSock, const uint8_t* p, size_t n);
    void sendAtpResponses(const Addr& dst, uint8_t srcSock, uint16_t tid,
                          const std::vector<std::vector<uint8_t>>& pkts,
                          uint8_t bitmap);
    void sendRtmpData(bool broadcast, const Addr& to);
    void nbpReply(const Addr& to, uint8_t id,
                  const std::vector<const NbpEntry*>& matches);
    void zipAtpHandler(std::shared_ptr<AtpTxn> t);
    static uint64_t txnKey(const Addr& a, uint16_t tid) {
        return uint64_t(a.net) << 32 | uint64_t(a.node) << 24
             | uint64_t(a.sock) << 16 | tid;
    }

    uint16_t net_ = 2;
    uint8_t node_ = 128;
    std::string zone_ = "POM68K";
    int64_t cpuHz_ = 15667200;
    int64_t now_ = 0;
    int64_t nextRtmp_ = 0;
    uint16_t nextTid_ = 1;
    bool bridgeRelay_ = false;

    std::map<uint8_t, DdpHandler> ddpHandlers_;
    std::map<uint8_t, AtpHandler> atpHandlers_;
    std::vector<NbpEntry> nbp_;
    std::map<uint64_t, XoEntry> xoCache_;
    std::map<uint64_t, std::shared_ptr<AtpTxn>> pendingTxns_;
    std::deque<PendingReq> requests_;
    Stats stats_;
};
