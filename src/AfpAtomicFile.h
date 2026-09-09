// Durable replacement for the AFP catalogue and AppleDouble sidecars.
#pragma once
#include <filesystem>
#include <span>
#include <cstdint>

bool afpSyncDirectory(const std::filesystem::path& directory);
bool afpAtomicWrite(const std::filesystem::path& destination,
                    const std::filesystem::path& temporary,
                    std::span<const uint8_t> bytes);
