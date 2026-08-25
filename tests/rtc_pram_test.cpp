// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The RTC's battery file (Rtc::loadPram/savePram), and the platforms
// that gained it on 2026-08-06 — the compacts, the Mac II family, the IIfx
// (docs/SIMPLIFICATIONS_REVIEW.md item F1; the Duo's PMU-resident PRAM is
// gated separately in msc_parity_test).
//
// What it pins:
//  * a round trip through the file preserves all 256 XPRAM bytes;
//  * the format is the flat 256 bytes `CentrisMemory` has written since the
//    djMEMC bring-up, so `.pram` files from earlier builds still load;
//  * a missing or short file returns false and leaves the live PRAM alone —
//    the caller keeps the factory image it seeded, never half of one;
//  * seconds are NOT persisted (each machine re-seeds from host wall time);
//  * the three new platforms forward to the same chip, so a file written by
//    one loads in another (they are tagged per machine in main.cpp for that
//    exact reason).
// Exit 0 = pass, 1 = fail.

#include "MacMemory.h"
#include "MacIIMemory.h"
#include "IIfxMemory.h"
#include "RbvMemory.h"
#include "Rtc.h"
#include "Via6522.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// Scratch path: honour the harness' temp dir when it sets one.
std::string tmpFile(const char* leaf) {
    const char* dir = std::getenv("TMPDIR");
    return std::string(dir && *dir ? dir : "/tmp") + "/pom68k_" + leaf;
}

// A recognisable, non-uniform image: byte i = i ^ 0x5A.
void fill(Rtc& r) {
    for (int i = 0; i < 256; i++) r.setXpram(uint8_t(i), uint8_t(i ^ 0x5A));
}
bool matchesFill(const Rtc& r) {
    for (int i = 0; i < 256; i++)
        if (r.xpram(uint8_t(i)) != uint8_t(i ^ 0x5A)) return false;
    return true;
}

}  // namespace

int main() {
    std::printf("rtc_pram_test — battery persistence and discrete-RTC "
                "heartbeat\n");
    const std::string path = tmpFile("rtc_pram_test.pram");
    std::remove(path.c_str());

    // ── 1. A missing file changes nothing ───────────────────────────────
    {
        Rtc r;
        r.reset();
        r.factoryDefaults();
        const uint8_t sig = r.xpram(0x0C);      // 'N' of the 'NuMc' validity
        check(!r.loadPram(path), "missing file: loadPram returns false");
        check(r.xpram(0x0C) == sig, "missing file: the factory image survives");
    }

    // ── 2. Round trip ───────────────────────────────────────────────────
    {
        Rtc w;
        w.reset();
        fill(w);
        w.setSeconds(0x11223344);
        w.savePram(path);

        std::ifstream in(path, std::ios::binary | std::ios::ate);
        check(bool(in), "savePram wrote a file");
        check(in && in.tellg() == 256,
              "the file is exactly 256 bytes (CentrisMemory's format)");

        Rtc r;
        r.reset();
        r.setSeconds(0xA5A5A5A5);
        check(r.loadPram(path), "loadPram accepts it");
        check(matchesFill(r), "all 256 XPRAM bytes came back");
        // Seconds are the machine's business, not the file's: every platform
        // re-seeds from host wall time, so a stale count must not win.
        check(r.seconds() == 0xA5A5A5A5,
              "the clock is untouched (seconds are not in the file)");
    }

    // ── 3. A short file is refused whole, never half-applied ────────────
    {
        std::ofstream(tmpFile("rtc_short.pram"), std::ios::binary | std::ios::trunc)
            .write("\xDE\xAD\xBE\xEF", 4);
        Rtc r;
        r.reset();
        fill(r);
        check(!r.loadPram(tmpFile("rtc_short.pram")),
              "short file: loadPram returns false");
        check(matchesFill(r), "short file: not one byte was applied");
        std::remove(tmpFile("rtc_short.pram").c_str());
    }

    // ── 4. The three platforms forward to the same chip ─────────────────
    // Same file, three machines: the format is one format. (main.cpp tags
    // the path per machine so they do not actually trade XPRAM through a
    // shared boot volume — that is a naming policy, not a format split.)
    {
        MacMemory compact;                       // Plus / SE / SE FDHD / Classic
        MacIIMemory macii;                       // II / IIx / IIcx / SE30
        IIfxMemory iifx;
        compact.rtc().reset(); fill(compact.rtc());
        compact.savePram(path);

        check(macii.loadPram(path), "Mac II family: loadPram forwards to the RTC");
        check(matchesFill(macii.rtc()), "Mac II family: the image arrived intact");
        check(iifx.loadPram(path), "IIfx: loadPram forwards to the RTC");
        check(matchesFill(iifx.rtc()), "IIfx: the image arrived intact");

        // …and the compacts read back what the Mac II wrote.
        for (int i = 0; i < 256; i++) macii.rtc().setXpram(uint8_t(i), uint8_t(i));
        macii.savePram(path);
        check(compact.loadPram(path), "compacts: loadPram forwards to the RTC");
        bool seq = true;
        for (int i = 0; i < 256; i++)
            if (compact.rtc().xpram(uint8_t(i)) != uint8_t(i)) seq = false;
        check(seq, "compacts: read back the Mac II's image byte for byte");
    }

    // ── 5. IIci: the discrete RTC's CKO edge reaches VIA1 CA2 ───────
    // Use a deliberately tiny CPU clock so this is a fast unit test of the
    // divider boundary.  The real 25 MHz value exercises the same arithmetic.
    {
        RbvMemory iici(0x800000, 1000, /*iici=*/true);
        const uint32_t s0 = iici.rtc().seconds();
        iici.tick(999);
        check(iici.rtc().seconds() == s0,
              "IIci: RTC does not advance before the one-second boundary");
        check(!(iici.via1().ifrRaw() & Via6522::CA2),
              "IIci: VIA1 CA2 stays clear before the CKO edge");
        iici.tick(1);
        check(iici.rtc().seconds() == s0 + 1,
              "IIci: RTC advances exactly at the one-second boundary");
        check((iici.via1().ifrRaw() & Via6522::CA2) != 0,
              "IIci: RTC CKO raises VIA1 CA2");
    }

    std::remove(path.c_str());
    std::printf("rtc_pram_test: %s\n", gFails ? "FAIL" : "PASS");
    return gFails ? 1 : 0;
}
