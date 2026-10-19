// Real Portable/PB100 PMU through production wiring, no guest/firmware patches.
#include "PortablePmu.h"
#include "AssetFingerprint.h"
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
constexpr const char* digest="0f9911ab33a62a2831d683d9648059bd7ad59c455b59dcd214c610ce16eebd57";
struct Host {
    PortablePmu p{15667200};
    bool waitAck(bool high) {
        for(int n=0;n<100000;++n) {
            p.tick(16);
            if(p.mcu().cpu().illegalOpcode()) return false;
            if(p.ack()==high) return true;
        }
        std::fprintf(stderr,"FAIL ACK timeout PC=%04x REQ=%d ACK=%d data=%02x\n",
                     p.mcu().cpu().r.pc,p.request(),p.ack(),p.data());return false;
    }
    bool send(uint8_t byte) {
        p.setData(byte);p.setRequest(false);
        if(!waitAck(false)) return false;
        p.setRequest(true);return waitAck(true);
    }
    bool finishReceive() {
        p.setRequest(false);if(!waitAck(true)) return false;p.setRequest(true);return true;
    }
    bool receive(uint8_t& byte) {
        if(!waitAck(false)) return false;
        byte=p.data();return finishReceive();
    }
    std::vector<uint8_t> save() { std::vector<uint8_t> b;sav::Writer ar(b);ar(p);return b; }
    bool load(const std::vector<uint8_t>& b) {
        sav::Reader ar(b.data(),b.size());ar(p);return ar.ok() && !ar.remaining();
    }
    bool clockRoundTrip() {
        // Apple PowerPrivEqu.a: timeWrite=$30, timeRead=$38.
        const uint8_t clock[]={0x12,0x34,0x56,0x78};
        if(!send(0x30) || !send(4)) return false;
        for(auto byte:clock) if(!send(byte)) return false;
        if(!send(0x38) || !send(0)) return false;
        uint8_t echo=0,count=0;
        if(!receive(echo) || !receive(count) || echo!=0x38 || count!=4) return false;
        for(auto expected:clock) {
            uint8_t actual=0;if(!receive(actual) || actual!=expected) return false;
        }
        return true;
    }
};
bool run(const std::string& path) {
    std::printf("ASSET PMU %s sha256 %s\n",path.c_str(),digest);
    if(testasset::sha256File(path)!=digest) {std::fprintf(stderr,"FAIL digest mismatch\n");return false;}
    std::ifstream f(path,std::ios::binary);
    const std::vector<uint8_t> fw((std::istreambuf_iterator<char>(f)),{});
    Host h;if(!h.p.loadFirmware(fw)) return false;h.p.reset();
    // Cold-start calibration polls two 60.15 Hz interrupts with IRQs disabled.
    // Drive the board clock as well as the MCU, without seeding private RAM.
    int64_t previous=0;
    for(int frame=1;frame<=30;++frame) {
        const int64_t next=int64_t(frame)*15667200*20/1203; // 60.15 Hz
        h.p.vblank();h.p.tick(int(next-previous));previous=next;
    }
    if(h.p.cpuHeld() || h.p.mcu().cpu().illegalOpcode()) {
        std::fprintf(stderr,"FAIL cold start PC=%04x held=%d\n",h.p.mcu().cpu().r.pc,h.p.cpuHeld());return false;
    }
    if(!h.send(0xea) || !h.send(0) || !h.waitAck(false)) return false;
    if(h.p.data()!=0xea) {std::fprintf(stderr,"FAIL version command echo %02x\n",h.p.data());return false;}
    const auto offered=h.save();
    auto finish=[&](Host& b) {
        uint8_t count=0,major=0,minor=0;
        if(!b.finishReceive() || !b.receive(count) || !b.receive(major) || !b.receive(minor)) return false;
        std::printf("version %02X%02X count=%u\n",major,minor,count);
        if(count!=2 || major!=2 || minor!=0xb5) {
            std::fprintf(stderr,"FAIL expected version 02B5\n");return false;
        }
        b.p.tick(32000);return !b.p.mcu().cpu().illegalOpcode();
    };
    if(!finish(h)) return false;
    const auto after=h.save();
    Host restored;restored.p.loadFirmware(fw);restored.p.reset();
    int resetEdges=0;restored.p.onResetPin=[&](bool){++resetEdges;};
    if(!restored.load(offered) || resetEdges || restored.p.data()!=0xea || restored.p.ack() ||
       !finish(restored) || restored.save()!=after) {
        std::fprintf(stderr,"FAIL offered-byte replay in fresh controller\n");return false;
    }
    if(!h.clockRoundTrip()) {std::fprintf(stderr,"FAIL RTC command round-trip\n");return false;}
    std::puts("PASS cold start, host reset release, version 02B5, exact fresh-instance replay and RTC command round-trip");
    return true;
}
}
int main() {
    const char* inputs[]={"roms/archive/macroms/pmu/Macintosh-Portable__PMU.bin",
                          "roms/archive/macroms/pmu/Powerbook-100__PMU.bin"};
    std::string paths[2];
    for(unsigned i=0;i<2;++i) {
        paths[i]=testasset::find(inputs[i]);
        if(paths[i].empty()) {std::printf("SKIP: private Portable PMU firmware absent: %s\n",inputs[i]);return 0;}
    }
    return run(paths[0]) && run(paths[1]) ? 0:1;
}
