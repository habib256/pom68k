// Real PowerBook PMU firmware: cold start, CPU reset release and the old
// parallel PMgrOp protocol. This is a controller gate, not a Macintosh boot.
// Apple PowerMgr.a SendByte/ReceiveByte: receive waits for ACK LOW BEFORE
// asserting REQ. That differs from both transmit and the later SPI interface.
#include "M50753.h"
#include "AssetFingerprint.h"
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
struct Firmware {
    const char* path;
    const char* hash;
    uint8_t major,minor;
};
constexpr Firmware inputs[]={
    {"roms/archive/macroms/pmu/Powerbook-140-145-170__PMU.bin",
     "7b91c8a2591b2644bc14c35cd00656ba722c356c79717ed0ef176e5e89ccbcc6",2,0xb3},
    {"roms/archive/macroms/pmu/Powerbook-160-165-180__Powerbook 160 PMU.bin",
     "125a8680ab3faa8cc2fed666463ea7741045d8a4b893d8d7b08f4f369790e465",3,0xa2},
    {"roms/archive/macroms/pmu/Powerbook-165c-180c__PMU.bin",
     "125a8680ab3faa8cc2fed666463ea7741045d8a4b893d8d7b08f4f369790e465",3,0xa2}
};
struct Board {
    M50753 pmu;
    bool req=true;
    uint8_t hostData=0,data=0,comms=0;
    bool released=false;
    Board() {
        pmu.readInput=[](){return uint8_t(0x30);}; // on, no target disk mode
        pmu.readAnalog=[](unsigned p){return uint8_t(p==0?0x7f:p==1?0xff:p==5?0x40:p==7?0x10:0);};
        pmu.readPort=[this](unsigned p){return uint8_t(p==1?0x88:p==2?hostData:p==3?(req?0x80:0):p==4?2:0);};
        pmu.writePort=[this](unsigned p,uint8_t v){
            if(p==2) data=v;
            if(p==3) { comms=v;if((v&3)==3) released=true; }
        };
    }
    bool waitAck(bool high) {
        for(int n=0;n<100000;++n) {
            pmu.step();
            if(pmu.cpu().illegalOpcode()) return false;
            if(bool(comms&0x40)==high) return true;
        }
        std::fprintf(stderr,"PMACK timeout PC=%04x REQ=%d pins=%02x\n",pmu.cpu().r.pc,req,comms);
        return false;
    }
    bool send(uint8_t byte) {
        hostData=byte;req=false;
        if(!waitAck(false)) return false;
        req=true;return waitAck(true);
    }
    bool finishReceive() { req=false;if(!waitAck(true)) return false;req=true;return true; }
    bool receive(uint8_t& byte) {
        if(!waitAck(false)) return false;
        byte=data;return finishReceive();
    }
    std::vector<uint8_t> save() {
        std::vector<uint8_t> bytes;sav::Writer ar(bytes);ar(pmu,req,hostData,released);return bytes;
    }
    bool load(const std::vector<uint8_t>& bytes) {
        sav::Reader ar(bytes.data(),bytes.size());ar(pmu,req,hostData,released);
        return ar.ok() && !ar.remaining();
    }
};

bool runFirmware(const Firmware& input,const std::string& path) {
    std::printf("ASSET PMU %s sha256 %s\n",path.c_str(),input.hash);
    if(testasset::sha256File(path)!=input.hash) {
        std::fprintf(stderr,"FAIL firmware digest mismatch\n");return false;
    }
    std::ifstream file(path,std::ios::binary);
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)),{});
    Board b;
    if(!b.pmu.loadRom(rom)) return false;
    b.pmu.reset();
    // No RAM seed or firmware patch: observe an actual cold start.
    b.pmu.run(150000);
    if(!b.released || (b.comms&3)!=3 || b.pmu.cpu().illegalOpcode()) {
        std::fprintf(stderr,"FAIL firmware did not release the host CPU\n");return false;
    }
    // Old PMgr interface: command, send-count, reply, reply-count, data.
    if(!b.send(0xea) || !b.send(0) || !b.waitAck(false)) return false;
    if(b.data!=0xea) { std::fprintf(stderr,"FAIL wrong command echo\n");return false; }
    const auto offered=b.save(); // ACK low, first reply byte offered on port 2
    auto finish=[&](){
        uint8_t count=0,major=0,minor=0;
        if(!b.finishReceive() || !b.receive(count) || !b.receive(major) || !b.receive(minor)) return false;
        if(count!=2 || major!=input.major || minor!=input.minor) {
            std::fprintf(stderr,"FAIL version frame %02x %02x %02x\n",count,major,minor);return false;
        }
        b.pmu.run(2000);return !b.pmu.cpu().illegalOpcode();
    };
    if(!finish()) return false;
    const auto after=b.save();
    if(!b.load(offered) || b.data!=0xea || (b.comms&0x40) || !finish() || b.save()!=after) {
        std::fprintf(stderr,"FAIL PMU handshake snapshot replay\n");return false;
    }
    std::printf("PASS cold start; host released; version %02X%02X over PMREQ/PMACK; in-flight reply restored\n",
                input.major,input.minor);
    return true;
}
}
int main() {
    std::string paths[3];
    for(unsigned i=0;i<3;++i) {
        paths[i]=testasset::find(inputs[i].path);
        if(paths[i].empty()) { std::printf("SKIP: private M50753 firmware absent: %s\n",inputs[i].path);return 0; }
    }
    bool ok=true;
    for(unsigned i=0;i<3;++i) ok=runFirmware(inputs[i],paths[i]) && ok;
    return ok?0:1;
}
