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
        const uint32_t base = (page_ ? 0x8000u : 0u) + W / 8;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x += 8) {
                uint8_t b = vram_[(base + uint32_t(y) * (W / 8) + uint32_t(x) / 8)
                                  & 0xFFFF];
                for (int i = 0; i < 8; i++)
                    out[size_t(y) * W + x + i] =
                        (b & (0x80u >> i)) ? 0x00000000u : 0x00FFFFFFu;
            }
        }
    }

    // ── Save states (SaveState.h contract) ──────────────────────────────
    template <class Ar> void visit(Ar& ar) {
        ar.blob(vram_);
        ar(page_);
    }

private:
    std::vector<uint8_t> vram_;
    bool page_ = false;
};
