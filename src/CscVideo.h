// POM68K — Color Screen Controller (Chips & Technologies 65220).
// Register/pixel semantics from MAME apple/csc.cpp, R. Belmont,
// BSD-3-Clause. The Duo 270c uses panel ID 0 (640x480 color).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

class CscVideo {
public:
    static constexpr uint32_t kVramSize = 0x200000;
    static constexpr int kWidth = 640, kHeight = 480;

    CscVideo() : vram_(kVramSize, 0) {}
    bool irq() const { return (regs_[0x14] & 3) == 3; }
    void vblank() { regs_[0x14] |= 2; }
    uint8_t peek(uint32_t offset) const {
        if (offset == 2) return 0; // fixed color TFT panel
        if (offset == 0x42)
            return uint8_t(palette_[regs_[0x46]] >> (16 - component_ * 8));
        return offset < sizeof regs_ ? regs_[offset] : 0;
    }
    uint8_t read(uint32_t offset) {
        const uint8_t value = peek(offset);
        if (offset == 0x42) advance(0x46);
        return value;
    }
    void write(uint32_t offset, uint8_t value) {
        if (offset >= sizeof regs_) return;
        if (offset == 0x14 && (value & 2)) {
            // Acknowledging VBL retains the old interrupt-enable bit.
            value = regs_[offset] & ~2;
        } else if (offset == 0x40 || offset == 0x46) {
            component_ = 0;
        } else if (offset == 0x42) {
            uint32_t& color = palette_[regs_[0x40]];
            const unsigned shift = 16 - component_ * 8;
            color = (color & ~(0xFFu << shift)) | (uint32_t(value) << shift);
            advance(0x40);
        } else if (offset == 0x12 && !(regs_[8] & 4)) {
            // Monochrome-panel palettes are spaced by the pixel width.
            if (value <= 3) {
                const int count = 1 << (1 << value);
                for (int i = 0; i < count; i++) {
                    int gray = 0;
                    if (value == 0) gray = i ? 0 : 255;
                    if (value == 1) gray = (i == 0 ? 255 : i == 1 ? 192 : i == 2 ? 64 : 0);
                    if (value == 2) gray = (15 - i) << 4;
                    if (value == 3) gray = 255 - i;
                    palette_[i << (8 - (1 << value))] = uint32_t(gray) * 0x010101u;
                }
            }
        }
        regs_[offset] = value;
    }
    uint8_t vramRead(uint32_t addr) const { return vram_[addr & (kVramSize - 1)]; }
    void vramWrite(uint32_t addr, uint8_t value) { vram_[addr & (kVramSize - 1)] = value; }

    void decode(std::vector<uint32_t>& out) const {
        out.assign(kWidth * kHeight, 0x00000Fu);
        const unsigned mode = regs_[0x12];
        if (!(regs_[8] & 8) || mode > 4) return;
        const int height = (regs_[8] & 2) ? 480 : 400;
        const int top = (regs_[8] & 6) == 4 ? 40 : 0;
        const unsigned bits = 1u << mode;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < kWidth; x++) {
                const unsigned bit = (y * kWidth + x) * bits;
                const uint8_t byte = vram_[bit / 8];
                uint32_t color;
                if (mode == 4) {
                    const uint16_t pixel = uint16_t(byte << 8 | vram_[bit / 8 + 1]);
                    color = uint32_t((pixel >> 10) & 31) << 19 |
                            uint32_t((pixel >> 5) & 31) << 11 |
                            uint32_t(pixel & 31) << 3;
                } else {
                    const unsigned index = (unsigned(byte) << (bit & 7)) & (0xFFu & (0xFFu << (8 - bits)));
                    color = palette_[index];
                }
                out[(y + top) * kWidth + x] = color;
            }
        }
    }
    template<class Ar> void visit(Ar& ar) {
        ar.blob(vram_);
        ar.bytes(regs_, sizeof regs_);
        ar(palette_);
        ar(component_);
    }

private:
    void advance(unsigned reg) {
        if (++component_ == 3) { component_ = 0; ++regs_[reg]; }
    }
    std::vector<uint8_t> vram_;
    uint8_t regs_[0x80] = {};
    std::array<uint32_t, 256> palette_{};
    uint8_t component_ = 0;
};
