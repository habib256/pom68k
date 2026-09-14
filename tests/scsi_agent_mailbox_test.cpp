// POM68K — the host ↔ guest-agent mailbox protocol (src/ScsiAgentMailbox.h)
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The two vendor CDBs as the agent issues them: a POLL with nothing
// pending answers "none"; a posted request rides the next POLL with its
// sequence; a REPORT carrying that sequence clears it and its words are
// kept; a REPORT with a stale sequence keeps the request pending; a
// second post replaces the first. No ROM, no image.

#include "ScsiAgentMailbox.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s   %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}
std::vector<uint8_t> poll(pom68k::ScsiAgentMailbox& m) {
    const uint8_t cdb[6] = { 0xC0, 0, 0, 0, 64, 0 };
    std::vector<uint8_t> out, none;
    const uint8_t st = m.command(cdb, 6, out, none);
    check(st == 0 && out.size() == 64 && std::memcmp(out.data(), "POMA", 4) == 0,
          "POLL answers GOOD with a 64-byte POMA payload");
    return out;
}
uint8_t report(pom68k::ScsiAgentMailbox& m, uint8_t kind, uint8_t id, uint8_t seq,
               int16_t err, uint16_t drive, const char* text) {
    const uint8_t cdb[6] = { 0xC1, 0, 0, 0, 64, 0 };
    std::vector<uint8_t> in(64, 0), out;
    std::memcpy(in.data(), "POMR", 4);
    in[4] = 1; in[5] = kind; in[6] = id; in[7] = seq;
    in[8] = uint8_t(uint16_t(err) >> 8); in[9] = uint8_t(err);
    in[10] = uint8_t(drive >> 8); in[11] = uint8_t(drive);
    in[12] = uint8_t(std::strlen(text));
    std::memcpy(&in[13], text, in[12]);
    return m.command(cdb, 6, out, in);
}
} // namespace

int main() {
    pom68k::ScsiAgentMailbox m;
    check(pom68k::ScsiAgentMailbox::handles((const uint8_t*)"\xC0\0\0\0\x40\0", 6) &&
          pom68k::ScsiAgentMailbox::handles((const uint8_t*)"\xC1\0\0\0\x40\0", 6) &&
          !pom68k::ScsiAgentMailbox::handles((const uint8_t*)"\x12\0\0\0\x24\0", 6),
          "only $C0/$C1 are the mailbox's");
    check(pom68k::ScsiAgentMailbox::writeByteCount((const uint8_t*)"\xC1\0\0\0\x40\0", 6) == 64 &&
          pom68k::ScsiAgentMailbox::writeByteCount((const uint8_t*)"\xC0\0\0\0\x40\0", 6) == 0,
          "REPORT owes cdb[4] DATA OUT bytes, POLL none");

    std::vector<uint8_t> p = poll(m);
    check(p[5] == 0 && p[7] == 0, "nothing pending → kind 0, seq 0");
    check(m.snapshot().polls == 1 && !m.snapshot().pending, "the poll counted as a heartbeat");

    m.post(pom68k::ScsiAgentMailbox::Mount, 2);
    p = poll(m);
    const uint8_t seq = p[7];
    check(p[5] == 1 && p[6] == 2 && seq != 0, "a posted mount rides the next POLL with a sequence");

    check(report(m, 1, 2, uint8_t(seq + 1), 0, 9, "Stale") == 0 && m.snapshot().pending,
          "a REPORT with another sequence leaves the request pending");
    check(report(m, 1, 2, seq, 0, 9, "Branche") == 0 && !m.snapshot().pending,
          "the REPORT with the right sequence clears it");
    const pom68k::ScsiAgentSnapshot s = m.snapshot();
    check(s.reported && s.lastKind == 1 && s.lastId == 2 && s.lastErr == 0 &&
              s.lastDrive == 9 && s.lastText == "Branche",
          "the report's kind, id, error, drive and words are kept");

    m.post(pom68k::ScsiAgentMailbox::Unmount, 2);
    m.post(pom68k::ScsiAgentMailbox::Mount, 3);
    p = poll(m);
    check(p[5] == 1 && p[6] == 3, "a second post replaces the first");
    check(report(m, 1, 3, p[7], -35, 0, "nsvErr") == 0 && m.snapshot().lastErr == -35,
          "a negative OSErr survives the big-endian round trip");

    std::vector<uint8_t> out;
    const std::vector<uint8_t> garbage(64, 0xFF);
    check(m.command((const uint8_t*)"\xC1\0\0\0\x40\0", 6, out, garbage) == 0x02,
          "a REPORT without the POMR signature is CHECK CONDITION");

    std::printf(failures ? "FAILED\n" : "PASS\n");
    return failures ? 1 : 0;
}
