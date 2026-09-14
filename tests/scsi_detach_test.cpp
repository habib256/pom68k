// POM68K — the cable coming out: a fixed SCSI target leaves the bus live
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// docs/SCSI_HOTPLUG.md § 7. What the two controllers and the target promise
// when a fixed disk is detached with the machine running:
//
//   1. Between sessions, detach() empties the slot: the next selection of
//      that ID times out the way it does for any ID nothing answers —
//      5380: bus stays free, no REQ; 53C96: I_DISCONNECT — and nothing
//      else on the bus moves.
//   2. Inside a session, detach() is refused and changes nothing: the
//      session completes normally, then the detach goes through. This is
//      the rule MachineHost's retry relies on (machinehost_test pins the
//      retry itself).
//   3. ScsiDisk::close() makes the target absent (present() false, no
//      blocks, empty write log) and the same object re-opens and answers
//      again — the slot is reusable, not burnt.
//   4. At board level, detachScsi() takes a fixed disk and refuses the boot
//      ID, a CD bay and an empty ID.
//
// Synthetic 4 MB HFS image (src/HfsBlankVolume.h); no ROM, no asset.

#include "HfsBlankVolume.h"
#include "Ncr5380.h"
#include "Ncr53c96.h"
#include "PortableEnv.h"
#include "Q605Memory.h"
#include "ScsiDisk.h"
#include "V8Memory.h"

#include <cstdio>
#include <string>
#include <vector>

#define CHECK(c, ...) do { if(!(c)){ std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
    std::fprintf(stderr, "\n"); return 1; } } while (0)

namespace {

constexpr uint64_t kBytes = 4ull << 20;
constexpr int kId = 2;

// ── NCR 5380: the Plus/SE-family initiator, byte by byte ─────────────────
bool req5380(Ncr5380& s) { return s.read(Ncr5380::R_CSR) & Ncr5380::CBS_REQ; }
bool bsy5380(Ncr5380& s) { return s.read(Ncr5380::R_CSR) & Ncr5380::CBS_BSY; }
void send5380(Ncr5380& s, uint8_t b) {
    s.write(Ncr5380::R_DATA, b);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);
    s.write(Ncr5380::R_ICR, 0);
}
uint8_t recv5380(Ncr5380& s) {
    uint8_t b = s.read(Ncr5380::R_DATA);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);
    s.write(Ncr5380::R_ICR, 0);
    return b;
}
// Arbitrate and select `id`; true when a target answered (REQ for COMMAND).
bool select5380(Ncr5380& s, int id) {
    s.write(Ncr5380::R_DATA, 0x80);
    s.write(Ncr5380::R_MODE, Ncr5380::MODE_ARBITRATE);
    s.write(Ncr5380::R_DATA, uint8_t(0x80 | (1u << id)));
    s.write(Ncr5380::R_MODE, 0);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    return req5380(s);
}
// TEST UNIT READY on a selected target through STATUS and MESSAGE IN.
int finish5380(Ncr5380& s) {
    const uint8_t cdb[6] = {0, 0, 0, 0, 0, 0};
    for (uint8_t b : cdb) send5380(s, b);
    const uint8_t status = recv5380(s);
    const uint8_t msg = recv5380(s);
    CHECK(status == 0x00 && msg == 0x00, "5380: TEST UNIT READY GOOD (%02X/%02X)",
          status, msg);
    CHECK(!bsy5380(s), "5380: bus free after the session");
    return 0;
}

// ── NCR 53C96: the Quadra initiator, FIFO and command register ───────────
using R = Ncr53c96;
void select53c96(R& s, int id) {
    s.write(R::R_COMMAND, R::CM_FLUSH_FIFO);
    s.write(R::R_FIFO, 0xC0);                          // IDENTIFY, LUN 0
    for (int i = 0; i < 6; i++) s.write(R::R_FIFO, 0); // TEST UNIT READY
    s.write(R::R_STATUS, uint8_t(id));                 // destination bus id
    s.write(R::R_COMMAND, R::CD_SELECT_ATN);
}
int finish53c96(R& s) {
    (void)s.read(R::R_ISTAT);
    s.write(R::R_COMMAND, R::CI_COMPLETE);
    const uint8_t ist = s.read(R::R_ISTAT);
    CHECK(ist & R::I_FUNCTION, "53C96: COMPLETE latched status (ISTAT %02X)", ist);
    const uint8_t status = s.read(R::R_FIFO);
    const uint8_t msg = s.read(R::R_FIFO);
    CHECK(status == 0x00 && msg == 0x00, "53C96: TEST UNIT READY GOOD (%02X/%02X)",
          status, msg);
    s.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
    const uint8_t d = s.read(R::R_ISTAT);
    CHECK(d & R::I_DISCONNECT, "53C96: bus free after the session (ISTAT %02X)", d);
    return 0;
}

int test5380(const std::string& img) {
    ScsiDisk boot, disk;
    CHECK(boot.open(img) && disk.open(img), "5380: open the synthetic image");
    Ncr5380 s;
    s.reset();
    s.attach(&boot, 0);
    s.attach(&disk, kId);

    // 1. answers, then between sessions the detach empties the slot
    CHECK(select5380(s, kId), "5380: attached target answers selection");
    CHECK(finish5380(s) == 0, "5380: first session");
    CHECK(!s.sessionOn(kId), "5380: no session between transactions");
    CHECK(s.detach(kId), "5380: detach between sessions");
    CHECK(s.target(kId) == nullptr, "5380: slot empty after detach");
    CHECK(!select5380(s, kId), "5380: selection of the detached ID times out (no REQ)");
    CHECK(!bsy5380(s), "5380: bus free after the timeout");
    CHECK(select5380(s, 0), "5380: the boot target still answers");
    CHECK(finish5380(s) == 0, "5380: boot target session after the detach");

    // 2. inside a session the detach is refused and the session completes
    s.attach(&disk, kId);
    CHECK(select5380(s, kId), "5380: re-attached target answers");
    CHECK(s.sessionOn(kId), "5380: session open after selection");
    CHECK(!s.detach(kId), "5380: detach refused mid-session");
    CHECK(s.target(kId) == &disk, "5380: refused detach changed nothing");
    CHECK(finish5380(s) == 0, "5380: the session completes after the refusal");
    CHECK(s.detach(kId), "5380: detach once the bus is free");
    CHECK(!s.detach(kId), "5380: detaching an empty slot changes nothing");
    CHECK(!s.detach(7) && !s.detach(-1), "5380: out-of-range IDs refused");
    return 0;
}

int test53c96(const std::string& img) {
    ScsiDisk boot, disk;
    CHECK(boot.open(img) && disk.open(img), "53C96: open the synthetic image");
    R s;
    s.reset();
    s.write(R::R_CONFIG1, 0x07);
    s.attach(&boot, 0);
    s.attach(&disk, kId);

    select53c96(s, kId);
    CHECK(s.sessionOn(kId), "53C96: session open after SELECT_ATN");
    CHECK(!s.detach(kId), "53C96: detach refused mid-session");
    CHECK(s.target(kId) == &disk, "53C96: refused detach changed nothing");
    CHECK(finish53c96(s) == 0, "53C96: the session completes after the refusal");
    CHECK(!s.sessionOn(kId), "53C96: no session after MSG_ACCEPT");
    CHECK(s.detach(kId), "53C96: detach between sessions");
    CHECK(s.target(kId) == nullptr, "53C96: slot empty after detach");
    select53c96(s, kId);
    const uint8_t ist = s.read(R::R_ISTAT);
    CHECK(ist & R::I_DISCONNECT, "53C96: selecting the detached ID raises I_DISCONNECT (%02X)",
          ist);
    CHECK(!s.sessionOn(kId), "53C96: no session on a timed-out selection");
    select53c96(s, 0);
    CHECK(finish53c96(s) == 0, "53C96: the boot target still answers");
    return 0;
}

int testTarget(const std::string& img) {
    ScsiDisk d;
    CHECK(d.open(img), "target: open");
    // A bare HFS image gets the DDM/partition-map façade in front (+96 blocks).
    CHECK(d.present() && d.blocks() == kBytes / 512 + d.hfsPrefixBlocks(),
          "target: present with %llu + %u blocks", (unsigned long long)(kBytes / 512),
          d.hfsPrefixBlocks());
    // A write the guest made, logged for save states — close() drops it.
    std::vector<uint8_t> out;
    const std::vector<uint8_t> in(512, 0x5A);
    const uint8_t wr[6] = {0x0A, 0, 0, 8, 1, 0};                // WRITE(6) lba 8
    d.command(wr, 6, out, in);
    CHECK(d.dirtyBlocks() == 1, "target: one dirty block before close");
    d.close();
    CHECK(!d.present() && d.blocks() == 0 && d.dirtyBlocks() == 0 && !d.cdrom(),
          "target: absent, empty and fixed-kind after close");
    CHECK(d.image().empty(), "target: the image is released");
    CHECK(d.open(img), "target: the same object re-opens");
    CHECK(d.present() && d.dirtyBlocks() == 0, "target: present and pristine again");
    const uint8_t rd[6] = {0x08, 0, 0, 8, 1, 0};                // READ(6) lba 8
    CHECK(d.command(rd, 6, out, in) == 0 && out.size() == 512 && out[0] != 0x5A,
          "target: the re-opened image is the file, not the closed session's write");
    return 0;
}

template <class Mem, class Ctrl>
int testBoard(const char* name, Mem& mem, Ctrl& (Mem::*scsi)(), const std::string& img) {
    CHECK(mem.attachScsi(img, false, 0), "%s: boot disk", name);
    CHECK(mem.attachScsi(img, false, kId), "%s: second fixed disk", name);
    CHECK(mem.attachCdromEmpty(3), "%s: empty CD bay", name);
    CHECK(!mem.detachScsi(0), "%s: the boot ID is never detached", name);
    CHECK(!mem.detachScsi(3), "%s: a CD bay is not detached (ejectBayMedia)", name);
    CHECK(!mem.detachScsi(4), "%s: an empty ID has nothing to detach", name);
    CHECK((mem.*scsi)().target(kId) != nullptr, "%s: target on the bus", name);
    CHECK(mem.detachScsi(kId), "%s: the fixed disk detaches", name);
    CHECK((mem.*scsi)().target(kId) == nullptr, "%s: slot empty on the controller", name);
    CHECK(!mem.scsiDiskAt(kId).present(), "%s: the ScsiDisk is absent", name);
    CHECK(!mem.detachScsi(kId), "%s: a second detach has nothing to do", name);
    CHECK(mem.attachScsi(img, false, kId), "%s: the slot takes a new disk", name);
    CHECK((mem.*scsi)().target(kId) != nullptr, "%s: back on the bus", name);
    CHECK((mem.*scsi)().target(0) != nullptr && (mem.*scsi)().target(3) != nullptr,
          "%s: the other targets were left alone", name);
    return 0;
}

} // namespace

int main() {
    const std::string img = pom68kTempPath("scsi_detach.img");
    if (!hfsblank::writeFile(img, hfsblank::build(kBytes, "Retire"))) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", img.c_str());
        return 1;
    }
    int rc = test5380(img);
    if (!rc) rc = test53c96(img);
    if (!rc) rc = testTarget(img);
    if (!rc) {
        static V8Memory v8(pom68k::defaultCoreConfig(), 10u << 20);
        rc = testBoard<V8Memory, Ncr5380>("V8", v8, &V8Memory::scsi, img);
    }
    if (!rc) {
        static Q605Memory q605(pom68k::defaultCoreConfig(), 32u << 20);
        rc = testBoard<Q605Memory, Ncr53c96>("Q605", q605, &Q605Memory::scsi, img);
    }
    std::remove(img.c_str());
    if (rc) return rc;
    std::printf("PASS: a fixed target leaves both buses between sessions, never inside one, "
                "and its slot is reusable\n");
    return 0;
}
