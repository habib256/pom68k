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
        int channel, const app::SerialPortConfig& config, const char* name,
        const char* label) {
        using ConfigKind = app::SerialTransportKind;
        GuiSerialPortState& port = state_.serial.ports[std::size_t(channel)];
        port.name = label;
        port.requested = config.requested;
        auto refuse = [&](std::string message) {
            std::fprintf(stderr, "serial %s: %s\n", name, message.c_str());
            port.message = std::move(message);
        };
        if (config.kind == ConfigKind::Disabled) return;
        if (config.kind == ConfigKind::Invalid)
            return refuse("invalid endpoint '" + config.requested +
                          "' (use pty, tcp:<port> or terminal)");
        if (channel == 0 && (network.appleTalk || network.ltoUdp))
            return refuse("unavailable while AppleTalk/LToUDP owns SCC channel B; "
                          "set POM68K_APPLETALK=0 and leave POM68K_LTOUDP unset");
        if (config.kind == ConfigKind::Terminal) {
            port.endpoint = "terminal intégré";
            port.terminal = std::make_shared<SerialTerminal>(true);
            std::fprintf(stderr, "serial %s: the Ports série window is the endpoint\n", name);
            return;
        }
        auto transport = std::make_unique<SerialHostTransport>();
        const auto kind = config.kind == ConfigKind::Pty
            ? SerialHostTransport::Kind::Pty : SerialHostTransport::Kind::Tcp;
        if (!transport->start(kind, config.tcpPort))
            return refuse("cannot open endpoint '" + config.requested +
                          (kind == SerialHostTransport::Kind::Tcp
                               ? "' (the port may already be in use)" : "'"));
        std::fprintf(stderr, "serial %s: %s\n", name, transport->endpoint().c_str());
        port.endpoint = transport->endpoint();
        port.terminal = std::make_shared<SerialTerminal>(false);
        serial_[std::size_t(channel)] = std::move(transport);
    };
    start(0, config_.devices().serialPrinter, "printer", "Imprimante (canal B)");
    start(1, config_.devices().serialModem, "modem", "Modem (canal A)");
}

} // namespace pom68k::gui
