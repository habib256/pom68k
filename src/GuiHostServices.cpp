// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "GuiHostServices.h"

#include "GuiShell.h"
#include "MachineHost.h"

#include <GLFW/glfw3.h>

#include <ctime>
#include <system_error>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif
#ifdef _WIN32
#include <process.h>
#endif
namespace pom68k::gui {
namespace {

#ifdef _WIN32
std::time_t utcTime(std::tm* value) { return ::_mkgmtime(value); }
#else
std::time_t utcTime(std::tm* value) { return ::timegm(value); }
#endif
} // namespace
GuiHostServices::GuiHostServices(GuiSessionState& state, GuiSessionObjects& objects, GuiShell& shell, const app::RuntimeConfig& config)
    : state_(state), objects_(objects), shell_(shell), config_(config) {
    const app::NetworkConfig& network = config_.network();
    const app::DiagnosticConfig& diagnostics = config_.diagnostics();
    state_.network.appleTalkEnabled = network.appleTalk;
    state_.network.appleTalkWasSpecified = network.appleTalkWasSpecified;
    state_.network.ltoUdpEnabled = network.ltoUdp;
    state_.network.appleTalkWireBoost = network.appleTalkWireBoost;
    state_.network.shareDirectory = network.shareDirectory;
    state_.network.atalk.configureDiagnostics(
        config_.core().diagnostics.appleTalkTrace, config_.core().diagnostics.macIpTrace);
    state_.diagnostics.keyTraceEnabled = diagnostics.keyTrace;
    state_.diagnostics.freezeProbeEnabled = diagnostics.freezeProbe;
    state_.cpu.speedGauge = GuiSpeedGauge(diagnostics.speedLog, diagnostics.speedLogSkip, diagnostics.speedLogCount);
    // One binding for the whole UI: the registry the composition root put
    // in the core policy IS the session's, and the window, the menu bar
    // and the qualification verdict all read that one.
    state_.peripherals.registry = config_.core().firmware.registry;
    state_.relaunch.launchArguments = config_.launchArguments();
    state_.peripherals.relaunch = [this](std::vector<FirmwareOverride> overrides) {
        state_.relaunch.firmwareOverrides = std::move(overrides);
        state_.relaunch.switchArguments = state_.relaunch.launchArguments;
        if (state_.relaunch.switchArguments.empty())
            state_.relaunch.switchArguments = {std::string()};
        state_.relaunch.showWindow = true;
    };
}
GuiHostServices::~GuiHostServices() {
    state_.peripherals.relaunch = {};
    state_.cpu.setCpuEngine = {};
    state_.cpu.getCpuEngine = {};
    state_.cpu.jitStats = {};
    state_.cpu.speedSample = {};
    state_.cpu.jitBackend = nullptr;
}
std::uint32_t GuiHostServices::hostMacSeconds() const {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    ::localtime_s(&local, &now);
#else
    ::localtime_r(&now, &local);
#endif
    return std::uint32_t(std::uint64_t(std::int64_t(utcTime(&local))) +
                         2082844800ULL);
}
void GuiHostServices::configureAppleTalk() {
    if (state_.network.configured) return;
    state_.network.configured = true;
    namespace fs = std::filesystem;
    std::error_code error;
    fs::path directory;
    if (!state_.network.shareDirectory.empty()) {
        directory = state_.network.shareDirectory;
    } else {
        const std::string executableDir =
            app::MachineFactory::executableDirectory();
        directory = executableDir.empty()
            ? fs::path("AppleShare")
            : fs::weakly_canonical(
                  fs::path(executableDir) / ".." / "AppleShare", error);
        if (directory.empty()) directory = "AppleShare";
    }
    fs::create_directories(directory, error);
    state_.network.atalk.setDefaultShareDir(
        fs::absolute(directory, error).string());
    if (!state_.network.appleTalkWasSpecified)
        std::fprintf(stderr,
                     "AppleTalk: in-process stack active (share %s; "
                     "POM68K_APPLETALK=0 disables)\n",
                     directory.string().c_str());
}
void GuiHostServices::initializeDriveSounds(MacAudioHost& audioHost) {
    if (!state_.audio.initialized) {
        state_.audio.initialized = true;
        const std::string probe =
            locate("assets/floppy_samples/35_step_1_1.wav");
        const std::string directory = probe.empty()
            ? std::string("assets/floppy_samples")
            : probe.substr(0, probe.find_last_of('/'));
        const bool floppyLoaded = state_.audio.floppySfx.loadSamples(
            directory, FloppySound::FormFactor::FF35);
        const bool hardDiskLoaded = state_.audio.hddSfx.loadSamples(
            directory, FloppySound::FormFactor::FF525);
        if (!floppyLoaded || !hardDiskLoaded)
            std::fprintf(stderr,
                         "sfx: drive samples not found under %s "
                         "(mechanical sounds off)\n",
                         directory.c_str());
        state_.audio.hddSfx.setVolume(0.25f);
        state_.audio.hddSfx.setAutoMotorOff(1500.0);
        if (!config_.devices().driveSounds) {
            state_.audio.floppySfx.setMuted(true);
            state_.audio.hddSfx.setMuted(true);
        }
    }
    audioHost.attachFx(&state_.audio.floppySfx);
    audioHost.attachFx(&state_.audio.hddSfx);
}
void GuiHostServices::requestRelaunch(
    GLFWwindow* window, const std::string& romName, const std::string& boot,
    const std::vector<std::string>& extras) {
    state_.relaunch.switchArguments = {romName, boot};
    for (const std::string& extra : extras)
        if (extra != boot) state_.relaunch.switchArguments.push_back(extra);
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int GuiHostServices::processRelaunch() const {
#if !defined(__EMSCRIPTEN__)
    if (shell_.smokeEnabled()) return shell_.finishSmoke(
        !state_.relaunch.switchArguments.empty());
    if (state_.relaunch.switchArguments.empty()) return 0;
    auto relaunchArguments = app::firmwareOverrideArguments(
        app::machineProfileArguments(state_.relaunch.switchArguments,
                                     state_.relaunch.targetProfile),
        state_.relaunch.firmwareOverrides);
    const std::string& executable = config_.executable();
#if defined(_WIN32)
    std::vector<const char*> arguments = {executable.c_str()};
    for (const std::string& argument : relaunchArguments)
        arguments.push_back(argument.c_str());
    arguments.push_back(nullptr);
    ::_execv(executable.c_str(), arguments.data());
#else
    std::vector<char*> arguments = {const_cast<char*>(executable.c_str())};
    for (const std::string& argument : relaunchArguments)
        arguments.push_back(const_cast<char*>(argument.c_str()));
    arguments.push_back(nullptr);
#if defined(__linux__)
    ::execv("/proc/self/exe", arguments.data());
#elif defined(__APPLE__)
    ::execv(executable.c_str(), arguments.data());
#else
    return 0;
#endif
#endif
    std::fprintf(stderr, "relaunch failed — start manually: %s \"%s...\"\n",
                 executable.c_str(),
                 state_.relaunch.switchArguments.front().c_str());
#endif
    return state_.relaunch.switchArguments.empty() ? 0 : 1;
}

void GuiHostServices::traceKey(std::uint8_t adb, bool down) const {
    keyTrace(state_.diagnostics.keyTraceEnabled, "push", adb, down);
}

} // namespace pom68k::gui
