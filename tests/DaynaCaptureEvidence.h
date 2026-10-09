// Independent decoding of the real guest's PCAP, not writer internals.
#pragma once
#include "AtalkHub.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

inline bool daynaCaptureEvidence(AtalkHub& hub,const std::string& path) {
    hub.stopEthernetCapture();
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(hub.snapshot().capture.busy && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto status=hub.snapshot().capture;
    std::ifstream input(path,std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    auto le=[](const uint8_t* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|
        (uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); };
    auto be=[](const uint8_t* p) { return unsigned(p[0])*256+p[1]; };
    if(status.busy || !status.error.empty() || data.size()<24 ||
       le(data.data())!=0xa1b2c3d4 || le(data.data()+20)!=1) return false;
    unsigned rarpRequest=0,rarpReply=0,dnsQuery=0,dnsReply=0,tcpRequest=0,tcpReply=0;
    size_t records=0,pos=24; uint64_t previous=0;
    while(pos+16<=data.size()) {
        const uint32_t size=le(data.data()+pos+8);
        const uint64_t stamp=uint64_t(le(data.data()+pos))*1000000+le(data.data()+pos+4);
        if(size<14 || size>1514 || size!=le(data.data()+pos+12) ||
           size>data.size()-pos-16 || stamp<previous) return false;
        previous=stamp; const uint8_t* d=data.data()+pos+16;
        const unsigned type=be(d+12);
        if(type==0x8035 && size>=42) {
            rarpRequest+=be(d+20)==3; rarpReply+=be(d+20)==4;
        }
        if(type==0x0800 && size>=34) {
            const size_t ip=(d[14]&15)*4;
            if(ip<20 || 14+ip>size) return false;
            const uint8_t* transport=d+14+ip; const size_t n=size-14-ip;
            if(d[23]==17 && n>=10 && transport[8]==0x68 && transport[9]==0x4b) {
                if(n>=12 && transport[10]&0x80) ++dnsReply; else ++dnsQuery;
            }
            if(d[23]==6 && n>=20) {
                const size_t header=(transport[12]>>4)*4;
                if(header<20 || header>n) return false;
                static constexpr char request[]="POM68K MacTCP DNS-to-TCP request\r\n";
                static constexpr char reply[]="POM68K host TCP reply after DNS\r\n";
                if(be(d+16)<ip+header || be(d+16)>ip+n) return false;
                const size_t length=be(d+16)-ip-header;
                if(length<=n-header) {
                    tcpRequest+=length==sizeof request-1 &&
                        !std::memcmp(transport+header,request,sizeof request-1);
                    tcpReply+=length==sizeof reply-1 &&
                        !std::memcmp(transport+header,reply,sizeof reply-1);
                }
            }
        }
        ++records; pos+=16+size;
    }
    std::printf("PCAP: %zu frames, %llu observations lost; RARP %u/%u DNS %u/%u TCP payload %u/%u\n",
        records,(unsigned long long)status.dropped,rarpRequest,rarpReply,dnsQuery,dnsReply,tcpRequest,tcpReply);
    return pos==data.size() && records==status.written &&
        status.submitted==status.written+status.dropped &&
        rarpRequest && rarpReply && dnsQuery && dnsReply && tcpRequest && tcpReply;
}
