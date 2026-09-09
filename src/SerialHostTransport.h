// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Non-blocking host endpoint for one asynchronous SCC channel. TCP listens
// only on loopback and keeps accepting after a client disconnects; PTY owns
// one Unix98/BSD pseudo-terminal master and publishes its slave path.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

class SerialHostTransport {
public:
    enum class Kind { Pty, Tcp };

    SerialHostTransport() = default;
    ~SerialHostTransport() { stop(); }
    SerialHostTransport(const SerialHostTransport&) = delete;
    SerialHostTransport& operator=(const SerialHostTransport&) = delete;

    bool start(Kind kind, std::uint16_t tcpPort = 0);
    void stop();
    bool active() const noexcept { return listenerFd_ >= 0 || ioFd_ >= 0; }
    bool connected() const noexcept;
    Kind kind() const noexcept { return kind_; }
    const std::string& endpoint() const noexcept { return endpoint_; }
    std::uint16_t tcpPort() const noexcept { return tcpPort_; }

    // All calls are made by the machine thread. sendByte never blocks; TCP
    // output with no connected client is lost like an unplugged serial lead.
    void sendByte(std::uint8_t value);
    void poll();
    bool readByte(std::uint8_t& value);

    std::uint64_t bytesTx() const noexcept { return bytesTx_; }
    std::uint64_t bytesRx() const noexcept { return bytesRx_; }
    std::uint64_t bytesDropped() const noexcept { return bytesDropped_; }

private:
    bool startPty();
    bool startTcp(std::uint16_t port);
    void acceptTcp();
    void flushOutput();
    void drainInput();
    void closeClient();

    static constexpr std::size_t kQueueLimit = 64 * 1024;
    Kind kind_ = Kind::Pty;
    int listenerFd_ = -1;
    int ioFd_ = -1;
    int ptyControlFd_ = -1; // keeps the raw slave termios alive
    std::string endpoint_;
    std::uint16_t tcpPort_ = 0;
    std::deque<std::uint8_t> input_;
    std::deque<std::uint8_t> output_;
    std::uint64_t bytesTx_ = 0;
    std::uint64_t bytesRx_ = 0;
    std::uint64_t bytesDropped_ = 0;
};
