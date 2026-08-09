// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate for the gate preamble (tests/AssetFingerprint.h): the thing that is
// supposed to tell you an image moved has to be right about WHICH image and
// about drVolAtrb, or it makes the 2026-08-06 class of misdiagnosis worse
// rather than better.
//
// Asset-free on purpose — it synthesises both image layouts it must parse:
//   • an Apple Partition Map volume ('ER' + 'PM' entries), what every .vhd is
//   • a bare HFS volume (MDB at offset 1024), what the .dsk floppies are
// SHA-256 is pinned against the FIPS 180-4 vectors plus the three padding
// boundaries (55/56/63 bytes) where a hand-rolled implementation goes wrong.

#include "AssetFingerprint.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int gFails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) gFails++;
}

static std::string sha(const std::string& s) {
    testasset::detail::Sha256 h;
    h.update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    return h.hex();
}

static void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
static void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

// An HFS master directory block with the given name and drAtrb, at `off`.
static void writeMdb(std::vector<uint8_t>& img, size_t off, const char* name,
                     uint16_t atrb) {
    if (img.size() < off + 512) img.resize(off + 512, 0);
    uint8_t* mdb = img.data() + off;
    put16(mdb, 0x4244);                              // drSigWord 'BD'
    put16(mdb + 10, atrb);                           // drAtrb
    size_t n = std::strlen(name);
    mdb[36] = uint8_t(n);                            // drVN, Str27
    std::memcpy(mdb + 37, name, n);
}

static bool writeFile(const std::string& path, const std::vector<uint8_t>& d) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    return bool(f);
}

int main() {
    // ── SHA-256 ─────────────────────────────────────────────────────────
    check(sha("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256(\"\") — FIPS 180-4");
    check(sha("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256(\"abc\") — FIPS 180-4");
    // 55/56/63 bracket the padding block: 56 is where the length no longer
    // fits and a second compression round is required.
    check(sha(std::string(55, 'a')) ==
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
          "sha256 of 55 bytes — padding fits");
    check(sha(std::string(56, 'a')) ==
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
          "sha256 of 56 bytes — padding spills a block");
    check(sha(std::string(63, 'a')) ==
          "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34",
          "sha256 of 63 bytes");
    check(sha(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
          "sha256 of exactly one block");
    check(sha(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "sha256 of 1e6 'a' — multi-block, crosses the 1 MiB read chunk");

    // ── Apple Partition Map volume ('ER' at block 0) ────────────────────
    // Layout copied from a real image: block 0 DDM, partition map from
    // block 1, Apple_HFS starting at block 96, MDB one KiB into it.
    {
        const uint32_t kHfsStart = 96;
        std::vector<uint8_t> img(size_t(kHfsStart + 8) * 512, 0);
        put16(img.data(), 0x4552);                   // sbSig 'ER'
        put16(img.data() + 2, 512);                  // sbBlkSize

        auto entry = [&](int idx, const char* type, uint32_t start) {
            uint8_t* e = img.data() + size_t(idx) * 512;
            put16(e, 0x504D);                        // pmSig 'PM'
            put32(e + 8, start);                     // pmPyPartStart
            std::memcpy(e + 48, type, std::strlen(type));
        };
        entry(1, "Apple_partition_map", 1);
        entry(2, "Apple_Driver43", 64);
        entry(3, "Apple_HFS", kHfsStart);
        writeMdb(img, size_t(kHfsStart) * 512 + 1024, "Mac-8.1-US", 0x0100);

        const std::string path = "asset_fp_apm.img";
        check(writeFile(path, img), "synthetic APM image written");
        testasset::HfsInfo hfs = testasset::probeHfs(path);
        check(hfs.found, "APM: MDB located through the partition map");
        check(hfs.name == "Mac-8.1-US", "APM: drVN read back");
        check(hfs.atrb == 0x0100, "APM: drAtrb read back");
        check(hfs.cleanlyUnmounted(), "APM: bit 8 set reads as cleanly unmounted");
        check(hfs.mdbOffset == size_t(kHfsStart) * 512 + 1024,
              "APM: MDB offset is partition start + 1024");
        check(testasset::fileSize(path) == img.size(), "fileSize matches");
        check(testasset::sha256File(path).size() == 64, "sha256File returns 64 hex");

        // The bit that carries the diagnosis: clear bit 8 and nothing else.
        // drAtrb is big-endian at MDB+10, so bit 8 lives in the HIGH byte.
        img[size_t(kHfsStart) * 512 + 1024 + 10] = 0x00;
        check(writeFile(path, img), "dirty variant written");
        testasset::HfsInfo dirty = testasset::probeHfs(path);
        check(dirty.found && dirty.atrb == 0x0000 && !dirty.cleanlyUnmounted(),
              "APM: bit 8 clear reads as NOT cleanly unmounted");
        std::remove(path.c_str());
    }

    // ── Bare HFS volume (no partition map) ──────────────────────────────
    {
        std::vector<uint8_t> img(4096, 0);
        writeMdb(img, 1024, "Macintosh HD", 0x0100);
        const std::string path = "asset_fp_flat.img";
        check(writeFile(path, img), "synthetic flat HFS image written");
        testasset::HfsInfo hfs = testasset::probeHfs(path);
        check(hfs.found && hfs.name == "Macintosh HD" && hfs.mdbOffset == 1024,
              "flat: MDB found at offset 1024 without a partition map");
        std::remove(path.c_str());
    }

    // A file that is neither must not be reported as a volume — a ROM is the
    // common case, and claiming a drVolAtrb for one would be a false tell.
    {
        std::vector<uint8_t> img(4096, 0x4E);        // looks like 68k code
        const std::string path = "asset_fp_rom.img";
        check(writeFile(path, img), "synthetic non-HFS file written");
        check(!testasset::probeHfs(path).found, "non-HFS file reports no volume");
        std::remove(path.c_str());
    }

    // ── Role deduction (what labels the preamble line) ──────────────────
    check(std::string(testasset::roleOf("roms/mame/macqd605/ff7439ee.bin")) == "rom",
          "roleOf: roms/ wins over the .bin extension");
    check(std::string(testasset::roleOf("hdv/MacOS-8.1-boot.vhd")) == "disk",
          "roleOf: hdv/ image is a disk");
    check(std::string(testasset::roleOf("hdv/System 7.5 HD.dsk")) == "disk",
          "roleOf: a .dsk attached over SCSI is still a disk");
    check(std::string(testasset::roleOf("disks35/Disk605.dsk")) == "floppy",
          "roleOf: disks35/ is the floppy bay");
    check(std::string(testasset::roleOf("hdv/Apeiron_1_0_3.toast")) == "cd",
          "roleOf: .toast is a CD");
    check(std::string(testasset::roleOf("../hdv/GISTPERSO-boot.vhd")) == "disk",
          "roleOf: the ../ search base does not change the role");

    // An absent asset must print, not crash: gates call report() on paths
    // that findAny() may have left empty.
    testasset::report("rom", "");

    std::printf("%s\n", gFails ? "FAILED" : "PASS");
    return gFails ? 1 : 0;
}
