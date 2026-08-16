// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// IWM READ-engine gate (§ 1.3 flux plan, step 6 — the last of the plan).
// The Plus/LC II read path is MAME's real cell engine now (iwm.cpp sync(),
// MODE_READ :398-455): a window state machine over SonyDrive's flux store,
// re-centring on every transition, framing a byte when the shifter's MSB
// goes high. It replaced a fixed 128-cycle cadence handing over one
// pre-encoded nibble per slot.
//
// The blocks below pin the three things that cadence could not express:
// the byte stream comes off TRANSITIONS (and still decodes), sync groups
// take ten windows where data bytes take eight, and the head starts where
// rotation left it. The write half lives in iwm_write_test; the end-to-end
// proof is disk_boot_etalon + lcii_floppy_etalon, which is what made this
// step expensive rather than difficult.

#include "Iwm.h"
#include "SonyDrive.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

uint8_t expectedByte(int track, int side, int sector, int i) {
    return uint8_t(track * 7 + side * 31 + sector * 13 + i);
}

std::vector<uint8_t> patternedImage() {
    std::vector<uint8_t> img(SonyDrive::kSize800K);
    size_t off = 0;
    for (int t = 0; t < 80; t++)
        for (int h = 0; h < 2; h++)
            for (int s = 0; s < SonyDrive::sectorsInTrack(t); s++)
                for (int i = 0; i < 512; i++)
                    img[off++] = expectedByte(t, h, s, i);
    return img;
}

// IWM register lines (reg = line*2 + set): ENABLE=4, Q6=6, Q7=7.
constexpr int kEnableOn = 9;
constexpr int kQ6On = 13, kQ6Off = 12;
constexpr int kQ7On = 15, kQ7Off = 14;
constexpr int kData = 0;                         // (q6,q7) = (0,0)

// Bring the chip up the way the Sony driver does, then read nibbles the
// way its denibble loop does: poll the data register, take a byte when the
// MSB is set. `stepCycles` is deliberately small — the loop must not be
// what paces the stream.
void startRead(Iwm& iwm, SonyDrive& drive) {
    iwm.reset();
    iwm.attachDrive(&drive, nullptr);
    // The mode register only latches through (q6,q7) = (1,1) with the drive
    // still disabled — write it the way the ROM does, or the chip keeps its
    // reset mode 0 and reads with a 28-clock window instead of 16.
    iwm.read(kQ6On);
    iwm.write(kQ7On, 0x1F);                      // mode $1F, every Mac's
    iwm.read(kQ6Off);
    iwm.read(kQ7Off);
    iwm.read(kEnableOn);
    drive.setMotor(true);
}

// The data register holds a framed byte for ~14 IWM clocks after the first
// read (Iwm::readRegister), exactly so the ROM's `tst.b` poll and its
// `move.b` consume see the same nibble. A poll loop must therefore take a
// byte ONCE and wait for the register to clear before arming again —
// counting every MSB-set read instead yields four copies of every nibble
// and a stream in which D5 AA 96 never appears.
std::vector<uint8_t> drainNibbles(Iwm& iwm, SonyDrive& drive, size_t want,
                                  int budgetCycles) {
    std::vector<uint8_t> out;
    out.reserve(want);
    bool armed = true;
    while (out.size() < want && budgetCycles > 0) {
        iwm.tick(4);
        drive.tick(4);
        budgetCycles -= 4;
        const uint8_t v = iwm.read(kData);
        if (!(v & 0x80)) { armed = true; continue; }
        if (armed) { out.push_back(v); armed = false; }
    }
    return out;
}

// How many address prologues (D5 AA 96) the stream carries.
int prologues(const std::vector<uint8_t>& n) {
    int c = 0;
    for (size_t i = 0; i + 2 < n.size(); i++)
        if (n[i] == 0xD5 && n[i + 1] == 0xAA && n[i + 2] == 0x96) c++;
    return c;
}

// The sector number of the first address field in the stream, or -1.
int firstSector(const std::vector<uint8_t>& n) {
    static const uint8_t kGcr6[0x40] = {
        0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
        0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
        0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
        0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
        0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
        0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
        0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
        0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
    };
    for (size_t i = 0; i + 4 < n.size(); i++) {
        if (!(n[i] == 0xD5 && n[i + 1] == 0xAA && n[i + 2] == 0x96)) continue;
        for (int v = 0; v < 64; v++)
            if (kGcr6[v] == n[i + 4]) return v;
        return -1;
    }
    return -1;
}
} // namespace

int main() {
    std::printf("iwm_read_test — IWM cell engine over the SonyDrive flux store\n");

    const std::vector<uint8_t> pattern = patternedImage();

    // ── The stream comes off transitions, and it decodes ──────────────
    {
        SonyDrive drive;
        drive.reset();
        check(drive.insertImage(pattern), "insert patterned 800K");
        Iwm iwm;
        startRead(iwm, drive);
        // ~9000 framed bytes make one revolution of a track-0 GCR track, so
        // 12000 covers every one of its 12 sectors with margin.
        const auto nib = drainNibbles(iwm, drive, 12000, 8 * 1000 * 1000);
        check(nib.size() == 12000, "the engine delivers a byte stream at all");
        check(prologues(nib) >= 12,
              "a full revolution of address prologues frames off the flux");
        // Every byte the engine frames has bit 7 set — that IS the framing
        // rule (async mode latches when the shifter's MSB goes high), and
        // it is what makes the GCR alphabet self-synchronising.
        bool msb = true;
        for (uint8_t v : nib) if (!(v & 0x80)) msb = false;
        check(msb, "every framed byte carries the MSB the shifter framed on");
    }

    // ── Rotational position: the head starts where it was left ────────
    // The old fixed cadence walked an encoded byte array from wherever its
    // index happened to be, so a read always began at the same place. A
    // real head does not: enabling the drive a third of a revolution in
    // must land a third of a revolution in.
    {
        SonyDrive early, late;
        early.reset(); late.reset();
        early.insertImage(pattern);
        late.insertImage(pattern);
        Iwm a, b;

        startRead(a, early);
        const auto s0 = drainNibbles(a, early, 800, 2 * 1000 * 1000);

        startRead(b, late);
        // Spin a third of a revolution before the first read, motor already
        // running (394 RPM at track 0, spin clock defaulting to the Plus).
        const int rev = 7833600 * 60 / 394;
        late.tick(rev / 3);
        const auto s1 = drainNibbles(b, late, 800, 2 * 1000 * 1000);

        const int f0 = firstSector(s0), f1 = firstSector(s1);
        check(f0 >= 0 && f1 >= 0, "both reads find an address field");
        check(f0 != f1, "a mid-revolution read lands on a different sector");
    }

    // ── Sync groups take ten windows, data bytes take eight ───────────
    // The self-sync group is $FF plus two zero cells, and the shifter
    // frames it after eight — so it costs ten cell times where a data
    // nibble costs eight. A fixed cadence had one number for both, which
    // is exactly the thing the format uses to keep a real IWM in step.
    {
        SonyDrive drive;
        drive.reset();
        drive.insertImage(pattern);
        Iwm iwm;
        startRead(iwm, drive);
        // Sample the gap between framed bytes in tick() cycles.
        std::vector<int> gaps;
        int since = 0;
        bool armed = true;
        for (int i = 0; i < 2 * 1000 * 1000 && gaps.size() < 600; i += 2) {
            iwm.tick(2);
            drive.tick(2);
            since += 2;
            const uint8_t v = iwm.read(kData);
            if (!(v & 0x80)) { armed = true; continue; }
            if (armed) { gaps.push_back(since); since = 0; armed = false; }
        }
        int wide = 0, narrow = 0;
        for (size_t i = 1; i < gaps.size(); i++) {   // skip the first, partial
            if (gaps[i] >= 150) wide++;
            else if (gaps[i] >= 110 && gaps[i] <= 140) narrow++;
        }
        check(narrow > 100, "data nibbles arrive on the 8-window cadence");
        check(wide > 20, "self-sync groups take their extra two cells");
    }

    // ── The window machine earns its keep: 12% peak-shift jitter ──────
    // Re-centring on every transition is the whole reason this is a state
    // machine and not a counter. Ideal edges never test it; displaced ones
    // do, and GCR 6&2 guarantees a transition at least every three cells
    // for it to re-centre on.
    {
        SonyDrive drive;
        drive.reset();
        drive.insertImage(pattern);
        drive.setFluxJitterPercent(12);
        check(drive.fluxJitterPercent() == 12, "12% read jitter armed");
        Iwm iwm;
        startRead(iwm, drive);
        const auto nib = drainNibbles(iwm, drive, 12000, 8 * 1000 * 1000);
        check(prologues(nib) >= 12,
              "12% jittered flux still frames a full revolution of prologues");
    }

    // ── A stopped spindle delivers nothing ────────────────────────────
    // MAME's get_next_transition returns `never` while the motor is off
    // (floppy.cpp:1175-1178); Mac drives gate the motor by command, not by
    // ENABLE, so an enabled controller over a stopped disk reads no bytes.
    {
        SonyDrive drive;
        drive.reset();
        drive.insertImage(pattern);
        Iwm iwm;
        startRead(iwm, drive);
        drive.setMotor(false);
        const auto nib = drainNibbles(iwm, drive, 4, 200000);
        check(nib.empty(), "a stopped spindle frames no bytes");
    }

    // ── The C15M chip, with the mode a C15M Mac actually writes ───────
    // Every check above runs a bare `Iwm` — clockScale_ 1, mode $1F — which
    // is the Mac PLUS. Every other machine in the tree clocks its IWM (or
    // its SWIM1's IWM personality) at C15M and its ROM writes **$17**: bit
    // 3 is the chip's clock speed and bit 4 its cell time, so the driver
    // picks the pair that suits the clock the board wired up, and both
    // pairs mean one 2 µs Sony GCR cell. MAME agrees — `iwm.cpp:335-361`
    // counts its tables in `clock()`, `mac128.cpp:1182` gives the Plus C7M
    // and `macii.cpp:938` gives the Mac II C15M.
    //
    // Nothing covered that combination, which is how the flux plan's step 6
    // shipped a `windowTicks()` 2× too long on nine platforms and no floppy
    // mounted on any of them for a day (CHANGELOG 2026-08-15 (third)). The
    // gate below is that combination, and it fails on the old arithmetic:
    // a 36-clock window scaled by a C7M-sized unit is 4.6 µs over a 2 µs
    // cell, and NOTHING frames — 0 prologues, not merely fewer.
    {
        SonyDrive drive;
        drive.reset();
        check(drive.insertImage(pattern), "insert patterned 800K (C15M rig)");
        drive.setSpinClockHz(15667200);
        Iwm iwm;
        iwm.setClockHz(15667200);                // clockScale_ = 2
        iwm.reset();
        iwm.attachDrive(&drive, nullptr);
        iwm.read(kQ6On);
        iwm.write(kQ7On, 0x17);                  // the C15M Mac's mode
        iwm.read(kQ6Off);
        iwm.read(kQ7Off);
        iwm.read(kEnableOn);
        drive.setMotor(true);
        // Twice the cycles for the same seconds of rotation.
        const auto nib = drainNibbles(iwm, drive, 12000, 8 * 1000 * 1000);
        check(nib.size() > 8000, "a C15M chip delivers a byte stream at all");
        check(prologues(nib) >= 12,
              "mode $17 on a C15M chip frames a revolution of prologues");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
