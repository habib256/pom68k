// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.4 gate — V8 video decode + Ariel palette + VBL cadence against the
// pinned model (docs/LCII_HARDWARE.md § Video; MAME v8.cpp:495-619):
// fixed 1024-byte row pitch, MSB-first pixels, low-1s pen padding
// (1 bpp uses pens $7F/$FF), depth from pseudo-VIA reg $10, monitor
// sense resolutions, and the 512×384 VBL into pseudo-VIA slot bit $40
// at 640×407 dots per frame. Exit 0 = pass, 1 = fail.

#include "V8Memory.h"
#include "V8Video.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

int main() {
    std::printf("v8_video_test — V8 video + Ariel + VBL (O6.4)\n");

    V8Memory mem;
    V8Video video(mem);
    std::vector<uint32_t> fb;

    // Ariel pens for 1 bpp: $7F = white, $FF = black (v8.cpp:532-539)
    auto setPen = [&](int n, uint8_t r, uint8_t g, uint8_t b) {
        mem.write8(0xF24000, uint8_t(n));
        mem.write8(0xF24001, r);
        mem.write8(0xF24001, g);
        mem.write8(0xF24001, b);
    };
    setPen(0x7F, 0xFF, 0xFF, 0xFF);
    setPen(0xFF, 0x00, 0x00, 0x00);

    // 1 bpp: $AA in row 0 → alternating black/white from the MSB;
    // row pitch is 1024 bytes regardless of the 64-byte visible width
    mem.write8(0xF26010, 0x00);              // depth 0 = 1 bpp
    mem.write8(0xF40000, 0xAA);              // row 0, first 8 pixels
    mem.write8(0xF40000 + 1024, 0x0F);       // row 1
    video.decode(fb);
    check(fb.size() == 512u * 384u, "sense 2 → 512×384 frame");
    check(fb[0] == 0x000000 && fb[1] == 0xFFFFFF, "1bpp: MSB first, 1 = black");
    check(fb[512 + 3] == 0xFFFFFF && fb[512 + 4] == 0x000000,
          "1bpp: row pitch is 1024 bytes");

    // 8 bpp: direct pen indexing
    setPen(0x42, 0x10, 0x20, 0x30);
    mem.write8(0xF26010, 0x03);              // depth 3 = 8 bpp
    mem.write8(0xF40000, 0x42);
    video.decode(fb);
    check(fb[0] == 0x102030, "8bpp: byte indexes the Ariel palette");

    // Monitor sense drives the resolution
    mem.setMonitorSense(6);
    video.decode(fb);
    check(fb.size() == 640u * 480u, "sense 6 → 640×480");
    mem.setMonitorSense(2);

    // Pseudo-VIA reg $10 read = sense bits over depth bits
    check((mem.read8(0xF26010) & 0x38) == 0x10, "reg $10 reads monitor sense 2");

    // VBL: one pulse per 640×407-dot frame into slot bit $40
    mem.write8(0xF26012, 0x80 | 0x40);       // slot IER: enable VBL
    mem.write8(0xF26013, 0x80 | 0x02);       // IER: any-slot
    int pulses = 0, maxIpl = 0;
    bool prev = false;
    for (int64_t c = 0; c < V8Memory::kCpuHz; c += 500) {
        mem.tick(500);
        bool pending = (mem.pseudoVia().read(2) & 0x40) == 0;   // active low
        if (pending && !prev) pulses++;
        prev = pending;
        if (mem.iplLevel() > maxIpl) maxIpl = mem.iplLevel();
    }
    check(pulses >= 59 && pulses <= 62, "VBL: ~60 pulses per emulated second");
    check(maxIpl == 2, "VBL raises IPL 2 through the pseudo-VIA");

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
