// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── EtherTalkLink: AppleTalk over the DaynaPort, not over the SCC ──
// `EtherLink` carries IPv4 and ARP between the card and the NAT. This is
// its AppleTalk half: the two SNAP protocols an EtherTalk Phase 2 node
// speaks, and the framing that lets the existing `AtalkStack` node answer
// on the Ethernet segment without knowing it is there.
//
//   guest → DaynaPort::sendFrame → (hub demux) → onGuestFrame → AtalkStack
//   AtalkStack::sendFrame → onStackFrame → wire_ → DaynaPort::receiveFrame
//
// What EtherTalk adds over LLAP, and why each piece is here:
//
//   • **802.3 + LLC/SNAP framing.** DDP rides `AA AA 03` + OUI `08 00 07`
//     + type `$809B`; AARP rides the same LLC with OUI `00 00 00` + type
//     `$80F3`. Only the LONG DDP header exists here (there is no LLAP
//     node byte to be short about), so a short header from the stack is
//     widened on the way out.
//   • **AARP** (Inside AppleTalk ch.2): a node claims an address by
//     probing it and keeps it if nobody objects, and resolves a peer's
//     address to a MAC with a Request. This class answers for THIS node's
//     address and for nothing else, and learns every mapping it sees —
//     from probes, requests, responses and from the source of any DDP.
//   • **Extended RTMP** (ch.5). A Macintosh with no router picks a
//     provisional address in the startup range ($FF00-$FFFE) — measured:
//     Dayna's own driver probes `$FFF9.1` ten times, then asks for a
//     router with an RTMP Request, and settles there. The router's answer
//     is what moves it onto a real network, and on an EXTENDED network
//     that answer must carry the network RANGE: `AtalkStack`'s RTMP Data
//     is the non-extended LocalTalk form, so this class emits the
//     extended one and drops the stack's on this segment.
//
// Zones, NBP, ATP, ASP and AFP need nothing of their own: they are DDP,
// and the stack already speaks them. ZIP GetNetInfo is answered by the
// stack too — its reply already sets UseBroadcast, which is what makes a
// zero-length zone multicast address legal.
//
// Sources: Inside AppleTalk 2nd ed. (AARP ch.2, EtherTalk ch.3, DDP ch.4,
// RTMP ch.5), RFC 1742 §3, extern/netatalk2 etc/atalkd, tashrouter.
// Gate: tests/ethertalk_test.cpp, and the real driver in
// tests/q605_dayna_driver_etalon.cpp.

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>

class DaynaPort;
class AtalkStack;

class EtherTalkLink {
public:
    EtherTalkLink(DaynaPort& nic, AtalkStack& stack) : nic_(nic), stack_(stack) {}

    // The AppleTalk broadcast group. Every node listens to it; the zone
    // multicast ($09:00:07:00:00:xx) is not used while GetNetInfo says
    // UseBroadcast.
    static constexpr std::array<std::uint8_t, 6> kAtalkBroadcast =
        { 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };

    // This node's own MAC on the segment. Locally administered, and it
    // must differ from the card's or the guest resolves itself.
    const std::array<std::uint8_t, 6>& mac() const { return mac_; }

    // Machine cycles, like every other deadline here: `latency` is how long
    // a frame waits before the card can see it (a gateway that answers
    // inside the guest's own send call is not a wire — see EtherLink.h),
    // and `rtmpPeriod` is the router beacon.
    void configure(std::int64_t latency, std::int64_t rtmpPeriod) {
        latency_ = latency > 0 ? latency : 0;
        rtmpPeriod_ = rtmpPeriod > 0 ? rtmpPeriod : 0;
    }

    // A frame the guest transmitted, already known to be AppleTalk.
    void onGuestFrame(const std::uint8_t* d, std::size_t n);
    // A frame the node emitted, in the stack's LLAP shape
    // (dst node, src node, type, DDP…).
    void onStackFrame(const std::uint8_t* d, std::size_t n);
    // Release what is due and beacon RTMP.
    void tick(std::int64_t now);

    // True once the guest has claimed an address on this segment.
    bool guestPresent() const { return !amt_.empty(); }
    std::size_t knownNodes() const { return amt_.size(); }

    long aarpProbesSeen = 0, aarpRequests = 0, aarpReplies = 0;
    long ddpToGuest = 0, ddpFromGuest = 0, rtmpBeacons = 0, rtmpAnswers = 0;
    long wireDrops = 0;

    // Only for a gate that wants to look at a frame before the card does.
    static bool isAppleTalk(const std::uint8_t* d, std::size_t n,
                            std::uint16_t* snapType = nullptr);

private:
    static constexpr std::size_t kEthHdr = 14;   // dst, src, length
    static constexpr std::size_t kSnapHdr = 8;   // AA AA 03 + OUI + type
    static constexpr std::size_t kMaxInFlight = 64;
    static constexpr std::uint16_t kSnapDdp = 0x809B;
    static constexpr std::uint16_t kSnapAarp = 0x80F3;

    void handleAarp(const std::uint8_t* p, std::size_t n);
    void sendAarp(std::uint16_t function, const std::array<std::uint8_t, 6>& dstMac,
                  const std::array<std::uint8_t, 6>& targetHw,
                  std::uint16_t targetNet, std::uint8_t targetNode);
    void sendRtmpData(const std::array<std::uint8_t, 6>& dstMac);
    void send(const std::array<std::uint8_t, 6>& dstMac, bool ddp,
              const std::uint8_t* payload, std::size_t n);
    void learn(std::uint16_t net, std::uint8_t node,
               const std::uint8_t* mac);
    // The MAC to address a datagram to, broadcast when the node is unknown.
    std::array<std::uint8_t, 6> macFor(std::uint16_t net, std::uint8_t node) const;

    DaynaPort& nic_;
    AtalkStack& stack_;
    std::array<std::uint8_t, 6> mac_ = { 0x02, 0x00, 0x4B, 0x36, 0x38, 0x02 };
    std::map<std::pair<std::uint16_t, std::uint8_t>,
             std::array<std::uint8_t, 6>> amt_;   // address mapping table
    std::int64_t latency_ = 0, rtmpPeriod_ = 0;
    std::int64_t now_ = 0, nextRtmp_ = 0;
    std::deque<std::pair<std::int64_t, std::vector<std::uint8_t>>> wire_;
};
