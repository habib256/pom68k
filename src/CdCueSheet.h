// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// CUE is a host representation of a disc, not an extra guest device.
// INDEX addresses are relative to FILE; the SCSI TOC uses disc addresses.
// Reference: https://github.com/libyal/libodraw/blob/main/documentation/CUE%20sheet%20format.asciidoc
// Snow's cuesheet backend
// (23a41e7), whose separate source and disc coordinates exposed our bug.
// Support BINARY AUDIO and one MODE1 track, in one session. Encoded audio,
// synthetic gaps and a stored track-1 pregap need additional sector sources
// or negative LBAs; reject them instead of inventing a TOC.
struct CdCueSheet {
    struct Source {
        std::string path;
        uint32_t start = 0, sectors = 0, sectorBytes = 0;
    };
    struct Track {
        uint8_t number = 0;
        bool audio = false;
        uint32_t startLba = 0, fileIndex = 0, offset = 0;
        uint32_t gapOffset = 0;
        bool hasIndex = false, hasGap = false;
    };
    std::vector<Source> sources;
    std::vector<Track> tracks;
    uint32_t sectors = 0;

    bool open(const std::string& path) {
        sources.clear(); tracks.clear(); sectors = 0;
        std::ifstream in(path);
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream row(line);
            std::string command;
            row >> command;
            if (command == "FILE") {
                std::string name, type;
                if (!(row >> std::quoted(name) >> type) || type != "BINARY" ||
                    name.empty()) return false;
                sources.push_back({(std::filesystem::path(path).parent_path() / name).string()});
            } else if (command == "TRACK") {
                unsigned number = 0;
                std::string type;
                if (sources.empty() || !(row >> number >> type) || number < 1 ||
                    number > 99 || number != tracks.size() + 1)
                    return false;
                const uint32_t bytes = type == "MODE1/2048" ? 2048 :
                    (type == "MODE1/2352" || type == "AUDIO") ? 2352 : 0;
                if (!bytes || (sources.back().sectorBytes && sources.back().sectorBytes != bytes))
                    return false;
                sources.back().sectorBytes = bytes;
                Track track;
                track.number = uint8_t(number);
                track.audio = type == "AUDIO";
                track.fileIndex = uint32_t(sources.size() - 1);
                tracks.push_back(track);
            } else if (command == "INDEX") {
                unsigned index = 0, m = 0, s = 0, f = 0;
                std::string time;
                char trailing = 0;
                if (tracks.empty() || tracks.back().fileIndex != sources.size() - 1 ||
                    !(row >> index >> time) || index > 1 ||
                    std::sscanf(time.c_str(), "%u:%u:%u%c", &m, &s, &f, &trailing) != 3 ||
                    s >= 60 || f >= 75 || m > 99) return false;
                const uint32_t offset = (m * 60 + s) * 75 + f;
                auto& track = tracks.back();
                if (index == 0) {
                    if (track.hasGap || track.hasIndex) return false;
                    track.hasGap = true; track.gapOffset = offset;
                } else if (index == 1) {
                    if (track.hasIndex || (track.hasGap && track.gapOffset > offset)) return false;
                    track.hasIndex = true; track.offset = offset;
                }
            } else if (command == "PREGAP" || command == "POSTGAP" || command == "FLAGS") {
                return false;
            } else if (command == "REM") {
                std::string key;
                row >> key;
                if (key == "SESSION") {
                    unsigned session = 0;
                    if (!(row >> session) || session != 1) return false;
                }
            } else if (!command.empty() && command != "REM" && command != "TITLE" &&
                       command != "PERFORMER" && command != "SONGWRITER" &&
                       command != "CATALOG" && command != "ISRC" &&
                       command != "CDTEXTFILE") {
                return false;
            }
        }
        if (tracks.empty() || tracks.front().offset != 0) return false;
        for (auto& source : sources) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(source.path, ec);
            if (ec || !source.sectorBytes || !size || size % source.sectorBytes ||
                size / source.sectorBytes > std::numeric_limits<uint32_t>::max() - sectors)
                return false;
            source.start = sectors;
            source.sectors = uint32_t(size / source.sectorBytes);
            sectors += source.sectors;
        }
        for (size_t i = 0; i < tracks.size(); ++i) {
            auto& track = tracks[i];
            const auto& source = sources[track.fileIndex];
            if (!track.hasIndex || track.offset >= source.sectors) return false;
            track.startLba = source.start + track.offset;
            const uint32_t extentStart = source.start +
                (track.hasGap ? track.gapOffset : track.offset);
            if (i && extentStart <= tracks[i - 1].startLba) return false;
        }
        // ScsiDisk currently exposes a single contiguous data extent.
        unsigned dataTracks = 0;
        for (const auto& track : tracks) dataTracks += !track.audio;
        return dataTracks <= 1;
    }

    bool readData(std::vector<uint8_t>& out, uint32_t& start) const {
        out.clear(); start = 0;
        for (size_t i = 0; i < tracks.size(); ++i) {
            const auto& track = tracks[i];
            if (track.audio) continue;
            const auto& source = sources[track.fileIndex];
            uint32_t end = source.sectors;
            if (i + 1 < tracks.size() && tracks[i + 1].fileIndex == track.fileIndex) {
                const auto& next = tracks[i + 1];
                end = next.hasGap ? next.gapOffset : next.offset;
            }
            if (end <= track.offset) return false;
            std::ifstream file(source.path, std::ios::binary);
            file.seekg(uint64_t(track.offset) * source.sectorBytes);
            out.resize(size_t(end - track.offset) * source.sectorBytes);
            start = track.startLba;
            return bool(file.read(reinterpret_cast<char*>(out.data()), out.size()));
        }
        return true; // CD-DA: no data track.
    }
};
