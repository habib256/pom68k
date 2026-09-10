// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── EtherLink: the wire between a DaynaPort and the in-process NAT ──
// `DaynaPort` moves Ethernet frames; `MacIpGateway` moves IP datagrams and
// already owns the whole user-mode NAT (TCP endpoint, UDP flows, DNS, ICMP
// echo). The three things that sit between them are the Ethernet header, ARP
// and the two MAC addresses — which is all this class is.
//
//   guest → DaynaPort::sendFrame → EtherLink → MacIpGateway::ipFromEther
//   host  → MacIpGateway ether sink → EtherLink → DaynaPort::receiveFrame
//
// ARP is answered as a PROXY for the whole subnet, not just for the gateway
// address. The gateway is the only thing on this segment, so every address
// the guest can reach is behind it; answering only for the router address
// would work for a correctly-configured MacTCP and fail silently for one
// whose subnet mask makes it think a host is local. Two addresses are never
// answered, and both matter: the guest's own (an answer reads as a duplicate
// address and MacTCP refuses to come up) and the sender's own (a gratuitous
// ARP / probe, same reason).
//
// The guest is configured by hand — an address in the gateway's subnet, the
// gateway as router, and a DNS server. There is no BOOTP/RARP responder
// here; MacIP's ATP address handout has no Ethernet equivalent, and adding a
// second address-assignment protocol is a bigger decision than this seam.
//
// Sources: RFC 826 (ARP), RFC 894 (IP over Ethernet). Gate:
// tests/daynaport_test.cpp.

#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

class DaynaPort;
class MacIpGateway;

class EtherLink {
public:
    EtherLink(DaynaPort& nic, MacIpGateway& gw) : nic_(nic), gw_(gw) {}

    // Wire the two callbacks. Call once, after both objects exist.
    void attach();

    // The gateway's own MAC, locally administered (bit 1 of byte 0). It has
    // to differ from the card's or the guest ARPs itself.
    const std::array<std::uint8_t, 6>& gatewayMac() const { return gwMac_; }
    // Last MAC seen sending from the guest — the destination for everything
    // the NAT sends back. Zero until the guest has spoken.
    const std::array<std::uint8_t, 6>& guestMac() const { return guestMac_; }

    // ── the segment's own latency ───────────────────────────────────────
    // A gateway on the other end of a wire cannot answer INSIDE the guest's
    // own send call, and this one did: `MacIpGateway` replies synchronously,
    // so an echo reply reached the Rx ring before the WRITE(6) that carried
    // the request had finished, and the driver READ it back in the same SCSI
    // transaction. Apple's MacTCP Ping 2.0.2 then reported "timeout" for
    // every one of its own replies (2026-09-10, `q605_dayna_driver_etalon`):
    // valid frames, verified checksums, delivered too early to be matched.
    // Frames to the guest therefore wait `cycles` of MACHINE time — the
    // owner sets it (`AtalkHub::attach`: 1 ms of the machine's own clock),
    // and 0 keeps the immediate path for a unit test that owns no clock.
    void setLatency(std::int64_t cycles) { latency_ = cycles > 0 ? cycles : 0; }
    std::int64_t latency() const { return latency_; }
    // Release everything now due. Machine cycles, like every other deadline.
    void tick(std::int64_t now);
    std::size_t inFlight() const { return wire_.size(); }

    // Entry points (public so a test can drive them without the callbacks).
    void onGuestFrame(const std::uint8_t* d, std::size_t n);
    void ipToGuest(std::uint32_t dstIp, const std::vector<std::uint8_t>& pkt);

    long arpRequests = 0, arpReplies = 0, ipToGuestFrames = 0, ipFromGuestFrames = 0;
    long wireDrops = 0;                  // queued past kMaxInFlight

private:
    // A queue this deep is already a stall, not a wire: the guest has
    // stopped reading, and the Rx ring's own ceiling is the next stop.
    static constexpr std::size_t kMaxInFlight = 64;

    void handleArp(const std::uint8_t* p, std::size_t n);
    void deliver(std::vector<std::uint8_t>&& f);
    void sendToGuest(const std::array<std::uint8_t, 6>& dst,
                     std::uint16_t ethType,
                     const std::uint8_t* payload, std::size_t n);

    DaynaPort& nic_;
    MacIpGateway& gw_;
    std::array<std::uint8_t, 6> gwMac_ = { 0x02, 0x00, 0x4B, 0x36, 0x38, 0x01 };
    std::array<std::uint8_t, 6> guestMac_ = {};
    std::uint32_t guestIp_ = 0;          // learned from ARP / IP source
    std::int64_t latency_ = 0;           // machine cycles, set by the owner
    std::int64_t now_ = 0;               // last tick, machine cycles
    std::deque<std::pair<std::int64_t, std::vector<std::uint8_t>>> wire_;
};
