// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Host text typed into the guest as key presses. Two halves, both pure:
//
//   planTyping   UTF-8 text → the physical key transitions that produce it
//                under an EXPLICIT guest layout (US or Apple French AZERTY;
//                the guest's KCHR is not readable from low memory, see
//                GuestKeyboard.h), plus every character that has no key —
//                reported, never guessed.
//   TextTyper    schedules those transitions in MACHINE time: each one is
//                due a fixed delay after the previous one was emitted, and
//                at most one is emitted per call, so a press and its
//                release never share a guest instant and nothing depends on
//                the host's frame rate. MachineHost polls it before every
//                quantum and journals each emitted transition as an
//                ordinary `key` event, so a recorded paste replays as typing.
//
// Return, Tab and printable ASCII are typed on both layouts; on AZERTY the
// unshifted accented keys too (é è ç à ù §). Dead keys and Option
// combinations are not offered. Gate: clipboard_typing_test.

#pragma once

#include "GuestKeyboard.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pom68k {

enum class GuestLayout : std::uint8_t { Us = 0, FrenchAzerty = 1 };

inline constexpr std::size_t kMaxTypedCharacters = 16384;

struct KeyTransition {
    std::uint8_t code = 0;     // physical key (Macintosh virtual key code)
    bool down = false;
    std::uint32_t delayUs = 0; // after the previous transition was emitted
    bool endsCharacter = false;
};

struct TypingPlan {
    std::vector<KeyTransition> steps;
    std::size_t characters = 0;          // typeable characters planned
    std::size_t skipped = 0;             // characters with no key
    std::vector<std::string> unrepresentable; // distinct, UTF-8, in order met
    bool truncated = false;              // longer than kMaxTypedCharacters
};

struct TypingTiming {
    std::uint32_t shiftLeadUs = 30000; // Shift down → key, key up → Shift up
    std::uint32_t holdUs = 50000;      // key down → key up
    std::uint32_t gapUs = 40000;       // one character → the next
};

namespace typing_detail {

// One code point from UTF-8; malformed bytes come back as U+FFFD, one each.
inline char32_t nextCodePoint(std::string_view text, std::size_t& i) {
    const auto byte = [&](std::size_t k) { return std::uint8_t(text[k]); };
    const std::uint8_t lead = byte(i++);
    if (lead < 0x80) return lead;
    const int extra = lead >= 0xF0 && lead < 0xF8 ? 3 : lead >= 0xE0 ? 2
                    : lead >= 0xC2 && lead < 0xE0 ? 1 : -1;
    if (extra < 0 || i + std::size_t(extra) > text.size()) return 0xFFFD;
    char32_t cp = lead & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        if ((byte(i) & 0xC0) != 0x80) return 0xFFFD;
        cp = cp << 6 | (byte(i++) & 0x3F);
    }
    return cp;
}

inline std::string utf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | cp >> 6); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += char(0xE0 | cp >> 12); out += char(0x80 | (cp >> 6 & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | cp >> 18); out += char(0x80 | (cp >> 12 & 0x3F));
        out += char(0x80 | (cp >> 6 & 0x3F)); out += char(0x80 | (cp & 0x3F));
    }
    return out;
}

} // namespace typing_detail

constexpr std::uint8_t kKeyReturn = 0x24, kKeyTab = 0x30;

inline guestkbd::Keystroke keystrokeFor(char32_t cp, GuestLayout layout) {
    if (cp == '\n') return {kKeyReturn};
    if (cp == '\t') return {kKeyTab};
    if (cp >= 0x20 && cp < 0x7F)
        return guestkbd::keystrokeFor(char(cp), layout == GuestLayout::FrenchAzerty);
    if (layout == GuestLayout::FrenchAzerty) {
        // The unshifted characters of the number row and the US ' key:
        // é è ç à ù §, by code point — MSVC reads a source without /utf-8
        // as its ANSI code page, where U'é' is a multi-character constant.
        switch (cp) {
            case U'\u00E9': return {0x13}; case U'\u00E8': return {0x1A};
            case U'\u00E7': return {0x19}; case U'\u00E0': return {0x1D};
            case U'\u00F9': return {0x27}; case U'\u00A7': return {0x16};
            default: break;
        }
    }
    return {};
}

inline TypingPlan planTyping(std::string_view text, GuestLayout layout,
                             const TypingTiming& timing = {}) {
    TypingPlan plan;
    std::size_t i = 0;
    while (i < text.size()) {
        char32_t cp = typing_detail::nextCodePoint(text, i);
        if (cp == '\r') {                      // CR LF and a lone CR: one Return
            if (i < text.size() && text[i] == '\n') ++i;
            cp = '\n';
        }
        if (plan.characters + plan.skipped >= kMaxTypedCharacters) {
            plan.truncated = true;
            break;
        }
        const guestkbd::Keystroke key = keystrokeFor(cp, layout);
        if (key.code == 0xFF) {
            ++plan.skipped;
            const std::string shown = typing_detail::utf8(cp);
            bool seen = false;
            for (const std::string& s : plan.unrepresentable) seen |= s == shown;
            if (!seen) plan.unrepresentable.push_back(shown);
            continue;
        }
        const std::uint32_t first = plan.steps.empty() ? 0 : timing.gapUs;
        if (key.shift) {
            plan.steps.push_back({guestkbd::kShift, true, first});
            plan.steps.push_back({key.code, true, timing.shiftLeadUs});
            plan.steps.push_back({key.code, false, timing.holdUs});
            plan.steps.push_back({guestkbd::kShift, false, timing.shiftLeadUs, true});
        } else {
            plan.steps.push_back({key.code, true, first});
            plan.steps.push_back({key.code, false, timing.holdUs, true});
        }
        ++plan.characters;
    }
    return plan;
}

class TextTyper {
public:
    // Appends `plan` behind whatever is still being typed.
    void start(const TypingPlan& plan) {
        for (const KeyTransition& step : plan.steps) queue_.push_back(step);
        charactersLeft_ += plan.characters;
    }

    // Drops what is not typed yet and releases every key it holds down,
    // one transition per poll like the rest.
    void cancel() {
        queue_.clear();
        charactersLeft_ = 0;
        for (int code = 0; code < 128; ++code)
            if (down_[code]) queue_.push_back({std::uint8_t(code), false, 0});
    }

    // A restored state owns its own keyboard: forget everything, release
    // nothing.
    void clear() {
        queue_.clear();
        charactersLeft_ = 0;
        for (bool& d : down_) d = false;
        lastClock_.reset();
    }

    // The transition due at machine clock `now` (cycles at `hz`), if any.
    std::optional<KeyTransition> poll(long long now, long long hz) {
        if (queue_.empty()) return std::nullopt;
        const KeyTransition& next = queue_.front();
        if (lastClock_) {
            const long long due = *lastClock_ + (long long)(
                (unsigned long long)next.delayUs * (unsigned long long)hz / 1000000ull);
            if (now < due) return std::nullopt;
        }
        KeyTransition step = next;
        queue_.pop_front();
        lastClock_ = now;
        down_[step.code & 0x7F] = step.down;
        if (step.endsCharacter && charactersLeft_) --charactersLeft_;
        if (queue_.empty()) lastClock_.reset();
        return step;
    }

    bool active() const { return !queue_.empty(); }
    std::size_t charactersLeft() const { return charactersLeft_; }

private:
    std::deque<KeyTransition> queue_;
    std::size_t charactersLeft_ = 0;
    bool down_[128] = {};
    std::optional<long long> lastClock_;
};

} // namespace pom68k
