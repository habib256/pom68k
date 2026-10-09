// RFC 950 wire fixtures independent of the gateway's packet builder.
#pragma once

void addressMaskCases() {
    Wire wire; MacIpGateway gw(wire.st);
    gw.configure(kGw, 0xFFFFFF00, 0x08080808);
    DaynaPort card; card.attach();
    std::vector<uint8_t> out, none;
    const uint8_t enable[6] = {0x0E,0,0,0,0,0x80}; card.command(enable,6,out,none);
    EtherLink link(card,gw); link.attach();
    const auto mac = card.mac();
    const std::array<uint8_t,6> broadcast{255,255,255,255,255,255};
    auto request = [&](uint32_t src, uint32_t dst) {
        std::vector<uint8_t> icmp{17,0,0,0,0x12,0x34,0xab,0xcd,0,0,0,0};
        const auto c = csum16(icmp.data(),icmp.size());
        icmp[2] = uint8_t(c >> 8); icmp[3] = uint8_t(c);
        return ethFrame(dst == kGw ? link.gatewayMac() : broadcast, mac, 0x0800,
                        ipPkt(src,dst,1,icmp));
    };
    auto send = [&](const std::vector<uint8_t>& f) {
        const uint8_t write[6] = {0x0A,0,0,uint8_t(f.size() >> 8),uint8_t(f.size()),0};
        CHECK(card.command(write,6,out,f) == 0,"mask fixture crosses DaynaPort WRITE(6)");
    };
    auto checkReply = [&](uint32_t dst, uint32_t mask, bool bcast) {
        const auto r = readOne(card);
        CHECK(r.ok && r.len == 64 && r.data.size() == 64,"mask reply is framed/padded by DaynaPort");
        if (r.data.size() < 46) return;
        const auto* f = r.data.data(); const auto* ip = f + 14;
        const auto& target = bcast ? broadcast : mac;
        CHECK(std::equal(target.begin(),target.end(),f),"mask reply has the RFC 950 Ethernet destination");
        CHECK(std::equal(link.gatewayMac().begin(),link.gatewayMac().end(),f + 6),"mask reply comes from the gateway MAC");
        CHECK(get16(f + 12) == 0x0800 && ip[0] == 0x45 && get16(ip + 2) == 32 && ip[9] == 1,
              "mask reply is an exact IPv4/ICMP datagram");
        CHECK(get32(ip + 12) == kGw && get32(ip + 16) == dst,"mask reply has the RFC 950 IP destination");
        CHECK(csum16(ip,20) == 0 && csum16(ip + 20,12) == 0,"independent verifier accepts both checksums");
        CHECK(ip[20] == 18 && ip[21] == 0 && get16(ip + 24) == 0x1234 && get16(ip + 26) == 0xabcd,
              "mask reply preserves identifier/sequence and uses type 18 code zero");
        CHECK(get32(ip + 28) == mask,"mask reply uses the configured network mask");
        CHECK(card.queued() == 0,"one request produces exactly one mask reply");
    };
    for (auto dst : {kGw, uint32_t(0xffffffff), uint32_t(0xc0a897ff)}) {
        send(request(kGuest,dst)); checkReply(kGuest,0xffffff00,false);
    }
    send(request(0,0xffffffff)); checkReply(0xffffffff,0xffffff00,true);
    CHECK(!gw.leased(0) && !gw.leased(kGuest),"mask discovery does not fabricate NAT address leases");

    // Ethernet padding is outside IPv4's total length; IPv4 options change IHL.
    auto df = request(kGuest,kGw);
    df[20] = 0x40; df[24] = df[25] = 0;
    const auto dfSum = csum16(df.data() + 14,20);
    df[24] = uint8_t(dfSum >> 8); df[25] = uint8_t(dfSum);
    send(df); checkReply(kGuest,0xffffff00,false);
    auto padded = request(kGuest,kGw); padded.resize(60,0xcc);
    send(padded); checkReply(kGuest,0xffffff00,false);
    auto options = request(kGuest,kGw);
    options.insert(options.begin() + 34,4,1); options[14] = 0x46; options[17] = 36;
    options[24] = options[25] = 0;
    const auto optsum = csum16(options.data() + 14,24);
    options[24] = uint8_t(optsum >> 8); options[25] = uint8_t(optsum);
    send(options); checkReply(kGuest,0xffffff00,false);

    auto fixIp = [](std::vector<uint8_t>& f) {
        f[24] = f[25] = 0; const auto c = csum16(f.data() + 14,20);
        f[24] = uint8_t(c >> 8); f[25] = uint8_t(c);
    };
    for (int variant = 0; variant < 15; ++variant) {
        auto f = request(kGuest,kGw);
        switch (variant) {
        case 0: f.resize(45); break;                        // truncated ICMP
        case 1: f[24] ^= 1; break;                         // bad IP checksum
        case 2: f[36] ^= 1; break;                         // bad ICMP checksum
        case 3: {                                         // bad code, valid checksum
            f[35] = 1; f[36] = f[37] = 0;
            const auto c = csum16(f.data() + 34,12); f[36] = uint8_t(c >> 8); f[37] = uint8_t(c);
            break;
        }
        case 4: f[20] = 0x20; fixIp(f); break;             // first fragment/MF
        case 5: f[21] = 1; fixIp(f); break;                // later fragment
        case 6: f[20] = 0x80; fixIp(f); break;             // reserved flag
        case 7: f[22] = 0; fixIp(f); break;                // expired TTL
        case 8: f[17] = 31; fixIp(f); break;               // short total length
        case 9: f[17] = 33; f.push_back(0); fixIp(f); break;// long total length
        case 10: f[6] ^= 2; break;                         // unrelated source MAC
        case 11: f[0] ^= 2; break;                         // unrelated destination MAC
        case 12: f = request(kGuest,0x08080808); break;     // not our router
        case 13: f = request(0xc0a89802,kGw); break;        // off-subnet source
        case 14: f = request(kGw,kGw); break;              // gateway impersonation
        }
        const auto learned = link.guestMac(); const auto count = link.ipToGuestFrames;
        send(f);
        CHECK(card.queued() == 0 && link.ipToGuestFrames == count,"invalid/unrelated mask request gets no reply");
        CHECK(link.guestMac() == learned,"rejected mask requests do not change the return MAC");
    }
    for (size_t n = 0; n < 46; ++n) {
        auto f = request(kGuest,kGw); f.resize(n); send(f);
        CHECK(card.queued() == 0,"every truncated mask frame is ignored safely");
    }
    link.setLatency(100); link.tick(1000);
    send(request(kGuest,kGw));
    CHECK(link.inFlight() == 1 && card.queued() == 0,"mask reply waits on the existing machine-time wire");
    link.tick(1099); CHECK(card.queued() == 0,"mask reply is not delivered early");
    link.tick(1100); checkReply(kGuest,0xffffff00,false);
    send(request(kGuest,kGw)); link.setUplink(false); link.tick(1200);
    CHECK(card.queued() == 0 && link.inFlight() == 0,"unplugging drops an in-flight mask reply");
    send(request(kGuest,kGw)); CHECK(link.inFlight() == 0,"unplugged mask request creates no traffic");
    link.setUplink(true); link.setLatency(0);
    gw.configure(kGw,0xfffffe00,0x08080808);
    send(request(kGuest,kGw)); checkReply(kGuest,0xfffffe00,false);

    // The very next ordinary transaction still crosses the shared gateway.
    std::vector<uint8_t> echo{8,0,0,0,0,7,0,1,'p','o','m'};
    const auto c = csum16(echo.data(),echo.size()); echo[2] = uint8_t(c >> 8); echo[3] = uint8_t(c);
    send(ethFrame(link.gatewayMac(),mac,0x0800,ipPkt(kGuest,kGw,1,echo)));
    const auto r = readOne(card);
    CHECK(r.data.size() >= 45 && r.data[34] == 0 && get32(r.data.data() + 30) == kGuest,
          "normal gateway echo round-trip works after mask discovery");
    CHECK(gw.leased(kGuest),"ordinary IP traffic establishes its normal raw-link lease");
}
