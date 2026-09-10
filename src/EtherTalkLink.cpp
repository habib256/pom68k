// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// EtherTalk Phase 2 framing, AARP and the extended RTMP beacon. Provenance
// and the shape of the seam are in EtherTalkLink.h.

#include "EtherTalkLink.h"

#include "AtalkStack.h"
#include "DaynaPort.h"

#include <cstring>

namespace {
constexpr std::uint8_t kLlcSnap[3] = { 0xAA, 0xAA, 0x03 };
constexpr std::uint8_t kOuiApple[3] = { 0x08, 0x00, 0x07 };
constexpr std::uint8_t kOuiZero[3] = { 0x00, 0x00, 0x00 };

// AARP functions (Inside AppleTalk ch.2).
constexpr std::uint16_t kAarpRequest = 1, kAarpResponse = 2, kAarpProbe = 3;
// DDP types this class reads or replaces.
constexpr std::uint8_t kDdpRtmpData = 1, kDdpRtmpRequest = 5;
constexpr std::uint8_t kSockRtmp = 1;
// LLAP frame types the stack emits.
constexpr std::uint8_t kLlapShortDdp = 0x01, kLlapLongDdp = 0x02;
constexpr std::size_t kLongDdpHdr = 13, kShortDdpHdr = 5;

std::uint16_t rd16(const std::uint8_t* p) {
    return std::uint16_t(p[0] << 8 | p[1]);
}
void wr16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(std::uint8_t(x >> 8));
    v.push_back(std::uint8_t(x));
}
}  // namespace

bool EtherTalkLink::isAppleTalk(const std::uint8_t* d, std::size_t n,
                                std::uint16_t* snapType) {
    if (!d || n < kEthHdr + kSnapHdr) return false;
    if (rd16(d + 12) > 1500) return false;                  // DIX, not 802.3
    if (std::memcmp(d + kEthHdr, kLlcSnap, 3) != 0) return false;
    const std::uint16_t type = rd16(d + kEthHdr + 6);
    const bool ddp = type == kSnapDdp &&
                     std::memcmp(d + kEthHdr + 3, kOuiApple, 3) == 0;
    const bool aarp = type == kSnapAarp &&
                      std::memcmp(d + kEthHdr + 3, kOuiZero, 3) == 0;
    if (!ddp && !aarp) return false;
    if (snapType) *snapType = type;
    return true;
}

// ── out ─────────────────────────────────────────────────────────────────
void EtherTalkLink::send(const std::array<std::uint8_t, 6>& dstMac, bool ddp,
                         const std::uint8_t* payload, std::size_t n) {
    std::vector<std::uint8_t> f;
    f.reserve(kEthHdr + kSnapHdr + n);
    f.insert(f.end(), dstMac.begin(), dstMac.end());
    f.insert(f.end(), mac_.begin(), mac_.end());
    // 802.3 length counts the LLC/SNAP header and the payload, not itself.
    wr16(f, std::uint16_t(kSnapHdr + n));
    f.insert(f.end(), kLlcSnap, kLlcSnap + 3);
    const std::uint8_t* oui = ddp ? kOuiApple : kOuiZero;
    f.insert(f.end(), oui, oui + 3);
    wr16(f, ddp ? kSnapDdp : kSnapAarp);
    f.insert(f.end(), payload, payload + n);
    if (!latency_) {
        nic_.receiveFrame(f.data(), f.size());
        return;
    }
    if (wire_.size() >= kMaxInFlight) { wireDrops++; return; }
    wire_.emplace_back(now_ + latency_, std::move(f));
}

void EtherTalkLink::learn(std::uint16_t net, std::uint8_t node,
                          const std::uint8_t* mac) {
    if (!node || node == 0xFF) return;             // not a node address
    if (net == stack_.net() && node == stack_.node()) return;   // ourselves
    std::array<std::uint8_t, 6> m{};
    std::memcpy(m.data(), mac, 6);
    // One MAC holds one AppleTalk address at a time. A Macintosh that
    // finds a router MOVES: it drops the startup-range address it probed
    // ($FF00-$FFFE) for one on the real network, and the stale mapping
    // would otherwise sit in this table forever.
    for (auto it = amt_.begin(); it != amt_.end();)
        it = (it->second == m && it->first != std::pair<std::uint16_t,
              std::uint8_t>{ net, node }) ? amt_.erase(it) : std::next(it);
    amt_[{ net, node }] = m;
}

std::array<std::uint8_t, 6> EtherTalkLink::macFor(std::uint16_t net,
                                                  std::uint8_t node) const {
    const auto it = amt_.find({ net, node });
    if (it != amt_.end()) return it->second;
    // An unknown node is reached through the broadcast group rather than
    // through an AARP Request and a queue: the segment holds one guest.
    return kAtalkBroadcast;
}

void EtherTalkLink::sendAarp(std::uint16_t function,
                             const std::array<std::uint8_t, 6>& dstMac,
                             const std::array<std::uint8_t, 6>& targetHw,
                             std::uint16_t targetNet, std::uint8_t targetNode) {
    std::vector<std::uint8_t> p;
    wr16(p, 0x0001);                               // hardware: Ethernet
    wr16(p, kSnapDdp);                             // protocol: AppleTalk
    p.push_back(6);                                // hardware address length
    p.push_back(4);                                // protocol address length
    wr16(p, function);
    p.insert(p.end(), mac_.begin(), mac_.end());   // sender hardware
    p.push_back(0);                                // AppleTalk address: pad,
    wr16(p, stack_.net());                         // net,
    p.push_back(stack_.node());                    // node
    p.insert(p.end(), targetHw.begin(), targetHw.end());
    p.push_back(0);
    wr16(p, targetNet);
    p.push_back(targetNode);
    send(dstMac, false, p.data(), p.size());
    if (function == kAarpResponse) aarpReplies++;
    if (function == kAarpRequest || function == kAarpProbe) aarpRequests++;
}

// RTMP Data for an EXTENDED network: the router's address, then the
// network-range tuple. The distance byte carries $80 to say "this tuple is
// a range", and $82 closes it with the RTMP version (Inside AppleTalk
// ch.5, netatalk etc/atalkd/rtmp.c). AtalkStack's own RTMP Data is the
// non-extended LocalTalk form, which a node on an extended network
// discards — this is why that one is dropped on this segment.
void EtherTalkLink::sendRtmpData(const std::array<std::uint8_t, 6>& dstMac) {
    const std::uint16_t net = stack_.net();
    std::vector<std::uint8_t> ddp;
    ddp.reserve(kLongDdpHdr + 8);
    ddp.push_back(0); ddp.push_back(0);            // length, filled below
    wr16(ddp, 0);                                  // no DDP checksum
    wr16(ddp, 0);                                  // dest net: 0 = this one
    wr16(ddp, net);                                // source net
    ddp.push_back(0xFF);                           // dest node: broadcast
    ddp.push_back(stack_.node());
    ddp.push_back(kSockRtmp);                      // dest socket: RTMP
    ddp.push_back(kSockRtmp);
    ddp.push_back(kDdpRtmpData);
    wr16(ddp, net);                                // router's own net
    ddp.push_back(8);                              // node ID length, bits
    ddp.push_back(stack_.node());
    wr16(ddp, net);                                // range start
    ddp.push_back(0x80);                           // distance 0, extended
    wr16(ddp, net);                                // range end
    ddp.push_back(0x82);                           // RTMP version 2
    const std::uint16_t len = std::uint16_t(ddp.size());
    ddp[0] = std::uint8_t(len >> 8);               // hop count 0 + length
    ddp[1] = std::uint8_t(len);
    send(dstMac, true, ddp.data(), ddp.size());
}

// ── in ──────────────────────────────────────────────────────────────────
void EtherTalkLink::handleAarp(const std::uint8_t* p, std::size_t n) {
    if (n < 28) return;
    if (rd16(p) != 0x0001 || rd16(p + 2) != kSnapDdp) return;
    if (p[4] != 6 || p[5] != 4) return;
    const std::uint16_t function = rd16(p + 6);
    const std::uint8_t* senderHw = p + 8;
    const std::uint16_t senderNet = rd16(p + 15);
    const std::uint8_t senderNode = p[17];
    const std::uint16_t targetNet = rd16(p + 25);
    const std::uint8_t targetNode = p[27];

    const bool forUs = targetNet == stack_.net() && targetNode == stack_.node();
    std::array<std::uint8_t, 6> senderMac{};
    std::memcpy(senderMac.data(), senderHw, 6);

    switch (function) {
    case kAarpProbe:
        aarpProbesSeen++;
        // A probe is "is this address taken?". Answering for an address
        // this node does not hold would keep the guest out of the network
        // it is trying to join; answering for one it DOES hold is the
        // whole point of the protocol.
        if (forUs) sendAarp(kAarpResponse, senderMac, senderMac,
                            senderNet, senderNode);
        break;
    case kAarpRequest:
        if (forUs) sendAarp(kAarpResponse, senderMac, senderMac,
                            senderNet, senderNode);
        learn(senderNet, senderNode, senderHw);
        break;
    case kAarpResponse:
        learn(senderNet, senderNode, senderHw);
        break;
    default:
        break;
    }
}

void EtherTalkLink::onGuestFrame(const std::uint8_t* d, std::size_t n) {
    std::uint16_t snap = 0;
    if (!isAppleTalk(d, n, &snap)) return;
    const std::uint8_t* p = d + kEthHdr + kSnapHdr;
    const std::size_t plen = n - kEthHdr - kSnapHdr;
    if (snap == kSnapAarp) { handleAarp(p, plen); return; }

    if (plen < kLongDdpHdr) return;
    const std::size_t ddpLen = rd16(p) & 0x03FF;
    if (ddpLen < kLongDdpHdr || ddpLen > plen) return;
    const std::uint16_t srcNet = rd16(p + 6);
    const std::uint8_t dstNode = p[8], srcNode = p[9];
    learn(srcNet, srcNode, d + 6);
    ddpFromGuest++;

    // An RTMP Request is a question only a router answers, and the answer
    // that matters on an extended network is the range — so it is answered
    // here rather than by the stack's non-extended responder.
    if (p[12] == kDdpRtmpRequest) {
        std::array<std::uint8_t, 6> src{};
        std::memcpy(src.data(), d + 6, 6);
        sendRtmpData(src);
        rtmpAnswers++;
        return;
    }

    // Everything else is the node's business. The stack reads LLAP frames:
    // two address bytes, the frame type, then the datagram it already
    // knows how to parse.
    std::vector<std::uint8_t> llap;
    llap.reserve(3 + ddpLen);
    llap.push_back(dstNode);
    llap.push_back(srcNode);
    llap.push_back(kLlapLongDdp);
    llap.insert(llap.end(), p, p + ddpLen);
    stack_.onGuestFrame(llap.data(), llap.size());
}

void EtherTalkLink::onStackFrame(const std::uint8_t* d, std::size_t n) {
    if (!d || n < 3 + kShortDdpHdr) return;
    const std::uint8_t dstNode = d[0];
    const std::uint8_t kind = d[2];
    const std::uint8_t* ddp = d + 3;
    const std::size_t ddpLen = n - 3;

    std::vector<std::uint8_t> out;
    if (kind == kLlapLongDdp) {
        if (ddpLen < kLongDdpHdr) return;
        // The stack's non-extended RTMP Data would tell a node on an
        // extended network something it cannot use (EtherTalkLink.h).
        if (ddp[12] == kDdpRtmpData) return;
        out.assign(ddp, ddp + ddpLen);
    } else if (kind == kLlapShortDdp) {
        // No short header exists on EtherTalk: widen it, filling in the
        // net numbers the LLAP form left implicit.
        if (ddpLen < kShortDdpHdr) return;
        const std::size_t dataLen = ddpLen - kShortDdpHdr;
        out.reserve(kLongDdpHdr + dataLen);
        out.push_back(0); out.push_back(0);
        wr16(out, 0);                              // no DDP checksum
        wr16(out, stack_.net());                   // dest net
        wr16(out, stack_.net());                   // source net
        out.push_back(dstNode);
        out.push_back(d[1]);
        out.push_back(ddp[2]);                     // dest socket
        out.push_back(ddp[3]);                     // source socket
        out.push_back(ddp[4]);                     // DDP type
        out.insert(out.end(), ddp + kShortDdpHdr, ddp + ddpLen);
        if (out[12] == kDdpRtmpData) return;
    } else {
        return;                                    // lapENQ and friends
    }
    const std::uint16_t len = std::uint16_t(out.size());
    out[0] = std::uint8_t((out[0] & 0xFC) | (len >> 8));
    out[1] = std::uint8_t(len);
    ddpToGuest++;
    send(dstNode == 0xFF ? kAtalkBroadcast : macFor(stack_.net(), dstNode),
         true, out.data(), out.size());
}

void EtherTalkLink::tick(std::int64_t now) {
    now_ = now;
    if (rtmpPeriod_ && now_ >= nextRtmp_) {
        // A router beacons whether or not anyone has spoken: that beacon is
        // how a node that missed its own RTMP Request still finds the
        // network range.
        sendRtmpData(kAtalkBroadcast);
        rtmpBeacons++;
        nextRtmp_ = now_ + rtmpPeriod_;
    }
    while (!wire_.empty() && wire_.front().first <= now_) {
        const std::vector<std::uint8_t>& f = wire_.front().second;
        nic_.receiveFrame(f.data(), f.size());
        wire_.pop_front();
    }
}
