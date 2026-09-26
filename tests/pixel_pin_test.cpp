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
#include "PortableEnv.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
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
    auto idle = [] { return 0L; };                 // a disk doing nothing

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
            pixelpin::settleAndHash(moving, advance, idle, kW, kH, 4, 4, 1);
        check(!r.settled, "a screen still moving is not pinned");
        check(!pixelpin::check("pixel_pin_test/moving", r),
              "…and the gate that asked for it fails");
        check(r.changed == 2 && r.y0 == 5 && r.y1 == 5,
              "…naming what moved: two pixels, on row 5");
    }
    // The settle looks where the hash looks. A clock ticking in the masked
    // rows over a still desktop must not keep it from settling — the LC 580
    // on Mac OS 8.1 failed exactly so, with 204 pixels moving at y 3-11.
    {
        int tick = 0;
        auto clock = [&](std::vector<std::uint32_t>& out) {
            out = halves;
            out[std::size_t(1) * kW + (tick % kW)] = black;
        };
        auto advance = [&](long) { tick++; };
        const pixelpin::Result r =
            pixelpin::settleAndHash(clock, advance, idle, kW, kH, 4, 4, 1);
        check(r.settled && r.hash == hHalves,
              "a clock in the masked rows does not keep a still desktop "
              "from settling");
    }
    {
        auto still = [&](std::vector<std::uint32_t>& out) { out = halves; };
        auto advance = [](long) {};
        const pixelpin::Result r =
            pixelpin::settleAndHash(still, advance, idle, kW, kH, 4, 4, 1);
        check(r.settled && r.hash == hHalves && r.captures == 1,
              "a settled screen is pinned on the first comparison");
    }

    // A still screen over a busy disk is not settled. The LC 575's Finder
    // held its bare desktop for 170 frames while it issued 123 SCSI
    // commands; a screen-only settle pinned that half-drawn desktop. Here
    // the screen never changes, the disk works for two spans, then stops:
    // the pin must wait for the quiet span, and say why it waited.
    {
        long ops = 0;
        int span = 0;
        auto still = [&](std::vector<std::uint32_t>& out) { out = halves; };
        auto advance = [&](long) { if (++span <= 2) ops += 40; };
        auto disk = [&] { return ops; };
        const pixelpin::Result r =
            pixelpin::settleAndHash(still, advance, disk, kW, kH, 4, 4, 1);
        check(r.settled && r.captures == 3 && r.hash == hHalves,
              "a still screen settles only once the disk is quiet");
        long ops2 = 0;
        auto busy = [&](long) { ops2 += 40; };
        auto disk2 = [&] { return ops2; };
        const pixelpin::Result stuck =
            pixelpin::settleAndHash(still, busy, disk2, kW, kH, 4, 4, 1);
        check(!stuck.settled && stuck.activity == 40,
              "…and a disk that never stops refuses the pin, naming it busy");
    }

    // Under the agent variant the pin stands aside: no frame is advanced
    // (the variant runs as it did before pins) and the verdict passes.
    {
        setenv("POM68K_TEST_AGENT", "1", 1);
        int advanced = 0;
        auto still = [&](std::vector<std::uint32_t>& out) { out = halves; };
        auto advance = [&](long) { advanced++; };
        const pixelpin::Result r =
            pixelpin::settleAndHash(still, advance, idle, kW, kH, 4, 4, 1);
        check(r.notApplicable && advanced == 0 &&
                  pixelpin::check("pixel_pin_test/agent", r),
              "under the agent, the pin advances nothing and stands aside");
        unsetenv("POM68K_TEST_AGENT");
    }

    // The table: an unknown gate passes loudly (that is how a profile gets
    // its first pin), a known one is compared.
    {
        bool known = true;
        (void)pixelpin::pinned("no_such_gate_in_the_table", known);
        check(!known, "a gate with no row is reported as unpinned");
        bool lcii = false;
        const std::uint64_t v =
            pixelpin::pinned("lcii_boot_etalon@boot.vhd", lcii);
        check(lcii && v == 0x674c4049ab7980f7ull,
              "tools/pixel_pins.tsv is read, and the LC II row is the pinned one");
    }

    // A pin names the machine AND the volume it booted: the first medium
    // the gate reported joins the key, spaces as underscores. The LC III's
    // first pin was taken on its third-choice image; keyed by gate alone,
    // a host holding the first choice would fail a pin about another disk.
    {
        check(pixelpin::keyFor("lc3_boot_etalon") == "lc3_boot_etalon",
              "a gate that reported no medium is keyed by its name alone");
        testasset::reportedMedia().push_back("System 7.5 HD.dsk");
        testasset::reportedMedia().push_back("Infinite HD.dsk");
        check(pixelpin::keyFor("lc3_boot_etalon") ==
                  "lc3_boot_etalon@System_7.5_HD.dsk",
              "…and otherwise by its name and the FIRST volume it reported");
        testasset::reportedMedia().clear();
    }

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("pixel_pin_test OK\n");
    return 0;
}
