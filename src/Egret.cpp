// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Egret.h"
#include "AdbBus.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace {
// Cuda/Egret pseudo commands (Linux include/uapi/linux/cuda.h +
// DingusPPC viacuda.h). $02/$08 are READ/WRITE_MCU_MEM with a 16-bit
// MCU-space address — PRAM lives at $0100-$01FF, MCU scratch RAM at
// $0000-$00FF (the System's parameter block at $B3 round-trips through
// it; observed [1,8,0,B3,…] + [1,2,0,A1] on both LC II and Q605 boots).
enum { kAutopoll = 0x01, kReadMcu = 0x02, kGetTime = 0x03, kGetPram = 0x07,
       kWriteMcu = 0x08, kSetTime = 0x09, kPowerDown = 0x0A, kSetPram = 0x0C,
       kSendDfac = 0x0E, kResetSystem = 0x11, kSetRate = 0x14,
       kGetRate = 0x16, kSetBitmap = 0x19, kGetBitmap = 0x1A,
       kOneSecMode = 0x1B };
} // namespace

Egret::Egret(Via6522& via, bool cudaPolarity, int clockHz)
    : via_(via), cudaPolarity_(cudaPolarity), clockHz_(clockHz) {}

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
    // Always (re)seed AppleTalk-inactive SPConfig even when 'NuMc' is already
    // present — AppleTalk 57.x self-heals 0/$F → active, and Infinite Mac
    // Sys7 images leave SysParam/XPRAM selecting EtherTalk/LocalTalk. Same
    // policy as Rtc::factoryDefaults (Mac II).
    const bool hadSig = pram_[0x0C] == 0x4E && pram_[0x0D] == 0x75
                     && pram_[0x0E] == 0x4D && pram_[0x0F] == 0x63;
    if (!hadSig)
        std::memset(pram_, 0, sizeof pram_);
    pram_[0x0C] = 0x4E; pram_[0x0D] = 0x75;      // 'NuMc'
    pram_[0x0E] = 0x4D; pram_[0x0F] = 0x63;
    pram_[0x01] = 0x80;                          // InternalWaitFlags=DynWait
    pram_[0x08] = 0x13; pram_[0x09] = 0x88;
    pram_[0x0A] = 0x00; pram_[0x0B] = 0xCC;
    pram_[0x10] = 0xA8; pram_[0x11] = 0x00;      // standard PRAM values
    // SPConfig low nibble = port B use (1 = AppleTalk, 2 = async — see the
    // block comment below). POM68K_APPLETALK=1 seeds LocalTalk ACTIVE for
    // headless LLAP tests; the default stays deterministic async ($22).
    pram_[0x12] = 0x00;
    pram_[0x13] = std::getenv("POM68K_APPLETALK") ? 0x21 : 0x22;
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
    if (!hadSig || (pram_[0x58] & 0x80) == 0)
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
    syncDelay_ = 0;
    ackDelay_ = 0;
    treqDelay_ = 0;
    cmd_.clear();
    resp_.clear();
    respIdx_ = 0;
    streamSrc_ = NO_STREAM;
    streamAddr_ = 0;
    pending_.clear();
    autopoll_ = false;
    pollRate_ = 11;
    deviceMap_[0] = deviceMap_[1] = 0;
    oneSecMode_ = 1;             // power-on boot heartbeat; the LC II ROM
    oneSecFirst_ = true;         // sends $1B 00 (off) in its first commands
    quiet_ = 0;
    pollAcc_ = 0;
    secAcc_ = 0;                 // else a warm reset fires the 1-second heartbeat early
    // seconds_/pram_ survive (battery-backed, like the Plus RTC);
    // mcuRam_ survives too (powered scratch RAM)
}

void Egret::portBChanged(uint8_t pb) {
    // Cuda flavor: fold the active-low TIP/BYTEACK into the Egret's
    // active-high view (see Egret.h)
    if (cudaPolarity_) pb ^= 0x30;
    const uint8_t rose = uint8_t(pb & ~lastPb_);
    const uint8_t fell = uint8_t(~pb & lastPb_);
    lastPb_ = pb;
    if (onEdge && ((rose | fell) & 0x30)) onEdge(pb, phase_, xcvr_);

    // ── session open (bit 5 rises in the folded active-high view) ──
    if (rose & 0x20) {
        if (phase_ == RESP_WAIT) {
            // Cuda: TREQ is asserted with a reply pending — the host's
            // TIP fall opens the read session; the first packet byte
            // (the type) is clocked +88 µs later (DingusPPC: the TIP
            // fall edge itself runs the out handler).
            phase_ = RESP_SEND;
            delay_ = usToCycles(kRespByteUs);
        } else if (phase_ == IDLE
                   && (!cudaPolarity_ || via_.srHostWritten())) {
            // Q6 Cuda: a command session only begins when the host has
            // actually LOADED a command byte into the SR (srHostWritten).
            // Between real transactions the Quadra ROM toggles
            // TIP/BYTEACK to poll the idle bus WITHOUT writing the SR
            // (device-manager Cuda path $408A9Cxx) — see the ghost
            // branch below.
            phase_ = HOST_CMD;
            cmd_.clear();
            // Q5 Cuda: the FIRST command byte is already in the SR when
            // TIP falls — the Cuda clocks it out at once; the SHIFT IFR
            // ack (+71 µs) is what the ROM polls ($408B3B22-32: move SR,
            // bclr TIP, btst IFR.2)
            if (cudaPolarity_) hostByte(via_.srValue());
        } else if (phase_ == IDLE && cudaPolarity_) {
            // Ghost session open: acked with a dummy SHIFT like every
            // TIP-low edge (DingusPPC null_out_handler)
            ackDelay_ = usToCycles(kRespByteUs);
        } else if (phase_ == RESP_SEND && !via_.shiftPending()) {
            // Egret flavor: the host read the unacked byte 0 and joins
            // the read session — clock byte 1
            delay_ = cudaPolarity_ ? usToCycles(kRespByteUs) : kByteDelay;
        }
    }

    // ── session close (bit 5 falls) ──
    if (fell & 0x20) {
        // Q5 Cuda: EVERY session close is SHIFT-acked — after TIP rises
        // the ROM waits one final dummy SR byte (+61 µs; $408B3BA4-3BB6:
        // ori #$30 -> wait TREQ high -> wait IFR.2 -> read SR)
        if (cudaPolarity_) ackDelay_ = usToCycles(kCloseAckUs);
        if (phase_ == HOST_CMD) {
            endCommand();
        } else if (phase_ == RESP_SEND || phase_ == RESP_WAIT) {
            // Host done consuming: PRAM/MCU reads are open-ended byte
            // streams with no wire length — the driver takes its count
            // then drops the session and spins on PB3 until we release
            // XCVR_SESSION/TREQ ($40A149C4: bclr #5 then btst #3 loop).
            // Abort the rest of the reply and free the bus (O6.11).
            releaseWire();
            quiet_ = kQuietDelay;
        }
    }

    // ── byte edges (bit 4) ──
    if ((rose | fell) & 0x10) {
        if (pb & 0x20) {                 // in-session: byte transfers
            if (phase_ == HOST_CMD) {
                if (cudaPolarity_) {
                    // Cuda: BYTEACK is a per-byte TOGGLE (via-cuda.c
                    // `via[B] ^= TACK`) — EVERY transition delivers a
                    // command byte
                    hostByte(via_.srValue());
                } else if (rose & 0x10) {
                    // Egret: VIA_FULL rise = byte ready (the fall only
                    // marks it consumed)
                    hostByte(via_.srValue());
                }
            } else if (phase_ == RESP_SEND) {
                // The pending-SR guard keeps multiple triggers from
                // skipping bytes; the byte is clocked when the delay
                // expires. Egret flavor clocks on the FALL only (the
                // rise happens BEFORE the host reads the SR — loading
                // there would overwrite the byte, ROM $A14E72-76).
                if (cudaPolarity_ || (fell & 0x10)) {
                    if (!via_.shiftPending())
                        delay_ = cudaPolarity_ ? usToCycles(kRespByteUs)
                                               : kByteDelay;
                }
            } else if (phase_ == IDLE && cudaPolarity_) {
                // Ghost session byte edge: dummy SHIFT (see above)
                ackDelay_ = usToCycles(kRespByteUs);
            }
        } else if (phase_ == IDLE && cudaPolarity_) {
            // Q5 Cuda startup sync: with the bus idle, the ROM toggles
            // BYTEACK and expects the Cuda to acknowledge by asserting
            // TREQ until the toggle is released (observed at $408A9F6A:
            // ORB poll -> BYTEACK low -> wait TREQ low -> BYTEACK high),
            // clocking one byte through the VIA shift register per edge
            // (the ROM waits for IFR bit 2, $408A9FB0/$408A9FE0).
            if (rose & 0x10) {
                xcvr_ = true;
                via_.loadSR(0xAA);
            } else {
                xcvr_ = false;           // startup-sync release
                // The second sync byte must land AFTER the host's SR
                // read that follows its TREQ-high check ($408A9FDC
                // clears the SHIFT flag before waiting on it).
                syncDelay_ = usToCycles(kCloseAckUs);
            }
        }
    }
}

void Egret::hostByte(uint8_t b) {
    cmd_.push_back(b);
    if (onByte) onByte(true, b);
    // Egret: the byte is acked at once (VIA_FULL handshake, pinned LC II
    // wire). Cuda: the SHIFT ack lands +71 µs later (DingusPPC pacing);
    // the delayed loadSR(srValue()) keeps the SR content and clears the
    // srHostWritten flag exactly like the real per-byte shift would.
    if (cudaPolarity_) ackDelay_ = usToCycles(kCmdAckUs);
    else via_.raiseShift();
}

void Egret::endCommand() {
    phase_ = IDLE;
    if (!cmd_.empty()) process(cmd_);
    cmd_.clear();
}

std::vector<uint8_t> Egret::replyHeader(uint8_t type, uint8_t flags,
                                        uint8_t echo) const {
    // Cuda framing is [type, flags, cmdEcho, …] (DingusPPC
    // response_header, viacuda.cpp:498) — byte 0 IS the packet type.
    if (cudaPolarity_) return { type, flags, echo };
    // The EGRET framing is DIFFERENT: [sync, status0, status1, cmdEcho]
    // where byte 0 is the discarded attention/sync byte ($A14D56) and
    // byte 1 is STATUS-0, always $00 on success — NOT the packet type.
    // The redo wrongly put `type` ($01 for pseudo) in byte 1; the LC II
    // ROM reads that as a status and a non-zero value broke the colour
    // (8 bpp) video bring-up — the desktop rendered as noise while the
    // 1 bpp etalon (which never exercises the colour path) stayed green
    // (2026-07-22 regression from LLE step 7). `flags` maps to status-1
    // (e.g. the $40 ADB-autopoll marker), exactly as before the redo.
    return { 0x01, 0x00, flags, echo };
}

void Egret::queueError(uint8_t err, uint8_t pktType, uint8_t cmd) {
    if (cudaPolarity_) {
        // Cuda error packet [$02, errCode, pktType, cmd] (DingusPPC
        // error_response, viacuda.cpp:509).
        queueResponse({ 0x02, err, pktType, cmd });
        return;
    }
    // Egret: same [sync, status0, status1, cmdEcho] shape as a normal
    // reply, with status0 = $02 marking the error (the LC II ROM's
    // $A14D9C caller only length-checks the two status bytes). Matches
    // the pre-redo `{01, 02, 00, type}`.
    (void)err; (void)pktType;
    queueResponse({ 0x01, 0x02, 0x00, cmd });
}

void Egret::queueResponse(std::vector<uint8_t> resp, StreamSrc stream,
                          uint16_t addr) {
    if (std::getenv("EGRET_CMD_LOG")) {
        std::fprintf(stderr, "[egret] cmd:");
        for (uint8_t b : cmd_) std::fprintf(stderr, " %02X", b);
        std::fprintf(stderr, "  reply:");
        for (uint8_t b : resp) std::fprintf(stderr, " %02X", b);
        if (stream != NO_STREAM)
            std::fprintf(stderr, " +stream@%03X", addr);
        std::fprintf(stderr, "\n");
    }
    resp_ = std::move(resp);
    respIdx_ = 0;
    streamSrc_ = stream;
    streamAddr_ = addr;
    if (cudaPolarity_) {
        // TREQ asserts +13 µs after the command session closed
        // (DingusPPC treq_timer); the host then opens the read session.
        phase_ = IDLE;
        treqDelay_ = usToCycles(kTreqUs);
    } else {
        phase_ = RESP_DELAY;
        delay_ = kByteDelay * 2;         // Egret "thinks", then raises XCVR
    }
}

void Egret::releaseWire() {
    xcvr_ = false;
    phase_ = IDLE;
    resp_.clear();
    respIdx_ = 0;
    streamSrc_ = NO_STREAM;
    delay_ = 0;
}

void Egret::loadNextByte() {
    uint8_t b = 0;
    bool have = false;
    if (respIdx_ < resp_.size()) {
        b = resp_[respIdx_++];
        have = true;
    } else switch (streamSrc_) {
    case STREAM_PRAM: b = pram_[streamAddr_++ & 0xFF]; have = true; break;
    case STREAM_MCU:  b = mcuRam_[streamAddr_++ & 0xFF]; have = true; break;
    case STREAM_ZERO: b = 0; have = true; break;
    case NO_STREAM: break;
    }
    if (!have) { releaseWire(); return; }        // safety: nothing left
    if (onByte) onByte(false, b);
    // XCVR_SESSION/TREQ drops WITH the last byte of a finite reply: the
    // host's per-byte "more?" check (btst #3 right after the SR read,
    // ROM $A4A444; DingusPPC negates TREQ on the last-byte load) must
    // already see it deasserted. Open-ended streams keep it asserted —
    // the HOST terminates those sessions.
    if (respIdx_ >= resp_.size() && streamSrc_ == NO_STREAM) {
        xcvr_ = false;
        if (!cudaPolarity_) {
            // Egret: the bus idles immediately; the quiet gap keeps the
            // next Egret-initiated packet from re-asserting XCVR before
            // the host's end-of-session check ($A15424) has seen it low.
            phase_ = IDLE;
            resp_.clear();
            respIdx_ = 0;
            quiet_ = kQuietDelay;
        }
        // Cuda: stay in RESP_SEND until the host closes the session
        // (the close is what frees the bus and fires the +61 µs ack).
    }
    via_.loadSR(b);                      // SR interrupt per byte (CB1 ×8)
}

void Egret::process(const std::vector<uint8_t>& cmd) {
    const uint8_t type = cmd[0];

    if (type == 0x00) {                  // ADB packet: [0, adbcmd, data…]
        if (cmd.size() < 2) { queueError(3, type, 0); return; }
        const uint8_t adbCmd = cmd[1];
        auto reply = replyHeader(0x00, 0x00, adbCmd);
        if (adb_) {
            auto data = adb_->command(adbCmd,
                std::vector<uint8_t>(cmd.begin() + 2, cmd.end()));
            reply.insert(reply.end(), data.begin(), data.end());
        }
        queueResponse(std::move(reply));
        return;
    }

    if (type != 0x01) {                  // unknown packet type
        queueError(1, type, cmd.size() > 1 ? cmd[1] : 0);
        return;
    }
    if (cmd.size() < 2) { queueError(3, type, 0); return; }

    const uint8_t c = cmd[1];
    auto reply = replyHeader(0x01, 0x00, c);
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
    case kReadMcu: {                     // → byte STREAM, no wire length
        // The two flavors address $02/$08 DIFFERENTLY, and conflating them
        // is what broke LC II colour boots (2026-07-22 regression from LLE
        // step 7): V8 rendered the colour desktop as noise because the ROM
        // read the video-mode sPRAM through here and got zeros.
        if (cmd.size() < 4) break;       // ack-only on a short command
        if (!cudaPolarity_) {
            // EGRET (LC II): [1, 2, 1, addr] — cmd[2] is a fixed marker,
            // cmd[3] the 8-bit offset straight into the 256-byte PRAM
            // (pinned O6.11 from the real LC II ROM traffic; cmd[2] is
            // $00 OR $01 on the wire and is ignored for addressing, as it
            // always was before the redo). The firmware streams
            // successive PRAM bytes; the HOST drops the session after its
            // count ($40A149C4).
            if (onXPramRead) onXPramRead(cmd[3], 32);
            queueResponse(std::move(reply), STREAM_PRAM, cmd[3]);
            return;
        }
        // CUDA (Quadra): real 16-bit MCU addressing (DingusPPC viacuda) —
        // PRAM window $0100-$01FF, MCU scratch RAM below, zeros elsewhere.
        const uint16_t a = uint16_t(uint16_t(cmd[2]) << 8 | cmd[3]);
        if (a >= 0x100 && a <= 0x1FF) {
            if (onXPramRead) onXPramRead(a - 0x100, 32);
            queueResponse(std::move(reply), STREAM_PRAM,
                          uint16_t(a - 0x100));
        } else if (a < 0x100) {
            queueResponse(std::move(reply), STREAM_MCU, a);
        } else {
            queueResponse(std::move(reply), STREAM_ZERO, 0);
        }
        return;
    }
    case kWriteMcu:                      // [1, 8, …, addr, data…] — length
        if (cmd.size() < 4) break;       // = the data itself
        if (!cudaPolarity_) {
            // EGRET: 8-bit XPRAM write straight into PRAM (cmd[3] offset,
            // cmd[2] ignored — matches the pre-redo behaviour and the
            // O6.11 LC II wire, e.g. `01 08 00 B3 …` writes PRAM $B3).
            for (size_t i = 4; i < cmd.size(); i++) {
                const uint8_t w = uint8_t(cmd[3] + (i - 4));
                pram_[w] = cmd[i];
                if (onXPramWrite) onXPramWrite(w, cmd[i]);
            }
            break;
        }
        // CUDA: 16-bit MCU addressing (PRAM $0100-$01FF, MCU scratch below).
        for (size_t i = 4; i < cmd.size(); i++) {
            const uint16_t w =
                uint16_t((uint16_t(cmd[2]) << 8 | cmd[3]) + (i - 4));
            if (w >= 0x100 && w <= 0x1FF) {
                pram_[w & 0xFF] = cmd[i];
                if (onXPramWrite) onXPramWrite(w & 0xFF, cmd[i]);
            } else if (w < 0x100) {
                mcuRam_[w] = cmd[i];
            }
        }
        break;                           // ack-only reply
    case kGetPram: {                     // [1, 7, addrHi, addrLo] → STREAM
        if (cmd.size() < 4) break;
        const uint16_t a = uint16_t(uint16_t(cmd[2]) << 8 | cmd[3]);
        if (a > 0xFF) { queueError(4, type, c); return; }
        if (onXPramRead) onXPramRead(a, 32);
        queueResponse(std::move(reply), STREAM_PRAM, a);
        return;
    }
    case kSetPram: {                     // [1, $C, addrHi, addrLo, data…]
        if (cmd.size() < 5) break;
        const uint16_t a = uint16_t(uint16_t(cmd[2]) << 8 | cmd[3]);
        if (a > 0xFF) { queueError(4, type, c); return; }
        for (size_t i = 4; i < cmd.size(); i++) {
            const uint8_t w = uint8_t(a + (i - 4));
            pram_[w] = cmd[i];
            if (onXPramWrite) onXPramWrite(w, cmd[i]);
        }
        break;
    }
    case kAutopoll:
        autopoll_ = cmd.size() >= 3 && cmd[2] != 0;
        break;
    case kSetRate:
        if (cmd.size() >= 3 && cmd[2] != 0) pollRate_ = cmd[2];
        break;
    case kGetRate:
        reply.push_back(pollRate_);
        break;
    case kSetBitmap:
        if (cmd.size() >= 4) { deviceMap_[0] = cmd[2]; deviceMap_[1] = cmd[3]; }
        break;
    case kGetBitmap:
        reply.push_back(deviceMap_[0]);
        reply.push_back(deviceMap_[1]);
        break;
    case kOneSecMode:                    // $1B: 0 off / 1 full / 2 header
        if (cmd.size() >= 3) {           // / 3 single tick byte (ERS; the
            oneSecMode_ = cmd[2] & 3;    // first packet after a change is
            oneSecFirst_ = true;         // always the full form)
        }
        break;
    case kSendDfac:                      // DFAC volume/filter: swallowed
    case kPowerDown:
    case kResetSystem:
    default:
        break;                           // ack-only reply
    }
    queueResponse(std::move(reply));
}

void Egret::tick(int cpuCycles) {
    if (held_) {
        holdTimer_ -= cpuCycles;
        if (holdTimer_ <= 0) held_ = false;   // releases the CPU
    }

    secAcc_ += cpuCycles;
    while (secAcc_ >= clockHz_) {
        secAcc_ -= clockHz_;
        seconds_++;
        // One-second packets per pseudo command $1B (via-cuda.c
        // cuda_input TIMER_PACKET; DingusPPC one_sec_mode). Power-on
        // default is mode 1: the boot ROMs read the heartbeat before
        // the System reprograms the mode (LC II: $1B 00 then Sys 7.5's
        // $1B 03; Mac OS 8.1 sends $1B 03 — both captured 2026-07-22).
        if (oneSecMode_ != 0 && pending_.size() < 4) {
            const bool full = oneSecFirst_ || oneSecMode_ == 1;
            oneSecFirst_ = false;
            if (!cudaPolarity_) {
                // Egret flavor shapes pinned against the LC II ROM's
                // readers: full = the 10-byte boot heartbeat (D1=8
                // reader at $A15376: sync + 8 + final), short = the
                // 3-byte [sync, TIMER(3), seconds] form ($A153C6).
                if (full)
                    pending_.push_back({ 0x01, 0x03, 0x00,
                        uint8_t(seconds_ >> 24), uint8_t(seconds_ >> 16),
                        uint8_t(seconds_ >> 8),  uint8_t(seconds_),
                        0x00, 0x00, 0x00 });
                else if (oneSecMode_ == 2)
                    pending_.push_back({ 0x01, 0x03, 0x00 });
                else
                    pending_.push_back({ 0x01, 0x03, uint8_t(seconds_) });
            } else {
                // Real Cuda shapes (DingusPPC autopoll_handler): full =
                // header + GetTime echo + 4-byte time, mode 2 = header
                // only, mode 3 = the single CUDA_PKT_TICK byte.
                if (full)
                    pending_.push_back({ 0x01, 0x00, 0x03,
                        uint8_t(seconds_ >> 24), uint8_t(seconds_ >> 16),
                        uint8_t(seconds_ >> 8),  uint8_t(seconds_) });
                else if (oneSecMode_ == 2)
                    pending_.push_back({ 0x01, 0x00, 0x03 });
                else
                    pending_.push_back({ 0x03 });
            }
        }
    }

    if (syncDelay_ > 0) {
        syncDelay_ -= cpuCycles;
        if (syncDelay_ <= 0) { syncDelay_ = 0; via_.loadSR(0xAA); }
    }
    if (ackDelay_ > 0) {
        ackDelay_ -= cpuCycles;
        if (ackDelay_ <= 0) {
            ackDelay_ = 0;
            // Dummy/ack SHIFT with the SR content untouched (the real
            // Cuda clocks whatever is on the line; DingusPPC leaves
            // via_sr). Also clears srHostWritten for the ghost gate.
            via_.loadSR(via_.srValue());
        }
    }
    if (treqDelay_ > 0) {
        treqDelay_ -= cpuCycles;
        if (treqDelay_ <= 0) {
            if (phase_ == IDLE) {
                treqDelay_ = 0;
                xcvr_ = true;            // TREQ: reply ready, host joins
                phase_ = RESP_WAIT;
            } else {
                treqDelay_ = 1;          // host mid-session: retry
            }
        }
    }

    if (delay_ > 0) {
        delay_ -= cpuCycles;
        if (delay_ <= 0) {
            delay_ = 0;
            if (phase_ == RESP_DELAY) {  // Egret: assert XCVR, clock byte 0
                xcvr_ = true;
                phase_ = RESP_SEND;
                loadNextByte();
            } else if (phase_ == RESP_SEND) {
                loadNextByte();
            }
        }
    }

    // ADB autopoll: when enabled and a device has data, send it as an
    // Egret-initiated packet [ADB(0), status|$40, talkCmd, data…]
    // — via-cuda.c cuda_input: buf[1] & 0x40 marks autopoll data.
    pollAcc_ += cpuCycles;
    const int pollPeriod =
        int(int64_t(pollRate_ ? pollRate_ : 11) * clockHz_ / 1000);
    if (pollAcc_ >= pollPeriod) {
        pollAcc_ = 0;
        if (autopoll_ && adb_ && adb_->srqPending() && pending_.size() < 2) {
            // Poll only a device that actually has data pending. The
            // keyboard's Talk R0 always answers ({$FF,$FF} = "no key"),
            // so a blanket "non-empty reply" test would let it claim the
            // slot every poll and the mouse would never be reached
            // (mouse frozen despite srqPending staying high). Gate each
            // address on its own pending flag instead.
            const uint8_t kbd = adb_->keyboardAddr(), mse = adb_->mouseAddr();
            struct { uint8_t addr; bool has; } devs[] = {
                { kbd, adb_->keyPending() }, { mse, adb_->mousePending() } };
            for (auto& d : devs) {
                if (!d.has) continue;
                uint8_t talk = uint8_t(d.addr << 4 | 0x0C);   // talk reg 0
                auto data = adb_->command(talk, {});
                if (data.empty()) continue;
                auto pkt = replyHeader(0x00, 0x40, talk);
                pkt.insert(pkt.end(), data.begin(), data.end());
                pending_.push_back(std::move(pkt));
                break;                   // one device per poll slot
            }
        }
    }

    if (quiet_ > 0) quiet_ -= cpuCycles;

    // NO unilateral retraction of an initiated packet (2026-07-17). Once
    // initiation puts the attention byte on the wire, the host's level-1
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
    // senders check XCVR first, ROM $A1536C).

    // Egret-initiated transfers. Egret flavor: assert XCVR_SESSION and
    // clock the attention byte (= packet byte 0); the host joins and
    // acks (via-cuda.c "case idle"; ROM reader $A1536C). Cuda flavor:
    // assert TREQ and draw attention with a dummy SHIFT +30 µs
    // (DingusPPC autopoll_handler); the packet bytes are clocked only
    // once the host opens the read session.
    if (phase_ == IDLE && delay_ == 0 && quiet_ <= 0 && treqDelay_ == 0
        && resp_.empty() && !pending_.empty()) {
        resp_ = pending_.front();
        pending_.erase(pending_.begin());
        respIdx_ = 0;
        streamSrc_ = NO_STREAM;
        xcvr_ = true;
        if (cudaPolarity_) {
            phase_ = RESP_WAIT;
            ackDelay_ = usToCycles(kAttnUs);
        } else {
            phase_ = RESP_SEND;
            loadNextByte();
        }
    }
}
