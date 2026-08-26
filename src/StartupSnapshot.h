// POM68K — immutable typed snapshot of legacy startup values.
// Raw names exist only while constructing the boundary object. Consumers can
// query values only through a StartupOption declared by StartupOptions.h.

#pragma once

#include "StartupValueDecoding.h"

#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pom68k {

class StartupSnapshot {
public:
    using Entry = std::pair<std::string, std::string>;

    StartupSnapshot() = default;
    StartupSnapshot(std::initializer_list<Entry> entries)
        : StartupSnapshot(std::vector<Entry>(entries)) {}

    explicit StartupSnapshot(std::vector<Entry> entries) {
        for (Entry& entry : entries) {
            if (!known(entry.first))
                throw std::invalid_argument(
                    "unknown startup option: " + entry.first);
            values_.insert_or_assign(
                std::move(entry.first), std::move(entry.second));
        }
    }

    std::size_t size() const noexcept { return values_.size(); }

    template <StartupOptionType Option>
    bool present(Option option) const {
        return values_.find(option.name) != values_.end();
    }

    template <startup_value::TextStartupOption Option>
    std::optional<std::string_view> text(Option option) const {
        return startup_value::text(option, lookup(option.name));
    }

    template <startup_value::HexStartupOption Option>
    std::optional<std::string_view> encodedText(Option option) const {
        return lookup(option.name);
    }

    template <BooleanStartupOption Option>
    bool boolean(Option option, bool fallback = false) const {
        return startup_value::boolean(option, lookup(option.name), fallback);
    }

    template <IntegerStartupOption Option>
    std::optional<int> integer(Option option) const {
        return startup_value::integer(option, lookup(option.name));
    }

    template <startup_value::HexStartupOption Option>
    std::optional<std::uint32_t> hexadecimal(Option option) const {
        return startup_value::hexadecimal(option, lookup(option.name));
    }

    template <StartupOptionType Option>
    requires (Option::value.kind == StartupValueKind::TraceLimit)
    long traceLimit(Option option, long defaultWhenPresent) const {
        return startup_value::traceLimit(
            option, lookup(option.name), defaultWhenPresent);
    }

private:
    static bool known(std::string_view name) {
        for (const StartupOptionSpec option : startup_option::kAll)
            if (option.name == name) return true;
        return false;
    }

    std::optional<std::string_view> lookup(std::string_view name) const {
        const auto found = values_.find(name);
        return found == values_.end()
            ? std::nullopt
            : std::optional<std::string_view>(found->second);
    }

    std::map<std::string, std::string, std::less<>> values_;
};

} // namespace pom68k
