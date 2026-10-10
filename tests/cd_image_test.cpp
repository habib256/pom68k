// POM68K — gate `cd_image_test`: CD images as discs (src/CdImage.h).
//
//   • layout — a three-file sheet (MODE1/2048 BINARY, AUDIO BINARY, AUDIO
//     WAVE) with INDEX 00, a first-in-file INDEX 01 past the file's start,
//     PREGAP, POSTGAP and a sub-index: every extent, start, stored end and
//     the lead-out land where the sheet's arithmetic puts them;
//   • samples — every raw sector the disc serves is exact: the BINARY's
//     bytes, the WAVE's data chunk (its partial last sector padded with
//     silence), a MOTOROLA source swapped to CD-DA order, and synthesized
//     gaps as silence; nothing past the lead-out;
//   • refusals — short and missing backing files, WAVE encodings other
//     than 16-bit stereo 44.1 kHz, unknown FILE types, MODE2, FLAGS PRE,
//     PREGAP with INDEX 00, a stored track-1 pregap, two data tracks and
//     malformed timing are each refused with a reason;
//   • flags — FLAGS DCP / PRE / 4CH become the Q control bits READ TOC
//     reports, and a PRE track is de-emphasized (CdDeemphasis.h: unity at
//     DC, 15/50 at Nyquist) on its way to the sink;
//   • the drive — ScsiDisk reads that disc: READ TOC answers the shifted
//     starts and lead-out, and a PLAY AUDIO across a PREGAP into the WAVE
//     hands the sink the same bytes, sector for sector.
//
// No ROM, no image: every source is written here.

#include "CdAudioSink.h"
#include "CdDeemphasis.h"
#include "CdImage.h"
#include "ScsiDisk.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

fs::path root;

void writeBytes(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    std::ofstream(root / name, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
void writeText(const std::string& name, const std::string& text) {
    std::ofstream(root / name) << text;
}
std::string path(const std::string& name) { return (root / name).string(); }

// Sector `n` of a source tagged `tag`: every byte says where it came from.
std::vector<std::uint8_t> sectors(std::uint8_t tag, std::uint32_t count,
                                  std::uint32_t bytes = 2352) {
    std::vector<std::uint8_t> out(std::size_t(count) * bytes);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = std::uint8_t(tag ^ (i / bytes) * 7 ^ (i % 251));
    return out;
}

void le16(std::vector<std::uint8_t>& v, unsigned x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
void le32(std::vector<std::uint8_t>& v, std::uint32_t x) { le16(v, x & 0xFFFF); le16(v, x >> 16); }

// RIFF/WAVE with `pcm` as its data chunk, after a LIST chunk of odd size
// (so the reader must honour RIFF's pad byte).
std::vector<std::uint8_t> wave(const std::vector<std::uint8_t>& pcm, unsigned channels = 2,
                               std::uint32_t rate = 44100, unsigned bits = 16,
                               unsigned format = 1) {
    std::vector<std::uint8_t> body;
    for (char c : std::string("WAVE")) body.push_back(std::uint8_t(c));
    for (char c : std::string("LIST")) body.push_back(std::uint8_t(c));
    le32(body, 3); body.insert(body.end(), {'a', 'b', 'c', 0});
    for (char c : std::string("fmt ")) body.push_back(std::uint8_t(c));
    le32(body, 16); le16(body, format); le16(body, channels); le32(body, rate);
    le32(body, rate * channels * bits / 8); le16(body, channels * bits / 8); le16(body, bits);
    for (char c : std::string("data")) body.push_back(std::uint8_t(c));
    le32(body, std::uint32_t(pcm.size()));
    body.insert(body.end(), pcm.begin(), pcm.end());
    std::vector<std::uint8_t> file = {'R', 'I', 'F', 'F'};
    le32(file, std::uint32_t(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

struct Recorder final : CdAudioSink {
    std::vector<std::vector<std::uint8_t>> sectors;
    void cdAudioSector(const std::uint8_t* raw) override {
        sectors.emplace_back(raw, raw + 2352);
    }
};

std::uint32_t be32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | p[3];
}

} // namespace

int main() {
    root = fs::temp_directory_path() /
        ("pom68k_cd_image_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);

    // ── The disc ──────────────────────────────────────────────────────
    //   data.bin   10 × 2048: track 1, MODE1/2048
    //   music.bin  30 × 2352: track 2 (INDEX 01 at 3, so the file's first
    //              three sectors are its stored pregap; INDEX 02 at 6), then
    //              track 3 (PREGAP 4, INDEX 01 at 12, POSTGAP 5)
    //   song.wav   2.5 sectors of PCM: track 4 (INDEX 00 at 0, INDEX 01 at 1)
    //   swap.bin   4 × 2352, MOTOROLA: track 5 (PREGAP 2)
    const auto data = sectors(0x10, 10, 2048);
    const auto music = sectors(0x20, 30);
    std::vector<std::uint8_t> pcm = sectors(0x40, 3);
    pcm.resize(2352 * 2 + 1176);
    const auto swapped = sectors(0x50, 4);
    writeBytes("data.bin", data);
    writeBytes("music.bin", music);
    writeBytes("song.wav", wave(pcm));
    writeBytes("swap.bin", swapped);
    writeText("disc.cue",
        "REM SESSION 1\n"
        "FILE \"data.bin\" BINARY\n"
        "  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n"
        "FILE \"music.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n    FLAGS DCP\n    INDEX 01 00:00:03\n    INDEX 02 00:00:06\n"
        "  TRACK 03 AUDIO\n    PREGAP 00:00:04\n    INDEX 01 00:00:12\n    POSTGAP 00:00:05\n"
        "FILE \"song.wav\" WAVE\n"
        "  TRACK 04 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:00:01\n"
        "FILE \"swap.bin\" MOTOROLA\n"
        "  TRACK 05 AUDIO\n    FLAGS PRE 4CH\n    PREGAP 00:00:02\n    INDEX 01 00:00:00\n");

    CdImage disc;
    check(disc.open(path("disc.cue")), "the three-encoding sheet opens");
    if (!disc.error().empty()) std::printf("      %s\n", disc.error().c_str());
    const auto& t = disc.tracks();
    // Disc arithmetic: data 0-9; music stored 10-21 (track 2 extent 10,
    // start 13); PREGAP 22-25; music 12-29 → 26-43 (track 3 start 26);
    // POSTGAP 44-48; wave 49-51 (start 50); PREGAP 52-53; swap 54-57.
    check(t.size() == 5, "five tracks");
    if (t.size() == 5) {
        check(t[0].startLba == 0 && t[0].storedEndLba == 10 && !t[0].audio,
              "track 1: the data track, LBA 0-9");
        check(t[1].extentLba == 10 && t[1].startLba == 13 && t[1].storedEndLba == 22,
              "track 2: a first-in-file INDEX 01 keeps the file's leading sectors as its pregap");
        check(t[2].extentLba == 22 && t[2].startLba == 26 && t[2].storedEndLba == 44,
              "track 3: PREGAP inserts silence before INDEX 01, in no file");
        check(t[3].extentLba == 49 && t[3].startLba == 50 && t[3].storedEndLba == 52,
              "track 4: POSTGAP pushes the next file; INDEX 00 bounds the WAVE's pregap");
        check(t[4].extentLba == 52 && t[4].startLba == 54 && t[4].storedEndLba == 58,
              "track 5: MOTOROLA, after its own PREGAP");
    }
    check(disc.leadOut() == 58, "lead-out after the last stored sector");

    std::uint8_t raw[2352];
    auto sectorIs = [&](std::uint32_t lba, const std::vector<std::uint8_t>& src,
                        std::uint32_t index) {
        return disc.readRawSector(lba, raw) &&
               std::memcmp(raw, src.data() + std::size_t(index) * 2352, 2352) == 0;
    };
    auto silent = [&](std::uint32_t lba) {
        if (!disc.readRawSector(lba, raw)) return false;
        for (std::uint8_t byte : raw) if (byte) return false;
        return true;
    };
    bool binary = true;
    for (std::uint32_t lba = 10; lba < 22; ++lba) binary &= sectorIs(lba, music, lba - 10);
    for (std::uint32_t lba = 26; lba < 44; ++lba) binary &= sectorIs(lba, music, lba - 14);
    check(binary, "BINARY audio is served byte for byte across the synthesized gap");
    bool gaps = true;
    for (std::uint32_t lba : {22u, 25u, 44u, 48u, 52u, 53u}) gaps &= silent(lba);
    check(gaps, "PREGAP and POSTGAP sectors are silence");
    std::vector<std::uint8_t> padded = pcm;
    padded.resize(3 * 2352, 0);
    check(sectorIs(49, padded, 0) && sectorIs(50, padded, 1) && sectorIs(51, padded, 2),
          "WAVE: the data chunk exactly, its partial last sector padded with silence");
    bool motorola = true;
    for (std::uint32_t i = 0; i < 4; ++i) {
        disc.readRawSector(54 + i, raw);
        for (std::size_t b = 0; b < 2352; ++b)
            motorola &= raw[b] == swapped[i * 2352 + (b ^ 1)];
    }
    check(motorola, "MOTOROLA samples are swapped to CD-DA byte order");
    check(!disc.readRawSector(58, raw) && !disc.readRawSector(0, raw),
          "nothing past the lead-out, and no raw audio from a 2048-byte data track");
    std::vector<std::uint8_t> user;
    std::uint32_t start = 99, framing = 0;
    check(disc.readData(user, start, framing) && start == 0 && framing == 2048 &&
              user == data,
          "the data track's stored sectors, as framed");

    // ── The drive reads the same disc ─────────────────────────────────
    {
        ScsiDisk drive;
        check(drive.openCdrom(path("disc.cue")) && drive.blocks() == 10 &&
                  drive.trackCount() == 5 && drive.trackStartLba(2) == 26 &&
                  drive.trackStartLba(4) == 54,
              "ScsiDisk mounts the data track and catalogues the shifted starts");
        std::vector<std::uint8_t> out;
        const std::vector<std::uint8_t> in;
        const std::uint8_t toc[10] = {0x43, 0, 0, 0, 0, 0, 0, 0, 100, 0};
        bool tocOk = drive.command(toc, 10, out, in) == 0 && out.size() >= 4 + 6 * 8;
        if (tocOk) {
            const std::uint32_t starts[] = {0, 13, 26, 50, 54};
            for (int i = 0; i < 5; ++i)
                tocOk &= out[4 + 8 * i + 2] == i + 1 && be32(&out[4 + 8 * i + 4]) == starts[i];
            tocOk &= out[4 + 8 * 5 + 2] == 0xAA && be32(&out[4 + 8 * 5 + 4]) == 58;
        }
        check(tocOk, "READ TOC: every start and the lead-out at 58");
        check(out.size() >= 4 + 6 * 8 && out[4 + 1] == 0x14 && out[4 + 8 + 1] == 0x12 &&
                  out[4 + 16 + 1] == 0x10 && out[4 + 32 + 1] == 0x19 && out[4 + 40 + 1] == 0x19,
              "ADR/CTRL: data 0x14, DCP 0x12, plain audio 0x10, PRE+4CH 0x19 (and the lead-out follows it)");
        Recorder sink;
        drive.setCdAudioSink(&sink);
        const std::uint8_t play[10] = {0x45, 0, 0, 0, 0, 47, 0, 0, 11, 0};
        check(drive.command(play, 10, out, in) == 0, "PLAY AUDIO from LBA 47, 11 sectors");
        drive.advanceAudio(1000000);
        bool played = sink.sectors.size() == 11;
        CdDeemphasis expected;
        bool filtered = false;
        for (std::size_t i = 0; played && i < 11; ++i) {
            const std::uint32_t lba = 47 + std::uint32_t(i);
            disc.readRawSector(lba, raw);
            if (lba >= 52) {               // track 5's extent: FLAGS PRE
                std::uint8_t plain[2352];
                std::memcpy(plain, raw, sizeof plain);
                expected.apply(raw);
                filtered |= std::memcmp(plain, raw, sizeof plain) != 0;
            }
            played = std::memcmp(sink.sectors[i].data(), raw, 2352) == 0;
        }
        check(played && filtered,
              "the sink hears POSTGAP, WAVE, PREGAP exactly, and the PRE track de-emphasized");
    }

    // ── De-emphasis: the curve's two ends ─────────────────────────────
    {
        auto run = [](std::int16_t a, std::int16_t b, int sectorsCount) {
            CdDeemphasis filter;
            std::uint8_t sector[2352];
            std::int16_t last = 0;
            for (int n = 0; n < sectorsCount; ++n) {
                for (int i = 0; i < 2352; i += 4) {
                    const std::int16_t v = ((i / 4) & 1) ? b : a;
                    for (int c = 0; c < 4; c += 2) {
                        sector[i + c] = std::uint8_t(std::uint16_t(v));
                        sector[i + c + 1] = std::uint8_t(std::uint16_t(v) >> 8);
                    }
                }
                filter.apply(sector);
                last = std::int16_t(std::uint16_t(sector[2348] | sector[2349] << 8));
            }
            return last;
        };
        const std::int16_t dc = run(10000, 10000, 4);
        const std::int16_t nyquist = run(10000, -10000, 4);
        check(dc >= 9999 && dc <= 10001, "de-emphasis passes DC at unity");
        check(nyquist <= -2990 && nyquist >= -3010,
              "and attenuates Nyquist to 15/50 (−10.46 dB)");
    }

    // ── Refusals, each with its reason ────────────────────────────────
    writeBytes("short.bin", std::vector<std::uint8_t>(2352 * 2 + 100, 0));
    writeBytes("mono.wav", wave(std::vector<std::uint8_t>(2352, 0), 1));
    writeBytes("eight.wav", wave(std::vector<std::uint8_t>(2352, 0), 2, 44100, 8));
    writeBytes("dat.wav", wave(std::vector<std::uint8_t>(2352, 0), 2, 48000));
    writeBytes("float.wav", wave(std::vector<std::uint8_t>(2352, 0), 2, 44100, 16, 3));
    {
        auto cut = wave(std::vector<std::uint8_t>(2352 * 2, 0));
        cut.resize(cut.size() - 100);
        writeBytes("cut.wav", cut);
    }
    writeBytes("notriff.wav", std::vector<std::uint8_t>(2352, 0));
    struct Bad { const char* sheet; const char* reason; const char* what; };
    const Bad bad[] = {
        {"FILE \"short.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "not a whole number", "a backing file that ends mid-sector"},
        {"FILE \"absent.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "missing source", "a missing backing file"},
        {"FILE \"cut.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "runs past the file", "a truncated WAVE data chunk"},
        {"FILE \"notriff.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "not a RIFF/WAVE", "a WAVE that is not RIFF"},
        {"FILE \"mono.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "unsupported WAVE encoding", "mono WAVE"},
        {"FILE \"eight.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "unsupported WAVE encoding", "8-bit WAVE"},
        {"FILE \"dat.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "unsupported WAVE encoding", "48 kHz WAVE"},
        {"FILE \"float.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "unsupported WAVE encoding", "floating-point WAVE"},
        {"FILE \"song.mp3\" MP3\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
         "unsupported FILE type", "an MP3 source"},
        {"FILE \"song.wav\" WAVE\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n",
         "BINARY file", "a data track in a WAVE"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n",
         "unsupported track type", "MODE2"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n FLAGS XYZ\n INDEX 01 00:00:00\n",
         "unknown FLAGS XYZ", "an unknown flag"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n FLAGS PRE\n",
         "before its INDEX 01", "FLAGS after the track's INDEX 01"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n"
         " TRACK 02 AUDIO\n PREGAP 00:00:02\n INDEX 00 00:00:05\n INDEX 01 00:00:06\n",
         "not both", "PREGAP together with INDEX 00"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 00 00:00:00\n INDEX 01 00:00:03\n",
         "track-1 pregap", "a stored track-1 pregap"},
        {"FILE \"data.bin\" BINARY\n TRACK 01 MODE1/2048\n INDEX 01 00:00:00\n"
         " TRACK 02 MODE1/2048\n INDEX 01 00:00:05\n",
         "more than one data track", "two data tracks"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n"
         " TRACK 02 AUDIO\n INDEX 01 00:00:40\n",
         "past the end", "an INDEX past its file"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n INDEX 02 00:00:00\n",
         "indexes must rise", "a sub-index that does not advance"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n POSTGAP 00:61:00\n",
         "POSTGAP needs", "malformed gap timing"},
        {"FILE \"music.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\nREM SESSION 2\n",
         "single-session", "a second session"},
    };
    for (const Bad& b : bad) {
        writeText("bad.cue", b.sheet);
        CdImage refused;
        const bool ok = !refused.open(path("bad.cue")) &&
            refused.error().find(b.reason) != std::string::npos &&
            refused.tracks().empty() && refused.leadOut() == 0;
        const std::string what = std::string(b.what) + " is refused with its reason";
        check(ok, what.c_str());
        if (!ok) std::printf("      got: %s\n", refused.error().c_str());
    }
    {
        writeText("bad.cue", bad[0].sheet);
        ScsiDisk drive;
        check(drive.openCdrom(path("disc.cue")) && !drive.openCdrom(path("bad.cue")) &&
                  drive.trackCount() == 0 && !drive.discLoaded(),
              "a refused sheet leaves the drive empty, not holding the previous disc");
    }

    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
