// Independent PCAP decoding, real card callbacks and bounded writer pressure.
#include "AtalkHub.h"
#include "DaynaPort.h"
#include "atalk_test_util.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace {
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
std::vector<uint8_t> read(const std::string& path) {
    std::ifstream in(path,std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)),{}};
}
template<class Poll> bool wait(Poll poll) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!poll() && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return poll();
}
}
int main() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/
        ("pom68k-capture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    struct Machine {
        Scc8530 serial;
        DaynaPort card;
        Scc8530& scc() { return serial; }
        DaynaPort& daynaPort() { return card; }
        int64_t cpuHz() const { return 25000000; }
    } mem;
    AtalkHub hub;
    int64_t now=31250000; // exact 1.25 seconds at the board clock
    CHECK(!hub.startEthernetCapture((root/"absent.pcap").string()),"no card/clock refuses capture");
    mem.card.attach();
    std::vector<uint8_t> out,none;
    const uint8_t enable[]={0x0e,0,0,0,0,0x80};
    mem.card.command(enable,6,out,none);
    hub.setService("stack",false); hub.setService("ethertalk",false);
    hub.attach(mem,1000000,nullptr,[&] { return now; }); // hub pace != board Hz
    const auto path=(root/"link.pcap").string();
    CHECK(hub.startEthernetCapture(path),"start with an attached real card clock");
    CHECK(!hub.startEthernetCapture(path),"second start cannot replace an active capture");
    int delivered=0;
    auto transport=mem.card.sendFrame;
    mem.card.sendFrame=[&](const uint8_t* d,size_t n) { ++delivered; transport(d,n); };
    std::vector<uint8_t> tx(60,0x42),rx(60,0x53);
    tx[12]=rx[12]=0x88; tx[13]=rx[13]=0xb5; // unrelated protocol, no gateway response
    const uint8_t write[]={0x0a,0,0,0,60,0};
    unsigned attempts=0;
    // A contended observer is allowed to lose a sample. Retry only a reported
    // loss; each actual card operation still reaches its transport exactly once.
    auto sample=[&](bool transmitted,const std::vector<uint8_t>& frame,uint64_t target) {
        for(int i=0;i<50;++i) {
            const auto before=hub.snapshot().capture;
            if(transmitted) { ++attempts; mem.card.command(write,6,out,frame); }
            else mem.card.receiveFrame(frame.data(),frame.size());
            wait([&] { const auto s=hub.snapshot().capture;
                return s.written>=target || s.dropped>before.dropped; });
            if(hub.snapshot().capture.written>=target) return true;
        }
        return false;
    };
    CHECK(sample(true,tx,1),"transmit is observed without substituting its transport");
    now+=25000; // one machine millisecond; no dependence on host delays
    CHECK(sample(false,rx,2),"receive is observed before the ring");
    const uint8_t readCdb[]={0x08,0,0,0,0,0xc0};
    mem.card.command(readCdb,6,out,none);
    hub.stopEthernetCapture();
    CHECK(wait([&] { return !hub.snapshot().capture.busy; }),"stop drains and closes asynchronously");
    auto status=hub.snapshot().capture;
    CHECK(status.error.empty() && status.written==2 &&
          status.submitted==status.written+status.dropped,"written/lost observations account for every sample");
    CHECK(delivered==int(attempts) && mem.card.framesFromGuest==attempts,
          "capture start/stop never duplicates guest transmissions");
    const auto data=read(path);
    CHECK(data.size()==24+2*(16+60),"PCAP contains exactly two complete frames, no READ duplicate");
    if(data.size()==176) {
        CHECK(u32(data.data())==0xa1b2c3d4 && data[4]==2 && data[6]==4 &&
              u32(data.data()+8)==0 && u32(data.data()+12)==0 &&
              u32(data.data()+16)==1514 && u32(data.data()+20)==1,"standard Ethernet PCAP 2.4 header");
        CHECK(u32(data.data()+24)==1 && u32(data.data()+28)==250000 &&
              u32(data.data()+32)==60 && u32(data.data()+36)==60,"TX timestamp and exact frame lengths");
        CHECK(std::equal(tx.begin(),tx.end(),data.begin()+40),"TX raw bytes intact");
        CHECK(u32(data.data()+100)==1 && u32(data.data()+104)==251000 &&
              std::equal(rx.begin(),rx.end(),data.begin()+116),"RX order, machine time and bytes intact");
    }
    const auto notes=read(path+".tsv");
    const std::string metadata(notes.begin(),notes.end());
    CHECK(metadata.find("# clock_hz\t25000000")!=std::string::npos &&
          metadata.find("1\t31250000\ttx")!=std::string::npos &&
          metadata.find("2\t31275000\trx")!=std::string::npos,"sidecar records actual clock and both directions");
    CHECK(hub.startEthernetCapture(path),"file refusal is asynchronous");
    CHECK(wait([&] { return !hub.snapshot().capture.busy; }) &&
          !hub.snapshot().capture.error.empty() && read(path)==data,"existing capture is never overwritten");

    // Observe arrival on the wire even when the hardware ring cannot admit it.
    // Polling READ consumes buffered frames; it must not observe them again.
    while(mem.card.queued()) mem.card.command(readCdb,6,out,none);
    unsigned arrivals=0;
    mem.card.observeFrame=[&](bool transmitted,const uint8_t*,size_t) {
        if(!transmitted) ++arrivals;
    };
    std::vector<uint8_t> large(1514,0x5a);
    const auto dropsBefore=mem.card.framesDropped;
    for(int i=0;i<5;++i) mem.card.receiveFrame(large.data(),large.size());
    CHECK(arrivals==5 && mem.card.queued()==4 && mem.card.framesDropped==dropsBefore+1,
          "ring loss and passive wire observation have distinct accounting");
    for(int i=0;i<4;++i) mem.card.command(readCdb,6,out,none);
    CHECK(arrivals==5 && mem.card.queued()==0,"SCSI reads do not recapture arrivals");

    pom68k::EthernetCapture pressure;
    const auto pressurePath=(root/"pressure.pcap").string();
    CHECK(pressure.start(pressurePath,1000000),"new session after the previous capture");
    std::vector<uint8_t> frame(1514,0x5a);
    bool bounded=true;
    for(int i=0;i<10000;++i) {
        pressure.observe(i,true,frame.data(),frame.size());
        if(i%100==0) bounded &= pressure.status().queued<=pressure.kQueueFrames;
    }
    pressure.stop();
    CHECK(wait([&] { return !pressure.status().busy; }),"pressure capture drains");
    status=pressure.status();
    CHECK(bounded && status.error.empty() && status.submitted==10000 &&
          status.written+status.dropped==10000,"bounded queue pressure accounts for output and loss");
    CHECK(pressure.start((root/"missing"/"file.pcap").string(),1000000),"bad path handled by worker");
    CHECK(wait([&] { return !pressure.status().busy; }) && !pressure.status().error.empty(),
          "I/O failure is visible and does not wedge the machine");
    const auto concurrentPath=(root/"concurrent.pcap").string();
    CHECK(pressure.start(concurrentPath,1000000),"concurrent stop session starts");
    std::thread producer([&] {
        for(int i=0;i<100000;++i) pressure.observe(i,true,frame.data(),frame.size());
    });
    wait([&] { return pressure.status().submitted>=1000; });
    pressure.stop(); producer.join();
    CHECK(wait([&] { return !pressure.status().busy; }),"GUI stop overlaps the machine producer safely");
    status=pressure.status();
    const auto footerBytes=read(concurrentPath+".tsv");
    const std::string footer(footerBytes.begin(),footerBytes.end());
    CHECK(status.error.empty() && status.submitted==status.written+status.dropped &&
          footer.find("# dropped\t"+std::to_string(status.dropped)+"\n")!=std::string::npos,
          "concurrent stop publishes complete loss accounting before closing metadata");
    CHECK(pressure.start((root/"backwards.pcap").string(),1000000),"restart after an I/O error");
    for(int i=0;i<50 && pressure.status().written==0;++i) {
        const auto losses=pressure.status().dropped;
        pressure.observe(100,true,frame.data(),frame.size());
        wait([&] { return pressure.status().written==1 || pressure.status().dropped>losses; });
    }
    for(int i=0;i<50 && pressure.status().busy;++i) {
        const auto losses=pressure.status().dropped;
        pressure.observe(50,false,frame.data(),frame.size());
        wait([&] { return !pressure.status().busy || pressure.status().dropped>losses; });
    }
    pressure.stop();
    CHECK(wait([&] { return !pressure.status().busy; }) && !pressure.status().error.empty(),
          "restore/reset clock reversal ends capture with a visible error");
    fs::remove_all(root);
    std::printf("ethernet_capture_test: %s\n",failures?"FAILED":"PASSED");
    return failures?1:0;
}
