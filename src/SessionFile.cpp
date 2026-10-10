// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Session file schema, parser and writer (SessionFile.h). One table names
// every key, its target input and its value rule; the parser, the overlay
// and the argument list all read it, so they cannot drift apart.

#include "SessionFile.h"

#include "MachineCatalog.h"
#include "MachineFactory.h"
#include "RuntimeConfig.h"
#include "RuntimeConfigParsers.h"
#include "StartupOptions.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>

namespace pom68k::app {
namespace {

namespace fs = std::filesystem;

enum class Target : std::uint8_t {
    Profile,  // --machine-profile=<slug>
    Rom,      // the positional ROM
    Media,    // the positional media, in order
    Argument, // <spelling><value>
    Startup,  // the startup option named <spelling>
};

enum class Rule : std::uint8_t {
    Text,
    Path,          // resolved; need not exist (created at run time)
    InputPath,     // resolved; must exist
    Boolean,       // 0 | 1
    InverseBoolean,// 0 | 1, the option says the opposite (fpu → NOFPU)
    Choice,        // one of `choices`, '|'-separated
    Integer,       // decimal
    DaynaPort,     // 0 (none) or 2-6
    Firmware,      // <target>:<lle|hle>:<path?> — path resolved, must exist
    Serial,        // pty | tcp:<port>
};

struct KeySpec {
    std::string_view key;
    Target target;
    Rule rule;
    std::string_view spelling = {};
    bool repeatable = false;
    std::string_view choices = {};
};

namespace option = startup_option;

constexpr KeySpec kKeys[] = {
    {"profile", Target::Profile, Rule::Text},
    {"rom", Target::Rom, Rule::InputPath},
    {"media", Target::Media, Rule::InputPath, {}, true},
    {"engine", Target::Startup, Rule::Choice, option::CpuEngine.name, false,
     "interp|jit"},
    {"backend", Target::Startup, Rule::Choice, option::JitBackend.name, false,
     "auto|threaded|x64|a64"},
    {"fpu", Target::Startup, Rule::InverseBoolean, option::NoFpu.name},
    {"firmware", Target::Argument, Rule::Firmware, kFirmwareOverrideOption,
     true},
    {"daynaport", Target::Argument, Rule::DaynaPort, kDaynaPortOption},
    {"appletalk", Target::Startup, Rule::Boolean, option::AppleTalk.name},
    {"localtalk-udp", Target::Startup, Rule::Boolean, option::LtoUdp.name},
    {"atalk-share", Target::Argument, Rule::Path, "--atalk-share="},
    {"atalk-server", Target::Argument, Rule::Text, "--atalk-server="},
    {"atalk-volume", Target::Argument, Rule::Text, "--atalk-volume="},
    {"atalk-printer", Target::Argument, Rule::Text, "--atalk-printer="},
    {"atalk-spool", Target::Argument, Rule::Path, "--atalk-spool="},
    {"atalk-queue", Target::Argument, Rule::Text, "--atalk-queue="},
    {"atalk-print-options", Target::Argument, Rule::Text,
     "--atalk-print-options="},
    {"atalk-gateway", Target::Argument, Rule::Text, "--atalk-gateway="},
    {"atalk-dns", Target::Argument, Rule::Text, "--atalk-dns="},
    {"atalk-ethertalk", Target::Argument, Rule::Boolean,
     "--atalk-ethertalk="},
    {"serial-printer", Target::Startup, Rule::Serial,
     option::SerialPrinter.name},
    {"serial-modem", Target::Startup, Rule::Serial, option::SerialModem.name},
    {"floppy", Target::Startup, Rule::InputPath, option::Floppy.name},
    {"floppy-read-only", Target::Startup, Rule::Boolean,
     option::FloppyReadOnly.name},
    {"ide", Target::Startup, Rule::InputPath, option::IdeDisk.name},
    {"monitor", Target::Startup, Rule::Integer, option::Monitor.name},
    {"kiosk", Target::Startup, Rule::Boolean, option::Kiosk.name},
    {"crt", Target::Startup, Rule::Choice, option::CrtPreset.name, false,
     "off|light|arcade|phosphor"},
    {"turbo", Target::Startup, Rule::Boolean, option::Turbo.name},
    {"audio", Target::Startup, Rule::Boolean, option::Audio.name},
    {"drive-sounds", Target::Startup, Rule::Boolean,
     option::DriveSounds.name},
};

const KeySpec* keySpec(std::string_view key) {
    for (const KeySpec& spec : kKeys)
        if (spec.key == key) return &spec;
    return nullptr;
}

// The startup options whose mere presence turns them on: `0` must remove
// them rather than set them.
bool presenceOption(std::string_view name) {
    for (const StartupOptionSpec spec : option::kAll)
        if (spec.name == name)
            return spec.value.kind == StartupValueKind::Presence;
    return false;
}

std::string_view trim(std::string_view text) {
    const auto blank = [](char c) {
        return c == ' ' || c == '\t' || c == '\r';
    };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return text;
}

bool choiceAllowed(std::string_view choices, std::string_view value) {
    while (!choices.empty()) {
        const std::size_t bar = choices.find('|');
        if (choices.substr(0, bar) == value) return true;
        if (bar == std::string_view::npos) break;
        choices.remove_prefix(bar + 1);
    }
    return false;
}

std::optional<int> decimal(std::string_view text) {
    int value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

// `"…"` with \" and \\ escapes, or the trimmed text as is.
std::optional<std::string> unquote(std::string_view text) {
    if (text.empty() || text.front() != '"') return std::string(text);
    if (text.size() < 2 || text.back() != '"') return std::nullopt;
    std::string value;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        char c = text[i];
        if (c == '\\') {
            if (i + 2 >= text.size()) return std::nullopt;
            c = text[++i];
            if (c != '\\' && c != '"') return std::nullopt;
        } else if (c == '"') {
            return std::nullopt;
        }
        value.push_back(c);
    }
    return value;
}

std::string quoteIfNeeded(const std::string& value) {
    const bool outerBlank = !value.empty() &&
        (trim(value).size() != value.size());
    if (!outerBlank && (value.empty() || value.front() != '"')) return value;
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') quoted.push_back('\\');
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

fs::path resolve(const fs::path& base, std::string_view value) {
    const fs::path path = pathFromUtf8(value);
    return (path.is_absolute() ? path : base / path).lexically_normal();
}

} // namespace

fs::path pathFromUtf8(std::string_view text) {
    return fs::path(std::u8string(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8FromPath(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),
                       text.size());
}

bool isSessionArgument(std::string_view argument) {
    return sessionArgumentPath(argument).has_value();
}

std::optional<std::string> sessionArgumentPath(std::string_view argument) {
    if (argument.starts_with(kSessionOption))
        return std::string(argument.substr(kSessionOption.size()));
    if (!argument.starts_with("-") && argument.size() > kSessionExtension.size() &&
        argument.ends_with(kSessionExtension))
        return std::string(argument);
    return std::nullopt;
}

std::vector<std::string> SessionFile::arguments() const {
    std::vector<std::string> arguments;
    for (const SessionEntry& entry : entries) {
        const KeySpec* spec = keySpec(entry.key);
        if (spec->target == Target::Profile)
            arguments.push_back(std::string(kMachineProfileOption) + entry.value);
        else if (spec->target == Target::Argument)
            arguments.push_back(std::string(spec->spelling) + entry.value);
    }
    return arguments;
}

StartupSnapshot SessionFile::overlay(const StartupSnapshot& environment) const {
    std::vector<StartupSnapshot::Entry> set;
    std::vector<std::string> erase;
    for (const SessionEntry& entry : entries) {
        const KeySpec* spec = keySpec(entry.key);
        if (spec->target != Target::Startup) continue;
        std::string value = entry.value;
        if (spec->rule == Rule::InverseBoolean)
            value = value == "1" ? "0" : "1";
        const bool off = (spec->rule == Rule::Boolean ||
                          spec->rule == Rule::InverseBoolean) && value == "0";
        if (off && presenceOption(spec->spelling)) {
            erase.emplace_back(spec->spelling);
            std::erase_if(set, [&](const StartupSnapshot::Entry& item) {
                return item.first == spec->spelling;
            });
        } else {
            set.emplace_back(std::string(spec->spelling), std::move(value));
        }
    }
    return environment.overlaid(std::move(set), erase);
}

SessionParse parseSession(std::string_view text, const fs::path& source,
                          const SessionFileExists& exists) {
    SessionParse result;
    result.source = source;
    SessionFile session;
    session.source = source;
    const fs::path base = source.parent_path();
    auto fail = [&](int line, std::string message) {
        result.errors.push_back({line, std::move(message)});
    };

    bool header = false;
    int lineNumber = 0;
    std::vector<std::string_view> seen;
    while (!text.empty()) {
        ++lineNumber;
        const std::size_t newline = text.find('\n');
        const std::string_view line = trim(text.substr(0, newline));
        text.remove_prefix(newline == std::string_view::npos
                               ? text.size() : newline + 1);
        if (line.empty() || line.front() == '#') continue;
        if (!header) {
            header = true;
            if (!line.starts_with(kSessionHeader)) {
                fail(lineNumber, "not a POM68K session: the first line must "
                                 "be `pom68k-session 1`");
                break;
            }
            const auto version =
                decimal(trim(line.substr(kSessionHeader.size())));
            if (version != kSessionFormatVersion) {
                fail(lineNumber, "unsupported session format version `" +
                    std::string(trim(line.substr(kSessionHeader.size()))) +
                    "` (this build reads version " +
                    std::to_string(kSessionFormatVersion) + ")");
                break;
            }
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            fail(lineNumber, "expected `key = value`");
            continue;
        }
        const std::string_view key = trim(line.substr(0, equals));
        const auto unquoted = unquote(trim(line.substr(equals + 1)));
        const KeySpec* spec = keySpec(key);
        if (!spec) {
            fail(lineNumber, "unknown key `" + std::string(key) + "`");
            continue;
        }
        if (!unquoted) {
            fail(lineNumber, "malformed quoted value for `" +
                             std::string(key) + "`");
            continue;
        }
        if (!spec->repeatable &&
            std::find(seen.begin(), seen.end(), spec->key) != seen.end()) {
            fail(lineNumber, "`" + std::string(key) + "` given twice");
            continue;
        }
        seen.push_back(spec->key);

        std::string value = *unquoted;
        const std::string what = "`" + std::string(key) + "`: ";
        switch (spec->rule) {
        case Rule::Text:
            break;
        case Rule::Path:
        case Rule::InputPath: {
            if (value.empty()) {
                fail(lineNumber, what + "empty path");
                continue;
            }
            const fs::path path = resolve(base, value);
            if (spec->rule == Rule::InputPath && !exists(path)) {
                fail(lineNumber, what + "missing file " + utf8FromPath(path));
                continue;
            }
            value = utf8FromPath(path);
            break;
        }
        case Rule::Boolean:
        case Rule::InverseBoolean:
            if (value != "0" && value != "1") {
                fail(lineNumber, what + "expected 0 or 1, got `" + value + "`");
                continue;
            }
            break;
        case Rule::Choice:
            if (!choiceAllowed(spec->choices, value)) {
                fail(lineNumber, what + "expected one of " +
                    std::string(spec->choices) + ", got `" + value + "`");
                continue;
            }
            break;
        case Rule::Integer:
            if (!decimal(value)) {
                fail(lineNumber, what + "expected a decimal integer, got `" +
                                 value + "`");
                continue;
            }
            break;
        case Rule::DaynaPort: {
            const auto id = decimal(value);
            if (!id || (*id != 0 && (*id < 2 || *id > 6))) {
                fail(lineNumber, what + "expected 0 (no card) or a SCSI ID "
                                 "2-6, got `" + value + "`");
                continue;
            }
            break;
        }
        case Rule::Firmware: {
            const std::size_t target = value.find(':');
            const std::size_t mode = target == std::string::npos
                ? std::string::npos : value.find(':', target + 1);
            const std::string_view head = std::string_view(value).substr(
                0, mode == std::string::npos ? value.size() : mode);
            const bool known = mode != std::string::npos &&
                choiceAllowed("adb|egret|cuda|toby", head.substr(0, target)) &&
                choiceAllowed("lle|hle", head.substr(target + 1));
            if (!known) {
                fail(lineNumber, what + "expected <adb|egret|cuda|toby>:"
                                 "<lle|hle>:<path>, got `" + value + "`");
                continue;
            }
            const std::string_view path =
                std::string_view(value).substr(mode + 1);
            if (!path.empty()) {
                const fs::path resolved = resolve(base, path);
                if (!exists(resolved)) {
                    fail(lineNumber, what + "missing file " +
                                     utf8FromPath(resolved));
                    continue;
                }
                value = std::string(head) + ':' + utf8FromPath(resolved);
            }
            break;
        }
        case Rule::Serial: {
            const auto kind = detail::parseSerialPort(value).kind;
            if (kind != SerialTransportKind::Pty && kind != SerialTransportKind::Tcp &&
                kind != SerialTransportKind::Terminal) {
                fail(lineNumber, what + "expected pty, tcp:<port> or terminal, got `" +
                                 value + "`");
                continue;
            }
            break;
        }
        }

        if (spec->target == Target::Profile) {
            if (!machineProfile(std::string_view(value))) {
                fail(lineNumber, what + "unknown machine profile `" + value +
                                 "`");
                continue;
            }
            session.profile = value;
        } else if (spec->target == Target::Rom) {
            session.rom = value;
        } else if (spec->target == Target::Media) {
            session.media.push_back(value);
        }
        session.entries.push_back({std::string(spec->key), std::move(value)});
    }
    if (!header)
        fail(0, "empty session: the first line must be `pom68k-session 1`");
    if (result.errors.empty()) result.session = std::move(session);
    return result;
}

SessionParse loadSession(const fs::path& path) {
    std::error_code error;
    const fs::path source = fs::absolute(path, error).lexically_normal();
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        SessionParse result;
        result.source = source;
        result.errors.push_back({0, "cannot read the session file"});
        return result;
    }
    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    SessionParse result = parseSession(text, source, [](const fs::path& file) {
        std::error_code ignored;
        return fs::is_regular_file(file, ignored);
    });
    if (result.session && result.session->profile && !result.session->rom) {
        const MachineProfile* profile =
            machineProfile(std::string_view(*result.session->profile));
        std::string rom = MachineFactory::findPath(profile->romPath);
        if (rom.empty() && profile->romCrc32)
            rom = MachineFactory::findRomBySignature(profile->romCrc32);
        if (rom.empty()) {
            result.errors.push_back({0, "profile `" + *result.session->profile +
                "`: no `rom` line and its ROM (" + profile->romPath +
                ") was not found"});
            result.session.reset();
        } else {
            result.session->rom =
                utf8FromPath(fs::absolute(rom, error).lexically_normal());
        }
    }
    return result;
}

SessionCapture SessionCapture::from(const RuntimeConfig& config) {
    SessionCapture capture;
    capture.profile = config.sessionProfile();
    if (const auto rom = config.romPath()) capture.rom = std::string(*rom);
    capture.media = config.mediaArguments();
    const jit::ResolvedConfig& jit = config.jit().resolved;
    if (jit.engineExplicit) capture.engine = jit.engine;
    if (jit.backend != "auto") capture.backend = jit.backend;
    capture.fpu = config.cpu().fpu;
    const CoreFirmwareConfig& firmware = config.core().firmware;
    capture.firmware = {
        {FirmwareTarget::Adb, firmware.adbLle, firmware.adbPath},
        {FirmwareTarget::Egret, firmware.egretLle, firmware.egretPath},
        {FirmwareTarget::Cuda, firmware.cudaLle, firmware.cudaPath},
        {FirmwareTarget::TobyDecl, firmware.tobyDeclLle, firmware.tobyDeclPath},
    };
    capture.daynaPortId = config.core().bus.daynaPortId;
    capture.appleTalk = config.network().appleTalk;
    capture.ltoUdp = config.network().ltoUdp;
    capture.network = config.network();
    capture.serialPrinter = config.devices().serialPrinter;
    capture.serialModem = config.devices().serialModem;
    capture.floppy = config.devices().startupFloppy;
    capture.floppyReadOnly = !config.devices().floppyWriteBack;
    capture.ide = config.core().storage.ideDisk;
    capture.monitorWidth = config.devices().monitorWidth;
    capture.kiosk = config.devices().kiosk;
    capture.crtPreset = config.devices().crtPreset;
    capture.turbo = config.devices().turbo;
    capture.audio = config.devices().audio;
    capture.driveSounds = config.devices().driveSounds;
    return capture;
}

std::vector<SessionEntry> SessionCapture::entries() const {
    std::vector<SessionEntry> entries;
    auto add = [&](std::string_view key, std::string value) {
        entries.push_back({std::string(key), std::move(value)});
    };
    auto absolute = [](const std::string& value) {
        std::error_code error;
        return utf8FromPath(
            fs::absolute(pathFromUtf8(value), error).lexically_normal());
    };
    auto flag = [](bool on) { return std::string(on ? "1" : "0"); };
    if (profile)
        if (const MachineProfile* named = machineProfile(*profile))
            add("profile", named->slug);
    if (rom && !rom->empty()) add("rom", absolute(*rom));
    for (const std::string& medium : media) add("media", absolute(medium));
    if (engine)
        add("engine", *engine == jit::EngineKind::Jit ? "jit" : "interp");
    if (backend) add("backend", *backend);
    add("fpu", flag(fpu));
    for (const FirmwareOverride& policy : firmware) {
        FirmwareOverride written = policy;
        if (written.path && !written.path->empty())
            written.path = absolute(*written.path);
        add("firmware", firmwareOverrideArgument(written).substr(
                            kFirmwareOverrideOption.size()));
    }
    add("daynaport", std::to_string(daynaPortId.value_or(0)));
    add("appletalk", flag(appleTalk));
    add("localtalk-udp", flag(ltoUdp));
    // The same `--atalk-<key>=` list a relaunch writes, one key per line.
    for (const std::string& argument : atalkArguments({}, network)) {
        const std::string_view rest =
            std::string_view(argument).substr(kAtalkOptionPrefix.size());
        const std::size_t equals = rest.find('=');
        const std::string key = "atalk-" + std::string(rest.substr(0, equals));
        std::string value(rest.substr(equals + 1));
        const KeySpec* spec = keySpec(key);
        if (!spec) continue;
        if (spec->rule == Rule::Path) {
            if (value.empty()) continue; // unset, not the current directory
            value = absolute(value);
        }
        add(key, std::move(value));
    }
    const auto serial = [&](std::string_view key, const SerialPortConfig& port) {
        if (port.kind == SerialTransportKind::Pty ||
            port.kind == SerialTransportKind::Tcp ||
            port.kind == SerialTransportKind::Terminal)
            add(key, port.requested);
    };
    serial("serial-printer", serialPrinter);
    serial("serial-modem", serialModem);
    if (floppy && !floppy->empty()) add("floppy", absolute(*floppy));
    add("floppy-read-only", flag(floppyReadOnly));
    if (ide && !ide->empty()) add("ide", absolute(*ide));
    if (monitorWidth) add("monitor", std::to_string(*monitorWidth));
    add("kiosk", flag(kiosk));
    if (!crtPreset.empty()) add("crt", crtPreset);
    add("turbo", flag(turbo));
    add("audio", flag(audio));
    add("drive-sounds", flag(driveSounds));
    return entries;
}

std::string serializeSession(const std::vector<SessionEntry>& entries,
                             const fs::path& destination) {
    const fs::path base = destination.parent_path().lexically_normal();
    auto portable = [&](const std::string& value) {
        const fs::path path = pathFromUtf8(value).lexically_normal();
        if (!path.is_absolute() || base.empty()) return value;
        const fs::path relative = path.lexically_relative(base);
        if (relative.empty() || *relative.begin() == "..") return value;
        const std::u8string generic = relative.generic_u8string();
        return std::string(reinterpret_cast<const char*>(generic.data()),
                           generic.size());
    };
    std::ostringstream text;
    text << "# POM68K session — see src/SessionFile.h for the keys\n"
         << kSessionHeader << ' ' << kSessionFormatVersion << '\n';
    for (const SessionEntry& entry : entries) {
        const KeySpec* spec = keySpec(entry.key);
        std::string value = entry.value;
        if (spec && (spec->rule == Rule::Path || spec->rule == Rule::InputPath)) {
            value = portable(value);
        } else if (spec && spec->rule == Rule::Firmware) {
            const std::size_t mode = value.find(':', value.find(':') + 1);
            if (mode != std::string::npos && mode + 1 < value.size())
                value = value.substr(0, mode + 1) +
                        portable(value.substr(mode + 1));
        }
        text << entry.key << " = " << quoteIfNeeded(value) << '\n';
    }
    return text.str();
}

std::string writeSessionFile(const fs::path& destination,
                             const std::vector<SessionEntry>& entries) {
    std::error_code error;
    const fs::path target = fs::absolute(destination, error).lexically_normal();
    if (target.extension() != pathFromUtf8(kSessionExtension))
        return "a session file name ends in " + std::string(kSessionExtension);
    fs::create_directories(target.parent_path(), error);
    fs::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << serializeSession(entries, target);
        if (!output.flush())
            return "cannot write " + utf8FromPath(temporary);
    }
    fs::rename(temporary, target, error);
    if (error) {
        fs::remove(temporary, error);
        return "cannot replace " + utf8FromPath(target);
    }
    const SessionParse reloaded = loadSession(target);
    return reloaded.session ? std::string() : describeSessionErrors(reloaded);
}

std::vector<fs::path> listSessions(const fs::path& directory) {
    std::vector<fs::path> sessions;
    std::error_code error;
    for (auto it = fs::directory_iterator(directory, error);
         !error && it != fs::directory_iterator(); it.increment(error)) {
        std::error_code ignored;
        if (it->is_regular_file(ignored) &&
            it->path().extension() == pathFromUtf8(kSessionExtension))
            sessions.push_back(it->path());
    }
    std::sort(sessions.begin(), sessions.end());
    return sessions;
}

SessionParse loadSessionArgument(int argc, char* const argv[]) {
    SessionParse result;
    for (int i = 1; i < argc; ++i) {
        const auto path = sessionArgumentPath(argv[i] ? argv[i] : "");
        if (!path) continue;
        if (!result.source.empty()) {
            result.session.reset();
            result.errors.push_back({0, "one session per process; also given: " + *path});
            return result;
        }
        result = loadSession(pathFromUtf8(*path));
        if (!result.session) return result;
    }
    return result;
}

std::string describeSessionErrors(const SessionParse& parse) {
    std::string text;
    for (const SessionDiagnostic& error : parse.errors) {
        text += utf8FromPath(parse.source);
        if (error.line > 0) text += ':' + std::to_string(error.line);
        text += ": " + error.message + '\n';
    }
    return text;
}

} // namespace pom68k::app
