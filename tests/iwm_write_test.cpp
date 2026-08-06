// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// IWM write-engine gate (LLE floppy write, Plus / LC II SWIM1-GCR):
// the write path is proven as the exact inverse of the read path. A data
// field is harvested from a patterned drive's own nibble stream and
// replayed byte-for-byte through the IWM write registers (MAME iwm.cpp
// write mode: q7-while-enabled entry, handshake bit 7 ready / bit 6
// underrun, async byte cadence) into a blank drive; the sector must
// commit with the payload intact. Underrun and write-protect paths are
// pinned too.

#include "Iwm.h"
#include "SonyDrive.h"
#include "Swim1.h"

#include <cstdio>
#include <cstring>
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

// Harvest one complete GCR data field (leading syncs .. DE AA epilogue)
// for `sector` on track 0 side 0 from the drive's read stream.
std::vector<uint8_t> harvestDataField(SonyDrive& drive, int sector) {
    std::vector<uint8_t> nib;
    for (int i = 0; i < 24000; i++) nib.push_back(drive.nextNibble(false));
    // kGcr6[sector] for the sector byte right after D5 AA AD
    static const uint8_t kGcr6Lo[16] = {
        0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
        0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    };
    for (size_t p = 3; p + 720 < nib.size(); p++) {
        if (!(nib[p] == 0xD5 && nib[p + 1] == 0xAA && nib[p + 2] == 0xAD &&
              nib[p + 3] == kGcr6Lo[sector & 0x0F]))
            continue;
        // 3 marks + sector + 703 payload/checksum nibbles + DE AA
        std::vector<uint8_t> field(nib.begin() + long(p),
                                   nib.begin() + long(p) + 4 + 703 + 2);
        return field;
    }
    return {};
}

// IWM register lines (reg = line*2 + set): ENABLE=4, Q6=6, Q7=7.
constexpr int kEnableOn = 9, kEnableOff = 8;
constexpr int kQ6On = 13, kQ6Off = 12;
constexpr int kQ7On = 15, kQ7Off = 14;

// Write one byte the way the Sony driver does: poll the handshake at the
// q6-off address until bit 7 (register empty), then store at q6-on.
bool feedByte(Iwm& iwm, uint8_t v, int budgetCycles = 4096) {
    while (budgetCycles > 0) {
        uint8_t h = iwm.read(kQ6Off);            // control 0x80: handshake
        if (h & 0x80) { iwm.write(kQ6On, v); return true; }
        iwm.tick(8);
        budgetCycles -= 8;
    }
    return false;
}
} // namespace

int main() {
    std::printf("iwm_write_test — IWM write engine + SonyDrive GCR write-back\n");

    const std::vector<uint8_t> pattern = patternedImage();
    constexpr int kSector = 3;

    // ── Harvest the encoded field from a patterned drive ──────────────
    SonyDrive src;
    src.reset();
    check(src.insertImage(pattern), "insert patterned 800K source");
    std::vector<uint8_t> field = harvestDataField(src, kSector);
    check(field.size() == 709, "harvest data field for sector 3");

    // ── Replay it through the IWM write path into a blank drive ───────
    {
        SonyDrive dst;
        dst.reset();
        check(dst.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0)),
              "insert blank 800K target");
        Iwm iwm;
        iwm.reset();
        iwm.attachDrive(&dst, nullptr);
        // Handshake before ENABLE (q7 set, not write mode): MAME whd 0xBF.
        iwm.read(kQ7On);
        check((iwm.read(kQ6Off) & 0xC0) == 0x80, "handshake idle: ready, b6 low");
        iwm.read(kQ7Off);
        iwm.read(kEnableOn);                     // ENABLE (motor via VIA)
        iwm.read(kQ6On);

        // First byte enters write mode on the same access (control 0xc0).
        iwm.write(kQ7On, 0xFF);
        check((iwm.read(kQ6Off) & 0x40) != 0, "write mode: b6 high (no underrun)");
        bool fed = true;
        for (int i = 0; i < 5 && fed; i++) fed = feedByte(iwm, 0xFF);   // syncs
        for (size_t i = 0; i < field.size() && fed; i++)
            fed = feedByte(iwm, field[i]);
        check(fed, "handshake paces the whole field without stalling");
        check((iwm.read(kQ6Off) & 0x40) != 0, "no underrun while fed");
        iwm.tick(256);                           // drain the last byte
        iwm.read(kQ7Off);                        // leave write mode -> flush

        uint8_t got[512];
        check(dst.readSector(0, 0, kSector, got), "read back written sector");
        bool match = true;
        for (int i = 0; i < 512 && match; i++)
            if (got[i] != expectedByte(0, 0, kSector, i)) match = false;
        check(match, "write path is the inverse of the read path");

        uint8_t other[512];
        dst.readSector(0, 0, kSector + 1, other);
        bool untouched = true;
        for (int i = 0; i < 512 && untouched; i++)
            if (other[i] != 0) untouched = false;
        check(untouched, "neighbour sector stays blank");
    }

    // ── Underrun: starving the shifter drops handshake bit 6 ──────────
    {
        SonyDrive dst;
        dst.reset();
        dst.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        Iwm iwm;
        iwm.reset();
        iwm.attachDrive(&dst, nullptr);
        iwm.read(kEnableOn);
        iwm.write(kQ7On, 0xFF);                  // one byte, then silence
        iwm.tick(1024);
        check((iwm.read(kQ6Off) & 0x40) == 0, "starved shifter: underrun (b6 low)");
        // Re-entering write mode re-arms the engine.
        iwm.read(kQ7Off);
        iwm.write(kQ7On, 0xFF);
        check((iwm.read(kQ6Off) & 0x40) != 0, "write re-entry clears underrun");
    }

    // ── Write-protected media never commits ───────────────────────────
    {
        SonyDrive dst;
        dst.reset();
        dst.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        dst.setWriteProtected(true);
        Iwm iwm;
        iwm.reset();
        iwm.attachDrive(&dst, nullptr);
        iwm.read(kEnableOn);
        iwm.write(kQ7On, 0xFF);
        for (int i = 0; i < 5; i++) feedByte(iwm, 0xFF);
        for (uint8_t b : field) feedByte(iwm, b);
        iwm.tick(256);
        iwm.read(kQ7Off);
        uint8_t got[512];
        dst.readSector(0, 0, kSector, got);
        bool untouched = true;
        for (int i = 0; i < 512 && untouched; i++)
            if (got[i] != 0) untouched = false;
        check(untouched, "write-protected image is untouched");
    }

    // ── Classic-IWM command/sense tables (MAME floppy.cpp parity) ─────
    // sense()/command() addr packing is (CA2<<3)|(CA1<<2)|(CA0<<1)|SEL;
    // MAME's reg is (SEL<<3)|(CA2<<2)|(CA1<<1)|CA0. The capability
    // signature f..c (2M, ready, MFM, rd1 — floppy.cpp:3229-3235) reads
    // here as sense(0xF), sense(0xD), sense(0xB), sense(0x9).
    {
        SonyDrive sd;                            // empty SuperDrive
        sd.setSuperDrive(true);
        sd.reset();
        check(sd.sense(0x9), "empty SuperDrive: RDDATA1 idles high (rd1=1)");
        check(sd.sense(0x8), "empty SuperDrive: RDDATA0 idles high");
        check(!sd.sense(0xF), "empty SuperDrive: 2M reads 0 (no media)");
        check(sd.sense(0xB), "empty SuperDrive: resets in MFM mode");
        const int hd20 = (sd.sense(0xF) << 3) | (sd.sense(0xD) << 2)
                       | (sd.sense(0xB) << 1) | sd.sense(0x9);
        check(hd20 != 0b1110, "empty SuperDrive signature is not HD-20");

        check(sd.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0)),
              "insert DD media into SuperDrive");
        check(sd.sense(0xF), "SuperDrive + DD media: 2M reads 1");
        // Disk-change latch (MAME m_dskchg on dskchg_writable drives):
        // INSERT clears it (floppy.cpp:672-673), only eject raises it
        // (:723); the DskchgClear strobe clears it unconditionally
        // (:3377-3379), media present or not.
        check(!sd.sense(0x6), "insert clears disk-switched (master :672)");
        check(!sd.mfmMode(), "DD insert derives GCR mode");
        sd.eject();
        check(sd.sense(0x6), "eject raises disk-switched");
        // DskchgClear = CA2=1,(CA1,CA0,SEL)=001 → addr 0x9 (MAME reg 0xC,
        // floppy.cpp:3377-3379): acks the change WITHOUT entering MFM.
        sd.command(0x9);
        check(!sd.sense(0x6), "DskchgClear clears the latch, even empty");
        check(sd.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0)),
              "re-insert DD media");
        sd.command(0x9);
        check(!sd.mfmMode(), "DskchgClear does not flip MFM on");
        // MFMModeOn = CA2=0,(0,1,1) → addr 0x3 (MAME reg 0x9); GCRModeOn
        // = CA2=1,(0,1,1) → addr 0xB (MAME reg 0xD, floppy.cpp:3369-3386).
        sd.command(0x3);
        check(sd.mfmMode(), "MFMModeOn strobe enters MFM");
        sd.command(0xD);                         // old (wrong) GCR-on slot
        check(sd.mfmMode(), "MAME reg 0xE stays unassigned (no GCR-on)");
        sd.command(0xB);
        check(!sd.mfmMode(), "GCRModeOn strobe returns to GCR");

        SonyDrive dd;                            // plain 800K mechanism
        dd.reset();
        dd.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        check(!dd.sense(0x9), "800K drive: RDDATA1 reads 0 (no MFM)");
        check(dd.sense(0xF), "800K drive: 2M always 1 (mfd51w)");
        dd.command(0x3);
        check(!dd.mfmMode(), "800K drive ignores MFMModeOn");

        SonyDrive hd;                            // SuperDrive + HD media
        hd.setSuperDrive(true);
        hd.reset();
        hd.insertImage(std::vector<uint8_t>(SonyDrive::kSize1440K, 0));
        check(!hd.sense(0xF), "SuperDrive + HD media: 2M reads 0");
        // GCR-on has NO media guard in MAME (floppy.cpp:3382-3386): the
        // strobe switches even on HD media — the GCR framer then reads
        // the MFM cells as garbage, like hardware. The setup-register
        // reflection (setMfmMode) keeps its guard: in MAME a setup write
        // never touches the drive's m_mfm at all.
        hd.command(0xB);
        check(!hd.mfmMode(), "GCRModeOn switches even on HD media (:3382)");
        hd.command(0x3);
        check(hd.mfmMode(), "MFMModeOn returns HD media to MFM");
        hd.setMfmMode(false);
        check(hd.mfmMode(), "setup reflection keeps the HD guard");
    }

    // ── Mechanism-vs-media senses + tach gate (floppy.cpp parity) ─────
    {
        SonyDrive ss;                            // 400K media, 800K mechanism
        ss.reset();
        check(ss.insertImage(std::vector<uint8_t>(SonyDrive::kSize400K, 0)),
              "insert 400K media");
        check(!ss.doubleSided(), "400K media derives single-sided geometry");
        check(ss.sense(0xC), "SIDES sense is the mechanism, not the media");
        check(ss.senseSwim(0x6), "SWIM DoubleSide sense likewise");

        // TACH runs only with media in and the spindle on (floppy.cpp:
        // 3293-3301); 15000 cycles is an odd tach phase if ungated.
        SonyDrive tach;
        tach.reset();
        tach.setMotor(true);
        tach.tick(15000);
        check(!tach.sense(0x7), "empty drive: classic TACH line low");
        check(!tach.senseSwim(0xB), "empty drive: SWIM tach low too");
    }

    // ── ENABLE (devsel) gating + motor gating through the IWM ─────────
    // MAME iwm.cpp:243-247: sense/commands only reach a SELECTED drive;
    // iwm.cpp:398-405 + floppy.cpp:1175-1178: no flux (nibbles) while the
    // spindle is stopped — Mac drives motor by command, not by ENABLE.
    {
        SonyDrive d;
        d.reset();
        d.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        Iwm iwm;
        iwm.reset();
        iwm.attachDrive(&d, nullptr);
        // Program mode $1F like the ROM: bit 2 set = no motor-off timer,
        // so dropping ENABLE deselects immediately.
        iwm.read(kQ6On);
        iwm.write(kQ7On, 0x1F);
        iwm.read(kQ7Off);
        iwm.setSel(true);                        // sense addr 0x1 = CSTIN
        check(iwm.read(kQ6On) & 0x80,
              "ENABLE off: sense reads high (drive deselected)");
        iwm.read(kEnableOn);
        check(!(iwm.read(kQ6On) & 0x80), "ENABLE on: CSTIN low (disk in)");

        // LSTRB command with ENABLE off is ignored (devsel = none):
        // STEP = CA2=0, addr&7 = 010 → ph0 set, ph1/ph2 clear, SEL low.
        iwm.setSel(false);
        iwm.read(kQ6Off);
        iwm.read(kEnableOff);
        iwm.read(1);                             // ph0 set (CA0 = 1)
        iwm.read(2);                             // ph1 clear
        iwm.read(4);                             // ph2 clear
        iwm.read(7); iwm.read(6);                // LSTRB pulse
        check(d.currentTrack() == 0, "ENABLE off: STEP strobe ignored");
        iwm.read(kEnableOn);
        iwm.read(7); iwm.read(6);
        check(d.currentTrack() == 1, "ENABLE on: STEP strobe steps");

        // Nibble stream needs the spindle, not just ENABLE.
        const long before = d.nibblesRead;
        iwm.tick(4096);
        check(d.nibblesRead == before, "motor off: no nibble stream");
        // MOTORON = CA2=0, addr&7 = 100 → ph1 set, ph0/ph2 clear.
        iwm.read(0);                             // ph0 clear
        iwm.read(3);                             // ph1 set
        iwm.read(7); iwm.read(6);                // strobe
        check(d.motorOn(), "MOTORON strobe reaches the enabled drive");
        iwm.tick(4096);
        check(d.nibblesRead > before, "motor on: nibbles flow");
    }

    // ── SWIM1 IWM→ISM magic counter + ISM write decode ────────────────
    // MAME swim1.cpp:554-581: the 1-0-1-1 bit-6 pattern on offset 0xf is
    // checked on EVERY IWM access; any other access resets the counter,
    // and reads reach the hook with data 0x00. swim1.cpp:269-337: the ISM
    // WRITE decode uses the full offset — 8-15 are ignored, not aliased.
    {
        Swim1 sw;
        SonyDrive d;
        sw.attachDrive(&d, nullptr);
        sw.reset();
        sw.write(15, 0x57);                      // 1
        sw.write(15, 0x17);                      // 0
        sw.write(15, 0x57);                      // 1
        sw.read(0);                              // foreign access: reset
        sw.write(15, 0x57);
        check(!sw.ism(), "interleaved access resets the IWM->ISM counter");
        // ...that last write restarted the pattern (counter = 1); a READ
        // of offset 0xf carries data 0x00 and stands in for the 0 step.
        sw.read(15);
        sw.write(15, 0x57);
        sw.write(15, 0x57);
        check(sw.ism(), "a read of offset 0xf advances the pattern's 0 step");

        check(sw.fifoCount() == 0, "ISM entry: FIFO empty");
        sw.write(8, 0xAA);                       // would alias to data (0)
        check(sw.fifoCount() == 0, "ISM write to offset 8 is ignored");
        sw.write(0, 0xAA);
        check(sw.fifoCount() == 1, "ISM write to offset 0 pushes the FIFO");
        sw.write(6, 0x40);                       // mode-clear bit 6
        check(!sw.ism(), "mode-clear returns to IWM");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
