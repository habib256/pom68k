// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── MacIpGateway: in-process MacIP (IP-in-DDP) gateway + user-mode NAT ──
// Replaces the external macipgw + tun + iptables chain: no root, no
// kernel AppleTalk. The guest's MacTCP / Open Transport "AppleTalk
// (MacIP)" stack finds the gateway by NBP (`<gw-ip>:IPGATEWAY@zone`),
// gets its address from one ATP transaction on socket 72 (function 1 =
// assign — reply carries IP/DNS/broadcast/netmask; 3 = server probe),
// then exchanges raw IP packets as DDP type-22 datagrams
// (extern/macipgw/macip.c is the wire oracle).
//
// The NAT is a slirp-style user-mode stack:
//   UDP  — one connected host socket per (guest port, dst) flow; DNS is
//          just UDP 53 through it.
//   TCP  — a miniature TCP endpoint facing the guest (SYN-ACK, ordered
//          delivery, retransmit-on-timeout, FIN both ways) proxied onto
//          a non-blocking host TCP socket. MSS 536, in-order-only (the
//          in-process wire does not reorder; the guest retransmits the
//          rare drop).
//   ICMP — echo answered for the gateway address itself (no raw sockets
//          without privileges; TCP/UDP is what the era software uses).
//
// Era caveat carried over from the external chain: plain HTTP only —
// 1990s TLS cannot shake hands with 2026 endpoints (docs/APPLETALK.md
// §6.4). Gate: tests/macip_gw_test.cpp.

#pragma once
#include "AtalkStack.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

class MacIpGateway {
public:
    explicit MacIpGateway(AtalkStack& st) : st_(st) {}
    ~MacIpGateway();

    // Host byte order. Defaults mirror tools/macip/macip.sh:
    // 192.168.151.1/24, DNS 8.8.8.8.
    void configure(uint32_t gwIp, uint32_t mask, uint32_t dns);
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void tick(int64_t now);

    struct Status {
        bool enabled = false;
        bool registered = false;
        std::string gwIp, dns;
        int leases = 0;
        std::string lastLease;           // last assigned dotted quad
        long ipFromGuest = 0, ipToGuest = 0;
        int udpFlows = 0, tcpConns = 0;
        int64_t lastActivity = -1;       // emuCycles of last IP datagram
    };
    Status status() const;

private:
    // Idle leases are reclaimed in tick(); generous enough that a quiet but
    // live MacTCP node keeps its address (traffic refreshes lastSeen).
    static constexpr int64_t kLeaseLifetimeSec = 3600;
    struct Lease {
        AtalkStack::Addr at;             // AppleTalk return address
        int64_t lastSeen = 0;
    };
    struct UdpFlow {
        int fd = -1;
        uint32_t gIp = 0;
        uint16_t gPort = 0;
        uint32_t rIp = 0;
        uint16_t rPort = 0;
        int64_t lastAct = 0;
    };
    struct TcpConn {
        int fd = -1;
        uint32_t gIp = 0, rIp = 0;
        uint16_t gPort = 0, rPort = 0;
        enum State { Connecting, Established, Dead } state = Connecting;
        uint32_t isn = 0;                // our initial seq
        uint32_t sndSeq = 0, sndUna = 0; // next to send / oldest unacked
        uint32_t rcvNxt = 0;             // next expected from guest
        uint32_t guestWin = 4096;
        bool synAcked = false;           // guest acked our SYN-ACK
        bool guestFin = false, finSent = false, sockEof = false;
        std::deque<uint8_t> toHost;      // guest→host bytes not yet written
        std::deque<uint8_t> unacked;     // host→guest bytes in flight
        int64_t rtxTimer = 0;
        int rtxCount = 0;
        int64_t lastAct = 0;
    };

    void atpHandler(std::shared_ptr<AtalkStack::AtpTxn> t);
    void ddpHandler(const AtalkStack::Addr& src, uint8_t type,
                    const uint8_t* p, size_t n);
    void handleIp(const AtalkStack::Addr& src, const uint8_t* p, size_t n);
    void handleTcpFromGuest(const uint8_t* ip, size_t n);
    void handleUdpFromGuest(const uint8_t* ip, size_t n);
    void sendIpToGuest(uint32_t dstIp, const std::vector<uint8_t>& pkt);
    void sendTcpSeg(TcpConn& c, uint8_t flags, uint32_t seq,
                    const uint8_t* data, size_t n);
    void tcpPump(TcpConn& c, int64_t now);
    void dropTcp(TcpConn& c, bool rst);
    uint32_t leaseFor(const AtalkStack::Addr& at);

    AtalkStack& st_;
    bool enabled_ = false;
    uint32_t gw_ = 0xC0A89701;           // 192.168.151.1
    uint32_t mask_ = 0xFFFFFF00;
    uint32_t dns_ = 0x08080808;
    uint16_t ipId_ = 1;
    uint32_t isnCounter_ = 0x12340000;

    std::map<uint32_t, Lease> leases_;   // guest IP → return address
    std::vector<UdpFlow> udp_;
    std::vector<std::unique_ptr<TcpConn>> tcp_;
    mutable Status stat_;
};
