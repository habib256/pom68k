// POM68K — the host ↔ guest-agent mailbox, carried on the SCSI bus
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// docs/SCSI_HOTPLUG.md § 3, step 3. A guest-side agent (dev/scsiagent, a
// Retro68 application) mounts and unmounts volumes on request, because
// only the guest's File Manager can. The host has to reach it, and the one
// channel every one of the twelve boards has is the SCSI bus itself, so
// the request rides on two VENDOR-SPECIFIC commands that any POM68K target
// answers once selected — the controller intercepts them before the disk
// sees the CDB:
//
//   $C0  POM68K AGENT POLL    (6-byte CDB, cdb[4] = allocation length)
//        DATA IN, 64 bytes:
//          0..3  "POMA"      4  protocol version (1)
//          5     kind: 0 none, 1 mount, 2 unmount
//          6     SCSI id the request is about
//          7     sequence number of the request (wraps at 255)
//   $C1  POM68K AGENT REPORT  (6-byte CDB, cdb[4] = transfer length)
//        DATA OUT, 64 bytes:
//          0..3  "POMR"      4  version      5  kind      6  id
//          7     sequence being answered
//          8..9  OSErr, big-endian signed
//          10..11 drive number the volume sits on (0 when none)
//          12..  Pascal string: the volume name, or the agent's own words
//
// A request is one slot: posting replaces an unanswered one. Every POLL is
// a heartbeat — the GUI shows the agent as present while polls keep coming
// (MachineHost turns the count into "seen within the last ~2 s"). Neither
// side ever writes guest memory: the guest asks, the host answers, the
// guest reports. The mailbox is not part of a save state: a pending request
// is a GUI edge, not machine state.

#pragma once

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace pom68k {

struct ScsiAgentSnapshot {
    std::uint32_t polls = 0;        // POLL commands answered so far
    bool pending = false;           // a request is waiting for its report
    std::uint8_t pendingKind = 0, pendingId = 0;
    std::uint8_t lastKind = 0, lastId = 0;
    std::int16_t lastErr = 0;
    int lastDrive = 0;
    std::string lastText;           // the report's Pascal string
    bool reported = false;          // at least one report has arrived
};

class ScsiAgentMailbox {
public:
    static constexpr std::uint8_t kPoll = 0xC0, kReport = 0xC1;
    static constexpr std::size_t kPayload = 64;
    enum Kind : std::uint8_t { None = 0, Mount = 1, Unmount = 2 };

    static bool handles(const std::uint8_t* cdb, int cdbLen) {
        return cdb && cdbLen >= 6 && (cdb[0] == kPoll || cdb[0] == kReport);
    }
    static int writeByteCount(const std::uint8_t* cdb, int cdbLen) {
        return (cdb && cdbLen >= 6 && cdb[0] == kReport) ? cdb[4] : 0;
    }

    // ── Host side ───────────────────────────────────────────────────────
    void post(Kind kind, int id) {
        std::lock_guard<std::mutex> l(mu_);
        s_.pending = true;
        s_.pendingKind = kind;
        s_.pendingId = std::uint8_t(id);
        ++seq_;
        if (!seq_) ++seq_;
    }
    ScsiAgentSnapshot snapshot() const {
        std::lock_guard<std::mutex> l(mu_);
        return s_;
    }

    // ── Bus side (the selected target's controller) ─────────────────────
    std::uint8_t command(const std::uint8_t* cdb, int cdbLen,
                         std::vector<std::uint8_t>& dataOut,
                         const std::vector<std::uint8_t>& dataIn) {
        std::lock_guard<std::mutex> l(mu_);
        if (!handles(cdb, cdbLen)) return 0x02;
        if (cdb[0] == kPoll) {
            ++s_.polls;
            std::size_t alloc = cdb[4] ? cdb[4] : kPayload;
            if (alloc > kPayload) alloc = kPayload;
            dataOut.assign(kPayload, 0);
            std::memcpy(dataOut.data(), "POMA", 4);
            dataOut[4] = 1;
            dataOut[5] = s_.pending ? s_.pendingKind : std::uint8_t(None);
            dataOut[6] = s_.pending ? s_.pendingId : 0;
            dataOut[7] = s_.pending ? seq_ : 0;
            dataOut.resize(alloc);
            return 0x00;
        }
        // REPORT
        if (dataIn.size() < 12 || std::memcmp(dataIn.data(), "POMR", 4) != 0)
            return 0x02;
        s_.reported = true;
        s_.lastKind = dataIn[5];
        s_.lastId = dataIn[6];
        s_.lastErr = std::int16_t(std::uint16_t(dataIn[8]) << 8 | dataIn[9]);
        s_.lastDrive = int(std::uint16_t(dataIn[10]) << 8 | dataIn[11]);
        s_.lastText.clear();
        if (dataIn.size() > 12) {
            std::size_t n = dataIn[12];
            if (n > dataIn.size() - 13) n = dataIn.size() - 13;
            s_.lastText.assign(reinterpret_cast<const char*>(dataIn.data() + 13), n);
        }
        if (s_.pending && dataIn[7] == seq_) s_.pending = false;
        return 0x00;
    }

private:
    mutable std::mutex mu_;
    ScsiAgentSnapshot s_;
    std::uint8_t seq_ = 0;
};

} // namespace pom68k
