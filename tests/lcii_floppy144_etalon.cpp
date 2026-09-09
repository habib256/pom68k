// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The 1.44 MB HD floppy MOUNT gate on the reference LC II (V8 + SWIM1).
//
// `lcii_floppy_etalon` covers the 800K GCR medium; this one covers the
// other half of the SuperDrive — MFM media read through the SWIM's ISM
// engine — and it asserts the mount the way the File Manager records it
// rather than by looking at pixels:
//
//   the System's VCB queue (VCBQHdr, low memory $0356) must gain a volume
//   control block whose vcbDrvNum is the internal floppy (1), whose name
//   is the medium's own MDB volume name, and whose allocation geometry
//   (vcbNmAlBlks / vcbAlBlkSiz) equals the MDB's — i.e. the driver read
//   the real sector 2, not a plausible-looking one.
//
// That is deliberately screen-free: the 800K gate's icon-strip judge is
// calibrated on one desktop image, and a mount is a File Manager fact.
// The read-write mount must also clear drAtrb "unmounted cleanly" in sector
// 2, first on the flux-backed medium and then in the host file on eject.
//
// Soft-skips without the LC II ROM, a bootable hdv/ image and a 1.44 MB
// HFS image in disks35/.

#include "AssetFingerprint.h"
#include "V8Memory.h"
#include "SonyDrive.h"
#include "Cpu030.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string find(const char* rel) { return testasset::find(rel); }

// The boot images carry an Apple partition map whose driver-type entry the
// ROM insists on; same normalisation every LC II etalon does.
void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

// Everything on this machine is 24-bit: mask every pointer read out of the
// guest, a Memory Manager tag byte lives in the top one.
uint32_t m24(uint32_t a) { return a & 0xFFFFFF; }

// One mounted volume, as the File Manager holds it (Inside Macintosh IV,
// "Volume Control Block"): the fields this gate compares against the MDB.
struct Vcb {
    uint32_t addr = 0;
    uint16_t sig = 0;
    uint16_t nmAlBlks = 0;
    uint32_t alBlkSiz = 0;
    int16_t drvNum = 0;
    std::string name;
};

} // namespace

int main() {
    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    if (rom.empty()) rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    std::string floppySrc;
    if (const char* o = getenv("POM68K_FLOPPY_IMG")) floppySrc = find(o);
    if (floppySrc.empty()) floppySrc = find("disks35/Stuffit_Expander_5.5.dsk");
    if (rom.empty() || img.empty() || floppySrc.empty()) {
        std::printf("SKIP: needs the 512 KB LC II ROM, a bootable hdv/ image "
                    "and a 1.44 MB HFS image in disks35/\n");
        return 0;
    }
    testasset::report({ rom, img, floppySrc });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }

    // ── The medium: a private copy, normalised to "cleanly unmounted" ──
    // The gate mutates it on purpose (write-back on), so the asset itself
    // must never be the thing that changes. Setting drAtrb bit 8 gives the
    // read-write mount a deterministic write to perform.
    std::vector<uint8_t> floppyOrig;
    {
        std::ifstream fin(floppySrc, std::ios::binary);
        floppyOrig.assign(std::istreambuf_iterator<char>(fin),
                          std::istreambuf_iterator<char>());
    }
    // disks35/Stuffit_Expander_5.5.dsk is 1 474 560 data bytes plus 84 bytes
    // of trailer and no DiskCopy 4.2 header; SonyDrive refuses a size it
    // cannot name, so trim the copy to the medium when the MDB is at $400.
    if (floppyOrig.size() > SonyDrive::kSize1440K &&
        floppyOrig.size() < SonyDrive::kSize1440K + 512 &&
        floppyOrig.size() >= 0x402 &&
        floppyOrig[0x400] == 0x42 && floppyOrig[0x401] == 0x44)
        floppyOrig.resize(SonyDrive::kSize1440K);
    if (floppyOrig.size() != SonyDrive::kSize1440K ||
        floppyOrig[0x400] != 0x42 || floppyOrig[0x401] != 0x44) {
        std::printf("SKIP: %s is not a 1.44 MB HFS medium (%zu bytes)\n",
                    floppySrc.c_str(), floppyOrig.size());
        return 0;
    }
    floppyOrig[0x40A] = uint8_t(floppyOrig[0x40A] | 0x01);   // drAtrb bit 8

    // The MDB is the ground truth every assertion below compares against.
    auto be16 = [&](size_t o) { return uint16_t(floppyOrig[o] << 8 | floppyOrig[o + 1]); };
    auto be32 = [&](size_t o) { return uint32_t(be16(o)) << 16 | be16(o + 2); };
    const uint16_t mdbNmAlBlks = be16(0x400 + 0x12);
    const uint32_t mdbAlBlkSiz = be32(0x400 + 0x14);
    std::string mdbName(reinterpret_cast<const char*>(&floppyOrig[0x400 + 0x25]),
                        floppyOrig[0x400 + 0x24]);
    std::printf("medium: %s — \"%s\", %u blocks of %u bytes, drAtrb $%04X\n",
                floppySrc.c_str(), mdbName.c_str(), mdbNmAlBlks, mdbAlBlkSiz,
                be16(0x400 + 0x0A));

    const char* floppyCopy = "lcii_floppy144.dsk";
    {
        std::ofstream fout(floppyCopy, std::ios::binary | std::ios::trunc);
        fout.write(reinterpret_cast<const char*>(floppyOrig.data()),
                   std::streamsize(floppyOrig.size()));
    }

    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    const int64_t kFrame = 640 * 407;            // 60.15 Hz @ 15.6672 MHz
    auto runFrames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    };
    auto peek16 = [&](uint32_t a) -> uint16_t {
        return uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1));
    };
    auto peek32 = [&](uint32_t a) -> uint32_t {
        return uint32_t(peek16(a)) << 16 | peek16(a + 2);
    };
    // Walk VCBQHdr ($0356: flags, qHead, qTail) — the File Manager's list of
    // mounted volumes. Offsets from Inside Macintosh IV's VCB: sig +8,
    // nmAlBlks +26, alBlkSiz +28, name +44 (Str27), drvNum +72.
    auto vcbs = [&]() {
        std::vector<Vcb> out;
        uint32_t v = m24(peek32(0x356 + 2));
        for (int guard = 0; v && guard < 32; guard++) {
            Vcb e;
            e.addr = v;
            e.sig = peek16(v + 8);
            e.nmAlBlks = peek16(v + 26);
            e.alBlkSiz = peek32(v + 28);
            e.drvNum = int16_t(peek16(v + 72));
            int n = mem.peek8(v + 44);
            if (n > 27) n = 27;
            for (int i = 0; i < n; i++) {
                char c = char(mem.peek8(v + 45 + uint32_t(i)));
                e.name.push_back((c >= 32 && c < 127) ? c : '.');
            }
            out.push_back(e);
            v = m24(peek32(v));
        }
        return out;
    };

    while (mem.cpuHeld()) mem.tick(1000);
    runFrames(16000);                            // boot to a settled Finder
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted during boot\n"); return 1; }
    {
        std::vector<Vcb> boot = vcbs();
        std::printf("boot: %zu volume(s) mounted:", boot.size());
        for (const Vcb& e : boot)
            std::printf(" \"%s\"(drive %d)", e.name.c_str(), e.drvNum);
        std::printf("\n");
        if (boot.empty()) {
            std::fprintf(stderr, "FAIL: no volume mounted after boot — the "
                                 "machine never reached the File Manager\n");
            return 1;
        }
        for (const Vcb& e : boot)
            if (e.drvNum == 1) {
                std::fprintf(stderr, "FAIL: drive 1 already carries \"%s\" "
                                     "before the insert\n", e.name.c_str());
                return 1;
            }
    }

    // ── Insert the HD medium and wait for the File Manager to record it ──
    SonyDrive& drv = mem.internalDrive();
    drv.setWriteBack(true);
    if (!mem.insertDisk(floppyCopy)) {
        std::fprintf(stderr, "FAIL: could not insert %s\n", floppyCopy);
        return 1;
    }
    if (!drv.isHd()) {
        std::fprintf(stderr, "FAIL: the drive did not take the medium as HD\n");
        return 1;
    }
    Vcb mounted;
    long waited = 0;
    for (; waited < 2400 && !mounted.addr; waited += 30) {
        runFrames(30);
        if (cpu.isHalted()) break;
        for (const Vcb& e : vcbs())
            if (e.drvNum == 1) { mounted = e; break; }
    }
    std::printf("floppy: SWIM1 personality=%s mode=$%02X setup=$%02X MFM=%d, "
                "head at track %d, %ld nibbles read\n",
                mem.swim().ism() ? "ISM" : "IWM", mem.swim().ismModeReg(),
                mem.swim().ismSetupReg(), drv.mfmMode() ? 1 : 0,
                drv.currentTrack(), drv.nibblesRead);
    if (!mounted.addr) {
        std::fprintf(stderr, "FAIL: no VCB for drive 1 after %ld frames — the "
                             "1.44 MB volume did not mount\n", waited);
        return 1;
    }
    std::printf("floppy: VCB at $%06X after %ld frames — \"%s\" sig $%04X, "
                "%u blocks of %u bytes\n", mounted.addr, waited,
                mounted.name.c_str(), mounted.sig, mounted.nmAlBlks,
                mounted.alBlkSiz);
    // The VCB is filled from sector 2 BEFORE _MountVol writes the MDB back
    // (the trace order is read 2, write 2, driveInfo, return), so let the
    // rest of the mount run before judging the read-write half.
    runFrames(600);
    Vcb settled;
    for (const Vcb& e : vcbs())
        if (e.drvNum == 1) { settled = e; break; }
    const bool remainedMounted = settled.addr != 0;
    if (remainedMounted) mounted = settled;

    bool ok = true;
    auto check = [&](bool c, const char* what) {
        std::printf("  %s %s\n", c ? "ok  " : "FAIL", what);
        if (!c) ok = false;
    };
    check(remainedMounted, "the floppy VCB remains queued after mount settles");
    check(mounted.sig == 0x4244, "the VCB carries the HFS signature 'BD'");
    check(mounted.name == mdbName, "the volume name is the medium's own");
    check(mounted.nmAlBlks == mdbNmAlBlks,
          "vcbNmAlBlks equals the MDB's drNmAlBlks");
    check(mounted.alBlkSiz == mdbAlBlkSiz,
          "vcbAlBlkSiz equals the MDB's drAlBlkSiz");
    check(drv.hasDisk() && drv.isHd() && drv.mfmMode(),
          "the medium is still an HD MFM disk in the drive");
    // _MountVol's write starts after the old sector-2 data mark, replaces
    // the complete MDB data field, and returns noErr. Require that field to
    // have survived the ISM/TSS → flux → MFM-verifier path.
    { uint8_t sec[512] = {};
      const bool got = drv.readSector(0, 0, 2, sec);
      check(got, "the MFM-written MDB reads back from the medium");
      check(sec[0] == 0x42 && sec[1] == 0x44,
            "the MFM-written MDB keeps its signature");
      check((sec[10] & 0x01) == 0,
            "the MFM-written MDB clears the clean-unmount bit"); }
    std::printf("floppy: nibbles %ld, ISM produced %ld bytes (%ld marks, "
                "%ld syncs), driver popped %ld (%ld empty), %ld error reads\n",
                drv.nibblesRead, mem.swim().ismStats().bytes,
                mem.swim().ismStats().marks, mem.swim().ismStats().syncs,
                mem.swim().ismStats().dataPops, mem.swim().ismStats().emptyPops,
                mem.swim().ismStats().errorReads);
    drv.eject();                                 // flush write-back to the copy
    {
        std::ifstream back(floppyCopy, std::ios::binary);
        std::vector<uint8_t> after((std::istreambuf_iterator<char>(back)),
                                   std::istreambuf_iterator<char>());
        back.close();
        check(after.size() == floppyOrig.size(), "the medium kept its size");
        check(after.size() >= 0x402 && after[0x400] == 0x42 &&
              after[0x401] == 0x44,
              "the MDB signature survived the session");
        check(after.size() >= 0x40C && (after[0x40A] & 0x01) == 0,
              "eject persisted the MFM-written clean-unmount bit");
        for (size_t i = 0; i + 1 < after.size() && i + 1 < floppyOrig.size(); i++)
            if (after[i] != floppyOrig[i]) {
                std::printf("floppy: host file first differs at $%zX "
                            "(sector %zu): $%02X -> $%02X\n", i, i / 512,
                            floppyOrig[i], after[i]);
                break;
            }
    }
    std::remove(floppyCopy);

    std::printf("%s — LC II 1.44 MB floppy mount\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
