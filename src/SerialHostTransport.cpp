// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "SerialHostTransport.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#elif !defined(__EMSCRIPTEN__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

// ── The socket layer: the only code that differs between BSD sockets and
// Winsock. Handles travel as std::intptr_t, -1 meaning none.
#if defined(_WIN32)
#define POM68K_SERIAL_SOCKETS 1

bool socketsReady() {
    static const bool ready = [] {
        WSADATA data;
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}
SOCKET sock(std::intptr_t h) { return SOCKET(h); }
SOCKET native(std::intptr_t h) { return SOCKET(h); }
bool wouldBlock() { return ::WSAGetLastError() == WSAEWOULDBLOCK; }
bool makeNonBlocking(std::intptr_t h) {
    u_long on = 1;
    return ::ioctlsocket(sock(h), FIONBIO, &on) == 0;
}
bool makeCloseOnExec(std::intptr_t) { return true; }   // no fork/exec to leak into
void closeSocket(std::intptr_t h) { ::closesocket(sock(h)); }
long socketWrite(std::intptr_t h, const std::uint8_t* data, std::size_t size) {
    return ::send(sock(h), reinterpret_cast<const char*>(data), int(size), 0);
}
long socketRead(std::intptr_t h, std::uint8_t* data, std::size_t size) {
    return ::recv(sock(h), reinterpret_cast<char*>(data), int(size), 0);
}
std::intptr_t socketOpen() {
    if (!socketsReady()) return -1;
    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    return s == INVALID_SOCKET ? -1 : std::intptr_t(s);
}
std::intptr_t socketAccept(std::intptr_t listener) {
    const SOCKET s = ::accept(sock(listener), nullptr, nullptr);
    return s == INVALID_SOCKET ? -1 : std::intptr_t(s);
}
using AddressSize = int;
#elif !defined(__EMSCRIPTEN__)
#define POM68K_SERIAL_SOCKETS 1

int native(std::intptr_t fd) { return int(fd); }
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
bool makeNonBlocking(std::intptr_t fd) {
    const int flags = ::fcntl(int(fd), F_GETFL, 0);
    return flags >= 0 && ::fcntl(int(fd), F_SETFL, flags | O_NONBLOCK) == 0;
}
bool makeCloseOnExec(std::intptr_t fd) {
    const int flags = ::fcntl(int(fd), F_GETFD, 0);
    return flags >= 0 && ::fcntl(int(fd), F_SETFD, flags | FD_CLOEXEC) == 0;
}
void closeSocket(std::intptr_t fd) { ::close(int(fd)); }
long socketWrite(std::intptr_t fd, const std::uint8_t* data, std::size_t size) {
#if defined(__linux__)
    return long(::send(int(fd), data, size, MSG_NOSIGNAL));
#else
    return long(::send(int(fd), data, size, 0));
#endif
}
long socketRead(std::intptr_t fd, std::uint8_t* data, std::size_t size) {
    return long(::recv(int(fd), data, size, 0));
}
std::intptr_t socketOpen() { return ::socket(AF_INET, SOCK_STREAM, 0); }
std::intptr_t socketAccept(std::intptr_t listener) {
    return ::accept(int(listener), nullptr, nullptr);
}
using AddressSize = socklen_t;
#else
#define POM68K_SERIAL_SOCKETS 0
#endif

} // namespace

bool SerialHostTransport::start(Kind kind, std::uint16_t tcpPort) {
    stop();
    kind_ = kind;
    return kind == Kind::Pty ? startPty() : startTcp(tcpPort);
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
#if !POM68K_SERIAL_SOCKETS
    (void)port;
    return false;
#else
    {
        const std::intptr_t listener = socketOpen();
        if (listener < 0) return false;
#if !defined(_WIN32)
        // Winsock's SO_REUSEADDR lets a second process steal a bound port;
        // only the BSD meaning (rebind past TIME_WAIT) is wanted.
        int one = 1;
        (void)::setsockopt(native(listener), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        AddressSize addressSize = sizeof address;
        if (::bind(native(listener), reinterpret_cast<sockaddr*>(&address),
                   sizeof address) != 0 ||
            ::listen(native(listener), 1) != 0 || !makeNonBlocking(listener) ||
            !makeCloseOnExec(listener) ||
            ::getsockname(native(listener), reinterpret_cast<sockaddr*>(&address),
                          &addressSize) != 0) {
            closeSocket(listener);
            return false;
        }
        listenerFd_ = listener;
        tcpPort_ = ntohs(address.sin_port);
        endpoint_ = "127.0.0.1:" + std::to_string(tcpPort_);
        return true;
    }
#endif
}

void SerialHostTransport::stop() {
#if POM68K_SERIAL_SOCKETS
    if (ioFd_ >= 0) {
        if (kind_ == Kind::Tcp) closeSocket(ioFd_);
#if !defined(_WIN32)
        else ::close(int(ioFd_));
#endif
    }
    if (listenerFd_ >= 0) closeSocket(listenerFd_);
#if !defined(_WIN32)
    if (ptyControlFd_ >= 0) ::close(ptyControlFd_);
#endif
#endif
    ioFd_ = listenerFd_ = -1;
    ptyControlFd_ = -1;
    endpoint_.clear();
    tcpPort_ = 0;
    input_.clear();
    output_.clear();
}

bool SerialHostTransport::connected() const noexcept {
    return ioFd_ >= 0 && (kind_ == Kind::Pty || listenerFd_ >= 0);
}

void SerialHostTransport::closeClient() {
#if POM68K_SERIAL_SOCKETS
    if (ioFd_ >= 0) closeSocket(ioFd_);
#endif
    ioFd_ = -1;
    bytesDropped_ += output_.size();
    output_.clear();
}

void SerialHostTransport::acceptTcp() {
#if POM68K_SERIAL_SOCKETS
    {
        if (kind_ != Kind::Tcp || listenerFd_ < 0 || ioFd_ >= 0) return;
        const std::intptr_t client = socketAccept(listenerFd_);
        if (client < 0) return;
        if (!makeNonBlocking(client) || !makeCloseOnExec(client)) {
            closeSocket(client);
            return;
        }
#ifdef __APPLE__
        int one = 1;
        (void)::setsockopt(native(client), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        ioFd_ = client;
    }
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
#if POM68K_SERIAL_SOCKETS
    {
        while (ioFd_ >= 0 && !output_.empty()) {
            std::uint8_t buffer[1024];
            const std::size_t count = std::min(output_.size(), sizeof buffer);
            for (std::size_t i = 0; i < count; ++i) buffer[i] = output_[i];
            long written = 0;
            if (kind_ == Kind::Tcp) written = socketWrite(ioFd_, buffer, count);
#if !defined(_WIN32)
            else written = long(::write(int(ioFd_), buffer, count));
#endif
            if (written > 0) {
                // A partial write keeps the rest queued, in order.
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
    }
#endif
}

void SerialHostTransport::drainInput() {
#if POM68K_SERIAL_SOCKETS
    {
        if (ioFd_ < 0) return;
        for (;;) {
            std::uint8_t buffer[1024];
            long count = 0;
            if (kind_ == Kind::Tcp) count = socketRead(ioFd_, buffer, sizeof buffer);
#if !defined(_WIN32)
            else count = long(::read(int(ioFd_), buffer, sizeof buffer));
#endif
            if (count > 0) {
                for (long i = 0; i < count; ++i) {
                    if (input_.size() < kQueueLimit) {
                        input_.push_back(buffer[i]);
                        ++bytesRx_;
                    } else {
                        ++bytesDropped_;
                    }
                }
                continue;
            }
            if (count < 0 && wouldBlock()) return;
#if !defined(_WIN32)
            if (count < 0 && kind_ == Kind::Pty && errno == EIO) return;
#endif
            // 0: the client closed; any other error: it is gone.
            if (kind_ == Kind::Tcp) closeClient();
            return;
        }
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
