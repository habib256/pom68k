// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "CdImage.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>

namespace {

constexpr std::uint32_t kRawSector = 2352;

// mm:ss:ff, 75 frames a second.
std::optional<std::uint32_t> msf(const std::string& text) {
    unsigned m = 0, s = 0, f = 0;
    char trailing = 0;
    if (std::sscanf(text.c_str(), "%u:%u:%u%c", &m, &s, &f, &trailing) != 3 ||
        m > 99 || s >= 60 || f >= 75)
        return std::nullopt;
    return (m * 60 + s) * 75 + f;
}

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
           std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
std::uint16_t le16(const std::uint8_t* p) {
    return std::uint16_t(p[0] | p[1] << 8);
}

// What the sheet said about one track before the disc is laid out.
struct SheetTrack {
    std::uint8_t number = 0;
    bool audio = false;
    std::uint32_t sectorBytes = 0, source = 0;
    std::uint8_t flags = 0;
    std::optional<std::uint32_t> index0, index1, pregap, postgap;
    std::uint32_t lastIndex = 0, lastIndexTime = 0;
};

} // namespace

bool CdImage::fail(std::string message) {
    const std::string kept = std::move(message);
    clear();
    error_ = kept;
    return false;
}

void CdImage::clear() {
    sources_.clear();
    tracks_.clear();
    spans_.clear();
    leadOut_ = 0;
    error_.clear();
    streamPath_.clear();
    if (stream_.is_open()) stream_.close();
    stream_.clear();
}

const CdImage::Track* CdImage::dataTrack() const {
    for (const Track& track : tracks_)
        if (!track.audio) return &track;
    return nullptr;
}

// A WAVE source: RIFF/WAVE, a PCM `fmt ` (or EXTENSIBLE with the PCM
// subformat) at exactly CD-DA's 16-bit, two channels, 44 100 Hz, and its
// `data` chunk. Chunks are word-aligned (RIFF pads odd sizes).
bool CdImage::openSource(Source& source) {
    std::error_code error;
    const std::uint64_t size = std::filesystem::file_size(source.path, error);
    if (error) return fail("missing source file " + source.path);
    if (source.encoding != Encoding::Wave) {
        if (!size || size % source.sectorBytes)
            return fail(source.path + " is " + std::to_string(size) +
                        " bytes, not a whole number of " +
                        std::to_string(source.sectorBytes) + "-byte sectors");
        source.dataOffset = 0;
        source.dataBytes = size;
    } else {
        std::ifstream in(source.path, std::ios::binary);
        std::array<std::uint8_t, 12> riff{};
        if (!in.read(reinterpret_cast<char*>(riff.data()), riff.size()) ||
            std::memcmp(riff.data(), "RIFF", 4) || std::memcmp(riff.data() + 8, "WAVE", 4))
            return fail(source.path + " is not a RIFF/WAVE file");
        bool format = false;
        std::uint64_t offset = 12;
        while (offset + 8 <= size) {
            std::array<std::uint8_t, 8> head{};
            in.seekg(std::streamoff(offset));
            if (!in.read(reinterpret_cast<char*>(head.data()), head.size())) break;
            const std::uint64_t bytes = le32(head.data() + 4);
            const std::uint64_t body = offset + 8;
            if (!std::memcmp(head.data(), "fmt ", 4)) {
                std::array<std::uint8_t, 40> fmt{};
                const std::size_t want = std::size_t(std::min<std::uint64_t>(bytes, fmt.size()));
                if (bytes < 16 || !in.read(reinterpret_cast<char*>(fmt.data()), std::streamsize(want)))
                    return fail(source.path + ": malformed WAVE format chunk");
                std::uint16_t tag = le16(fmt.data());
                if (tag == 0xFFFE && bytes >= 40) tag = le16(fmt.data() + 24);
                const unsigned channels = le16(fmt.data() + 2);
                const std::uint32_t rate = le32(fmt.data() + 4);
                const unsigned bits = le16(fmt.data() + 14);
                if (tag != 1 || channels != 2 || rate != 44100 || bits != 16)
                    return fail(source.path + ": unsupported WAVE encoding (format " +
                        std::to_string(tag) + ", " + std::to_string(channels) +
                        " channel(s), " + std::to_string(rate) + " Hz, " +
                        std::to_string(bits) + "-bit); CD audio is 16-bit PCM "
                        "stereo at 44100 Hz");
                format = true;
            } else if (!std::memcmp(head.data(), "data", 4)) {
                if (!format) return fail(source.path + ": WAVE data before its format");
                if (body + bytes > size)
                    return fail(source.path + ": WAVE data chunk runs past the file");
                if (!bytes || bytes % 4)
                    return fail(source.path + ": WAVE data is not whole stereo frames");
                source.dataOffset = body;
                source.dataBytes = bytes;
                break;
            }
            offset = body + bytes + (bytes & 1);
        }
        if (!source.dataBytes) return fail(source.path + ": WAVE file without data");
    }
    const std::uint64_t sectors =
        (source.dataBytes + source.sectorBytes - 1) / source.sectorBytes;
    if (sectors > std::numeric_limits<std::uint32_t>::max() / 2)
        return fail(source.path + " is too large for a disc");
    source.sectors = std::uint32_t(sectors);
    return true;
}

bool CdImage::open(const std::string& cuePath) {
    clear();
    std::ifstream in(cuePath);
    if (!in) return fail("cannot read " + cuePath);
    const std::filesystem::path base = std::filesystem::path(cuePath).parent_path();
    std::vector<SheetTrack> sheet;
    std::string line;
    int lineNumber = 0;
    auto bad = [&](const std::string& why) {
        return fail(cuePath + ":" + std::to_string(lineNumber) + ": " + why);
    };
    while (std::getline(in, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream row(line);
        std::string command;
        row >> command;
        if (command == "FILE") {
            std::string name, type;
            if (!(row >> std::quoted(name) >> type) || name.empty())
                return bad("FILE needs a name and a type");
            Source source;
            source.path = (base / name).string();
            if (type == "BINARY") source.encoding = Encoding::Binary;
            else if (type == "MOTOROLA") source.encoding = Encoding::Motorola;
            else if (type == "WAVE") source.encoding = Encoding::Wave;
            else return bad("unsupported FILE type " + type +
                            " (BINARY, MOTOROLA and WAVE are read)");
            sources_.push_back(source);
        } else if (command == "TRACK") {
            unsigned number = 0;
            std::string type;
            if (sources_.empty()) return bad("TRACK before any FILE");
            if (!(row >> number >> type) || number < 1 || number > 99 ||
                number != sheet.size() + 1)
                return bad("tracks must be numbered 1, 2, 3… in order");
            const std::uint32_t bytes = type == "MODE1/2048" ? 2048u
                : (type == "MODE1/2352" || type == "AUDIO") ? kRawSector : 0u;
            if (!bytes) return bad("unsupported track type " + type);
            Source& source = sources_.back();
            if (source.sectorBytes && source.sectorBytes != bytes)
                return bad("one FILE cannot mix 2048- and 2352-byte sectors");
            if (source.encoding != Encoding::Binary && type != "AUDIO")
                return bad("a data track must come from a BINARY file");
            source.sectorBytes = bytes;
            SheetTrack track;
            track.number = std::uint8_t(number);
            track.audio = type == "AUDIO";
            track.sectorBytes = bytes;
            track.source = std::uint32_t(sources_.size() - 1);
            sheet.push_back(track);
        } else if (command == "INDEX") {
            unsigned index = 0;
            std::string time;
            if (sheet.empty() || sheet.back().source != sources_.size() - 1)
                return bad("INDEX outside a track of the current FILE");
            const auto offset = (row >> index >> time) ? msf(time) : std::nullopt;
            if (!offset || index > 99) return bad("INDEX needs nn mm:ss:ff");
            SheetTrack& track = sheet.back();
            const bool ordered = index == 0
                ? !track.index0 && !track.index1
                : index == 1 ? !track.index1 && (!track.index0 || *track.index0 <= *offset)
                : track.index1 && index == track.lastIndex + 1 &&
                  *offset > track.lastIndexTime;
            if (!ordered) return bad("indexes must rise: 00 (optional), 01, 02…");
            if (index == 0) {
                if (track.pregap) return bad("a track has PREGAP or INDEX 00, not both");
                track.index0 = offset;
            } else if (index == 1) {
                track.index1 = offset;
            }
            track.lastIndex = index;
            track.lastIndexTime = *offset;
        } else if (command == "PREGAP" || command == "POSTGAP") {
            std::string time;
            const auto length = (row >> time) ? msf(time) : std::nullopt;
            if (sheet.empty() || !length || !*length)
                return bad(command + " needs a track and a nonzero mm:ss:ff");
            SheetTrack& track = sheet.back();
            if (command == "PREGAP") {
                if (track.index1 || track.index0 || track.pregap)
                    return bad("PREGAP comes once, before the track's indexes");
                track.pregap = length;
            } else {
                if (!track.index1 || track.postgap)
                    return bad("POSTGAP comes once, after INDEX 01");
                track.postgap = length;
            }
        } else if (command == "FLAGS") {
            if (sheet.empty() || sheet.back().index1)
                return bad("FLAGS belongs to a track, before its INDEX 01");
            std::string flag;
            while (row >> flag) {
                if (flag == "DCP") sheet.back().flags |= kFlagDcp;
                else if (flag == "PRE") sheet.back().flags |= kFlagPre;
                else if (flag == "4CH") sheet.back().flags |= kFlag4Ch;
                else if (flag != "SCMS")
                    return bad("unknown FLAGS " + flag + " (DCP, 4CH, PRE, SCMS)");
            }
        } else if (command == "REM") {
            std::string key;
            row >> key;
            unsigned session = 0;
            if (key == "SESSION" && (!(row >> session) || session != 1))
                return bad("only single-session discs are read");
        } else if (!command.empty() && command != "TITLE" && command != "PERFORMER" &&
                   command != "SONGWRITER" && command != "CATALOG" &&
                   command != "ISRC" && command != "CDTEXTFILE") {
            return bad("unknown command " + command);
        }
    }
    lineNumber = 0;
    if (sheet.empty()) return bad("no TRACK");
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        if (!sources_[i].sectorBytes)
            return fail(cuePath + ": FILE " + sources_[i].path + " has no TRACK");
        if (!openSource(sources_[i])) return false;
    }

    unsigned dataTracks = 0;
    std::uint64_t cursor = 0;
    for (std::size_t i = 0; i < sheet.size(); ++i) {
        const SheetTrack& t = sheet[i];
        const Source& source = sources_[t.source];
        const std::string name = "track " + std::to_string(t.number) + ": ";
        if (!t.index1) return fail(cuePath + ": " + name + "no INDEX 01");
        dataTracks += !t.audio;
        const bool firstInFile = i == 0 || sheet[i - 1].source != t.source;
        // A stored track-1 pregap sits at negative disc addresses, which
        // neither the TOC nor READ(10) can carry.
        if (i == 0 && (*t.index1 != 0 || (t.index0 && *t.index0 != 0)))
            return fail(cuePath + ": " + name + "a stored track-1 pregap "
                        "(INDEX 01 past the file's start) is not supported");
        // The first track of a file owns the file's leading sectors as a
        // stored pregap even without INDEX 00.
        const std::uint32_t first = firstInFile ? 0 : t.index0.value_or(*t.index1);
        const bool lastInFile = i + 1 == sheet.size() || sheet[i + 1].source != t.source;
        const std::uint32_t end = lastInFile ? source.sectors
            : sheet[i + 1].index0.value_or(sheet[i + 1].index1.value_or(0));
        if (*t.index1 >= end)
            return fail(cuePath + ": " + name + "INDEX 01 at or past the end of its sectors");

        Track track;
        track.number = t.number;
        track.audio = t.audio;
        track.flags = t.flags;
        track.sectorBytes = t.sectorBytes;
        track.source = t.source;
        track.extentLba = std::uint32_t(cursor);
        if (t.pregap) {
            spans_.push_back({std::uint32_t(cursor), *t.pregap, true, 0, 0});
            cursor += *t.pregap;
        }
        track.startLba = std::uint32_t(cursor + (*t.index1 - first));
        track.sourceStart = *t.index1;
        spans_.push_back({std::uint32_t(cursor), end - first, false, t.source, first});
        cursor += end - first;
        track.storedEndLba = std::uint32_t(cursor);
        if (t.postgap) {
            spans_.push_back({std::uint32_t(cursor), *t.postgap, true, 0, 0});
            cursor += *t.postgap;
        }
        if (cursor > std::numeric_limits<std::uint32_t>::max() / 2)
            return fail(cuePath + ": the disc is too long");
        tracks_.push_back(track);
    }
    // ScsiDisk serves one contiguous data extent.
    if (dataTracks > 1) return fail(cuePath + ": more than one data track");
    leadOut_ = std::uint32_t(cursor);
    return true;
}

bool CdImage::readSource(std::uint32_t index, std::uint32_t sector,
                         std::uint32_t count, std::uint8_t* out) {
    const Source& source = sources_[index];
    const std::uint64_t bytes = std::uint64_t(count) * source.sectorBytes;
    const std::uint64_t at = std::uint64_t(sector) * source.sectorBytes;
    if (sector >= source.sectors || at + bytes > std::uint64_t(source.sectors) * source.sectorBytes)
        return false;
    if (streamPath_ != source.path) {
        if (stream_.is_open()) stream_.close();
        stream_.clear();
        streamPath_ = source.path;
        stream_.open(streamPath_, std::ios::binary);
    }
    if (!stream_.is_open()) return false;
    // A WAVE's last sector may be partial: the rest is silence.
    const std::uint64_t stored = at >= source.dataBytes ? 0
        : std::min(bytes, source.dataBytes - at);
    stream_.clear();
    stream_.seekg(std::streamoff(source.dataOffset + at));
    if (!stream_.read(reinterpret_cast<char*>(out), std::streamsize(stored)))
        return false;
    std::memset(out + stored, 0, std::size_t(bytes - stored));
    if (source.encoding == Encoding::Motorola)
        for (std::uint64_t i = 0; i + 1 < bytes; i += 2) std::swap(out[i], out[i + 1]);
    return true;
}

bool CdImage::readRawSector(std::uint32_t lba, std::uint8_t* out) {
    const auto span = std::upper_bound(spans_.begin(), spans_.end(), lba,
        [](std::uint32_t value, const Span& s) { return value < s.lba; });
    if (span == spans_.begin()) return false;
    const Span& hit = *std::prev(span);
    if (lba - hit.lba >= hit.sectors) return false;
    if (hit.silence) {
        std::memset(out, 0, kRawSector);
        return true;
    }
    if (sources_[hit.source].sectorBytes != kRawSector) return false;
    return readSource(hit.source, hit.sourceSector + (lba - hit.lba), 1, out);
}

bool CdImage::readData(std::vector<std::uint8_t>& out, std::uint32_t& startLba,
                       std::uint32_t& sectorBytes) {
    out.clear();
    startLba = 0;
    sectorBytes = 0;
    const Track* track = dataTrack();
    if (!track) return true; // CD-DA: no data track.
    const std::uint32_t count = track->storedEndLba - track->startLba;
    out.resize(std::size_t(count) * track->sectorBytes);
    startLba = track->startLba;
    sectorBytes = track->sectorBytes;
    return readSource(track->source, track->sourceStart, count, out.data());
}
