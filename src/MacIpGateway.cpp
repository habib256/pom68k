// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// MacIpGateway — see MacIpGateway.h. The MacIP side mirrors
// extern/macipgw/macip.c byte for byte (assignment reply = control
// {version16=0, pad16, function32} + data {ip, dns, broadcast, pad,
// mask}); the NAT side is a from-scratch user-mode slirp-lite.

#include "MacIpGateway.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr uint8_t kMacIpSock = 72;       // MACIP_PORT (macip.c:67)
constexpr uint8_t kDdpMacIp = 22;        // IP-in-DDP protocol type
constexpr size_t kMtu = 586;             // MACIP_MAXMTU
constexpr size_t kMss = 536;

std::string iptoa(uint32_t ip) {
    char b[20];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255,
                  (ip >> 8) & 255, ip & 255);
    return b;
}
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void wr16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = uint8_t(v); }
void wr32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24; p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

uint16_t csum(const uint8_t* p, size_t n, uint32_t acc = 0) {
    for (size_t i = 0; i + 1 < n; i += 2) acc += rd16(p + i);
    if (n & 1) acc += uint32_t(p[n - 1]) << 8;
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    return uint16_t(~acc);
}
// TCP/UDP pseudo-header checksum
uint16_t l4sum(uint32_t src, uint32_t dst, uint8_t proto, const uint8_t* seg,
               size_t n) {
    uint32_t acc = (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF)
                 + proto + uint32_t(n);
    return csum(seg, n, acc);
}
bool seqLt(uint32_t a, uint32_t b) { return int32_t(a - b) < 0; }

// Opt-in datagram tracer: POM68K_MACIP_DEBUG=1 logs every IP datagram
// crossing the gateway, both directions, to stderr. When the guest's
// MacTCP/OT bombs, the LAST line before the bomb is the datagram that did
// it — which is the only way to tell "we sent something malformed" from
// "the era stack fell over on its own".
bool macipDbg() {
    static int on = -1;
    if (on < 0) on = std::getenv("POM68K_MACIP_DEBUG") ? 1 : 0;
    return on == 1;
}

// One line per datagram: direction, proto, endpoints, length, and — for
// TCP — the flags/seq/ack that let a capture be replayed by eye.
void traceIp(const char* dir, const uint8_t* p, size_t n) {
    if (!macipDbg() || n < 20) return;
    const size_t ihl = size_t(p[0] & 0x0F) * 4;
    const uint16_t frag = (n >= 8) ? rd16(p + 6) : 0;
    char tail[96] = "";
    if (p[9] == 6 && n >= ihl + 20 && !(frag & 0x1FFF)) {
        const uint8_t* t = p + ihl;
        std::snprintf(tail, sizeof tail,
                      " %u->%u flags$%02X seq=%u ack=%u win=%u payload=%zu",
                      rd16(t), rd16(t + 2), t[13], rd32(t + 4), rd32(t + 8),
                      rd16(t + 14), n - ihl - size_t(t[12] >> 4) * 4);
    } else if (p[9] == 17 && n >= ihl + 8 && !(frag & 0x1FFF)) {
        std::snprintf(tail, sizeof tail, " %u->%u", rd16(p + ihl),
                      rd16(p + ihl + 2));
    }
    std::fprintf(stderr, "[macip] %s %s -> %s proto=%u len=%zu%s%s\n", dir,
                 iptoa(rd32(p + 12)).c_str(), iptoa(rd32(p + 16)).c_str(),
                 p[9], n, (frag & 0x3FFF) ? " FRAG" : "", tail);
}

#ifndef _WIN32
void setNonBlock(int fd) { ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
#endif
} // namespace

MacIpGateway::~MacIpGateway() {
#ifndef _WIN32
    for (auto& f : udp_) if (f.fd >= 0) ::close(f.fd);
    for (auto& c : tcp_) if (c->fd >= 0) ::close(c->fd);
#endif
}

void MacIpGateway::configure(uint32_t gwIp, uint32_t mask, uint32_t dns) {
    bool was = enabled_;
    if (was) setEnabled(false);
    gw_ = gwIp;
    mask_ = mask;
    dns_ = dns;
    if (was) setEnabled(true);
}

void MacIpGateway::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    if (on) {
        st_.bindAtp(kMacIpSock, [this](std::shared_ptr<AtalkStack::AtpTxn> t) {
            atpHandler(std::move(t));
        });
        st_.bindDdp(kMacIpSock, [this](const AtalkStack::Addr& s, uint8_t ty,
                                       const uint8_t* p, size_t n) {
            ddpHandler(s, ty, p, n);
        });
        st_.nbpRegister(iptoa(gw_), "IPGATEWAY", kMacIpSock);
    } else {
        st_.nbpUnregister(iptoa(gw_), "IPGATEWAY");
#ifndef _WIN32
        for (auto& f : udp_) if (f.fd >= 0) ::close(f.fd);
        for (auto& c : tcp_) if (c->fd >= 0) ::close(c->fd);
#endif
        udp_.clear();
        tcp_.clear();
        leases_.clear();
    }
}

MacIpGateway::Status MacIpGateway::status() const {
    stat_.enabled = enabled_;
    stat_.registered = enabled_;
    stat_.gwIp = iptoa(gw_);
    stat_.dns = iptoa(dns_);
    stat_.leases = int(leases_.size());
    stat_.udpFlows = int(udp_.size());
    stat_.tcpConns = int(tcp_.size());
    return stat_;
}

// ── MacIP control: ATP socket 72 ────────────────────────────────────────

uint32_t MacIpGateway::leaseFor(const AtalkStack::Addr& at) {
    for (auto& [ip, l] : leases_)
        if (l.at.net == at.net && l.at.node == at.node) {
            l.at = at;                    // refresh return socket
            l.lastSeen = st_.now();
            return ip;
        }
    uint32_t base = gw_ & mask_;
    for (uint32_t h = 2; h < (~mask_ & 0xFF); h++) {
        uint32_t ip = base | h;
        if (ip == gw_ || leases_.count(ip)) continue;
        leases_[ip] = { at, st_.now() };
        stat_.lastLease = iptoa(ip);
        return ip;
    }
    return 0;
}

void MacIpGateway::atpHandler(std::shared_ptr<AtalkStack::AtpTxn> t) {
    // Request = 4 user bytes (client's version/pad shorts) + function32.
    if (!enabled_ || t->req.size() < 8) return;
    uint32_t func = rd32(t->req.data() + 4);
    std::vector<uint8_t> pkt(4, 0);       // version short 0 + pad (macipgw wire)
    switch (func) {
    case 1: {                             // MACIP_ASSIGN
        uint32_t ip = leaseFor(t->src);
        if (ip) {
            pkt.resize(4 + 24, 0);
            wr32(pkt.data() + 4, 1);      // function: assigned
            wr32(pkt.data() + 8, ip);
            wr32(pkt.data() + 12, dns_);
            wr32(pkt.data() + 16, (gw_ & mask_) | ~mask_);   // broadcast
            wr32(pkt.data() + 24, mask_);
        } else {
            pkt.resize(8, 0);             // function 0: no address left
        }
        break;
    }
    case 3:                               // MACIP_SERVER probe
        pkt.resize(8, 0);
        wr32(pkt.data() + 4, 3);
        break;
    default:
        pkt.resize(8, 0);                 // unknown → function 0
        break;
    }
    stat_.lastActivity = st_.now();
    t->respond({ std::move(pkt) });
}

// ── data plane ──────────────────────────────────────────────────────────

void MacIpGateway::ddpHandler(const AtalkStack::Addr& src, uint8_t type,
                              const uint8_t* p, size_t n) {
    if (type == kDdpMacIp) handleIp(src, p, n);
}

void MacIpGateway::sendIpToGuest(uint32_t dstIp, const std::vector<uint8_t>& pkt) {
    auto it = leases_.find(dstIp);
    if (it == leases_.end()) return;
    stat_.ipToGuest++;
    traceIp("->guest", pkt.data(), pkt.size());
    if (pkt.size() <= kMtu) {
        st_.sendDdp(it->second.at, kMacIpSock, kDdpMacIp, pkt.data(), pkt.size());
        return;
    }
    // Fragment: DDP caps a datagram at 586 bytes and era guest stacks
    // reassemble fine (same ceiling as macipgw's tun MTU).
    size_t ihl = (pkt[0] & 0x0F) * 4;
    size_t maxPay = ((kMtu - ihl) / 8) * 8;
    for (size_t off = 0; off < pkt.size() - ihl; off += maxPay) {
        size_t part = std::min(maxPay, pkt.size() - ihl - off);
        std::vector<uint8_t> f(pkt.begin(), pkt.begin() + long(ihl));
        f.insert(f.end(), pkt.begin() + long(ihl + off),
                 pkt.begin() + long(ihl + off + part));
        wr16(f.data() + 2, uint16_t(f.size()));
        uint16_t fo = uint16_t(off / 8);
        if (off + part < pkt.size() - ihl) fo |= 0x2000;   // more fragments
        wr16(f.data() + 6, fo);
        wr16(f.data() + 10, 0);
        wr16(f.data() + 10, csum(f.data(), ihl));
        st_.sendDdp(it->second.at, kMacIpSock, kDdpMacIp, f.data(), f.size());
    }
}

void MacIpGateway::handleIp(const AtalkStack::Addr& src, const uint8_t* p,
                            size_t n) {
    if (!enabled_ || n < 20 || (p[0] >> 4) != 4) return;
    size_t ihl = (p[0] & 0x0F) * 4;
    if (ihl < 20 || n < ihl) return;
    uint32_t sip = rd32(p + 12), dip = rd32(p + 16);
    uint8_t proto = p[9];
    stat_.ipFromGuest++;
    stat_.lastActivity = st_.now();
    traceIp("guest->", p, n);

    // Learn/refresh the mapping from traffic too (macipgw does the same).
    if ((sip & mask_) == (gw_ & mask_) && sip != gw_) {
        leases_[sip] = { src, st_.now() };
    }

    if (dip == gw_) {
        if (proto == 1 && n >= ihl + 8 && p[ihl] == 8) {   // ICMP echo
            std::vector<uint8_t> r(p, p + n);
            wr32(r.data() + 12, gw_);
            wr32(r.data() + 16, sip);
            r[ihl] = 0;                                    // echo reply
            wr16(r.data() + ihl + 2, 0);
            uint16_t ic = csum(r.data() + ihl, n - ihl);
            wr16(r.data() + ihl + 2, ic);
            wr16(r.data() + 10, 0);
            wr16(r.data() + 10, csum(r.data(), ihl));
            sendIpToGuest(sip, r);
        }
        return;
    }
    if ((dip & mask_) == (gw_ & mask_)) return;            // guest↔guest: no
    switch (proto) {
    case 6: handleTcpFromGuest(p, n); break;
    case 17: handleUdpFromGuest(p, n); break;
    default: break;                                        // ICMP out: drop
    }
}

// ── UDP flows ───────────────────────────────────────────────────────────

void MacIpGateway::handleUdpFromGuest(const uint8_t* p, size_t n) {
#ifndef _WIN32
    size_t ihl = (p[0] & 0x0F) * 4;
    if (n < ihl + 8) return;
    uint32_t gIp = rd32(p + 12), rIp = rd32(p + 16);
    uint16_t gPort = rd16(p + ihl), rPort = rd16(p + ihl + 2);
    const uint8_t* pay = p + ihl + 8;
    size_t plen = n - ihl - 8;

    UdpFlow* flow = nullptr;
    for (auto& f : udp_)
        if (f.gIp == gIp && f.gPort == gPort && f.rIp == rIp && f.rPort == rPort)
            flow = &f;
    if (!flow) {
        UdpFlow f;
        f.fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (f.fd < 0) return;
        setNonBlock(f.fd);
        sockaddr_in sa {};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(rIp);
        sa.sin_port = htons(rPort);
        if (::connect(f.fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0
            && errno != EINPROGRESS) {
            ::close(f.fd);
            return;
        }
        f.gIp = gIp; f.gPort = gPort; f.rIp = rIp; f.rPort = rPort;
        udp_.push_back(f);
        flow = &udp_.back();
    }
    flow->lastAct = st_.now();
    ::send(flow->fd, pay, plen, 0);
#else
    (void)p; (void)n;
#endif
}

// ── TCP-lite ────────────────────────────────────────────────────────────

void MacIpGateway::sendTcpSeg(TcpConn& c, uint8_t flags, uint32_t seq,
                              const uint8_t* data, size_t n) {
    bool syn = (flags & 0x02) != 0;
    size_t th = syn ? 24 : 20;            // MSS option on SYN-ACK
    std::vector<uint8_t> pkt(20 + th + n);
    uint8_t* ip = pkt.data();
    ip[0] = 0x45;
    wr16(ip + 2, uint16_t(pkt.size()));
    wr16(ip + 4, ipId_++);
    ip[8] = 64;                            // TTL
    ip[9] = 6;
    wr32(ip + 12, c.rIp);                 // we impersonate the remote host
    wr32(ip + 16, c.gIp);
    wr16(ip + 10, csum(ip, 20));
    uint8_t* t = ip + 20;
    wr16(t, c.rPort);
    wr16(t + 2, c.gPort);
    wr32(t + 4, seq);
    wr32(t + 8, (flags & 0x10) ? c.rcvNxt : 0);
    t[12] = uint8_t((th / 4) << 4);
    t[13] = flags;
    wr16(t + 14, 8192);                   // our receive window
    if (syn) { t[20] = 2; t[21] = 4; wr16(t + 22, kMss); }
    if (n) std::memcpy(t + th, data, n);
    wr16(t + 16, 0);
    wr16(t + 16, l4sum(c.rIp, c.gIp, 6, t, th + n));
    sendIpToGuest(c.gIp, pkt);
}

void MacIpGateway::dropTcp(TcpConn& c, bool rst) {
#ifndef _WIN32
    if (rst) sendTcpSeg(c, 0x04 | 0x10, c.sndSeq, nullptr, 0);   // RST|ACK
    if (c.fd >= 0) ::close(c.fd);
    c.fd = -1;
    c.state = TcpConn::Dead;
#else
    (void)c; (void)rst;
#endif
}

void MacIpGateway::handleTcpFromGuest(const uint8_t* p, size_t n) {
#ifndef _WIN32
    size_t ihl = (p[0] & 0x0F) * 4;
    if (n < ihl + 20) return;
    const uint8_t* t = p + ihl;
    uint32_t gIp = rd32(p + 12), rIp = rd32(p + 16);
    uint16_t gPort = rd16(t), rPort = rd16(t + 2);
    uint32_t seq = rd32(t + 4), ack = rd32(t + 8);
    size_t th = (t[12] >> 4) * 4;
    uint8_t flags = t[13];
    if (n < ihl + th) return;
    const uint8_t* pay = t + th;
    size_t plen = n - ihl - th;

    TcpConn* c = nullptr;
    for (auto& cc : tcp_)
        if (cc->gIp == gIp && cc->gPort == gPort && cc->rIp == rIp
            && cc->rPort == rPort && cc->state != TcpConn::Dead)
            c = cc.get();

    if (!c) {
        if (!(flags & 0x02) || (flags & 0x10)) {           // want a pure SYN
            if (!(flags & 0x04)) {                         // answer junk w/ RST
                TcpConn tmp;
                tmp.gIp = gIp; tmp.rIp = rIp; tmp.gPort = gPort; tmp.rPort = rPort;
                tmp.rcvNxt = seq + plen + 1;
                sendTcpSeg(tmp, 0x04 | 0x10, ack, nullptr, 0);
            }
            return;
        }
        auto nc = std::make_unique<TcpConn>();
        nc->fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (nc->fd < 0) return;
        setNonBlock(nc->fd);
        sockaddr_in sa {};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(rIp);
        sa.sin_port = htons(rPort);
        if (::connect(nc->fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0
            && errno != EINPROGRESS) {
            ::close(nc->fd);
            return;
        }
        nc->gIp = gIp; nc->rIp = rIp; nc->gPort = gPort; nc->rPort = rPort;
        nc->isn = isnCounter_ += 0x10000;
        nc->sndSeq = nc->isn;
        nc->sndUna = nc->isn;
        nc->rcvNxt = seq + 1;
        nc->guestWin = rd16(t + 14);
        nc->lastAct = st_.now();
        nc->rtxTimer = st_.now() + 15 * st_.cpuHz();       // connect deadline
        tcp_.push_back(std::move(nc));
        return;
    }

    c->lastAct = st_.now();
    c->guestWin = rd16(t + 14);
    if (flags & 0x04) { dropTcp(*c, false); return; }      // guest RST

    if ((flags & 0x02) && c->state == TcpConn::Established) {
        // SYN retransmit: our SYN-ACK got lost — resend it.
        if (!c->synAcked) sendTcpSeg(*c, 0x12, c->isn, nullptr, 0);
        return;
    }

    if (flags & 0x10) {                                    // ACK processing
        while (seqLt(c->sndUna, ack) && seqLt(c->sndUna, c->sndSeq + 1)) {
            if (!c->synAcked) { c->synAcked = true; c->sndUna++; continue; }
            if (!c->unacked.empty()) {
                size_t k = std::min<size_t>(c->unacked.size(), ack - c->sndUna);
                if (!k) break;
                c->unacked.erase(c->unacked.begin(), c->unacked.begin() + long(k));
                c->sndUna += uint32_t(k);
                continue;
            }
            if (c->finSent && c->sndUna != c->sndSeq) { c->sndUna++; continue; }
            break;
        }
        c->rtxCount = 0;
        c->rtxTimer = st_.now() + st_.cpuHz();
    }

    if (plen) {
        if (seq == c->rcvNxt) {
            c->toHost.insert(c->toHost.end(), pay, pay + plen);
            c->rcvNxt += uint32_t(plen);
        }
        sendTcpSeg(*c, 0x10, c->sndSeq, nullptr, 0);       // ACK (or dup-ACK)
    }
    if (flags & 0x01) {                                    // guest FIN
        if (seq + plen == c->rcvNxt || seq == c->rcvNxt) {
            if (!c->guestFin) { c->guestFin = true; c->rcvNxt++; }
            sendTcpSeg(*c, 0x10, c->sndSeq, nullptr, 0);
        }
    }
#else
    (void)p; (void)n;
#endif
}

void MacIpGateway::tcpPump(TcpConn& c, int64_t now) {
#ifndef _WIN32
    if (c.state == TcpConn::Connecting) {
        sockaddr_in sa {};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(c.rIp);
        sa.sin_port = htons(c.rPort);
        int r = ::connect(c.fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
        if (r == 0 || errno == EISCONN) {
            sendTcpSeg(c, 0x12, c.isn, nullptr, 0);        // SYN|ACK
            c.sndSeq = c.isn + 1;
            c.state = TcpConn::Established;
            c.rtxTimer = now + st_.cpuHz();
            return;
        }
        if (errno == EINPROGRESS || errno == EALREADY || errno == EAGAIN) {
            if (now >= c.rtxTimer) dropTcp(c, true);       // connect timeout
            return;
        }
        dropTcp(c, true);                                  // refused etc.
        return;
    }
    if (c.state != TcpConn::Established) return;

    // guest → host
    while (!c.toHost.empty()) {
        std::vector<uint8_t> chunk(c.toHost.begin(),
                                   c.toHost.begin()
                                       + long(std::min<size_t>(c.toHost.size(), 4096)));
        ssize_t w = ::send(c.fd, chunk.data(), chunk.size(), MSG_NOSIGNAL);
        if (w <= 0) break;
        c.toHost.erase(c.toHost.begin(), c.toHost.begin() + w);
    }
    if (c.guestFin && c.toHost.empty() && !c.sockEof)
        ::shutdown(c.fd, SHUT_WR);

    // host → guest, windowed
    while (c.synAcked && !c.sockEof) {
        size_t inFlight = c.unacked.size();
        if (inFlight >= std::min<size_t>(4 * kMss, c.guestWin)) break;
        uint8_t buf[kMss];
        ssize_t r = ::recv(c.fd, buf, sizeof buf, 0);
        if (r > 0) {
            sendTcpSeg(c, 0x18, c.sndSeq, buf, size_t(r));  // PSH|ACK
            c.unacked.insert(c.unacked.end(), buf, buf + r);
            c.sndSeq += uint32_t(r);
            if (c.rtxCount == 0) c.rtxTimer = now + st_.cpuHz();
            continue;
        }
        if (r == 0) { c.sockEof = true; break; }
        break;                                              // EAGAIN
    }
    if (c.sockEof && !c.finSent && c.synAcked) {
        sendTcpSeg(c, 0x11, c.sndSeq, nullptr, 0);          // FIN|ACK
        c.finSent = true;
        c.sndSeq++;
        c.rtxTimer = now + st_.cpuHz();
    }

    // retransmit
    if (c.sndUna != c.sndSeq && now >= c.rtxTimer) {
        if (++c.rtxCount > 6) { dropTcp(c, true); return; }
        if (!c.synAcked) {
            sendTcpSeg(c, 0x12, c.isn, nullptr, 0);
        } else if (!c.unacked.empty()) {
            size_t k = std::min<size_t>(c.unacked.size(), kMss);
            std::vector<uint8_t> seg(c.unacked.begin(), c.unacked.begin() + long(k));
            sendTcpSeg(c, 0x18, c.sndUna, seg.data(), seg.size());
        } else if (c.finSent) {
            sendTcpSeg(c, 0x11, c.sndSeq - 1, nullptr, 0);
        }
        c.rtxTimer = now + st_.cpuHz();
    }

    if (c.guestFin && c.finSent && c.sndUna == c.sndSeq && c.toHost.empty())
        dropTcp(c, false);                                  // clean close
    if (now - c.lastAct > 300 * st_.cpuHz())
        dropTcp(c, true);                                   // idle reap
#else
    (void)c; (void)now;
#endif
}

void MacIpGateway::tick(int64_t now) {
#ifndef _WIN32
    if (!enabled_) return;
    for (auto& c : tcp_)
        if (c->state != TcpConn::Dead) tcpPump(*c, now);
    tcp_.erase(std::remove_if(tcp_.begin(), tcp_.end(),
                              [](const std::unique_ptr<TcpConn>& c) {
                                  return c->state == TcpConn::Dead;
                              }),
               tcp_.end());

    for (auto it = udp_.begin(); it != udp_.end();) {
        uint8_t buf[2048];
        ssize_t r;
        bool alive = true;
        while ((r = ::recv(it->fd, buf, sizeof buf, 0)) > 0) {
            size_t plen = std::min<size_t>(size_t(r), 1400);
            std::vector<uint8_t> pkt(28 + plen);
            uint8_t* ip = pkt.data();
            ip[0] = 0x45;
            wr16(ip + 2, uint16_t(pkt.size()));
            wr16(ip + 4, ipId_++);
            ip[8] = 64;
            ip[9] = 17;
            wr32(ip + 12, it->rIp);
            wr32(ip + 16, it->gIp);
            wr16(ip + 10, csum(ip, 20));
            wr16(ip + 20, it->rPort);
            wr16(ip + 22, it->gPort);
            wr16(ip + 24, uint16_t(8 + plen));
            std::memcpy(ip + 28, buf, plen);
            wr16(ip + 26, 0);
            wr16(ip + 26, l4sum(it->rIp, it->gIp, 17, ip + 20, 8 + plen));
            sendIpToGuest(it->gIp, pkt);
            it->lastAct = now;
        }
        if (now - it->lastAct > 120 * st_.cpuHz()) {        // idle flow reap
            ::close(it->fd);
            alive = false;
        }
        it = alive ? std::next(it) : udp_.erase(it);
    }
#else
    (void)now;
#endif
}
