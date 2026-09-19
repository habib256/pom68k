// POM68K — gate `pixel_pin_test`: the pixel-pin mechanism itself
// (tests/PixelPin.h), with no asset and no machine.
//
// The boot etalons judge a screen by luminance ratios — a menu bar mostly
// white, a desktop in a dithered band. What those cannot see is the whole
// point of pinning pixels, and it is what this gate pins down: two screens
// with the SAME ratio and different arrangements must hash differently,
// the masked menu-bar rows must not reach the hash at all, and a screen
// still changing must refuse to be pinned rather than pin a half-drawn
// frame.

#include "PixelPin.h"

#include <cstdio>
#include <cstdint>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

constexpr int kW = 32, kH = 24;

double blackRatio(const std::vector<std::uint32_t>& fb, int fromRow) {
    long black = 0;
    for (int y = fromRow; y < kH; y++)
        for (int x = 0; x < kW; x++)
            if ((fb[std::size_t(y) * kW + x] & 0xFF) < 0x80) black++;
    return double(black) / (double(kW) * (kH - fromRow));
}
} // namespace

int main() {
    const std::uint32_t black = 0x000000, white = 0xFFFFFF;

    // Two frames, same count of black pixels, different arrangement: the
    // left half black, versus every other column black. A ratio cannot
    // tell them apart; the whole judgement of a boot etalon is that ratio.
    std::vector<std::uint32_t> halves(kW * kH, white), stripes(kW * kH, white);
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++) {
            if (x < kW / 2) halves[std::size_t(y) * kW + x] = black;
            if (x % 2 == 0) stripes[std::size_t(y) * kW + x] = black;
        }
    check(blackRatio(halves, 4) == blackRatio(stripes, 4),
          "the two frames have the same black ratio");
    const std::uint64_t hHalves = pixelpin::hashRegion(halves, kW, kH, 4);
    const std::uint64_t hStripes = pixelpin::hashRegion(stripes, kW, kH, 4);
    check(hHalves != hStripes,
          "…and different pins — the ratio's blind spot is covered");

    // The mask is the point of the menu-bar rows: the clock lives there.
    std::vector<std::uint32_t> clockTicked = halves;
    clockTicked[std::size_t(1) * kW + 30] = black;        // inside the mask
    check(pixelpin::hashRegion(clockTicked, kW, kH, 4) == hHalves,
          "a pixel inside the masked rows never reaches the pin");
    std::vector<std::uint32_t> iconMoved = halves;
    iconMoved[std::size_t(10) * kW + 30] = black;         // below the mask
    check(pixelpin::hashRegion(iconMoved, kW, kH, 4) != hHalves,
          "a pixel below them always does");

    // A frame that keeps changing must refuse the pin. This is what keeps
    // a pin from recording whichever half-drawn frame an engine landed on.
    {
        int tick = 0;
        auto moving = [&](std::vector<std::uint32_t>& out) {
            out.assign(kW * kH, white);
            out[std::size_t(5) * kW + (tick % kW)] = black;
        };
        auto advance = [&](long) { tick++; };
        const pixelpin::Result r =
            pixelpin::settleAndHash(moving, advance, kW, kH, 4, 4, 1);
        check(!r.settled, "a screen still moving is not pinned");
        check(!pixelpin::check("pixel_pin_test/moving", r),
              "…and the gate that asked for it fails");
    }
    {
        auto still = [&](std::vector<std::uint32_t>& out) { out = halves; };
        auto advance = [](long) {};
        const pixelpin::Result r =
            pixelpin::settleAndHash(still, advance, kW, kH, 4, 4, 1);
        check(r.settled && r.hash == hHalves && r.captures == 1,
              "a settled screen is pinned on the first comparison");
    }

    // The table: an unknown gate passes loudly (that is how a profile gets
    // its first pin), a known one is compared.
    {
        bool known = true;
        (void)pixelpin::pinned("no_such_gate_in_the_table", known);
        check(!known, "a gate with no row is reported as unpinned");
        bool lcii = false;
        const std::uint64_t v = pixelpin::pinned("lcii_boot_etalon", lcii);
        check(lcii && v == 0x674c4049ab7980f7ull,
              "tools/pixel_pins.tsv is read, and the LC II row is the pinned one");
    }

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("pixel_pin_test OK\n");
    return 0;
}
