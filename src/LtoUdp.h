// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── LToUDP: LocalTalk-over-UDP transport ──
// The de-facto standard virtual LLAP cable (Mini vMac, TashRouter, netatalk
// bridges): raw LLAP frames on UDP multicast 239.192.76.84:1954, each
// datagram prefixed with a 4-byte per-instance sender tag so a node ignores
// its own multicast-looped packets. No FCS on the wire — each end's SCC
// model regenerates/validates it (Scc8530::injectRxFrame appends it).
//
// Usage: start() once, wire Scc8530::onTxFrame (channel B = LocalTalk) to
// send(), and call poll() from the machine loop — every foreign frame is
// handed to the callback for injectRxFrame(). All calls are non-blocking.

#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

class LtoUdp {
public:
    ~LtoUdp() { stop(); }

    bool start();                        // join the multicast group
    void stop();
    bool active() const { return fd_ >= 0; }

    void send(const uint8_t* frame, size_t n);
    // Drain pending datagrams; cb receives each foreign LLAP frame.
    void poll(const std::function<void(const uint8_t*, size_t)>& cb);

    long framesTx = 0, framesRx = 0;     // debug counters

private:
    int fd_ = -1;
    uint32_t tag_ = 0;                   // random per-instance sender tag
};
