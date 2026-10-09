// RFC 903 protocol oracle through the real DaynaPort SCSI packet interface.
#pragma once
void rarpCases() {
    Wire wire; MacIpGateway gw(wire.st); gw.configure(kGw,0xffffff00,0x08080808); gw.setEnabled(true);
    DaynaPort card; card.attach(); std::vector<uint8_t> out, none;
    const uint8_t en[6]{0x0e,0,0,0,0,0x80}; card.command(en,6,out,none);
    EtherLink link(card,gw); link.attach(); const auto mac = card.mac();
    const std::array<uint8_t,6> broadcast{255,255,255,255,255,255};
    auto request = [&]() {
        std::vector<uint8_t> arp(28,0xa5); // SPA/TPA undefined, not required zero
        arp[0]=0; arp[1]=1; arp[2]=8; arp[3]=0; arp[4]=6; arp[5]=4; arp[6]=0; arp[7]=3;
        std::copy(mac.begin(),mac.end(),arp.begin()+8); std::copy(mac.begin(),mac.end(),arp.begin()+18);
        return ethFrame(broadcast,mac,0x8035,arp);
    };
    auto send = [&](const std::vector<uint8_t>& f) {
        const uint8_t cmd[6]{0x0a,0,0,uint8_t(f.size()>>8),uint8_t(f.size()),0};
        CHECK(card.command(cmd,6,out,f)==0,"RARP request crosses SCSI WRITE(6)");
    };
    auto reply = [&](uint32_t assigned) {
        const auto r = readOne(card);
        CHECK(r.data.size()==64,"RARP response has Ethernet padding and FCS");
        if(r.data.size()<42) return;
        const auto* p=r.data.data()+14;
        CHECK(get16(r.data.data()+12)==0x8035 && get16(p)==1 && get16(p+2)==0x0800 &&
              p[4]==6 && p[5]==4 && get16(p+6)==4,"RARP reply protocol/header/opcode match RFC 903");
        CHECK(std::equal(mac.begin(),mac.end(),r.data.begin()) &&
              std::equal(mac.begin(),mac.end(),p+18),"RARP response targets the requesting card");
        CHECK(std::equal(link.gatewayMac().begin(),link.gatewayMac().end(),p+8) &&
              get32(p+14)==kGw && get32(p+24)==assigned,"RARP contains server and assigned IPv4 addresses");
        CHECK(card.queued()==0,"one RARP response per valid request");
    };
    wire.atpReq(47,72,72,0x7000,{0,0,0,0,0,0,0,1});
    auto ddp=wire.atpResps(0x7000,72);
    CHECK(!ddp.empty() && ddp[0].size()>=28 && get32(ddp[0].data()+8)==kGuest,"MacIP occupies .2 first");
    send(request()); reply(kGuest+1);
    CHECK(gw.leased(kGuest+1),"RARP reserves .3 without colliding with MacIP");
    send(request()); reply(kGuest+1);
    CHECK(gw.status().leases==2,"repeated RARP keeps its hardware binding");
    auto unicast = request();
    std::copy(link.gatewayMac().begin(),link.gatewayMac().end(),unicast.begin());
    send(unicast); reply(kGuest+1);
    wire.clear(); wire.atpReq(48,72,72,0x7001,{0,0,0,0,0,0,0,1});
    ddp=wire.atpResps(0x7001,72);
    CHECK(!ddp.empty() && ddp[0].size()>=28 && get32(ddp[0].data()+8)==kGuest+2,
          "subsequent MacIP assignment skips the RARP reservation");
    std::vector<uint8_t> echo{8,0,0,0,0,7,0,1,'r','a','r','p'};
    const auto c=csum16(echo.data(),echo.size()); echo[2]=uint8_t(c>>8); echo[3]=uint8_t(c);
    send(ethFrame(link.gatewayMac(),mac,0x0800,ipPkt(kGuest+1,kGw,1,echo)));
    auto r=readOne(card);
    CHECK(r.data.size()>=46 && r.data[34]==0 && get32(r.data.data()+30)==kGuest+1,
          "assigned address completes an ordinary gateway echo round trip");
    CHECK(gw.leaseForEther(mac)==kGuest+1 && gw.status().leases==3,
          "IP traffic refreshes the lease without erasing the RARP MAC binding");
    // A link cannot steal the address reserved for the other link.
    send(ethFrame(link.gatewayMac(),mac,0x0800,ipPkt(kGuest,kGw,1,echo)));
    CHECK(card.queued()==0,"Ethernet source cannot take a MacIP-owned reservation");
    send(request()); reply(kGuest+1);
    wire.clear();
    wire.sendDdp(47,72,72,22,ipPkt(kGuest+1,kGw,1,echo));
    CHECK(wire.out.empty() && card.queued()==0 && gw.leaseForEther(mac)==kGuest+1,
          "MacIP source cannot overwrite the Ethernet RARP reservation");
    for(int v=0;v<10;++v) {
        auto f=request();
        switch(v) {
        case 0:f[15]=2;break; case 1:f[16]=0x86;break; // hardware/protocol
        case 2:f[18]=5;break; case 3:f[19]=16;break;   // address lengths
        case 4:f[21]=4;break;                         // reply opcode
        case 5:f[6]^=2;break; case 6:f[22]^=2;break; case 7:f[32]^=2;break;
        case 8:f[0]^=2;break; case 9:f.resize(41);break;
        }
        const auto learned=link.guestMac(); const auto replies=link.rarpReplies;
        send(f);
        CHECK(card.queued()==0 && link.rarpReplies==replies && gw.status().leases==3,
              "malformed/unrelated RARP frame has no response or new reservation");
        CHECK(link.guestMac()==learned,"rejected RARP does not replace the return MAC");
    }
    for(size_t n=0;n<42;++n) {
        auto f=request(); f.resize(n); send(f);
        CHECK(card.queued()==0 && gw.status().leases==3,"all truncated RARP frames are safely ignored");
    }
    link.setLatency(10); link.tick(100); send(request());
    CHECK(link.inFlight()==1 && card.queued()==0,"RARP uses the existing wire latency");
    link.tick(109); CHECK(card.queued()==0,"RARP is not delivered early");
    link.tick(110); reply(kGuest+1);
    send(request()); link.setUplink(false); link.tick(120);
    CHECK(card.queued()==0 && link.inFlight()==0,"unplugging drops pending RARP traffic");
    link.setUplink(true); link.setLatency(0);
    gw.setEnabled(false); send(request()); reply(kGuest+1);
    CHECK(!gw.leased(kGuest) && gw.leased(kGuest+1),"disabling MacIP preserves the Ethernet RARP reservation");
    gw.setEtherSink({}); CHECK(!gw.leased(kGuest+1),"detaching Ethernet releases its RARP reservation");
    link.attach();
    gw.configure(kGw,0xfffffffc,kGuest); // /30: sole usable guest address is DNS
    send(request()); CHECK(card.queued()==0,"RARP never hands out the configured DNS address");
    gw.configure(kGw,0xfffffffc,0x08080808);
    send(request()); reply(kGuest);
    auto other=mac; other[5]^=1;
    CHECK(gw.leaseForEther(other)==0,"exhausted /30 pool returns no assignment");
    CHECK(gw.leaseForEther({})==0,"zero MAC cannot acquire a reservation");
    other[0]|=1; CHECK(gw.leaseForEther(other)==0,"multicast MAC cannot acquire a reservation");
    gw.configure(kGw,0xfffffff8,0x08080808);
    send(request()); reply(kGuest);
    send(ethFrame(link.gatewayMac(),mac,0x0800,ipPkt(kGuest+1,kGw,1,echo)));
    readOne(card);
    CHECK(gw.leased(kGuest+1),"ordinary Ethernet traffic has a learned address");
    wire.st.tick(3601*wire.hz); gw.tick(3601*wire.hz);
    CHECK(gw.leased(kGuest) && !gw.leased(kGuest+1),
          "idle RARP binding survives learned-lease expiry: RARP has no renewal protocol");
    gw.setEtherSink({});
    CHECK(!gw.leased(kGuest),"persistent RARP binding still retires with its link");
}
