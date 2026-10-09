// Independently constructed MOOF chunks; physical tracks, not sector mocks.
#include "MoofImage.h"
#include "Iwm.h"
#include "SonyDrive.h"
#include "AssetFingerprint.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>

using Bytes = std::vector<uint8_t>;
static int failures = 0;
static void check(bool ok, const char* description) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", description); failures += !ok;
}
static Bytes fixture(bool flux = false) {
    Bytes b(2048, 0);
    std::memcpy(b.data(), "MOOF\xff\x0a\x0d\x0a", 8);
    std::memcpy(b.data() + 12, "INFO", 4); moof::put32(b, 16, 60);
    b[20] = 1; b[21] = 2; b[23] = 1; b[24] = 16; moof::put16(b, 58, 1);
    std::memcpy(b.data() + 80, "TMAP", 4); moof::put32(b, 84, 160);
    std::fill(b.begin() + 88, b.begin() + 248, 255);
    b[88] = 0; b[90] = 0; // two physical tracks share one encoded source
    std::memcpy(b.data() + 248, "TRKS", 4); moof::put32(b, 252, 1792);
    moof::put16(b, 256, 3); moof::put16(b, 258, 1); moof::put32(b, 260, 13);
    b[1536] = 0xa9; b[1537] = 0xaf; // only the first five bits of the last byte exist
    if (flux) {
        moof::put16(b, 60, 4); moof::put16(b, 62, 1);
        b.resize(2560, 0);
        std::memcpy(b.data() + 2048, "FLUX", 4); moof::put32(b, 2052, 160);
        std::fill(b.begin() + 2056, b.begin() + 2216, 255); b[2056] = 0;
        std::memcpy(b.data() + 2216, "PAD ", 4); moof::put32(b, 2220, 336);
        moof::put32(b, 260, 4);
        b[1536] = 16; b[1537] = 32; b[1538] = 255; b[1539] = 10;
    }
    const size_t end = b.size(); b.resize(end + 20);
    std::memcpy(b.data() + end, "META", 4); moof::put32(b, end + 4, 12);
    std::memcpy(b.data() + end + 8, "title\tprobe\n", 12);
    return b; // CRC zero is explicitly valid in Applesauce's specification
}
static Bytes save(SonyDrive& drive) {
    Bytes bytes; sav::Writer writer(bytes); drive.visit(writer); return bytes;
}
static bool load(SonyDrive& drive, Bytes& bytes) {
    sav::Reader reader(bytes.data(), bytes.size()); drive.visit(reader); return reader.ok();
}
static void putFile(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
#include "FloppyReadNoiseCases.h"
static void nativeIwmWriteCase() {
    const auto path = std::filesystem::temp_directory_path() / "pom68k-iwm-bit-test.moof";
    putFile(path, fixture());
    SonyDrive a, b; Iwm ia, ib;
    check(a.insert(path.string()), "native IWM fixture inserted"); a.setMotor(true);
    ia.reset(); ia.attachDrive(&a, nullptr);
    ia.read(13); ia.write(15, 0x1F); ia.read(14); ia.read(9); ia.write(15, 0xA5);
    const auto before = a.debugFlux(); ia.tick(10);
    Bytes snapshot; { sav::Writer w(snapshot); a.visit(w); ia.visit(w); }
    ib.reset(); ib.attachDrive(&b, nullptr);
    { sav::Reader r(snapshot.data(), snapshot.size()); b.visit(r); ib.visit(r);
      check(r.ok(), "native mid-cell drive/controller snapshot reads"); }
    ia.tick(10); ib.tick(10); ia.read(14); ib.read(14);
    std::vector<int64_t> expected{8 * 2 * FluxPll::kSubCell};
    for (auto t : before) if (t >= 20 * 2 * FluxPll::kSubCell) expected.push_back(t);
    check(a.debugFlux() == expected && a.dirty(),
          "native partial byte alters flux without inventing a valid sector");
    Bytes sa, sb;
    { sav::Writer w(sa); a.visit(w); ia.visit(w); }
    { sav::Writer w(sb); b.visit(w); ib.visit(w); }
    check(sa == sb, "native partial write restores byte-identically in a fresh pair");
    a.setWriteBack(false); b.setWriteBack(false); std::filesystem::remove(path);
}
int main(int argc, char** argv) {
    if (argc > 1) {
        const auto path = testasset::find(argv[1]);
        if (path.empty()) { std::printf("SKIP: needs %s\n", argv[1]); return 0; }
        testasset::report({path});
        SonyDrive original, restored;
        check(original.insert(path) && original.hasNativeTracks(), "complete real MOOF medium inserted");
        if (failures) return 1;
        original.commitFlux(0, 100000, {1000, 17000, 37000}, false);
        const auto changed = original.debugFlux();
        auto full = save(original);
        check(load(restored, full) && save(restored) == full,
              "large full-medium snapshot restores byte-identically into a fresh reader");
        for (int track = 0; track < 80; ++track) {
            for (bool head : {false, true}) {
                original.fluxAngleTicks(head); restored.fluxAngleTicks(head);
                check(original.debugFlux() == restored.debugFlux() &&
                      original.fluxRevTicks() == restored.fluxRevTicks(),
                      "every track/face retains transitions, duration and phase after restore");
            }
            original.commandSwim(0); original.commandSwim(1);
            restored.commandSwim(0); restored.commandSwim(1);
        }
        restored.reset(); restored.fluxAngleTicks(false);
        check(restored.debugFlux() == changed, "rewound modified track survives full-medium restore and reset");
        full.resize(full.size() - 7);
        SonyDrive truncated;
        check(!load(truncated, full), "truncated full-medium state refused");
        return failures ? 1 : 0;
    }
    moof::Image img;
    nativeIwmWriteCase();
    auto bits = fixture();
    check(moof::unpack(bits, img), "CRC-zero, non-byte-aligned bitstream and shared track descriptors");
    const auto original = img.medium.tracks[0];
    check(original.edges == std::vector<int64_t>{moof::ticks(0), moof::ticks(32), moof::ticks(64),
          moof::ticks(112), moof::ticks(128), moof::ticks(160), moof::ticks(192)} &&
          original.revolution == moof::ticks(208), "MSB-first exact bit count, timing and index phase");
    check(moof::unpack(fixture(true), img) && img.medium.tracks[0].edges ==
          std::vector<int64_t>{0, moof::ticks(16), moof::ticks(48)} &&
          img.medium.tracks[0].revolution == moof::ticks(313), "FLUX overrides TMAP and 255 continues an interval");
    auto bad = bits; moof::put32(bad, 8, 1);
    check(!moof::unpack(bad, img), "nonzero CRC mismatch refused");
    moof::put32(bad, 8, moof::crc(std::span<const uint8_t>(bad).subspan(12)));
    check(moof::unpack(bad, img), "valid standard CRC32 accepted");
    for (int variant = 0; variant < 8; ++variant) {
        bad = bits;
        switch (variant) {
            case 0: bad[88] = 160; break;
            case 1: moof::put16(bad, 256, 2); break;
            case 2: moof::put32(bad, 260, 4097); break;
            case 3: bad.pop_back(); break;
            case 4: moof::put32(bad, 252, 0xffffffff); break;
            case 5: bad[21] = 4; break;
            case 6: bad[24] = 0; break;
            case 7: moof::put16(bad, 60, 4); break;
        }
        check(!moof::unpack(bad, img), "malformed map, extent, chunk, geometry or timing refused");
    }
    bad = bits; bad.resize(6656); bad[24] = 255;
    moof::put32(bad, 252, 6400); moof::put16(bad, 258, 10); moof::put32(bad, 260, 40000);
    check(!moof::unpack(bad, img), "oversized revolution refused before spindle arithmetic can overflow");
    const auto path = std::filesystem::temp_directory_path() / "pom68k-native-track-test.moof";
    putFile(path, bits);
    SonyDrive drive;
    check(drive.insert(path.string()) && drive.hasNativeTracks() && drive.image().empty(),
          "native MOOF has no fabricated sector shadow");
    const auto before = drive.debugFlux();
    drive.commandSwim(0); drive.commandSwim(1); drive.fluxAngleTicks(true);
    drive.commandSwim(4); drive.commandSwim(1); drive.fluxAngleTicks(false);
    check(drive.debugFlux() == before, "seek and head selection preserve native tracks and source aliases");
    drive.commitFlux(0, moof::ticks(64), {moof::ticks(8), moof::ticks(24)}, false);
    const auto written = drive.debugFlux();
    check(written != before && drive.dirty(), "invalid sector fields still write physical transitions");
    auto state = save(drive);
    SonyDrive restored;
    check(load(restored, state) && save(restored) == state, "fresh-device snapshot retains the entire medium and write state");
    restored.commandSwim(0); restored.commandSwim(1);
    check(restored.debugFlux() == before, "aliased source tracks become independent after a physical write");
    restored.reset(); restored.fluxAngleTicks(false);
    check(restored.debugFlux() == written, "reset restores head position without rebuilding written tracks");
    restored.setWriteBack(true);
    check(restored.flushToFile(), "native write-back exports MOOF atomically");
    {
        std::ifstream in(path, std::ios::binary);
        Bytes exported((std::istreambuf_iterator<char>(in)), {});
        check(exported.size() > 80 && !std::memcmp(exported.data() + 25, "POM68K native tracks", 20) &&
              std::search(exported.begin(), exported.end(), bits.end() - 20, bits.end()) != exported.end(),
              "export names its actual writer while preserving original META content");
    }
    SonyDrive reopened;
    check(reopened.insert(path.string()) && reopened.debugFlux() == written,
          "reopen preserves physical flux and index phase on the 125 ns grid");
    reopened.commandSwim(0); reopened.commandSwim(1);
    check(reopened.debugFlux() == before, "export preserves untouched tracks as physical data");
    putFile(path, fixture(true));
    reopened.setWriteBack(true);
    check(reopened.insert(path.string()), "mixed bit/flux image insert");
    const auto flux = reopened.debugFlux();
    reopened.commitFlux(moof::ticks(80), moof::ticks(16), {moof::ticks(4)}, true);
    const auto changed = reopened.debugFlux();
    check(reopened.flushToFile() && drive.insert(path.string()) && drive.debugFlux() == changed && changed != flux,
          "flux-track write/export retains phase, including writes in another encoding");
    reopened.commitFlux(0, reopened.fluxRevTicks() * 2, {1, reopened.fluxRevTicks() + 7}, false);
    check(reopened.debugFlux() == std::vector<int64_t>{7}, "multi-revolution write retains the final pass");
    putFile(path, bits); drive.insert(path.string());
    drive.writeNibble(0xa5);
    auto pending = save(drive);
    drive.writeNibble(0xc3); drive.flushWrite(false);
    const auto nibbleWritten = drive.debugFlux();
    SonyDrive pendingRestored;
    check(load(pendingRestored, pending), "snapshot taken during an IWM native write loads");
    pendingRestored.writeNibble(0xc3); pendingRestored.flushWrite(false);
    check(pendingRestored.debugFlux() == nibbleWritten && nibbleWritten != before,
          "IWM native bytes and write origin resume identically after restoration");
    pendingRestored.setWriteBack(true);
    SonyDrive quantized;
    check(pendingRestored.flushToFile() && quantized.insert(path.string()), "off-grid IWM write exports a readable MOOF");
    bool bounded = quantized.debugFlux().size() == nibbleWritten.size();
    for (size_t i = 0; bounded && i < nibbleWritten.size(); ++i)
        bounded = std::abs(quantized.debugFlux()[i] - nibbleWritten[i]) <= moof::ticks(1) / 2 + 1;
    check(bounded, "export quantization is bounded by half the MOOF 125 ns grid");
    bad = bits; bad[22] = 1; putFile(path, bad);
    check(drive.insert(path.string()) && drive.isWriteProtected(), "INFO write protection reaches the physical sensor");
    const auto protectedFlux = drive.debugFlux();
    drive.commitFlux(0, drive.fluxRevTicks(), {1}, false);
    drive.writeNibble(0xff); drive.flushWrite(false);
    check(drive.debugFlux() == protectedFlux && !drive.dirty(), "protected native medium rejects both write paths");
    drive.eject(); check(!drive.hasDisk(), "eject clears every native track");
    // Sector media also keep off-rate writes after seeking away and resetting.
    drive.insertImage(Bytes(SonyDrive::kSize800K));
    drive.commitFlux(0, 100000, {1000, 3000, 9000}, false, 40000);
    const auto offRate = drive.debugFlux();
    drive.commandSwim(0); drive.commandSwim(1); drive.reset();
    check(drive.debugFlux() == offRate, "sector-backed off-rate track survives seek/reset without canonicalization");
    readNoiseCases(path);
    std::filesystem::remove(path);
    return failures ? 1 : 0;
}
