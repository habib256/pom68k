// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Dafb.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

void Dafb::reset() {
    std::memset(regs_, 0, sizeof regs_);
    std::memset(clut_, 0, sizeof clut_);
    intStatus_ = 0;
    swatchIntEnable_ = 0;
    cursorLine_ = 0;
    palAddress_ = palIdx_ = 0;
    ac842Pbctrl_ = pcbr1_ = 0;
    base_ = 0;
    stride_ = 1024;
    config_ = 0;
    mode_ = 0;
    std::memset(hParams_, 0, sizeof hParams_);
    std::memset(vParams_, 0, sizeof vParams_);
    swatchMode_ = 1;                             // display disabled at reset
    hres_ = vres_ = htotal_ = vtotal_ = 0;
    monitorId_ = 0;
    pixelClock_ = 31334400;
    gazShift_ = 0; gazBits_ = 0; gazLastClock_ = 0;
    gazMclk_ = 31334400;
    dp8534Shift_ = 0;
    std::memset(dp8531Regs_, 0, sizeof dp8531Regs_);
    framePos_ = 0;
    prevLine_ = 0;
}

void Dafb::recalcIrq() {
    // write_irq → MEMCjr via2_irq_w<0x40>: nubus bit 6, active low.
    if (onIrq) onIrq(intStatus_ != 0);
}

uint32_t Dafb::read32(uint32_t off) {
    switch (off & 0x3FC) {
        // main ($000): dafb_r
        case 0x00: return (base_ >> 9) & 0xFFF;
        case 0x04: return (base_ >> 5) & 0x0F;
        case 0x08: return stride_ >> 2;           // stride in 32-bit words
        case 0x10: return config_;
        case 0x1C: {
            // Inverse of monitor sense (dafb_r $1c). Plain codes come back
            // whole; extended (type 6/7) codes are probed by driving one ID
            // pin at a time ($1C writes) and reading the other two.
            uint8_t mon = monitorConfig_;
            uint8_t res;
            if (mon & 0x40) {                     // extended code ext(bc,ac,ab)
                res = 7;
                if (monitorId_ == 0x4)
                    res &= uint8_t(4 | (((mon >> 5) & 1) << 1) | ((mon >> 4) & 1));
                if (monitorId_ == 0x2)
                    res &= uint8_t((((mon >> 3) & 1) << 2) | 2 | ((mon >> 2) & 1));
                if (monitorId_ == 0x1)
                    res &= uint8_t((((mon >> 1) & 1) << 2) | ((mon & 1) << 1) | 1);
            } else {
                res = mon;
            }
            return res ^ 7u;
        }
        case 0x24: return regs_[0x24 >> 2];       // SCSI ctrl (vestigial on
        case 0x28: return regs_[0x28 >> 2];       // MEMCjr — TurboSCSI moved
                                                  // into PrimeTime)
        case 0x2C:
            // test/version (dafb.cpp:426-427): the flavour's version in
            // bits 11-9 — 1 on the discrete DAFB (Q700/Q900), 3 on every
            // DAFB II (Q950/djMEMC/MEMCjr device_start).
            return (regs_[0x2C >> 2] & 0x1FF) | (uint32_t(version_) << 9);
        // Swatch ($100): swatch_r
        case 0x108: return intStatus_;
        case 0x10C: intStatus_ &= ~4u; recalcIrq(); return 0;
        case 0x114: intStatus_ &= ~1u; recalcIrq(); return 0;
        case 0x124: case 0x128: case 0x12C: case 0x130: case 0x134:
        case 0x138: case 0x13C: case 0x140: case 0x144: case 0x148:
            return hParams_[((off & 0x3FC) - 0x124) >> 2];
        case 0x14C: case 0x150: case 0x154: case 0x158: case 0x15C:
        case 0x160: case 0x164:
            return vParams_[((off & 0x3FC) - 0x14C) >> 2];
        // RAMDAC ($200): ramdac_r (Antelope PCBR1 dance)
        case 0x200: palIdx_ = 0; return palAddress_;
        case 0x210: {
            // ── Deliberate divergence, in POM68K's favour ───────────────
            // The read phase cycles R→G→B→R here, as the AC842/Antelope
            // silicon does. MAME lets m_pal_idx run UNBOUNDED
            // (dafb.cpp:726-731): a 4th consecutive read falls off the end
            // of its switch and answers 0, and the counter it leaves
            // behind (3, 4, …) then poisons the WRITE path — ramdac_w's
            // switch has no case above 2, so every subsequent palette
            // write stores nothing and the `== 3` re-sync never triggers
            // again (dafb.cpp:772-790). Nothing on the shipped profiles
            // reads the CLUT four times in a row, so this costs no parity;
            // adopting MAME's form would import a latent state trap.
            uint8_t c = clut_[palAddress_][palIdx_ % 3];
            if (++palIdx_ == 3) palIdx_ = 0;
            return c;
        }
        case 0x220:
            // The PCBR1 dance exists only on the AC842a/Antelope flavours
            // (dafb_q950/memc/memcjr ramdac_r overrides); the plain AC842
            // of the Q700/Q900 always answers PCBR0 (dafb.cpp:743-745).
            if (ramdac_ != Ramdac::Ac842
                && palAddress_ == 1 && (ac842Pbctrl_ & 0x06) == 0x06)
                return pcbr1_;
            return ac842Pbctrl_;
        default:
            if ((off & 0x300) == 0x300) return 0;   // Gazelle clockgen
            return regs_[(off >> 2) & 0xFF];
    }
}

void Dafb::write32(uint32_t off, uint32_t v) {
    uint32_t idx = (off >> 2) & 0xFF;
    regs_[idx] = v;
    switch (off & 0x3FC) {
        case 0x00:
            base_ = (base_ & 0x1E0) | ((v & 0xFFF) << 9);
            break;
        case 0x04:
            base_ = (base_ & ~0x1E0u) | ((v & 0x0F) << 5);
            break;
        case 0x08:
            stride_ = v << 2;                     // register is 32-bit words
            break;
        case 0x10:
            config_ = uint16_t(v);
            break;
        case 0x1C:
            // Drive monitor sense lines (dafb_w $1c): 0 = drive, 1 = tri-state.
            monitorId_ = uint8_t((v & 7) ^ 7);
            break;
        case 0x100:                               // Swatch mode; bit 0 = blank
            swatchMode_ = v;
            break;
        case 0x104:                               // Swatch int enable
            // Bit 1 = aux-scanline interrupt. MAME calls fatalerror() on it
            // (dafb.cpp:646-649); POM68K takes the write and stays silent.
            // Aligning would mean aborting the emulator on a register
            // write — an unhelpful observable, and one no guest on the
            // shipped profiles produces (the Apple driver arms bits 0 and 2
            // only). If a guest is ever seen to set bit 1, its timer
            // belongs right here next to the VBL and cursor ones.
            swatchIntEnable_ = v;
            if (!(v & 1)) intStatus_ &= ~1u;
            if (!(v & 4)) intStatus_ &= ~4u;
            recalcIrq();
            break;
        case 0x10C: intStatus_ &= ~4u; recalcIrq(); break;
        case 0x114: intStatus_ &= ~1u; recalcIrq(); break;
        case 0x118: cursorLine_ = v & 0xFFF; break;
        case 0x124: case 0x128: case 0x12C: case 0x130: case 0x134:
        case 0x138: case 0x13C: case 0x140: case 0x144: case 0x148:
            // HSERR/HLFLN/HEQ/HSP/HBWAY/HBRST/HBP/HAL/HFP/HPIX
            hParams_[((off & 0x3FC) - 0x124) >> 2] = uint16_t(v);
            break;
        case 0x14C: case 0x150: case 0x154: case 0x158: case 0x15C:
        case 0x160: case 0x164:
            // VHLINE/VSYNC/VBPEQ/VBP/VAL/VFP/VFPEQ (half-line units)
            vParams_[((off & 0x3FC) - 0x14C) >> 2] = uint16_t(v);
            break;
        case 0x200: palAddress_ = uint8_t(v); palIdx_ = 0; break;
        case 0x210:
            // A monochrome monitor (sense 1 = 15" Portrait, 3 = 21"
            // Two-Page) wires only the blue DAC to the video amplifier, so
            // the R and G bytes are dropped on the floor and the blue one
            // drives all three primaries (dafb.cpp:758-770). Same rule the
            // RBV's portrait display gets (RbvVideo.h pen()) and Sonora's
            // mono modeline (SonoraMemory::dacWrite) — applied at the
            // write here, as MAME does, because the DAFB's renderers live
            // in three different memory maps and would each need the sense.
            if (monitorConfig_ == 1 || monitorConfig_ == 3) {
                if (palIdx_ == 2)
                    clut_[palAddress_][0] = clut_[palAddress_][1] =
                        clut_[palAddress_][2] = uint8_t(v);
            } else {
                clut_[palAddress_][palIdx_] = uint8_t(v);
            }
            if (++palIdx_ == 3) { palIdx_ = 0; palAddress_++; }
            break;
        case 0x220:                               // PCBR0 (+ PCBR1 on DAFB II)
            // AC842a/Antelope only (see the read side): the plain AC842
            // takes every $220 write as PCBR0 (dafb_base::ramdac_w:792-820)
            // and has no x555 mode. PCBR1 version ID: $01 = AC842a (Q950,
            // dafb.cpp:1131), $02 = Antelope (djMEMC/MEMCjr, :1258/:1400).
            if (ramdac_ != Ramdac::Ac842
                && palAddress_ == 1 && (ac842Pbctrl_ & 0x06) == 0x06)
                pcbr1_ = uint8_t(v & 0xF0)
                       | (ramdac_ == Ramdac::Ac842a ? 0x01 : 0x02);
            else {
                ac842Pbctrl_ = uint8_t(v);
                if (ramdac_ != Ramdac::Ac842
                    && (pcbr1_ & 0xC0) == 0xC0 && (ac842Pbctrl_ & 0x06) == 0x06) {
                    mode_ = 5;                    // AC842a/Antelope x555
                } else {
                    switch (ac842Pbctrl_ & 0x1C) {
                        case 0x00: mode_ = 0; break; // 1 bpp
                        case 0x08: mode_ = 1; break; // 2 bpp
                        case 0x10: mode_ = 2; break; // 4 bpp
                        case 0x18: mode_ = 3; break; // 8 bpp
                        case 0x1C: mode_ = 4; break; // 24 bpp
                    }
                }
                recalcMode();                     // ramdac_w → recalc_mode()
            }
            break;
        default: break;
    }
}

// dafb.cpp recalc_mode(): derive the active area and totals from the
// Swatch CRTC parameters. HFP-HAL is the active width; VAL/VFP are in
// half-line units (interlace). The AC842a clock-divider bits (PCBR0
// 6-5) multiply the width in non-convolved modes and divide it when
// convolution (config bit 3) is on; MEMCjr machines never enable
// convolution (no NTSC/PAL support), the branch is kept for register
// parity. Interlace (config bit 2) doubles the vertical.
void Dafb::recalcMode() {
    enum { HAL = 7, HFP = 8, HPIX = 9, VAL = 4, VFP = 5, VFPEQ = 6 };
    htotal_ = hParams_[HPIX];
    vtotal_ = vParams_[VFPEQ] >> 1;
    if (!htotal_ || !vtotal_) return;

    hres_ = uint32_t(hParams_[HFP]) - hParams_[HAL];
    vres_ = uint32_t(vParams_[VFP] >> 1) - (vParams_[VAL] >> 1);

    // dafb.cpp:833-839 — the Quadra 700 programs the wrong base for the
    // 512×384 mode and is off-by-1 on the vertical res; MAME patches both
    // on version 1 only (the Q800/Q605 drivers program it correctly).
    if (hres_ == 512 && version_ == 1) {
        base_ = 0x1000;
        vres_ = 384;
    }

    const uint32_t clockdiv = 1u << ((ac842Pbctrl_ & 0x60) >> 5);
    if (config_ & 0x08) {                        // convolution (see above)
        hres_ /= clockdiv;
        // MAME also runs `m_stride /= clockdiv` here (dafb.cpp:847). We
        // deliberately do not. It is a DESTRUCTIVE edit of the stride
        // register echo — clear convolution again and MAME's $008 readback
        // stays permanently divided, and a second recalc divides it once
        // more — while being unobservable in MAME itself, since
        // screen_update pins the pitch at 1024 for the whole time
        // convolution is on (dafb.cpp:267, mirrored by Dafb::stride()).
        // Gate: "clearing convolution restores programmed stride" in
        // tests/q605_dafb_test.cpp.
        hres_ -= 23;                             // dafb.cpp Q700 quirk
    } else {
        hres_ *= clockdiv;
        htotal_ *= clockdiv;
    }
    if (config_ & 0x04) {                        // interlace
        vres_ <<= 1;
        vtotal_ <<= 1;
    }
}

// Three DAFB flavours, three different clock chips behind the same +$300
// window (dafb.cpp): the discrete DAFB of the Quadra 700 has a National
// DP8531 (dafb_base::clockgen_w:884), djMEMC a DP8534
// (dafb_memc_device::clockgen_w:1197) and MEMCjr Apple's "Gazelle"
// (dafb_memcjr_device::clockgen_w:1322). They share nothing but the
// address range, so route on the ctor variant — feeding one chip's
// bitstream to another's decoder latches garbage (the DP8531 nibble
// writes to register 12 land exactly on the Gazelle's $3C3 port).
void Dafb::clockgenWrite8(uint32_t off, uint8_t v) {
    const uint32_t before = pixelClock_;
    switch (clockgen_) {
    case Clockgen::Gazelle: clockgenGazelle(off, v); break;
    case Clockgen::Dp8534:  clockgenDp8534(off, v);  break;
    case Clockgen::Dp8531:  clockgenDp8531(off, v);  break;
    }
    // POM68K_DAFB_CLOCK_TRACE=1 — one line per latched clock. The frame
    // cadence (tick()) hangs off pixelClock_, so a machine whose ROM never
    // reaches its own decoder shows nothing here and silently keeps the
    // legacy 60 Hz/525-line shape.
    if (trace_ && pixelClock_ != before) {
        static const char* kName[] = { "Gazelle", "DP8534", "DP8531" };
        std::fprintf(stderr, "dafb: %s latched pclk %u Hz (was %u)\n",
                     kName[int(clockgen_)], pixelClock_, before);
    }
}

// Gazelle (dafb_memcjr clockgen_w): a 20-bit word is bit-banged into byte
// port $3C3 — bit 0 = data, bit 1 = clock (latch on rising edge). Layout
// (LSB first): bit 0 = /pclk-select, bits 5-4 = log2 P, bits 12-6 = N,
// bits 19-13 = M; clock = N/(M·P) × 31.3344 MHz.
void Dafb::clockgenGazelle(uint32_t off, uint8_t v) {
    if ((off & 0xFF) != 0xC3) return;
    if ((v & 2) && !(gazLastClock_ & 2)) {
        gazShift_ >>= 1;
        gazShift_ |= (v & 1) ? (1u << 19) : 0;
        if (++gazBits_ == 20) {
            gazBits_ = 0;
            const bool pclkSelect = !(gazShift_ & 1);
            const uint32_t P = 1u << ((gazShift_ >> 4) & 3);
            const uint32_t N = (gazShift_ >> 6) & 0x7F;
            const uint32_t M = (gazShift_ >> 13) & 0x7F;
            // MAME divides blind: `(double)m_N / ((double)m_M * m_P)` with
            // M = 0 yields inf, and the u32 cast of that is UB
            // (dafb.cpp:1350-1352). The same blind divide is guarded in
            // clockgenDp8534 (RCNT) and clockgenDp8531 (R). All three
            // guards are POM68K-only and stay: a junk bitstream must leave
            // the pixel clock alone, never freeze or explode the frame
            // cadence that hangs off it.
            if (M && P) {
                const uint32_t clk =
                    uint32_t(31334400.0 * double(N) / (double(M) * double(P)));
                if (pclkSelect) pixelClock_ = clk;
                else            gazMclk_ = clk;
            }
            gazShift_ = 0;
        }
    }
    gazLastClock_ = v;
}

// MAME's bitswap<8>(v, 0,1,2,3,4,5,6,7) — a plain bit reversal of a byte.
static inline uint8_t revByte(uint8_t v) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i) r = uint8_t(r | (((v >> i) & 1) << (7 - i)));
    return r;
}

// DP8534 (dafb_memc clockgen_w:1197) — djMEMC's clock chip. No datasheet
// exists; MAME reverse-engineered it from the DT3152 parameter solver.
// The bitstream is clocked into $303 one bit at a time (MSB first, hence
// a left shift, unlike the Gazelle's right shift) and committed by ANY
// write to $313 — there is no bit counter. On commit the 40-bit word is
// shifted up by 2 and read back as five bit-REVERSED bytes; P, RCNT and
// NCNT straddle those byte boundaries. pclk = 20 MHz × NCNT / RCNT / (P+1).
void Dafb::clockgenDp8534(uint32_t off, uint8_t v) {
    switch (off & 0xFF) {
    case 3:
        dp8534Shift_ = (dp8534Shift_ << 1) | uint64_t(v & 1);
        break;
    case 19: {
        const uint64_t params = dp8534Shift_ << 2;
        dp8534Shift_ = 0;

        const uint8_t param1 = revByte(uint8_t((params >> 32) & 0xFF));
        const uint8_t param2 = revByte(uint8_t((params >> 24) & 0xFF));
        const uint8_t param3 = revByte(uint8_t((params >> 16) & 0xFF));
        const uint8_t param4 = revByte(uint8_t((params >> 8) & 0xFF));
        const uint8_t param5 = revByte(uint8_t(params & 0xFF));

        const uint8_t p    = uint8_t((param1 >> 7) | (param2 << 1));
        const uint8_t rcnt = uint8_t((param2 >> 7) | (param3 << 1));
        const uint8_t ncnt = uint8_t((param4 >> 1) | (param5 << 7));

        if (!rcnt) break;                        // MAME divides blind; don't
        const double vco = 20000000.0 * double(ncnt) / double(rcnt);
        pixelClock_ = uint32_t(vco / double(p + 1) + 0.5);
        break;
    }
    default:
        break;
    }
}

// DP8531 (dafb_base clockgen_w:884) — the discrete DAFB's clock chip, and
// the only one that is register- rather than stream-programmed: nibbles
// land at $303, $313 … $3F3 (register = offset >> 4, only the low nibble
// of the byte is kept) and writing register 15 latches the new clock.
// R = regs 6:5:4, P = 1 << reg 9, the N modulus = regs 3:2:1:0 split into
// a swallow counter A (low 5 bits, inverted) and a main counter B; then
// N = 32(B-A) + 31(1+A) and pclk = (20 MHz / R) × N / P.
void Dafb::clockgenDp8531(uint32_t off, uint8_t v) {
    const uint32_t o = off & 0xFF;
    if ((o & 3) != 3) return;
    dp8531Regs_[(o >> 4) & 0xF] = uint8_t(v & 0x0F);
    if ((o >> 4) != 15) return;

    const int r = (dp8531Regs_[6] << 8) | (dp8531Regs_[5] << 4) | dp8531Regs_[4];
    if (!r) return;                              // MAME divides blind; don't
    const int p = 1 << dp8531Regs_[9];
    const int nModulus = (dp8531Regs_[3] << 12) | (dp8531Regs_[2] << 8)
                       | (dp8531Regs_[1] << 4)  |  dp8531Regs_[0];
    int a = (nModulus & 0x1F) ^ 0x1F;            // inverse of the low 5 bits
    int b = (nModulus & 0xFFE0) >> 5;            // the top 11 bits
    a = a < b ? a : b;
    b = b > 2 ? b : 2;

    const int n = (32 * (b - a)) + (31 * (1 + a));
    const int64_t vco = int64_t(20000000 / r) * n;   // int64: MAME's int
    pixelClock_ = uint32_t(vco / p);                 // overflows on junk input
}

// Swatch frame clock: derived from the programmed CRTC totals and the
// Gazelle pixel clock once the ROM has set them (640×480 Hi-Res =
// 896 × 525 at the driver's programmed pclk); the 60 Hz / 525-line
// legacy shape covers pre-init. The VBL timer fires at line 480, the
// cursor timer at the programmed line, each frame, when enabled by
// Swatch $104 (dafb.cpp vbl_tick/cursor_tick — MAME hardcodes line 480
// for VBL too).
void Dafb::tick(int cpuCycles) {
    framePos_ += cpuCycles;
    int64_t frameLen = cpuHz_ / 60;
    int totalLines = 525;
    // dafb.cpp:869-875 — MAME reconfigures the screen whenever recalc_mode
    // leaves a nonzero active area; there is no 480-line floor. (An old
    // `vtotal_ > 480` guard here silently pinned sub-480 modes — the
    // Q700's 512×384, vtotal 407 — to the legacy 60 Hz / 525-line shape.)
    if (htotal_ && vtotal_ && hres_ && vres_ && pixelClock_ >= 1000000) {
        int64_t programmed = int64_t(htotal_) * vtotal_ * cpuHz_ / pixelClock_;
        if (programmed > 0) {
            frameLen = programmed;
            totalLines = int(vtotal_);
        }
    }
    // Published for the raster beam (VideoBeam.h) — one geometry, derived
    // here, rather than a second copy of the same arithmetic elsewhere.
    frameLen_ = frameLen;
    totalLines_ = totalLines;
    if (framePos_ >= frameLen) {
        framePos_ -= frameLen;
        // Completed frames: the position alone is modulo, so a decoder
        // sampling once per frame at a fixed phase could not tell a whole
        // frame from no time at all (VideoBeam::setPos).
        frameCount_++;
    }
    int line = int(framePos_ * totalLines / frameLen);
    if (line != prevLine_) {
        bool wrap = line < prevLine_;
        auto crossed = [&](int target) {
            return wrap ? (target > prevLine_ || target <= line)
                        : (target > prevLine_ && target <= line);
        };
        uint8_t st = intStatus_;
        // MAME hardcodes the VBL at screen line 480 (vbl_tick re-arms
        // time_until_pos(480)); screen_device::time_until_pos wraps vpos
        // modulo the frame height, so on a sub-480-line frame the timer
        // still fires once per frame — keep that wrap here.
        if ((swatchIntEnable_ & 1) && crossed(480 % totalLines)) st |= 1;
        if ((swatchIntEnable_ & 4) && crossed(int(cursorLine_ % totalLines)))
            st |= 4;
        prevLine_ = line;
        if (st != intStatus_) { intStatus_ = st; recalcIrq(); }
    }
}

int Dafb::cyclesToNextEvent() const {
    if (frameLen_ <= 0 || totalLines_ <= 0 || !(swatchIntEnable_ & 5))
        return 0x7fffffff;
    int64_t best = 0x7fffffff;
    auto consider = [&](int line) {
        line %= totalLines_;
        int64_t at = (int64_t(line) * frameLen_ + totalLines_ - 1) / totalLines_;
        int64_t d = at - framePos_;
        if (d <= 0) d += frameLen_;
        if (d < best) best = d;
    };
    if (swatchIntEnable_ & 1) consider(480);
    if (swatchIntEnable_ & 4) consider(int(cursorLine_));
    return int(best > 0 ? best : 1);
}
