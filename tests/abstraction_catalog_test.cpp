// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: the abstraction catalogue (src/AbstractionCatalog.*) against the
// tree it describes and the document it mirrors.
//
//   * every row is well formed, its id unique, its group one contiguous run;
//   * every file a row names exists in the repository;
//   * every platform kind, every lle::Module and every firmware::kManifest
//     entry is covered by at least one row;
//   * docs/LLE_VS_HLE.md's master table lists exactly the same ids with the
//     same class, fallback class and section, and every section it cites is
//     a heading of that document;
//   * resolve() reports what the registry says: firmware vs HLE fallback
//     with the dump that would raise it, absence on a machine that did not
//     build the part, the engine in use, a fallback-only hack engaged only
//     while its fallback runs.
//
// Runs from the source root (WORKING_DIRECTORY); needs no private asset.

#include "AbstractionCatalog.h"
#include "FirmwareManifest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace pom68k;
using namespace pom68k::abstraction;

int gFails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL %s\n", what.c_str());
        gFails++;
    }
}

std::vector<std::string> words(const char* text) {
    std::istringstream in(text);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t`");
    const auto e = s.find_last_not_of(" \t`");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::vector<std::string> cells(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    for (std::size_t i = 1; i < line.size(); ++i) {
        if (line[i] == '|') { out.push_back(trim(cell)); cell.clear(); }
        else cell += line[i];
    }
    return out;
}

void structure() {
    std::set<std::string> ids, closedGroups;
    std::string group;
    for (const Entry& e : catalogue()) {
        const std::string id = e.id;
        check(!id.empty() && ids.insert(id).second, "unique non-empty id: " + id);
        bool idChars = !id.empty();
        for (char c : id) idChars &= (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        check(idChars, "id is lower-case kebab: " + id);
        check(*e.group && *e.subsystem && *e.modelled && *e.whyNot && *e.files,
              "every text field filled: " + id);
        check(e.platforms != 0 && (e.platforms & ~kAllPlatforms) == 0,
              "platform mask non-empty and in range: " + id);
        if (group != e.group) {
            check(!closedGroups.count(e.group), "group is one contiguous run: " + std::string(e.group));
            if (!group.empty()) closedGroups.insert(group);
            group = e.group;
        }
        for (const std::string& f : words(e.files))
            check(std::filesystem::exists(f), id + " names an existing file: " + f);
        // Firmware rows and their fallbacks are coherent.
        if (e.live == LiveSource::Firmware) {
            check(levelIsLle(e.level), id + ": a firmware row's class is the firmware path");
            check(!e.hasFallback || !levelIsLle(e.fallback),
                  id + ": its fallback is an HLE class");
        } else {
            check(!e.hasFallback, id + ": only firmware rows have a fallback");
        }
        for (SnapMachine m : e.exceptProfiles) {
            const MachineProfile* p = profileFor(m);
            check(p && (e.platforms & platformBit(p->platform)),
                  id + ": an excepted profile belongs to a listed platform");
        }
        // The window draws these strings with a font that has only Latin-1.
        for (const char* s : {e.subsystem, e.modelled, e.whyNot, e.group})
            for (const char* c = s; *c; ++c)
                check((unsigned char)*c < 0x80, id + ": drawn text is ASCII");
    }
    check(catalogue().size() >= 30, "the catalogue is not a stub");
}

void coverage() {
    for (unsigned k = 0; k <= unsigned(PlatformKind::Portable); ++k) {
        bool covered = false;
        for (const Entry& e : catalogue()) covered |= (e.platforms >> k) & 1;
        check(covered, "platform kind " + std::to_string(k) + " is covered");
    }
    for (lle::Module m : {lle::HleEgretCuda, lle::HleAdbModem, lle::HleTobyDeclRom,
                          lle::PmuFirmware, lle::PbVideoRom}) {
        bool covered = false;
        for (const Entry& e : catalogue())
            covered |= e.live == LiveSource::Firmware && e.module == m;
        check(covered, "lle::Module " + std::to_string(unsigned(m)) + " has a firmware row");
    }
    for (const firmware::Entry& f : firmware::kManifest) {
        int rows = 0;
        for (const Entry& e : catalogue())
            for (std::string_view label : e.manifest) rows += label == f.label;
        check(rows == 1, std::string("manifest entry covered by exactly one row: ") + f.label);
    }
    for (const Entry& e : catalogue())
        for (std::string_view label : e.manifest) {
            bool known = false;
            for (const firmware::Entry& f : firmware::kManifest) known |= label == f.label;
            check(known, std::string(e.id) + " cites a manifest label that exists: " +
                             std::string(label));
        }
    // Every profile carries at least the CPU, the board logic and video.
    for (const MachineProfile& p : kMachineProfiles) {
        int rows = 0;
        for (const Entry& e : catalogue()) rows += presentOn(e, p.snapshot);
        check(rows >= 8, std::string("profile has a plausible row count: ") + p.label);
    }
}

void document() {
    std::ifstream in("docs/LLE_VS_HLE.md");
    check(bool(in), "docs/LLE_VS_HLE.md is readable from the source root");
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);

    std::map<std::string, std::vector<std::string>> table;
    bool inside = false, sawBegin = false, sawEnd = false;
    for (const std::string& l : lines) {
        if (l.find("<!-- abstraction-catalogue:begin -->") != std::string::npos) {
            inside = sawBegin = true;
            continue;
        }
        if (l.find("<!-- abstraction-catalogue:end -->") != std::string::npos) {
            inside = false;
            sawEnd = true;
            continue;
        }
        if (!inside || l.rfind("| `", 0) != 0) continue;
        const std::vector<std::string> c = cells(l);
        if (c.size() != 5) { check(false, "master-table row has five cells: " + l); continue; }
        check(table.emplace(c[0], c).second, "master-table id listed once: " + c[0]);
    }
    check(sawBegin && sawEnd, "the master table has its begin/end markers");

    auto heading = [&](const std::string& section) {
        for (const std::string& l : lines) {
            if (l.empty() || l[0] != '#') continue;
            const std::size_t at = l.find_first_not_of("# ");
            if (at == std::string::npos || l.compare(at, section.size(), section) != 0) continue;
            const char next = at + section.size() < l.size() ? l[at + section.size()] : ' ';
            if (next == ' ' || next == '.') return true;
        }
        return false;
    };

    for (const Entry& e : catalogue()) {
        const auto it = table.find(e.id);
        check(it != table.end(), std::string("the doc's master table lists ") + e.id);
        if (it == table.end()) continue;
        const std::vector<std::string>& c = it->second;
        check(c[2] == levelName(e.level),
              std::string(e.id) + ": class agrees (doc '" + c[2] + "')");
        const std::string fallback = e.hasFallback ? levelName(e.fallback)
            : e.live == LiveSource::Firmware ? "required" : "-";
        check(c[3] == fallback, std::string(e.id) + ": fallback agrees (doc '" + c[3] + "')");
        check(c[4] == e.docSection, std::string(e.id) + ": section agrees (doc '" + c[4] + "')");
        if (std::string(e.docSection) != "-")
            check(heading(e.docSection),
                  std::string(e.id) + ": section " + e.docSection + " is a heading of the doc");
    }
    for (const auto& [id, c] : table)
        check(find(id) != nullptr, "the catalogue has the doc's row " + id);
}

lle::Device device(lle::Module module, lle::Mode mode) {
    lle::Device d;
    d.module = module;
    d.mode = mode;
    d.why = mode == lle::Mode::Lle ? lle::Why::LleFirmware : lle::Why::HleNoDump;
    if (mode == lle::Mode::Lle) d.firmware = "roms/cuda/341s0788.bin";
    d.candidates = {"roms/cuda/341s0788.bin", "../roms/cuda/341s0788.bin"};
    return d;
}

void live() {
    const Entry* egret = find("egret-cuda");
    const Entry* adb = find("adb-transceiver");
    const Entry* hack = find("adb-slot-shift-hack");
    const Entry* engine = find("cpu-engine");
    const Entry* via = find("via");
    check(egret && adb && hack && engine && via, "the live rows exist");
    if (!(egret && adb && hack && engine && via)) return;

    SessionView q605;
    q605.machine = SnapMachine::Q605;
    q605.devices = {device(lle::HleEgretCuda, lle::Mode::Lle)};
    LiveRow r = resolve(*egret, q605);
    check(r.present && r.known && r.active == Level::PureLle && !r.degraded &&
              r.detail.find("341s0788.bin") != std::string::npos,
          "Cuda on its firmware: Pure LLE, naming the dump");

    q605.devices = {device(lle::HleEgretCuda, lle::Mode::Hle)};
    r = resolve(*egret, q605);
    check(r.present && r.degraded && r.active == Level::HleReplacement &&
              r.hint.find("roms/cuda/341s0788.bin") != std::string::npos,
          "Cuda on its fallback: HLE replacement, hint names the expected dump");

    SessionView q700;  // a Spike board that built no Egret
    q700.machine = SnapMachine::Q700;
    q700.devices = {device(lle::HleAdbModem, lle::Mode::Lle)};
    check(!resolve(*egret, q700).present, "no report, no row: the Quadra 700 has no Egret");
    check(resolve(*adb, q700).present, "the Quadra 700's ADB transceiver is present");

    SessionView plus;
    plus.machine = SnapMachine::Plus;
    check(!resolve(*adb, plus).present && !presentOn(*find("adb-devices"), SnapMachine::Plus),
          "the Plus has no ADB");
    check(resolve(*via, plus).present && !resolve(*via, plus).known,
          "a static row is present and reports no live state");
    check(!presentOn(*find("scsi-5380"), SnapMachine::Mac128K) &&
              presentOn(*find("scsi-5380"), SnapMachine::Plus),
          "SCSI arrives with the Plus");

    SessionView macii;
    macii.machine = SnapMachine::MacII;
    macii.devices = {device(lle::HleAdbModem, lle::Mode::Lle)};
    r = resolve(*hack, macii);
    check(r.present && r.known && !r.engaged && !r.degraded,
          "the Slot Manager hack is dormant under firmware LLE");
    macii.devices = {device(lle::HleAdbModem, lle::Mode::Hle)};
    r = resolve(*hack, macii);
    check(r.engaged && r.degraded && !r.hint.empty(),
          "the Slot Manager hack is engaged on the ADB fallback");
    check(!resolve(*hack, q605).present, "the hack exists only on the GLUE board");

    q605.engine = 1;
    q605.backend = "x64";
    q605.strictRequested = q605.strictQualified = true;
    r = resolve(*engine, q605);
    check(r.known && r.state == "JIT x64" && r.detail.find("strict") != std::string::npos &&
              r.active == Level::PureLle,
          "the engine row names the backend and the strict lock");
    q605.engine = 0;
    check(resolve(*engine, q605).state == "interpreter", "the engine row names the interpreter");

    SessionView none;  // no machine: show every row, nothing live
    check(resolve(*egret, none).present && !resolve(*egret, none).known,
          "with no machine bound every row is shown as classified");
}

} // namespace

int main() {
    std::printf("abstraction_catalog_test\n");
    structure();
    coverage();
    document();
    live();
    std::printf("abstraction_catalog_test: %zu rows, %s\n", catalogue().size(),
                gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
