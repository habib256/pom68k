// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The « Ports série » window's view of one SCC channel, shared between the
// machine thread (which owns the SCC and the host transport) and the GUI.
//
// Decided once: the terminal OBSERVES every configured port and is the
// ENDPOINT of none but its own.
//
//   observer  a `pty` or `tcp:` port: the window shows the bytes the guest
//             sent and the bytes the SCC actually accepted from the host —
//             mirrored after the fact, so opening the window never consumes
//             a byte owed to the external bridge, and its input is off;
//   endpoint  a `terminal` port: no host transport; what the user types is
//             queued here (bounded, refused when full) and the machine
//             thread hands it to the SCC only when the receiver can take a
//             byte — the same backpressure a transport gets.
//
// Everything is bounded: the log keeps the last kLogBytes (older bytes
// counted, not kept), the input queue kInputBytes. Gate: scc_serial_host_test.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

class SerialTerminal {
public:
    static constexpr std::size_t kLogBytes = 16384;
    static constexpr std::size_t kInputBytes = 4096;

    enum class Direction : std::uint8_t { FromGuest, ToGuest };

    explicit SerialTerminal(bool endpoint) : endpoint_(endpoint) {}
    bool endpoint() const noexcept { return endpoint_; }

    // ── machine thread ───────────────────────────────────────────────────
    void observe(Direction direction, std::uint8_t value) {
        std::lock_guard<std::mutex> l(mu_);
        if (log_.size() == kLogBytes) {
            log_.pop_front();
            ++logDropped_;
        }
        log_.push_back({direction, value});
        ++(direction == Direction::FromGuest ? fromGuest_ : toGuest_);
    }
    // The next byte the user typed, for the SCC (endpoint ports only).
    bool takeInput(std::uint8_t& value) {
        std::lock_guard<std::mutex> l(mu_);
        if (input_.empty()) return false;
        value = input_.front();
        input_.pop_front();
        return true;
    }
    // The host transport's state, after each poll (observer ports).
    void publish(bool connected, std::uint64_t dropped) {
        std::lock_guard<std::mutex> l(mu_);
        connected_ = connected;
        transportDropped_ = dropped;
    }

    // ── GUI thread ───────────────────────────────────────────────────────
    // Queues what fits and returns how many bytes were taken: a full queue
    // refuses the rest instead of growing (the guest drains it at its
    // receiver's pace). An observer port takes nothing.
    std::size_t send(std::string_view bytes) {
        std::lock_guard<std::mutex> l(mu_);
        if (!endpoint_) return 0;
        std::size_t taken = 0;
        for (const char c : bytes) {
            if (input_.size() == kInputBytes) break;
            input_.push_back(std::uint8_t(c));
            ++taken;
        }
        return taken;
    }

    struct View {
        std::string text;            // printable; CR/LF as line ends, others \xNN
        std::uint64_t fromGuest = 0, toGuest = 0;
        std::uint64_t logDropped = 0, transportDropped = 0;
        std::size_t pendingInput = 0;
        bool connected = false;
    };
    View view() const {
        std::lock_guard<std::mutex> l(mu_);
        View v;
        v.fromGuest = fromGuest_;
        v.toGuest = toGuest_;
        v.logDropped = logDropped_;
        v.transportDropped = transportDropped_;
        v.pendingInput = input_.size();
        v.connected = connected_;
        v.text.reserve(log_.size());
        bool lastCr = false;
        for (const Entry& e : log_) {
            const std::uint8_t c = e.value;
            if (c == '\n' && lastCr) { lastCr = false; continue; } // CR LF: one line end
            lastCr = c == '\r';
            if (c == '\r' || c == '\n') v.text += '\n';
            else if (c == '\t' || (c >= 0x20 && c < 0x7F)) v.text += char(c);
            else {
                static const char hex[] = "0123456789ABCDEF";
                v.text += "\\x";
                v.text += hex[c >> 4];
                v.text += hex[c & 15];
            }
        }
        return v;
    }
    void clearLog() {
        std::lock_guard<std::mutex> l(mu_);
        log_.clear();
    }

private:
    struct Entry { Direction direction; std::uint8_t value; };
    const bool endpoint_;
    mutable std::mutex mu_;
    std::deque<Entry> log_;
    std::deque<std::uint8_t> input_;
    std::uint64_t fromGuest_ = 0, toGuest_ = 0, logDropped_ = 0, transportDropped_ = 0;
    bool connected_ = false;
};

// ── The per-channel pump, machine thread (GuiHostServices::pollNetwork) ──
// Host → guest for one SCC channel: a transport's bytes, else an endpoint
// terminal's input, each handed over only while the receiver can take a
// byte, and mirrored into the terminal only once the SCC took it.
template <class Scc, class Transport>
void pumpSerialChannel(Scc& scc, int channel, Transport* transport,
                       SerialTerminal* terminal) {
    std::uint8_t value = 0;
    if (transport) {
        transport->poll();
        while (scc.canInjectRxByte(channel) && transport->readByte(value)) {
            scc.injectRxByte(channel, value);
            if (terminal) terminal->observe(SerialTerminal::Direction::ToGuest, value);
        }
        if (terminal) terminal->publish(transport->connected(), transport->bytesDropped());
    } else if (terminal && terminal->endpoint()) {
        while (scc.canInjectRxByte(channel) && terminal->takeInput(value)) {
            scc.injectRxByte(channel, value);
            terminal->observe(SerialTerminal::Direction::ToGuest, value);
        }
    }
}

// Guest → host: the byte the SCC emitted, to the transport and the log.
template <class Transport>
void serialGuestByte(std::uint8_t value, Transport* transport, SerialTerminal* terminal) {
    if (transport) transport->sendByte(value);
    if (terminal) terminal->observe(SerialTerminal::Direction::FromGuest, value);
}
