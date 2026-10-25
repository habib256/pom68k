// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Display Card 8•24 — contract and sources in DisplayCard824.h. Line
// citations are MAME 0.285 src/devices/bus/nubus/nubus_48gc.cpp.

#include "DisplayCard824.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace {

// :243-263 — sense codes: [0] none driven, [1] sense2 driven low, [2] sense1,
// [3] sense0 (a driven line pulls the lines it is wired to low).
struct MonitorInfo { bool mono; uint8_t sense[4]; };
constexpr MonitorInfo kMonitors[16] = {
    {false, {0, 0, 0, 0}},   //  0: RGB 21"
    {true,  {1, 1, 1, 0}},   //  1: Full-Page (B&W 15")
    {false, {2, 2, 0, 2}},   //  2: RGB 12"
    {true,  {3, 3, 1, 2}},   //  3: Two-Page (B&W 21")
    {false, {4, 0, 4, 4}},   //  4: NTSC Monitor
    {false, {5, 1, 5, 4}},   //  5: RGB 15"
    {false, {6, 2, 4, 6}},   //  6: Hi-Res (12-14")
    {false, {6, 0, 0, 6}},   //  7: Multiple Scan 14"
    {false, {6, 0, 4, 6}},   //  8: Multiple Scan 16"
    {false, {6, 2, 0, 6}},   //  9: Multiple Scan 21"
    {false, {7, 0, 0, 0}},   // 10: PAL Encoder
    {false, {7, 1, 1, 0}},   // 11: NTSC Encoder
    {false, {7, 1, 1, 6}},   // 12: VGA/Super VGA
    {false, {7, 2, 5, 2}},   // 13: RGB 16"
    {false, {7, 3, 0, 0}},   // 14: PAL Monitor
    {false, {7, 3, 4, 4}},   // 15: RGB 19"
};

constexpr uint32_t kRegBase = 0x200000;   // :351-354
constexpr uint32_t kRegEnd = 0x200400;

}  // namespace

bool DisplayCard824::monitorFromName(const std::string& name, uint8_t& out) {
    struct Entry { const char* name; uint8_t monitor; };
    static constexpr Entry kNames[] = {
        {"hires", kHiRes}, {"rgb12", kRgb12}, {"rgb16", kRgb16},
        {"rgb21", kRgb21},
    };
    for (const Entry& e : kNames)
        if (name == e.name) { out = e.monitor; return true; }
    return false;
}

bool DisplayCard824::monitorFromWidth(int width, uint8_t& out) {
    struct Entry { int width; uint8_t monitor; };
    static constexpr Entry kWidths[] = {
        {512, kRgb12}, {640, kHiRes}, {832, kRgb16}, {1152, kRgb21},
    };
    for (const Entry& e : kWidths)
        if (width == e.width) { out = e.monitor; return true; }
    return false;
}

DisplayCard824::DisplayCard824(NuBus& bus, int slot, int64_t cpuHz,
                               uint8_t monitor)
    : bus_(bus), slot_(slot), cpuHz_(cpuHz),
      monitor_(monitorSupported(monitor) ? monitor : uint8_t(kHiRes)) {
    vram_.assign(kVramSize, 0);
    reset();
}

// :480-508.
void DisplayCard824::reset() {
    std::fill(vram_.begin(), vram_.end(), 0);
    vblDisable_ = true;
    control_ = 0x0002;
    preload_ = 256 - 8;
    base_ = 0;
    stride_ = 80 / 4;
    clutOffs_ = clutCnt_ = 0;
    colors_ = {};
    ramdacMode_ = ramdacConv_ = 0;
    hhalf_ = 0;
    hactive_ = 286; hbporch_ = 22; hsync_ = 30; hfporch_ = 18;
    vactive_ = 1740; vbporch_ = 78; vsync_ = 6; vfporch_ = 6;
    multiplier_ = 190; modulus_ = 19; pdiv_ = 1;
    pens_.fill(0);
    latch_ = {};
    latchMask_ = 0;
    latchReg_ = 0;
    framePos_ = 0;
    frameCount_ = 0;
    setIrq(false);
    updateCrtc();
}

std::vector<uint8_t> DisplayCard824::installDeclRom(const std::vector<uint8_t>& raw) {
    // MAME install_declaration_rom (nubus.cpp:399-533): the last byte names
    // the byte lanes, the one before it says whether the image is inverted.
    if (raw.size() != kDeclRomSize) return {};
    uint8_t lanes = raw.back();
    const bool inverted = raw[raw.size() - 2] == 0xFF;
    if (inverted) lanes ^= 0xFF;
    std::vector<uint8_t> image;
    if (lanes == 0x78) {                       // lane 3 only — the 341-0868
        image.assign(raw.size() * 4, 0);
        for (std::size_t i = 0; i < raw.size(); ++i) image[i * 4 + 3] = raw[i];
    } else if (lanes == 0x0F) {
        image = raw;
    } else {
        return {};
    }
    if (inverted)
        for (uint8_t& b : image) b ^= 0xFF;
    return image;
}

std::vector<uint8_t> DisplayCard824::loadDeclRom(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    return installDeclRom(raw);
}

int DisplayCard824::bitsPerPixel() const {
    switch (ramdacMode_) {
        case 0x0: return 1;
        case 0x4: return 2;
        case 0x8: return 4;
        case 0xC: return 8;
        case 0xD: return 24;
        default: return 0;
    }
}

void DisplayCard824::setIrq(bool on) {
    irq_ = on;
    bus_.setSlotIrq(slot_, on);
}

uint8_t DisplayCard824::senseBits() const {                 // :819-826
    const MonitorInfo& m = kMonitors[monitor_ & 15];
    uint8_t sense = m.sense[0];
    if ((control_ >> 11) & 1) sense &= m.sense[1];
    if ((control_ >> 10) & 1) sense &= m.sense[2];
    if ((control_ >> 9) & 1) sense &= m.sense[3];
    return sense;
}

// ── CRTC + clock synthesizer → frame geometry and clock (:748-811) ──────
void DisplayCard824::updateCrtc() {
    const int vtotal = vactive_ + vbporch_ + vsync_ + vfporch_;
    crtcValid_ = false;
    if (vtotal && vactive_ && multiplier_ && modulus_) {
        const bool conv = convolution();
        // Progressive only: vertical counts are half-lines (MAME's own
        // simplification; the interlaced monitors are not offered).
        vlines_ = vtotal >> 1;
        top_ = (vsync_ + vbporch_) >> 1;
        const int height = vactive_ >> 1;
        const int divider = 256 - preload_;
        const double pixclk = 20e6 / modulus_ * multiplier_ / double(1 << pdiv_);
        const int htotal = hactive_ + hbporch_ + hsync_ + hfporch_ + 8;
        const int hstart = hbporch_ + hsync_ + 4;
        const int hact = hactive_ + 2;
        int scale = 0;
        switch (ramdacMode_) {
            case 0x0: scale = 5; break;   // 32 pixels per longword
            case 0x4: scale = 4; break;
            case 0x8: scale = 3; break;
            case 0xC: scale = 2; break;
            case 0xD: scale = 0; break;   // 24 bpp: one pixel per longword
        }
        const int cshift = conv ? 2 : 0;
        hpixels_ = ((htotal << scale) >> cshift) / divider;
        left_ = ((hstart << scale) >> cshift) / divider;
        const int width = ((hact << scale) >> cshift) / divider;
        halflinePixels_ = ((int(hhalf_) << scale) >> cshift) / divider;
        const double ticks = double(int64_t(hpixels_) * vlines_) * double(1 << cshift);
        const int64_t frame = int64_t(double(cpuHz_) * ticks / pixclk);
        if (width > 0 && height > 0 && width <= 2048 && height <= 1024 &&
            vlines_ > height && hpixels_ > 0 && frame >= cpuHz_ / 250) {
            width_ = width;
            height_ = height;
            frameCycles_ = frame;
            activeCycles_ = frame * height / vlines_;
            crtcValid_ = true;
        }
    }
    if (!crtcValid_ && frameCycles_ <= 0) {
        // Before any plausible programming: a 60 Hz cadence keeps time.
        frameCycles_ = cpuHz_ / 60;
        activeCycles_ = frameCycles_ * 480 / 525;
        vlines_ = 525;
        top_ = 0;
        hpixels_ = 800;
        left_ = 0;
    }
    if (framePos_ >= frameCycles_) framePos_ %= frameCycles_;
}

// ── Registers ───────────────────────────────────────────────────────────
uint32_t DisplayCard824::regRead(uint32_t reg) const {
    if (reg < kRegBase + 0x10) {                             // :813-837
        switch (reg - kRegBase) {
            case 0x00: return uint32_t(control_ & 0xF1FF) | (uint32_t(senseBits()) << 9);
            case 0x04: return preload_;
            case 0x08: return base_;
            case 0x0C: return stride_;
        }
        return 0;
    }
    if (reg >= kRegBase + 0x100 && reg < kRegBase + 0x200) { // :839-945
        switch (reg - (kRegBase + 0x100)) {
            case 0x08: return hhalf_;
            case 0x0C: return hactive_;
            case 0x10: return hbporch_;
            case 0x14: return hsync_;
            case 0x18: return hfporch_;
            case 0x24: return vactive_;
            case 0x28: return vbporch_;
            case 0x2C: return vsync_;
            case 0x30: return vfporch_;
            case 0xC0: {
                // Beam status, active-low phase flags. The frame clock starts
                // at the first visible line; the CRTC counts from sync.
                const int64_t perLine = std::max<int64_t>(1, frameCycles_ / std::max(1, vlines_));
                const int lineFromTop = int(framePos_ / perLine);
                const int vpos = (top_ + lineFromTop) % std::max(1, vlines_);
                const int hpos = int((framePos_ % perLine) * hpixels_ / perLine);
                const int halfline = (vpos << 1) | (hpos >= left_ + halflinePixels_ ? 1 : 0);
                uint8_t r = 0x0F;
                if (hpos < left_ || hpos > left_ + width_ - 1) r |= 0x20;
                if (halfline < vsync_) r &= uint8_t(~0x04);
                else if (halfline < vsync_ + vbporch_) r &= uint8_t(~0x02);
                else if (halfline < vsync_ + vbporch_ + vactive_) r &= uint8_t(~0x01);
                else r &= uint8_t(~0x08);
                return r;
            }
        }
        return 0;
    }
    if (reg == kRegBase + 0x200) return 0;   // CLUT address: read disabled (:951-954)
    return 0;
}

void DisplayCard824::regWrite(uint32_t reg, uint32_t data) {
    if (reg < kRegBase + 0x10) {                             // :962-999
        data &= 0xFFFF;
        switch (reg - kRegBase) {
            case 0x00:
                control_ = uint16_t(data & 0x7FFF);          // reset bit reads 0
                updateCrtc();
                break;
            case 0x04: preload_ = uint16_t(data & 0xFF); updateCrtc(); break;
            case 0x08: base_ = data; break;
            case 0x0C: stride_ = data; break;
        }
        return;
    }
    if (reg >= kRegBase + 0x100 && reg < kRegBase + 0x200) { // :1001-1071
        data &= 0xFFFF;
        switch (reg - (kRegBase + 0x100)) {
            case 0x08: hhalf_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x0C: hactive_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x10: hbporch_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x14: hsync_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x18: hfporch_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x24: vactive_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x28: vbporch_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x2C: vsync_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x30: vfporch_ = uint16_t(data & 0x0FFF); updateCrtc(); break;
            case 0x3C: vblDisable_ = (data & 2) != 0; break;
            case 0x48: setIrq(false); break;                // clear the VBL interrupt
        }
        return;
    }
    if (reg >= kRegBase + 0x200 && reg < kRegBase + 0x210) { // :1073-1106
        data &= 0xFF;
        switch (reg - (kRegBase + 0x200)) {
            case 0x00: clutOffs_ = uint8_t(data); clutCnt_ = 0; break;
            case 0x04:
                colors_[clutCnt_++] = uint8_t(data);
                if (clutCnt_ == 3) {
                    pens_[clutOffs_] = uint32_t(colors_[0]) << 16 |
                                       uint32_t(colors_[1]) << 8 | colors_[2];
                    clutOffs_++;
                    clutCnt_ = 0;
                }
                break;
            case 0x08:
                ramdacMode_ = uint8_t((data >> 1) & 0xF);
                ramdacConv_ = uint8_t(data & 1);
                updateCrtc();
                break;
        }
        return;
    }
    if (reg >= kRegBase + 0x300 && reg < kRegBase + 0x340) { // :1108-1141
        data &= 0x0F;
        const uint32_t o = (reg - (kRegBase + 0x300)) / 4;
        if (o <= 3) {
            multiplier_ = uint16_t((multiplier_ & ~(0x0F << (o * 4))) | (data << (o * 4)));
            updateCrtc();
        } else if (o >= 4 && o <= 6) {
            const uint32_t k = o & 3;
            modulus_ = uint16_t((modulus_ & ~(0x0F << (k * 4))) | (data << (k * 4)));
            updateCrtc();
        } else if (o == 9) {
            pdiv_ = uint8_t(data);
            updateCrtc();
        }
    }
}

// One CPU bus cycle, rebuilt from its bytes. The registers sit on the low
// lanes and the 68020 smears a narrow write across the port, so the value a
// register receives is the low word of the 32-bit transfer:
//   long (lanes 0-3)   → lanes 2-3          word at +2 → lanes 2-3
//   word at +0         → lanes 0-1 (smeared) byte      → the byte, smeared
// Lanes 0-1 complete a word that may still be the high half of a long, so
// it waits: a following lane-2/3 write of the same register supersedes it;
// anything else (another register, a lane already written — the next cycle
// of a word-then-long sequence the driver issues on the CLUT data port —,
// a read, VRAM, the next tick) commits it.
void DisplayCard824::writeReg(uint32_t slotOff, uint8_t v) {
    const uint32_t reg = slotOff & ~3u;
    const unsigned lane = slotOff & 3u;
    if (latchMask_ && (reg != latchReg_ || (latchMask_ & (1u << lane))))
        flushLatch();
    latchReg_ = reg;
    latch_[lane] = v;
    latchMask_ = uint8_t(latchMask_ | (1u << lane));
    if (lane == 3) {
        const uint8_t hi = (latchMask_ & 4) ? latch_[2] : v;
        latchMask_ = 0;
        regWrite(reg, uint32_t(hi) << 8 | v);
    }
}

void DisplayCard824::flushLatch() {
    if (!latchMask_) return;
    const uint8_t mask = latchMask_;
    latchMask_ = 0;
    uint8_t hi = 0, lo = 0;
    if (mask & 2) { lo = latch_[1]; hi = (mask & 1) ? latch_[0] : lo; }
    else if (mask & 4) { lo = hi = latch_[2]; }
    else { lo = hi = latch_[0]; }
    regWrite(latchReg_, uint32_t(hi) << 8 | lo);
}

// ── VRAM: indexed view, or packed 24-bit (control bit 2, :980, :1143) ───
uint8_t DisplayCard824::vramRead(uint32_t off) const {
    if (!packedView()) return off < kVramSize ? vram_[off] : 0;
    const uint32_t lane = off & 3;
    if (lane == 0) return 0;
    const uint64_t i = uint64_t(off >> 2) * 3 + (lane - 1);
    return i < kVramSize ? vram_[size_t(i)] : 0;
}

void DisplayCard824::vramWrite(uint32_t off, uint8_t v) {
    if (!packedView()) {
        if (off < kVramSize) vram_[off] = v;
        return;
    }
    const uint32_t lane = off & 3;
    if (lane == 0) return;
    const uint64_t i = uint64_t(off >> 2) * 3 + (lane - 1);
    if (i < kVramSize) vram_[size_t(i)] = v;
}

uint8_t DisplayCard824::read8(uint32_t slotOff) {
    flushLatch();
    const uint32_t off = slotOff & 0xFFFFFF;
    if (off < kRegBase) return vramRead(off);
    if (off < kRegEnd) {
        const uint32_t value = regRead(off & ~3u);
        return uint8_t(value >> (24 - 8 * (off & 3)));
    }
    return 0;
}

uint16_t DisplayCard824::read16(uint32_t slotOff) {
    return uint16_t(read8(slotOff) << 8 | read8(slotOff + 1));
}

uint32_t DisplayCard824::read32(uint32_t slotOff) {
    return uint32_t(read16(slotOff)) << 16 | read16(slotOff + 2);
}

void DisplayCard824::write8(uint32_t slotOff, uint8_t v) {
    const uint32_t off = slotOff & 0xFFFFFF;
    if (off >= kRegBase && off < kRegEnd) { writeReg(off, v); return; }
    flushLatch();
    if (off < kRegBase) vramWrite(off, v);
}

void DisplayCard824::write16(uint32_t slotOff, uint16_t v) {
    write8(slotOff, uint8_t(v >> 8));
    write8(slotOff + 1, uint8_t(v));
}

void DisplayCard824::write32(uint32_t slotOff, uint32_t v) {
    write16(slotOff, uint16_t(v >> 16));
    write16(slotOff + 2, uint16_t(v));
}

// ── Frame clock and VBL (:516-539) ──────────────────────────────────────
void DisplayCard824::tick(int cpuCycles) {
    flushLatch();
    if (frameCycles_ <= 0) return;
    int64_t left = cpuCycles;
    while (left > 0) {
        const int64_t before = framePos_;
        const int64_t edge = before < activeCycles_ ? activeCycles_ : frameCycles_;
        const int64_t step = std::min(left, edge - before);
        framePos_ += step;
        left -= step;
        if (before < activeCycles_ && framePos_ >= activeCycles_ && !vblDisable_)
            setIrq(true);                                    // VBL start
        if (framePos_ >= frameCycles_) {
            framePos_ = 0;
            frameCount_++;
        }
    }
}

// ── Scan-out (:541-746) ─────────────────────────────────────────────────
uint32_t DisplayCard824::screenPixel(uint32_t rgb) const {
    if (kMonitors[monitor_ & 15].mono) {
        const uint32_t b = rgb & 0xFF;                       // :608
        rgb = b << 16 | b << 8 | b;
    }
    return rgb | 0xFF000000u;
}

void DisplayCard824::decodeRows(std::vector<uint32_t>& out, int y0, int y1) const {
    if (out.size() < size_t(width_) * size_t(height_)) return;
    y0 = std::max(0, y0);
    y1 = std::min(height_, y1);
    const int bpp = bitsPerPixel();
    if (!scanning() || !bpp || !crtcValid_) {                // :543-547
        for (int y = y0; y < y1; ++y)
            std::fill_n(out.begin() + std::ptrdiff_t(y) * width_, width_, 0xFF000000u);
        return;
    }
    const bool direct = bpp == 24;
    const bool conv = convolution() && !direct;
    const uint64_t baseOff = uint64_t(base_) << ((direct ? 6 : 5) + (convolution() ? 1 : 0));
    const uint64_t stride = uint64_t(stride_) << (direct ? 3 : 2);
    auto at = [&](uint64_t i) -> uint8_t { return i < kVramSize ? vram_[size_t(i)] : 0; };
    auto index = [&](uint64_t row, int x) -> uint32_t {
        const uint64_t bit = uint64_t(x) * uint64_t(bpp);
        const uint8_t byte = at(row + bit / 8);
        const int shift = 8 - bpp - int(bit % 8);
        return uint32_t(byte >> shift) & ((1u << bpp) - 1);
    };
    for (int y = y0; y < y1; ++y) {
        uint32_t* line = out.data() + size_t(y) * size_t(width_);
        const uint64_t row = baseOff + uint64_t(y) * stride;
        for (int x = 0; x < width_; ++x) {
            uint32_t rgb;
            if (direct) {
                const uint64_t p = row + uint64_t(x) * 3;
                rgb = uint32_t(at(p)) << 16 | uint32_t(at(p + 1)) << 8 | at(p + 2);
            } else if (!conv) {
                rgb = pens_[index(row, x)];
            } else {
                // 1:2:1 vertical convolution of the rows above/at/below.
                const uint32_t a = pens_[index(row, x)];
                const uint32_t b = pens_[index(row + stride, x)];
                const uint32_t c = pens_[index(row + 2 * stride, x)];
                auto mixc = [&](int sh) {
                    const uint32_t s = ((a >> sh) & 0xFF) + (((b >> sh) & 0xFF) << 1)
                                     + ((c >> sh) & 0xFF) + 2;
                    return (s >> 2) << sh;
                };
                rgb = mixc(16) | mixc(8) | mixc(0);
            }
            line[x] = screenPixel(rgb);
        }
    }
}

void DisplayCard824::decode(std::vector<uint32_t>& out) const {
    out.assign(size_t(width_) * size_t(height_), 0xFF000000u);
    decodeRows(out, 0, height_);
}

void DisplayCard824::raster(std::vector<uint32_t>& out) {
    const size_t need = size_t(width_) * size_t(height_);
    if (out.size() != need) {
        out.assign(need, 0xFF000000u);
        beam_.restartFrame();
    }
    beam_.setGeometry(frameCycles_, activeCycles_, vlines_, height_);
    if (!beam_.valid()) { decodeRows(out, 0, height_); return; }
    beam_.setPos(framePos_, frameCount_);
    beam_.pumpRows([&](int a, int b) { decodeRows(out, a, b); });
}
