// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Ethernet framing + ARP between DaynaPort and MacIpGateway. Design note and
// the proxy-ARP rationale: EtherLink.h.

#include "EtherLink.h"
#include "DaynaPort.h"
#include "MacIpGateway.h"
#include <algorithm>
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
std::uint16_t checksum(const std::uint8_t* p, std::size_t n) {
    std::uint32_t sum = 0;
    while (n >= 2) { sum += be16(p); p += 2; n -= 2; }
    if (n) sum += std::uint32_t(*p) << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return std::uint16_t(~sum);
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

// Built by appending ranges, never by memcpy into a freshly sized vector:
// GCC 13's LTO -Wstringop-overflow pass lost that allocation and reported
// the 6-byte MAC copy as writing into a region of size 0 (the SaveState.h
// false-positive class, 2026-09-01 census), from lto1 where a source pragma
// no longer reaches. The nightly LTO build carries -Werror since 2026-09-16.
void EtherLink::sendToGuest(const std::array<std::uint8_t, 6>& dst,
                            std::uint16_t ethType,
                            const std::uint8_t* payload, std::size_t n) {
    std::uint8_t hdr[kEthHdr];
    std::copy(dst.begin(), dst.end(), hdr);
    std::copy(gwMac_.begin(), gwMac_.end(), hdr + 6);
    wr16(hdr + 12, ethType);
    std::vector<std::uint8_t> f;
    f.reserve(kEthHdr + n);
    f.insert(f.end(), hdr, hdr + kEthHdr);
    if (n) f.insert(f.end(), payload, payload + n);
    deliver(std::move(f));
}

// Straight to the card when the owner set no latency (a unit test with no
// clock); otherwise onto the wire, to be released by tick().
void EtherLink::deliver(std::vector<std::uint8_t>&& f) {
    if (!uplink()) return;                 // no cable, nothing reaches the card
    if (!latency_) {
        nic_.receiveFrame(f.data(), f.size());
        return;
    }
    if (wire_.size() >= kMaxInFlight) { wireDrops++; return; }
    wire_.emplace_back(now_ + latency_, std::move(f));
}

void EtherLink::tick(std::int64_t now) {
    now_ = now;
    while (!wire_.empty() && wire_.front().first <= now_) {
        const std::vector<std::uint8_t>& f = wire_.front().second;
        // A frame already on the wire when the cable came out is lost with
        // it. Dropped HERE, on the machine thread, rather than by clearing
        // the queue from setUplink(): the GUI thread must not touch a deque
        // the machine thread may be inside (AtalkHub.h, thread contract).
        if (uplink()) nic_.receiveFrame(f.data(), f.size());
        wire_.pop_front();
    }
}

void EtherLink::onGuestFrame(const std::uint8_t* d, std::size_t n) {
    if (!uplink()) return;                 // the card still accepted the
                                          // WRITE(6); the wire carries nothing
    if (!d || n < kEthHdr) return;
    if (be16(d + 12) == 0x8035) { handleRarp(d, n); return; }
    if (be16(d + 12) == kEtherTypeIp && handleAddressMask(d, n)) return;
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

// RFC 903: answer only the attached card asking for its own IPv4 address.
// Undefined request protocol-address fields are deliberately ignored.
void EtherLink::handleRarp(const std::uint8_t* frame, std::size_t n) {
    if (n < kEthHdr + 28) return;
    const auto* p = frame + kEthHdr;
    static constexpr std::array<uint8_t, 6> broadcast{255,255,255,255,255,255};
    const auto& mac = nic_.mac();
    if (be16(p) != 1 || be16(p + 2) != kEtherTypeIp || p[4] != 6 || p[5] != 4 ||
        be16(p + 6) != 3 || !std::equal(mac.begin(), mac.end(), frame + 6) ||
        !std::equal(mac.begin(), mac.end(), p + 8) ||
        !std::equal(mac.begin(), mac.end(), p + 18) ||
        (!std::equal(gwMac_.begin(), gwMac_.end(), frame) &&
         !std::equal(broadcast.begin(), broadcast.end(), frame))) return;
    rarpRequests++;
    const auto ip = gw_.leaseForEther(mac);
    if (!ip) return;                              // RFC 903 has no error reply
    uint8_t reply[28] = {};
    wr16(reply, 1); wr16(reply + 2, kEtherTypeIp); reply[4] = 6; reply[5] = 4;
    wr16(reply + 6, 4);
    std::copy(gwMac_.begin(), gwMac_.end(), reply + 8); wr32(reply + 14, gw_.gwIp());
    std::copy(mac.begin(), mac.end(), reply + 18); wr32(reply + 24, ip);
    guestMac_ = mac; guestIp_ = ip;
    sendToGuest(mac, 0x8035, reply, sizeof reply); rarpReplies++;
}

// RFC 950 Appendix I: zero-source discovery gets a broadcast reply; an
// addressed host gets unicast. This is a local router service, not NAT traffic.
// Return true for a mask request even when malformed, so it cannot teach the
// NAT a bogus lease or replace the Ethernet return address.
bool EtherLink::handleAddressMask(const std::uint8_t* frame, std::size_t n) {
    const auto* ip = frame + kEthHdr;
    n -= kEthHdr;
    if (n < 20 || (ip[0] >> 4) != 4 || ip[9] != 1) return false;
    const std::size_t ihl = (ip[0] & 15) * 4;
    if (ihl < 20 || ihl >= n || ip[ihl] != 17) return false;
    const std::size_t total = be16(ip + 2);
    if (total != ihl + 12 || total > n || !ip[8] || (be16(ip + 6) & 0xbfff) ||
        checksum(ip, ihl) || ip[ihl + 1] || checksum(ip + ihl, 12)) return true;
    static constexpr std::array<std::uint8_t, 6> broadcast{255,255,255,255,255,255};
    if (!std::equal(nic_.mac().begin(), nic_.mac().end(), frame + 6) ||
        (!std::equal(gwMac_.begin(), gwMac_.end(), frame) &&
         !std::equal(broadcast.begin(), broadcast.end(), frame))) return true;
    const auto mask = gw_.netmask(), gateway = gw_.gwIp();
    const auto subnet = gateway & mask, directedBroadcast = subnet | ~mask;
    const auto src = be32(ip + 12), dst = be32(ip + 16);
    if (dst != gateway && dst != directedBroadcast && dst != 0xffffffffu) return true;
    if (src && ((src & mask) != subnet || src == gateway ||
                src == subnet || src == directedBroadcast)) return true;
    std::uint8_t reply[32] = {};
    reply[0] = 0x45; wr16(reply + 2, sizeof reply); reply[8] = 64; reply[9] = 1;
    wr32(reply + 12, gateway); wr32(reply + 16, src ? src : 0xffffffffu);
    reply[20] = 18; std::copy(ip + ihl + 4, ip + ihl + 8, reply + 24);
    wr32(reply + 28, mask);
    wr16(reply + 22, checksum(reply + 20, 12));
    wr16(reply + 10, checksum(reply, 20));
    guestMac_ = nic_.mac();
    if (src) guestIp_ = src;
    ipFromGuestFrames++; ipToGuestFrames++;
    sendToGuest(src ? guestMac_ : broadcast, kEtherTypeIp, reply, sizeof reply);
    return true;
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
