// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Immutable reference-fixture routing. A writable consumer presented with
// .../ref/... receives a persistent sibling under .../work/... instead. The
// source is never opened for writing; clone failure degrades to read-only.

#pragma once

#include <filesystem>
#include <string>

namespace pom68k {

struct WritableFixture {
    std::string path;
    bool reference = false;
    bool copied = false;
    bool writable = true;
    std::string error;
};

// A well-known boot image named as hdv/foo first resolves to hdv/ref/foo when
// that immutable copy exists. Explicit hdv/ref and hdv/work paths, nested
// user directories and non-media paths keep their spelling. This makes the
// migration additive: an old checkout with only hdv/foo behaves exactly as
// before, while a reference-bearing checkout cannot accidentally prefer the
// mutable legacy file beside it.
inline std::string preferReferenceFixture(const std::string& source) {
    namespace fs = std::filesystem;
    const fs::path src(source);
    const fs::path parent = src.parent_path();
    if (parent.filename() != "hdv") return source;

    const fs::path candidate = parent / "ref" / src.filename();
    std::error_code ec;
    return fs::is_regular_file(candidate, ec) && !ec ? candidate.string()
                                                     : source;
}

inline bool isReferenceFixturePath(const std::string& source) {
    const std::filesystem::path parent =
        std::filesystem::path(source).parent_path();
    return parent.filename() == "ref" && parent.parent_path().filename() == "hdv";
}

inline WritableFixture writableFixture(const std::string& source) {
    namespace fs = std::filesystem;
    WritableFixture out;
    out.path = source;

    fs::path src(source), dst;
    bool found = false;
    for (const fs::path& part : src) {
        if (!found && part == "ref") {
            dst /= "work";
            found = true;
        } else {
            dst /= part;
        }
    }
    if (!found) return out;

    out.reference = true;
    out.path = dst.string();
    std::error_code ec;
    if (fs::exists(dst, ec)) {
        if (!ec && fs::is_regular_file(dst, ec)) return out;
        out.writable = false;
        out.error = "work path exists but is not a regular file";
        return out;
    }
    ec.clear();
    fs::create_directories(dst.parent_path(), ec);
    if (ec) {
        out.writable = false;
        out.error = "cannot create work directory: " + ec.message();
        return out;
    }
    ec.clear();
    if (!fs::copy_file(src, dst, fs::copy_options::none, ec)) {
        out.writable = false;
        out.error = "cannot clone reference fixture: " + ec.message();
        return out;
    }
    out.copied = true;
    return out;
}

} // namespace pom68k
