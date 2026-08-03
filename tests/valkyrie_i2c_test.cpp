// POM68K — the Valkyrie's I2C clock generator (Quadra 630 / LC 580).
//
// Until 2026-08-02 the Valkyrie's pixel clock was a constant: the Cuda
// programs it over I2C (Valkyrie is slave $28) and POM68K did not model
// that bus, so `LLE_VS_HLE.md` § 1.1 carried it as an open gap. It is a
// real one — a **traced Q630 boot** shows the guest writing four
// transactions to $28 during video bring-up:
//
//     50 00 00      address $28/W, sub-address 0, data 0   (reg 0: ignored)
//     50 02 1B      N = $1B = 27
//     50 01 0E      M = $0E = 14
//     50 03 02      P = $02
//
// so the machine actually asks for 3986400 × 2^2 × 27 / 14 = 30.752 MHz,
// not the 31.3344 MHz reset value.
//
// This gate asserts the arithmetic AND the observable it exists for: the
// pixel clock is what paces `Valkyrie::tick`, so a different clock means a
// different number of CPU cycles per frame — i.e. a different VBL cadence
// for the guest. Asserting on `pixelClock()` alone would only prove the
// setter compiles.
//
// The other half of the path — the Cuda firmware bit-banging the frame,
// `CudaLle::i2cWire` decoding address/sub-address/data — is covered by the
// boot trace above and by `cuda_lle_test`; what is NOT covered anywhere
// else is this device's response, which is why the gate lives here.

#include "Valkyrie.h"

#include <cstdint>
#include <cstdio>
#include <cmath>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

constexpr int64_t kCpuHz = 33000000;      // Quadra 630
constexpr uint32_t kResetClock = 31334400;

// Put the cell in the 640×480 mode the ROM programs (timing number 6,
// htotal 864 / vtotal 525) with the screen enabled, so tick() advances.
void bringUp(Valkyrie& v) {
    v.reset();
    v.writeReg8(0x00, 6);                 // video timing, bit 7 clear = live
    v.writeReg8(0x04, 3);                 // 8 bpp
    v.writeReg8(0x18, 0x80);              // screen enable
}

// CPU cycles the cell takes to complete one whole frame — the observable
// the pixel clock actually moves. Ticks in small quanta so the count is
// not quantised by the step size.
int64_t cyclesPerFrame(Valkyrie& v) {
    const uint64_t start = v.frameCount();
    while (v.frameCount() == start) v.tick(16);      // reach a frame edge
    const uint64_t base = v.frameCount();
    int64_t cycles = 0;
    while (v.frameCount() == base) { v.tick(16); cycles += 16; }
    return cycles;
}
}  // namespace

int main() {
    std::printf("valkyrie_i2c_test — the Cuda-programmed pixel clock\n");

    // ── 1. Reset state, and the frame cadence it produces ───────────────
    Valkyrie v(kCpuHz);
    bringUp(v);
    check(v.pixelClock() == kResetClock, "reset: pixel clock is the 31.3344 MHz default");
    const int64_t resetFrame = cyclesPerFrame(v);
    // 864 × 525 pixels at 31.3344 MHz, expressed in 33 MHz CPU cycles.
    const double wantReset = 864.0 * 525.0 * double(kCpuHz) / double(kResetClock);
    check(std::fabs(double(resetFrame) - wantReset) < 64.0,
          "reset: one frame takes htotal*vtotal*cpuHz/pixelClock cycles");

    // ── 2. The real Q630 boot sequence, byte for byte ───────────────────
    bringUp(v);
    v.i2cWrite(0, 0x00);                  // register 0 — written, ignored
    check(v.pixelClock() == kResetClock, "register 0 is written and ignored");
    v.i2cWrite(2, 0x1B);                  // N
    v.i2cWrite(1, 0x0E);                  // M
    v.i2cWrite(3, 0x02);                  // P
    const uint32_t want = uint32_t(3986400.0 * 4.0 * 27.0 / 14.0);
    check(v.pixelClock() == want, "M/N/P = $0E/$1B/$02 -> 30.752 MHz");
    check(v.pixelClock() != kResetClock, "the programmed clock differs from the default");

    // ── 3. …and the guest's frame cadence moved with it ─────────────────
    const int64_t progFrame = cyclesPerFrame(v);
    const double wantProg = 864.0 * 525.0 * double(kCpuHz) / double(want);
    check(std::fabs(double(progFrame) - wantProg) < 64.0,
          "the frame period follows the programmed clock");
    check(progFrame > resetFrame,
          "a SLOWER pixel clock means a LONGER frame (the gate bites)");
    // Refresh rates, for the record: 69.08 Hz at reset, 67.80 Hz programmed.
    // Apple's nominal 640x480 Mac mode is 66.67 Hz (30.24 MHz dot clock), so
    // MAME's 3986400 reference lands 1.7 % high — see the test header note in
    // CHANGELOG 2026-08-02. Assert the DIRECTION, which is unambiguous.
    const double refReset = double(kResetClock) / (864.0 * 525.0);
    const double refProg = double(want) / (864.0 * 525.0);
    check(std::fabs(refProg - 66.67) < std::fabs(refReset - 66.67),
          "programmed refresh is closer to Apple's nominal 66.67 Hz than the default");

    // ── 4. The 512x384 "garbage program" guard (valkyrie.cpp:566) ───────
    bringUp(v);
    v.i2cWrite(1, 0x00);                  // M = 0 — a bare divide would trap
    v.i2cWrite(2, 0x00);                  // N = 0
    check(v.pixelClock() == 15670000, "M = N = 0 falls back to 15.67 MHz, not a divide by zero");

    // ── 5. Only registers 1-3 are divisors ──────────────────────────────
    bringUp(v);
    v.i2cWrite(2, 0x1B);
    v.i2cWrite(1, 0x0E);
    v.i2cWrite(3, 0x02);
    const uint32_t held = v.pixelClock();
    v.i2cWrite(4, 0xFF);                  // past the register file
    v.i2cWrite(0x7F, 0xFF);
    check(v.pixelClock() == held, "writes outside registers 1-3 leave the clock alone");

    if (gFails) {
        std::printf("valkyrie_i2c_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("valkyrie_i2c_test: OK\n");
    return 0;
}
