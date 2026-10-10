// POM68K — gate `session_config_test`: session files (src/SessionFile.h).
//
//   • the schema — every refusal is reported, with its line, before any
//     machine exists: missing header, unsupported version, unknown and
//     duplicate keys, invalid values, missing input files;
//   • paths — relative values resolve against the file, the writer keeps a
//     path below the file's directory relative, so a directory moved whole
//     reopens; spaces, outer blanks and non-ASCII names survive the round
//     trip;
//   • precedence — command line > session > environment > default, through
//     RuntimeConfig::parse itself: startup values replace the
//     environment's (a presence option included), command-line options
//     win over the session's, and positional ROM/media replace its set;
//   • identity — MachineFactory refuses a session whose ROM starts another
//     profile than the one it names, and accepts the one it names.
//
// No ROM, no disk image: every input is a file this gate writes.

#include "MachineFactory.h"
#include "MachineSession.h"
#include "RuntimeConfig.h"
#include "SessionFile.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace pom68k;
using namespace pom68k::app;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

void writeFile(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

// A 512 KB "ROM" whose checksum word picks its profile (MachineFactory).
std::string rom512(std::uint32_t checksum) {
    std::string bytes(512u << 10, '\0');
    for (int i = 0; i < 4; ++i)
        bytes[i] = char((checksum >> (24 - 8 * i)) & 0xFF);
    return bytes;
}

bool hasError(const SessionParse& parse, int line, const std::string& text) {
    for (const SessionDiagnostic& error : parse.errors)
        if (error.line == line && error.message.find(text) != std::string::npos)
            return true;
    return false;
}

std::optional<std::string> valueOf(const SessionFile& session,
                                   std::string_view key) {
    for (const SessionEntry& entry : session.entries)
        if (entry.key == key) return entry.value;
    return std::nullopt;
}

struct Argv {
    std::vector<std::string> owned;
    std::vector<char*> pointers;
    explicit Argv(std::vector<std::string> arguments) : owned(std::move(arguments)) {
        owned.insert(owned.begin(), "POM68K");
        for (std::string& argument : owned) pointers.push_back(argument.data());
        pointers.push_back(nullptr);
    }
    int argc() const { return int(owned.size()); }
    char* const* argv() { return pointers.data(); }
};

struct RecordingRuntime final : MachineSessionRuntime {
    SnapMachine* ran;
    explicit RecordingRuntime(SnapMachine* target) : ran(target) {}
    int run(MachineSession& session) override {
        *ran = session.profile().snapshot;
        return 0;
    }
};

void schema(const fs::path& root) {
    const auto none = [](const fs::path&) { return false; };
    const auto all = [](const fs::path&) { return true; };
    const fs::path source = root / "schema.pomsession";

    check(hasError(parseSession("", source, all), 0, "empty session"),
          "an empty file is refused");
    check(hasError(parseSession("# only a comment\nrom = x\n", source, all), 2,
                   "not a POM68K session"),
          "a file without the header line is refused at that line");
    const SessionParse future =
        parseSession("pom68k-session 2\nprofile = lcii\n", source, all);
    check(!future.session && hasError(future, 1, "unsupported session format version `2`"),
          "an unsupported version is refused by number");

    const SessionParse bad = parseSession(
        "pom68k-session 1\n"
        "colour = blue\n"            // 2 unknown
        "profile = lcii\n"
        "profile = q605\n"           // 4 twice
        "engine = fast\n"            // 5 choice
        "daynaport = 7\n"            // 6 range
        "serial-modem = com1\n"      // 7 transport
        "kiosk = yes\n"              // 8 boolean
        "monitor = wide\n"           // 9 integer
        "firmware = adb:maybe:\n"    // 10 shape
        "rom = roms/none.rom\n"      // 11 missing
        "media = \"unterminated\n"   // 12 quoting
        "just words\n"               // 13 no '='
        "atalk-share =\n",           // 14 empty path
        source, none);
    check(!bad.session, "any error refuses the whole session");
    check(hasError(bad, 2, "unknown key `colour`"), "unknown key, line 2");
    check(hasError(bad, 4, "given twice"), "duplicate single key, line 4");
    check(hasError(bad, 5, "interp|jit"), "engine outside its choices, line 5");
    check(hasError(bad, 6, "2-6"), "DaynaPort outside 0/2-6, line 6");
    check(hasError(bad, 7, "pty, tcp:<port> or terminal"), "serial endpoint, line 7");
    check(hasError(bad, 8, "0 or 1"), "boolean spelling, line 8");
    check(hasError(bad, 9, "decimal"), "integer, line 9");
    check(hasError(bad, 10, "<lle|hle>"), "firmware policy shape, line 10");
    check(hasError(bad, 11, "missing file") &&
              hasError(bad, 11, utf8FromPath(root / "roms/none.rom")),
          "a missing ROM is named by its resolved path, line 11");
    check(hasError(bad, 12, "malformed quoted"), "quoting, line 12");
    check(hasError(bad, 13, "key = value"), "a line without '=', line 13");
    check(hasError(bad, 14, "empty path"), "empty path, line 14");
    check(bad.errors.size() == 12, "every error is reported, not just the first");
    check(hasError(parseSession("pom68k-session 1\nprofile = lc9000\n",
                                source, all), 2, "unknown machine profile"),
          "a profile outside the catalogue is refused");
    const std::string described = describeSessionErrors(bad);
    check(described.find(utf8FromPath(source) + ":6: ") != std::string::npos,
          "diagnostics read <file>:<line>: <message>");
}

void paths(const fs::path& root) {
    const fs::path home = root / "Sessions été";
    const fs::path rom = home / "roms" / "LC II.rom";
    const fs::path disk = home / "disques" / "Système 7.5 — boot.vhd";
    const fs::path outside = root / "elsewhere" / "floppy.dsk";
    const fs::path firmware = home / "fw" / "341s0788.bin";
    writeFile(rom, rom512(0x35C28F5F));
    writeFile(disk, "disk");
    writeFile(outside, "floppy");
    writeFile(firmware, "mcu");
    const fs::path file = home / "lc ii.pomsession";

    const std::vector<SessionEntry> entries = {
        {"profile", "lcii"},
        {"rom", utf8FromPath(rom)},
        {"media", utf8FromPath(disk)},
        {"floppy", utf8FromPath(outside)},
        {"firmware", "cuda:lle:" + utf8FromPath(firmware)},
        {"atalk-server", "  Serveur de Gist  "},
        {"atalk-volume", "\"quoted\" name"},
        {"atalk-share", utf8FromPath(home / "Partage")},
        {"engine", "interp"},
    };
    const std::string text = serializeSession(entries, file);
    writeFile(file, text);
    check(text.find("rom = roms/LC II.rom\n") != std::string::npos,
          "a path below the session's directory is written relatively");
    check(text.find("floppy = " + utf8FromPath(outside) + "\n") !=
              std::string::npos,
          "a path outside it stays absolute");
    check(text.find("firmware = cuda:lle:fw/341s0788.bin\n") != std::string::npos,
          "the firmware override's path is relative too");

    SessionParse loaded = loadSession(file);
    check(loaded.session.has_value(), "the written session loads");
    if (!loaded.session) {
        std::printf("%s", describeSessionErrors(loaded).c_str());
        return;
    }
    const SessionFile& session = *loaded.session;
    check(session.rom == utf8FromPath(rom) && session.media.size() == 1 &&
              session.media[0] == utf8FromPath(disk),
          "ROM and media resolve back to the absolute originals (spaces, UTF-8)");
    check(valueOf(session, "atalk-server") == "  Serveur de Gist  " &&
              valueOf(session, "atalk-volume") == "\"quoted\" name",
          "outer blanks and quotes survive the round trip");
    check(serializeSession(session.entries, file) == text,
          "writing what was read gives the same file");

    const fs::path moved = root / "moved here";
    fs::rename(home, moved);
    const SessionParse reopened = loadSession(moved / "lc ii.pomsession");
    check(reopened.session &&
              reopened.session->rom == utf8FromPath(moved / "roms" / "LC II.rom") &&
              valueOf(*reopened.session, "firmware") ==
                  "cuda:lle:" + utf8FromPath(moved / "fw" / "341s0788.bin") &&
              valueOf(*reopened.session, "floppy") == utf8FromPath(outside),
          "a directory moved whole reopens with its relative paths");
    fs::remove(moved / "disques" / "Système 7.5 — boot.vhd");
    const SessionParse broken = loadSession(moved / "lc ii.pomsession");
    check(!broken.session && hasError(broken, 5, "missing file"),
          "a medium removed after saving refuses the session before launch");
    fs::rename(moved, home);
}

void precedence(const fs::path& root) {
    const fs::path rom = root / "p" / "lc.rom";
    const fs::path other = root / "p" / "other.rom";
    const fs::path disk = root / "p" / "boot.vhd";
    writeFile(rom, rom512(0x350EACF0));     // Macintosh LC
    writeFile(other, rom512(0x00000000));   // falls through to the LC II
    writeFile(disk, "disk");
    const fs::path file = root / "p" / "lc.pomsession";
    writeFile(file,
              "pom68k-session 1\n"
              "profile = lc\n"
              "rom = lc.rom\n"
              "media = boot.vhd\n"
              "engine = interp\n"
              "localtalk-udp = 0\n"
              "fpu = 0\n"
              "daynaport = 3\n"
              "atalk-server = From session\n"
              "turbo = 1\n");
    const SessionParse loaded = loadSession(file);
    check(loaded.session.has_value(), "precedence fixture loads");
    if (!loaded.session) return;
    const StartupSnapshot environment{
        {"POM68K_CPU_ENGINE", "jit"},
        {"POM68K_LTOUDP", "1"},
        {"POM68K_AUDIO", "0"},
    };

    Argv plain({"--session=" + utf8FromPath(file)});
    RuntimeConfig config = RuntimeConfig::parse(
        plain.argc(), plain.argv(), environment, &*loaded.session);
    check(config.jit().resolved.engine == jit::EngineKind::Interp,
          "a session startup value replaces the environment's");
    check(!config.network().ltoUdp,
          "`0` removes a presence option the environment set");
    check(!config.devices().audio, "an environment value the session lacks stays");
    check(!config.cpu().fpu && config.devices().turbo,
          "fpu = 0 and turbo = 1 reach the typed configuration");
    check(config.core().bus.daynaPortId == 3 &&
              config.network().serverName == "From session",
          "session relaunch arguments are applied");
    check(config.romPath() == utf8FromPath(rom) &&
              config.mediaArguments() == std::vector<std::string>{utf8FromPath(disk)},
          "the session's ROM and media are the positional inputs");
    check(config.sessionPath() == file &&
              config.sessionProfile() == SnapMachine::Lc,
          "the configuration names its session and profile");
    check(config.launchArguments() ==
              std::vector<std::string>{"--session=" + utf8FromPath(file)},
          "the relaunch line carries the session, not its expansion");

    Argv overrides({"--session=" + utf8FromPath(file), "--daynaport=5",
                    "--atalk-server=From command line",
                    "--machine-profile=lcii", utf8FromPath(other)});
    config = RuntimeConfig::parse(overrides.argc(), overrides.argv(),
                                  environment, &*loaded.session);
    check(config.core().bus.daynaPortId == 5 &&
              config.network().serverName == "From command line",
          "command-line options win over the session's");
    check(config.romPath() == utf8FromPath(other) && config.mediaArguments().empty(),
          "a command-line ROM replaces the session's ROM and media together");
    check(config.sessionProfile() == SnapMachine::LcII,
          "a command-line profile replaces the session's");

    // Identity: the LC session on the LC ROM runs; the same session on the
    // fall-through ROM is refused by name.
    SnapMachine ran = SnapMachine::Plus;
    config = RuntimeConfig::parse(plain.argc(), plain.argv(), environment,
                                  &*loaded.session);
    MachineSession accepted = MachineFactory::create(
        std::move(config), std::make_unique<RecordingRuntime>(&ran));
    check(accepted.run() == 0 && ran == SnapMachine::Lc,
          "a session starts the profile it names");
    Argv wrongRom({"--session=" + utf8FromPath(file), utf8FromPath(other)});
    ran = SnapMachine::Plus;
    MachineSession refused = MachineFactory::create(
        RuntimeConfig::parse(wrongRom.argc(), wrongRom.argv(), environment,
                             &*loaded.session),
        std::make_unique<RecordingRuntime>(&ran));
    check(refused.run() == 2 && ran == SnapMachine::Plus,
          "a ROM of another profile is refused before the machine runs");

    Argv noSession({utf8FromPath(other)});
    config = RuntimeConfig::parse(noSession.argc(), noSession.argv(), environment);
    check(config.sessionPath().empty() && !config.sessionProfile() &&
              config.jit().resolved.engine == jit::EngineKind::Jit &&
              config.network().ltoUdp,
          "without a session nothing changes");
    Argv two({"--session=" + utf8FromPath(file), utf8FromPath(file)});
    const SessionParse twice = loadSessionArgument(two.argc(), two.argv());
    check(!twice.session && hasError(twice, 0, "one session per process"),
          "a second session argument is refused");
    Argv one({"--daynaport=3", utf8FromPath(file)});
    const SessionParse positional = loadSessionArgument(one.argc(), one.argv());
    check(positional.session && positional.session->source == file,
          "a positional .pomsession is the session");
    check(sessionArgumentPath("/x/a.pomsession") == "/x/a.pomsession" &&
              sessionArgumentPath("--session=b") == "b" &&
              !sessionArgumentPath("disk.vhd") &&
              !sessionArgumentPath(".pomsession"),
          "a session is named by --session= or a .pomsession positional");
}

// Save what a configured process runs, reopen it under an EMPTY
// environment, and find the same typed configuration: "reopen the same
// configured machine".
void capture(const fs::path& root) {
    const fs::path dir = root / "capture";
    const fs::path rom = dir / "lcii.rom";
    const fs::path disk = dir / "disk one.vhd";
    const fs::path floppy = dir / "boot.dsk";
    const fs::path ide = dir / "ide.hda";
    const fs::path adb = dir / "fw" / "adb.bin";
    writeFile(rom, rom512(0x35C28F5F));
    writeFile(disk, "disk");
    writeFile(floppy, "floppy");
    writeFile(ide, "ide");
    writeFile(adb, "adb");
    const StartupSnapshot environment{
        {"POM68K_CPU_ENGINE", "interp"},
        {"POM68K_JIT_BACKEND", "threaded"},
        {"POM68K_NOFPU", "1"},
        {"POM68K_APPLETALK", "0"},
        {"POM68K_LTOUDP", "1"},
        {"POM68K_SERIAL_MODEM", "tcp:6502"},
        {"POM68K_SERIAL_PRINTER", "pty"},
        {"POM68K_FLOPPY", utf8FromPath(floppy)},
        {"POM68K_FLOPPY_RO", "1"},
        {"POM68K_IDE", utf8FromPath(ide)},
        {"POM68K_MONITOR", "512"},
        {"POM68K_KIOSK", "1"},
        {"POM68K_CRT", "phosphor"},
        {"POM68K_TURBO", "1"},
        {"POM68K_AUDIO", "0"},
        {"POM68K_DRIVE_SFX", "0"},
        {"POM68K_SHARE_DIR", utf8FromPath(dir / "share")},
    };
    Argv argv({"--machine-profile=lcii", "--daynaport=4",
               "--firmware-override=adb:hle:" + utf8FromPath(adb),
               "--atalk-server=Mon serveur", "--atalk-volume=",
               "--atalk-gateway=10.0.0.1/24", utf8FromPath(rom),
               utf8FromPath(disk)});
    const RuntimeConfig original =
        RuntimeConfig::parse(argv.argc(), argv.argv(), environment);
    SessionCapture captured = SessionCapture::from(original);
    captured.profile = SnapMachine::LcII; // what the shell says is running
    const fs::path file = dir / "saved.pomsession";
    writeFile(file, serializeSession(captured.entries(), file));

    const SessionParse loaded = loadSession(file);
    check(loaded.session.has_value(), "a captured session loads");
    if (!loaded.session) {
        std::printf("%s", describeSessionErrors(loaded).c_str());
        return;
    }
    Argv reopen({"--session=" + utf8FromPath(file)});
    const RuntimeConfig again = RuntimeConfig::parse(
        reopen.argc(), reopen.argv(), StartupSnapshot{}, &*loaded.session);
    check(again.romPath() == original.romPath() &&
              again.mediaArguments() == original.mediaArguments() &&
              again.sessionProfile() == SnapMachine::LcII,
          "reopened: profile, ROM and media");
    check(again.jit().resolved.engine == original.jit().resolved.engine &&
              again.jit().resolved.backend == "threaded" &&
              again.cpu().fpu == original.cpu().fpu,
          "reopened: engine, backend and FPU");
    const auto& f0 = original.core().firmware;
    const auto& f1 = again.core().firmware;
    check(f1.adbLle == f0.adbLle && f1.adbPath == f0.adbPath &&
              f1.egretLle == f0.egretLle && f1.cudaLle == f0.cudaLle &&
              f1.tobyDeclLle == f0.tobyDeclLle && !f0.adbLle,
          "reopened: firmware provenance");
    const NetworkConfig& n0 = original.network();
    const NetworkConfig& n1 = again.network();
    check(again.core().bus.daynaPortId == 4 && n1.appleTalk == n0.appleTalk &&
              n1.ltoUdp && n1.shareDirectory == n0.shareDirectory &&
              n1.serverName == "Mon serveur" && n1.volumeName == "" &&
              n1.gateway == n0.gateway,
          "reopened: DaynaPort card and network services");
    const DeviceConfig& d0 = original.devices();
    const DeviceConfig& d1 = again.devices();
    check(d1.serialModem.kind == SerialTransportKind::Tcp &&
              d1.serialModem.tcpPort == 6502 &&
              d1.serialPrinter.kind == SerialTransportKind::Pty,
          "reopened: serial endpoints");
    check(d1.startupFloppy == d0.startupFloppy &&
              d1.floppyWriteBack == d0.floppyWriteBack &&
              again.core().storage.ideDisk == original.core().storage.ideDisk &&
              d1.monitorWidth == 512 && d1.kiosk && d1.crtPreset == "phosphor" &&
              d1.turbo && !d1.audio && !d1.driveSounds,
          "reopened: media options, display and sound");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() /
        ("pom68k_session_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    schema(root);
    paths(root);
    precedence(root);
    capture(root);
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
