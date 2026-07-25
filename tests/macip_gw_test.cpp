// POM68K — gate `macip_gw_test`: the in-process MacIP gateway + user-mode
// NAT, exercised with real host sockets on the loopback. Pins: NBP
// IPGATEWAY registration, the ATP socket-72 address assignment (macipgw
// wire layout: control shorts + function32, data = ip/dns/broadcast/
// mask), ICMP echo to the gateway, a UDP round-trip through a flow
// socket, and a full TCP-lite conversation (SYN → SYN-ACK, data both
// ways, FIN both ways) proxied onto a host TCP connection.

#include "MacIpGateway.h"
#include "atalk_test_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
uint16_t csum16(const uint8_t* p, size_t n, uint32_t acc = 0) {
    for (size_t i = 0; i + 1 < n; i += 2) acc += uint32_t(p[i]) << 8 | p[i + 1];
    if (n & 1) acc += uint32_t(p[n - 1]) << 8;
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    return uint16_t(~acc);
}

std::vector<uint8_t> ipPkt(uint32_t src, uint32_t dst, uint8_t proto,
                           const std::vector<uint8_t>& l4) {
    std::vector<uint8_t> p(20);
    p[0] = 0x45;
    put16v(p, 0); p.resize(20);
    p[2] = uint8_t((20 + l4.size()) >> 8);
    p[3] = uint8_t(20 + l4.size());
    p[8] = 64;
    p[9] = proto;
    p[12] = src >> 24; p[13] = uint8_t(src >> 16); p[14] = uint8_t(src >> 8); p[15] = uint8_t(src);
    p[16] = dst >> 24; p[17] = uint8_t(dst >> 16); p[18] = uint8_t(dst >> 8); p[19] = uint8_t(dst);
    uint16_t c = csum16(p.data(), 20);
    p[10] = c >> 8; p[11] = uint8_t(c);
    p.insert(p.end(), l4.begin(), l4.end());
    return p;
}

std::vector<uint8_t> tcpSeg(uint16_t sport, uint16_t dport, uint32_t seq,
                            uint32_t ack, uint8_t flags,
                            const std::string& data = {}) {
    std::vector<uint8_t> t(20);
    t[0] = sport >> 8; t[1] = uint8_t(sport);
    t[2] = dport >> 8; t[3] = uint8_t(dport);
    t[4] = seq >> 24; t[5] = uint8_t(seq >> 16); t[6] = uint8_t(seq >> 8); t[7] = uint8_t(seq);
    t[8] = ack >> 24; t[9] = uint8_t(ack >> 16); t[10] = uint8_t(ack >> 8); t[11] = uint8_t(ack);
    t[12] = 0x50;
    t[13] = flags;
    t[14] = 0x20; t[15] = 0x00;                    // window 8192
    t.insert(t.end(), data.begin(), data.end());
    return t;
}

constexpr uint32_t kGw = 0xC0A89701;               // 192.168.151.1
constexpr uint32_t kGuest = 0xC0A89702;
constexpr uint32_t kLo = 0x7F000001;
} // namespace

int main() {
    Wire w;
    MacIpGateway gw(w.st);
    gw.configure(kGw, 0xFFFFFF00, 0x08080808);
    gw.setEnabled(true);

    auto pump = [&](int rounds = 20) {
        for (int i = 0; i < rounds; i++) {
            w.now += w.hz / 1000;
            w.st.tick(w.now);
            gw.tick(w.now);
            ::usleep(1000);
        }
    };
    auto lastIpToGuest = [&]() -> std::vector<uint8_t> {
        for (auto it = w.out.rbegin(); it != w.out.rend(); ++it)
            if (it->ddpType == 22) return it->pay;
        return {};
    };

    // ── NBP finds the gateway ──
    {
        std::vector<uint8_t> lk = { uint8_t(0x2 << 4 | 1), 0x21 };
        put16v(lk, 2); lk.push_back(47); lk.push_back(72); lk.push_back(0);
        putP(lk, "="); putP(lk, "IPGATEWAY"); putP(lk, "POM68K");
        w.sendDdp(47, 72, 2, 2, lk, 0xFF);
        bool found = false;
        for (auto& g : w.out)
            if (g.ddpType == 2 && (g.pay[0] >> 4) == 3
                && g.pay.size() > 21
                && !std::memcmp(g.pay.data() + 8, "192.168.151.1", 13))
                found = true;
        CHECK(found, "IPGATEWAY registered under the gateway IP");
    }

    // ── address assignment (ATP function 1) ──
    {
        w.clear();
        w.atpReq(47, 72, 72, 0x5000, { 0, 0, 0, 0, 0, 0, 0, 1 });
        auto r = w.atpResps(0x5000, 72);
        CHECK(!r.empty() && r[0].size() >= 28, "assign reply full length");
        if (!r.empty() && r[0].size() >= 28) {
            CHECK(get32(r[0].data() + 4) == 1, "assign function echo");
            CHECK(get32(r[0].data() + 8) == kGuest, "first lease = .2");
            CHECK(get32(r[0].data() + 12) == 0x08080808, "DNS advertised");
            CHECK(get32(r[0].data() + 24) == 0xFFFFFF00, "netmask advertised");
        }
        CHECK(gw.status().leases == 1, "one lease held");
    }

    // ── ICMP echo to the gateway ──
    {
        w.clear();
        std::vector<uint8_t> icmp = { 8, 0, 0, 0, 0, 1, 0, 1, 'p', 'i', 'n', 'g' };
        uint16_t c = csum16(icmp.data(), icmp.size());
        icmp[2] = c >> 8; icmp[3] = uint8_t(c);
        auto pkt = ipPkt(kGuest, kGw, 1, icmp);
        w.sendDdp(47, 72, 72, 22, pkt);
        auto r = lastIpToGuest();
        CHECK(r.size() == pkt.size() && r[20] == 0 && get32(r.data() + 16) == kGuest,
              "gateway answers ping");
    }

    // ── UDP round-trip through the NAT ──
    {
        int us = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in sa {};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(us, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
        socklen_t sl = sizeof sa;
        ::getsockname(us, reinterpret_cast<sockaddr*>(&sa), &sl);
        uint16_t port = ntohs(sa.sin_port);
        ::fcntl(us, F_SETFL, O_NONBLOCK);

        std::vector<uint8_t> udp = { 0x07, 0xD0, uint8_t(port >> 8), uint8_t(port),
                                     0, 12, 0, 0, 'p', 'i', 'n', 'g' };
        w.clear();
        w.sendDdp(47, 72, 72, 22, ipPkt(kGuest, kLo, 17, udp));
        uint8_t buf[64];
        sockaddr_in from {};
        socklen_t fl = sizeof from;
        ssize_t n = -1;
        for (int i = 0; i < 50 && n < 0; i++) {
            pump(1);
            n = ::recvfrom(us, buf, sizeof buf, 0,
                           reinterpret_cast<sockaddr*>(&from), &fl);
        }
        CHECK(n == 4 && !std::memcmp(buf, "ping", 4), "UDP reached the host");
        ::sendto(us, "pong", 4, 0, reinterpret_cast<sockaddr*>(&from), fl);
        std::vector<uint8_t> back;
        for (int i = 0; i < 50 && back.empty(); i++) {
            pump(1);
            back = lastIpToGuest();
            if (!back.empty() && (back.size() < 32 || back[9] != 17)) back.clear();
        }
        CHECK(back.size() == 32 && !std::memcmp(back.data() + 28, "pong", 4),
              "UDP reply tunneled back as DDP 22");
        ::close(us);
    }

    // ── TCP-lite: connect, data both ways, close both ways ──
    {
        int ls = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in sa {};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(ls, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
        socklen_t sl = sizeof sa;
        ::getsockname(ls, reinterpret_cast<sockaddr*>(&sa), &sl);
        uint16_t port = ntohs(sa.sin_port);
        ::listen(ls, 1);
        ::fcntl(ls, F_SETFL, O_NONBLOCK);

        w.clear();
        w.sendDdp(47, 72, 72, 22,
                  ipPkt(kGuest, kLo, 6, tcpSeg(3000, port, 1000, 0, 0x02)));
        int as = -1;
        std::vector<uint8_t> synAck;
        for (int i = 0; i < 100 && (as < 0 || synAck.empty()); i++) {
            pump(1);
            if (as < 0) as = ::accept(ls, nullptr, nullptr);
            auto r = lastIpToGuest();
            if (r.size() >= 40 && r[9] == 6 && (r[33] & 0x12) == 0x12) synAck = r;
        }
        CHECK(as >= 0, "host connection accepted");
        CHECK(!synAck.empty(), "SYN-ACK reached the guest");
        if (as < 0 || synAck.empty()) { std::printf("%d failure(s)\n", failures); return 1; }
        ::fcntl(as, F_SETFL, O_NONBLOCK);
        uint32_t isn = get32(synAck.data() + 24);
        CHECK(get32(synAck.data() + 28) == 1001, "SYN-ACK acks the guest ISN");

        // guest ACK + "hello"
        w.sendDdp(47, 72, 72, 22,
                  ipPkt(kGuest, kLo, 6, tcpSeg(3000, port, 1001, isn + 1, 0x10)));
        w.sendDdp(47, 72, 72, 22,
                  ipPkt(kGuest, kLo, 6, tcpSeg(3000, port, 1001, isn + 1, 0x18, "hello")));
        char rb[16] = {};
        ssize_t n = -1;
        for (int i = 0; i < 50 && n < 0; i++) { pump(1); n = ::recv(as, rb, sizeof rb, 0); }
        CHECK(n == 5 && !std::memcmp(rb, "hello", 5), "guest data reached the host app");

        // host app answers "world"
        w.clear();
        ::send(as, "world", 5, 0);
        std::vector<uint8_t> data;
        for (int i = 0; i < 50 && data.empty(); i++) {
            pump(1);
            auto r = lastIpToGuest();
            if (r.size() == 45 && r[9] == 6 && (r[33] & 0x08)) data = r;
        }
        CHECK(data.size() == 45 && !std::memcmp(data.data() + 40, "world", 5),
              "host data segmented back to the guest");
        uint32_t hseq = data.empty() ? 0 : get32(data.data() + 24);
        // guest ACKs the data, then closes
        w.sendDdp(47, 72, 72, 22,
                  ipPkt(kGuest, kLo, 6, tcpSeg(3000, port, 1006, hseq + 5, 0x10)));
        w.sendDdp(47, 72, 72, 22,
                  ipPkt(kGuest, kLo, 6, tcpSeg(3000, port, 1006, hseq + 5, 0x11)));
        // host side sees EOF and closes too
        char eb[8];
        ssize_t e = -1;
        for (int i = 0; i < 50 && e != 0; i++) { pump(1); e = ::recv(as, eb, sizeof eb, 0); }
        CHECK(e == 0, "guest FIN surfaced as EOF on the host socket");
        ::close(as);
        std::vector<uint8_t> fin;
        for (int i = 0; i < 50 && fin.empty(); i++) {
            pump(1);
            auto r = lastIpToGuest();
            if (r.size() >= 40 && r[9] == 6 && (r[33] & 0x01)) fin = r;
        }
        CHECK(!fin.empty(), "host close forwarded as FIN");
        if (!fin.empty()) {
            uint32_t fseq = get32(fin.data() + 24);
            w.sendDdp(47, 72, 72, 22,
                      ipPkt(kGuest, kLo, 6,
                            tcpSeg(3000, port, 1007, fseq + 1, 0x10)));
        }
        pump(5);
        CHECK(gw.status().tcpConns == 0, "connection fully reaped");
        ::close(ls);
    }

    CHECK(gw.status().ipFromGuest > 0 && gw.status().ipToGuest > 0,
          "GUI counters run");

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("macip_gw_test OK\n");
    return 0;
}
