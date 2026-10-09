// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The VIA1 PA4 internal-connector line (Iwm.h), without a ROM: ENABLE1
// reaches the PA4-low mechanism or the PA4-high one, ENABLE2 the external
// port whatever PA4 says, an empty PA4-high connector answers like no
// drive at all, an unwired board ignores PA4, and the line level survives
// a snapshot into a freshly wired controller. The ROM-level proof is
// se_three_drive_etalon.

#include "SaveState.h"
#include "SonyDrive.h"
#include "Swim1.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// IWM state lines: reg = line*2 + set (Iwm.h).
constexpr int kEnableOn = 9, kExtOff = 10, kExtOn = 11, kQ6On = 13, kQ7Off = 14;

// CSTIN (sense address 1: CA2=CA1=CA0=0, SEL=1) — 0 when a disk is in.
// Returns the status register's sense bit through the ENABLE1/2 choice.
bool cassetteAbsent(Swim1& s, bool external) {
    s.setSel(true);
    s.read(external ? kExtOn : kExtOff);
    s.read(kEnableOn);
    s.read(kQ6On);
    return (s.read(kQ7Off) & 0x80) != 0;
}

void wire(Swim1& s, SonyDrive& low, SonyDrive& ext, bool wired, SonyDrive* high) {
    s.attachDrive(&low, &ext);
    s.wireInternalSelect(wired, high);
}
}  // namespace

int main() {
    std::printf("se_drive_select_test — VIA1 PA4 internal connectors\n");
    const std::vector<uint8_t> blank(SonyDrive::kSize800K, 0);

    SonyDrive low, high, ext;
    low.reset(); high.reset(); ext.reset();
    check(low.insertImage(blank) && ext.insertImage(blank), "insert PA4-low and external media");

    // Dual-floppy SE: the PA4-high mechanism is fitted but empty.
    {
        Swim1 s;
        s.reset();
        wire(s, low, ext, true, &high);
        s.setInternalSelect(false);
        check(!cassetteAbsent(s, false), "PA4 low + ENABLE1: the PA4-low disk");
        s.setInternalSelect(true);
        check(cassetteAbsent(s, false), "PA4 high + ENABLE1: the empty PA4-high mechanism");
        check(!cassetteAbsent(s, true), "PA4 high + ENABLE2: still the external disk");
        check(high.insertImage(blank) && !cassetteAbsent(s, false),
              "inserting into PA4 high is seen on PA4 high");
        high.eject();

        // A snapshot keeps the line level: a fresh controller with the
        // same wiring answers from the same connector.
        std::vector<sav::u8> buf;
        { sav::Writer w(buf); s.visit(w); }
        Swim1 restored;
        restored.reset();
        wire(restored, low, ext, true, &high);
        bool ok = false;
        { sav::Reader r(buf.data(), buf.size()); restored.visit(r); ok = r.ok(); }
        check(ok && restored.iwm().internalSelect(), "restore keeps PA4 high");
        check(cassetteAbsent(restored, false), "restored ENABLE1 still reaches PA4 high");
    }

    // Single-floppy SE and Classic: the PA4-high connector is empty, and
    // an empty connector answers like a missing drive (sense pulled high).
    {
        Swim1 s;
        s.reset();
        wire(s, low, ext, true, nullptr);
        s.setInternalSelect(true);
        check(cassetteAbsent(s, false), "empty PA4-high connector: no cassette, no drive");
        s.setInternalSelect(false);
        check(!cassetteAbsent(s, false), "PA4 low reaches the lone internal mechanism");
    }

    // Boards without the line (Plus, Mac II …): PA4 is not a drive select.
    {
        Swim1 s;
        s.reset();
        wire(s, low, ext, false, &high);
        s.setInternalSelect(true);
        check(!cassetteAbsent(s, false), "unwired board: ENABLE1 ignores PA4");
    }

    std::printf(gFails ? "FAIL: %d checks\n" : "PASS\n", gFails);
    return gFails ? 1 : 0;
}
