// POM68K — the Valkyrie frame clock free-runs (Quadra 630 / LC 580).
//
// MAME parity (apple/valkyrie.cpp): the $14 vblank register answers
// `m_screen->vblank()` (valkyrie.cpp:255-256) and MAME's screen device
// runs from machine start — screen enable ($18) only arms or cancels the
// VBL *timer* (valkyrie.cpp:326-336), and every $10 write re-arms it too
// (valkyrie.cpp:323). Until 2026-08-05 POM68K gated the whole frame
// counter on the $18 enable: $14 stayed frozen at 0 and $10 armed
// nothing, so a guest that waits for a VBL edge before enabling the
// screen wedged forever.
//
// This gate asserts the decoupling:
//   1. $14 oscillates 0↔1 across frames BEFORE any $18 or $10 write;
//   2. no VBL IRQ fires while the machinery is unarmed;
//   3. a $10 write arms it — the IRQ fires at the next display end, and
//      re-fires every frame after an ack (POM68K re-arms at vres each
//      frame, richer than MAME's hard-coded line 480 — kept on purpose);
//   4. a $18 disable cancels the IRQ but NOT the beam: $14 keeps moving.

#include "Valkyrie.h"

#include <cstdint>
#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

constexpr int64_t kCpuHz = 33000000;      // Quadra 630

// Run ~`frames` frame periods in small quanta, sampling $14 and the IRQ
// line. Reports how many 0->1 transitions $14 made and how many times the
// IRQ line rose. Acks (via $10) after each rise only when `ack` is set —
// note the ack itself re-arms the timer, exactly as MAME's $10 write does.
struct Sweep { int vblRises = 0; int irqRises = 0; };
Sweep sweep(Valkyrie& v, bool& irqLevel, int frames, bool ack) {
    Sweep s;
    // One frame in CPU cycles: htotal*vtotal*cpuHz/pixelClock (mode 6:
    // 864 x 525 at the 31.3344 MHz reset clock ≈ 477 731 cycles).
    const int64_t total = int64_t(frames) * 864 * 525 * kCpuHz / 31334400;
    uint8_t prev = v.readReg8(0x14);
    bool prevIrq = irqLevel;
    for (int64_t t = 0; t < total; t += 16) {
        v.tick(16);
        const uint8_t vbl = v.readReg8(0x14);
        if (vbl && !prev) s.vblRises++;
        if (irqLevel && !prevIrq) {
            s.irqRises++;
            if (ack) v.writeReg8(0x10, 0);     // ack (and re-arm)
        }
        prev = vbl;
        prevIrq = irqLevel;
    }
    return s;
}
}  // namespace

int main() {
    std::printf("valkyrie_test — the frame clock free-runs; $10/$18 arm the VBL\n");

    Valkyrie v(kCpuHz);
    bool irq = false;
    v.onIrq = [&](bool level) { irq = level; };
    v.reset();
    v.writeReg8(0x00, 6);                 // timing number 6: 640x480, live
    v.writeReg8(0x04, 3);                 // 8 bpp
    // NO $18 (screen enable) and NO $10 (config/ack) written yet.

    // ── 1. $14 oscillates before $18 is ever written ────────────────────
    Sweep pre = sweep(v, irq, 3, false);
    check(pre.vblRises >= 2, "$14 rises each frame with the screen still disabled");
    check(v.readReg8(0x14) == 0 || pre.vblRises > 0,
          "$14 is a live level, not a stuck value");

    // ── 2. …but no IRQ while the VBL machinery is unarmed ───────────────
    check(pre.irqRises == 0, "no VBL IRQ before $10 or $18 arms the timer");
    check(!irq, "the IRQ line is still low");

    // ── 3. a $10 write arms the VBL (valkyrie.cpp:323) ──────────────────
    v.writeReg8(0x10, 0);                 // ack + arm, screen STILL disabled
    Sweep armed = sweep(v, irq, 3, true);
    check(armed.irqRises >= 2, "VBL IRQ fires every frame once $10 armed it");
    check(armed.vblRises >= 2, "$14 keeps oscillating alongside the IRQ");

    // ── 4. a $18 disable cancels the timer, not the beam ────────────────
    check(!irq, "the ack-on-rise sweep left the IRQ line low");
    v.writeReg8(0x18, 0x00);              // anything but $8x/$02 = cancel
    Sweep off = sweep(v, irq, 3, false);
    check(off.irqRises == 0, "$18 disable stops the VBL IRQ");
    check(off.vblRises >= 2, "…but $14 keeps free-running (MAME screen never stops)");

    // ── 5. a $18 enable arms it again (valkyrie.cpp:326-331) ────────────
    v.writeReg8(0x18, 0x80);
    Sweep on = sweep(v, irq, 3, true);
    check(on.irqRises >= 2, "$18 = $80 re-arms the VBL IRQ");

    // ── 6. The VBL fires at the MODE's vres, not at a hard-coded 480 ────
    // PIN (audit § 2.13, "POM68K plus riche"). MAME's vbl_tick re-arms at
    // screen line 480 unconditionally (valkyrie.cpp:534-540), which on
    // timing number 2 — 512×384, vtotal 407 — wraps to line 73, i.e. in
    // the middle of the visible picture. POM68K raises it as the beam
    // crosses vres, so the interrupt and the $14 blanking register (which
    // read from the same accumulator) can never disagree: at every IRQ
    // rise, $14 must already read 1.
    {
        Valkyrie m(kCpuHz);
        bool mIrq = false;
        m.onIrq = [&](bool level) { mIrq = level; };
        m.reset();
        m.writeReg8(0x00, 2);             // 512x384, htotal 640, vtotal 407
        m.writeReg8(0x04, 3);             // 8 bpp
        m.writeReg8(0x18, 0x80);          // screen enable (arms the VBL)
        int atVres = 0, atMameLine = 0, atWrap = 0;
        bool prevIrq = mIrq;
        const int64_t total = int64_t(3) * 640 * 407 * kCpuHz / 31334400;
        for (int64_t t = 0; t < total; t += 16) {
            m.tick(16);
            if (mIrq && !prevIrq) {
                const int line = m.currentLine();
                if (line == 384) atVres++;
                if (line >= 60 && line <= 90) atMameLine++;   // 480 % 407 = 73
                if (line < 60) atWrap++;                      // frame rollover
                m.writeReg8(0x10, 0);                         // ack + re-arm
            }
            prevIrq = mIrq;
        }
        check(atVres >= 2, "512x384: the VBL rises at vres (384) every frame");
        check(atMameLine == 0,
              "512x384: nothing fires at line 480%407 = 73, mid-picture");
        // Exactly ONE VBL per frame. The cell used to raise a second,
        // spurious one at the frame wrap (line 0) in every mode: tick()'s
        // `|| line < prevLine_` arm re-fired after the vres crossing had
        // already been serviced, so an acking guest saw double the VBL
        // rate. Found and fixed 2026-08-06 (the wrap arm now also requires
        // prevLine_ < vbl, the form Dafb::tick's crossed() already had);
        // q630_boot_etalon + lc580_boot_etalon confirmed the cadence.
        check(atWrap == 0, "512x384: no second VBL at the frame wrap");
    }

    if (gFails) {
        std::printf("valkyrie_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("valkyrie_test: OK\n");
    return 0;
}
