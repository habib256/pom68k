// POM68K — a save state belongs to one disk content, in any process
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// DiskTimeline.h's contract, on the real ScsiDisk and AtaDisk with the real
// sav::Writer/Reader. A state is a pair (RAM, disk content); restoring it
// must reproduce that content exactly — in memory AND in a write-back file —
// or be refused by name, never combine old RAM with later disk data.
//
//   1. Write-back off, same process: writes after the save are undone.
//   2. Write-back on, fresh process: the backing file holds later writes;
//      the `.pomundo` journal rewinds it, memory and file both.
//   3. Two states, restored in alternation across processes: the restore's
//      own writes keep the journal linear, so every state stays reachable.
//   4. Changed base (write-back off, image replaced): refused, reason given.
//   5. Journal deleted: refused, reason names the journal.
//   6. Torn journal tail (crash mid-record): trimmed at attach, still works.
//   7. Compaction at a small cap: the oldest state is refused, the newest
//      restores.
//   8. Medium topology: a state with a disk refuses a machine without one;
//      a state without a disk accepts one.
//   9. ATA: the same cross-process rewind, with a guest WRITE SECTORS.
//  10. Journal not creatable: same-process restore still exact; a
//      cross-process one is refused rather than guessed.
//
// Scratch images only (never hdv/ref); no ROM, no asset.

#include "AtaDisk.h"
#include "DiskTimeline.h"
#include "SaveState.h"
#include "ScsiDisk.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

constexpr uint32_t kBlocks = 256;               // 128 KB
constexpr uint32_t kBs = 512;

fs::path scratch;

std::string makeImage(const char* name, uint8_t fill) {
    const fs::path p = scratch / name;
    std::vector<uint8_t> img(size_t(kBlocks) * kBs, fill);
    for (uint32_t b = 0; b < kBlocks; ++b) img[size_t(b) * kBs + 7] = uint8_t(b);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
    fs::remove(p.string() + ".pomundo");
    return p.string();
}

std::vector<uint8_t> fileBytes(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

template <class D> std::vector<uint8_t> save(D& d) {
    std::vector<uint8_t> out;
    sav::Writer w(out);
    w(d);
    return out;
}

template <class D> bool load(D& d, const std::vector<uint8_t>& s,
                             std::string* why = nullptr) {
    sav::Reader r(s.data(), s.size());
    r(d);
    if (why) *why = r.reason();
    return r.ok() && !r.remaining();
}

void put(ScsiDisk& d, uint32_t lba, uint8_t v, uint32_t count = 1) {
    std::vector<uint8_t> b(size_t(count) * kBs, v);
    d.hostWrite(lba, b.data(), count);
}

// A guest WRITE(10), through the target's command path.
void guestWrite(ScsiDisk& d, uint32_t lba, uint8_t v) {
    uint8_t cdb[10] = {0x2A, 0, uint8_t(lba >> 24), uint8_t(lba >> 16),
                       uint8_t(lba >> 8), uint8_t(lba), 0, 0, 1, 0};
    std::vector<uint8_t> out, in(kBs, v);
    d.command(cdb, 10, out, in);
}

bool same(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

// ── 1 ────────────────────────────────────────────────────────────────────
void sameProcess() {
    std::puts("1. write-back off, same process");
    const std::string p = makeImage("s1.img", 0x5A);
    ScsiDisk d;
    check(d.open(p, false), "opens");
    put(d, 3, 0x11);
    guestWrite(d, 4, 0x22);
    const auto atSave = d.image();
    const auto st = save(d);
    put(d, 3, 0x33);
    put(d, 9, 0x44, 4);
    std::string why;
    check(load(d, st, &why), "restores");
    check(same(d.image(), atSave), "content equals the state's");
    check(save(d) == st, "load→save is byte-identical");
    check(!fs::exists(p + ".pomundo"), "no journal without write-back");
}

// ── 2 + 3 ────────────────────────────────────────────────────────────────
void crossProcess() {
    std::puts("2. write-back on, fresh process rewinds the file");
    const std::string p = makeImage("s2.img", 0x5A);
    std::vector<uint8_t> s1, s2, c1, c2;
    {
        ScsiDisk d;
        check(d.open(p, true), "opens write-back");
        check(d.timeline().journalling(), "journal active");
        put(d, 1, 0x10);
        guestWrite(d, 2, 0x20);
        c1 = d.image();
        s1 = save(d);
        put(d, 1, 0x30);                         // after S1
        put(d, 50, 0x31, 8);
        c2 = d.image();
        s2 = save(d);
        put(d, 50, 0x40, 2);                     // after S2
        put(d, 100, 0x41);
    }                                            // "quit"
    {
        ScsiDisk d;
        check(d.open(p, true), "reopens");
        check(!same(d.image(), c1), "file holds the later writes");
        std::string why;
        check(load(d, s1, &why), "S1 restores in a new process");
        if (!why.empty()) std::printf("     reason: %s\n", why.c_str());
        check(same(d.image(), c1), "memory equals S1's content");
        check(same(fileBytes(p), c1), "backing file rewound to S1");
        // Across write-back processes the since-open log is relative to a
        // different opened file, so the payload differs; the content does not.
        const auto again = save(d);
        ScsiDisk e;
        e.open(p, false);
        check(load(e, again) && same(e.image(), c1), "re-saved state restores the same content");
    }
    std::puts("3. alternate states across processes");
    {
        ScsiDisk d;
        check(d.open(p, true), "reopens");
        check(load(d, s2), "S2 restores after S1's rewind");
        check(same(d.image(), c2) && same(fileBytes(p), c2), "content and file equal S2");
        put(d, 7, 0x77);
        check(load(d, s1), "S1 again, same process");
        check(same(d.image(), c1) && same(fileBytes(p), c1), "content and file equal S1");
    }
    {
        ScsiDisk d;
        check(d.open(p, true), "reopens");
        check(load(d, s2), "S2 again, another process");
        check(same(fileBytes(p), c2), "file equals S2");
    }

    std::puts("5. journal deleted");
    {
        ScsiDisk d;
        check(d.open(p, true), "reopens");
        put(d, 200, 0x99);                       // diverge from S2
        const auto st = save(d);
        put(d, 201, 0x98);
        (void)st;
    }
    fs::remove(p + ".pomundo");
    {
        ScsiDisk d;
        check(d.open(p, true), "reopens");
        const auto before = d.image();
        std::string why;
        check(!load(d, s1, &why), "S1 refused without its journal");
        check(why.find("journal") != std::string::npos, "the reason names the journal");
        std::printf("     reason: %s\n", why.c_str());
        check(same(d.image(), before) && same(fileBytes(p), before),
              "a refusal changes neither memory nor file");
    }
}

// ── 4 ────────────────────────────────────────────────────────────────────
void changedBase() {
    std::puts("4. changed base without write-back");
    const std::string p = makeImage("s4.img", 0x5A);
    std::vector<uint8_t> st;
    {
        ScsiDisk d;
        d.open(p, false);
        put(d, 5, 0x55);
        st = save(d);
    }
    {
        ScsiDisk d;
        d.open(p, false);
        check(load(d, st), "unchanged base: restores in a fresh process");
    }
    makeImage("s4.img", 0x6B);                   // same size, other content
    {
        ScsiDisk d;
        d.open(p, false);
        std::string why;
        check(!load(d, st, &why), "different image of the same size: refused");
        check(why.find("differs") != std::string::npos, "the reason says the content differs");
        std::printf("     reason: %s\n", why.c_str());
    }
}

// ── 6 ────────────────────────────────────────────────────────────────────
void tornTail() {
    std::puts("6. torn journal tail");
    const std::string p = makeImage("s6.img", 0x5A);
    std::vector<uint8_t> st, c;
    {
        ScsiDisk d;
        d.open(p, true);
        put(d, 9, 0x19);
        c = d.image();
        st = save(d);
        put(d, 9, 0x29);
    }
    const auto good = fs::file_size(p + ".pomundo");
    {
        std::ofstream j(p + ".pomundo", std::ios::binary | std::ios::app);
        const char torn[] = {'B', 1, 0, 0, 0, 0x42, 0x42};   // lba 1, 2 of 512 bytes
        j.write(torn, sizeof torn);
    }
    {
        ScsiDisk d;
        check(d.open(p, true), "reopens over a torn tail");
        check(fs::file_size(p + ".pomundo") == good, "torn record trimmed");
        put(d, 10, 0x1A);                        // appends after the trim
        check(load(d, st), "the state restores");
        check(same(d.image(), c) && same(fileBytes(p), c), "content and file equal it");
    }
}

// ── 7 ────────────────────────────────────────────────────────────────────
void compaction() {
    std::puts("7. compaction at a small cap");
    const std::string p = makeImage("s7.img", 0x5A);
    std::vector<uint8_t> old, recent, cRecent;
    {
        ScsiDisk d;
        d.open(p, true);
        put(d, 0, 0x01);
        old = save(d);
        for (int round = 0; round < 6; ++round) put(d, 10 + round * 10, 0x02, 10);
        cRecent = d.image();
        recent = save(d);
        put(d, 250, 0x03, 2);
    }
    const auto big = fs::file_size(p + ".pomundo");
    // The cap is checked at attach, so it is set before open().
    {
        ScsiDisk d;
        d.timeline().setJournalCap(8 * 1024);
        d.open(p, true);
        check(fs::file_size(p + ".pomundo") < big, "journal compacted");
        std::string why;
        check(!load(d, old, &why), "the oldest state is refused");
        std::printf("     reason: %s\n", why.c_str());
        check(load(d, recent), "the newest state restores");
        check(same(d.image(), cRecent) && same(fileBytes(p), cRecent), "content and file equal it");
    }
}

// ── 8 ────────────────────────────────────────────────────────────────────
void topology() {
    std::puts("8. medium topology");
    const std::string p = makeImage("s8.img", 0x5A);
    ScsiDisk with, without;
    with.open(p, false);
    const auto stWith = save(with);
    const auto stWithout = save(without);
    std::string why;
    check(!load(without, stWith, &why), "state with a disk: refused on an empty slot");
    std::printf("     reason: %s\n", why.c_str());
    check(load(with, stWithout), "state without a disk: accepted with one attached");
}

// ── 9 ────────────────────────────────────────────────────────────────────
void ataWrite(AtaDisk& d, uint8_t lba, uint8_t v) {
    d.writeRegister(AtaDisk::kDevice, 0x40);
    d.writeRegister(AtaDisk::kSectorCount, 1);
    d.writeRegister(AtaDisk::kLbaLow, lba);
    d.writeRegister(AtaDisk::kLbaMid, 0);
    d.writeRegister(AtaDisk::kLbaHigh, 0);
    d.writeRegister(AtaDisk::kCommand, 0x30);    // WRITE SECTORS
    for (int i = 0; i < 256; ++i) d.writeData(uint16_t(v | (v << 8)));
}
uint8_t ataByte(const std::string& p, uint32_t lba) {
    const auto f = fileBytes(p);
    return f.size() > size_t(lba) * kBs ? f[size_t(lba) * kBs] : 0;
}

void ata() {
    std::puts("9. ATA cross-process rewind");
    const std::string p = makeImage("s9.img", 0x5A);
    std::vector<uint8_t> st;
    {
        AtaDisk d;
        check(d.open(p, true), "opens write-back");
        ataWrite(d, 4, 0x44);
        st = save(d);
        ataWrite(d, 4, 0x45);
        ataWrite(d, 5, 0x55);
    }
    check(ataByte(p, 4) == 0x45 && ataByte(p, 5) == 0x55, "file holds the later writes");
    {
        AtaDisk d;
        d.open(p, true);
        std::string why;
        check(load(d, st, &why), "restores in a new process");
        if (!why.empty()) std::printf("     reason: %s\n", why.c_str());
        check(ataByte(p, 4) == 0x44 && ataByte(p, 5) == 0x5A, "file rewound");
    }
}

// ── 10 ───────────────────────────────────────────────────────────────────
void noJournal() {
    std::puts("10. journal cannot be created");
    const fs::path dir = scratch / "ro";
    fs::create_directories(dir);
    const fs::path img = dir / "s10.img";
    fs::copy_file(makeImage("s10src.img", 0x5A), img, fs::copy_options::overwrite_existing);
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
    std::vector<uint8_t> st, c;
    bool journalling = true;
    {
        ScsiDisk d;
        d.open(img.string(), true);
        journalling = d.timeline().journalling();
        put(d, 2, 0x22);
        c = d.image();
        st = save(d);
        put(d, 2, 0x23);
        check(load(d, st) && same(d.image(), c) && same(fileBytes(img.string()), c),
              "same process: exact, file included");
        put(d, 3, 0x24);       // outside the state's blocks: only a journal undoes it
    }
    if (journalling) {
        std::puts("     (directory writable for this user — cross-process half skipped)");
    } else {
        ScsiDisk d;
        d.open(img.string(), true);
        std::string why;
        check(!load(d, st, &why), "fresh process without a journal: refused");
        std::printf("     reason: %s\n", why.c_str());
    }
    fs::permissions(dir, fs::perms::owner_all);
}

// The digest is a function of content alone: two images with equal bytes
// agree, one changed byte anywhere disagrees, and a block swap is caught.
void digest() {
    std::puts("0. digest");
    std::vector<uint8_t> a(4 * kBs, 0), b;
    for (size_t i = 0; i < a.size(); ++i) a[i] = uint8_t(i * 7 + i / kBs);
    b = a;
    DiskTimeline ta, tb;
    ta.attach(a.data(), a.size(), kBs, "", "a");
    tb.attach(b.data(), b.size(), kBs, "", "b");
    check(ta.digest(a.data()) == tb.digest(b.data()), "equal content, equal digest");
    std::vector<uint8_t> next(a.begin() + kBs, a.begin() + 2 * kBs);
    next[100] ^= 1;
    tb.willWrite(1, b.data() + kBs, next.data());
    std::memcpy(b.data() + kBs, next.data(), kBs);
    check(ta.digest(a.data()) != tb.digest(b.data()), "one bit differs, digest differs");
    std::vector<uint8_t> s = a;
    std::swap_ranges(s.begin(), s.begin() + kBs, s.begin() + kBs);
    DiskTimeline ts;
    ts.attach(s.data(), s.size(), kBs, "", "s");
    check(ts.digest(s.data()) != ta.digest(a.data()), "swapped blocks, digest differs");
    // A direct poke (ScsiDisk::image()) bypasses willWrite; the digest is
    // recomputed from the bytes on its next use.
    std::memcpy(b.data() + kBs, a.data() + kBs, kBs);
    tb.imageTouched();
    check(tb.digest(b.data()) == ta.digest(a.data()), "recomputed after a direct poke");
}

}  // namespace

int main() {
    scratch = fs::temp_directory_path() /
              ("pom68k-media-timeline-" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch);
    digest();
    sameProcess();
    crossProcess();
    changedBase();
    tornTail();
    compaction();
    topology();
    ata();
    noJournal();
    fs::remove_all(scratch);
    std::printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
