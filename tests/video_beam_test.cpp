// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// LLE_VS_HLE §1.1 gate, step 1 — the raster beam.
// `VideoBeam` turns a platform's existing frame accumulator into a scan
// position and a row-granular decode schedule. What must hold, because the
// decoders are about to depend on it:
//   - every visible row is emitted EXACTLY ONCE per frame, in order, with
//     no gap and no repeat — a missed row is a stale band on screen, a
//     repeated one is wasted work that also re-reads changed VRAM;
//   - the frame tail is flushed on the wrap, so a frame whose last pump
//     landed mid-screen still finishes;
//   - `line()` tracks total lines (blanking included) while the row
//     schedule tracks the ACTIVE window — the two are independent, which is
//     what lets a 640×480 decode ride a 407-line modeline.

#include "VideoBeam.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// Drive a whole frame in `steps` equal advances, recording how many times
// each visible row was emitted.
std::vector<int> sweep(VideoBeam& b, int64_t frameCycles, int visible,
                       int steps, int frames = 1) {
    std::vector<int> hits(size_t(visible), 0);
    for (int f = 0; f < frames; f++)
        for (int s = 1; s <= steps; s++) {
            b.setPos(frameCycles * s / steps % frameCycles);
            b.pumpRows([&](int y0, int y1) {
                for (int y = y0; y < y1; y++) hits[size_t(y)]++;
            });
        }
    return hits;
}
} // namespace

int main() {
    std::printf("video_beam_test — raster position + row schedule\n");

    // V8 / LC II 512×384 geometry: 640×407 dots, 384 visible lines.
    const int64_t kFrame = 640 * 407, kActive = 640 * 384;

    {   // ── position: line() spans the TOTAL lines, vblank past the active ──
        VideoBeam b;
        b.setGeometry(kFrame, kActive, 407, 384);
        check(b.valid(), "geometry accepted");
        b.setPos(0);
        check(b.line() == 0 && !b.inVBlank(), "top of frame: line 0, not blanking");
        b.setPos(kActive - 1);
        check(b.line() == 383 && !b.inVBlank(), "last active line: 383, not blanking");
        b.setPos(kActive);
        check(b.inVBlank(), "vblank starts exactly at the active end");
        b.setPos(kFrame - 1);
        check(b.line() == 406 && b.inVBlank(), "last line of the frame is 406");
        check(b.scannedRows() == 384, "blanking reports the whole frame scanned");
    }

    {   // ── the row schedule: once each, in order, no gap ──
        VideoBeam b;
        b.setGeometry(kFrame, kActive, 407, 384);
        int last = -1; bool ordered = true;
        std::vector<int> hits(384, 0);
        for (int s = 1; s <= 1000; s++) {
            b.setPos(kFrame * s / 1000 % kFrame);
            b.pumpRows([&](int y0, int y1) {
                if (y0 != last + 1) ordered = false;
                for (int y = y0; y < y1; y++) hits[size_t(y)]++;
                last = y1 - 1;
            });
        }
        check(ordered, "rows are emitted contiguously, in scan order");
        bool once = true;
        for (int h : hits) if (h != 1) once = false;
        check(once, "every visible row emitted exactly once in a frame");
    }

    {   // ── coarse stepping: a pump per frame still emits every row ──
        VideoBeam b;
        b.setGeometry(kFrame, kActive, 407, 384);
        auto hits = sweep(b, kFrame, 384, 3, 4);     // 3 pumps per frame, 4 frames
        bool all = true;
        for (int h : hits) if (h != 4) all = false;
        check(all, "3 pumps/frame × 4 frames: each row exactly 4 times");
    }

    {   // ── the wrap flushes the frame tail ──
        // Land mid-screen, then jump straight past the wrap: the rows below
        // the landing point still have to be emitted, or they stay stale.
        VideoBeam b;
        b.setGeometry(kFrame, kActive, 407, 384);
        b.setPos(kActive / 2);
        b.pumpRows([](int, int) {});
        std::vector<int> hits(384, 0);
        b.setPos(640 * 3);                           // wrapped, 3 lines in
        b.pumpRows([&](int y0, int y1) {
            for (int y = y0; y < y1; y++) hits[size_t(y)]++;
        });
        bool tail = true;
        for (int y = 192; y < 384; y++) if (hits[size_t(y)] != 1) tail = false;
        check(tail, "wrap flushes the unscanned tail of the old frame");
        check(hits[0] == 1 && hits[2] == 1, "…and starts the new frame's head");
        check(hits[3] == 0 && hits[100] == 0,
              "…without emitting a row the beam has not finished scanning");
    }

    {   // ── visible height > modeline height: still one pass per frame ──
        // A 640×480 decode on the V8's 407-line modeline. The row schedule
        // rides the ACTIVE window, so it cannot run out of lines.
        VideoBeam b;
        b.setGeometry(kFrame, kActive, 407, 480);
        auto hits = sweep(b, kFrame, 480, 200, 2);
        bool all = true;
        for (int h : hits) if (h != 2) all = false;
        check(all, "480 rows on a 407-line modeline: each row once per frame");
    }

    {   // ── a geometry change restarts the frame, never half-renders ──
        VideoBeam b;
        b.setGeometry(kFrame, kActive, 407, 384);
        b.setPos(kActive / 2);
        b.pumpRows([](int, int) {});
        b.setGeometry(kFrame, kActive, 407, 480);    // depth/sense switch
        std::vector<int> hits(480, 0);
        for (int s = 1; s <= 200; s++) {
            b.setPos(kFrame * s / 200 % kFrame);
            b.pumpRows([&](int y0, int y1) {
                for (int y = y0; y < y1; y++) hits[size_t(y)]++;
            });
        }
        bool all = true;
        for (int h : hits) if (h != 1) all = false;
        check(all, "new geometry redraws the whole frame from row 0");
    }

    {   // ── degenerate geometry is refused, not divided by ──
        VideoBeam b;
        b.setGeometry(0, 0, 0, 0);
        check(!b.valid(), "zero geometry rejected");
        b.setPos(1234);
        int calls = 0;
        b.pumpRows([&](int, int) { calls++; });
        check(calls == 0 && b.line() == 0, "invalid beam emits nothing");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
