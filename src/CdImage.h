// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// A CD image as a disc: tracks at absolute disc LBAs, laid over spans that
// each come from one backing source or are synthesized silence. ScsiDisk
// asks it for the TOC, the data track's extent and raw 2352-byte audio
// sectors; parsing the sheet and reading the sources stays here.
//
// CUE INDEX times are relative to their FILE; the disc is the
// concatenation of the files plus the gaps the sheet synthesizes:
//
//   PREGAP t   silence on the disc before the track's INDEX 01, in no file
//   POSTGAP t  silence after the track's stored sectors, in no file
//   INDEX 00   a pregap STORED in the file (the extent starts there)
//   INDEX 02+  sub-indexes: accepted, they move nothing in the TOC
//
// Sources: BINARY (raw sectors, CD-DA little-endian), MOTOROLA (CD-DA
// big-endian, swapped on read), WAVE (RIFF PCM, 16-bit stereo 44.1 kHz
// only — any other encoding is refused by name; a final partial sector is
// padded with silence). Track types AUDIO, MODE1/2048, MODE1/2352; at most
// one data track, one session. FLAGS DCP, PRE and 4CH become the track's
// Q control bits; PRE also makes playback de-emphasize (CdDeemphasis.h).
// Refused: a stored track-1 pregap (INDEX 01 past the file's start on
// track 1, which needs negative disc addresses), later sessions and MODE2.
// Reference: https://github.com/libyal/libodraw/blob/main/documentation/CUE%20sheet%20format.asciidoc
// Gate: cd_image_test.

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class CdImage {
public:
    enum class Encoding : std::uint8_t { Binary, Motorola, Wave };
    struct Source {
        std::string path;
        Encoding encoding = Encoding::Binary;
        std::uint64_t dataOffset = 0; // first sector's byte (WAVE: data chunk)
        std::uint64_t dataBytes = 0;
        std::uint32_t sectorBytes = 0; // 2048 or 2352
        std::uint32_t sectors = 0;     // a WAVE's last partial sector counts
    };
    // Q-channel control bits a sheet's FLAGS set (IEC 60908 § 17.5.1).
    static constexpr std::uint8_t kFlagPre = 0x1, kFlagDcp = 0x2, kFlag4Ch = 0x8;
    struct Track {
        std::uint8_t number = 0;
        bool audio = false;
        std::uint8_t flags = 0;        // kFlag* — data tracks add 0x4 in the Q
        std::uint32_t sectorBytes = 0;
        std::uint32_t extentLba = 0;   // pregap start (PREGAP or INDEX 00)
        std::uint32_t startLba = 0;    // INDEX 01
        std::uint32_t storedEndLba = 0;// end of its stored sectors (before POSTGAP)
        std::uint32_t source = 0;
        std::uint32_t sourceStart = 0; // source sector at startLba
    };
    // A run of disc sectors: from `source` at `sourceSector`, or silence.
    struct Span {
        std::uint32_t lba = 0, sectors = 0;
        bool silence = false;
        std::uint32_t source = 0, sourceSector = 0;
    };

    // Parses `cuePath` and lays out the disc. On failure the image is
    // empty and `error()` says why.
    bool open(const std::string& cuePath);
    void clear();

    const std::vector<Track>& tracks() const { return tracks_; }
    const std::vector<Span>& spans() const { return spans_; }
    const std::vector<Source>& sources() const { return sources_; }
    std::uint32_t leadOut() const { return leadOut_; }
    const std::string& error() const { return error_; }
    // The data track, or nullptr on an audio disc.
    const Track* dataTrack() const;

    // The data track's stored sectors, as framed in its source (2048 or
    // 2352 bytes each), and the disc LBA of the first one.
    bool readData(std::vector<std::uint8_t>& out, std::uint32_t& startLba,
                  std::uint32_t& sectorBytes);
    // One raw 2352-byte sector at a disc LBA, CD-DA byte order (16-bit
    // little-endian stereo): silence inside a synthesized gap. False past
    // the lead-out, on a 2048-byte source, or when the source cannot be read.
    bool readRawSector(std::uint32_t lba, std::uint8_t* out2352);

private:
    bool fail(std::string message);
    bool openSource(Source& source);
    bool readSource(std::uint32_t index, std::uint32_t sector, std::uint32_t count,
                    std::uint8_t* out);

    std::vector<Source> sources_;
    std::vector<Track> tracks_;
    std::vector<Span> spans_;
    std::uint32_t leadOut_ = 0;
    std::string error_;
    std::string streamPath_;   // the one open source stream, cached
    std::ifstream stream_;
};
