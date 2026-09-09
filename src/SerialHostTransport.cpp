// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "SerialHostTransport.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }

bool makeNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool makeCloseOnExec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

ssize_t socketWrite(int fd, const void* data, std::size_t size) {
#if defined(__linux__)
    return ::send(fd, data, size, MSG_NOSIGNAL);
#else
    return ::send(fd, data, size, 0);
#endif
}
#endif
} // namespace

bool SerialHostTransport::start(Kind kind, std::uint16_t tcpPort) {
    stop();
    kind_ = kind;
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)tcpPort;
    return false;
#else
    return kind == Kind::Pty ? startPty() : startTcp(tcpPort);
#endif
}

bool SerialHostTransport::startPty() {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    return false;
#else
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0 || !makeCloseOnExec(master) ||
        ::grantpt(master) < 0 || ::unlockpt(master) < 0) {
        if (master >= 0) ::close(master);
        return false;
    }
    const char* slaveName = ::ptsname(master);
    if (!slaveName) {
        ::close(master);
        return false;
    }
    endpoint_ = slaveName;

    // Make the published side a transparent byte pipe before handing it to
    // screen, socat or a test. A client remains free to change termios later.
    const int slave = ::open(endpoint_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    termios mode{};
    if (slave < 0 || !makeCloseOnExec(slave) ||
        ::tcgetattr(slave, &mode) < 0) {
        if (slave >= 0) ::close(slave);
        ::close(master);
        endpoint_.clear();
        return false;
    }
    ::cfmakeraw(&mode);
    if (::tcsetattr(slave, TCSANOW, &mode) < 0) {
        ::close(slave);
        ::close(master);
        endpoint_.clear();
        return false;
    }
    // macOS resets a PTY's termios when the last slave descriptor closes.
    // Retain this control descriptor so later clients inherit raw/no-echo
    // semantics; it never consumes data.
    ptyControlFd_ = slave;
    ioFd_ = master;
    return true;
#endif
}

bool SerialHostTransport::startTcp(std::uint16_t port) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)port;
    return false;
#else
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;
    int one = 1;
    (void)::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) < 0 ||
        ::listen(listener, 1) < 0 || !makeNonBlocking(listener) ||
        !makeCloseOnExec(listener)) {
        ::close(listener);
        return false;
    }
    socklen_t addressSize = sizeof address;
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                      &addressSize) < 0) {
        ::close(listener);
        return false;
    }
    listenerFd_ = listener;
    tcpPort_ = ntohs(address.sin_port);
    endpoint_ = "127.0.0.1:" + std::to_string(tcpPort_);
    return true;
#endif
}

void SerialHostTransport::stop() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (ioFd_ >= 0) ::close(ioFd_);
    if (listenerFd_ >= 0) ::close(listenerFd_);
    if (ptyControlFd_ >= 0) ::close(ptyControlFd_);
#endif
    ioFd_ = listenerFd_ = ptyControlFd_ = -1;
    endpoint_.clear();
    tcpPort_ = 0;
    input_.clear();
    output_.clear();
}

bool SerialHostTransport::connected() const noexcept {
    return ioFd_ >= 0 && (kind_ == Kind::Pty || listenerFd_ >= 0);
}

void SerialHostTransport::closeClient() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (ioFd_ >= 0) ::close(ioFd_);
#endif
    ioFd_ = -1;
    bytesDropped_ += output_.size();
    output_.clear();
}

void SerialHostTransport::acceptTcp() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (kind_ != Kind::Tcp || listenerFd_ < 0 || ioFd_ >= 0) return;
    sockaddr_in peer{};
    socklen_t size = sizeof peer;
    const int client = ::accept(listenerFd_, reinterpret_cast<sockaddr*>(&peer), &size);
    if (client < 0) return;
    if (!makeNonBlocking(client) || !makeCloseOnExec(client)) {
        ::close(client);
        return;
    }
#ifdef __APPLE__
    int one = 1;
    (void)::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    ioFd_ = client;
#endif
}

void SerialHostTransport::sendByte(std::uint8_t value) {
    if (ioFd_ < 0) {
        ++bytesDropped_;
        return;
    }
    if (output_.size() >= kQueueLimit) {
        ++bytesDropped_;
        return;
    }
    output_.push_back(value);
    flushOutput();
}

void SerialHostTransport::flushOutput() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    while (ioFd_ >= 0 && !output_.empty()) {
        std::uint8_t buffer[1024];
        const std::size_t count = std::min(output_.size(), sizeof buffer);
        for (std::size_t i = 0; i < count; ++i) buffer[i] = output_[i];
        const ssize_t written = kind_ == Kind::Tcp
            ? socketWrite(ioFd_, buffer, count)
            : ::write(ioFd_, buffer, count);
        if (written > 0) {
            output_.erase(output_.begin(),
                          output_.begin() + std::ptrdiff_t(written));
            bytesTx_ += std::uint64_t(written);
            continue;
        }
        if (written < 0 && wouldBlock()) return;
        if (kind_ == Kind::Tcp) closeClient();
        else {
            // EIO means no process currently has the PTY slave open. Those
            // bytes were sent into an unplugged cable and must not replay.
            bytesDropped_ += output_.size();
            output_.clear();
        }
        return;
    }
#endif
}

void SerialHostTransport::drainInput() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (ioFd_ < 0) return;
    for (;;) {
        std::uint8_t buffer[1024];
        const ssize_t count = kind_ == Kind::Tcp
            ? ::recv(ioFd_, buffer, sizeof buffer, 0)
            : ::read(ioFd_, buffer, sizeof buffer);
        if (count > 0) {
            for (ssize_t i = 0; i < count; ++i) {
                if (input_.size() < kQueueLimit) {
                    input_.push_back(buffer[i]);
                    ++bytesRx_;
                } else {
                    ++bytesDropped_;
                }
            }
            continue;
        }
        if (count < 0 && (wouldBlock() ||
                          (kind_ == Kind::Pty && errno == EIO)))
            return;
        if (kind_ == Kind::Tcp) closeClient();
        return;
    }
#endif
}

void SerialHostTransport::poll() {
    acceptTcp();
    flushOutput();
    drainInput();
}

bool SerialHostTransport::readByte(std::uint8_t& value) {
    if (input_.empty()) return false;
    value = input_.front();
    input_.pop_front();
    return true;
}
