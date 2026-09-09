// POM68K — typed host-serial endpoint decoder.

#include "RuntimeConfigParsers.h"

namespace pom68k::app::detail {

SerialPortConfig parseSerialPort(const std::optional<std::string>& value) {
    SerialPortConfig config;
    if (!value) return config;
    config.requested = *value;
    if (*value == "pty") {
        config.kind = SerialTransportKind::Pty;
        return config;
    }
    constexpr std::string_view prefix = "tcp:";
    if (!std::string_view(*value).starts_with(prefix)) {
        config.kind = SerialTransportKind::Invalid;
        return config;
    }
    const std::string_view portText =
        std::string_view(*value).substr(prefix.size());
    if (portText.empty()) {
        config.kind = SerialTransportKind::Invalid;
        return config;
    }
    std::uint32_t port = 0;
    for (const char digit : portText) {
        if (digit < '0' || digit > '9') {
            config.kind = SerialTransportKind::Invalid;
            return config;
        }
        port = port * 10 + std::uint32_t(digit - '0');
        if (port > 65535) {
            config.kind = SerialTransportKind::Invalid;
            return config;
        }
    }
    config.kind = SerialTransportKind::Tcp;
    config.tcpPort = std::uint16_t(port);
    return config;
}

} // namespace pom68k::app::detail
