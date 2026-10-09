// Test-only real host DNS/TCP peers and ordinary HFS delivery for NetProbe.
#pragma once
#include "HfsInject.h"
#include "HfsBlankVolume.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

class DaynaNetProbe {
public:
    ~DaynaNetProbe() {
#ifndef _WIN32
        for(int fd : {dns_,tcp_,peer_}) if(fd>=0) ::close(fd);
#endif
    }
    bool prepare(ScsiDisk& disk, const std::string& path) {
#ifndef _WIN32
        // Select the host's real IPv4 endpoint without sending any datagram.
        // Services bind ephemeral ports on that interface; no public server,
        // privileged port, address alias or emulator transport substitute.
        int route=::socket(AF_INET,SOCK_DGRAM,0);
        if(route<0) return false;
        sockaddr_in dest{}; dest.sin_family=AF_INET;
        dest.sin_addr.s_addr=htonl(0xcb007101); dest.sin_port=htons(9);
        sockaddr_in local{}; socklen_t size=sizeof local;
        const bool have=::connect(route,reinterpret_cast<sockaddr*>(&dest),sizeof dest)==0 &&
            ::getsockname(route,reinterpret_cast<sockaddr*>(&local),&size)==0;
        ::close(route);
        if(!have) return false;
        address_=ntohl(local.sin_addr.s_addr);
        if(!address_ || (address_>>24)==127 || (address_&0xffffff00)==0xc0a89700) return false;
        dns_=bindSocket(SOCK_DGRAM,local,dnsPort_);
        tcp_=bindSocket(SOCK_STREAM,local,tcpPort_);
        if(dns_<0 || tcp_<0 || ::listen(tcp_,1)) return false;
        char config[96];
        std::snprintf(config,sizeof config,"%u.%u.%u.%u %u %u\n",address_>>24,
            (address_>>16)&255,(address_>>8)&255,address_&255,dnsPort_,tcpPort_);
        std::printf("netprobe: controlled host endpoint %s",config);
        std::ifstream in("dev/netprobe/build/NetProbe.bin",std::ios::binary);
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),{});
        hfsinject::MacBinary app; std::string err;
        if(!hfsinject::decodeMacBinary(raw,app,err)) return false;
        hfsinject::ScsiDiskIo io(disk); uint32_t start=0,length=0,cnid=0;
        if(!hfsinject::findHfsVolume(io,start,length,err)) return false;
        hfsinject::Volume original(io,start,length);
        hfsinject::MacBinary ping;
        if(!original.open(err) || !original.readFile(2,"MacTCP Ping",ping,err)) return false;
        // The reference TOOLS catalogue has no spare node. Prepare an ordinary
        // larger HFS test volume, carrying the original Ping's exact two forks.
        auto image=hfsblank::build(8u<<20,"TOOLS");
        hfsinject::MemoryIo staging(image);
        hfsinject::Volume volume(staging,0,uint32_t(image.size()/512));
        hfsinject::MacBinary cfg; cfg.name="NetProbe.cfg"; cfg.type="TEXT"; cfg.creator="ttxt";
        cfg.data.assign(config,config+std::strlen(config));
        const bool installed=volume.open(err) && volume.addFile(2,ping,3870700000u,cnid,err) &&
            volume.addFile(2,app,3870700000u,cnid,err) &&
            volume.addFile(2,cfg,3870700000u,cnid,err) && volume.commit(err);
        if(!installed) std::fprintf(stderr,"netprobe HFS delivery: %s\n",err.c_str());
        return installed && hfsblank::writeFile(path,image) && disk.open(path,true);
#else
        (void)disk; (void)path; return false;
#endif
    }
    void poll() {
#ifndef _WIN32
        std::array<uint8_t,512> query{}; sockaddr_in client{}; socklen_t size=sizeof client;
        const ssize_t n=::recvfrom(dns_,query.data(),query.size(),0,
                                  reinterpret_cast<sockaddr*>(&client),&size);
        static constexpr uint8_t question[]={6,'p','o','m','6','8','k',4,'t','e','s','t',0,0,1,0,1};
        if(n==29 && query[0]==0x68 && query[1]==0x4b && query[2]==1 && query[3]==0 &&
           query[4]==0 && query[5]==1 && !query[6] && !query[7] && !query[8] &&
           !query[9] && !query[10] && !query[11] &&
           std::equal(std::begin(question),std::end(question),query.begin()+12)) {
            std::vector<uint8_t> reply(query.begin(),query.begin()+n);
            reply[2]=0x81; reply[3]=0x80; reply[7]=1;
            const uint8_t answer[]={0xc0,0x0c,0,1,0,1,0,0,0,30,0,4,
                uint8_t(address_>>24),uint8_t(address_>>16),uint8_t(address_>>8),uint8_t(address_)};
            reply.insert(reply.end(),std::begin(answer),std::end(answer));
            if(::sendto(dns_,reply.data(),reply.size(),0,
                        reinterpret_cast<sockaddr*>(&client),size)==ssize_t(reply.size())) dnsAnswered_++;
        }
        if(peer_<0) {
            peer_=::accept(tcp_,nullptr,nullptr);
            if(peer_>=0) nonblock(peer_);
        }
        if(peer_<0) return;
        std::array<char,256> b{}; const ssize_t count=::recv(peer_,b.data(),b.size(),0);
        if(count>0) {
            if(request_.size()+size_t(count)>256) { bad_=true; return; }
            request_.append(b.data(),size_t(count));
        } else if(count==0) eof_=true;
        const auto& request=kRequest;
        const auto& reply=kReply;
        if(request_.size()>=sizeof request-1 && request_!=request) { bad_=true; return; }
        if(request_==request && sent_<sizeof reply-1) {
            const ssize_t bytes=::send(peer_,reply+sent_,sizeof reply-1-sent_,0);
            if(bytes>0) sent_+=size_t(bytes);
            if(sent_==sizeof reply-1) ::shutdown(peer_,SHUT_WR);
        }
#endif
    }
    std::string report(ScsiDisk& disk) {
        hfsinject::ScsiDiskIo io(disk); uint32_t start=0,length=0; std::string err;
        hfsinject::MacBinary file;
        if(!hfsinject::findHfsVolume(io,start,length,err)) return {};
        hfsinject::Volume volume(io,start,length);
        if(!volume.open(err) || !volume.readFile(2,"NetProbe.txt",file,err)) return {};
        return {file.data.begin(),file.data.end()};
    }
    bool ok() const { return dnsAnswered_>0 && sent_==sizeof kReply-1 && eof_ && !bad_; }
    void describe() const {
        std::printf("netprobe host: DNS replies=%u, TCP request=%zu bytes, reply=%zu, EOF=%d bad=%d\n",
                    dnsAnswered_,request_.size(),sent_,eof_,bad_);
    }
private:
    inline static constexpr char kRequest[]="POM68K MacTCP DNS-to-TCP request\r\n";
    inline static constexpr char kReply[]="POM68K host TCP reply after DNS\r\n";
#ifndef _WIN32
    static void nonblock(int fd) { ::fcntl(fd,F_SETFL,::fcntl(fd,F_GETFL,0)|O_NONBLOCK); }
    static int bindSocket(int type,sockaddr_in address,uint16_t& port) {
        int fd=::socket(AF_INET,type,0); if(fd<0) return -1;
        address.sin_port=0; socklen_t length=sizeof address;
        if(::bind(fd,reinterpret_cast<sockaddr*>(&address),length) ||
           ::getsockname(fd,reinterpret_cast<sockaddr*>(&address),&length)) { ::close(fd); return -1; }
        port=ntohs(address.sin_port); nonblock(fd); return fd;
    }
#endif
    int dns_=-1,tcp_=-1,peer_=-1; uint32_t address_=0;
    uint16_t dnsPort_=0,tcpPort_=0; unsigned dnsAnswered_=0;
    std::string request_; size_t sent_=0; bool eof_=false,bad_=false;
};
