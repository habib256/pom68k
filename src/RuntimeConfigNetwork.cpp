// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The `--atalk-<key>=<value>` family, both directions: what RuntimeConfig
// reads into NetworkConfig and what a relaunch writes back from it. One
// table names the keys, so the two cannot drift apart. Gate: atalk_hub_test.

#include "RuntimeConfig.h"

#include <algorithm>
#include <array>

namespace pom68k::app {
namespace {

struct AtalkKey {
    std::string_view key;
    std::optional<std::string> NetworkConfig::* field;
};

constexpr std::array<AtalkKey, 6> kAtalkKeys = {{
    {"server", &NetworkConfig::serverName},
    {"volume", &NetworkConfig::volumeName},
    {"printer", &NetworkConfig::printerName},
    {"spool", &NetworkConfig::spoolDirectory},
    {"gateway", &NetworkConfig::gateway},
    {"dns", &NetworkConfig::dns},
}};

} // namespace

bool applyAtalkArgument(NetworkConfig& network, std::string_view argument) {
    if (!argument.starts_with(kAtalkOptionPrefix)) return false;
    const std::string_view rest = argument.substr(kAtalkOptionPrefix.size());
    const std::size_t eq = rest.find('=');
    if (eq == std::string_view::npos) return false;
    const std::string_view key = rest.substr(0, eq);
    const std::string value(rest.substr(eq + 1));
    if (key == "share") {
        network.shareDirectory = value;
        return true;
    }
    for (const AtalkKey& entry : kAtalkKeys)
        if (entry.key == key) {
            network.*entry.field = value;
            return true;
        }
    return false;
}

std::vector<std::string> atalkArguments(std::vector<std::string> arguments,
                                        const NetworkConfig& network) {
    std::erase_if(arguments, [](const std::string& argument) {
        return argument.starts_with(kAtalkOptionPrefix);
    });
    std::vector<std::string> serialized;
    auto emit = [&](std::string_view key, const std::string& value) {
        serialized.push_back(std::string(kAtalkOptionPrefix) +
                             std::string(key) + '=' + value);
    };
    if (!network.shareDirectory.empty()) emit("share", network.shareDirectory);
    for (const AtalkKey& entry : kAtalkKeys)
        if (const auto& value = network.*entry.field) emit(entry.key, *value);
    arguments.insert(arguments.begin(), serialized.begin(), serialized.end());
    return arguments;
}

} // namespace pom68k::app
