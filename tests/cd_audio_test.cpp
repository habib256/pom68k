// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The CD-audio lead ───────────────────────────────────────────────────
// A real AppleCD drive decodes CD-DA itself and puts it on the analog lead
// to the logic board's mixer; the guest starts the play with a SCSI command
// and then hears music the CPU never reads. This gate proves that path end
// to end without a sound device: the drive hands over the sectors the head
// passes (and the RIGHT ones, un-deframed, in order), and CdAudioSource
// turns them into host frames that honour volume and mute.
//
// The disc is synthesized, not found: a flat 2048-byte image cannot carry
// an audio track, and real mixed discs are other people's music.

#include "CdAudioPump.h"
#include "CdAudioSource.h"
#include "ScsiDisk.h"
#include "SaveState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

// Counts sectors and keeps them, so the gate can say WHICH sector arrived
// rather than only how many.
struct RecordingSink : CdAudioSink {
    std::vector<std::vector<uint8_t>> sectors;
    void cdAudioSector(const uint8_t* raw) override {
        sectors.emplace_back(raw, raw + 2352);
    }
};

// Read guest-visible Q position at the physical INDEX 00/01 boundary.
static void checkPregap(const char* path, uint32_t gap, uint32_t start, uint8_t track) {
    ScsiDisk disc;
    check(disc.openCdrom(path), "mount the disc with its stored INDEX 00 sectors");
    RecordingSink sink;
    disc.setCdAudioSink(&sink);
    std::vector<uint8_t> out, in;
    const uint32_t length = start - gap + 1;
    const uint8_t play[10] = { 0x45, 0, uint8_t(gap >> 24), uint8_t(gap >> 16),
        uint8_t(gap >> 8), uint8_t(gap), 0, uint8_t(length >> 8), uint8_t(length), 0 };
    check(disc.command(play, 10, out, in) == 0 && disc.audioLba() == gap,
          "PLAY accepts the physical audio pregap belonging to the next track");
    auto position = [&](ScsiDisk& target, uint32_t lba) {
        const uint8_t index = lba < start ? 0 : 1;
        const uint32_t distance = lba < start ? start - lba : lba - start;
        const uint8_t subMsf[10] = { 0x42, 2, 0x40, 1, 0, 0, 0, 0, 16, 0 };
        check(target.command(subMsf, 10, out, in) == 0 && out.size() == 16 &&
              out[5] == 0x10 && out[6] == track && out[7] == index &&
              out[12] == 0 && out[13] == distance / (60 * 75) &&
              out[14] == (distance / 75) % 60 && out[15] == distance % 75,
              "Q reports the audio track, INDEX 00 countdown or INDEX 01 time");
        const uint8_t subLba[10] = { 0x42, 0, 0x40, 1, 0, 0, 0, 0, 16, 0 };
        check(target.command(subLba, 10, out, in) == 0 && out.size() == 16 &&
              ((uint32_t(out[12]) << 24) | (uint32_t(out[13]) << 16) |
               (uint32_t(out[14]) << 8) | out[15]) == uint32_t(lba - start) &&
              ((uint32_t(out[8]) << 24) | (uint32_t(out[9]) << 16) |
               (uint32_t(out[10]) << 8) | out[11]) == lba,
              "Q relative LBA is negative in INDEX 00; absolute LBA stays continuous");
    };
    position(disc, gap);
    disc.advanceAudio((uint64_t(start - gap - 1) * 1000000 + 74) / 75);
    position(disc, start - 1);
    std::vector<uint8_t> state;
    sav::Writer writer(state);
    writer(disc);
    ScsiDisk restored;
    check(restored.openCdrom(path), "rebind the same CUE before restoring its pregap position");
    sav::Reader reader(state.data(), state.size());
    reader(restored);
    check(reader.ok() && !reader.remaining(), "restore the play head inside INDEX 00");
    position(restored, start - 1);
    disc.advanceAudio(13334);
    restored.advanceAudio(13334);
    position(disc, start);
    position(restored, start);
    check(sink.sectors.size() == start - gap &&
          std::all_of(sink.sectors.begin(), sink.sectors.end(), [](const auto& sector) {
              return std::all_of(sector.begin(), sector.end(), [](auto byte) { return byte == 0; });
          }), "all stored pregap sectors reach the audio lead unchanged");
    disc.advanceAudio(13334);
    check(sink.sectors.size() == length && std::any_of(sink.sectors.back().begin(),
          sink.sectors.back().end(), [](auto byte) { return byte != 0; }),
          "the first INDEX 01 sector follows the stored pause with no skipped sector");
}

int main() {
    std::printf("CD audio: the drive's own lead to the host mixer\n");

    // ── A mixed disc whose audio sectors are identifiable ───────────────
    // Each audio sector carries its own LBA in every sample, so "the wrong
    // sector was handed over" and "the sector was de-framed" are both
    // visible failures rather than silence that looks like success.
    const uint32_t kData = 8, kAudio = 6, kPregap = 150;
    std::vector<uint8_t> bin;
    auto raw = [&](bool audio, uint32_t lba, int16_t sample) {
        std::vector<uint8_t> s(2352, 0);
        if (audio) {
            for (int i = 0; i < 588; i++) {
                s[i * 4]     = uint8_t(sample & 0xFF);
                s[i * 4 + 1] = uint8_t((sample >> 8) & 0xFF);
                s[i * 4 + 2] = uint8_t((-sample) & 0xFF);     // R = −L
                s[i * 4 + 3] = uint8_t(((-sample) >> 8) & 0xFF);
            }
        } else {
            static const uint8_t sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
            std::memcpy(s.data(), sync, 12);
            const uint32_t f = lba + 150;
            s[12] = uint8_t((((f / (60 * 75)) / 10) << 4) | ((f / (60 * 75)) % 10));
            s[13] = uint8_t(((((f / 75) % 60) / 10) << 4) | (((f / 75) % 60) % 10));
            s[14] = uint8_t((((f % 75) / 10) << 4) | ((f % 75) % 10));
            s[15] = 0x01;
        }
        bin.insert(bin.end(), s.begin(), s.end());
    };
    uint32_t lba = 0;
    for (uint32_t i = 0; i < kData; i++, lba++) raw(false, lba, 0);
    for (uint32_t i = 0; i < kPregap; i++, lba++) raw(true, lba, 0);
    const uint32_t audioStart = lba;
    for (uint32_t i = 0; i < kAudio; i++, lba++)
        raw(true, lba, int16_t(0x1000 + i));          // one value per sector
    { std::ofstream f("cd_audio_test.bin", std::ios::binary);
      f.write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size())); }
    const uint32_t am = audioStart;   // cue times are file-relative
    char cue[512];
    std::snprintf(cue, sizeof cue,
        "FILE \"cd_audio_test.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n    INDEX 00 00:00:08\n    INDEX 01 %02u:%02u:%02u\n",
        am / (60 * 75), (am / 75) % 60, am % 75);
    { std::ofstream f("cd_audio_test.cue"); f << cue; }
    checkPregap("cd_audio_test.cue", kData, audioStart, 2);

    ScsiDisk disc;
    check(disc.openCdrom("cd_audio_test.cue"), "the synthesized mixed disc mounts");

    RecordingSink sink;
    disc.setCdAudioSink(&sink);

    std::vector<uint8_t> out, in;
    const uint32_t len = 3;
    const uint8_t play[10] = { 0x45, 0,
        uint8_t(audioStart >> 24), uint8_t(audioStart >> 16),
        uint8_t(audioStart >> 8), uint8_t(audioStart),
        0, uint8_t(len >> 8), uint8_t(len), 0 };
    check(disc.command(play, 10, out, in) == 0, "PLAY AUDIO starts the play");

    // ── The sectors the head passes are the sectors that sound ──────────
    disc.advanceAudio(39999);
    check(sink.sectors.size() == 2, "third sector is not emitted before 40 ms");
    disc.advanceAudio(1);
    check(sink.sectors.size() == 3, "three sectors of machine time deliver three sectors");
    bool ordered = sink.sectors.size() == 3;
    for (size_t i = 0; ordered && i < sink.sectors.size(); i++) {
        const int16_t want = int16_t(0x1000 + i);
        const auto lo = uint8_t(want & 0xFF), hi = uint8_t((want >> 8) & 0xFF);
        if (sink.sectors[i][0] != lo || sink.sectors[i][1] != hi) ordered = false;
    }
    check(ordered, "they arrive in order, each carrying its own track content");
    check(sink.sectors.size() == 3 && sink.sectors[0].size() == 2352,
          "handed over raw — 2352 bytes, not de-framed to 2048");

    const size_t before = sink.sectors.size();
    const uint8_t pause[10] = { 0x4B, 0, 0, 0, 0, 0, 0, 0, 0x00, 0 };
    disc.command(pause, 10, out, in);
    disc.advanceAudio(1000000ull);
    check(sink.sectors.size() == before, "a paused disc puts nothing on the lead");

    // ── The host side: sectors in, resampled frames out ─────────────────
    CdAudioSource source;
    source.setSampleRate(44100);                  // disc rate: 1:1, no drift
    check(source.buffered() == 0, "a source with no disc playing is empty");
    source.cdAudioSector(sink.sectors[0].data());
    check(source.buffered() >= 580 && source.buffered() <= 590,
          "one sector yields one sector of frames (588 at the disc's rate)");

    float mix[64 * 2] = {};
    source.mixStereo(mix, 64);
    const float want = float(0x1000) / 32768.0f;
    bool sounds = true;
    for (int i = 0; i < 64; i++) {
        if (std::fabs(mix[i * 2] - want) > 1e-4f) sounds = false;
        if (std::fabs(mix[i * 2 + 1] + want) > 1e-4f) sounds = false;   // R = −L
    }
    check(sounds, "the mix carries the disc's samples, both channels distinct");

    float second[8 * 2] = {};
    source.setMuted(true);
    source.mixStereo(second, 8);
    bool silent = true;
    for (float f : second) if (f != 0.0f) silent = false;
    check(silent, "mute adds nothing to the host buffer");
    source.setMuted(false);

    float half[8 * 2] = {};
    source.setVolume(0.5f);
    source.mixStereo(half, 8);
    check(std::fabs(half[0] - want * 0.5f) < 1e-4f, "volume scales the lead");

    // Additive, never overwriting: the machine's own samples are already
    // in the buffer when a source is called.
    float mixed[4 * 2];
    for (float& f : mixed) f = 0.25f;
    source.setVolume(1.0f);
    source.mixStereo(mixed, 4);
    check(std::fabs(mixed[0] - (0.25f + want)) < 1e-4f,
          "sources mix additively over the machine's own samples");

    source.reset();
    check(source.buffered() == 0, "reset drops everything in flight");

    // ── MODE SELECT page $0E: the drive's own volume knob ───────────────
    // What the Sound control panel's CD slider and the AppleCD Audio
    // Player's volume actually move. A drive that reports the page and then
    // throws away what is written to it has a volume control that does
    // nothing.
    {
        CdAudioSource guestVol;
        guestVol.setSampleRate(44100);
        disc.setCdAudioSink(&guestVol);
        check(guestVol.guestVolumeLeft() == 255 &&
              guestVol.guestVolumeRight() == 255,
              "a drive nobody has touched plays at full level");

        // Header(4) + page $0E: port 0 → channel 0 at $40, port 1 → channel
        // 1 at $20, ports 2 and 3 muted.
        const uint8_t sel[4 + 16] = {
            0, 0, 0, 0,
            0x0E, 0x0E, 0x04, 0, 0, 0, 0, 0,
            0x01, 0x40, 0x02, 0x20, 0x00, 0xFF, 0x00, 0xFF };
        const uint8_t modeSelect6[6] = { 0x15, 0x10, 0, 0, sizeof sel, 0 };
        std::vector<uint8_t> params(sel, sel + sizeof sel);
        check(disc.command(modeSelect6, 6, out, params) == 0,
              "MODE SELECT (6) with page $0E is accepted");
        check(guestVol.guestVolumeLeft() == 0x40 &&
              guestVol.guestVolumeRight() == 0x20,
              "each output port's level lands on the channel it selects");

        // A port selecting no channel is muted, and the strongest port wins
        // a channel it shares.
        const uint8_t both[4 + 16] = {
            0, 0, 0, 0,
            0x0E, 0x0E, 0x04, 0, 0, 0, 0, 0,
            0x03, 0x10, 0x03, 0x80, 0x00, 0xFF, 0x00, 0xFF };
        std::vector<uint8_t> p2(both, both + sizeof both);
        disc.command(modeSelect6, 6, out, p2);
        check(guestVol.guestVolumeLeft() == 0x80 &&
              guestVol.guestVolumeRight() == 0x80,
              "a port feeding both channels is heard on both, loudest wins");

        // And it reaches the mix, in series with the host user's knob.
        guestVol.cdAudioSector(sink.sectors[0].data());
        float m[4 * 2] = {};
        guestVol.setVolume(1.0f);
        guestVol.mixStereo(m, 4);
        check(std::fabs(m[0] - want * (128.0f / 255.0f)) < 1e-3f,
              "the guest's level scales the host mix");
    }

    // ── The 1 ms grain loses nothing ────────────────────────────────────
    // A board does not look at its transports on every bus access
    // (CdAudioPump.h). The grain must be an optimisation and not a change:
    // the same machine time delivered in bus-sized dribbles through the
    // pump, and in one lump straight to the disc, must land the play head
    // on exactly the same sector.
    {
        const int64_t hz = 25000000;                  // a 25 MHz board
        std::array<ScsiDisk, 1> pumped;
        pumped[0].openCdrom("cd_audio_test.cue");
        ScsiDisk lump;
        lump.openCdrom("cd_audio_test.cue");

        const uint32_t whole = 5;
        const uint8_t playAll[10] = { 0x45, 0,
            uint8_t(audioStart >> 24), uint8_t(audioStart >> 16),
            uint8_t(audioStart >> 8), uint8_t(audioStart),
            0, uint8_t(whole >> 8), uint8_t(whole), 0 };
        pumped[0].command(playAll, 10, out, in);
        lump.command(playAll, 10, out, in);

        CdAudioPump pump;
        int64_t delivered = 0;
        while (delivered + 37 <= hz / 20) {           // 50 ms, 37 cycles at a time
            pump.advance(pumped, 37, hz);
            delivered += 37;
        }
        lump.advanceAudioCycles(delivered, hz);
        check(pumped[0].audioLba() == lump.audioLba(),
              "dribbled cycles and one lump land on the same sector");
        check(lump.audioLba() > audioStart,
              "and the play actually moved, so the comparison means something");
    }

    // ── An audio CD: no user data anywhere on it ────────────────────────
    // The most ordinary CD-DA case, and the one the AppleCD Audio Player
    // exists for. Nothing to mount as a volume — the disc IS its TOC and
    // its tracks — so a drive that judges "is a disc present?" by counting
    // user-data blocks reports an empty tray for every audio CD there is.
    {
        std::vector<uint8_t> abin;
        for (uint32_t i = 0; i < 300; i++) {
            std::vector<uint8_t> sector(2352, 0);
            for (int f = 0; f < 588; f++) sector[f * 4] = uint8_t(i);
            abin.insert(abin.end(), sector.begin(), sector.end());
        }
        { std::ofstream f("cd_audio_only.bin", std::ios::binary);
          f.write(reinterpret_cast<const char*>(abin.data()), std::streamsize(abin.size())); }
        { std::ofstream f("cd_audio_only.cue");
          f << "FILE \"cd_audio_only.bin\" BINARY\n"
               "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
               "  TRACK 02 AUDIO\n    INDEX 01 00:02:00\n"; }

        ScsiDisk only;
        check(only.openCdrom("cd_audio_only.cue"), "an audio-only .cue mounts");
        check(only.present(), "the drive answers selection");
        const uint8_t tur[6] = { 0x00, 0, 0, 0, 0, 0 };
        check(only.command(tur, 6, out, in) == 0,
              "TEST UNIT READY says the disc is there, with no data blocks");
        check(only.blocks() == 0, "and it really has none");

        const uint8_t toc[10] = { 0x43, 0x02, 0, 0, 0, 0, 0, 0, 40, 0 };
        check(only.command(toc, 10, out, in) == 0 && out.size() >= 20,
              "READ TOC returns the audio tracks");
        check(only.trackCount() == 2 && only.trackIsAudio(0) && only.trackIsAudio(1),
              "both tracks are audio");

        const uint8_t cap[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        check(only.command(cap, 10, out, in) == 0 && out.size() == 8,
              "READ CAPACITY answers with the lead-out, not NOT READY");

        const uint8_t read10[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
        check(only.command(read10, 10, out, in) == 2,
              "READ is refused: there is no user data to hand back");

        const uint8_t playFirst[10] = { 0x45, 0, 0, 0, 0, 0, 0, 0, 2, 0 };
        check(only.command(playFirst, 10, out, in) == 0 && only.audioState() == 1,
              "and the first track plays, which is the whole point of the disc");

        // CD-DA is exactly 75 sectors / 44100 stereo frames per second.
        // Rounding the sector period to 13333 µs emits sector 75 too early.
        RecordingSink clockSink;
        only.setCdAudioSink(&clockSink);
        const uint8_t playTwoSeconds[10] = { 0x45, 0, 0, 0, 0, 0, 0, 0, 150, 0 };
        check(only.command(playTwoSeconds, 10, out, in) == 0,
              "start a two-second physical audio extent");
        only.advanceAudio(999999);
        check(clockSink.sectors.size() == 74 && only.audioLba() == 74,
              "sector 75 waits for the complete first second");
        only.command(pause, 10, out, in);
        only.advanceAudio(1000000);
        check(clockSink.sectors.size() == 74,
              "pause preserves the pending sector's fractional deadline");

        std::vector<uint8_t> state;
        sav::Writer writer(state);
        writer(only);
        ScsiDisk restored;
        check(restored.openCdrom("cd_audio_only.cue"),
              "mount the same medium for a fresh transport restore");
        sav::Reader reader(state.data(), state.size());
        reader(restored);
        check(reader.ok() && !reader.remaining() && restored.audioState() == 2,
              "snapshot restores the paused transport and fractional phase");
        RecordingSink restoredSink;
        restored.setCdAudioSink(&restoredSink);
        const uint8_t resume[10] = { 0x4B, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
        only.command(resume, 10, out, in);
        restored.command(resume, 10, out, in);
        only.advanceAudio(1);
        restored.advanceAudio(1);
        check(clockSink.sectors.size() == 75 && restoredSink.sectors.size() == 1 &&
              only.audioLba() == 75 && restored.audioLba() == 75 &&
              restoredSink.sectors[0] == clockSink.sectors.back(),
              "resume and fresh restore emit sector 75 at exactly one second");
        only.advanceAudio(999999);
        restored.advanceAudio(999999);
        check(clockSink.sectors.size() == 149 && restored.audioLba() == 149,
              "the second full-second boundary also remains exact");
        only.advanceAudio(1);
        restored.advanceAudio(1);
        check(clockSink.sectors.size() == 150 && restoredSink.sectors.size() == 76 &&
              only.audioState() == 3 && restored.audioState() == 3,
              "both transports complete exactly at two seconds");

        ScsiDisk chunked;
        check(chunked.openCdrom("cd_audio_only.cue"), "mount for irregular clock ticks");
        chunked.command(playTwoSeconds, 10, out, in);
        RecordingSink chunkedSink;
        chunked.setCdAudioSink(&chunkedSink);
        uint64_t elapsed = 0;
        while (elapsed < 2000000) {
            const uint64_t tick = std::min<uint64_t>(37, 2000000 - elapsed);
            chunked.advanceAudio(tick);
            elapsed += tick;
        }
        check(chunkedSink.sectors == clockSink.sectors && chunked.audioState() == 3,
              "irregular ticks deliver the identical two seconds of audio");

        std::remove("cd_audio_only.bin");
        std::remove("cd_audio_only.cue");
    }

    // ── A disc whose DATA track is not the first ────────────────────────
    // A mixed-mode disc can place audio before its data track. This fixture
    // describes one session, not a CD Extra multisession layout. POM68K used
    // to miss the data extent unless it was track 1, leaving nothing to mount.
    // READ(10) carries ABSOLUTE disc addresses, so the data track's start
    // has to come off before the image is indexed.
    {
        const uint32_t kAudioSectors = 120, kDataSectors = 6;
        std::vector<uint8_t> bin;
        for (uint32_t i = 0; i < kAudioSectors; i++) {
            std::vector<uint8_t> s2(2352, 0x33);
            bin.insert(bin.end(), s2.begin(), s2.end());
        }
        const uint32_t dataStart = kAudioSectors;
        for (uint32_t i = 0; i < kDataSectors; i++) {
            std::vector<uint8_t> s2(2352, uint8_t(0x70 + i));
            static const uint8_t sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
            std::memcpy(s2.data(), sync, 12);
            s2[15] = 0x01;
            bin.insert(bin.end(), s2.begin(), s2.end());
        }
        { std::ofstream f("cd_audio_extra.bin", std::ios::binary);
          f.write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size())); }
        char sheet[512];
        std::snprintf(sheet, sizeof sheet,
            "FILE \"cd_audio_extra.bin\" BINARY\n"
            "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
            "  TRACK 02 MODE1/2352\n    INDEX 01 %02u:%02u:%02u\n",
            dataStart / (60 * 75), (dataStart / 75) % 60, dataStart % 75);
        { std::ofstream f("cd_audio_extra.cue"); f << sheet; }

        ScsiDisk extra;
        check(extra.openCdrom("cd_audio_extra.cue"),
              "a disc whose data track comes second mounts");
        check(extra.blocks() == kDataSectors,
              "only the data track's sectors count as user data");
        const uint8_t capacity[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        check(extra.command(capacity, 10, out, in) == 0 && out.size() == 8 &&
              ((uint32_t(out[0]) << 24) | (uint32_t(out[1]) << 16) |
               (uint32_t(out[2]) << 8) | out[3]) == kAudioSectors + kDataSectors - 1,
              "capacity reports the final absolute LBA, not the data extent's size");
        // The volume's first block is at the DATA TRACK's absolute address.
        const uint8_t rd[10] = { 0x28, 0,
            uint8_t(dataStart >> 24), uint8_t(dataStart >> 16),
            uint8_t(dataStart >> 8), uint8_t(dataStart), 0, 0, 1, 0 };
        check(extra.command(rd, 10, out, in) == 0 && out.size() == 2048 &&
              out[0] == 0x70,
              "READ at the data track's absolute LBA returns its first block");
        const uint8_t rd0[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
        check(extra.command(rd0, 10, out, in) == 2,
              "a READ inside the audio region is refused, not zero-filled");
        const uint8_t playAudio[10] = { 0x45, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
        check(extra.command(playAudio, 10, out, in) == 0 && extra.audioState() == 1,
              "PLAY accepts the preceding audio track");
        const uint8_t sense[6] = { 0x03, 0, 0, 0, 18, 0 };
        for (uint32_t position : { dataStart, dataStart + kDataSectors - 1 }) {
            const uint8_t playData[10] = { 0x45, 0, 0, 0, 0, uint8_t(position),
                                         0, 0, 1, 0 };
            check(extra.command(playData, 10, out, in) == 2,
                  "PLAY rejects a data sector even when an earlier track is audio");
            check(extra.command(sense, 6, out, in) == 0 && out.size() >= 14 &&
                  (out[2] & 15) == 5 && out[12] == 0x64,
                  "rejected PLAY reports ILLEGAL REQUEST / ILLEGAL MODE FOR THIS TRACK");
            check(extra.audioState() == 1 && extra.audioLba() == 0,
                  "a rejected PLAY preserves the existing audio transport");
        }
        const uint32_t dataFrame = dataStart + 150;
        const uint8_t playDataMsf[10] = { 0x47, 0, 0, uint8_t(dataFrame / (60 * 75)),
            uint8_t((dataFrame / 75) % 60), uint8_t(dataFrame % 75),
            uint8_t((dataFrame + 1) / (60 * 75)),
            uint8_t(((dataFrame + 1) / 75) % 60), uint8_t((dataFrame + 1) % 75), 0 };
        check(extra.command(playDataMsf, 10, out, in) == 2,
              "PLAY AUDIO MSF also rejects the later data track");

        // And ejecting really empties the drive, audio tracks included.
        extra.eject();
        const uint8_t tur2[6] = { 0x00, 0, 0, 0, 0, 0 };
        check(extra.command(tur2, 6, out, in) == 2,
              "after eject the drive reports an empty tray");

        std::remove("cd_audio_extra.bin");
        std::remove("cd_audio_extra.cue");
    }

    // A single physical disc represented by three files. FILE offsets
    // restart at zero, while guest LBAs and the TOC remain continuous.
    {
        const char* paths[] = {"cd_multi_data.bin", "cd_multi_a.bin", "cd_multi_b.bin"};
        { std::ofstream f(paths[0], std::ios::binary);
          std::vector<uint8_t> data(2 * 2048, 0x71);
          f.write(reinterpret_cast<const char*>(data.data()), data.size()); }
        for (int file = 1; file < 3; ++file) {
            std::ofstream f(paths[file], std::ios::binary);
            for (int sector = 0; sector < (file == 1 ? 152 : 3); ++sector) {
                const auto value = file == 1 ? (sector < 150 ? 0 : 17 + sector - 150) : 32 + sector;
                std::vector<uint8_t> bytes(2352, uint8_t(value));
                f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
        }
        { std::ofstream f("cd_multi.cue");
          f << "FILE \"cd_multi_data.bin\" BINARY\n"
               " TRACK 01 MODE1/2048\n INDEX 01 00:00:00\n"
               "FILE \"cd_multi_a.bin\" BINARY\n"
               " TRACK 02 AUDIO\n INDEX 00 00:00:00\n INDEX 01 00:02:00\n"
               "FILE \"cd_multi_b.bin\" BINARY\n"
               " TRACK 03 AUDIO\n INDEX 01 00:00:00\n"; }
        checkPregap("cd_multi.cue", 2, 152, 2);
        ScsiDisk multi;
        RecordingSink recorded;
        multi.setCdAudioSink(&recorded);
        check(multi.openCdrom("cd_multi.cue") && multi.blocks() == 2,
              "a multi-FILE disc keeps its MODE1/2048 data extent");
        check(multi.trackCount() == 3 && multi.trackStartLba(1) == 152 &&
              multi.trackStartLba(2) == 154,
              "file-relative indices become absolute TOC positions");
        const uint8_t read[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0};
        check(multi.command(read, 10, out, in) == 0 && out.size() == 2048 && out[0] == 0x71,
              "READ(10) reads the data FILE, not the last audio FILE");
        const uint8_t toc[10] = {0x43, 0, 0, 0, 0, 0, 0, 0, 40, 0};
        check(multi.command(toc, 10, out, in) == 0 && out.size() == 36 && out[35] == 157,
              "READ TOC lead-out includes every source file");
        const uint8_t playFiles[10] = {0x45, 0, 0, 0, 0, 152, 0, 0, 5, 0};
        check(multi.command(playFiles, 10, out, in) == 0,
              "PLAY AUDIO accepts a span crossing two FILE sources");
        multi.advanceAudio(66667);
        bool correct = recorded.sectors.size() == 5;
        const uint8_t values[] = {17, 18, 32, 33, 34};
        for (size_t i = 0; correct && i < 5; ++i)
            for (auto byte : recorded.sectors[i]) correct &= byte == values[i];
        check(correct, "audio crosses FILE boundaries with no skipped or repeated sector");
        multi.eject();
        check(!multi.openCdrom("cd_missing.cue"), "missing sheets do not retain the previous disc");

        // The least common multiple of the two sector widths is ambiguous
        // by length. A declared 2048-byte track must never be de-framed just
        // because its user bytes happen to start like a raw sector.
        { std::ofstream f(paths[0], std::ios::binary);
          std::vector<uint8_t> data(147 * 2048, 0x71);
          data[0] = 0; data[11] = 0;
          for (int i = 1; i < 11; ++i) data[i] = 0xFF;
          f.write(reinterpret_cast<const char*>(data.data()), data.size()); }
        { std::ofstream f("cd_multi.cue");
          f << "FILE \"cd_multi_data.bin\" BINARY\n"
               " TRACK 01 MODE1/2048\n INDEX 01 00:00:00\n"; }
        check(multi.openCdrom("cd_multi.cue") && multi.blocks() == 147,
              "declared MODE1/2048 framing wins over an accidental raw sync pattern");

        // Malformed and unsupported representations must not produce a
        // plausible but wrong physical disc.
        const char* bad[] = {
            "FILE \"cd_multi_a.bin\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
            "FILE \"cd_multi_a.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:60:00\n",
            "FILE \"cd_multi_a.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:03\n",
            "FILE \"cd_multi_a.bin\" BINARY\n TRACK 01 AUDIO\n PREGAP 00:02:00\n INDEX 01 00:00:00\n",
            "FILE \"cd_multi_a.bin\" BINARY\n TRACK 01 AUDIO\n FLAGS PRE\n INDEX 01 00:00:00\n",
            "FILE \"cd_multi_a.bin\" BINARY\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n INDEX 02 00:00:01\n",
            "FILE \"cd_multi_a.bin\" BINARY\n TRACK 01 AUDIO\n",
            "FILE \"cd_multi_data.bin\" BINARY\n TRACK 01 MODE1/2048\n INDEX 01 00:00:00\n TRACK 02 AUDIO\n INDEX 01 00:00:01\n"
        };
        bool rejected = true;
        for (auto sheet : bad) {
            { std::ofstream f("cd_multi_bad.cue"); f << sheet; }
            rejected &= !multi.openCdrom("cd_multi_bad.cue");
        }
        check(rejected, "invalid indices, framing, missing indices and unsupported gaps are rejected");
        for (auto path : paths) std::remove(path);
        std::remove("cd_multi.cue");
        std::remove("cd_multi_bad.cue");
    }

    std::remove("cd_audio_test.bin");
    std::remove("cd_audio_test.cue");
    std::printf(failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
