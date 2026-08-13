// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Sonora video gate — the CLUT's monochrome blue gun, and the modeline
// that decides it (SIMPLIFICATIONS_REVIEW.md F2).
//
// A monochrome monitor wires the DAC's BLUE line to the video amplifier,
// so on a mono modeline the blue byte drives all three primaries and the
// R/G bytes are dropped. MAME applies that at the CLUT WRITE, not in the
// decoder (mv_sonora.cpp:373-388 dac_w), keyed on the ACTIVE MODELINE's
// `monochrome` flag — not on the monitor sense, which only tells the ROM
// what to program. POM68K does the same in SonoraMemory::dacWrite, which
// is why the blue gun is absent from SonoraVideo.h and present anyway.
//
// The only mono modeline in the table is $01, the 640×870 15" Portrait
// (SonoraMemory.cpp kModelines). Both LC III monitors are RGB, so no boot
// etalon ever walks this path — hence this gate.
//
// Drives the real MMIO windows the guest uses ($50F28000 vctrl,
// $50F24000 DAC), not the private setters. Exit 0 = pass, 1 = fail.

#include "SonoraMemory.h"
#include "SonoraVideo.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

constexpr uint32_t kVctrl = 0x50F28000;      // video control regs 0-7
constexpr uint32_t kDac   = 0x50F24000;      // CLUT: 0 = address, 1 = data

// Modeline ids (SonoraMemory.cpp kModelines)
constexpr uint8_t kPortrait = 0x01;          // 640×870, monochrome
constexpr uint8_t kRgb13    = 0x06;          // 640×480, colour
constexpr uint8_t kRgb12    = 0x02;          // 512×384, colour
} // namespace

int main() {
    std::printf("sonora_video_test — CLUT blue gun + modeline geometry\n");

    SonoraMemory mem(0x800000);
    SonoraVideo video(mem);

    auto selectModeline = [&](uint8_t id) { mem.write8(kVctrl, id); };
    auto setPen = [&](int n, uint8_t r, uint8_t g, uint8_t b) {
        mem.write8(kDac, uint8_t(n));
        mem.write8(kDac + 1, r);
        mem.write8(kDac + 1, g);
        mem.write8(kDac + 1, b);
    };

    // ── 1. Colour modeline: R/G/B land in their own primaries ───────────
    selectModeline(kRgb13);
    setPen(0x10, 0x11, 0x22, 0x33);
    check(mem.pen(0x10) == 0x00112233u, "13\" RGB: pen keeps R, G and B");

    // ── 2. Mono modeline: blue drives all three ─────────────────────────
    // Same three bytes, different modeline: R and G are dropped and the
    // blue byte is replicated. This is the F2 behaviour.
    selectModeline(kPortrait);
    setPen(0x10, 0x11, 0x22, 0x33);
    check(mem.pen(0x10) == 0x00333333u, "15\" Portrait: blue gun drives R, G and B");

    setPen(0x20, 0xFF, 0xFF, 0x00);
    check(mem.pen(0x20) == 0x00000000u, "15\" Portrait: white R/G with blue 0 is black");
    setPen(0x21, 0x00, 0x00, 0xFF);
    check(mem.pen(0x21) == 0x00FFFFFFu, "15\" Portrait: blue $FF is white");

    // The gate must BITE: on a colour modeline those same two writes are
    // yellow and blue, not black and white.
    selectModeline(kRgb13);
    setPen(0x20, 0xFF, 0xFF, 0x00);
    setPen(0x21, 0x00, 0x00, 0xFF);
    check(mem.pen(0x20) == 0x00FFFF00u && mem.pen(0x21) == 0x000000FFu,
          "control: the same writes stay yellow/blue in colour");

    // ── 3. The CLUT address auto-increments across a triple ─────────────
    selectModeline(kRgb12);
    mem.write8(kDac, 0x40);
    for (uint8_t i = 0; i < 6; i++) mem.write8(kDac + 1, uint8_t(0x80 + i));
    check(mem.pen(0x40) == 0x00808182u && mem.pen(0x41) == 0x00838485u,
          "DAC: one address write feeds consecutive pens");

    // ── 4. Geometry follows the ACTIVE MODELINE, not the sense ──────────
    // The header comment on SonoraVideo::size is load-bearing: the sense
    // only tells the ROM what to program. Set them to disagree.
    int hres = 0, vres = 0;
    mem.setMonitorSense(kRgb13);             // sense says 640×480…
    selectModeline(kPortrait);               // …modeline says 640×870
    video.size(hres, vres);
    check(hres == 640 && vres == 870, "size(): the modeline wins over the sense");

    selectModeline(kRgb12);
    video.size(hres, vres);
    check(hres == 512 && vres == 384, "size(): 12\" RGB modeline is 512×384");

    // ── 5. A mono pen actually reaches the decoded picture ──────────────
    // Blue-gun pens are only worth anything if the decoder reads them.
    // 1 bpp uses pens $7F (clear) and $FF (set), padded with low 1s.
    selectModeline(kPortrait);
    mem.write8(kVctrl + 1, 0);                       // depth 0 = 1 bpp
    setPen(0x7F, 0x00, 0x00, 0x00);                  // clear → black
    setPen(0xFF, 0xAA, 0x55, 0xC0);                  // set → blue $C0 only
    mem.write8(0x60000000, 0x80);                    // VRAM row 0: pixel 0 set
    std::vector<uint32_t> fb;
    video.decode(fb);
    video.size(hres, vres);
    check(fb.size() == size_t(hres) * vres, "decode(): surface is hres × vres");
    check(!fb.empty() && fb[0] == 0x00C0C0C0u,
          "decode(): a set 1 bpp pixel renders the replicated blue");
    check(fb.size() > 1 && fb[1] == 0x00000000u,
          "decode(): the next pixel is the clear pen");

    if (gFails) {
        std::printf("sonora_video_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("sonora_video_test: OK\n");
    return 0;
}
