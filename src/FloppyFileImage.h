// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#pragma once
#include "DartImage.h"
#include "Dc42Image.h"
#include <cctype>
#include <string>

// Host container provenance, independent of the physical Sony mechanism.
class FloppyFileImage {
    enum class Format : uint8_t { Raw, Dc42, Dart };
    Format format_ = Format::Raw;
    std::vector<uint8_t> header_;
public:
    void clear() { format_ = Format::Raw; header_.clear(); }
    bool wrapped() const { return format_ != Format::Raw; }
    bool unpack(std::vector<uint8_t>& raw, std::vector<uint8_t>& tags,
                const std::string& path) {
        clear();
        std::string suffix = path.size() >= 5 ? path.substr(path.size() - 5) : path;
        for (char& c : suffix) c = char(std::tolower(static_cast<unsigned char>(c)));
        // Exact raw geometries take priority over speculative DART headers.
        if (suffix == ".dart" || (raw.size() != 409600 && raw.size() != 819200 &&
            raw.size() != 1474560 && dart::candidate(raw))) {
            if (!dart::unpack(raw, tags)) return false;
            format_ = Format::Dart;
            return true;
        }
        if (!dc42::unpack(raw, header_, tags)) return false;
        if (!header_.empty()) format_ = Format::Dc42;
        return true;
    }
    bool write(std::ostream& out, const std::vector<uint8_t>& data,
               const std::vector<uint8_t>& tags) {
        return format_ == Format::Dart ? dart::write(out, data, tags) :
               format_ == Format::Dc42 && dc42::write(out, header_, data, tags);
    }
    template<class Ar> void visit(Ar& ar) {
        ar(format_, header_);
        if constexpr (Ar::loading) {
            if ((format_ == Format::Raw || format_ == Format::Dart) ? !header_.empty() :
                format_ != Format::Dc42 || header_.size() != 84 ||
                header_[82] != 1 || header_[83] != 0) ar.fail();
        }
    }
};
