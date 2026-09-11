// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The DaynaPort SCSI/Link through each SCSI controller the machines carry.
// `daynaport_test` pins the card's command set by calling it directly; this
// gate drives it the way a guest does, through the chip:
//
//   daynaport_bus_test 5380   the NCR 5380 of the compacts, the Mac II
//                             family and the 030 boards — ncr5380_test's
//                             arbitration, selection and REQ/ACK bytes
//   daynaport_bus_test 53c96  the NCR 53C96 of the Quadras — ncr53c96_test's
//                             FIFO, SELECT-with-ATN and Transfer Information
//
// Asserted on both: a card that is not attached answers no selection; once
// attached, INQUIRY returns the 37-byte SCSI/Link identity, ENABLE turns the
// interface on, a WRITE(6) frame arrives on sendFrame byte for byte, and a
// frame given to receiveFrame comes back through READ(6) behind its 6-byte
// header with its FCS. Asset-free: the card needs no image.

#include "DaynaPort.h"
#include "Ncr5380.h"
#include "Ncr53c96.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

#define CHECK(c, ...) do { if (!(c)) { std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
    std::fprintf(stderr, "\n"); return 1; } } while (0)

constexpr int kId = 3;

struct Reply {
    bool selected = false;
    std::uint8_t status = 0xFF, msg = 0xFF;
    std::vector<std::uint8_t> in;
};

// ── NCR 5380: polled REQ/ACK, following the target's phases ─────────────
bool req(Ncr5380& s) { return s.read(Ncr5380::R_CSR) & Ncr5380::CBS_REQ; }
void sendByte(Ncr5380& s, std::uint8_t b) {
    s.write(Ncr5380::R_DATA, b);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);
    s.write(Ncr5380::R_ICR, 0);
}
std::uint8_t recvByte(Ncr5380& s) {
    const std::uint8_t b = s.read(Ncr5380::R_DATA);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_ACK);
    s.write(Ncr5380::R_ICR, 0);
    return b;
}

Reply run5380(Ncr5380& s, const std::vector<std::uint8_t>& cdb,
              const std::vector<std::uint8_t>& out) {
    Reply r;
    s.write(Ncr5380::R_DATA, 0x80);                          // own ID 7
    s.write(Ncr5380::R_MODE, Ncr5380::MODE_ARBITRATE);
    s.write(Ncr5380::R_DATA, std::uint8_t(0x80 | (1 << kId)));
    s.write(Ncr5380::R_MODE, 0);
    s.write(Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    if (!req(s)) { s.write(Ncr5380::R_ICR, 0); return r; }  // nobody answered
    r.selected = true;
    for (std::uint8_t b : cdb) sendByte(s, b);
    std::size_t o = 0;
    for (int guard = 0; guard < 8192 && req(s); guard++) {
        const std::uint8_t csr = s.read(Ncr5380::R_CSR);
        const bool io = csr & Ncr5380::CBS_IO, cd = csr & Ncr5380::CBS_CD,
                   msg = csr & Ncr5380::CBS_MSG;
        if (!cd && !msg) {                                   // DATA
            if (io) r.in.push_back(recvByte(s));
            else sendByte(s, o < out.size() ? out[o++] : 0);
        } else if (cd && !msg && io) {
            r.status = recvByte(s);
        } else if (cd && msg && io) {
            r.msg = recvByte(s);
        } else {
            break;
        }
    }
    return r;
}

// ── NCR 53C96: FIFO, SELECT-with-ATN, Transfer Information ─────────────
using R = Ncr53c96;

Reply run53c96(R& s, const std::vector<std::uint8_t>& cdb,
               const std::vector<std::uint8_t>& out, std::size_t inLen) {
    Reply r;
    s.write(R::R_COMMAND, R::CM_FLUSH_FIFO);
    s.write(R::R_FIFO, 0xC0);                                // IDENTIFY
    for (std::uint8_t b : cdb) s.write(R::R_FIFO, b);
    s.write(R::R_STATUS, std::uint8_t(kId));                 // destination ID
    s.write(R::R_COMMAND, R::CD_SELECT_ATN);
    const std::uint8_t sel = s.read(R::R_ISTAT);
    if (sel & R::I_DISCONNECT) return r;                     // selection timeout
    r.selected = true;
    auto count = [&](std::size_t n) {
        s.write(R::R_TCLOW, std::uint8_t(n));
        s.write(R::R_TCMID, std::uint8_t(n >> 8));
        s.write(R::R_TCHIGH, 0);
    };
    if (!out.empty()) {
        (void)s.read(R::R_ISTAT);                            // ncr53c96_test's WRITE
        count(out.size());
        s.write(R::R_COMMAND, R::CI_XFER);
        for (std::uint8_t b : out) s.write(R::R_FIFO, b);
        (void)s.read(R::R_ISTAT);
    } else if (inLen) {
        count(inLen);
        s.write(R::R_COMMAND, R::CI_XFER);
        for (std::size_t i = 0; i < inLen; i++) r.in.push_back(s.read(R::R_FIFO));
    }
    (void)s.read(R::R_ISTAT);
    s.write(R::R_COMMAND, R::CI_COMPLETE);                   // STATUS + message
    if (s.read(R::R_ISTAT) & R::I_FUNCTION) {
        r.status = s.read(R::R_FIFO);
        r.msg = s.read(R::R_FIFO);
    }
    s.write(R::R_COMMAND, R::CI_MSG_ACCEPT);                 // → BUS FREE
    (void)s.read(R::R_ISTAT);
    return r;
}

// ── One protocol, either chip ───────────────────────────────────────────
template <class Chip, class Run>
int exercise(Chip& chip, DaynaPort& card, Run run, const char* name) {
    const std::vector<std::uint8_t> none;
    const std::vector<std::uint8_t> tur = { 0x00, 0, 0, 0, 0, 0 };
    CHECK(!run(chip, tur, none, 0).selected,
          "%s: a card that is not attached answered selection", name);

    card.attach();
    const std::vector<std::uint8_t> inquiry = { 0x12, 0, 0, 0, 37, 0 };
    Reply q = run(chip, inquiry, none, 37);
    CHECK(q.selected, "%s: the attached card did not answer selection", name);
    CHECK(q.status == 0x00 && q.msg == 0x00,
          "%s: INQUIRY status %02X msg %02X", name, q.status, q.msg);
    CHECK(q.in.size() == 37 && q.in[0] == 0x03 &&
          std::memcmp(q.in.data() + 8, "Dayna   SCSI/Link", 17) == 0,
          "%s: INQUIRY is not the 37-byte SCSI/Link identity (%zu bytes)",
          name, q.in.size());

    const std::vector<std::uint8_t> enable = { 0x0E, 0, 0, 0, 0, 0x80 };
    Reply e = run(chip, enable, none, 0);
    CHECK(e.status == 0x00 && card.enabled(), "%s: ENABLE left the interface off", name);

    std::vector<std::uint8_t> frame(64);
    for (std::size_t i = 0; i < frame.size(); i++) frame[i] = std::uint8_t(i * 7 + 1);
    std::vector<std::uint8_t> sent;
    card.sendFrame = [&](const std::uint8_t* d, std::size_t n) { sent.assign(d, d + n); };
    const std::vector<std::uint8_t> write = { 0x0A, 0, 0, 0, std::uint8_t(frame.size()), 0x00 };
    Reply w = run(chip, write, frame, 0);
    CHECK(w.status == 0x00, "%s: WRITE(6) status %02X", name, w.status);
    CHECK(sent == frame, "%s: WRITE(6) delivered %zu bytes, not the frame",
          name, sent.size());

    card.receiveFrame(frame.data(), frame.size());
    const std::size_t reply = 6 + frame.size() + 4;          // header, frame, FCS
    const std::vector<std::uint8_t> read = { 0x08, 0, 0, 0x05, 0xF0, 0xC0 };
    Reply rd = run(chip, read, none, reply);
    CHECK(rd.status == 0x00, "%s: READ(6) status %02X", name, rd.status);
    CHECK(rd.in.size() == reply, "%s: READ(6) returned %zu bytes, want %zu",
          name, rd.in.size(), reply);
    CHECK(rd.in[0] == 0 && rd.in[1] == frame.size() + 4,
          "%s: READ(6) header length %u", name, unsigned(rd.in[0] << 8 | rd.in[1]));
    CHECK(std::equal(frame.begin(), frame.end(), rd.in.begin() + 6),
          "%s: READ(6) frame bytes differ", name);
    std::printf("%s: selection, INQUIRY, ENABLE, WRITE(6) and READ(6) OK\n", name);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "5380";
    DaynaPort card;
    if (which == "5380") {
        Ncr5380 scsi;
        scsi.reset();
        scsi.attach(&card, kId);
        return exercise(scsi, card,
            [](Ncr5380& s, const std::vector<std::uint8_t>& cdb,
               const std::vector<std::uint8_t>& out, std::size_t) {
                return run5380(s, cdb, out);
            }, "NCR 5380");
    }
    if (which == "53c96") {
        R scsi;
        scsi.reset();
        scsi.write(R::R_CONFIG1, 0x07);                      // own ID 7
        scsi.attach(&card, kId);
        return exercise(scsi, card, run53c96, "NCR 53C96");
    }
    std::fprintf(stderr, "usage: daynaport_bus_test 5380|53c96\n");
    return 2;
}
