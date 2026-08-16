// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── "Is the Finder actually up?", without asking the fixture ──
//
// The boot etalons on the Mac II family used to answer this with a pair of
// dark ratios plus a floor on `scsi().commands`. The ratios are satisfied
// by a stalled Welcome with jailbars (measured 0.22/0.22 against a Finder's
// 0.10/0.50), so the SCSI floor was the term doing the discriminating —
// and it was a proxy for boot progress, calibrated against ONE state of ONE
// disk image.
//
// It broke on 2026-08-15, on a tree with no relevant change: `HD20SC.vhd`
// had its `drVolAtrb` bit 8 SET at some point on 2026-08-14 (it read $0000
// when the fingerprint preamble was written — `DEV.md` § 6). The System
// scavenges a volume it finds dirty and skips it when it finds it clean, so
// the SAME boot to the SAME desktop issues 295 commands off the dirty image
// and 178 off the clean one — a one-bit fixture edit, verified by clearing
// the bit on a copy and re-running (`vramWrites` is identical, 422140, in
// both: the painting does not change, only the mount-time verification).
// The floor of 200 sat between the two, and it sat ABOVE the ~235 of the
// jailbar stall it was there to reject, so it could not simply be re-tuned.
//
// So the two terms below replace it, and neither can be moved by the state
// of a volume:
//
//   curApName()  — the guest's own answer. $910 is `CurApName`, the running
//                  application's name; the Finder writes it when it starts.
//                  Measured on the Mac II boot: garbage until frame 449,
//                  "Finder" from frame 500 to the end of the run.
//   menuBarRun() — the longest run of light pixels ACROSS the menu bar. A
//                  real menu bar is a white strip with a few titles on the
//                  left; measured 636 on the Mac II and IIx (640 wide),
//                  508 on the SE/30 (512 wide), and 1-3 on every pre-Finder
//                  frame of the same boots. A 50 % desktop dither cannot
//                  hold a light run (the same reason `BeyondBoot.h::
//                  lightRun` works), and neither can a jailbar field, whose
//                  runs are the stripe width — that last one is reasoning,
//                  not a measurement: no jailbar dump survives to measure.
//
// Both directions were run against the gate itself, not just the helpers:
// with the volume's boot blocks zeroed, `macii_boot_etalon` reports
// `menu run 5, CurApName "", SCSI commands 12` and FAILS, while the same
// gate passes on HD20SC with bit 8 set (178 commands) AND with it cleared
// (292) — which is the whole point of the change.
//
// `scsi().commands` stays PRINTED by every gate that used to assert on it.
// It is a good first thing to read when a boot gate goes red — it is just
// not a boot criterion, and 178-vs-295 is the receipt.

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace findersig {

// $910 CurApName, a Str31: the name of the application currently running —
// which under MultiFinder is the FRONTMOST one, so one read answers two
// different questions. "Has the Finder started?" is the boot etalons' use
// above. "Is the Finder the application my keyboard gesture will reach?" is
// the beyond-boot persist legs' use, and it is not the same question: the
// Duo's 7.5.5 volume boots to a perfectly good Finder desktop with
// **Stickies** frontmost (Startup Items), which ate a whole Cmd-N —
// `BeyondBoot.h::persist`, `CHANGELOG.md` 2026-08-15.
//
// `read8` returns the byte at a guest address, or a NEGATIVE value when it
// cannot be read. That is not defensive padding: the machines that run
// their System behind the PMMU need a page-table walk for a logical address
// (`Mmu030Peek.h`) and a walk can fail. A failed read is "no name", never
// a garbage one.
template <class Read8>
inline std::string curApNameAt(Read8 read8) {
    const int n = read8(0x910);
    if (n < 0) return {};
    const int len = n > 31 ? 31 : n;
    std::string s;
    for (int i = 0; i < len; i++) {
        const int c = read8(0x910 + 1 + i);
        if (c < 0x20 || c >= 0x7F) return {};      // not a live Str31
        s += char(c);
    }
    return s;
}

// The PHYSICAL read, and templated on the memory because every *Memory
// class spells `peek8` the same way. Physical is right on the machines that
// use this overload (Mac II / IIx / SE-30 boot System 6 with low RAM at
// physical 0); on a RAM-based-video machine it would need the walk instead,
// see `pom68k-peek-is-physical-rbv` — pass `curApNameAt` a walking reader
// there, the way `duo_beyond_etalon` does.
template <class Mem>
inline std::string curApName(const Mem& mem) {
    return curApNameAt([&](uint32_t a) { return int(mem.peek8(a)); });
}

// Longest horizontal run of light pixels inside the menu-bar band.
inline int menuBarRun(const std::vector<uint32_t>& fb, int W, int H) {
    int best = 0;
    for (int y = 2; y < 20 && y < H; y++) {
        if (fb.size() < size_t(y) * size_t(W) + size_t(W)) break;
        int run = 0;
        for (int x = 0; x < W; x++) {
            if ((fb[size_t(y) * W + x] & 0xFF) >= 0x80) {
                if (++run > best) best = run;
            } else {
                run = 0;
            }
        }
    }
    return best;
}

// Half the screen width. The Finder measures 636/640 (Mac II, IIx) and
// 508/512 (SE/30); the pre-Finder frames of those same boots measure 1-3.
// The gap is two orders of magnitude, so the threshold is placed where it
// says something rather than where it is tight.
inline int menuBarRunFloor(int W) { return W / 2; }

}  // namespace findersig
