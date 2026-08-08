// POM68K — gate `daynaport_test`: the DaynaPort SCSI/Link target and the
// Ethernet link that joins it to the in-process NAT.
//
// Two halves, and the second is the one that proves the seam:
//   • the SCSI surface — the command set from SLINKCMD.TXT as PiSCSI
//     implements it (BSD-3-Clause): the 6-byte READ header and its
//     more-data flag, both WRITE payload formats, SET MAC, ENABLE/DISABLE,
//     the 37-byte INQUIRY the Mac driver insists on, and the DATA OUT
//     lengths the controllers ask the target for;
//   • the round trip — an ICMP echo travelling guest → Ethernet frame →
//     EtherLink → MacIpGateway's NAT → back out as a frame the guest can
//     READ(6). That path crosses every piece added for this: ScsiTarget,
//     DaynaPort, EtherLink and the gateway's raw-link leases.
//
// No ROM, no disk image, no host sockets (ICMP echo to the gateway is
// answered internally), so this gate always runs.

#include "DaynaPort.h"
#include "EtherLink.h"
#include "MacIpGateway.h"
#include "atalk_test_util.h"

#include <array>
#include <cstring>

namespace {
constexpr uint32_t kGw    = 0xC0A89701;            // 192.168.151.1
constexpr uint32_t kGuest = 0xC0A89702;            // 192.168.151.2
const std::array<uint8_t, 6> kGuestMac = { 0x08, 0x00, 0x07, 0x11, 0x22, 0x33 };

uint16_t csum16(const uint8_t* p, size_t n) {
    uint32_t acc = 0;
    for (size_t i = 0; i + 1 < n; i += 2) acc += uint32_t(p[i]) << 8 | p[i + 1];
    if (n & 1) acc += uint32_t(p[n - 1]) << 8;
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    return uint16_t(~acc);
}

// Same CRC-32 as DaynaPort.cpp, recomputed here on purpose: a gate that
// reuses the implementation's helper cannot catch the implementation
// getting the polynomial wrong.
uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    return c ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> ethFrame(const std::array<uint8_t, 6>& dst,
                              const std::array<uint8_t, 6>& src,
                              uint16_t type, const std::vector<uint8_t>& pay) {
    std::vector<uint8_t> f(14);
    std::memcpy(f.data(), dst.data(), 6);
    std::memcpy(f.data() + 6, src.data(), 6);
    f[12] = uint8_t(type >> 8); f[13] = uint8_t(type);
    f.insert(f.end(), pay.begin(), pay.end());
    return f;
}

std::vector<uint8_t> ipPkt(uint32_t src, uint32_t dst, uint8_t proto,
                           const std::vector<uint8_t>& l4) {
    std::vector<uint8_t> p(20, 0);
    p[0] = 0x45;
    p[2] = uint8_t((20 + l4.size()) >> 8);
    p[3] = uint8_t(20 + l4.size());
    p[8] = 64;
    p[9] = proto;
    p[12] = uint8_t(src >> 24); p[13] = uint8_t(src >> 16);
    p[14] = uint8_t(src >> 8);  p[15] = uint8_t(src);
    p[16] = uint8_t(dst >> 24); p[17] = uint8_t(dst >> 16);
    p[18] = uint8_t(dst >> 8);  p[19] = uint8_t(dst);
    uint16_t c = csum16(p.data(), 20);
    p[10] = uint8_t(c >> 8); p[11] = uint8_t(c);
    p.insert(p.end(), l4.begin(), l4.end());
    return p;
}

// Pull one frame out of the card the way a driver does: READ(6) with the
// control byte, then unpack the 6-byte header.
struct RxFrame { bool ok = false; size_t len = 0; uint32_t flags = 0; std::vector<uint8_t> data; };
RxFrame readOne(DaynaPort& nic) {
    std::vector<uint8_t> out, none;
    const uint8_t cdb[6] = { 0x08, 0, 0, 0, 0x00, 0xC0 };
    RxFrame r;
    if (nic.command(cdb, 6, out, none) != 0 || out.size() < 6) return r;
    r.ok = true;
    r.len = size_t((out[0] << 8) | out[1]);
    r.flags = (uint32_t(out[2]) << 24) | (uint32_t(out[3]) << 16)
            | (uint32_t(out[4]) << 8) | out[5];
    r.data.assign(out.begin() + 6, out.end());
    return r;
}
} // namespace

int main() {
    std::vector<uint8_t> out, none;

    // ══ Part 1: the SCSI surface ════════════════════════════════════════
    DaynaPort nic;
    std::vector<std::vector<uint8_t>> sent;
    nic.sendFrame = [&](const uint8_t* d, size_t n) { sent.emplace_back(d, d + n); };

    CHECK(!nic.present(), "not on the bus until attached");
    nic.attach();
    CHECK(nic.present(), "attached target answers selection");

    // INQUIRY: 37 bytes, processor device. A 36-byte reply is what the Mac
    // driver rejects, so the odd length is the assertion.
    {
        const uint8_t cdb[6] = { 0x12, 0, 0, 0, 37, 0 };
        CHECK(nic.command(cdb, 6, out, none) == 0, "INQUIRY GOOD");
        CHECK(out.size() == 37, "INQUIRY is 37 bytes, not 36");
        CHECK(out[0] == 0x03, "INQUIRY: processor device");
        CHECK(out[4] == 0x20, "INQUIRY additional length $20");
        CHECK(std::memcmp(&out[8], "Dayna   ", 8) == 0, "INQUIRY vendor 'Dayna'");
        CHECK(std::memcmp(&out[16], "SCSI/Link       ", 16) == 0, "INQUIRY product");
    }

    // A frame arriving before ENABLE goes nowhere: the interface is off.
    {
        std::vector<uint8_t> f = ethFrame(kGuestMac, kGuestMac, 0x0800,
                                          std::vector<uint8_t>(40, 0xAA));
        nic.receiveFrame(f.data(), f.size());
        CHECK(nic.queued() == 0, "a disabled interface carries nothing");
    }

    // ENABLE INTERFACE
    {
        const uint8_t cdb[6] = { 0x0E, 0, 0, 0, 0, 0x80 };
        CHECK(nic.command(cdb, 6, out, none) == 0, "ENABLE INTERFACE GOOD");
        CHECK(nic.enabled(), "interface enabled");
    }

    // READ(6) without the driver's control byte must not be answered with a
    // frame header — that CDB came from something probing for a disk.
    {
        const uint8_t cdb[6] = { 0x08, 0, 0, 0, 0x01, 0x00 };
        CHECK(nic.command(cdb, 6, out, none) == 2, "READ(6) without control → CHECK");
        const uint8_t rs[6] = { 0x03, 0, 0, 0, 18, 0 };
        nic.command(rs, 6, out, none);
        CHECK(out[2] == 0x05 && out[12] == 0x24, "…ILLEGAL REQUEST / INVALID FIELD");
    }

    // The driver's one-block startup probe returns nothing, on purpose.
    {
        const uint8_t cdb[6] = { 0x08, 0, 0, 0, 0x01, 0xC0 };
        CHECK(nic.command(cdb, 6, out, none) == 0, "one-block probe GOOD");
        CHECK(out.empty(), "one-block probe returns no data");
    }

    // Empty ring: header only, length 0, no-more-data.
    {
        RxFrame r = readOne(nic);
        CHECK(r.ok && r.data.empty(), "empty ring: header only");
        CHECK(r.len == 0, "empty ring: length 0");
        CHECK(r.flags == 0, "empty ring: no-more-data");
    }

    // A real frame comes back padded to the Ethernet minimum with a VALID
    // FCS, and the length field counts the FCS.
    {
        std::vector<uint8_t> pay(30);
        for (size_t i = 0; i < pay.size(); i++) pay[i] = uint8_t(i * 3 + 1);
        std::vector<uint8_t> f = ethFrame(kGuestMac, kGuestMac, 0x0800, pay);
        nic.receiveFrame(f.data(), f.size());
        CHECK(nic.queued() == 1, "frame queued");

        RxFrame r = readOne(nic);
        CHECK(r.ok, "READ(6) GOOD");
        CHECK(r.len == 64, "44-byte frame padded to 60 + 4 FCS");
        CHECK(r.data.size() == 64, "payload length matches the header");
        CHECK(std::memcmp(r.data.data(), f.data(), f.size()) == 0,
              "the frame arrives byte for byte");
        const uint32_t c = crc32(r.data.data(), 60);
        CHECK(r.data[60] == uint8_t(c) && r.data[61] == uint8_t(c >> 8) &&
              r.data[62] == uint8_t(c >> 16) && r.data[63] == uint8_t(c >> 24),
              "the FCS is a real CRC-32 over the padded frame");
        CHECK(r.flags == 0, "single frame: no-more-data");
    }

    // Two queued frames: the first read says "more available".
    {
        std::vector<uint8_t> a = ethFrame(kGuestMac, kGuestMac, 0x0800,
                                          std::vector<uint8_t>(60, 0x11));
        std::vector<uint8_t> b = ethFrame(kGuestMac, kGuestMac, 0x0800,
                                          std::vector<uint8_t>(60, 0x22));
        nic.receiveFrame(a.data(), a.size());
        nic.receiveFrame(b.data(), b.size());
        RxFrame r1 = readOne(nic);
        CHECK(r1.flags == 0x10, "more-data flag while the ring is not empty");
        CHECK(r1.data[14] == 0x11, "frames come back in order (first)");
        RxFrame r2 = readOne(nic);
        CHECK(r2.flags == 0x00, "last frame clears the more-data flag");
        CHECK(r2.data[14] == 0x22, "frames come back in order (second)");
    }

    // WRITE(6), raw format: the CDB length is the frame length.
    {
        sent.clear();
        std::vector<uint8_t> f = ethFrame(kGuestMac, kGuestMac, 0x0800,
                                          std::vector<uint8_t>(50, 0x5A));
        const uint8_t cdb[6] = { 0x0A, 0, 0,
                                 uint8_t(f.size() >> 8), uint8_t(f.size()), 0x00 };
        CHECK(nic.writeByteCount(cdb, 6) == int(f.size()), "WRITE raw: DATA OUT length");
        CHECK(nic.command(cdb, 6, out, f) == 0, "WRITE(6) raw GOOD");
        CHECK(sent.size() == 1 && sent[0] == f, "the frame left verbatim");
    }

    // WRITE(6), $80 format: 2-byte length, 2 pad, frame, 4 trailing zeros —
    // and the CDB length is short by 8.
    {
        sent.clear();
        std::vector<uint8_t> f = ethFrame(kGuestMac, kGuestMac, 0x0800,
                                          std::vector<uint8_t>(46, 0xC3));
        std::vector<uint8_t> payload(4, 0);
        payload[0] = uint8_t(f.size() >> 8);
        payload[1] = uint8_t(f.size());
        payload.insert(payload.end(), f.begin(), f.end());
        payload.insert(payload.end(), 4, 0);
        const uint8_t cdb[6] = { 0x0A, 0, 0,
                                 uint8_t(f.size() >> 8), uint8_t(f.size()), 0x80 };
        CHECK(nic.writeByteCount(cdb, 6) == int(f.size()) + 8,
              "WRITE $80: DATA OUT length is the CDB length + 8");
        CHECK(nic.command(cdb, 6, out, payload) == 0, "WRITE(6) $80 GOOD");
        CHECK(sent.size() == 1 && sent[0] == f, "the frame is unwrapped from the header");
    }

    // A driver that appends its own FCS must not have it forwarded.
    {
        sent.clear();
        std::vector<uint8_t> f = ethFrame(kGuestMac, kGuestMac, 0x0800,
                                          std::vector<uint8_t>(60, 0x7E));
        std::vector<uint8_t> wire = f;
        const uint32_t c = crc32(f.data(), f.size());
        wire.push_back(uint8_t(c));       wire.push_back(uint8_t(c >> 8));
        wire.push_back(uint8_t(c >> 16)); wire.push_back(uint8_t(c >> 24));
        const uint8_t cdb[6] = { 0x0A, 0, 0,
                                 uint8_t(wire.size() >> 8), uint8_t(wire.size()), 0x00 };
        CHECK(nic.command(cdb, 6, out, wire) == 0, "WRITE with trailing FCS GOOD");
        CHECK(sent.size() == 1 && sent[0] == f, "a valid trailing FCS is stripped");
    }

    // SET MAC ADDRESS, then RETRIEVE STATISTICS reports it.
    {
        const uint8_t setmac[6] = { 0x0C, 0, 0, 0, 0x08, 0x40 };
        CHECK(nic.writeByteCount(setmac, 6) == 6, "SET MAC gathers 6 bytes");
        const std::vector<uint8_t> m = { 0x00, 0xA0, 0x40, 1, 2, 3 };
        CHECK(nic.command(setmac, 6, out, m) == 0, "SET MAC GOOD");
        CHECK(std::memcmp(nic.mac().data(), m.data(), 6) == 0, "MAC updated");

        const uint8_t stats[6] = { 0x09, 0, 0, 0x00, 0x12, 0 };
        CHECK(nic.command(stats, 6, out, none) == 0, "RETRIEVE STATS GOOD");
        CHECK(out.size() == 18, "stats are 18 bytes");
        CHECK(std::memcmp(out.data(), m.data(), 6) == 0, "stats carry the MAC");
        for (size_t i = 6; i < 18; i++) CHECK(out[i] == 0, "error counters are zero");
    }

    // SET MULTICAST: a list is accepted, an empty one is refused.
    {
        const uint8_t mc[6] = { 0x0D, 0, 0, 0, 6, 0 };
        CHECK(nic.writeByteCount(mc, 6) == 6, "SET MULTICAST gathers its list");
        CHECK(nic.command(mc, 6, out, std::vector<uint8_t>(6, 0)) == 0, "SET MULTICAST GOOD");
        const uint8_t mc0[6] = { 0x0D, 0, 0, 0, 0, 0 };
        CHECK(nic.command(mc0, 6, out, none) == 2, "SET MULTICAST with no list → CHECK");
    }

    // Ring overflow drops the arriving frame and counts it — it does not grow.
    {
        std::vector<uint8_t> big = ethFrame(kGuestMac, kGuestMac, 0x0800,
                                            std::vector<uint8_t>(1000, 0x99));
        const long before = nic.framesDropped;
        for (int i = 0; i < 20; i++) nic.receiveFrame(big.data(), big.size());
        CHECK(nic.framesDropped > before, "an overflowing ring drops and counts");
        size_t total = 0;
        for (size_t i = 0; i < nic.queued(); i++) total += big.size();
        CHECK(total <= DaynaPort::kRxRingBytes, "the ring stays inside its ceiling");
    }

    // DISABLE restores the built-in MAC and empties the ring.
    {
        const uint8_t cdb[6] = { 0x0E, 0, 0, 0, 0, 0x00 };
        CHECK(nic.command(cdb, 6, out, none) == 0, "DISABLE INTERFACE GOOD");
        CHECK(!nic.enabled(), "interface disabled");
        CHECK(nic.queued() == 0, "disable empties the ring");
        const uint8_t builtin[6] = { 0x00, 0x80, 0x19, 0x10, 0x98, 0xE3 };
        CHECK(std::memcmp(nic.mac().data(), builtin, 6) == 0,
              "disable restores the built-in MAC");
    }

    // An opcode this target does not carry is refused, not ignored.
    {
        const uint8_t cdb[10] = { 0x28, 0, 0,0,0,0, 0, 0, 1, 0 };   // READ(10)
        CHECK(nic.command(cdb, 10, out, none) == 2, "READ(10) → CHECK");
        const uint8_t rs[6] = { 0x03, 0, 0, 0, 18, 0 };
        nic.command(rs, 6, out, none);
        CHECK(out[2] == 0x05 && out[12] == 0x20, "…ILLEGAL REQUEST / INVALID COMMAND");
    }

    // ══ Part 2: the link, end to end ════════════════════════════════════
    Wire w;
    MacIpGateway gw(w.st);
    gw.configure(kGw, 0xFFFFFF00, 0x08080808);
    gw.setEnabled(true);

    DaynaPort card;
    card.attach();
    { const uint8_t en[6] = { 0x0E, 0, 0, 0, 0, 0x80 }; card.command(en, 6, out, none); }

    EtherLink link(card, gw);
    link.attach();

    // ARP for the gateway → a reply carrying the gateway's MAC.
    {
        std::vector<uint8_t> arp(28, 0);
        arp[1] = 1;                                   // hardware: Ethernet
        arp[2] = 0x08; arp[3] = 0x00;                 // protocol: IPv4
        arp[4] = 6; arp[5] = 4;
        arp[7] = 1;                                   // op: request
        std::memcpy(&arp[8], kGuestMac.data(), 6);
        arp[14] = uint8_t(kGuest >> 24); arp[15] = uint8_t(kGuest >> 16);
        arp[16] = uint8_t(kGuest >> 8);  arp[17] = uint8_t(kGuest);
        arp[24] = uint8_t(kGw >> 24); arp[25] = uint8_t(kGw >> 16);
        arp[26] = uint8_t(kGw >> 8);  arp[27] = uint8_t(kGw);
        std::array<uint8_t, 6> bcast = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
        std::vector<uint8_t> f = ethFrame(bcast, kGuestMac, 0x0806, arp);

        link.onGuestFrame(f.data(), f.size());
        RxFrame r = readOne(card);
        CHECK(r.ok && r.data.size() >= 42, "ARP request is answered");
        if (r.data.size() >= 42) {
            CHECK(r.data[12] == 0x08 && r.data[13] == 0x06, "reply is an ARP frame");
            CHECK(r.data[20] == 0 && r.data[21] == 2, "op = reply");
            CHECK(std::memcmp(&r.data[22], link.gatewayMac().data(), 6) == 0,
                  "reply carries the gateway MAC");
            CHECK(std::memcmp(&r.data[0], kGuestMac.data(), 6) == 0,
                  "reply is unicast back to the asker");
        }
    }

    // ARP for the guest's OWN address must go unanswered — a reply is how
    // MacTCP concludes the address is taken and refuses to initialise.
    {
        const long before = link.arpReplies;
        std::vector<uint8_t> arp(28, 0);
        arp[1] = 1; arp[2] = 0x08; arp[3] = 0x00; arp[4] = 6; arp[5] = 4; arp[7] = 1;
        std::memcpy(&arp[8], kGuestMac.data(), 6);
        arp[14] = uint8_t(kGuest >> 24); arp[15] = uint8_t(kGuest >> 16);
        arp[16] = uint8_t(kGuest >> 8);  arp[17] = uint8_t(kGuest);
        arp[24] = uint8_t(kGuest >> 24); arp[25] = uint8_t(kGuest >> 16);
        arp[26] = uint8_t(kGuest >> 8);  arp[27] = uint8_t(kGuest);
        std::array<uint8_t, 6> bcast = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
        std::vector<uint8_t> f = ethFrame(bcast, kGuestMac, 0x0806, arp);
        link.onGuestFrame(f.data(), f.size());
        CHECK(link.arpReplies == before, "a probe for the guest's own address is ignored");
        CHECK(card.queued() == 0, "…and nothing is queued for it");
    }

    // The round trip: ICMP echo from the guest, over Ethernet, answered by
    // the NAT and delivered back as a frame the driver can READ(6).
    {
        std::vector<uint8_t> icmp = { 8, 0, 0, 0, 0, 7, 0, 1, 'p','o','m','6','8','k' };
        uint16_t c = csum16(icmp.data(), icmp.size());
        icmp[2] = uint8_t(c >> 8); icmp[3] = uint8_t(c);
        std::vector<uint8_t> pkt = ipPkt(kGuest, kGw, 1, icmp);
        std::vector<uint8_t> f = ethFrame(link.gatewayMac(), kGuestMac, 0x0800, pkt);

        link.onGuestFrame(f.data(), f.size());
        RxFrame r = readOne(card);
        CHECK(r.ok && r.data.size() >= 14 + 20 + 8, "an echo reply comes back as a frame");
        if (r.data.size() >= 42) {
            CHECK(std::memcmp(&r.data[0], kGuestMac.data(), 6) == 0,
                  "addressed to the guest's MAC");
            CHECK(std::memcmp(&r.data[6], link.gatewayMac().data(), 6) == 0,
                  "sourced from the gateway's MAC");
            CHECK(r.data[12] == 0x08 && r.data[13] == 0x00, "ethertype IPv4");
            const uint8_t* ip = &r.data[14];
            CHECK((ip[0] >> 4) == 4, "payload is IPv4");
            CHECK(ip[9] == 1, "protocol ICMP");
            const uint32_t sip = (uint32_t(ip[12]) << 24) | (uint32_t(ip[13]) << 16)
                               | (uint32_t(ip[14]) << 8) | ip[15];
            const uint32_t dip = (uint32_t(ip[16]) << 24) | (uint32_t(ip[17]) << 16)
                               | (uint32_t(ip[18]) << 8) | ip[19];
            CHECK(sip == kGw && dip == kGuest, "reply is gateway → guest");
            CHECK(ip[20] == 0, "ICMP type 0 = echo reply");
            CHECK(std::memcmp(&ip[28], "pom68k", 6) == 0, "the payload survived");
        }
        CHECK(gw.status().leases == 1, "the guest holds one lease");
    }

    if (failures) {
        std::printf("daynaport_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("daynaport_test: SCSI/Link command set + Ethernet round trip, "
                "gate passed\n");
    return 0;
}
