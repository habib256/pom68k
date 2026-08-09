// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.5 gate — LC II SCSI through the V8 bus: 53C80 registers at $F10000
// (stride $10), the pseudo-DMA read alias (reg 6 @ +$260), the wide
// DRQ-handshaked window at $F06000/$F12000, and the DRQ-gated /DSACK
// timeout that raises a BUS ERROR — the mechanism the SCSI Manager's
// blind MOVE.L loops rely on to terminate (macscsi.cpp:5-52;
// maclc.cpp:222-266). Extended with the MAME-parity checks of the 2026-08
// audit: DRQ mismatch asymmetry (a trailing PDMA read must NOT eat the
// status byte — machine/ncr5380.cpp:227-245), arbitration cancel
// (ncr5380.cpp:389-405), ICR.RST edge/latch/IRQ (ncr5380.cpp:330-355),
// and the MODE SELECT / FORMAT UNIT DATA OUT parameter phases
// (bus/nscsi/hd.cpp:601-631). Soft-skips without hdv/HD20SC.vhd.
// Exit 0 = pass, 1 = fail.

#include "AssetFingerprint.h"
#include "V8Memory.h"
#include "Cpu030.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

// 53C80 register access through the LC II map
constexpr uint32_t kRegBase = 0xF10000;
uint8_t rd(V8Memory& m, int reg) { return m.read8(kRegBase + uint32_t(reg) * 0x10); }
void wr(V8Memory& m, int reg, uint8_t v) { m.write8(kRegBase + uint32_t(reg) * 0x10, v); }
constexpr uint8_t kPhaseMask =
    Ncr5380::CBS_MSG | Ncr5380::CBS_CD | Ncr5380::CBS_IO;
} // namespace

int main() {
    std::printf("scsi_pdma_test — V8 SCSI pseudo-DMA + BERR timeout (O6.5)\n");
    std::string img = find("hdv/HD20SC.vhd");
    if (img.empty()) { std::printf("SKIP: hdv/HD20SC.vhd not found\n"); return 0; }
    testasset::report({ img });

    V8Memory mem;
    Cpu030 cpu(mem);
    mem.setCpu(&cpu);
    check(mem.attachScsi(img), "SCSI disk attached");

    // Clearing MODE_ARBITRATE cancels a pending arbitration and returns
    // the engine to bus-free (MAME ncr5380.cpp:389-405) — without it the
    // CSR reads BSY forever and no later selection can start.
    wr(mem, Ncr5380::R_DATA, 0x80);
    wr(mem, Ncr5380::R_MODE, Ncr5380::MODE_ARBITRATE);
    check(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_BSY, "arbitration: CSR shows BSY");
    check(rd(mem, Ncr5380::R_ICR) & Ncr5380::ICR_AIP, "arbitration: ICR shows AIP");
    wr(mem, Ncr5380::R_MODE, 0);
    check(rd(mem, Ncr5380::R_CSR) == 0, "cancelled arbitration: bus reads free");
    check(!(rd(mem, Ncr5380::R_ICR) & Ncr5380::ICR_AIP),
          "cancelled arbitration: AIP drops");

    // Audit § 2.6(d): ICR bits 6/5 are READ-ONLY status (AIP / lost
    // arbitration) — a write lands only in the low $9F (MAME icmd_w,
    // machine/ncr5380.cpp:355, IC_WRITE = $9f; write-side bit 6 is TEST
    // mode). Writing them back must not fake an "arbitration in progress",
    // and the latching bits below them must still take.
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_AIP | Ncr5380::ICR_LA);
    check(rd(mem, Ncr5380::R_ICR) == 0, "ICR bits 6/5 are read-only: write ignored");
    wr(mem, Ncr5380::R_ICR, uint8_t(Ncr5380::ICR_AIP | Ncr5380::ICR_ATN));
    check(rd(mem, Ncr5380::R_ICR) == Ncr5380::ICR_ATN,
          "the same write still latches the writable bits");
    check((rd(mem, Ncr5380::R_BSR) & Ncr5380::BSR_ATN) != 0,
          "and ATN reaches the Bus and Status register");
    wr(mem, Ncr5380::R_ICR, 0);
    // § 2.6(c), pinned divergence: PHASE MATCH reads 0 at BUS FREE even
    // though MAME's raw comparator (bas_r, ncr5380.cpp:475-486) would say
    // "match" for TCR = 0 = DATA OUT. Guard the deliberate reading.
    wr(mem, Ncr5380::R_TCR, 0x00);
    check(!(rd(mem, Ncr5380::R_BSR) & Ncr5380::BSR_PHASE),
          "no PHASE MATCH while the bus is free (deliberate, see phaseMatch)");

    // Arbitration + selection of target 0 (SCSI Manager sequence)
    wr(mem, Ncr5380::R_DATA, 0x80);
    wr(mem, Ncr5380::R_MODE, Ncr5380::MODE_ARBITRATE);
    wr(mem, Ncr5380::R_DATA, 0x81);
    wr(mem, Ncr5380::R_MODE, 0);
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    check(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_REQ, "REQ after selection");

    // READ(6) of block 0, one block, sent with ACK pulses
    const uint8_t cdb[6] = { 0x08, 0, 0, 0, 1, 0 };
    for (uint8_t b : cdb) {
        wr(mem, Ncr5380::R_DATA, b);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }

    // Blind transfer: the SCSI Manager programs the expected phase into
    // the TCR (I/O = DATA IN) before arming MODE_DMA — on the real 5380
    // DRQ only pulses while the bus phase matches (ncr5380.cpp:217-226),
    // and the mismatch DIRECTION decides the trailing DRQ (see below).
    wr(mem, Ncr5380::R_TCR, 0x01);
    wr(mem, Ncr5380::R_MODE, Ncr5380::MODE_DMA);
    check((mem.pseudoVia().read(3) & 0x01) != 0, "DRQ visible in pseudo-VIA IFR bit 0");

    // Audit #37: plain register reads of R0/IDR are side-effect-free even
    // with MODE_DMA armed (MAME csdata_r/idata_r, machine/ncr5380.cpp:
    // 273-279, 501-506) — only the DACK windows consume. Block 0 opens
    // with 'E' of the 'ER' DDM; two reads must see the SAME byte.
    check(rd(mem, Ncr5380::R_DATA) == 0x45 && rd(mem, Ncr5380::R_DATA) == 0x45,
          "plain R0 read under MODE_DMA does not consume");
    check(rd(mem, Ncr5380::R_IDR) == 0x45 && rd(mem, Ncr5380::R_IDR) == 0x45,
          "plain IDR read under MODE_DMA does not consume");

    // Exactly 512 bytes through the windows (the old accounting read the
    // wide window twice and silently consumed STATUS + MESSAGE as data —
    // finding #3's symptom, enshrined green).
    std::vector<uint8_t> block;
    block.push_back(mem.read8(0xF06000));            // narrow window
    uint16_t w = mem.read16(0xF06000);               // wide window (2 bytes)
    block.push_back(uint8_t(w >> 8));
    block.push_back(uint8_t(w));
    block.push_back(mem.read8(0xF12000));            // window alias
    while (block.size() < 512)
        block.push_back(mem.read8(0xF10260));        // pdma reg-6 alias
    check(block[0] == 0x45 && block[1] == 0x52,
          "block 0 starts with the 'ER' Driver Descriptor Map");

    // The phase change to STATUS under MODE_DMA latches the IRQ; after a
    // RECEIVE the last byte was already DACKed, so DRQ stays LOW (MAME
    // machine/ncr5380.cpp:227-245 — a spurious raise feeds pseudo-DMA
    // read loops the status byte as data). The next window access
    // BUS-ERRORS — this is how the SCSI Manager's MOVE.L loop terminates
    // (macscsi.cpp:5-52) — and the status byte survives it.
    uint8_t bsr = rd(mem, Ncr5380::R_BSR);
    check((bsr & Ncr5380::BSR_IRQ) != 0, "DATA->STATUS under DMA latched BSR.IRQ");
    check(!(bsr & Ncr5380::BSR_DRQ), "receive mismatch: DRQ stays low");
    bool berr = false;
    try { (void)mem.read8(0xF06000); } catch (moira::MmuBusError&) { berr = true; }
    check(berr, "trailing window read raises a bus error, not a data byte");
    berr = false;
    try { mem.write8(0xF06000, 0); } catch (moira::MmuBusError&) { berr = true; }
    check(berr, "window write without DRQ raises a bus error");

    // R0 under MODE_DMA with DRQ low is a bus latch: the status byte is
    // visible but NOT consumed — the bus must still sit in STATUS after.
    check(rd(mem, Ncr5380::R_DATA) == 0x00, "status byte visible in R0");
    check((rd(mem, Ncr5380::R_CSR) & kPhaseMask) ==
              (Ncr5380::CBS_CD | Ncr5380::CBS_IO),
          "status byte not consumed by the R0 read");

    // STATUS + MESSAGE via the register path (ACK pulses), then bus free
    wr(mem, Ncr5380::R_MODE, 0);
    auto ackByte = [&] {
        uint8_t b = rd(mem, Ncr5380::R_DATA);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
        return b;
    };
    uint8_t status = ackByte();
    uint8_t msg = ackByte();
    check(status == 0x00 && msg == 0x00, "GOOD status + COMMAND COMPLETE");
    check(!(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_BSY), "bus free after transfer");
    (void)rd(mem, Ncr5380::R_RPI);                   // drop the mismatch IRQ

    // ICR.RST: edge-triggered chip reset that interrupts the initiator
    // itself and stays readable until released (ncr5380.cpp:330-355) —
    // the Mac II SCSIReset needs the IRQ edge on VIA2.
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_RST);
    check((rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_RST) != 0, "RST reads back in CSR");
    check((rd(mem, Ncr5380::R_ICR) & Ncr5380::ICR_RST) != 0, "RST reads back in ICR");
    check((rd(mem, Ncr5380::R_BSR) & Ncr5380::BSR_IRQ) != 0,
          "bus reset raised the IRQ latch");
    wr(mem, Ncr5380::R_ICR, 0);                      // release RST
    (void)rd(mem, Ncr5380::R_RPI);                   // Reset Parity/Interrupt
    check(!(rd(mem, Ncr5380::R_BSR) & Ncr5380::BSR_IRQ), "RPI cleared the IRQ");
    check(rd(mem, Ncr5380::R_CSR) == 0, "bus free after RST release");

    // MODE SELECT(6) carries a DATA OUT parameter phase of cdb[4] bytes
    // (MAME hd.cpp:622-631); dropping it desynchronizes the initiator.
    // The fixed-disk personality accepts-and-ignores the parameter list
    // and answers GOOD, like MAME's target.
    wr(mem, Ncr5380::R_DATA, 0x81);
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    check(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_REQ, "reselected for MODE SELECT");
    const uint8_t msel[6] = { 0x15, 0x10, 0, 0, 12, 0 };
    for (uint8_t b : msel) {
        wr(mem, Ncr5380::R_DATA, b);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }
    check((rd(mem, Ncr5380::R_CSR) & (kPhaseMask | Ncr5380::CBS_REQ)) ==
              Ncr5380::CBS_REQ,
          "MODE SELECT enters DATA OUT for its parameter list");
    for (int i = 0; i < 12; i++) {
        wr(mem, Ncr5380::R_DATA, uint8_t(i));
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }
    check((rd(mem, Ncr5380::R_CSR) & kPhaseMask) ==
              (Ncr5380::CBS_CD | Ncr5380::CBS_IO),
          "parameter list consumed, STATUS follows");
    uint8_t st2 = ackByte();
    uint8_t msg2 = ackByte();
    check(st2 == 0x00, "MODE SELECT answers GOOD (hd.cpp:622-631)");
    check(msg2 == 0x00, "COMMAND COMPLETE after MODE SELECT");
    check(!(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_BSY), "bus free after MODE SELECT");

    // FORMAT UNIT with FmtData: the CDB carries no length — the 4-byte
    // defect-list header does (bytes 2-3). Header says 8 defect bytes:
    // the DATA OUT phase must extend past the header, then reach STATUS.
    wr(mem, Ncr5380::R_DATA, 0x81);
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    const uint8_t fmt[6] = { 0x04, 0x10, 0, 0, 0, 0 };
    for (uint8_t b : fmt) {
        wr(mem, Ncr5380::R_DATA, b);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }
    check((rd(mem, Ncr5380::R_CSR) & (kPhaseMask | Ncr5380::CBS_REQ)) ==
              Ncr5380::CBS_REQ,
          "FORMAT UNIT FmtData enters DATA OUT");
    const uint8_t hdr[4] = { 0, 0, 0, 8 };           // defect length = 8
    for (uint8_t b : hdr) {
        wr(mem, Ncr5380::R_DATA, b);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }
    check((rd(mem, Ncr5380::R_CSR) & (kPhaseMask | Ncr5380::CBS_REQ)) ==
              Ncr5380::CBS_REQ,
          "defect-list header extends the parameter phase");
    for (int i = 0; i < 8; i++) {
        wr(mem, Ncr5380::R_DATA, 0);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }
    check((rd(mem, Ncr5380::R_CSR) & kPhaseMask) ==
              (Ncr5380::CBS_CD | Ncr5380::CBS_IO),
          "defect list consumed, STATUS follows");
    check(ackByte() == 0x00, "FORMAT UNIT answers GOOD (hd.cpp:601-620)");
    (void)ackByte();                                 // COMMAND COMPLETE
    check(!(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_BSY), "bus free after FORMAT UNIT");

    // Audit #39: an out-of-range READ answers CHECK CONDITION and REQUEST
    // SENSE reports ILLEGAL REQUEST / INVALID FIELD IN CDB $24 (MAME
    // hd.cpp:567-580) — the old target zero-filled the data and said GOOD.
    wr(mem, Ncr5380::R_DATA, 0x81);
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    const uint8_t oob[10] = { 0x28, 0, 0x7F, 0xFF, 0xFF, 0xFF, 0, 0, 1, 0 };
    for (uint8_t b : oob) {
        wr(mem, Ncr5380::R_DATA, b);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }
    check((rd(mem, Ncr5380::R_CSR) & kPhaseMask) ==
              (Ncr5380::CBS_CD | Ncr5380::CBS_IO),
          "out-of-range READ(10) goes straight to STATUS");
    check(ackByte() == 0x02, "out-of-range READ answers CHECK CONDITION");
    check(ackByte() == 0x00, "COMMAND COMPLETE after CHECK");
    wr(mem, Ncr5380::R_DATA, 0x81);
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    const uint8_t rqs[6] = { 0x03, 0, 0, 0, 14, 0 };
    for (uint8_t b : rqs) {
        wr(mem, Ncr5380::R_DATA, b);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }
    uint8_t sense[14];
    for (uint8_t& b : sense) b = ackByte();
    check((sense[2] & 0x0F) == 0x05 && sense[12] == 0x24,
          "REQUEST SENSE reports ILLEGAL REQUEST / $24");
    (void)ackByte();                                 // status
    (void)ackByte();                                 // COMMAND COMPLETE
    check(!(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_BSY), "bus free after sense");

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
