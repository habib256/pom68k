#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FirmwareChoice -- which MCU dump a device loads, and what the alternatives
// were.
//
// Eight devices used to answer that question with the same twenty lines of
// copy-paste: read the enable knob, walk a list of candidate paths, load the
// first one that opens, print a NON-CONFORMANT notice on failure, register
// the outcome.  They had already drifted -- only `V8Memory` honoured a
// per-device path override (`POM68K_CUDA_FW`), so on the other six Egret/Cuda
// boards "use THIS dump" simply did not exist, and the eighth (the ADB
// transceiver) had no override at all.
//
// `select()` is that sequence, once.  Every firmware device now gets the same
// three-step search, the same wording, the same report, and the same override:
//
//   1. the per-device path knob, when set -- an arbitrary file, anywhere;
//   2. the device's own ordered candidate list (factory part first);
//   3. failing both, the documented HLE substitute (LLE_VS_HLE § 2).
//
// The knobs are per MODULE, not per platform, because that is the granularity
// a user thinks in: `POM68K_CUDA_FW` covers the Egret/Cuda MCU on all seven
// boards that carry one (the name predates the generalisation and is kept --
// renaming a documented knob to gain symmetry is not worth breaking a script
// over), `POM68K_ADB_FW` covers the PIC1654S transceiver.
//
// A note on step 1 that is behaviour, not detail: an override that fails to
// load does NOT abort the search.  It warns and falls through to the
// candidates, because the alternative -- a machine that refuses to boot
// because a diagnostic path was left in the environment -- is worse than one
// that boots on its factory part and says so.  That was `V8Memory`'s original
// behaviour and it is now everyone's.
// ─────────────────────────────────────────────────────────────────────────────

#include "LleSession.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace pom68k::fw {

// What one device is asking for. `logTag` prefixes the stderr notices so a
// terminal user still sees which board spoke, exactly as before.
struct Request {
    lle::Module module = lle::HleEgretCuda;
    std::string name;                        // window row title
    std::string enableKnob;                  // POM68K_EGRET_LLE / _CUDA_LLE
    std::string pathKnob;                    // POM68K_CUDA_FW / POM68K_ADB_FW
    std::string logTag;                      // "V8", "Q605", "AdbVia"
    std::vector<std::string> candidates;     // factory part first
};

// The device's own loader: it alone knows what a valid image is for its MCU
// core, so the search never inspects the bytes it is handing over.
using Loader = std::function<bool(const std::vector<std::uint8_t>&)>;

namespace detail {
inline bool readFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return !out.empty();
}
} // namespace detail

// Runs the search, reports the outcome to the registry, and returns whether
// firmware is running. Reporting HLE is still what poisons product mode.
inline bool select(const Request& req, const Loader& load) {
    const char* enable = req.enableKnob.empty()
                             ? nullptr : std::getenv(req.enableKnob.c_str());
    const bool wanted = !enable || std::atoi(enable) != 0;

    const char* forced = req.pathKnob.empty()
                             ? nullptr : std::getenv(req.pathKnob.c_str());
    const std::string forcedPath = forced ? forced : "";

    std::string loaded;
    if (wanted) {
        std::vector<std::uint8_t> image;
        if (!forcedPath.empty()) {
            if (detail::readFile(forcedPath, image) && load(image))
                loaded = forcedPath;
            else
                std::fprintf(stderr,
                             "%s: %s=%s inutilisable — retour à la liste "
                             "d'origine\n", req.logTag.c_str(),
                             req.pathKnob.c_str(), forcedPath.c_str());
        }
        for (const std::string& p : req.candidates) {
            if (!loaded.empty()) break;
            if (detail::readFile(p, image) && load(image)) loaded = p;
        }
        if (loaded.empty())
            std::fprintf(stderr,
                         "%s: no MCU firmware dump found — running the "
                         "NON-CONFORMANT HLE substitute "
                         "(docs/LLE_VS_HLE.md §2)\n", req.logTag.c_str());
    } else {
        std::fprintf(stderr,
                     "%s: %s=0 — NON-CONFORMANT HLE substitute forced\n",
                     req.logTag.c_str(), req.enableKnob.c_str());
    }

    lle::Device d;
    d.module = req.module;
    d.name = req.name;
    d.knob = req.enableKnob;
    d.mode = loaded.empty() ? lle::Mode::Hle : lle::Mode::Lle;
    d.why = !loaded.empty() ? lle::Why::LleFirmware
            : wanted        ? lle::Why::HleNoDump
                            : lle::Why::HleForced;
    d.firmware = loaded;
    d.candidates = req.candidates;
    d.pathKnob = req.pathKnob;
    d.firmwareForced = forcedPath;
    lle::report(d);
    return !loaded.empty();
}

// Every dump the window may offer for a device: the candidate paths that
// exist, plus every other file sitting in the same directories -- a user who
// dumped a different revision of the same part should be able to pick it
// without editing a source file.
//
// Deduplicated by FILENAME, because a candidate list carries each path twice
// ("roms/cuda/x.bin" and "../roms/cuda/x.bin") to work from either the repo
// root or `build/`; only one of the two bases exists in any given run, and
// treating them as one entry is what keeps the picker from showing doubles.
// Sorted, so the order does not depend on the filesystem's.
inline std::vector<std::string>
discoverDumps(const std::vector<std::string>& candidates) {
    namespace fs = std::filesystem;
    std::vector<std::string> found;
    auto haveName = [&](const std::string& name) {
        for (const std::string& f : found)
            if (fs::path(f).filename().string() == name) return true;
        return false;
    };
    for (const std::string& c : candidates) {
        std::error_code ec;
        const fs::path dir = fs::path(c).parent_path();
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!e.is_regular_file(ec)) continue;
            const std::string name = e.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (haveName(name)) continue;
            found.push_back(e.path().generic_string());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

} // namespace pom68k::fw
