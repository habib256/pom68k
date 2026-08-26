// POM68K — value decoding for typed legacy startup options.
// The deliberately distinct boolean and integer policies preserve historical
// command-line compatibility while keeping those rules out of domain parsers.

#pragma once

#include "StartupOptions.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace pom68k::startup_value {

template <class Option>
concept TextStartupOption =
    std::remove_cvref_t<Option>::value.kind == StartupValueKind::Text ||
    std::remove_cvref_t<Option>::value.kind == StartupValueKind::NonemptyText ||
    std::remove_cvref_t<Option>::value.kind == StartupValueKind::Custom;

template <class Option>
concept HexStartupOption =
    std::remove_cvref_t<Option>::value.kind == StartupValueKind::HexInteger ||
    std::remove_cvref_t<Option>::value.kind ==
        StartupValueKind::NonemptyHexInteger;

template <TextStartupOption Option>
std::optional<std::string_view> text(
    Option, std::optional<std::string_view> raw) {
    if constexpr (Option::value.kind == StartupValueKind::NonemptyText)
        if (raw && raw->empty()) return std::nullopt;
    return raw;
}

template <BooleanStartupOption Option>
bool boolean(Option, std::optional<std::string_view> raw, bool fallback) {
    if (!raw) return fallback;
    if constexpr (Option::value.kind == StartupValueKind::EnabledBoolean) {
        return !raw->empty() && *raw != "0";
    } else if constexpr (
        Option::value.kind == StartupValueKind::ExactZeroBoolean) {
        return *raw != "0";
    } else if constexpr (
        Option::value.kind == StartupValueKind::LeadingZeroBoolean) {
        return raw->empty() ? fallback : raw->front() != '0';
    } else if constexpr (
        Option::value.kind == StartupValueKind::NumericBoolean) {
        return std::atoi(std::string(*raw).c_str()) != 0;
    } else {
        if (raw->empty()) return fallback;
        const char first = raw->front();
        return first != '0' && first != 'n' && first != 'N' &&
               first != 'f' && first != 'F';
    }
}

template <IntegerStartupOption Option>
std::optional<int> integer(Option, std::optional<std::string_view> raw) {
    if (!raw) return std::nullopt;
    if constexpr (
        Option::value.kind == StartupValueKind::DecimalBoundedInteger)
        if (raw->empty()) return std::nullopt;

    const std::string value(*raw);
    if constexpr (
        Option::value.kind == StartupValueKind::NonnegativeDecimalInteger) {
        return std::max(0, std::atoi(value.c_str()));
    } else if constexpr (
        Option::value.kind == StartupValueKind::DecimalInteger) {
        return std::atoi(value.c_str());
    } else if constexpr (
        Option::value.kind == StartupValueKind::AutoBaseInteger) {
        return int(std::strtol(value.c_str(), nullptr, 0));
    } else if constexpr (
        Option::value.kind == StartupValueKind::AutoBaseBoundedInteger) {
        const long parsed = std::strtol(value.c_str(), nullptr, 0);
        return parsed < Option::value.minimum || parsed > Option::value.maximum
            ? std::nullopt : std::optional<int>(int(parsed));
    } else {
        const int parsed = std::atoi(value.c_str());
        return parsed < Option::value.minimum || parsed > Option::value.maximum
            ? std::nullopt : std::optional<int>(parsed);
    }
}

template <HexStartupOption Option>
std::optional<std::uint32_t> hexadecimal(
    Option, std::optional<std::string_view> raw) {
    if (!raw) return std::nullopt;
    if constexpr (Option::value.kind == StartupValueKind::NonemptyHexInteger)
        if (raw->empty()) return std::nullopt;
    return std::uint32_t(
        std::strtoul(std::string(*raw).c_str(), nullptr, 16));
}

template <class Option>
requires (Option::value.kind == StartupValueKind::TraceLimit)
long traceLimit(Option, std::optional<std::string_view> raw,
                long defaultWhenPresent) {
    if (!raw) return 0;
    const long parsed = std::atol(std::string(*raw).c_str());
    return parsed > 1 ? parsed : defaultWhenPresent;
}

} // namespace pom68k::startup_value
