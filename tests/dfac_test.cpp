// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Dfac.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
}

int main() {
    std::printf("dfac_test — original LC/LC II/Classic II audio stage\n");
    Dfac dfac;
    dfac.reset();
    check(dfac.process(20000) == 0, "reset state mutes the ASC input");

    dfac.writeSettings(0xE2);                // volume 7, input enabled
    check(dfac.settings() == 0xE2 && dfac.process(20000) == 20000,
          "volume 7 with input enabled is unit gain");
    dfac.writeSettings(0xC2);                // volume 6 = -3 dB
    check(std::abs(int(dfac.process(20000)) - 14159) <= 1,
          "volume 6 applies the documented -3 dB attenuation");
    dfac.writeSettings(0xE0);                // input gate clear
    check(dfac.process(20000) == 0,
          "the low-pass/input-enable bit gates the signal");

    // Egret serial protocol: data is sent least-significant bit first,
    // sampled on clock rises, and committed only on a latch rise.
    dfac.reset();
    constexpr uint8_t kSettings = 0xA2;
    for (int bit = 0; bit < 8; bit++) {
        dfac.dataWrite((kSettings >> bit) & 1);
        dfac.clockWrite(true);
        dfac.clockWrite(false);
    }
    check(dfac.settings() == 0, "shifted settings wait for the latch edge");
    dfac.latchWrite(true);
    check(dfac.settings() == kSettings,
          "eight Egret clock edges latch the original byte bit-for-bit");
    dfac.latchWrite(false);

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
