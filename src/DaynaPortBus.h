// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── A machine's SCSI bus, populated from its configuration ──
// Every machine here carries a SCSI bus, NCR 5380 or 53C96, and both
// controllers take any `ScsiTarget` (ScsiTarget.h). So what a constructor
// does with its bus is the same everywhere: configure its disk slots, and put
// the DaynaPort SCSI/Link on it when the configuration asks — one call,
// `configureScsiBus`, plus a `DaynaPort` member and a `daynaPort()` accessor,
// which is what `AtalkHub` and `GuiHostServices` detect with a `requires`
// clause to wire the card to the in-process NAT. No registry.
//
// The card is opt-in and OFF by default: a new device answering selection
// changes what the ROM's bus probe finds, and every boot etalon is calibrated
// against a bus with only disks on it. POM68K_DAYNAPORT=<id> puts it at that
// ID (=1 means "pick the default", ID 3 — where MAME parks the CD-ROM, so
// choose another if a disc is mounted; decoded in RuntimeConfigCore.cpp into
// `CoreBusConfig::daynaPortId`). The card is on the bus regardless of
// AppleTalk; what it is WIRED to is the in-process NAT in AtalkHub. With
// POM68K_APPLETALK=0 the guest still sees the card and it carries nothing — a
// cable-unplugged state, not a missing device.
//
// Restart and restore: neither controller serializes its target table — a
// snapshot carries the selected target as an ID and re-resolves it — and
// `reset()` keeps it, so attaching once, here, from the session's
// configuration is what survives both. The card's own state is not in save
// states (DaynaPort.h).
//
// Gates: daynaport_ncr5380_test and daynaport_ncr53c96_test (the card through
// both real controllers), and one <family>_dayna_boot_etalon per platform.

#pragma once
#include "CoreConfig.h"
#include "DaynaPort.h"

#include <cstdio>
#include <optional>

namespace pom68k {

template <class Bus>
void attachDaynaPort(DaynaPort& card, Bus& scsi, const std::optional<int>& id) {
    if (!id) return;
    card.attach();
    scsi.attach(&card, *id);
    std::fprintf(stderr, "DaynaPort SCSI/Link at SCSI ID %d (guest needs the "
                 "SCSI/Link driver + a manual MacTCP address in the gateway's "
                 "subnet)\n", *id);
}

template <class Bus, class Disks>
void configureScsiBus(Bus& scsi, Disks& disks, DaynaPort& card,
                      const CoreConfig& config) {
    for (auto& disk : disks) disk.configure(config.storage);
    attachDaynaPort(card, scsi, config.bus.daynaPortId);
}

} // namespace pom68k
