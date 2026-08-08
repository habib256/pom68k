// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Ethernet framing + ARP between DaynaPort and MacIpGateway. Design note and
// the proxy-ARP rationale: EtherLink.h.

#include "EtherLink.h"
#include "DaynaPort.h"
#include "MacIpGateway.h"
#include <cstring>

namespace {
constexpr std::size_t kEthHdr = 14;
constexpr std::uint16_t kEtherTypeIp = 0x0800;
constexpr std::uint16_t kEtherTypeArp = 0x0806;

std::uint16_t be16(const std::uint8_t* p) { return std::uint16_t((p[0] << 8) | p[1]); }
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
         | (std::uint32_t(p[2]) << 8) | p[3];
}
void wr16(std::uint8_t* p, std::uint16_t v) { p[0] = std::uint8_t(v >> 8); p[1] = std::uint8_t(v); }
void wr32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16);
    p[2] = std::uint8_t(v >> 8);  p[3] = std::uint8_t(v);
}
} // namespace

void EtherLink::attach() {
    nic_.sendFrame = [this](const std::uint8_t* d, std::size_t n) {
        onGuestFrame(d, n);
    };
    gw_.setEtherSink([this](std::uint32_t dstIp, const std::vector<std::uint8_t>& pkt) {
        ipToGuest(dstIp, pkt);
    });
}

void EtherLink::sendToGuest(const std::array<std::uint8_t, 6>& dst,
                            std::uint16_t ethType,
                            const std::uint8_t* payload, std::size_t n) {
    std::vector<std::uint8_t> f(kEthHdr + n);
    std::memcpy(f.data(), dst.data(), 6);
    std::memcpy(f.data() + 6, gwMac_.data(), 6);
    wr16(f.data() + 12, ethType);
    if (n) std::memcpy(f.data() + kEthHdr, payload, n);
    nic_.receiveFrame(f.data(), f.size());
}

void EtherLink::onGuestFrame(const std::uint8_t* d, std::size_t n) {
    if (!d || n < kEthHdr) return;
    // Learn the guest's MAC from its source address, never from the
    // destination: a broadcast frame's destination is FF:FF:FF:FF:FF:FF and
    // replying there would work by accident until it stopped.
    std::memcpy(guestMac_.data(), d + 6, 6);

    const std::uint16_t type = be16(d + 12);
    if (type == kEtherTypeArp) { handleArp(d + kEthHdr, n - kEthHdr); return; }
    if (type != kEtherTypeIp) return;         // no IPX, no EtherTalk (yet)

    ipFromGuestFrames++;
    const std::uint8_t* ip = d + kEthHdr;
    const std::size_t ipLen = n - kEthHdr;
    if (ipLen >= 20 && (ip[0] >> 4) == 4) guestIp_ = be32(ip + 12);
    gw_.ipFromEther(ip, ipLen);
}

// RFC 826. Only Ethernet/IPv4 requests are answered; everything else falls
// on the floor, which is what a host with no matching protocol does.
void EtherLink::handleArp(const std::uint8_t* p, std::size_t n) {
    if (n < 28) return;
    if (be16(p) != 1 || be16(p + 2) != kEtherTypeIp) return;   // Ethernet/IPv4
    if (p[4] != 6 || p[5] != 4) return;                        // address sizes
    const std::uint16_t op = be16(p + 6);
    const std::uint32_t senderIp = be32(p + 14);
    const std::uint32_t targetIp = be32(p + 24);

    if (senderIp) guestIp_ = senderIp;
    if (op != 1) return;                                       // requests only
    arpRequests++;

    // Never answer for an address that IS (or may become) the guest's own:
    // MacTCP reads a reply to its own probe as a duplicate address and
    // refuses to initialise.
    if (targetIp == senderIp) return;                          // gratuitous/probe
    if (targetIp == guestIp_) return;
    if (gw_.leased(targetIp)) return;
    // Proxy for the whole subnet — the gateway is the only thing out here.
    if ((targetIp & gw_.netmask()) != (gw_.gwIp() & gw_.netmask())
        && targetIp != gw_.gwIp() && targetIp != gw_.dnsIp())
        return;

    std::uint8_t r[28] = {};
    wr16(r, 1);                                 // hardware type: Ethernet
    wr16(r + 2, kEtherTypeIp);                  // protocol type: IPv4
    r[4] = 6; r[5] = 4;
    wr16(r + 6, 2);                             // op: reply
    std::memcpy(r + 8, gwMac_.data(), 6);       // sender hardware
    wr32(r + 14, targetIp);                     // sender protocol (the asked-for IP)
    std::memcpy(r + 18, p + 8, 6);              // target hardware = the asker
    wr32(r + 24, senderIp);                     // target protocol
    std::array<std::uint8_t, 6> dst{};
    std::memcpy(dst.data(), p + 8, 6);
    sendToGuest(dst, kEtherTypeArp, r, sizeof r);
    arpReplies++;
}

void EtherLink::ipToGuest(std::uint32_t dstIp, const std::vector<std::uint8_t>& pkt) {
    // Nothing to address a frame to until the guest has spoken once. The NAT
    // only ever answers traffic the guest started, so this cannot lose a
    // datagram that mattered.
    static const std::array<std::uint8_t, 6> zero{};
    if (guestMac_ == zero || pkt.empty()) return;
    if (dstIp && guestIp_ && dstIp != guestIp_) return;
    ipToGuestFrames++;
    sendToGuest(guestMac_, kEtherTypeIp, pkt.data(), pkt.size());
}
