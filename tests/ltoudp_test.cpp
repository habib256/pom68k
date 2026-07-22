// POM68K — gate `ltoudp_test`: the LToUDP multicast cable between two
// in-process endpoints (same wire format as Mini vMac / TashRouter:
// 239.192.76.84:1954, 4-byte sender tag + raw LLAP frame). Soft-skips when
// the environment forbids multicast (sandboxed CI).

#include "LtoUdp.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

int main() {
    LtoUdp a, b;
    if (!a.start() || !b.start()) {
        std::printf("SKIP: multicast unavailable\n");
        return 0;
    }

    const uint8_t enq[3] = {42, 42, 0x81};
    std::vector<uint8_t> gotB, gotA;
    bool selfB = false;

    a.send(enq, 3);
    const uint8_t ack[3] = {42, 42, 0x82};
    b.send(ack, 3);

    // Multicast loopback delivery is asynchronous — poll briefly.
    for (int i = 0; i < 50 && (gotB.empty() || gotA.empty()); i++) {
        b.poll([&](const uint8_t* d, size_t n) {
            gotB.assign(d, d + n);
        });
        a.poll([&](const uint8_t* d, size_t n) {
            gotA.assign(d, d + n);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // B must never see its own ack (tag filter).
    b.poll([&](const uint8_t* d, size_t n) {
        if (n == 3 && d[2] == 0x82) selfB = true;
    });

    int failures = 0;
    if (gotB.size() != 3 || std::memcmp(gotB.data(), enq, 3) != 0) {
        std::printf("FAIL: B did not receive A's ENQ\n");
        failures++;
    }
    if (gotA.size() != 3 || std::memcmp(gotA.data(), ack, 3) != 0) {
        std::printf("FAIL: A did not receive B's ACK\n");
        failures++;
    }
    if (selfB) {
        std::printf("FAIL: sender tag did not filter self-reception\n");
        failures++;
    }
    if (!failures)
        std::printf("PASS: LToUDP cable (ENQ/ACK both ways, self-filtered)\n");
    return failures ? 1 : 0;
}
