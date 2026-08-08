// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── DaynaPort SCSI/Link: an Ethernet card that answers SCSI commands ──
// The era's answer to "this Mac has no NuBus slot and no Ethernet": a box on
// the SCSI chain that a driver drives with READ(6) to pull a received frame
// and WRITE(6) to push one out. For POM68K it is the second kind of target
// the bus has ever carried (see ScsiTarget.h) and the only one that gives a
// guest real Ethernet — every machine here has a SCSI bus, and only some can
// take a NuBus Ethernet card.
//
// What this buys over the existing LocalTalk path: the in-process AppleTalk
// stack (`AtalkStack`) reaches the guest through the SCC at LLAP speed —
// 230.4 kbit/s, and the SCC is the most timing-fragile device in the tree.
// The SCSI bus is neither. Wired to `EtherLink` + `MacIpGateway`, MacTCP and
// Open Transport get IP over Ethernet instead of IP-in-DDP.
//
// Source of truth: Dayna's own SLINKCMD.TXT (bitsavers, apple/scsi/dayna/)
// as implemented by PiSCSI cpp/devices/scsi_daynaport.cpp (BSD-3-Clause,
// GPL-compatible) — the command set, the 6-byte READ header, the two WRITE
// payload formats and the 37-byte INQUIRY the Mac driver insists on all come
// from there. MAME models no SCSI Ethernet target at all, so there is no
// higher-ranked oracle to defer to.
//
// NOT modelled, each deliberately:
//   • multicast filtering — SET MULTICAST ADDRESS ($0D) is accepted and its
//     list discarded. The link this target sits on is point-to-point (one
//     gateway, one guest), so there is nothing to filter out;
//   • the dropped-packet report (flags $FFFFFFFF) — the Rx ring drops when
//     full and counts it, but never tells the guest, exactly as PiSCSI does.
//     Reopening condition: a driver observed to depend on the report;
//   • save states — a snapshot restores with an empty Rx ring. Frames in
//     flight are not guest state, and adding a device to a machine's chunk
//     list would change the on-disk format for every existing .pomss.
//
// Gate: tests/daynaport_test.cpp.

#pragma once
#include "ScsiTarget.h"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

class DaynaPort : public ScsiTarget {
public:
    // A real SCSI/Link holds roughly 6 KB of received frames; past that a
    // real one drops. Modelling the ceiling matters more than its exact
    // value: an unbounded ring would turn a guest that stops polling into a
    // memory leak that looks like a host problem.
    static constexpr std::size_t kRxRingBytes = 6 * 1024;
    static constexpr std::size_t kMaxFrame = 1514;   // DIX, no FCS

    // The target only exists on the bus once the machine attaches it.
    void attach(bool on = true) { attached_ = on; }
    bool present() const override { return attached_; }

    // ENABLE INTERFACE ($0E) — the driver's own on/off switch. A disabled
    // interface still answers SCSI; it just carries no traffic, and losing
    // the guest-set MAC on disable is the documented hardware behaviour.
    bool enabled() const { return enabled_; }

    // ── Link side ───────────────────────────────────────────────────────
    // A frame the guest transmitted. Raw DIX Ethernet, no FCS: the FCS the
    // guest's driver may have appended is stripped here, because what sits
    // on the other side of this callback is software, not a wire.
    std::function<void(const std::uint8_t*, std::size_t)> sendFrame;
    // A frame arriving for the guest (raw DIX, no FCS). Queued for the next
    // READ(6); dropped and counted if the ring is full.
    void receiveFrame(const std::uint8_t* d, std::size_t n);

    const std::array<std::uint8_t, 6>& mac() const { return mac_; }
    void setMac(const std::array<std::uint8_t, 6>& m) { mac_ = m; }

    // ── SCSI side ───────────────────────────────────────────────────────
    std::uint8_t command(const std::uint8_t* cdb, int cdbLen,
                         std::vector<std::uint8_t>& dataOut,
                         const std::vector<std::uint8_t>& dataIn) override;
    int writeByteCount(const std::uint8_t* cdb, int cdbLen) const override;

    // ── Observability (the GUI network window reads these) ──────────────
    long framesToGuest = 0, framesFromGuest = 0, framesDropped = 0;
    long bytesToGuest = 0, bytesFromGuest = 0;
    std::size_t queued() const { return rx_.size(); }

private:
    void setSense(std::uint8_t key, std::uint8_t asc);

    bool attached_ = false;
    bool enabled_ = false;
    // Dayna Communications' OUI ($00:80:19) with a fixed suffix — the same
    // address PiSCSI ships. A guest that sets its own via SET MAC ($0C/$40)
    // overrides it until the interface is disabled.
    std::array<std::uint8_t, 6> mac_ = { 0x00, 0x80, 0x19, 0x10, 0x98, 0xE3 };
    static constexpr std::array<std::uint8_t, 6> kBuiltinMac =
        { 0x00, 0x80, 0x19, 0x10, 0x98, 0xE3 };

    std::deque<std::vector<std::uint8_t>> rx_;
    std::size_t rxBytes_ = 0;
    std::uint8_t senseKey_ = 0, senseAsc_ = 0;
};
