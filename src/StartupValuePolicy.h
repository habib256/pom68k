// POM68K — lexical policies carried by typed startup options.

#pragma once

#include <cstdint>

namespace pom68k {

struct StartupValuePolicy {
    enum class Kind : std::uint8_t {
        Presence,
        Text,
        NonemptyText,
        EnabledBoolean,
        ExactZeroBoolean,
        LeadingZeroBoolean,
        NumericBoolean,
        JitBoolean,
        NonnegativeDecimalInteger,
        DecimalInteger,
        AutoBaseInteger,
        AutoBaseBoundedInteger,
        DecimalBoundedInteger,
        HexInteger,
        NonemptyHexInteger,
        TraceLimit,
        Custom,
    };

    Kind kind;
    int minimum = 0;
    int maximum = 0;
    constexpr bool operator==(const StartupValuePolicy&) const = default;
};

using StartupValueKind = StartupValuePolicy::Kind;

namespace startup_policy {
inline constexpr StartupValuePolicy Presence{StartupValueKind::Presence};
inline constexpr StartupValuePolicy Text{StartupValueKind::Text};
inline constexpr StartupValuePolicy NonemptyText{StartupValueKind::NonemptyText};
inline constexpr StartupValuePolicy EnabledBoolean{
    StartupValueKind::EnabledBoolean};
inline constexpr StartupValuePolicy ExactZeroBoolean{
    StartupValueKind::ExactZeroBoolean};
inline constexpr StartupValuePolicy LeadingZeroBoolean{
    StartupValueKind::LeadingZeroBoolean};
inline constexpr StartupValuePolicy NumericBoolean{
    StartupValueKind::NumericBoolean};
inline constexpr StartupValuePolicy JitBoolean{StartupValueKind::JitBoolean};
inline constexpr StartupValuePolicy NonnegativeDecimalInteger{
    StartupValueKind::NonnegativeDecimalInteger};
inline constexpr StartupValuePolicy DecimalInteger{
    StartupValueKind::DecimalInteger};
inline constexpr StartupValuePolicy AutoBaseInteger{
    StartupValueKind::AutoBaseInteger};
inline constexpr StartupValuePolicy HexInteger{StartupValueKind::HexInteger};
inline constexpr StartupValuePolicy NonemptyHexInteger{
    StartupValueKind::NonemptyHexInteger};
inline constexpr StartupValuePolicy TraceLimit{StartupValueKind::TraceLimit};
inline constexpr StartupValuePolicy Custom{StartupValueKind::Custom};

constexpr StartupValuePolicy autoBounded(int minimum, int maximum) {
    return {StartupValueKind::AutoBaseBoundedInteger, minimum, maximum};
}

constexpr StartupValuePolicy decimalBounded(int minimum, int maximum) {
    return {StartupValueKind::DecimalBoundedInteger, minimum, maximum};
}
} // namespace startup_policy

constexpr bool startupValueIsBoolean(StartupValueKind kind) {
    return kind == StartupValueKind::EnabledBoolean ||
           kind == StartupValueKind::ExactZeroBoolean ||
           kind == StartupValueKind::LeadingZeroBoolean ||
           kind == StartupValueKind::NumericBoolean ||
           kind == StartupValueKind::JitBoolean;
}

constexpr bool startupValueIsInteger(StartupValueKind kind) {
    return kind == StartupValueKind::NonnegativeDecimalInteger ||
           kind == StartupValueKind::DecimalInteger ||
           kind == StartupValueKind::AutoBaseInteger ||
           kind == StartupValueKind::AutoBaseBoundedInteger ||
           kind == StartupValueKind::DecimalBoundedInteger;
}

} // namespace pom68k
