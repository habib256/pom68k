// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// lcii_prober_oracle — the Retro68 differential oracle on the LC II
// (TODO § Preuve, CHANGELOG 2026-10-02 (night)).
//
// The guest-side POM68K Prober (dev/prober) writes its findings — Gestalt,
// low memory, the bus-error topology, devices — as a TSV next to itself the
// moment it launches. Put it in Startup Items of a copy of the LC II's
// locked reference volume, boot, and the report is in the image. Booting
// the SAME prepared image under MAME `maclc2` gives the other half
// (tools/prober_oracle.sh).
//
//   lcii_prober_oracle <out-dir> [frames]       the rig
//     <out-dir>/prepared.hd  the image after injection, before any boot
//     <out-dir>/pom68k.hd    the same image after the POM68K run
//     <out-dir>/pom68k.tsv   the Prober's report read back from it
//     <out-dir>/companion.hd the blank "Infinite HD" both sides attach
//   lcii_prober_oracle --extract <image> <out.tsv>
//     the report out of any image the Prober ran on (MAME's)
//   lcii_prober_oracle --check <maclc2.tsv>     the gate
//     runs the POM68K half and compares its report with MAME's, recorded
//     in tools/prober_oracle_maclc2.tsv, on every field both models can judge.
//
// The image is hdv/System 7.1 HD.dsk (the lock's `lcii` volume, routed to
// hdv/ref/), POM68K_BEYOND_IMG overriding it for exploration. The gate
// soft-skips without the ROM, that volume or the built Prober.

#include "AssetFingerprint.h"
#include "BeyondBoot.h"
#include "Cpu030.h"
#include "HfsInject.h"
#include "InfiniteHdCompanion.h"
#include "JitTestConfig.h"
#include "V8Memory.h"
#include "V8Video.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* const kReport = "POM68K Prober.txt";

// Fields the two models cannot both judge, each with the reason it is not
// evidence (CHANGELOG 2026-10-02 (night), (late night)). Everything else in
// MAME's report must appear in POM68K's with the same value, and nothing
// more.
const std::map<std::string, const char*> kUnjudged = {
    { "clock.macSeconds", "each side seeds its RTC differently" },
    { "clock.dateTime", "each side seeds its RTC differently" },
    { "ident.fpu", "MAME's 030 FPU produces 68881 FSAVE frames; the LC II socket takes a 68882" },
    { "ident.memTop", "follows the system-heap allocation order, a CPU-throughput symptom "
                      "(TODO § Fidélité, cacheBoost)" },
    { "probe.VIA1@Plus", "unmapped on the LC II; maclc raises no bus error there" },
    { "probe.VIA2@II", "unmapped on the LC II; maclc raises no bus error there" },
};

std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), std::streamsize(size));
    return bool(out);
}

// The report's data fork as host text: CR → LF, MacRoman above $7F kept
// visible as \xNN (the Prober writes ASCII; a volume name may not be).
bool extractReport(hfsinject::BlockIo& io, std::string& text, std::string& err) {
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

Report parse(const std::string& text) {
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

int compare(const std::string& goldenText, const std::string& ours) {
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
        if (const auto u = kUnjudged.find(k); u != kUnjudged.end()) {
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
    std::printf("%d fields judged, %zu unjudged, %d differ\n", judged, kUnjudged.size(), bad);
    return bad;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--extract") == 0) {
        std::vector<uint8_t> image = readAll(argv[2]);
        hfsinject::MemoryIo io(image);
        std::string text, err;
        if (!extractReport(io, text, err)) {
            std::fprintf(stderr, "FAIL: %s: %s\n", argv[2], err.c_str());
            return 1;
        }
        return writeFile(argv[3], text.data(), text.size()) ? 0 : 1;
    }
    const bool check = argc == 3 && std::strcmp(argv[1], "--check") == 0;
    if (argc < 2 || (!check && argv[1][0] == '-')) {
        std::fprintf(stderr, "usage: lcii_prober_oracle <out-dir> [frames]\n"
                             "       lcii_prober_oracle --extract <image> <out.tsv>\n"
                             "       lcii_prober_oracle --check <maclc2.tsv>\n");
        return 2;
    }
    const std::string outDir = check ? std::string() : argv[1];
    const long frames = !check && argc > 2 ? std::atol(argv[2]) : 20000;

    const std::string rom = testasset::find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/System 7.1 HD.dsk");
    const std::string bin = testasset::find("dev/prober/build/POM68KProber.bin");
    if (rom.empty() || img.empty() || bin.empty()) {
        std::printf("SKIP: needs the LC II ROM, hdv/ref/System 7.1 HD.dsk and "
                    "dev/prober/build/POM68KProber.bin\n");
        return 0;
    }
    testasset::report({ rom, img });
    std::string golden;
    if (check) {
        const std::vector<uint8_t> g = readAll(argv[2]);
        if (g.empty()) { std::fprintf(stderr, "FAIL: cannot read %s\n", argv[2]); return 1; }
        golden.assign(g.begin(), g.end());
    }

    // MAME's Egret starts from a cold PRAM that the ROM initialises with
    // AppleTalk active (SPConfig port B = 1); POM68K's factory XPRAM seeds
    // it inactive ($22) unless asked. Seed it active here so both open .MPP.
    pom68k::CoreConfig config = pom68k::defaultCoreConfig();
    config.peripherals.appleTalkPram = true;
    V8Memory mem(config);
    if (!mem.loadRom(readAll(rom))) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    // MAME's maclc2 is run with its FPU socket filled (tools/prober_oracle.sh):
    // POM68K's default 68882 compares like for like. POM68K_NOFPU=1 boots the
    // bare machine, which does not reach the Finder yet (TODO § Fidélité).
    const bool withFpu = std::getenv("POM68K_NOFPU") == nullptr;
    Cpu030 cpu(mem, testjit::resolveFromEnvironment(), config.cpu,
               withFpu, false);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    // The Infinite Mac volume's Startup Items alias stops the Finder on an
    // alert before it reaches the Prober; the blank companion resolves it.
    // The rig keeps a copy so MAME boots with the same two disks.
    if (!infinitehd::attach(mem, img)) return 1;
    if (!check && infinitehd::wants(img)) {
        // As attached — inside the partition-map façade the ROM needs.
        const std::vector<uint8_t>& c = mem.scsiDiskAt(1).image();
        if (!writeFile(outDir + "/companion.hd", c.data(), c.size())) return 1;
    }

    hfsinject::MacBinary app;
    std::string err;
    if (!hfsinject::decodeMacBinary(readAll(bin), app, err)) {
        std::fprintf(stderr, "FAIL: %s: %s\n", bin.c_str(), err.c_str());
        return 1;
    }
    hfsinject::ScsiDiskIo io(mem.scsiDisk());
    // A fixed install date, so the prepared image is the same bytes every run.
    const hfsinject::Outcome o = hfsinject::installStartupItem(io, app, 3870700000u);
    std::printf("prober: %s\n", o.message.c_str());
    if (o.kind != hfsinject::Outcome::Installed &&
        o.kind != hfsinject::Outcome::AlreadyPresent) return 1;
    if (!check) {
        const std::vector<uint8_t>& d = mem.scsiDisk().image();
        if (!writeFile(outDir + "/prepared.hd", d.data(), d.size())) {
            std::fprintf(stderr, "FAIL: cannot write %s/prepared.hd\n", outDir.c_str());
            return 1;
        }
    }

    while (mem.cpuHeld()) mem.tick(1000);
    const int64_t kFrame = 640 * 407;        // 60.15 Hz @ 15.6672 MHz
    // The gate stops as soon as the whole report is on disk (looked for
    // every ten guest seconds); the rig runs its full budget, so its low
    // memory and image compare with MAME's after the same span.
    std::string ours;
    bool have = false;
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (check && f % 600 == 599 && extractReport(io, ours, err) && parse(ours).complete()) {
            std::printf("report complete after %ld frames\n", f + 1);
            have = true;
            break;
        }
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }

    if (!have && !extractReport(io, ours, err)) {
        std::vector<uint32_t> fb;
        V8Video video(mem);
        video.decode(fb);
        int w = 0, h = 0;
        video.size(w, h);
        beyondboot::dumpPpm("lcii_prober_oracle.ppm", fb, w, h);     // POM68K_DUMP=1
        std::fprintf(stderr, "FAIL: no Prober report in the image after %ld frames: %s\n",
                     frames, err.c_str());
        return 1;
    }
    if (check) {
        const int bad = compare(golden, ours);
        if (bad) { std::printf("FAIL: POM68K disagrees with MAME maclc2\n"); return 1; }
        std::printf("PASSED — the Prober reads the same LC II under POM68K and MAME\n");
        return 0;
    }

    const std::vector<uint8_t>& d = mem.scsiDisk().image();
    if (!writeFile(outDir + "/pom68k.hd", d.data(), d.size())) return 1;
    if (!writeFile(outDir + "/pom68k.tsv", ours.data(), ours.size())) return 1;
    // Low memory at the end of the run, for a byte-level comparison with
    // MAME's (CHANGELOG 2026-10-02 (late night) walked the system heap).
    std::vector<uint8_t> low(0x40000);
    for (uint32_t a = 0; a < low.size(); a++) low[a] = mem.peek8(a);
    writeFile(outDir + "/pom68k.lowmem", low.data(), low.size());
    std::printf("wrote %s/prepared.hd, pom68k.hd and pom68k.tsv after %ld frames\n",
                outDir.c_str(), frames);
    return 0;
}
