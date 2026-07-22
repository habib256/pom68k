// POM68K — LLAP two-System etalon: two full Mac II machines boot System 7
// with AppleTalk seeded ACTIVE (POM68K_APPLETALK=1 → SPConfig $21) on a
// shared virtual LLAP cable (scc.onTxFrame ↔ scc.injectRxFrame). System 7
// opens .MPP at BOOT when AppleTalk is active (System 6 only opens it
// lazily from the Chooser — no wire traffic headless), so each LAP Manager
// runs the real address-acquisition dialogue on the wire: ENQ probes
// (dst = src = tentative ID) that the OTHER machine must carry.
// Asserts: both nodes probe, both HEAR the other side's traffic, the final
// node IDs are distinct, and neither boot is harmed by the cable.
// Boots are offset ~2 s so the Ticks-seeded random IDs differ (two real
// machines never power up cycle-synchronous). Soft-skips without assets.

#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { "", "../" }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

struct Node {
    MacIIMemory mem;
    Cpu020 cpu{mem, true};
    long enqSent = 0;
    long framesSent = 0;
    long framesHeard = 0;             // frames injected from the peer
    int lastEnqId = -1;

    bool init(const std::vector<uint8_t>& rom, const std::string& img) {
        if (!mem.loadRom(rom)) return false;
        mem.installTobyVideo();
        mem.setCpu(&cpu);
        cpu.hardReset();
        return mem.attachScsi(img);
    }
};

int main() {
    std::string rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    if (rom.empty()) rom = find("roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM");
    std::string img = find("hdv/System 7.0 HD.dsk");
    if (img.empty()) img = find("hdv/System 7.1 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs Mac II ROM + hdv/System 7.0|7.1 HD.dsk\n");
        return 0;
    }
    setenv("POM68K_APPLETALK", "1", 1);       // SPConfig $21 before reset

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    static Node a, b;                          // large: keep off the stack
    if (!a.init(romData, img) || !b.init(romData, img)) {
        std::fprintf(stderr, "FAIL: machine init\n");
        return 1;
    }

    // Virtual cable with per-direction accounting.
    auto wire = [](Node& from, Node& to) {
        from.mem.scc().onTxFrame = [&from, &to](int ch, const uint8_t* d, size_t n) {
            if (ch != 0) return;               // channel B = LocalTalk
            from.framesSent++;
            if (n >= 3 && d[0] == d[1] && d[2] == 0x81) {  // lapENQ probe
                from.enqSent++;
                from.lastEnqId = d[0];
            }
            to.framesHeard++;
            to.mem.scc().injectRxFrame(0, d, n);
        };
    };
    wire(a, b);
    wire(b, a);

    const int64_t kFrame = 800 * 525;          // one 60 Hz quantum
    const long kOffset = 120;                  // B starts ~2 s after A
    const long kMax = 20000;
    long f = 0;
    for (; f < kMax && !a.cpu.isHalted() && !b.cpu.isHalted(); f++) {
        a.cpu.runCycles(kFrame);
        if (f >= kOffset) b.cpu.runCycles(kFrame);
        // Early exit once the milestone claim is proven on the wire.
        if (a.enqSent && b.enqSent && a.framesHeard && b.framesHeard
            && a.lastEnqId >= 0 && b.lastEnqId >= 0
            && a.lastEnqId != b.lastEnqId && f > kOffset + 600)
            break;
    }

    std::printf("frames=%ld  A: sent=%ld enq=%ld heard=%ld id=%d  "
                "B: sent=%ld enq=%ld heard=%ld id=%d\n",
                f, a.framesSent, a.enqSent, a.framesHeard, a.lastEnqId,
                b.framesSent, b.enqSent, b.framesHeard, b.lastEnqId);

    int failures = 0;
    auto check = [&](bool ok, const char* msg) {
        if (!ok) { std::printf("FAIL: %s\n", msg); failures++; }
    };
    check(!a.cpu.isHalted() && !b.cpu.isHalted(), "no double fault");
    check(a.enqSent > 0, "A probed an LLAP address (ENQ on the wire)");
    check(b.enqSent > 0, "B probed an LLAP address (ENQ on the wire)");
    check(a.framesHeard > 0, "A heard B's traffic through the cable");
    check(b.framesHeard > 0, "B heard A's traffic through the cable");
    check(a.lastEnqId != b.lastEnqId, "final node IDs are distinct");
    if (!failures)
        std::printf("PASS: two Systems share the LLAP cable "
                    "(A node %d, B node %d)\n", a.lastEnqId, b.lastEnqId);
    return failures ? 1 : 0;
}
