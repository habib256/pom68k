// POM68K — gate `ethertalk_test`: AppleTalk over the DaynaPort card.
//
// `daynaport_test` pins the card's SCSI surface and its IPv4 path; this
// one pins the other protocol family on the same wire — the one that lets
// AppleTalk leave the SCC (EtherTalkLink.h). Everything here is framed the
// way the real Dayna driver framed it, byte for byte off the wire it put
// on the emulated card (2026-09-10, q605_dayna_driver_etalon):
//
//   AARP probe   09:00:07:FF:FF:FF … 00 01 80 9B 06 04 00 03 …
//   RTMP Request 802.3 + AA AA 03 + 08 00 07 + 80 9B + long DDP, type 5
//
// No ROM, no disk image, no host sockets: this gate always runs.

#include "AtalkStack.h"
#include "DaynaPort.h"
#include "EtherTalkLink.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); failures++; }
}
#define CHECK(cond, what) check((cond), (what))

constexpr uint16_t kNet = 2;          // the node's network
constexpr uint8_t kNode = 128;        // and its node number
constexpr uint16_t kStartupNet = 0xFFF9;   // where a router-less Mac lands
constexpr uint8_t kGuestNode = 1;
const std::array<uint8_t, 6> kGuestMac = { 0x00, 0x80, 0x19, 0x10, 0x98, 0xE3 };
const std::array<uint8_t, 6> kBroadcast = { 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };

struct RxFrame {
    bool ok = false;
    std::vector<uint8_t> data;
};

RxFrame readOne(DaynaPort& nic) {
    std::vector<uint8_t> out, none;
    const uint8_t cdb[6] = { 0x08, 0, 0, 0, 0x00, 0xC0 };
    RxFrame r;
    if (nic.command(cdb, 6, out, none) != 0 || out.size() < 6) return r;
    const size_t len = size_t((out[0] << 8) | out[1]);
    if (len < 4 || out.size() < 6 + len) return r;      // empty ring
    r.ok = true;
    r.data.assign(out.begin() + 6, out.begin() + 6 + len - 4);  // drop the FCS
    return r;
}

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

// 802.3 + LLC/SNAP, the way EtherTalk frames everything.
std::vector<uint8_t> snapFrame(const std::array<uint8_t, 6>& dst,
                               const std::array<uint8_t, 6>& src, bool ddp,
                               const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.insert(f.end(), dst.begin(), dst.end());
    f.insert(f.end(), src.begin(), src.end());
    put16(f, uint16_t(8 + payload.size()));
    f.push_back(0xAA); f.push_back(0xAA); f.push_back(0x03);
    if (ddp) { f.push_back(0x08); f.push_back(0x00); f.push_back(0x07); }
    else     { f.push_back(0x00); f.push_back(0x00); f.push_back(0x00); }
    put16(f, ddp ? 0x809B : 0x80F3);
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

std::vector<uint8_t> aarp(uint16_t function, const std::array<uint8_t, 6>& senderHw,
                          uint16_t senderNet, uint8_t senderNode,
                          uint16_t targetNet, uint8_t targetNode) {
    std::vector<uint8_t> p;
    put16(p, 0x0001);
    put16(p, 0x809B);
    p.push_back(6);
    p.push_back(4);
    put16(p, function);
    p.insert(p.end(), senderHw.begin(), senderHw.end());
    p.push_back(0); put16(p, senderNet); p.push_back(senderNode);
    for (int i = 0; i < 6; i++) p.push_back(0);
    p.push_back(0); put16(p, targetNet); p.push_back(targetNode);
    return p;
}

// Long DDP, the only header form EtherTalk has.
std::vector<uint8_t> longDdp(uint16_t dstNet, uint8_t dstNode, uint8_t dstSock,
                             uint16_t srcNet, uint8_t srcNode, uint8_t srcSock,
                             uint8_t ddpType, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> p;
    p.push_back(0); p.push_back(0);
    put16(p, 0);
    put16(p, dstNet);
    put16(p, srcNet);
    p.push_back(dstNode);
    p.push_back(srcNode);
    p.push_back(dstSock);
    p.push_back(srcSock);
    p.push_back(ddpType);
    p.insert(p.end(), data.begin(), data.end());
    const uint16_t len = uint16_t(p.size());
    p[0] = uint8_t(len >> 8);
    p[1] = uint8_t(len);
    return p;
}

bool isSnap(const std::vector<uint8_t>& f, uint16_t type) {
    return f.size() >= 22 && f[14] == 0xAA && f[15] == 0xAA && f[16] == 0x03 &&
           uint16_t(f[20] << 8 | f[21]) == type;
}

// The router beacons on its own schedule, so the frame a check is about is
// not always the next one out of the ring: read until it appears.
// ddpType < 0 means "any" (and is the only sensible ask for AARP).
RxFrame nextFrame(DaynaPort& nic, uint16_t snapType, int ddpType = -1) {
    for (int i = 0; i < 8; i++) {
        RxFrame r = readOne(nic);
        if (!r.ok) return RxFrame{};
        if (!isSnap(r.data, snapType)) continue;
        if (ddpType < 0) return r;
        if (r.data.size() > 22 + 12 && r.data[22 + 12] == uint8_t(ddpType))
            return r;
    }
    return RxFrame{};
}

// Everything currently queued, thrown away.
void drain(DaynaPort& nic) { while (readOne(nic).ok) {} }
}  // namespace

int main() {
    DaynaPort nic;
    nic.attach();
    std::vector<uint8_t> out, none;
    const uint8_t enable[6] = { 0x0E, 0, 0, 0, 0, 0x80 };
    nic.command(enable, 6, out, none);

    AtalkStack stack;
    stack.configure(kNet, kNode, "POM68K", 1000000);
    EtherTalkLink link(nic, stack);
    stack.sendFrame = [&](const uint8_t* d, size_t n) { link.onStackFrame(d, n); };
    // 1 ms of a 1 MHz machine = 1000 cycles; a 10 s beacon.
    link.configure(1000, 10 * 1000000);
    int64_t now = 0;

    // ── AARP: this node defends its own address and no other ────────────
    {
        const auto probe = snapFrame(kBroadcast, kGuestMac, false,
                                     aarp(3, kGuestMac, kStartupNet, kGuestNode,
                                          kStartupNet, kGuestNode));
        link.onGuestFrame(probe.data(), probe.size());
        link.tick(now += 1000);
        // The router's first beacon goes out on that same tick.
        CHECK(!nextFrame(nic, 0x80F3).ok,
              "a probe for an address this node does not hold is ignored");
        CHECK(link.aarpProbesSeen == 1, "the probe was seen");
        drain(nic);

        const auto forUs = snapFrame(kBroadcast, kGuestMac, false,
                                     aarp(3, kGuestMac, kStartupNet, kGuestNode,
                                          kNet, kNode));
        link.onGuestFrame(forUs.data(), forUs.size());
        CHECK(!readOne(nic).ok, "the answer waits for the segment's latency");
        link.tick(now += 1000);
        const RxFrame r = nextFrame(nic, 0x80F3);
        CHECK(r.ok, "a probe for THIS node is answered");
        if (r.ok && r.data.size() >= 50) {
            CHECK(uint16_t(r.data[28] << 8 | r.data[29]) == 2,
                  "and the answer is an AARP Response");
            CHECK(uint16_t(r.data[37] << 8 | r.data[38]) == kNet &&
                  r.data[39] == kNode,
                  "carrying this node's own AppleTalk address");
            CHECK(std::memcmp(&r.data[30], link.mac().data(), 6) == 0,
                  "and its own MAC");
            CHECK(std::memcmp(r.data.data(), kGuestMac.data(), 6) == 0,
                  "addressed to the node that asked");
        }
    }

    // ── RTMP: the router's answer is what moves a Mac off the startup
    //    range, and on an extended network it must carry the RANGE ──────
    {
        const auto req = snapFrame(kBroadcast, kGuestMac, true,
                                   longDdp(0, 0xFF, 1, kStartupNet, kGuestNode, 1,
                                           5, {}));
        drain(nic);
        link.onGuestFrame(req.data(), req.size());
        link.tick(now += 1000);
        const RxFrame r = nextFrame(nic, 0x809B, 1);
        CHECK(r.ok, "an RTMP Request is answered");
        if (r.ok && r.data.size() >= 22 + 21) {
            const uint8_t* ddp = r.data.data() + 22;
            CHECK(ddp[12] == 1, "with RTMP Data");
            CHECK(uint16_t(ddp[13] << 8 | ddp[14]) == kNet, "from this network");
            CHECK(ddp[15] == 8 && ddp[16] == kNode, "naming this router");
            CHECK(uint16_t(ddp[17] << 8 | ddp[18]) == kNet &&
                  ddp[19] == 0x80 &&
                  uint16_t(ddp[20] << 8 | ddp[21]) == kNet && ddp[22] == 0x82,
                  "and the extended range tuple, distance 0, RTMP version 2");
        }
        CHECK(link.rtmpAnswers == 1, "the request was answered once");
    }

    // ── DDP both ways: the stack answers an NBP lookup it can serve ─────
    {
        stack.nbpRegister("POM68K", "AFPServer", 251);
        std::vector<uint8_t> nbp;
        nbp.push_back(0x21);                       // LkUp, one tuple
        nbp.push_back(0x07);                       // NBP transaction id
        put16(nbp, kNet);                          // requester: net,
        nbp.push_back(kGuestNode);                 // node,
        nbp.push_back(0xFE);                       // socket
        nbp.push_back(0);                          // enumerator
        auto pstr = [&](const std::string& s) {
            nbp.push_back(uint8_t(s.size()));
            nbp.insert(nbp.end(), s.begin(), s.end());
        };
        pstr("=");
        pstr("AFPServer");
        pstr("*");
        const auto lookup = snapFrame(kBroadcast, kGuestMac, true,
                                      longDdp(kNet, 0xFF, 2, kNet, kGuestNode,
                                              0xFE, 2, nbp));
        drain(nic);
        link.onGuestFrame(lookup.data(), lookup.size());
        link.tick(now += 1000);
        const RxFrame r = nextFrame(nic, 0x809B, 2);
        CHECK(r.ok, "an NBP lookup is answered");
        if (r.ok && r.data.size() >= 22 + 14) {
            const uint8_t* ddp = r.data.data() + 22;
            CHECK(ddp[12] == 2, "with a DDP type 2 (NBP) datagram");
            CHECK((ddp[13] >> 4) == 3, "an NBP LkUp-Reply");
            CHECK(uint16_t(ddp[6] << 8 | ddp[7]) == kNet && ddp[9] == kNode,
                  "from this node on this network");
            CHECK(std::memcmp(r.data.data(), kGuestMac.data(), 6) == 0,
                  "addressed to the asker's own MAC, learned from its frames");
        }
        // The guest spoke from the startup range first and from the real
        // network second: one MAC, one address, the old mapping gone.
        CHECK(link.knownNodes() == 1, "the asker is in the mapping table, once");
    }

    // ── the beacon ──────────────────────────────────────────────────────
    {
        drain(nic);
        const long before = link.rtmpBeacons;
        link.tick(now += 10 * 1000000);
        link.tick(now += 1000);
        CHECK(link.rtmpBeacons > before, "the router beacons on its own");
        const RxFrame r = nextFrame(nic, 0x809B, 1);
        CHECK(r.ok && std::memcmp(r.data.data(), kBroadcast.data(), 6) == 0,
              "the beacon is RTMP Data to the AppleTalk broadcast group");
    }

    if (failures) {
        std::printf("ethertalk_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ethertalk_test: AARP, extended RTMP and DDP over 802.3/SNAP, "
                "gate passed\n");
    return 0;
}
