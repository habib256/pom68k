// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── V8 video (LC II family) ──
// Framebuffer at VRAM+0, row pitch FIXED at 1024 bytes for 1/2/4/8 bpp
// (16 bpp is packed at hres×2). Depth = pseudo-VIA reg $10 bits 0-2
// (0=1bpp … 4=16bpp); pixels MSB-first; palette indices padded with low
// 1s (1 bpp uses Ariel pens $7F/$FF). Resolution from the monitor sense:
// 1 = 640×870 portrait, 2 = 512×384 12" RGB, 6 = 640×480 13" RGB.
// Source of truth: MAME v8.cpp screen_update :495-619 (master
// 2026-07-15); pinned in docs/LCII_HARDWARE.md § Video.
//
// **Raster decode** (LLE_VS_HLE §1.1, 2026-08-02). `raster()` renders each
// visible row ONCE, at the moment the beam scans it out, driven by
// `VideoBeam` off `V8Memory`'s own frame accumulator. That is what makes a
// mid-frame register change land on the right scanline: the rows above the
// change keep the old palette/depth, the rows below get the new one, as on
// the glass. `decode()` — the whole frame in one go, against whatever the
// registers say *now* — remains for callers that want a still (tests,
// screenshots) and is what the machine loops used to do every publish.
// Gates: tests/v8_video_test.cpp, tests/v8_raster_test.cpp.

#pragma once
#include "V8Memory.h"
#include "VideoBeam.h"
#include <algorithm>
#include <cstdint>
#include <vector>

class V8Video {
public:
    explicit V8Video(V8Memory& mem) : mem_(mem) {}

    static void resolution(uint8_t montype, int& hres, int& vres) {
        switch (montype) {
        case 1:  hres = 640; vres = 870; break;   // 15" portrait
        case 2:  hres = 512; vres = 384; break;   // 12" RGB
        case 6:
        default: hres = 640; vres = 480; break;   // 13" RGB
        }
    }

    // The live decode geometry, Classic II's fixed panel included.
    void size(int& hres, int& vres) const {
        if (mem_.model() == V8Memory::Model::ClassicII) { hres = 512; vres = 342; }
        else resolution(mem_.monitorSense(), hres, vres);
    }

    // Whole-frame decode against the CURRENT register state.
    void decode(std::vector<uint32_t>& out) const {
        int hres, vres;
        size(hres, vres);
        out.assign(size_t(hres) * vres, 0u);
        decodeRows(out, 0, vres);
    }

    // Render visible rows [y0, y1) into `out`, which must already hold a
    // full hres×vres surface. Out-of-range rows are clipped, so a caller
    // whose geometry changed under it cannot write past the surface.
    void decodeRows(std::vector<uint32_t>& out, int y0, int y1) const {
        int hres, vres;
        size(hres, vres);
        if (out.size() < size_t(hres) * vres) return;
        y0 = std::max(y0, 0);
        y1 = std::min(y1, vres);
        if (y0 >= y1) return;

        // Classic II (Eagle): fixed 512×342 1bpp scanned out of MAIN RAM at
        // device offset $1F9A80, 64-byte pitch, no Ariel involvement
        // (v8.cpp:667-691 eagle_device::screen_update).
        if (mem_.model() == V8Memory::Model::ClassicII) {
            const uint8_t* fb = mem_.eagleFrame();
            for (int y = y0; y < y1; y++) {
                uint32_t* dst = out.data() + size_t(y) * 512;
                for (int x = 0; x < 512 / 8; x++) {
                    uint8_t px = fb[y * 64 + x];
                    for (int b = 7; b >= 0; b--)
                        *dst++ = ((px >> b) & 1) ? 0x000000 : 0xFFFFFF;
                }
            }
            return;
        }

        // Same guard as VaspVideo: a sense the GUI does not offer today can
        // still be set through setMonitorSense(), and the 1/2/4/8 bpp arms
        // index at a fixed 1024-byte pitch with no bound of their own.
        if (size_t(vres) * 1024 > V8Memory::kVramSize) {
            std::fill(out.begin() + size_t(y0) * hres,
                      out.begin() + size_t(y1) * hres, 0u);
            return;
        }

        const uint8_t* vram = mem_.vram();
        const Ariel& pal = const_cast<V8Memory&>(mem_).ariel();

        switch (mem_.videoConfig() & 7) {
        case 0:                                    // 1 bpp: pens $7F/$FF
            for (int y = y0; y < y1; y++) {
                uint32_t* dst = out.data() + size_t(y) * hres;
                for (int x = 0; x < hres; x += 8) {
                    uint8_t px = vram[y * 1024 + x / 8];
                    for (int b = 0; b < 8; b++)
                        *dst++ = pal.pen(0x7F | ((px << b) & 0x80));
                }
            }
            break;
        case 1:                                    // 2 bpp
            for (int y = y0; y < y1; y++) {
                uint32_t* dst = out.data() + size_t(y) * hres;
                for (int x = 0; x < hres / 4; x++) {
                    uint8_t px = vram[y * 1024 + x];
                    for (int b = 0; b < 8; b += 2)
                        *dst++ = pal.pen(0x3F | ((px << b) & 0xC0));
                }
            }
            break;
        case 2:                                    // 4 bpp
            for (int y = y0; y < y1; y++) {
                uint32_t* dst = out.data() + size_t(y) * hres;
                for (int x = 0; x < hres / 2; x++) {
                    uint8_t px = vram[y * 1024 + x];
                    *dst++ = pal.pen(0x0F | (px & 0xF0));
                    *dst++ = pal.pen(0x0F | ((px << 4) & 0xF0));
                }
            }
            break;
        case 3:                                    // 8 bpp
            for (int y = y0; y < y1; y++) {
                uint32_t* dst = out.data() + size_t(y) * hres;
                for (int x = 0; x < hres; x++)
                    *dst++ = pal.pen(vram[y * 1024 + x]);
            }
            break;
        case 4:                                    // 16 bpp x1r5g5b5, hres×2 pitch
        default:
            // 16 bpp at 640×480 needs 614400 B > the 512 KB VRAM window —
            // not a valid hardware combo, but reachable via setMonitorSense
            // + a depth write, so bound the read (out-of-window = black).
            for (int y = y0; y < y1; y++) {
                uint32_t* dst = out.data() + size_t(y) * hres;
                for (int x = 0; x < hres; x++) {
                    size_t off = size_t(y * hres + x) * 2;
                    uint16_t px = (off + 1 < V8Memory::kVramSize)
                        ? uint16_t(vram[off] << 8 | vram[off + 1]) : 0;
                    *dst++ = uint32_t(((px >> 10) & 0x1F) << 19
                                    | ((px >> 5) & 0x1F) << 11
                                    | (px & 0x1F) << 3);
                }
            }
            break;
        }
    }

    // Advance the beam off the machine's frame accumulator and decode the
    // rows it has crossed. Safe to call at any granularity: more often is
    // finer, never wrong — the row schedule guarantees each row is rendered
    // exactly once per frame regardless of how the calls fall.
    // `full` marks the once-per-publish call. These two always have a valid
    // frame length (V8 sets it at reset, VASP's is a fixed 60 Hz), so they
    // have no no-CRTC-yet fallback to gate — the parameter exists so every
    // decoder presents the same signature to the machine loops.
    void raster(std::vector<uint32_t>& out, bool full = false) {
        int hres, vres;
        size(hres, vres);
        const size_t need = size_t(hres) * vres;
        if (out.size() != need) {
            out.assign(need, 0u);
            beam_.restartFrame();
        }
        beam_.setGeometry(mem_.frameCycles(), mem_.frameActiveCycles(),
                          mem_.frameTotalLines(), vres);
        (void)full;
        beam_.setPos(mem_.framePos(), mem_.frameCount());
        beam_.pumpRows([&](int a, int b) { decodeRows(out, a, b); });
    }

    // Scan position, for a caller that wants to know where the beam is.
    const VideoBeam& beam() const { return beam_; }

private:
    V8Memory& mem_;
    VideoBeam beam_;
};
