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

#include "CdAudioSource.h"
#include "ScsiDisk.h"

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
    const uint32_t am = audioStart + 150;
    char cue[512];
    std::snprintf(cue, sizeof cue,
        "FILE \"cd_audio_test.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n    INDEX 01 00:02:00\n"
        "  TRACK 02 AUDIO\n    INDEX 01 %02u:%02u:%02u\n",
        am / (60 * 75), (am / 75) % 60, am % 75);
    { std::ofstream f("cd_audio_test.cue"); f << cue; }

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
    disc.advanceAudio(1000000ull / 75 * 3);
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

    std::remove("cd_audio_test.bin");
    std::remove("cd_audio_test.cue");
    std::printf(failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
