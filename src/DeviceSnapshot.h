// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Typed device snapshots for the debugger ──
// A device describes itself as named fields, read from its own members on
// the machine thread — never through a bus read, so a snapshot cannot
// clear a VIA flag, latch an SCC status or advance an IWM. Each device
// class lists the members its save-state visit() already names
// (`debugFields`); each memory map assembles its board's devices under a
// kind and a name (`debugDevices`). Dependency-free on purpose: device
// headers include this and nothing of the debugger.
//
//   kind   "VIA", "SCC", "Floppy", "SCSI", "ADB" or "Video" — the
//          categories the debugger promises for every board;
//   name   the part on this board ("VIA1", "SWIM2", "53C96 (bus 2)"…);
//   field  a member's name and value; small byte arrays as hex text,
//          larger containers as their size, nested objects omitted.
//
// Gate: tests/debug_inspection_test.cpp (every board).

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace pom68k::dev {

struct Field {
    std::string name;
    std::int64_t value = 0;
    std::uint8_t bits = 0;      // width of `value`; 0 = `text` only
    std::string text;
};

struct Snapshot {
    std::string kind;
    std::string name;
    std::vector<Field> fields;
};

namespace detail {
inline std::string hexBytes(const std::uint8_t* p, std::size_t n) {
    std::string s;
    char b[4];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, i ? " %02X" : "%02X", p[i]);
        s += b;
    }
    return s;
}

inline void add(std::vector<Field>& out, std::string_view name, const auto& v) {
    using T = std::remove_cvref_t<decltype(v)>;
    Field f;
    f.name = std::string(name);
    if (!f.name.empty() && f.name.back() == '_') f.name.pop_back();
    if constexpr (std::is_same_v<T, bool>) {
        f.value = v ? 1 : 0;
        f.bits = 1;
    } else if constexpr (std::is_enum_v<T>) {
        f.value = static_cast<std::int64_t>(v);
        f.bits = std::uint8_t(sizeof(T) * 8);
    } else if constexpr (std::is_integral_v<T>) {
        f.value = static_cast<std::int64_t>(v);
        f.bits = std::uint8_t(sizeof(T) * 8);
    } else if constexpr (std::is_array_v<T>) {
        constexpr std::size_t n = std::extent_v<T>;
        using E = std::remove_extent_t<T>;
        if constexpr (std::is_same_v<E, std::uint8_t>) {
            f.text = n <= 32 ? hexBytes(v, n) : std::to_string(n) + " octets";
        } else if constexpr (std::is_integral_v<E> && n <= 16) {
            for (std::size_t i = 0; i < n; ++i)
                f.text += (i ? " " : "") + std::to_string(static_cast<long long>(v[i]));
        } else {
            f.text = std::to_string(n) + " éléments";
        }
    } else if constexpr (requires { v.size(); v.data(); }) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(*v.data())>,
                                     std::uint8_t>)
            f.text = v.size() <= 32 ? hexBytes(v.data(), v.size())
                                    : std::to_string(v.size()) + " octets";
        else
            f.text = std::to_string(v.size()) + " éléments";
    } else if constexpr (requires { v.size(); }) {
        f.text = std::to_string(v.size()) + " éléments";
    } else {
        return;                 // a nested object: its own snapshot, if any
    }
    out.push_back(std::move(f));
}

// Pairs the stringized argument list ("a_, b_, c_") with the values.
inline std::string_view nextName(std::string_view& names) {
    while (!names.empty() && (names.front() == ' ' || names.front() == ','))
        names.remove_prefix(1);
    std::size_t end = names.find(',');
    std::string_view n = names.substr(0, end);
    names.remove_prefix(end == std::string_view::npos ? names.size() : end);
    while (!n.empty() && n.back() == ' ') n.remove_suffix(1);
    return n;
}
} // namespace detail

template <class... Ts>
void collect(std::vector<Field>& out, std::string_view names, const Ts&... values) {
    (detail::add(out, detail::nextName(names), values), ...);
}

// A board's device under its kind and name, from the device's own fields.
template <class Device>
void add(std::vector<Snapshot>& out, const char* kind, const char* name,
         const Device& d) {
    Snapshot s;
    s.kind = kind;
    s.name = name;
    d.debugFields(s.fields);
    out.push_back(std::move(s));
}

} // namespace pom68k::dev

// Inside a device class: POM_DEVICE_FIELDS(out, member_, other_, …).
#define POM_DEVICE_FIELDS(out, ...) \
    ::pom68k::dev::collect((out), #__VA_ARGS__, __VA_ARGS__)
