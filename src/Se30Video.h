// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SE/30 internal video: 64 KB VRAM on pseudo-slot $E, 512×342×1, two pages
// selected by VIA1 PA6. Source: MAME macii.cpp macse30_map /
// screen_update_macse30 (2026-07-31). The declaration ROM (se30vrom.uk6) is
// installed by NuBus at the top of the $FE slot window; VBL arrives as a
// slot-$E interrupt gated by VIA1 PB6 (MacIIMemory owns that wiring).

#pragma once
#include "NuBus.h"
#include "VideoBeam.h"
#include <cstdint>
#include <vector>

class Se30Video : public NuBusDevice {
public:
    static constexpr int W = 512;
    static constexpr int H = 342;
    static constexpr uint32_t kVramSize = 0x10000;   // 64 KB

    Se30Video() : vram_(kVramSize, 0) {}

    // MAME maps the VRAM at $FE000000 (64 KB) and mirrors it across
    // $FEE00000-$FEEFFFFF (the 24-bit $Exxxxx window lands there). Serving
    // the 64 KB mirror at every slot offset is a superset of that map; the
    // NuBus layer shadows the top 128 KB of each MB with the decl ROM, which
    // the framebuffer (2 pages < $E000) never reaches.
    uint8_t  read8(uint32_t slotOff) override { return vram_[slotOff & 0xFFFF]; }
    uint16_t read16(uint32_t slotOff) override {
        return uint16_t(read8(slotOff) << 8) | read8(slotOff + 1);
    }
    uint32_t read32(uint32_t slotOff) override {
        return uint32_t(read16(slotOff)) << 16 | read16(slotOff + 2);
    }
    void write8(uint32_t slotOff, uint8_t v) override { vram_[slotOff & 0xFFFF] = v; }
    void write16(uint32_t slotOff, uint16_t v) override {
        write8(slotOff, uint8_t(v >> 8));
        write8(slotOff + 1, uint8_t(v));
    }
    void write32(uint32_t slotOff, uint32_t v) override {
        write16(slotOff, uint16_t(v >> 16));
        write16(slotOff + 2, uint16_t(v));
    }

    // VIA1 PA6: page select (MAME via_out_a m_screen_buffer).
    void setPage(bool p) { page_ = p; }
    bool page() const { return page_; }

    // Host decode: 00RRGGBB, W×H, MSB = leftmost, 1 = black. MAME's
    // screen_update_macse30 base: page × $8000, plus one line (W/8 bytes) of
    // fetch lead-in. (Its u16 index ^1 is a host-endianness artifact of the
    // u32-backed share, not hardware — byte order here is the guest's own.)
    void decode(std::vector<uint32_t>& out) const {
        out.resize(size_t(W) * size_t(H));
        decodeRows(out, 0, H);
    }

    // Render visible rows [y0, y1) into an existing W×H surface.
    void decodeRows(std::vector<uint32_t>& out, int y0, int y1) const {
        if (out.size() < size_t(W) * size_t(H)) return;
        y0 = y0 < 0 ? 0 : y0;
        y1 = y1 > H ? H : y1;
        if (y0 >= y1) return;
        const uint32_t base = (page_ ? 0x8000u : 0u) + W / 8;
        for (int y = y0; y < y1; y++) {
            for (int x = 0; x < W; x += 8) {
                uint8_t b = vram_[(base + uint32_t(y) * (W / 8) + uint32_t(x) / 8)
                                  & 0xFFFF];
                for (int i = 0; i < 8; i++)
                    out[size_t(y) * W + x + i] =
                        (b & (0x80u >> i)) ? 0x00000000u : 0x00FFFFFFu;
            }
        }
    }

    // Advance the beam and decode the rows it has crossed (LLE_VS_HLE
    // §1.1, VideoBeam.h). The compact PANEL is fixed — 704×370 dots,
    // 512×342 visible — but this pseudo-slot card has no CRTC of its own,
    // so the frame CLOCK is the machine's (MacIIMemory's 60 Hz
    // accumulator, passed in rather than duplicated here).
    void raster(std::vector<uint32_t>& out, int64_t framePos,
                int64_t frameCycles, uint64_t frameSeq) {
        const size_t need = size_t(W) * size_t(H);
        if (out.size() != need) {
            out.assign(need, 0u);
            beam_.restartFrame();
        }
        beam_.setGeometry(frameCycles, frameCycles * H / 370, 370, H);
        if (!beam_.valid()) { decodeRows(out, 0, H); return; }
        beam_.setPos(framePos, frameSeq);
        beam_.pumpRows([&](int a, int b) { decodeRows(out, a, b); });
    }

    // ── Save states (SaveState.h contract) ──────────────────────────────
    template <class Ar> void visit(Ar& ar) {
        ar.blob(vram_);
        ar(page_);
    }

private:
    std::vector<uint8_t> vram_;
    bool page_ = false;
    VideoBeam beam_;                 // not serialized: pure cache
};
