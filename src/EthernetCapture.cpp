// POM68K — VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#include "EthernetCapture.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

namespace pom68k {
namespace {
void le32(uint8_t* out, uint32_t value) {
    for(int i=0;i<4;++i) out[i]=uint8_t(value>>(8*i));
}
bool bytes(FILE* f,const void* p,size_t n) { return std::fwrite(p,1,n,f)==n; }
}
EthernetCapture::~EthernetCapture() {
    stop();
    if(worker_.joinable()) worker_.join(); // owner tears down after the machine
}
bool EthernetCapture::start(std::string path,int64_t clockHz) {
    if(busy_.load() || path.empty() || clockHz<=0 ||
       uint64_t(clockHz)>std::numeric_limits<uint64_t>::max()/1000000) return false;
    if(worker_.joinable()) worker_.join(); // busy is cleared only after close
    {
        std::lock_guard<std::mutex> lock(mu_);
        path_=path; error_.clear(); head_=count_=0;
    }
    submitted_=0; written_=0; dropped_=0;
    busy_=true; accepting_=true;
    try { worker_=std::thread(&EthernetCapture::write,this,std::move(path),clockHz); }
    catch(const std::exception& e) {
        fail(e.what()); finishObservations(); busy_=false; return false;
    }
    return true;
}
void EthernetCapture::stop() {
    { std::lock_guard<std::mutex> lock(mu_); accepting_=false; }
    ready_.notify_one();
}
EthernetCapture::Status EthernetCapture::status() const {
    std::lock_guard<std::mutex> lock(mu_);
    return {accepting_.load(),busy_.load(),submitted_.load(),written_.load(),
            dropped_.load(),count_,path_,error_};
}
void EthernetCapture::observe(int64_t cycles,bool tx,const uint8_t* data,size_t n) {
    if(!data || n<14 || n>kMaxFrame) return;
    ++observations_;
    struct Done { std::atomic<unsigned>& count; ~Done() { --count; } } done{observations_};
    if(!accepting_.load()) return;
    ++submitted_;
    std::unique_lock<std::mutex> lock(mu_,std::try_to_lock);
    if(!lock.owns_lock()) { ++dropped_; return; }
    if(!accepting_.load(std::memory_order_relaxed) || count_==kQueueFrames) {
        ++dropped_; return;
    }
    Frame& frame=queue_[(head_+count_)%kQueueFrames];
    frame.cycles=cycles; frame.transmitted=tx; frame.size=uint32_t(n);
    std::memcpy(frame.data.data(),data,n); ++count_;
    lock.unlock(); ready_.notify_one();
}
void EthernetCapture::fail(std::string error) {
    accepting_=false;
    std::lock_guard<std::mutex> lock(mu_);
    if(error_.empty()) error_=std::move(error);
    dropped_+=count_; count_=0;
}
void EthernetCapture::finishObservations() {
    // Only the host worker/control thread waits. An observer that entered
    // before stop/error must finish its loss accounting before the footer.
    while(observations_.load()) std::this_thread::yield();
}
void EthernetCapture::write(std::string path,int64_t hz) {
    // libpcap's pcap-savefile specification: explicit little-endian 2.4,
    // microseconds, LINKTYPE_ETHERNET (1), normalized frames without FCS.
    // https://github.com/the-tcpdump-group/libpcap/blob/master/pcap-savefile.manfile.in
    FILE* pcap=std::fopen(path.c_str(),"wbx");
    if(!pcap) {
        fail("PCAP: "+std::string(std::strerror(errno))); finishObservations();
        busy_=false; return;
    }
    const std::string metadata=path+".tsv";
    // Binary: the companion's lines end in LF on every host (Windows' text
    // mode would write CRLF, and the gate's reader matches "\n").
    FILE* meta=std::fopen(metadata.c_str(),"wbx");
    if(!meta) {
        fail("Metadata: "+std::string(std::strerror(errno)));
        finishObservations();
        std::fclose(pcap); busy_=false; return;
    }
    uint8_t header[24]{};
    le32(header,0xa1b2c3d4); header[4]=2; header[6]=4;
    le32(header+16,kMaxFrame); le32(header+20,1);
    bool good=bytes(pcap,header,sizeof header) &&
        std::fprintf(meta,"# clock_domain\tmachine_cycles\n# clock_hz\t%lld\n"
                          "# pcap_epoch_seconds\t0\nindex\tcycles\tdirection\n",
                     (long long)hz)>0;
    int64_t last=-1;
    while(good) {
        Frame frame;
        {
            std::unique_lock<std::mutex> lock(mu_);
            ready_.wait(lock,[&] { return count_ || !accepting_.load(); });
            if(!count_) {
                if(observations_.load()) { lock.unlock(); std::this_thread::yield(); continue; }
                break;
            }
            frame=queue_[head_]; head_=(head_+1)%kQueueFrames; --count_;
        }
        if(frame.cycles<0 || frame.cycles<last || uint64_t(frame.cycles/hz)>UINT32_MAX) {
            ++dropped_; fail("Machine clock moved backwards or exceeds PCAP range");
            good=false; break;
        }
        last=frame.cycles;
        uint8_t packet[16];
        le32(packet,uint32_t(frame.cycles/hz));
        le32(packet+4,uint32_t(uint64_t(frame.cycles%hz)*1000000/uint64_t(hz)));
        le32(packet+8,frame.size); le32(packet+12,frame.size);
        if(!bytes(pcap,packet,sizeof packet) || !bytes(pcap,frame.data.data(),frame.size)) {
            ++dropped_; good=false; break;
        }
        const uint64_t index=++written_;
        good=std::fprintf(meta,"%llu\t%lld\t%s\n",(unsigned long long)index,
                          (long long)frame.cycles,frame.transmitted?"tx":"rx")>0;
    }
    if(!good) fail("Capture write failed");
    finishObservations();
    if(std::fprintf(meta,"# written\t%llu\n# dropped\t%llu\n",
        (unsigned long long)written_.load(),(unsigned long long)dropped_.load())<0)
        fail("Metadata write failed");
    const bool pcapClosed=std::fclose(pcap)==0;
    const bool metaClosed=std::fclose(meta)==0;
    if(!pcapClosed || !metaClosed) fail("Capture close failed");
    accepting_=false; busy_=false;
}
}
