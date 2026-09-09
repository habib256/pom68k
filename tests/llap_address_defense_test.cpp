// POM68K — gate `llap_address_defense_test`: a guest-side SCC probes the
// in-process server's node 128 with lapENQ while its half-duplex receiver is
// off. The server's lapACK must preempt ordinary queued traffic, start inside
// LLAP's 200 us IFG, survive receiver re-arm, and leave normal frames on the
// full 400 us inter-dialog path.

#include "AtalkHub.h"
#include "Scc8530.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr int kB = 0;
constexpr int kByteCycles = 544;
constexpr int64_t kCpuHz = int64_t(kByteCycles) * 28800;
constexpr int kStep = 8;
constexpr int kMaxIfgCycles = int(kCpuHz / 5000); // 200 us
constexpr int kMinIdgCycles = int(kCpuHz / 2500); // 400 us
int failures = 0;

struct Memory {
    Scc8530 chip;
    Scc8530& scc() { return chip; }
};

void check(bool ok, const char* what) {
    std::printf("  %-67s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

void wr(Scc8530& scc, int reg, std::uint8_t value) {
    scc.writeCtl(kB, std::uint8_t(reg));
    scc.writeCtl(kB, value);
}

void armReceiver(Scc8530& scc, bool enabled) {
    wr(scc, 3, enabled ? 0xD5 : 0xD4); // 8-bit, hunt, address search
}

std::uint8_t rr0(Scc8530& scc) {
    scc.writeCtl(kB, 0);
    return scc.readCtl(kB);
}

struct ReceivedFrame {
    std::vector<std::uint8_t> bytes;
    int startCycles = -1;
    std::uint8_t eofStatus = 0;
};

ReceivedFrame receiveOne(Scc8530& scc, int budgetCycles) {
    ReceivedFrame frame;
    for (int elapsed = kStep; elapsed <= budgetCycles; elapsed += kStep) {
        scc.tick(kStep);
        const std::uint8_t status = rr0(scc);
        if (frame.startCycles < 0 && (status & 0x01))
            frame.startCycles = elapsed;
        while (rr0(scc) & 0x01) {
            scc.writeCtl(kB, 1);
            const std::uint8_t byteStatus = scc.readCtl(kB);
            frame.bytes.push_back(scc.readData(kB));
            if (byteStatus & 0x80) {
                frame.eofStatus = byteStatus;
                return frame;
            }
        }
    }
    return frame;
}
} // namespace

int main() {
    std::printf("llap_address_defense_test — prompt lapENQ defence\n");

    Memory mem;
    auto& scc = mem.scc();
    scc.reset();
    scc.setByteCycles(kByteCycles);
    scc.setLosslessRx(true); // in-process hub without an LToUDP cable

    AtalkHub hub;
    hub.attach(mem, kCpuHz, nullptr);
    wr(scc, 9, 0x08);  // master interrupt enable
    wr(scc, 4, 0x20);  // SDLC
    wr(scc, 6, 128);   // tentative guest address = server address
    wr(scc, 5, 0x68);  // Tx 8-bit + enable
    wr(scc, 15, 0x10); // hunt interrupt
    wr(scc, 1, 0x11);  // Rx all chars + external/status
    armReceiver(scc, true);

    bool enqReachedHub = false;
    scc.onTxFrame = [&](int channel, const std::uint8_t* data,
                        std::size_t size) {
        if (channel != kB) return;
        enqReachedHub = size == 3 && data[2] == 0x81;
        hub.onGuestFrame(data, size);
    };

    // Pin priority too: a normal frame already waits on the virtual wire.
    // The lapACK response belongs before it; the ordinary frame must remain.
    armReceiver(scc, false);
    const std::uint8_t ordinary[4] = {128, 42, 0x01, 0xA5};
    scc.injectRxFrame(kB, ordinary, sizeof ordinary);

    const std::uint8_t enq[3] = {128, 128, 0x81};
    scc.writeCtl(kB, 0xC0); // reset Tx underrun: begin frame
    for (const auto byte : enq) {
        scc.writeData(kB, byte);
        scc.tick(kByteCycles);
    }
    for (int cycles = 0; !enqReachedHub && cycles < 8 * kByteCycles;
         cycles += kStep)
        scc.tick(kStep);
    check(enqReachedHub, "guest SCC transmitted lapENQ to the in-process hub");
    check(hub.snapshot().net.enqSeen == 1, "server recognized the node-128 probe");

    // No hub.tick(): the dedicated control path must already have queued ACK.
    armReceiver(scc, true);
    const auto ack = receiveOne(scc, kMaxIfgCycles + 8 * kByteCycles);
    std::printf("  measured: lapACK first byte=%d cycles; IFG ceiling=%d\n",
                ack.startCycles, kMaxIfgCycles);
    check(ack.startCycles >= 0 && ack.startCycles <= kMaxIfgCycles,
          "first lapACK byte lands inside the 200 us inter-frame deadline");
    check(ack.bytes.size() == 5 &&
              !std::memcmp(ack.bytes.data(), "\x80\x80\x82", 3),
          "guest receives lapACK before ordinary queued traffic");
    check((ack.eofStatus & 0x80) && !(ack.eofStatus & 0x40),
          "lapACK closes with EOF and a valid FCS");

    const auto normal = receiveOne(scc, kMinIdgCycles + 8 * kByteCycles);
    std::printf("  measured: ordinary first byte=%d cycles; IDG floor=%d\n",
                normal.startCycles, kMinIdgCycles);
    check(normal.startCycles >= kMinIdgCycles,
          "ordinary peer frame still waits the full 400 us IDG");
    check(normal.bytes.size() == 6 && normal.bytes[2] == 0x01 &&
              normal.bytes[3] == 0xA5,
          "preempted ordinary frame remains intact after lapACK");

    std::printf("%s\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}
