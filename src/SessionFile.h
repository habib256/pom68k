// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Session files (`.pomsession`): one configured machine on disk — product
// profile, ROM and media, engine preference, network, serial and display
// choices. Guest execution state stays in `.pomss`.
//
// A session is not a second configuration path. Each key maps onto an
// input RuntimeConfig::parse already reads: a relaunch argument
// (`--machine-profile=`, `--daynaport=`, `--firmware-override=`,
// `--atalk-<key>=`), a startup option of StartupOptions.h, or the
// positional ROM/media. Precedence, decided once here:
//
//   command line  >  session  >  process environment  >  default
//
// — session arguments are consumed BEFORE the command line's, so a later
// command-line value wins; a session startup value replaces the
// environment's; and positional ROM/media given on the command line
// replace the session's whole set (ROM and media together). Relative paths
// resolve against the session file's directory, never the shell's current
// directory; the writer stores a path below that directory relatively, so
// a directory moved whole still opens. Gate: session_config_test.
//
// Format (UTF-8 text, version 1):
//
//   # comment
//   pom68k-session 1
//   profile = lcii
//   rom = roms/maclcii.rom
//   media = hdv/work/System 7.5.vhd
//   engine = jit
//
// Blank lines and lines whose first non-blank character is `#` are ignored.
// A value is the rest of the line, trimmed; a value with outer blanks or a
// leading quote is written as "…" with \" and \\ escapes. Unknown keys,
// duplicate single keys, invalid values, a missing or unsupported version
// line and missing input files are refused, all reported, before any
// machine is built.

#pragma once

#include "RuntimeConfig.h"
#include "StartupSnapshot.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pom68k::app {

inline constexpr std::string_view kSessionOption = "--session=";
inline constexpr std::string_view kSessionExtension = ".pomsession";
inline constexpr std::string_view kSessionHeader = "pom68k-session";
inline constexpr int kSessionFormatVersion = 1;

// A session argument on a command line: `--session=<path>`, or a
// positional `<path>.pomsession` (what a file association hands over).
bool isSessionArgument(std::string_view argument);
std::optional<std::string> sessionArgumentPath(std::string_view argument);

struct SessionEntry {
    std::string key;
    // Validated; a path value is absolute (resolved against the file).
    std::string value;
};

struct SessionFile {
    std::filesystem::path source;      // absolute; empty for a draft
    std::vector<SessionEntry> entries; // file order
    std::optional<std::string> profile;
    std::optional<std::string> rom;
    std::vector<std::string> media;

    // Arguments to consume ahead of the command line (relaunch options).
    std::vector<std::string> arguments() const;
    // The environment with this session's startup values in force.
    StartupSnapshot overlay(const StartupSnapshot& environment) const;
};

struct SessionDiagnostic {
    int line = 0; // 0: the file as a whole
    std::string message;
};

struct SessionParse {
    std::filesystem::path source;
    std::optional<SessionFile> session; // set only when there is no error
    std::vector<SessionDiagnostic> errors;
};

// Filesystem query the loader validates input files with; injectable so the
// parser itself stays pure.
using SessionFileExists = std::function<bool(const std::filesystem::path&)>;

SessionParse parseSession(std::string_view text,
                          const std::filesystem::path& source,
                          const SessionFileExists& exists);

// Reads `path` and parses it with the real filesystem. A profile without a
// `rom` line takes the catalogue's ROM for that profile, so a hand-written
// session may name the machine only.
SessionParse loadSession(const std::filesystem::path& path);

// What a running session saves: the typed configuration it started from,
// with the GUI's live choices written over it (profile, ROM actually
// loaded, engine switch, staged DaynaPort card, the hub's edited names,
// display and drive-sound toggles). Every key is written explicitly, so
// the file reopens the same machine whatever the environment then holds.
// Relative paths are made absolute against the current directory, which
// is the one the command line was given in.
struct SessionCapture {
    std::optional<SnapMachine> profile;
    std::optional<std::string> rom;
    std::vector<std::string> media;
    std::optional<jit::EngineKind> engine; // only when chosen or switchable
    std::optional<std::string> backend;    // only when not `auto`
    bool fpu = true;
    std::vector<FirmwareOverride> firmware;
    std::optional<int> daynaPortId;
    bool appleTalk = true;
    bool ltoUdp = false;
    NetworkConfig network;
    SerialPortConfig serialPrinter, serialModem;
    std::optional<std::string> floppy;
    bool floppyReadOnly = false;
    bool seSecondFloppy = false;
    std::optional<std::string> ide;
    std::optional<int> monitorWidth;
    bool kiosk = false;
    std::string crtPreset;
    bool turbo = false;
    bool audio = true;
    bool driveSounds = true;

    static SessionCapture from(const RuntimeConfig& config);
    std::vector<SessionEntry> entries() const;
};

// The session a command line names (none: no session, no error), loaded.
// Two session arguments are an error: one session per process.
SessionParse loadSessionArgument(int argc, char* const argv[]);

// `key = value` lines of a version-1 file written at `destination`: paths
// below its directory become relative. Entries keep the order given.
std::string serializeSession(const std::vector<SessionEntry>& entries,
                             const std::filesystem::path& destination);

// Writes the entries to `destination` (through a temporary file renamed in
// place), then reads the file back as a launch would. Empty on success;
// otherwise what failed — a write error, or the diagnostics of the
// reload (a medium that has since disappeared, for instance).
std::string writeSessionFile(const std::filesystem::path& destination,
                             const std::vector<SessionEntry>& entries);

// The `.pomsession` files directly inside `directory`, sorted by name.
std::vector<std::filesystem::path>
listSessions(const std::filesystem::path& directory);

// Every diagnostic as `<file>:<line>: <message>` lines.
std::string describeSessionErrors(const SessionParse& parse);

// UTF-8 ↔ path, so a non-ASCII name survives on every host.
std::filesystem::path pathFromUtf8(std::string_view text);
std::string utf8FromPath(const std::filesystem::path& path);

} // namespace pom68k::app
