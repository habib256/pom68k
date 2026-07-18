// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Egret.h"
#include "AdbBus.h"
#include <cstring>
#include <fstream>

namespace {
constexpr int64_t kCpuHz = 15667200;

// Cuda/Egret pseudo commands (Linux include/uapi/linux/cuda.h; $02/$08
// are Egret-specific XPRAM block ops, pinned O6.11 from the LC II ROM's
// wire traffic: read [1,2,1,addr], write [1,8,1,addr,data…])
enum { kAutopoll = 0x01, kReadXPram = 0x02, kGetTime = 0x03, kGetPram = 0x07,
       kWriteXPram = 0x08, kSetTime = 0x09, kPowerDown = 0x0A, kSetPram = 0x0C,
       kSendDfac = 0x0E, kResetSystem = 0x11 };
} // namespace

Egret::Egret(Via6522& via, bool cudaPolarity)
    : via_(via), cudaPolarity_(cudaPolarity) {}

bool Egret::loadPram(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(pram_), sizeof pram_);
    uint8_t s[4];
    if (f.read(reinterpret_cast<char*>(s), 4))
        seconds_ = uint32_t(s[0]) << 24 | uint32_t(s[1]) << 16
                 | uint32_t(s[2]) << 8 | s[3];
    return true;
}

void Egret::savePram(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(pram_), sizeof pram_);
    uint8_t s[4] = { uint8_t(seconds_ >> 24), uint8_t(seconds_ >> 16),
                     uint8_t(seconds_ >> 8), uint8_t(seconds_) };
    f.write(reinterpret_cast<const char*>(s), 4);
}

// Factory XPRAM contents, applied when no battery file carries the
// system software's 'NuMc' validity signature at $0C-$0F. Values =
// Basilisk II's known-good defaults (main.cpp:106-133; study in
// docs/BASILISK_ROM_NOTES.md §5.1/§7.5): $01 = DynWait (don't stall the
// boot waiting for SCSI spin-up), $08-$1F = standard classic-PRAM block,
// $76-$77 = OSDefault MacOS. Unlike Basilisk we do NOT force the 32-bit
// boot-mode byte $8A — the V8 machine handles the ROM's real 24-bit
// startup path. A valid signature also spares the ROM's cold-PRAM
// detours (full-RAM burn-in, PRAM re-init) on a first boot.
void Egret::factoryDefaults() {
    if (pram_[0x0C] == 0x4E && pram_[0x0D] == 0x75
     && pram_[0x0E] == 0x4D && pram_[0x0F] == 0x63) return;
    std::memset(pram_, 0, sizeof pram_);
    pram_[0x0C] = 0x4E; pram_[0x0D] = 0x75;      // 'NuMc'
    pram_[0x0E] = 0x4D; pram_[0x0F] = 0x63;
    pram_[0x01] = 0x80;                          // InternalWaitFlags=DynWait
    pram_[0x08] = 0x13; pram_[0x09] = 0x88;
    pram_[0x0A] = 0x00; pram_[0x0B] = 0xCC;
    pram_[0x10] = 0xA8; pram_[0x11] = 0x00;      // standard PRAM values
    pram_[0x12] = 0x00; pram_[0x13] = 0x22;
    pram_[0x14] = 0xCC; pram_[0x15] = 0x0A;
    pram_[0x16] = 0xCC; pram_[0x17] = 0x0A;
    pram_[0x1C] = 0x00; pram_[0x1D] = 0x02;
    pram_[0x1E] = 0x63; pram_[0x1F] = 0x00;
    pram_[0x76] = 0x00; pram_[0x77] = 0x01;      // OSDefault = MacOS
    // Built-in video sPRAM: $80 = ROM-initialised flag, low bits = mode
    // index (0 = 1 bpp … 3 = 8 bpp). The ROM writes $80 on a cold boot
    // (B&W); picking "256 colors" in Monitors + Restart writes $83, and
    // the next boot comes up 8 bpp from the ROM onward (verified against
    // the real LC II ROM on the O6 machine). Seed color for first boots.
    pram_[0x58] = 0x83;
    // AppleTalk stays INACTIVE via SPConfig = XPRAM $13 above: classic
    // PRAM byte 3, low nibble = port B use, 1 = useATalk, 2 = useAsync
    // (Apple system source, Patches Release Notes #1032330 + supermario
    // BeforePatches.a). $22 = both ports async ⇒ the LAP Manager never
    // opens LocalTalk (O6.11). $E0-$E3 (LAPMgrEqu.a ATalkPRAM) is only
    // the CONNECTION selector (low byte = 'atlk' id, 0 = built-in
    // LocalTalk); Basilisk's $00F1000A there does NOT disable anything —
    // a bad id falls back to built-in (NetBootlmgr.a InstallE) — so
    // leave it 0, which is also what the ROM's own XPRAM re-init does.
}

void Egret::reset() {
    phase_ = IDLE;
    held_ = true;
    holdTimer_ = kResetHold;
    xcvr_ = false;
    lastPb_ = 0;
    delay_ = 0;
    cmd_.clear();
    resp_.clear();
    pending_.clear();
    autopoll_ = false;
    firstTick_ = true;
    quiet_ = 0;
    pollAcc_ = 0;
    secAcc_ = 0;                 // else a warm reset fires the 1-second heartbeat early
    // seconds_/pram_ survive (battery-backed, like the Plus RTC)
}

void Egret::portBChanged(uint8_t pb) {
    // Cuda flavor: fold the active-low TIP/BYTEACK into the Egret's
    // active-high view (see Egret.h)
    if (cudaPolarity_) pb ^= 0x30;
    const uint8_t rose = uint8_t(pb & ~lastPb_);
    const uint8_t fell = uint8_t(~pb & lastPb_);
    lastPb_ = pb;
    if (onEdge && ((rose | fell) & 0x30)) onEdge(pb, phase_, xcvr_);

    // Response pacing, decoded from the ROM's own driver ($A14D4E-$A14D90
    // + subroutines $A14E4A/$A14E6A/$A14E8A): byte 0 is read WITHOUT a
    // PB4 ack right after the command; the host then raises SYS_SESSION
    // again (→ byte 1) and acks each further byte with a PB4 rise-read-
    // fall — the FALL means "consumed, clock the next one". The pending-
    // SR guard keeps multiple triggers from skipping bytes.
    if (rose & 0x20) {                   // SYS_SESSION rise
        // Q6 Cuda: a command session only begins when the host has actually
        // LOADED a command byte into the SR (srHostWritten). Between real
        // transactions the Quadra ROM toggles TIP/BYTEACK to poll the idle
        // bus WITHOUT writing the SR (device-manager Cuda path $408A9Cxx);
        // the old code grabbed the stale SR (our last $AA sync ack) as
        // "byte 0", built a ghost `AA AA AA AA AA` command, and answered
        // with the unknown-type error report `{01,02,00,AA}` — whose status
        // byte $02 was captured by the device-manager receiver as the
        // _GetOSDefault result (→ wanted ddType $00, the block-0 re-read
        // loop). Gating on srHostWritten() rejects that ghost session so
        // OSDefault reads PRAM $76:$77 = $0001. The Egret (non-cuda) path
        // is unchanged.
        if (phase_ == IDLE && (!cudaPolarity_ || via_.srHostWritten())) {
            phase_ = HOST_CMD;
            cmd_.clear();
            // Q5 Cuda: the FIRST command byte is already in the SR when
            // TIP falls — the Cuda clocks it out at once; the SHIFT IFR
            // ack is what the ROM polls ($408B3B22-32: move SR, bclr
            // TIP, btst IFR.2)
            if (cudaPolarity_) {
                hostByte(via_.srValue());
                via_.loadSR(via_.srValue());   // SHIFT ack, SR untouched
            }
        } else if (phase_ == RESP_SEND && !via_.shiftPending()) {
            delay_ = kByteDelay;
        }
    }

    if (rose & 0x10) {                   // VIA_FULL rise: host byte ready
        // Q5 Cuda startup sync: with the bus idle, the ROM toggles
        // BYTEACK and expects the Cuda to acknowledge by asserting
        // TREQ until the toggle is released (observed at $408A9F6A:
        // ORB poll -> BYTEACK low -> wait TREQ low -> BYTEACK high).
        if (phase_ == IDLE && cudaPolarity_ && !(pb & 0x20)) {
            // ...and clocks one byte through the VIA shift register —
            // the ROM waits for IFR bit 2 after each BYTEACK edge
            // ($408A9FB0/$408A9FE0)
            xcvr_ = true;
            via_.loadSR(0xAA);
        } else
        if (phase_ == HOST_CMD) {
            hostByte(via_.srValue());
            // Cuda: every accepted command byte is SHIFT-acked
            if (cudaPolarity_) via_.loadSR(via_.srValue());
        }
        // In RESP_SEND the rise happens BEFORE the host reads the SR
        // (ROM $A14E72-76) — loading here would overwrite the byte.
        // (RESP_SEND: nothing to do — the byte is clocked on the FALL)
        // Q5 Cuda flavor: BYTEACK is a per-byte TOGGLE (via-cuda.c
        // `via[B] ^= TACK`) — EVERY transition consumes/clocks a byte,
        // not just the falling half.
        else if (phase_ == RESP_SEND && cudaPolarity_) {
            if (!via_.shiftPending()) delay_ = kByteDelay;
        }
    }
    if (fell & 0x10) {                   // VIA_FULL fall: byte consumed
        if (phase_ == IDLE && cudaPolarity_) {
            xcvr_ = false;               // startup-sync release
            // The second sync byte must land AFTER the host's SR read
            // that follows its TREQ-high check ($408A9FDC clears the
            // SHIFT flag before waiting on it) — clock it a few µs late.
            syncDelay_ = kByteDelay;
        }
        // Cuda: BYTEACK toggles on every byte — the falling half also
        // delivers a command byte during a host session
        if (phase_ == HOST_CMD && cudaPolarity_) {
            hostByte(via_.srValue());
            via_.loadSR(via_.srValue());
        }
        if (phase_ == RESP_SEND) {
            if (!via_.shiftPending()) delay_ = kByteDelay;
        }
    }

    if (fell & 0x20) {                   // SYS_SESSION drop: command complete
        // Q5 Cuda: the session close is SHIFT-acked too — after TIP
        // rises the ROM waits one final SR byte ($408B3BA4-3BB6:
        // ori #$30 -> wait TREQ high -> wait IFR.2 -> read SR)
        if (cudaPolarity_) syncDelay_ = kByteDelay;
        if (phase_ == HOST_CMD) endCommand();
        // In RESP_SEND the host is done consuming: XPRAM reads are a
        // stream with no wire length — the ROM driver takes its count
        // then drops SYS_SESSION and spins on PB3 until we release
        // XCVR_SESSION ($40A149C4: bclr #5 then btst #3 loop). Abort
        // the rest of the reply and free the bus (O6.11).
        else if (phase_ == RESP_SEND) {
            xcvr_ = false;
            phase_ = IDLE;
            resp_.clear();
            initiated_ = false;
            delay_ = 0;
            quiet_ = kQuietDelay;
        }
    }
}

// A host byte is "clocked" through the shift register: raise the SR
// interrupt as the 8 CB1 pulses would (via-cuda.c per-byte handshake)
void Egret::hostByte(uint8_t b) {
    cmd_.push_back(b);
    if (onByte) onByte(true, b);
    via_.raiseShift();
}

void Egret::endCommand() {
    phase_ = IDLE;
    if (!cmd_.empty()) process(cmd_);
    cmd_.clear();
}

void Egret::queueResponse(std::vector<uint8_t> resp) {
    resp_ = std::move(resp);
    respIdx_ = 0;
    phase_ = RESP_DELAY;
    initiated_ = false;
    delay_ = kByteDelay * 2;             // Egret "thinks", then raises XCVR
}

void Egret::loadNextByte() {
    if (respIdx_ >= resp_.size()) {      // safety: nothing left
        xcvr_ = false;
        phase_ = IDLE;
        resp_.clear();
        return;
    }
    uint8_t b = resp_[respIdx_++];
    if (onByte) onByte(false, b);
    // XCVR_SESSION drops WITH the last byte: the host's per-byte "more?"
    // check (btst #3 right after the SR read, ROM $A4A444) must already
    // see it deasserted, or its untimed SHIFT wait deadlocks. The quiet
    // gap keeps the next Egret-initiated packet from re-asserting XCVR
    // before the host's end-of-session check ($A15424) has seen it low.
    if (respIdx_ >= resp_.size()) {
        xcvr_ = false;
        phase_ = IDLE;
        resp_.clear();
        initiated_ = false;
        quiet_ = kQuietDelay;
    }
    via_.loadSR(b);                      // SR interrupt per byte (CB1 ×8)
}

void Egret::process(const std::vector<uint8_t>& cmd) {
    const uint8_t type = cmd[0];

    // Reply shape (oracle = the ROM's own drivers, $A14D4E-$A14D9E and
    // $A4A1EA-$A4A3C0): [sync, status0, status1, cmdEcho, data…].
    // The sync byte is read without an ack and discarded ($A14D56,
    // $A4A374); the two status bytes are length-checked only (their
    // reads fail on early XCVR drop; an error report carries 2 — the
    // $A14D9C caller tests it); byte 3 must echo the command ($A4A3A4
    // for GetPram, $A4A234 for SetPram where it is also the last byte);
    // data follows. XCVR_SESSION must drop exactly with the last byte.

    if (type == 0x00) {                  // ADB packet: [0, adbcmd, data…]
        if (cmd.size() < 2) return;
        const uint8_t adbCmd = cmd[1];
        std::vector<uint8_t> reply = { 0x01, 0x00, 0x00, adbCmd };
        if (adb_) {
            auto data = adb_->command(adbCmd,
                std::vector<uint8_t>(cmd.begin() + 2, cmd.end()));
            reply.insert(reply.end(), data.begin(), data.end());
        }
        queueResponse(std::move(reply));
        return;
    }

    if (type == 0x01) {                  // pseudo: [1, cmd, args…]
        if (cmd.size() < 2) return;
        const uint8_t c = cmd[1];
        std::vector<uint8_t> reply = { 0x01, 0x00, 0x00, c };
        switch (c) {
        case kGetTime:
            reply.push_back(uint8_t(seconds_ >> 24));
            reply.push_back(uint8_t(seconds_ >> 16));
            reply.push_back(uint8_t(seconds_ >> 8));
            reply.push_back(uint8_t(seconds_));
            break;
        case kSetTime:
            if (cmd.size() >= 6)
                seconds_ = uint32_t(cmd[2]) << 24 | uint32_t(cmd[3]) << 16
                         | uint32_t(cmd[4]) << 8 | cmd[5];
            break;
        case kReadXPram:                 // [1, 2, 1, addr] → byte STREAM
            // Q6.2: the Quadra Cuda's ReadXPram reply carries NO command-
            // echo byte — its header is 3 bytes [sync, status0, status1]
            // and the XPRAM data follows directly. The device-manager
            // receive ISR ($408A9BC0-$408A9C30) reads exactly that header
            // count then copies the data; if we echo the command as a 4th
            // header byte, the ROM captures cmdEcho($02) as data[0] and
            // _GetOSDefault (ReadXPram $76) returns $0200 → wanted ddType
            // $00 → the Start Manager's boot-driver scan matches no DDM
            // descriptor and re-reads block 0 forever. Dropping the echo
            // makes _GetOSDefault return PRAM $76:$77 = $0001 (verified
            // byte-identical to the MAME macqd605 golden boot). The LC II
            // Egret path keeps the 4-byte header (its driver echoes the
            // command, comment above), so gate on cudaPolarity_.
            if (cudaPolarity_ && !reply.empty()) reply.pop_back();
            if (cmd.size() >= 4) {       // No length on the wire (O6.11,
                // pinned from the ROM: the 'NuMc' check sends [1,2,1,$0C]
                // and reads 4 bytes, the boot-flag read sends [1,2,1,$8A]
                // and reads ONE — cmd[2] is $01 in every capture, even
                // for the trap's 20-byte requests). The real Egret
                // streams successive XPRAM bytes; the HOST terminates by
                // dropping SYS_SESSION after its count (ROM driver
                // $40A149C4: bclr PB5, wait XCVR release — handled in
                // portBChanged). 32 bytes covers every observed block.
                int addr = cmd[3];
                if (onXPramRead) onXPramRead(addr, 32);
                for (int i = 0; i < 32; i++)
                    reply.push_back(pram_[(addr + i) & 0xFF]);
            }
            break;
        case kWriteXPram:                // [1, 8, 1, addr, data…] — length
            for (size_t i = 4; i < cmd.size(); i++)   // = the data itself
                pram_[(cmd[3] + (i - 4)) & 0xFF] = cmd[i];
            break;                       // ack-only reply
        case kGetPram:                   // [1, 7, addrHi, addrLo] → STREAM
            if (cmd.size() >= 4) {       // Same host-terminated stream as
                // kReadXPram (O6.11): the ROM's SysParam restore reads 16
                // bytes at $10 then 4 at $08 through ONE GetPram each
                // ($40A1559C: recv-count $10, dest $1F8), while the
                // 24-bit reader takes a single byte then drops
                // SYS_SESSION and waits for XCVR release ($A4A3B4-BC).
                int addr = cmd[2] << 8 | cmd[3];
                if (onXPramRead) onXPramRead(addr, 32);
                for (int i = 0; i < 32; i++)
                    reply.push_back(pram_[(addr + i) & 0xFF]);
            }
            break;
        case kSetPram:                   // [1, $C, addrHi, addrLo, value]
            if (cmd.size() >= 5)
                pram_[(cmd[2] << 8 | cmd[3]) & 0xFF] = cmd[4];
            break;
        case kAutopoll:
            autopoll_ = cmd.size() >= 3 && cmd[2] != 0;
            break;
        case kSendDfac:                  // DFAC volume/filter: swallowed
        case kPowerDown:
        case kResetSystem:
        default:
            break;                       // ack-only reply
        }
        queueResponse(std::move(reply));
        return;
    }

    // Unknown packet type → error report (status = 2)
    queueResponse({ 0x01, 0x02, 0x00, type });
}

void Egret::tick(int cpuCycles) {
    if (held_) {
        holdTimer_ -= cpuCycles;
        if (holdTimer_ <= 0) held_ = false;   // releases the 68030
    } else if (firstTick_ && cudaPolarity_) {
        // Q5: the Quadra ROM wants the Cuda IDLE (TREQ high) right after
        // reset — it initiates the first transaction itself. No
        // unsolicited power-on packet in the Cuda flavor; the periodic
        // TIMER packets below stay.
        firstTick_ = false;
    }

    secAcc_ += cpuCycles;
    while (secAcc_ >= kCpuHz) {
        secAcc_ -= kCpuHz;
        seconds_++;
        // "Egret sends these periodically" (via-cuda.c cuda_input,
        // TIMER_PACKET) — the boot ROM waits for them as liveness/clock
        // heartbeats. Lengths pinned against the ROM's readers: the
        // FIRST packet after reset is read with the D1=8 reader at
        // $A15376 (sync + 8 + final = 10 bytes, time included); later
        // ticks use the short reader ($A153C6-flavour: sync + type +
        // final = 3 bytes).
        if (pending_.size() < 4) {
            if (firstTick_ || cudaPolarity_) {
                // Q5: the Quadra ROM's tick reader consumes the LONG
                // form every time (it stalls mid-read on the 3-byte
                // short form) — the Cuda flavor always sends it.
                firstTick_ = false;
                pending_.push_back({ 0x01, 0x03, 0x00,
                                     uint8_t(seconds_ >> 24), uint8_t(seconds_ >> 16),
                                     uint8_t(seconds_ >> 8),  uint8_t(seconds_),
                                     0x00, 0x00, 0x00 });
            } else {
                pending_.push_back({ 0x01, 0x03, uint8_t(seconds_) });
            }
        }
    }

    if (syncDelay_ > 0) {
        syncDelay_ -= cpuCycles;
        if (syncDelay_ <= 0) { syncDelay_ = 0; via_.loadSR(0xAA); }
    }

    if (delay_ > 0) {
        delay_ -= cpuCycles;
        if (delay_ <= 0) {
            delay_ = 0;
            if (phase_ == RESP_DELAY) {  // assert XCVR and clock byte 1
                xcvr_ = true;
                phase_ = RESP_SEND;
                loadNextByte();
            } else if (phase_ == RESP_SEND) {
                loadNextByte();
            }
        }
    }

    // ADB autopoll: when enabled and a device has data, send it as an
    // Egret-initiated packet [sync, ADB(0), status|$40, talkCmd, data…]
    // — via-cuda.c cuda_input: buf[1] & 0x40 marks autopoll data.
    pollAcc_ += cpuCycles;
    if (pollAcc_ >= 172339) {            // ≈11 ms ADB poll period
        pollAcc_ = 0;
        if (autopoll_ && adb_ && adb_->srqPending() && pending_.size() < 2) {
            for (uint8_t addr : { adb_->keyboardAddr(), adb_->mouseAddr() }) {
                uint8_t talk = uint8_t(addr << 4 | 0x0C);   // talk reg 0
                auto data = adb_->command(talk, {});
                if (data.empty()) continue;
                std::vector<uint8_t> pkt = { 0x01, 0x00, 0x40, talk };
                pkt.insert(pkt.end(), data.begin(), data.end());
                pending_.push_back(std::move(pkt));
                break;                   // one device per poll slot
            }
        }
    }

    if (quiet_ > 0) quiet_ -= cpuCycles;

    // NO unilateral retraction of an initiated packet (2026-07-17). Once
    // initiation loads the sync byte into the VIA SR, the host's level-1
    // shift interrupt is already in flight — retracting after that
    // manufactures a "ghost" short session: the host services the L1
    // late (SC2K's per-VBL redraw preempts the byte loop for 300K+
    // cycles), consumes the stale sync, sees XCVR already low, and
    // counts a 1-byte packet; the System's Egret driver then computes
    // the ADB record length as (count 1) - (header 4) = -3 and its
    // dispatcher `dbra` copies 64KB over the stack → the SC2K
    // "coprocesseur absent" bomb (TODO ★, dispatchtrace/coptrace
    // evidence: fatal COPYSETUP D2=$FFFD at clk 8837754661). The real
    // Egret never truncates an initiated transfer — the byte handshake
    // is synchronous and host-clocked; collisions are host-handled (its
    // senders check XCVR first, ROM $A1536C). The old boot-time
    // "bus-quiet deadlock" this abort addressed is covered by the
    // machine gates (egret_test, lcii_boot_etalon — green without it).

    // Egret-initiated transfers: assert XCVR_SESSION and clock the sync
    // byte; the host joins and acks (via-cuda.c "case idle"; ROM reader
    // $A1536C — it may already hold SYS_SESSION high while waiting, and
    // its senders check XCVR first, so collisions are host-handled).
    if (phase_ == IDLE && delay_ == 0 && quiet_ <= 0 && !pending_.empty()) {
        resp_ = pending_.front();
        pending_.erase(pending_.begin());
        respIdx_ = 0;
        xcvr_ = true;
        phase_ = RESP_SEND;
        initiated_ = true;
        loadNextByte();
    }
}
