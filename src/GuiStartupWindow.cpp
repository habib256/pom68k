// POM68K — first launch and preparation of another Macintosh.
#include "GuiStartupWindow.h"
#include "GuiSessionState.h"
#include "MachineFactory.h"
#include "DiskBays.h"
#include "SessionFile.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace pom68k::gui {
namespace {
namespace fs = std::filesystem;

void copyPath(std::array<char, 1024>& field, const std::string& path) {
    std::snprintf(field.data(), field.size(), "%s", path.c_str());
}

bool romSize(std::uintmax_t size) {
    return size == (64u << 10) || size == (128u << 10) ||
           size == (256u << 10) || size == (512u << 10) || size == (1u << 20);
}

bool matchesModel(const MachineProfile& profile, std::string_view query) {
    if (query.empty()) return true;
    const auto contains = [query](std::string_view text) {
        return std::search(text.begin(), text.end(), query.begin(), query.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            }) != text.end();
    };
    return contains(profile.label) || contains(profile.group) ||
           contains(profile.slug);
}

void discoverRom(GuiStartupState& form, bool refresh = false) {
    const MachineProfile& profile = *machineProfile(form.profile);
    std::string found = app::MachineFactory::findPath(profile.romPath);
    if (found.empty() && profile.romCrc32)
        found = app::MachineFactory::findRomBySignature(profile.romCrc32, refresh);
    copyPath(form.rom, found.empty() ? profile.romPath : found);
    form.ready = false;
    form.message.clear();
}

void startBrowsing(GuiStartupState& form, int kind) {
    form.browsing = kind;
    const auto path = app::pathFromUtf8(kind == 1 ? form.rom.data() : form.media.data());
    std::error_code error;
    const auto parent = path.parent_path();
    copyPath(form.folder, app::utf8FromPath(fs::is_directory(parent, error)
        ? parent : fs::current_path(error)));
    form.browseError.clear();
}

void drawFileBrowser(GuiStartupState& form) {
    if (!form.browsing) return;
    ImGui::SeparatorText(form.browsing == 1 ? "Choose a ROM" : "Choose a startup disk");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##browseFolder", form.folder.data(), form.folder.size());
    const auto directory = app::pathFromUtf8(form.folder.data());
    if (ImGui::SmallButton("Parent folder"))
        copyPath(form.folder, app::utf8FromPath(directory.parent_path()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Close browser")) {
        form.browsing = 0;
        return;
    }
    struct Entry { fs::path path; bool directory; };
    std::vector<Entry> entries;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const bool folder = it->is_directory(error);
        if (error) break;
        if (folder || it->is_regular_file(error)) entries.push_back({it->path(), folder});
    }
    form.browseError = error ? "Cannot open folder: " + error.message() : "";
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory;
        return a.path.filename() < b.path.filename();
    });
    ImGui::BeginChild("Files", ImVec2(0, 150), ImGuiChildFlags_Borders);
    for (const Entry& entry : entries) {
        const std::string label = app::utf8FromPath(entry.path.filename()) +
                                  (entry.directory ? " /" : "");
        if (ImGui::Selectable(label.c_str())) {
            if (entry.directory) copyPath(form.folder, app::utf8FromPath(entry.path));
            else {
                std::error_code absoluteError;
                const auto absolute = fs::absolute(entry.path, absoluteError);
                if (absoluteError) {
                    form.browseError = absoluteError.message();
                    break;
                }
                copyPath(form.browsing == 1 ? form.rom : form.media,
                         app::utf8FromPath(absolute));
                form.browsing = 0;
                form.ready = false;
                form.message.clear();
            }
            break;
        }
    }
    if (entries.empty() && !error) ImGui::TextDisabled("This folder is empty.");
    ImGui::EndChild();
    if (!form.browseError.empty()) ImGui::TextWrapped("%s", form.browseError.c_str());
}
} // namespace

void openStartupWindow(GuiSessionState& state, SnapMachine current) {
    GuiStartupState& form = state.startup;
    form.profile = form.missingRom ? SnapMachine::LcII : current;
    form.floppy = !storageCapabilities(*machineProfile(form.profile)).scsi;
    form.open = form.popupRequested = true;
    form.ready = false;
    form.browsing = 0;
    form.modelSearch.fill(0);
    form.media[0] = 0;
    discoverRom(form);
    if (!form.missingRom && !state.sessionFile.romName.empty())
        copyPath(form.rom, state.sessionFile.romName);
}

bool validateStartup(GuiStartupState& form) {
    form.ready = false;
    form.arguments.clear();
    const MachineProfile* profile = machineProfile(form.profile);
    if (!profile) { form.message = "Choose a Macintosh model."; return false; }
    std::error_code error;
    const auto romPath = app::pathFromUtf8(form.rom.data());
    if (!fs::is_regular_file(romPath, error)) {
        form.message = "ROM not found. Choose the ROM file for this Macintosh.";
        return false;
    }
    const auto size = fs::file_size(romPath, error);
    if (error || !romSize(size)) {
        form.message = "Unsupported ROM size: expected 64, 128, 256, or 512 KB, or 1 MB.";
        return false;
    }
    const auto rom = app::MachineFactory::readFile(form.rom.data());
    if (rom.size() != size) { form.message = "Could not read the complete ROM."; return false; }
    // The factory owns identity decoding, including every shared-ROM model.
    std::string option = std::string(app::kMachineProfileOption) + profile->slug;
    char executable[] = "POM68K";
    char* argv[] = {executable, option.data()};
    const auto config = app::RuntimeConfig::parse(2, argv, StartupSnapshot{});
    const auto& detected = app::MachineFactory::selectProfile(config, rom);
    if (detected.snapshot != profile->snapshot) {
        form.message = std::string("This ROM belongs to ") + detected.label +
                       ". Choose that model or select another ROM.";
        return false;
    }
    const auto medium = app::pathFromUtf8(form.media.data());
    if (!fs::is_regular_file(medium, error) || fs::file_size(medium, error) == 0 || error) {
        form.message = "Startup disk is missing or empty. Choose an image containing Mac OS.";
        return false;
    }
    const auto capabilities = storageCapabilities(*profile);
    const bool floppy = profile->platform == PlatformKind::Compact &&
                        (!capabilities.scsi || form.floppy);
    if (!diskBaysPathIsImage(form.media.data()) || diskBaysPathIsCd(form.media.data()) ||
        (floppy != diskBaysPathIsFloppy(form.media.data()))) {
        form.message = floppy ? "Choose a compatible startup floppy image."
                              : "Choose a startup hard disk image; CDs and floppies are not valid here.";
        return false;
    }
    const auto absoluteRom = fs::absolute(romPath, error);
    if (error) { form.message = "Cannot access the ROM path."; return false; }
    const auto absoluteMedia = fs::absolute(medium, error);
    if (error) { form.message = "Cannot access the disk path."; return false; }
    form.arguments = {option, app::utf8FromPath(absoluteRom)};
    if (profile->platform == PlatformKind::Compact && !floppy)
        form.arguments.emplace_back(); // compact slot 0 is the floppy
    form.arguments.push_back(app::utf8FromPath(absoluteMedia));
    form.ready = true;
    form.message = "ROM matches this model and the disk is present. Startup will check system compatibility.";
    return true;
}

void drawStartupWindow(GuiSessionState& state, SnapMachine current) {
    GuiStartupState& form = state.startup;
    if (!form.considered) {
        form.considered = true;
        if (state.sessionFile.romName.empty()) {
            form.missingRom = true;
            openStartupWindow(state, current);
        } else {
            std::error_code error;
            const auto size = fs::file_size(app::pathFromUtf8(state.sessionFile.romName), error);
            form.missingRom = error || !romSize(size);
            if (form.missingRom) openStartupWindow(state, current);
        }
    }
    if (form.popupRequested) {
        ImGui::OpenPopup(kStartupWindowTitle);
        form.popupRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560, 180),
        ImVec2(720, ImGui::GetMainViewport()->WorkSize.y - 40));
    if (!ImGui::BeginPopupModal(kStartupWindowTitle, &form.open,
                              ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(0);
    if (form.missingRom)
        ImGui::TextWrapped("No usable Macintosh ROM was loaded. The current screen is a 68000 demo. Set up your Mac here to start Mac OS.");
    else
        ImGui::TextWrapped("Set up another Mac. Launching it restarts the emulator; save your work in the current Mac first.");
    ImGui::SeparatorText("1. Choose a Mac");
    const MachineProfile* selected = machineProfile(form.profile);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##startupModel", selected ? selected->label : "Choose a model")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##modelSearch", "Search model or family...",
                                 form.modelSearch.data(), form.modelSearch.size());
        ImGui::BeginChild("Models", ImVec2(0, 240), ImGuiChildFlags_Borders);
        std::string_view group;
        int visible = 0;
        bool picked = false;
        for (const auto& profile : kMachineProfiles) {
            if (!matchesModel(profile, form.modelSearch.data())) continue;
            if (group != profile.group) {
                group = profile.group;
                ImGui::SeparatorText(profile.group);
            }
            ++visible;
            if (ImGui::Selectable(profile.label, form.profile == profile.snapshot)) {
                form.profile = profile.snapshot;
                form.floppy = !storageCapabilities(profile).scsi;
                discoverRom(form);
                picked = true;
            }
        }
        if (!visible) ImGui::TextDisabled("No matching Macintosh models.");
        ImGui::EndChild();
        if (picked) form.modelSearch.fill(0);
        ImGui::EndCombo();
    }
    selected = machineProfile(form.profile);
    ImGui::SeparatorText("2. Select a ROM");
    ImGui::TextWrapped("ROMs and startup disks are your own files. POM68K does not include them.");
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::InputTextWithHint("##startupRom", "ROM file path", form.rom.data(), form.rom.size());
    if (ImGui::Button("Browse ROM...")) startBrowsing(form, 1);
    ImGui::SameLine();
    if (ImGui::Button("Search roms/")) discoverRom(form, true);
    ImGui::SeparatorText("3. Choose a startup disk");
    if (selected && selected->platform == PlatformKind::Compact && storageCapabilities(*selected).scsi) {
        if (ImGui::RadioButton("Hard disk", !form.floppy)) { form.floppy = false; changed = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Floppy", form.floppy)) { form.floppy = true; changed = true; }
    }
    ImGui::TextWrapped(form.floppy ? "Choose a floppy with a system compatible with this Mac."
                                  : "Choose a hard disk with a compatible system. An empty disk cannot start Mac OS.");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::InputTextWithHint("##startupMedia", "Startup disk image path", form.media.data(), form.media.size());
    if (ImGui::Button("Browse disk...")) startBrowsing(form, 2);
    drawFileBrowser(form);
    if (changed) { form.ready = false; form.message.clear(); }
    if (form.profile == SnapMachine::Duo230)
        ImGui::TextWrapped("The Duo 230 also needs power manager firmware at roms/pge/pge_boot.bin.");
    ImGui::Separator();
    if (ImGui::Button("Start Macintosh") && validateStartup(form)) {
        state.relaunch.switchArguments = form.arguments;
        state.relaunch.verbatim = true;
        state.relaunch.closeWindow = true;
        form.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Check setup")) validateStartup(form);
    ImGui::SameLine();
    if (ImGui::Button(form.missingRom ? "Continue demo" : "Cancel")) {
        form.open = false;
        ImGui::CloseCurrentPopup();
    }
    if (!form.message.empty()) ImGui::TextWrapped("%s", form.message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}
} // namespace pom68k::gui
