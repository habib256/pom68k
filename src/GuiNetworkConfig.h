// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "AtalkHub.h"
#include "RuntimeConfig.h"

namespace pom68k::gui {

// The relaunch line's view of the services' identity ⇄ the hub's own
// Config. Both directions live here, next to each other, because a relaunch
// must read back exactly what the window applied (NetworkWindow.cpp).
inline void applyNetworkConfig(AtalkHub& hub, const app::NetworkConfig& network) {
    AtalkHub::Config next = hub.config();
    if (!network.shareDirectory.empty()) next.shareDir = network.shareDirectory;
    if (network.serverName) next.serverName = *network.serverName;
    if (network.volumeName) next.volName = *network.volumeName;
    if (network.printerName) next.printerName = *network.printerName;
    if (network.spoolDirectory) next.spoolDir = *network.spoolDirectory;
    if (network.printQueue) next.printQueue = *network.printQueue;
    if (network.printOptions) next.printOptions = *network.printOptions;
    if (network.gateway) AtalkHub::parseCidr(*network.gateway, next.gwIp, next.gwMask);
    if (network.dns) AtalkHub::parseIpv4(*network.dns, next.dns);
    if (network.etherTalk) next.ethertalk = *network.etherTalk != "0";
    hub.reconfigure(next);
}
inline app::NetworkConfig networkConfigOf(const AtalkHub::Config& cfg) {
    app::NetworkConfig network;
    network.shareDirectory = cfg.shareDir;
    network.serverName = cfg.serverName;
    network.volumeName = cfg.volName;
    network.printerName = cfg.printerName;
    network.spoolDirectory = cfg.spoolDir;
    network.printQueue = cfg.printQueue;
    network.printOptions = cfg.printOptions;
    network.gateway = AtalkHub::formatCidr(cfg.gwIp, cfg.gwMask);
    network.dns = AtalkHub::formatIpv4(cfg.dns);
    network.etherTalk = cfg.ethertalk ? "1" : "0";
    return network;
}

} // namespace pom68k::gui
