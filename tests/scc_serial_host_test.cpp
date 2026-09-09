// POM68K — gate `scc_serial_host_test`: a real PTY and a loopback TCP
// client traverse the asynchronous SCC path in both directions. The Rx side
// deliberately exceeds the three-byte hardware FIFO: the host queue must
// retain the remainder until the guest drains a slot.

#include "Scc8530.h"
#include "SerialHostTransport.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %-68s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++gFailures;
}

void dumpFailure(const char* side, const std::vector<std::uint8_t>& bytes) {
    std::printf("    %s (%zu):", side, bytes.size());
    for (const auto value : bytes) std::printf(" %02X", unsigned(value));
    std::printf("\n");
}

void wr(Scc8530& scc, int channel, int reg, std::uint8_t value) {
    scc.writeCtl(channel, std::uint8_t(reg));
    scc.writeCtl(channel, value);
}

void makeNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool readExact(int fd, std::vector<std::uint8_t>& out, std::size_t wanted,
               SerialHostTransport& transport) {
    for (int attempt = 0; attempt < 100 && out.size() < wanted; ++attempt) {
        transport.poll();
        std::uint8_t buffer[32];
        const ssize_t count = ::read(fd, buffer, sizeof buffer);
        if (count > 0) out.insert(out.end(), buffer, buffer + count);
        if (out.size() < wanted)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return out.size() == wanted;
}

void pumpRx(SerialHostTransport& transport, Scc8530& scc, int channel) {
    transport.poll();
    std::uint8_t value = 0;
    while (scc.canInjectRxByte(channel) && transport.readByte(value))
        scc.injectRxByte(channel, value);
}

void roundTrip(const char* label, SerialHostTransport& transport, int peer,
               int channel) {
    constexpr int pace = 544;
    Scc8530 scc;
    scc.reset();
    wr(scc, channel, 4, 0x44); // async x16, one stop bit
    wr(scc, channel, 3, 0xC1); // Rx 8-bit + enable
    wr(scc, channel, 5, 0x68); // Tx 8-bit + enable
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
    const bool sccTxOk = wireTx.size() == sizeof guestBytes &&
        std::memcmp(wireTx.data(), guestBytes, sizeof guestBytes) == 0;
    check(sccTxOk,
          std::string(label) + ": SCC emits each drained async byte");
    if (!sccTxOk) dumpFailure("SCC Tx", wireTx);
    std::vector<std::uint8_t> hostSaw;
    const bool hostTxOk = readExact(peer, hostSaw, sizeof guestBytes, transport) &&
        std::memcmp(hostSaw.data(), guestBytes, sizeof guestBytes) == 0;
    check(hostTxOk,
          std::string(label) + ": paced guest Tx reaches host in order");
    if (!hostTxOk) dumpFailure("host saw", hostSaw);

    const std::uint8_t hostBytes[] = {'H', 'O', 'S', 'T', '!'};
    check(::write(peer, hostBytes, sizeof hostBytes) == ssize_t(sizeof hostBytes),
          std::string(label) + ": host writes five bytes");
    pumpRx(transport, scc, channel);
    std::vector<std::uint8_t> guestSaw;
    for (int attempt = 0; attempt < 100 && guestSaw.size() < sizeof hostBytes;
         ++attempt) {
        if (scc.readCtl(channel) & 0x01)
            guestSaw.push_back(scc.readData(channel));
        pumpRx(transport, scc, channel);
        if (guestSaw.size() < sizeof hostBytes)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool guestRxOk = guestSaw.size() == sizeof hostBytes &&
        std::memcmp(guestSaw.data(), hostBytes, sizeof hostBytes) == 0;
    check(guestRxOk,
          std::string(label) + ": host Rx survives SCC FIFO backpressure");
    if (!guestRxOk) dumpFailure("guest saw", guestSaw);
}
} // namespace

int main() {
    std::printf("scc_serial_host_test — PTY/TCP host round-trip\n");

    {
        SerialHostTransport pty;
        check(pty.start(SerialHostTransport::Kind::Pty),
              "PTY endpoint opens");
        check(!pty.endpoint().empty(), "PTY publishes its slave path");
        const int peer = ::open(pty.endpoint().c_str(),
                                O_RDWR | O_NOCTTY | O_NONBLOCK);
        check(peer >= 0, "PTY slave opens from the host side");
        if (peer >= 0) {
            roundTrip("PTY / printer B", pty, peer, 0);
            ::close(peer);
        }
    }

    {
        SerialHostTransport tcp;
        check(tcp.start(SerialHostTransport::Kind::Tcp, 0),
              "TCP listener opens on an ephemeral port");
        check(tcp.endpoint().rfind("127.0.0.1:", 0) == 0 && tcp.tcpPort() != 0,
              "TCP publishes a loopback-only endpoint");
        const int peer = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(tcp.tcpPort());
        const bool connected = peer >= 0 &&
            ::connect(peer, reinterpret_cast<sockaddr*>(&address),
                      sizeof address) == 0;
        check(connected, "TCP client connects");
        if (connected) {
            makeNonBlocking(peer);
            for (int attempt = 0; attempt < 100 && !tcp.connected(); ++attempt) {
                tcp.poll();
                if (!tcp.connected())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(tcp.connected(), "TCP transport accepts the client");
            roundTrip("TCP / modem A", tcp, peer, 1);
        }
        if (peer >= 0) ::close(peer);
    }

    std::printf("%s\n", gFailures ? "FAILED" : "PASS");
    return gFailures ? 1 : 0;
}
