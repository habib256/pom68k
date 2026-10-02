// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The machine-independent half of the guest-side differential oracle
// (tests/prober_oracle.cpp): read the Prober's report back out of a disk
// image, parse it, and compare it with MAME's recorded report on every
// field the two models can both judge.

#pragma once

#include "HfsInject.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace proberoracle {

inline const char* const kReport = "POM68K Prober.txt";

// A field one model cannot judge → why it is not evidence.
using Unjudged = std::map<std::string, const char*>;

inline std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

inline bool writeFile(const std::string& path, const void* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), std::streamsize(size));
    return bool(out);
}

// The report's data fork as host text: CR → LF, MacRoman above $7F kept
// visible as \xNN (the Prober writes ASCII; a volume name may not be).
inline bool extractReport(hfsinject::BlockIo& io, std::string& text, std::string& err) {
    uint32_t start = 0, length = 0;
    if (!hfsinject::findHfsVolume(io, start, length, err)) return false;
    hfsinject::Volume vol(io, start, length);
    if (!vol.open(err)) return false;
    uint32_t folder = 0;
    for (const std::string& n : hfsinject::startupItemsNames())
        if ((folder = vol.findFolder(vol.blessedFolder(), n)) != 0) break;
    if (folder == 0) { err = "no Startup Items under the blessed folder"; return false; }
    hfsinject::MacBinary file;
    if (!vol.readFile(folder, kReport, file, err)) return false;
    text.clear();
    for (uint8_t c : file.data) {
        if (c == '\r') text += '\n';
        else if (c >= 0x80) { char b[8]; std::snprintf(b, sizeof b, "\\x%02X", c); text += b; }
        else text += char(c);
    }
    return true;
}

// "section.key" → value, from the TSV rows; `findings` from the header,
// `lines` the data rows actually present (equal once the file is whole).
struct Report {
    std::map<std::string, std::string> rows;
    std::string findings;
    size_t lines = 0;
    bool complete() const { return !findings.empty() && std::to_string(lines) == findings; }
};

inline Report parse(const std::string& text) {
    Report r;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells;
        std::istringstream ls(line);
        for (std::string c; std::getline(ls, c, '\t');) cells.push_back(c);
        if (cells[0].rfind("POM68K-Prober", 0) == 0) {
            for (const std::string& c : cells)
                if (c.rfind("findings=", 0) == 0) r.findings = c.substr(9);
            continue;
        }
        if (cells.size() >= 3) { r.rows[cells[0] + "." + cells[1]] = cells[2]; r.lines++; }
    }
    return r;
}

// Everything in MAME's report must appear in POM68K's with the same value,
// and nothing more, except the unjudged fields. Returns the differences.
inline int compare(const std::string& goldenText, const std::string& ours,
                   const Unjudged& unjudged) {
    const Report mame = parse(goldenText), pom = parse(ours);
    int bad = 0, judged = 0;
    if (mame.findings != pom.findings) {
        std::printf("  findings: MAME %s, POM68K %s\n", mame.findings.c_str(), pom.findings.c_str());
        bad++;
    }
    std::set<std::string> keys;
    for (const auto& [k, v] : mame.rows) keys.insert(k);
    for (const auto& [k, v] : pom.rows) keys.insert(k);
    for (const std::string& k : keys) {
        const auto m = mame.rows.find(k), p = pom.rows.find(k);
        const std::string mv = m == mame.rows.end() ? "(absent)" : m->second;
        const std::string pv = p == pom.rows.end() ? "(absent)" : p->second;
        if (const auto u = unjudged.find(k); u != unjudged.end()) {
            std::printf("  unjudged %-18s MAME %s, POM68K %s — %s\n", k.c_str(), mv.c_str(),
                        pv.c_str(), u->second);
            continue;
        }
        judged++;
        if (mv != pv) {
            std::printf("  DIFFER   %-18s MAME %s, POM68K %s\n", k.c_str(), mv.c_str(), pv.c_str());
            bad++;
        }
    }
    std::printf("%d fields judged, %zu unjudged, %d differ\n", judged, unjudged.size(), bad);
    return bad;
}

} // namespace proberoracle
