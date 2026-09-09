// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// GUI composition for the two host serial endpoints. The transport itself is
// headless; this unit owns only product policy, SCC port identity and messages.

#include "GuiHostServices.h"

#include <cstdio>
#include <memory>

namespace pom68k::gui {

void GuiHostServices::configureSerial() {
    const app::NetworkConfig& network = config_.network();
    const auto start = [this, &network](
        int channel, const app::SerialPortConfig& config, const char* name) {
        using ConfigKind = app::SerialTransportKind;
        if (config.kind == ConfigKind::Disabled) return;
        if (config.kind == ConfigKind::Invalid) {
            std::fprintf(stderr,
                "serial %s: invalid endpoint '%s' (use pty or tcp:<port>)\n",
                name, config.requested.c_str());
            return;
        }
        if (channel == 0 && (network.appleTalk || network.ltoUdp)) {
            std::fprintf(stderr,
                "serial printer: unavailable while AppleTalk/LToUDP owns SCC "
                "channel B; set POM68K_APPLETALK=0 and leave POM68K_LTOUDP unset\n");
            return;
        }
        auto transport = std::make_unique<SerialHostTransport>();
        const auto kind = config.kind == ConfigKind::Pty
            ? SerialHostTransport::Kind::Pty : SerialHostTransport::Kind::Tcp;
        if (!transport->start(kind, config.tcpPort)) {
            std::fprintf(stderr, "serial %s: cannot open endpoint '%s'\n",
                         name, config.requested.c_str());
            return;
        }
        std::fprintf(stderr, "serial %s: %s\n", name,
                     transport->endpoint().c_str());
        serial_[std::size_t(channel)] = std::move(transport);
    };
    start(0, config_.devices().serialPrinter, "printer");
    start(1, config_.devices().serialModem, "modem");
}

} // namespace pom68k::gui
