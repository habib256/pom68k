// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// LLE_VS_HLE §1.1 gate, step 2 — raster decode on the V8 (LC II).
//
// The gap this closes: POM68K decoded the WHOLE framebuffer at publish
// time, against whatever the registers happened to say at that instant.
// Two consequences, both guest-visible and both wrong rather than
// approximate:
//   - a mid-frame palette/depth change repainted the entire frame, so a
//     raster split (different palette above and below a scanline — the
//     classic Mac trick for more than 256 colours on screen) showed only
//     the LAST state, on every line;
//   - VRAM written after the beam had already passed a row still appeared
//     on that row, i.e. the emulator was more "up to date" than the glass.
//
// `V8Video::raster()` renders each row once, when the beam scans it. This
// gate drives a frame line by line, changes state exactly halfway, and
// checks the seam is where the beam was — not everywhere, not nowhere.

#include "V8Memory.h"
#include "V8Video.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

constexpr int kW = 512, kH = 384;          // monitor sense 2, 12" RGB
constexpr uint32_t kWhite = 0xFFFFFF, kBlack = 0x000000;

void setPen(V8Memory& mem, int n, uint8_t r, uint8_t g, uint8_t b) {
    mem.write8(0xF24000, uint8_t(n));
    mem.write8(0xF24001, r);
    mem.write8(0xF24001, g);
    mem.write8(0xF24001, b);
}

// Run the beam over `lines` scanlines, rastering as it goes. The V8 scans
// 640 dots per line at C15M = 640 CPU cycles on the LC II.
void scan(V8Memory& mem, V8Video& video, std::vector<uint32_t>& fb, int lines) {
    for (int i = 0; i < lines; i++) {
        mem.tick(640);
        video.raster(fb);
    }
}
} // namespace

int main() {
    std::printf("v8_raster_test — beam-placed decode (LLE_VS_HLE §1.1)\n");

    V8Memory mem(pom68k::defaultCoreConfig());
    V8Video video(mem);
    std::vector<uint32_t> fb;

    mem.write8(0xF26010, 0x03);                 // 8 bpp
    setPen(mem, 0x42, 0xFF, 0xFF, 0xFF);        // pen $42 = white
    setPen(mem, 0x43, 0x00, 0x00, 0x00);        // pen $43 = black
    for (int y = 0; y < kH; y++)                // column 0 of every row = $42
        mem.write8(0xF40000 + uint32_t(y) * 1024, 0x42);

    // Park the beam at the top of a frame so the split lands where we mean.
    while (mem.framePos() != 0) mem.tick(1);
    video.raster(fb);
    check(fb.size() == size_t(kW) * kH, "raster surface is 512×384");

    {   // ── a palette change mid-frame splits the picture at the beam ──
        scan(mem, video, fb, 192);              // top half scanned as white
        setPen(mem, 0x42, 0x00, 0x00, 0x00);    // pen $42 becomes black
        scan(mem, video, fb, 192);              // bottom half scanned as black

        check(fb[size_t(10) * kW] == kWhite, "row 10 keeps the palette it was scanned with");
        check(fb[size_t(190) * kW] == kWhite, "last row above the change is unchanged");
        check(fb[size_t(200) * kW] == kBlack, "row 200 has the new palette");
        check(fb[size_t(383) * kW] == kBlack, "last row has the new palette");

        // The seam must be AT the beam, not at 0 and not at the bottom.
        int seam = -1;
        for (int y = 1; y < kH; y++)
            if (fb[size_t(y) * kW] != fb[size_t(y - 1) * kW]) {
                if (seam >= 0) { seam = -2; break; }   // more than one seam
                seam = y;
            }
        check(seam >= 190 && seam <= 194, "exactly one seam, within a line or two of 192");

        // Contrast: the old whole-frame path repaints everything with the
        // state as of NOW — no seam at all. That is the bug, kept here so
        // the difference is visible rather than asserted in prose.
        std::vector<uint32_t> whole;
        video.decode(whole);
        bool uniform = true;
        for (int y = 0; y < kH; y++)
            if (whole[size_t(y) * kW] != kBlack) uniform = false;
        check(uniform, "decode() (whole-frame) shows only the latest state");
    }

    {   // ── VRAM written behind the beam does not reach rows already scanned ──
        while (mem.framePos() != 0) mem.tick(1);   // top of the next frame
        video.raster(fb);
        setPen(mem, 0x42, 0xFF, 0xFF, 0xFF);       // white again
        for (int y = 0; y < kH; y++)
            mem.write8(0xF40000 + uint32_t(y) * 1024, 0x42);
        scan(mem, video, fb, 192);                 // top half: white

        for (int y = 0; y < kH; y++)               // guest repaints EVERY row
            mem.write8(0xF40000 + uint32_t(y) * 1024, 0x43);
        scan(mem, video, fb, 192);                 // bottom half: black

        check(fb[size_t(50) * kW] == kWhite,
              "a row already scanned keeps the content it was scanned with");
        check(fb[size_t(300) * kW] == kBlack,
              "a row scanned after the write shows the new content");
    }

    {   // ── the beam position itself, and that it reaches vblank ──
        while (mem.framePos() != 0) mem.tick(1);
        video.raster(fb);
        check(video.beam().line() == 0, "beam at line 0 at the top of the frame");
        scan(mem, video, fb, 200);
        const int line = video.beam().line();
        check(line >= 199 && line <= 201, "beam tracks the scanline it is on");
        check(!video.beam().inVBlank(), "line 200 of 384 active is not blanking");
        scan(mem, video, fb, 190);
        check(video.beam().inVBlank(), "past the active window the beam is blanking");
    }

    {   // ── coarse callers stay correct: one raster() per frame ──
        // The machine loops call raster() when they publish, not per line.
        // Every row must still be rendered exactly once per frame.
        while (mem.framePos() != 0) mem.tick(1);
        video.raster(fb);
        setPen(mem, 0x43, 0x11, 0x22, 0x33);
        for (int f = 0; f < 3; f++) {
            mem.tick(int(mem.frameCycles()));
            video.raster(fb);
        }
        bool all = true;
        for (int y = 0; y < kH; y++)
            if (fb[size_t(y) * kW] != 0x112233u) all = false;
        check(all, "one raster() per frame still repaints the whole frame");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
