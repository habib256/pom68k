// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#include "DartImage.h"
#include "SonyDrive.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

static int failures = 0;
static void check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", name);
    failures += !ok;
}
static std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void save(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
static void put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
    bytes[at] = uint8_t(value >> 8); bytes[at + 1] = uint8_t(value);
}
static std::vector<uint8_t> header(int kib, uint8_t compression) {
    std::vector<uint8_t> bytes(kib == 1440 ? 148 : 84, 0);
    bytes[0] = compression; bytes[1] = kib == 1440 ? 16 : 1;
    put16(bytes, 2, uint16_t(kib));
    return bytes;
}
static std::vector<uint8_t> expected(const std::string& name) {
    std::vector<uint8_t> bytes(20960);
    uint32_t random = 0x12345678;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (name == "pattern") bytes[i] = uint8_t(i < 20480 ?
            i / 512 * 17 + i % 512 * 13 : (i - 20480) / 12 * 7 + (i - 20480) % 12 + 1);
        else if (name == "literal") {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            bytes[i] = uint8_t(random);
        }
    }
    return bytes;
}
static bool physicalTagMatch(SonyDrive& a, SonyDrive& b) {
    a.setMotor(true); b.setMotor(true);
    for (int i = 0; i < 24000; ++i)
        if (a.nextNibble(false) != b.nextNibble(false)) return false;
    return true;
}
int main() {
    const auto fixtures = std::filesystem::path(__FILE__).parent_path() / "fixtures/dart";
    for (const std::string name : {"zero", "pattern", "literal"}) {
        const auto compressed = read(fixtures / (name + ".lzh"));
        const auto plain = expected(name);
        std::array<uint8_t, 20960> block{};
        check(!compressed.empty() && DartLzh(compressed).expand(block) &&
              std::equal(block.begin(), block.end(), plain.begin()),
              "independently encoded LZH block expands byte for byte");
        auto archive = header(400, 1);
        for (int i = 0; i < 20; ++i) {
            put16(archive, 4 + i * 2, uint16_t(compressed.size()));
            archive.insert(archive.end(), compressed.begin(), compressed.end());
        }
        std::vector<uint8_t> tags;
        check(dart::unpack(archive, tags) && archive.size() == 409600 && tags.size() == 9600,
              "LZH archive splits forty-sector chunks into data and physical tags");
        bool same = archive.size() == 409600 && tags.size() == 9600;
        for (int i = 0; same && i < 20; ++i) {
            same = std::equal(plain.begin(), plain.begin() + 20480, archive.begin() + i * 20480) &&
                   std::equal(plain.begin() + 20480, plain.end(), tags.begin() + i * 480);
        }
        check(same, "every decoded chunk keeps its independent data and tag ordering");
        auto truncated = compressed;
        if (!truncated.empty()) truncated.resize(truncated.size() / 2);
        check(!DartLzh(truncated).expand(block), "truncated LZH input cannot synthesize missing bits");
    }
    for (const int kib : {400, 800, 1440}) {
        std::vector<uint8_t> data(size_t(kib) * 1024), tags(kib == 1440 ? 0 : data.size() / 512 * 12);
        for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i / 512 * 17 + i % 512);
        for (size_t i = 0; i < tags.size(); ++i) tags[i] = uint8_t(i * 7 + 1);
        std::ostringstream out(std::ios::binary);
        check(dart::write(out, data, tags), "write a DART fast-mode archive for real Mac geometry");
        const auto text = out.str();
        std::vector<uint8_t> archive(text.begin(), text.end()), imported = archive, importedTags;
        check(dart::unpack(imported, importedTags) && imported == data && importedTags == tags,
              "stored DART chunks preserve all data and GCR tags; MFM has no tags");
        auto uncompressed = archive;
        uncompressed[0] = 2;
        for (int i = 0; i < kib / 20; ++i) put16(uncompressed, 4 + i * 2, 20960);
        check(dart::unpack(uncompressed, importedTags) && uncompressed == data && importedTags == tags,
              "legacy uncompressed chunks use byte lengths, not RLE word counts");
        const std::string path = "dart_image_tmp.dart";
        save(path, archive);
        SonyDrive drive;
        drive.setSuperDrive(kib == 1440);
        check(drive.insert(path), "Sony mechanism accepts the decoded Macintosh medium");
        uint8_t sector[512];
        check(drive.readSector(79, kib == 400 ? 0 : 1, kib == 1440 ? 17 : 7, sector) &&
              sector[0] == data[data.size() - 512], "last physical sector retains archive ordering");
        if (kib != 1440) {
            std::vector<uint8_t> dc42(84, 0);
            dc42[82] = 1;
            dc42::put32(dc42.data() + 64, uint32_t(data.size()));
            dc42::put32(dc42.data() + 68, uint32_t(tags.size()));
            dc42.insert(dc42.end(), data.begin(), data.end());
            dc42.insert(dc42.end(), tags.begin(), tags.end());
            save("dart_image_tmp.image", dc42);
            SonyDrive reference;
            check(reference.insert("dart_image_tmp.image") && physicalTagMatch(drive, reference),
                  "DART and DC42 drive the identical physical GCR byte stream");
            std::remove("dart_image_tmp.image");
        }
        drive.setWriteBack(true);
        std::fill_n(sector, 512, 0x5a);
        check(drive.writeSector(0, 0, 0, sector), "guest can write an imported DART sector");
        std::vector<uint8_t> state;
        sav::Writer writer(state); writer(drive);
        SonyDrive restored;
        sav::Reader reader(state.data(), state.size()); reader(restored);
        check(reader.ok() && !reader.remaining() && restored.flushToFile(),
              "fresh snapshot restore retains DART write-back provenance");
        imported = read(path);
        check(imported.size() != data.size() && dart::unpack(imported, importedTags) &&
              imported[0] == 0x5a && importedTags == tags,
              "write-back stays DART and retains guest-written data and all tags");
        std::remove(path.c_str());
        auto bad = archive;
        bad.pop_back();
        check(!dart::unpack(bad, importedTags), "truncated stored chunk rejected");
        bad = archive; put16(bad, 4, 0);
        check(!dart::unpack(bad, importedTags), "missing required chunk rejected");
        bad = archive; bad.push_back(0);
        check(!dart::unpack(bad, importedTags), "unaccounted trailing data rejected");
        bad = archive; bad[1] = 2;
        check(!dart::unpack(bad, importedTags), "Lisa/DOS/Apple II media are not relabeled Macintosh");
        if (kib == 400) {
            bad = archive; put16(bad, 44, 0xffff);
            check(!dart::unpack(bad, importedTags), "nonzero unused 400K table entry rejected");
        }
    }
    // Word-oriented RLE: a literal sector prefix followed by repeated data
    // and tag words. Expected content is specified independently here.
    auto rle = header(400, 0);
    for (int i = 0; i < 20; ++i) {
        put16(rle, 4 + i * 2, 7);
        for (uint16_t word : {uint16_t(2), uint16_t(0x1234), uint16_t(0xabcd),
             uint16_t(-10238), uint16_t(0x5678), uint16_t(-240), uint16_t(0x9abc)}) {
            rle.push_back(uint8_t(word >> 8)); rle.push_back(uint8_t(word));
        }
    }
    auto goodRle = rle;
    std::vector<uint8_t> tags;
    check(dart::unpack(goodRle, tags) && goodRle[0] == 0x12 && goodRle[3] == 0xcd &&
          goodRle[20479] == 0x78 && tags[0] == 0x9a && tags[479] == 0xbc,
          "RLE counts words, preserves literal words and separates repeated tag words");
    for (uint16_t count : {uint16_t(0), uint16_t(0x8000), uint16_t(0x7fff), uint16_t(1)}) {
        auto bad = rle; put16(bad, 84, count);
        check(!dart::unpack(bad, tags), "zero, overflowing and incomplete RLE runs rejected");
    }
    // 20960 is a raw byte length only in mode 2. In mode 0 it denotes
    // 20960 encoded words: one literal word per token is valid, albeit large.
    auto expandedRle = header(400, 0);
    for (int i = 0; i < 20; ++i) {
        put16(expandedRle, 4 + i * 2, 20960);
        for (int word = 0; word < 10480; ++word)
            for (uint8_t byte : {uint8_t(0), uint8_t(1), uint8_t(0x12), uint8_t(0x34)})
                expandedRle.push_back(byte);
    }
    check(dart::unpack(expandedRle, tags) && expandedRle.size() == 409600 &&
          expandedRle.front() == 0x12 && expandedRle.back() == 0x34 && tags[0] == 0x12,
          "RLE length 20960 is not misclassified as an uncompressed chunk");
    save("dart_image_bad.dart", std::vector<uint8_t>(409600, 0));
    SonyDrive invalid;
    check(!invalid.insert("dart_image_bad.dart"), "explicit DART suffix cannot fall back to raw media");
    std::remove("dart_image_bad.dart");
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
