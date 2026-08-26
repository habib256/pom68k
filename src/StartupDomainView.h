// POM68K — typed domain views over an injected startup-value snapshot.

#pragma once

#include "StartupSnapshot.h"

namespace pom68k::app {

namespace detail {

template <StartupDomain Domain>
class StartupDomainView {
public:
    explicit StartupDomainView(const StartupSnapshot& values)
        : values_(values) {}

    template <class Option> requires StartupOptionFor<Option, Domain>
    bool present(Option option) const { return values_.present(option); }

    template <class Option>
    requires StartupOptionFor<Option, Domain> &&
             startup_value::HexStartupOption<Option>
    std::optional<std::string> encodedText(Option option) const {
        const auto value = values_.encodedText(option);
        return value ? std::optional<std::string>(std::string(*value))
                     : std::nullopt;
    }

    template <class Option>
    requires StartupOptionFor<Option, Domain> &&
             startup_value::TextStartupOption<Option>
    std::optional<std::string> text(Option option) const {
        const auto value = values_.text(option);
        return value ? std::optional<std::string>(std::string(*value))
                     : std::nullopt;
    }

    template <class Option>
    requires StartupOptionFor<Option, Domain> && BooleanStartupOption<Option>
    bool boolean(Option option, bool fallback = false) const {
        return values_.boolean(option, fallback);
    }

    template <class Option>
    requires StartupOptionFor<Option, Domain> && IntegerStartupOption<Option>
    std::optional<int> integer(Option option) const {
        return values_.integer(option);
    }

    template <class Option>
    requires StartupOptionFor<Option, Domain> &&
             startup_value::HexStartupOption<Option>
    std::optional<std::uint32_t> hexadecimal(Option option) const {
        return values_.hexadecimal(option);
    }

    template <class Option>
    requires StartupOptionFor<Option, Domain> &&
             (Option::value.kind == StartupValueKind::TraceLimit)
    long traceLimit(Option option, long fallback) const {
        return values_.traceLimit(option, fallback);
    }

private:
    const StartupSnapshot& values_;
};

using ProductStartupView = StartupDomainView<StartupDomain::Product>;
using CoreStartupView = StartupDomainView<StartupDomain::Core>;
using MachineStartupView = StartupDomainView<StartupDomain::Machine>;

} // namespace detail
} // namespace pom68k::app
