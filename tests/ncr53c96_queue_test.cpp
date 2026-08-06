// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// 53C96 MAME-parity gate: the 2-deep command queue, bus reset, and Transfer
// Pad — the register-level behaviours A/UX and NetBSD drivers lean on.
//
//   1. Command queue (ncr53c90.cpp:886-916 command_w / command_pop_and_chain,
//      :1116-1117 istatus_r): a command written while the previous one's
//      interrupt is unread queues behind it and only starts when the
//      interrupt-status read pops the finished one; a THIRD write sets
//      S_GROSS_ERROR (status flag, no interrupt) and is dropped; CM_RESET
//      jumps the queue. Reading the STATUS register with an interrupt
//      pending clears the sticky error bits by itself (53c90a status_r,
//      ncr53c90.cpp:1288-1296) — gated on the interrupt, so with none
//      pending the flag survives.
//   2. CM_RESET_BUS (ncr53c90.cpp:965-969 + :324-334): kills the in-flight
//      session (no ghost payload bytes out of the FIFO port afterwards), the
//      deferred completion of the aborted command, and the command queue.
//   3. CI_PAD (ncr53c90.cpp:694-737): TC exhaustion completes with
//      I_FUNCTION + S_TC0 (function_complete), a phase change first with
//      I_BUS (bus_complete); the transmit side pads the write payload with
//      zeros.
//   4. Mode/command validation (#40, ncr53c90.cpp:930-935 start_command +
//      :1298-1308 ncr53c90a check_valid_command): an initiator command while
//      disconnected — or a disconnected-group command while connected —
//      latches I_ILLEGAL instantly and executes nothing.
//   5. S_TC0 (#41, ncr53c90.cpp:1234-1251 decrement_tcounter): strictly the
//      DMA transfer-counter-zero flag — never on polled ($10) drains, never
//      on a chunk cut short by a phase change.
//
// Self-contained: builds its own tiny raw disk image in the system temp
// directory (no asset dependency, never skips). Modelled on
// tests/ncr53c96_test.cpp.

#include "Ncr53c96.h"
#include "ScsiDisk.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using R = Ncr53c96;

#define CHECK(c, ...) do { if(!(c)){ std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
    std::fprintf(stderr, "\n"); return 1; } } while (0)

// FLUSH FIFO, IDENTIFY + CDB into the FIFO, destination id 0, SELECT_ATN.
// Leaves the select's I_BUS|I_FUNCTION latched (caller reads R_ISTAT).
static void selectNoWait(R& s, const std::vector<uint8_t>& cdb) {
    s.write(R::R_COMMAND, R::CM_FLUSH_FIFO);
    s.write(R::R_FIFO, 0xC0);                 // IDENTIFY (LUN 0, disc ok)
    for (uint8_t b : cdb) s.write(R::R_FIFO, b);
    s.write(R::R_STATUS, 0x00);               // destination bus id = 0
    s.write(R::R_COMMAND, R::CD_SELECT_ATN);
}

static void setTc(R& s, uint32_t n) {
    s.write(R::R_TCLOW, uint8_t(n));
    s.write(R::R_TCMID, uint8_t(n >> 8));
    s.write(R::R_TCHIGH, uint8_t(n >> 16));
}

int main() {
    // ── Build a 16-block patterned raw image (block b, byte i = b*7+i) ──
    const std::string img =
        (std::filesystem::temp_directory_path() / "pom68k_ncr53c96_queue.hda").string();
    {
        std::ofstream out(img, std::ios::binary | std::ios::trunc);
        for (int b = 0; b < 16; b++)
            for (int i = 0; i < 512; i++)
                out.put(char(uint8_t(b * 7 + i)));
    }
    ScsiDisk disk;
    CHECK(disk.open(img), "open temp image %s", img.c_str());
    std::filesystem::remove(img);             // loaded whole into memory

    R scsi;
    scsi.reset();
    scsi.write(R::R_CONFIG1, 0x07);           // own ID 7
    scsi.attach(&disk, 0);

    const std::vector<uint8_t> tur   = { 0x00, 0, 0, 0, 0, 0 };       // TEST UNIT READY
    const std::vector<uint8_t> read6 = { 0x08, 0, 0, 0, 0x01, 0 };    // READ(6) LBA 0, 1 blk

    // ── 1a. Pipeline: CI_COMPLETE queued behind an unread select IRQ ──
    {
        selectNoWait(scsi, tur);              // → STATUS phase, I_BUS|I_FUNCTION latched
        CHECK(scsi.irq(), "select raised its interrupt");
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);          // queues (slot 1)
        CHECK((scsi.read(R::R_FLAGS) & 0x1F) == 0,
              "queued CI_COMPLETE has NOT latched STATUS+msg into the FIFO yet");
        CHECK((scsi.read(R::R_STATUS) & 0x07) == (R::S_CD | R::S_IO),
              "still in STATUS phase — queued command did not run");

        // Third command while two are held → S_GROSS_ERROR, dropped.
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        uint8_t st = scsi.read(R::R_STATUS);
        CHECK(st & R::S_GROSS_ERROR,
              "third command sets S_GROSS_ERROR (ncr53c90.cpp:890-893)");
        CHECK(st & R::S_INTERRUPT, "and the select's interrupt is still pending");
        // 53c90a status_r side effect (§ 2.7 re-check, ncr53c90.cpp:1288-1296):
        // with an interrupt pending, the READ that reported the sticky error
        // bits also clears them — the ISTAT read is not the only eraser. The
        // audit filed this as unobservable because nothing raised those bits;
        // the 2-deep queue gave S_GROSS_ERROR a producer, so it is modelled.
        CHECK(!(scsi.read(R::R_STATUS) & R::S_GROSS_ERROR),
              "a second status read finds S_GROSS_ERROR cleared by status_r");
        CHECK((scsi.read(R::R_STATUS) & 0x07) == (R::S_CD | R::S_IO),
              "dropped command did not run (no BUS FREE)");

        // ISTAT read #1: the select's cause, chain starts.
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist == (R::I_BUS | R::I_FUNCTION),
              "first ISTAT read = select cause alone, got %02X", ist);
        CHECK(!(scsi.read(R::R_STATUS) & R::S_GROSS_ERROR),
              "S_GROSS_ERROR stays clear across the ISTAT read (:1109-1111)");
        CHECK((scsi.read(R::R_FLAGS) & 0x1F) == 2,
              "pop-and-chain ran CI_COMPLETE: STATUS+msg now in the FIFO");

        // ISTAT read #2: the chained CI_COMPLETE's own cause.
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_FUNCTION,
              "second ISTAT read = chained CI_COMPLETE cause, got %02X", ist);
        CHECK(scsi.read(R::R_FIFO) == 0x00, "GOOD status byte");
        CHECK(scsi.read(R::R_FIFO) == 0x00, "COMMAND COMPLETE message byte");

        // The dropped CI_MSG_ACCEPT never ran — issue it for real now.
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist & R::I_DISCONNECT, "MSG ACCEPT → BUS FREE, got %02X", ist);
        std::printf("  queue: pipeline + gross error + pop-and-chain OK\n");
    }

    // ── 1b. Deferred IRQ: queueing with the latency model armed ──
    {
        scsi.setLatency(1000);                // flat 1000-cycle deferral
        selectNoWait(scsi, tur);              // completion held back
        CHECK(!scsi.irq(), "select completion deferred by the latency model");
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);          // queues (slot 1)
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);        // third → gross error
        CHECK(scsi.read(R::R_STATUS) & R::S_GROSS_ERROR,
              "gross error with the queue full and NO interrupt pending");
        CHECK(!scsi.irq(), "S_GROSS_ERROR alone raises no interrupt");
        // The status_r eraser is gated on a PENDING interrupt (:1294
        // `if (irq && ...)`), so with none the bit survives any number of
        // status reads — this is the other half of the § 2.7 alignment.
        CHECK(scsi.read(R::R_STATUS) & R::S_GROSS_ERROR,
              "no interrupt pending: status_r leaves S_GROSS_ERROR alone");
        CHECK(scsi.read(R::R_ISTAT) == 0, "ISTAT reads 0 while deferred");
        CHECK((scsi.read(R::R_FLAGS) & 0x1F) == 0,
              "a zero ISTAT read pops nothing (ncr53c90.cpp:1116-1117)");
        scsi.tick(2000);                      // deliver the deferred IRQ
        CHECK(scsi.irq(), "deferred select completion arrived");
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist == (R::I_BUS | R::I_FUNCTION),
              "deferred select cause, got %02X", ist);
        CHECK((scsi.read(R::R_FLAGS) & 0x1F) == 2,
              "pop-and-chain ran the queued CI_COMPLETE");
        (void)scsi.read(R::R_ISTAT);          // CI_COMPLETE cause
        (void)scsi.read(R::R_FIFO); (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);          // I_DISCONNECT
        scsi.setLatency(0);
        std::printf("  queue: deferred-IRQ pipeline OK\n");
    }

    // ── 1c. CM_RESET: dropped on a FULL queue (the gross-error check comes
    //        FIRST, ncr53c90.cpp:890-900), jumps the queue otherwise ──
    {
        selectNoWait(scsi, tur);              // interrupt latched, slot 0 held
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);          // slot 1
        scsi.write(R::R_COMMAND, R::CM_RESET);             // full → dropped
        CHECK(scsi.irq(), "reset on a full queue is dropped, not executed");
        CHECK(scsi.read(R::R_STATUS) & R::S_GROSS_ERROR, "and flags S_GROSS_ERROR");
        (void)scsi.read(R::R_ISTAT);          // pop select, chain CI_COMPLETE
        scsi.write(R::R_COMMAND, R::CM_RESET);             // one deep → jumps
        CHECK(!scsi.irq(), "chip reset clears the interrupt");
        CHECK(scsi.read(R::R_ISTAT) == 0, "istatus empty after chip reset");
        CHECK((scsi.read(R::R_FLAGS) & 0x1F) == 0, "FIFO empty after chip reset");
        scsi.write(R::R_CONFIG1, 0x07);       // re-establish own ID
        std::printf("  queue: CM_RESET vs the queue OK\n");
    }

    // ── 2. CM_RESET_BUS kills the in-flight session (no ghost bytes) ──
    {
        selectNoWait(scsi, read6);
        (void)scsi.read(R::R_ISTAT);          // pop the select
        setTc(scsi, 512);
        scsi.write(R::R_COMMAND, R::CI_XFER | R::CMD_DMA);
        for (int i = 0; i < 8; i++) (void)scsi.dmaRead();  // partial drain
        CHECK(scsi.drq(), "payload still pending before the bus reset");
        scsi.write(R::R_COMMAND, R::CM_RESET_BUS);
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist & R::I_SCSI_RESET, "bus reset raises I_SCSI_RESET, got %02X", ist);
        CHECK(!scsi.drq(), "DRQ dropped with the session");
        CHECK(scsi.read(R::R_FIFO) == 0,
              "no ghost payload out of the FIFO port after the bus reset");
        CHECK(scsi.read(R::R_FIFO) == 0, "still no ghost bytes");
        CHECK(scsi.read(R::R_ISTAT) == 0, "no stale deferred interrupt either");
        std::printf("  bus reset: session + deferred IRQ flushed OK\n");
    }

    // ── 3a. Receive pad, TC exhaustion → I_FUNCTION + S_TC0 ──
    {
        selectNoWait(scsi, read6);
        (void)scsi.read(R::R_ISTAT);          // pop the select
        setTc(scsi, 16);
        scsi.write(R::R_COMMAND, R::CI_PAD | R::CMD_DMA);  // discard 16 of 512
        CHECK(scsi.read(R::R_STATUS) & R::S_TC0, "S_TC0 latched on pad TC0");
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_FUNCTION,
              "pad TC exhaustion = function_complete (ncr53c90.cpp:736), got %02X", ist);

        // ── 3b. Receive pad, phase change first → I_BUS ──
        setTc(scsi, 1024);                    // more than the 496 left
        scsi.write(R::R_COMMAND, R::CI_PAD | R::CMD_DMA);
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_BUS,
              "pad ended by the phase change = bus_complete (ncr53c90.cpp:721), got %02X", ist);
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);          // queue emptied by :721
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_FUNCTION, "CI_COMPLETE after pads, got %02X", ist);
        CHECK(scsi.read(R::R_FIFO) == 0x00, "GOOD status after padded READ");
        (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);
        std::printf("  recv pad: TC0 → I_FUNCTION, phase change → I_BUS OK\n");
    }

    // ── 3c. Transmit pad: WRITE(6) padded to a full block of zeros ──
    {
        const std::vector<uint8_t> write6 = { 0x0A, 0, 0, 0x02, 0x01, 0 }; // LBA 2
        selectNoWait(scsi, write6);
        (void)scsi.read(R::R_ISTAT);          // pop the select → DATA OUT
        setTc(scsi, 512);
        scsi.write(R::R_COMMAND, R::CI_PAD | R::CMD_DMA);  // send 512 zero bytes
        CHECK(scsi.read(R::R_STATUS) & R::S_TC0, "send pad latched S_TC0");
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_FUNCTION,
              "send pad TC exhaustion = function_complete (ncr53c90.cpp:713), got %02X", ist);
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);
        (void)scsi.read(R::R_ISTAT);
        CHECK(scsi.read(R::R_FIFO) == 0x00, "GOOD status after padded WRITE");
        (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);

        // Read LBA 2 back: the pad wrote zeros over the pattern.
        const std::vector<uint8_t> rd2 = { 0x08, 0, 0, 0x02, 0x01, 0 };
        selectNoWait(scsi, rd2);
        (void)scsi.read(R::R_ISTAT);
        setTc(scsi, 512);
        scsi.write(R::R_COMMAND, R::CI_XFER | R::CMD_DMA);
        for (int i = 0; i < 512; i++)
            CHECK(scsi.dmaRead() == 0, "padded block reads back as zeros (byte %d)", i);
        (void)scsi.read(R::R_ISTAT);
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);
        (void)scsi.read(R::R_ISTAT);
        CHECK(scsi.read(R::R_FIFO) == 0x00, "GOOD status after read-back");
        (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);
        std::printf("  send pad: zero-fill + execution OK\n");
    }

    // ── 4. Mode/command validation → I_ILLEGAL (#40, ncr53c90.cpp:930-935
    //       start_command + :1298-1308 ncr53c90a check_valid_command) ──
    {
        // Disconnected: an initiator-group command is illegal — instantly,
        // not a deferred I_BUS.
        scsi.write(R::R_COMMAND, R::CI_XFER);
        CHECK(scsi.irq(), "disconnected CI_XFER latches its interrupt at once");
        CHECK(!scsi.drq(), "and raises no DRQ");
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_ILLEGAL,
              "disconnected CI_XFER = I_ILLEGAL (check_valid_command), got %02X", ist);

        // DMA variant: illegal too, and the transfer counter must NOT reload
        // (MAME returns before load_tcounter, ncr53c90.cpp:930-943).
        setTc(scsi, 64);
        scsi.write(R::R_COMMAND, R::CI_XFER | R::CMD_DMA);
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_ILLEGAL, "disconnected DMA CI_XFER = I_ILLEGAL, got %02X", ist);
        CHECK(scsi.read(R::R_TCLOW) == 0 && scsi.read(R::R_TCMID) == 0,
              "illegal command does not reload the transfer counter");

        // Connected: a disconnected-group command is illegal (group 4 needs
        // MODE_D), and the session survives the fault.
        selectNoWait(scsi, tur);              // → STATUS phase
        (void)scsi.read(R::R_ISTAT);          // pop the select
        scsi.write(R::R_COMMAND, R::CD_ENABLE_SEL);
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_ILLEGAL,
              "connected CD_ENABLE_SEL = I_ILLEGAL (MODE_D group), got %02X", ist);
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist == R::I_FUNCTION, "session still live after the illegal, got %02X", ist);
        CHECK(scsi.read(R::R_FIFO) == 0x00, "GOOD status");
        (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);
        std::printf("  validation: I_ILLEGAL both ways, session survives OK\n");
    }

    // ── 5. S_TC0 is strictly the DMA counter-zero flag (#41,
    //       ncr53c90.cpp:1234-1251 decrement_tcounter: !dma_command → no-op) ──
    {
        // Polled (non-DMA $10) READ: the payload drains via R_FIFO with I_BUS
        // completions but S_TC0 never sets.
        selectNoWait(scsi, read6);
        (void)scsi.read(R::R_ISTAT);
        setTc(scsi, 512);                     // latch armed, but non-DMA ignores it
        // A DMA NOP latches the counter and clears a STALE S_TC0 (from the
        // CI_PAD TC-exhaustion above — MAME never clears it on istatus_r,
        // only a DMA command reload does; this is the real drivers' idiom).
        scsi.write(R::R_COMMAND, R::CM_NOP | R::CMD_DMA);
        scsi.write(R::R_COMMAND, R::CI_XFER);
        CHECK(!(scsi.read(R::R_STATUS) & R::S_TC0),
              "non-DMA Transfer Info must not pre-announce S_TC0");
        for (int i = 0; i < 512; i++) (void)scsi.read(R::R_FIFO);
        CHECK(!(scsi.read(R::R_STATUS) & R::S_TC0),
              "non-DMA payload end: I_BUS without S_TC0");
        uint8_t ist = scsi.read(R::R_ISTAT);
        CHECK(ist & R::I_BUS, "polled drain still completes with I_BUS, got %02X", ist);
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);
        (void)scsi.read(R::R_ISTAT);
        (void)scsi.read(R::R_FIFO); (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);

        // Short DMA chunk: TC 1024 > the 512 bytes the target has — the phase
        // change ends the transfer with tcounter non-zero → I_BUS, no S_TC0,
        // residual count readable.
        selectNoWait(scsi, read6);
        (void)scsi.read(R::R_ISTAT);
        setTc(scsi, 1024);
        scsi.write(R::R_COMMAND, R::CI_XFER | R::CMD_DMA);
        CHECK(!(scsi.read(R::R_STATUS) & R::S_TC0),
              "short chunk (TC > payload): no S_TC0 pre-announce");
        for (int i = 0; i < 512; i++) (void)scsi.dmaRead();
        CHECK(!(scsi.read(R::R_STATUS) & R::S_TC0),
              "phase change before TC=0: bus_complete without S_TC0");
        ist = scsi.read(R::R_ISTAT);
        CHECK(ist & R::I_BUS, "short chunk completes with I_BUS, got %02X", ist);
        CHECK(scsi.read(R::R_TCLOW) == 0x00 && scsi.read(R::R_TCMID) == 0x02,
              "residual counter = 512 after the short chunk");
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);
        (void)scsi.read(R::R_ISTAT);
        (void)scsi.read(R::R_FIFO); (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);

        // Control: exact DMA chunk (TC == payload) DOES latch S_TC0 — the
        // flag the OS 8.1 driver polls before bursting the window.
        selectNoWait(scsi, read6);
        (void)scsi.read(R::R_ISTAT);
        setTc(scsi, 512);
        scsi.write(R::R_COMMAND, R::CI_XFER | R::CMD_DMA);
        CHECK(scsi.read(R::R_STATUS) & R::S_TC0,
              "DMA chunk == payload: S_TC0 latched");
        for (int i = 0; i < 512; i++) (void)scsi.dmaRead();
        CHECK(scsi.read(R::R_STATUS) & R::S_TC0, "S_TC0 still latched after the drain");
        (void)scsi.read(R::R_ISTAT);
        scsi.write(R::R_COMMAND, R::CI_COMPLETE);
        (void)scsi.read(R::R_ISTAT);
        (void)scsi.read(R::R_FIFO); (void)scsi.read(R::R_FIFO);
        scsi.write(R::R_COMMAND, R::CI_MSG_ACCEPT);
        (void)scsi.read(R::R_ISTAT);
        std::printf("  S_TC0: DMA-counter-zero only (polled + short chunk clean) OK\n");
    }

    std::printf("ncr53c96_queue_test: command queue, bus reset flush, "
                "transfer pad (recv + send), I_ILLEGAL validation, "
                "S_TC0 DMA-only OK\n");
    return 0;
}
