// POM68K — gate `scc_serial_host_test`: the SCC's asynchronous path to the
// host endpoints (SerialHostTransport.h) and the « Ports série » terminal
// (SerialTerminal.h).
//
//   • round trips — a real PTY (Unix) and a loopback TCP client (BSD
//     sockets, Winsock on Windows) in both directions; the Rx side exceeds
//     the three-byte hardware FIFO, so the host queue must keep the rest
//     until the guest drains a slot;
//   • TCP lifecycle — a client that disconnects is noticed, what the guest
//     sends meanwhile is counted dropped like an unplugged lead, and a new
//     client is accepted and served;
//   • partial writes and saturation — a guest burst larger than the host
//     reads arrives in order, every byte either sent or counted dropped;
//     a host burst larger than the queue is bounded and counted;
//   • the terminal — as an observer it mirrors only bytes the SCC actually
//     took, never draining what the transport still holds for the guest;
//     as the endpoint its input is bounded, refused when full, and reaches
//     the guest in order at the receiver's pace.

#include "Scc8530.h"
#include "SerialHostTransport.h"
#include "SerialTerminal.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %-72s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++gFailures;
}

void dumpFailure(const char* side, const std::vector<std::uint8_t>& bytes) {
    std::printf("    %s (%zu):", side, bytes.size());
    for (std::size_t i = 0; i < bytes.size() && i < 64; ++i)
        std::printf(" %02X", unsigned(bytes[i]));
    std::printf("\n");
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ── A loopback TCP client, BSD or Winsock ────────────────────────────────
#if defined(_WIN32)
using Sock = SOCKET;
const Sock kNoSock = INVALID_SOCKET;
void closeSock(Sock s) { ::closesocket(s); }
void nonBlocking(Sock s) { u_long on = 1; ::ioctlsocket(s, FIONBIO, &on); }
#else
using Sock = int;
const Sock kNoSock = -1;
void closeSock(Sock s) { ::close(s); }
void nonBlocking(Sock s) {
    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags >= 0) (void)::fcntl(s, F_SETFL, flags | O_NONBLOCK);
}
#endif

// `receiveBuffer` (bytes, before connect) shrinks the window the client
// advertises, so the transport's writes go partial and its queue fills.
Sock connectLoopback(std::uint16_t port, int receiveBuffer = 0) {
    const Sock s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSock) return kNoSock;
    if (receiveBuffer)
        (void)::setsockopt(s, SOL_SOCKET, SO_RCVBUF,
                           reinterpret_cast<const char*>(&receiveBuffer), sizeof receiveBuffer);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        closeSock(s);
        return kNoSock;
    }
    nonBlocking(s);
    return s;
}
long peerWrite(Sock s, const std::uint8_t* data, std::size_t n) {
    return long(::send(s, reinterpret_cast<const char*>(data), int(n), 0));
}
long peerRead(Sock s, std::uint8_t* data, std::size_t n) {
    return long(::recv(s, reinterpret_cast<char*>(data), int(n), 0));
}

bool waitConnected(SerialHostTransport& t, bool wanted) {
    for (int attempt = 0; attempt < 400 && t.connected() != wanted; ++attempt) {
        t.poll();
        if (t.connected() != wanted) sleepMs(1);
    }
    return t.connected() == wanted;
}

void wr(Scc8530& scc, int channel, int reg, std::uint8_t value) {
    scc.writeCtl(channel, std::uint8_t(reg));
    scc.writeCtl(channel, value);
}

void setupAsync(Scc8530& scc, int channel) {
    scc.reset();
    wr(scc, channel, 4, 0x44); // async x16, one stop bit
    wr(scc, channel, 3, 0xC1); // Rx 8-bit + enable
    wr(scc, channel, 5, 0x68); // Tx 8-bit + enable
}

// Reads what the guest's receiver holds, pumping between reads.
std::vector<std::uint8_t> guestReads(Scc8530& scc, int channel, std::size_t wanted,
                                     SerialHostTransport* t, SerialTerminal* term) {
    std::vector<std::uint8_t> got;
    for (int attempt = 0; attempt < 20000 && got.size() < wanted; ++attempt) {
        pumpSerialChannel(scc, channel, t, term);
        bool any = false;
        while (scc.readCtl(channel) & 0x01) {
            got.push_back(scc.readData(channel));
            any = true;
            pumpSerialChannel(scc, channel, t, term);
        }
        if (!any && got.size() < wanted) sleepMs(1);
    }
    return got;
}

#if !defined(_WIN32)
bool readExact(int fd, std::vector<std::uint8_t>& out, std::size_t wanted,
               SerialHostTransport& transport) {
    for (int attempt = 0; attempt < 100 && out.size() < wanted; ++attempt) {
        transport.poll();
        std::uint8_t buffer[32];
        const ssize_t count = ::read(fd, buffer, sizeof buffer);
        if (count > 0) out.insert(out.end(), buffer, buffer + count);
        if (out.size() < wanted) sleepMs(1);
    }
    return out.size() == wanted;
}
#endif

// Guest "GU" out, host "HOST!" in, through one endpoint.
template <class PeerRead, class PeerWrite>
void roundTrip(const char* label, SerialHostTransport& transport, int channel,
               PeerRead&& readPeer, PeerWrite&& writePeer) {
    constexpr int pace = 544;
    Scc8530 scc;
    setupAsync(scc, channel);
    std::vector<std::uint8_t> wireTx;
    scc.onTxByte = [&](int emittedChannel, std::uint8_t value) {
        if (emittedChannel == channel) {
            wireTx.push_back(value);
            transport.sendByte(value);
        }
    };
    const std::uint8_t guestBytes[] = {'G', 'U'};
    scc.writeData(channel, guestBytes[0]);
    scc.writeData(channel, guestBytes[1]);
    scc.tick(pace);
    scc.tick(pace);
    check(wireTx.size() == 2 && std::memcmp(wireTx.data(), guestBytes, 2) == 0,
          std::string(label) + ": SCC emits each drained async byte");
    std::vector<std::uint8_t> hostSaw = readPeer(2);
    const bool hostTxOk = hostSaw.size() == 2 && std::memcmp(hostSaw.data(), guestBytes, 2) == 0;
    check(hostTxOk, std::string(label) + ": paced guest Tx reaches host in order");
    if (!hostTxOk) dumpFailure("host saw", hostSaw);

    const std::uint8_t hostBytes[] = {'H', 'O', 'S', 'T', '!'};
    check(writePeer(hostBytes, sizeof hostBytes),
          std::string(label) + ": host writes five bytes");
    const std::vector<std::uint8_t> guestSaw =
        guestReads(scc, channel, sizeof hostBytes, &transport, nullptr);
    const bool guestRxOk = guestSaw.size() == sizeof hostBytes &&
        std::memcmp(guestSaw.data(), hostBytes, sizeof hostBytes) == 0;
    check(guestRxOk, std::string(label) + ": host Rx survives SCC FIFO backpressure");
    if (!guestRxOk) dumpFailure("guest saw", guestSaw);
}

std::vector<std::uint8_t> drainPeer(Sock peer, SerialHostTransport& t, std::size_t wanted) {
    std::vector<std::uint8_t> out;
    int idle = 0;
    while (out.size() < wanted && idle < 300) {
        t.poll();
        std::uint8_t buffer[4096];
        const long n = peerRead(peer, buffer, sizeof buffer);
        if (n > 0) { out.insert(out.end(), buffer, buffer + n); idle = 0; }
        else { ++idle; sleepMs(1); }
    }
    return out;
}

// Is `small` a subsequence of `big`, in order?
bool subsequence(const std::vector<std::uint8_t>& small, const std::vector<std::uint8_t>& big) {
    std::size_t j = 0;
    for (std::size_t i = 0; i < big.size() && j < small.size(); ++i)
        if (big[i] == small[j]) ++j;
    return j == small.size();
}

void tcpLifecycle() {
    SerialHostTransport tcp;
    check(tcp.start(SerialHostTransport::Kind::Tcp, 0),
          "TCP listener opens on an ephemeral port");
    check(tcp.endpoint().rfind("127.0.0.1:", 0) == 0 && tcp.tcpPort() != 0,
          "TCP publishes a loopback-only endpoint");
    Sock peer = connectLoopback(tcp.tcpPort());
    check(peer != kNoSock && waitConnected(tcp, true), "TCP transport accepts the client");
    if (peer == kNoSock) return;
    roundTrip("TCP / modem A", tcp, 1,
        [&](std::size_t n) { return drainPeer(peer, tcp, n); },
        [&](const std::uint8_t* d, std::size_t n) { return peerWrite(peer, d, n) == long(n); });

    // Disconnect: noticed at the next read, then the lead is unplugged.
    closeSock(peer);
    check(waitConnected(tcp, false), "a client that disconnects is noticed");
    const std::uint64_t dropped0 = tcp.bytesDropped();
    for (int i = 0; i < 10; ++i) tcp.sendByte(std::uint8_t('x'));
    check(tcp.bytesDropped() == dropped0 + 10,
          "bytes sent with no client are counted dropped, not queued for the next one");

    // Reconnect: the listener kept listening.
    peer = connectLoopback(tcp.tcpPort());
    check(peer != kNoSock && waitConnected(tcp, true), "a new client is accepted after the first left");
    if (peer == kNoSock) return;
    roundTrip("TCP / reconnected", tcp, 1,
        [&](std::size_t n) { return drainPeer(peer, tcp, n); },
        [&](const std::uint8_t* d, std::size_t n) { return peerWrite(peer, d, n) == long(n); });

    // Partial writes: a burst the host does not read while it is sent, to
    // a client whose 4 KiB window keeps the kernel from absorbing it.
    closeSock(peer);
    waitConnected(tcp, false);
    peer = connectLoopback(tcp.tcpPort(), 4096);
    check(peer != kNoSock && waitConnected(tcp, true), "a narrow-window client connects");
    if (peer == kNoSock) return;
    std::vector<std::uint8_t> burst(4 * 1024 * 1024);
    for (std::size_t i = 0; i < burst.size(); ++i) burst[i] = std::uint8_t(i * 7 + i / 251);
    const std::uint64_t tx0 = tcp.bytesTx(), drop0 = tcp.bytesDropped();
    for (std::uint8_t b : burst) tcp.sendByte(b);
    const std::vector<std::uint8_t> received = drainPeer(peer, tcp, burst.size());
    const std::uint64_t sent = tcp.bytesTx() - tx0, dropped = tcp.bytesDropped() - drop0;
    check(sent + dropped == burst.size() && received.size() == sent && dropped > 0,
          "guest burst: every byte either sent or counted dropped (" + std::to_string(sent) +
              " sent, " + std::to_string(dropped) + " dropped)");
    check(subsequence(received, burst),
          "guest burst: what arrives is in order, partial writes resumed where they stopped");

    // Saturation the other way: the host floods, the guest reads nothing.
    std::vector<std::uint8_t> flood(160 * 1024, 0x5A);
    std::size_t written = 0;
    for (int attempt = 0; attempt < 2000 && written < flood.size(); ++attempt) {
        const long n = peerWrite(peer, flood.data() + written, flood.size() - written);
        if (n > 0) written += std::size_t(n);
        tcp.poll();
        if (n <= 0) sleepMs(1);
    }
    for (int i = 0; i < 50; ++i) { tcp.poll(); sleepMs(1); }
    std::size_t queued = 0;
    std::uint8_t value = 0;
    while (tcp.readByte(value)) ++queued;
    check(queued == 64 * 1024 && tcp.bytesRx() >= queued,
          "host flood: the queue holds its bound (" + std::to_string(queued) + " bytes)");
    check(written == flood.size() && tcp.bytesDropped() > drop0 + dropped,
          "host flood: the excess is counted dropped, the host is never blocked");
    closeSock(peer);
}

void terminalCases() {
    // Observer: a TCP port whose window is open must not drain the transport.
    {
        SerialHostTransport tcp;
        tcp.start(SerialHostTransport::Kind::Tcp, 0);
        Sock peer = connectLoopback(tcp.tcpPort());
        waitConnected(tcp, true);
        SerialTerminal observer(false);
        Scc8530 scc;
        setupAsync(scc, 1);
        const std::uint8_t hostBytes[] = {'1', '2', '3', '4', '5', '6', '7'};
        peerWrite(peer, hostBytes, sizeof hostBytes);
        for (int i = 0; i < 50 && tcp.bytesRx() < sizeof hostBytes; ++i) { tcp.poll(); sleepMs(1); }
        pumpSerialChannel(scc, 1, &tcp, &observer);
        const auto first = observer.view();
        check(first.toGuest == 3 && first.connected,
              "observer: mirrors the three bytes the SCC FIFO took, not the seven that arrived");
        check(observer.send("no") == 0, "observer: its input is off — it is not the endpoint");
        const auto guest = guestReads(scc, 1, sizeof hostBytes, &tcp, &observer);
        check(guest.size() == sizeof hostBytes &&
                  std::memcmp(guest.data(), hostBytes, sizeof hostBytes) == 0 &&
                  observer.view().toGuest == sizeof hostBytes &&
                  observer.view().text == "1234567",
              "observer: the guest still receives everything, and the log shows it once");
        serialGuestByte(std::uint8_t('\r'), &tcp, &observer);
        serialGuestByte(std::uint8_t(0x01), &tcp, &observer);
        check(observer.view().fromGuest == 2 && observer.view().text == "1234567\n\\x01",
              "observer: guest bytes are logged too, CR as a line end, controls escaped");
        closeSock(peer);
    }
    // Endpoint: the terminal is the far end of the line.
    {
        SerialTerminal terminal(true);
        Scc8530 scc;
        setupAsync(scc, 1);
        std::string big(SerialTerminal::kInputBytes + 100, 'a');
        for (std::size_t i = 0; i < big.size(); ++i) big[i] = char('a' + i % 26);
        const std::size_t taken = terminal.send(big);
        check(taken == SerialTerminal::kInputBytes && terminal.view().pendingInput == taken,
              "endpoint: input is bounded, the excess refused at once, not queued");
        pumpSerialChannel<Scc8530, SerialHostTransport>(scc, 1, nullptr, &terminal);
        check(terminal.view().pendingInput == taken - 3,
              "endpoint: only what the receiver can take leaves the queue");
        const auto guest = guestReads(scc, 1, taken,
                                      static_cast<SerialHostTransport*>(nullptr), &terminal);
        check(guest.size() == taken &&
                  std::equal(guest.begin(), guest.end(), big.begin()) &&
                  terminal.view().pendingInput == 0,
              "endpoint: the guest reads every accepted byte, in order");
        check(terminal.view().toGuest == taken && terminal.view().logDropped == 0,
              "endpoint: every byte the guest took is in the log");
        for (std::size_t i = 0; i < SerialTerminal::kLogBytes; ++i)
            terminal.observe(SerialTerminal::Direction::FromGuest, 'z');
        check(terminal.view().logDropped == taken &&
                  terminal.view().text == std::string(SerialTerminal::kLogBytes, 'z'),
              "the log keeps its last kLogBytes and counts the older ones");
    }
}
} // namespace

int main() {
    std::printf("scc_serial_host_test — host endpoints and the serial terminal\n");
#if defined(_WIN32)
    WSADATA wsa;
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

#if !defined(_WIN32)
    {
        SerialHostTransport pty;
        check(pty.start(SerialHostTransport::Kind::Pty), "PTY endpoint opens");
        check(!pty.endpoint().empty(), "PTY publishes its slave path");
        const int peer = ::open(pty.endpoint().c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        check(peer >= 0, "PTY slave opens from the host side");
        if (peer >= 0) {
            roundTrip("PTY / printer B", pty, 0,
                [&](std::size_t n) {
                    std::vector<std::uint8_t> out;
                    readExact(peer, out, n, pty);
                    return out;
                },
                [&](const std::uint8_t* d, std::size_t n) {
                    return ::write(peer, d, n) == ssize_t(n);
                });
            ::close(peer);
        }
    }
#else
    {
        SerialHostTransport pty;
        check(!pty.start(SerialHostTransport::Kind::Pty),
              "PTY is refused on Windows, never faked");
    }
#endif
    tcpLifecycle();
    terminalCases();

    std::printf("%s\n", gFailures ? "FAILED" : "PASS");
    return gFailures ? 1 : 0;
}
