// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// LToUDP wire format (Mini vMac LTOVUDP / TashRouter "ltoudp" port):
// UDP multicast 239.192.76.84:1954, payload = 4-byte sender tag + LLAP
// frame (dest, src, type, payload...). Loopback stays enabled so several
// instances on one host see each other; the tag filters self-reception.

#include "LtoUdp.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr const char* kGroup = "239.192.76.84";
constexpr uint16_t kPort = 1954;
constexpr size_t kMaxFrame = 700;        // LLAP max is 3 + 600 + margin
} // namespace

bool LtoUdp::start() {
#ifdef _WIN32
    return false;                        // POSIX-only for now
#else
    if (fd_ >= 0) return true;
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kPort);
    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        std::perror("LToUDP bind");
        stop();
        return false;
    }
    ip_mreq mreq{};
    inet_pton(AF_INET, kGroup, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) < 0) {
        std::perror("LToUDP join");
        stop();
        return false;
    }
    unsigned char loop = 1;              // same-host instances must hear us
    setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    // Per-instance tag: pid ⊕ clock — only needs to differ between peers.
    tag_ = uint32_t(::getpid()) ^ uint32_t(std::clock()) ^ 0x504F4D38u; // 'POM8'
    std::fprintf(stderr, "LToUDP: joined %s:%u (tag %08X)\n", kGroup, kPort, tag_);
    return true;
#endif
}

void LtoUdp::stop() {
#ifndef _WIN32
    if (fd_ >= 0) ::close(fd_);
#endif
    fd_ = -1;
}

void LtoUdp::send(const uint8_t* frame, size_t n) {
#ifndef _WIN32
    if (fd_ < 0 || !n || n > kMaxFrame) return;
    uint8_t pkt[4 + kMaxFrame];
    std::memcpy(pkt, &tag_, 4);
    std::memcpy(pkt + 4, frame, n);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    inet_pton(AF_INET, kGroup, &dst.sin_addr);
    dst.sin_port = htons(kPort);
    ::sendto(fd_, pkt, n + 4, 0, reinterpret_cast<sockaddr*>(&dst), sizeof dst);
    framesTx++;
#else
    (void)frame; (void)n;
#endif
}

void LtoUdp::poll(const std::function<void(const uint8_t*, size_t)>& cb) {
#ifndef _WIN32
    if (fd_ < 0) return;
    uint8_t pkt[4 + kMaxFrame];
    for (;;) {
        ssize_t n = ::recv(fd_, pkt, sizeof pkt, 0);
        if (n <= 4) {
            if (n < 0) break;            // EWOULDBLOCK: drained
            continue;                    // runt datagram
        }
        uint32_t tag;
        std::memcpy(&tag, pkt, 4);
        if (tag == tag_) continue;       // our own multicast loopback
        framesRx++;
        cb(pkt + 4, size_t(n) - 4);
    }
#else
    (void)cb;
#endif
}
