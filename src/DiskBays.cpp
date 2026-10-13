// DiskBays -- the one "Disques" window. See DiskBays.h for the contract.

#include "DiskBays.h"
#include "GuiDisplay.h"
#include "DockLayout.h"
#include "HfsBlankVolume.h"
#include "ScsiDisk.h"

#include "imgui.h"


#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

namespace pom68k {
namespace {

namespace fs = std::filesystem;

constexpr int kMaxBays = 6;                 // SCSI 1..6; 0 is the boot disk

bool  gOpen         = true;    // a base window: docked right by default
bool  gStaged       = false;                // a reboot-requiring edit is pending
bool  gReserveEmpty = false;                // opt-in: empty removable bays at boot
char  gPathEntry[1024] = {0};
char  gBrowseFolder[1024] = {0};
char  gNewDiskName[32] = "New disk";
int   gNewDiskMb = 250;
std::string gLastError;
std::string gAddedMessage;
bool gScrollToAdded = false;

// Images the user brought in this session (dropped or typed). Kept separate
// from the scanned set so a path outside hdv/ survives a rescan.
std::vector<std::string> gSessionImages;

// Staged configuration. Only meaningful while gStaged is true.
std::string              gStagedBoot;
std::vector<std::string> gStagedExtras;

bool endsWithNoCase(const std::string& p, const char* ext) {
    size_t n = std::char_traits<char>::length(ext);
    if (p.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(p[p.size() - n + i]) != ext[i]) return false;
    return true;
}

bool hasCdExtension(const std::string& p) {
    return endsWithNoCase(p, ".iso")  || endsWithNoCase(p, ".cdr")
        || endsWithNoCase(p, ".toast") || endsWithNoCase(p, ".cue")
        || endsWithNoCase(p, ".bin");
}

// Extension first, then the Apple prefix: a .toast that declares 512-byte
// blocks is a disk dump. Putting it in the CD bay is why it appears in
// Disques and never on the desktop (CHANGELOG 2026-08-15, 2026-09-13).
bool isCd(const std::string& p) {
    if (!hasCdExtension(p)) return false;
    if (endsWithNoCase(p, ".cue") || endsWithNoCase(p, ".bin")) return true;
    return scsiAppleImageBlockSize(p) != 512;
}

// Everything the command line accepts. The old menu listed only .vhd/.hda/
// .img, which is why a bootable `System 7.1 HD.dsk` never showed up in it.
bool isDiskImage(const std::string& p) {
    return endsWithNoCase(p, ".vhd") || endsWithNoCase(p, ".hda")
        || endsWithNoCase(p, ".img") || endsWithNoCase(p, ".dsk")
        || endsWithNoCase(p, ".image") || endsWithNoCase(p, ".dart") || endsWithNoCase(p, ".moof")
        || isCd(p);
}

bool samePath(const std::string& a, const std::string& b) {
    if (a == b) return true;
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

void rememberImage(const std::string& path) {
    bool known = false;
    for (const std::string& image : gSessionImages)
        if (samePath(image, path)) { known = true; break; }
    if (!known) gSessionImages.push_back(path);
    gAddedMessage = "Added " + fs::path(path).filename().string() +
                    " to the pickers. Choose it in a drive below.";
    gScrollToAdded = true;
    gLastError.clear();
}

bool addImageFile(const std::string& path) {
    std::error_code error;
    if (path.empty()) gLastError = "Choose a disk image first.";
    else if (!fs::is_regular_file(path, error)) gLastError = "File not found: " + path;
    else if (!isDiskImage(path)) gLastError = "Unsupported disk image: " + path;
    else {
        const auto absolute = fs::absolute(path, error);
        if (!error) {
            rememberImage(absolute.string());
            gPathEntry[0] = '\0';
            return true;
        }
        gLastError = "Cannot access image: " + error.message();
    }
    return false;
}

// First existing candidate for a well-known media directory. The emulator is
// run both from the tree root and from build/, so probe upward a little.
std::string probeDir(const char* name) {
    std::error_code ec;
    for (const std::string& base : { std::string(), std::string("../"),
                                     std::string("../../") }) {
        std::string cand = base + name;
        if (fs::is_directory(cand, ec)) return cand;
    }
    return {};
}

void scanInto(const std::string& dir, std::vector<std::string>& out) {
    if (dir.empty()) return;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        std::string p = e.path().string();
        if (isDiskImage(p)) out.push_back(p);
    }
}

std::string fileName(const std::string& p) {
    return fs::path(p).filename().string();
}

std::string sizeLabel(const std::string& p) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    if (ec) return "?";
    char buf[32];
    if (n >= 1024ull * 1024 * 1024)
        std::snprintf(buf, sizeof buf, "%.1f GB", double(n) / (1024.0 * 1024 * 1024));
    else if (n >= 1024ull * 1024)
        std::snprintf(buf, sizeof buf, "%.0f MB", double(n) / (1024.0 * 1024));
    else
        std::snprintf(buf, sizeof buf, "%.0f KB", double(n) / 1024.0);
    return buf;
}

std::string sanitizeDiskStem(const char* raw) {
    std::string s;
    for (const char* p = raw; *p && s.size() < 27; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c == '/' || c == '\\' || c == ':') continue;
        s.push_back(c == ' ' ? '-' : char(c));
    }
    return s.empty() ? std::string("New-disk") : s;
}

std::string workDiskDir() {
    const std::string w = probeDir("hdv/work");
    if (!w.empty()) return w;
    const std::string h = probeDir("hdv");
    const std::string dir = h.empty() ? std::string("hdv/work") : (h + "/work");
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// Snapshot the running configuration into the staging buffers.
void beginStaging(const DiskBaysHost& host) {
    if (gStaged) return;
    gStagedBoot   = host.bootPath;
    gStagedExtras = host.extras ? *host.extras : std::vector<std::string>{};
    gStaged = true;
}

void discardStaging() {
    gStaged = false;
    gStagedExtras.clear();
    gStagedBoot.clear();
    gLastError.clear();
}

// The bay list as the window shows it: the live extras, or the staged copy.
const std::vector<std::string>& activeExtras(const DiskBaysHost& host) {
    static const std::vector<std::string> kEmpty;
    if (gStaged) return gStagedExtras;
    return host.extras ? *host.extras : kEmpty;
}

// A bay is hot-swappable when the runner gave us the hooks AND a removable
// (CD) target existed there when the ROM probed the bus — either a CD image
// attached at boot or the reserved empty drive (kCdBayToken). Fixed disks
// never swap live: Classic Mac OS only re-reads a medium it saw change.
bool bayIsLive(const DiskBaysHost& host, int index) {
    if (!host.insertBay || !host.ejectBay || !host.bayIsCd) return false;
    if (!host.extras) return false;
    if (index >= int(host.extras->size())) return false;
    if ((*host.extras)[index].empty()) return false;
    return host.bayIsCd(index + 1);
}

int firstEmptyDiskSlot(const DiskBaysHost& host) {
    const std::vector<std::string>& ex = activeExtras(host);
    for (int i = 0; i < kMaxBays; i++) {
        if (bayIsLive(host, i)) continue;
        const std::string cur = i < int(ex.size()) ? ex[size_t(i)] : std::string();
        if (cur.empty()) return i;
    }
    return -1;
}

// A SuperDrive holds 400 K, 800 K or 1.44 MB. Tagged containers add overhead.
constexpr unsigned long long kMaxFloppyBytes = 148ull + 1474560ull + 2880ull * 12ull;
// Legal RLE can use a count word for every literal word, doubling each chunk.
constexpr unsigned long long kMaxDartBytes = 148ull + 72ull * 41920ull;

bool looksLikeFloppy(const std::string& p) {
    if (isCd(p)) return false;
    if (!endsWithNoCase(p, ".dsk") && !endsWithNoCase(p, ".img")
        && !endsWithNoCase(p, ".image") && !endsWithNoCase(p, ".dart") && !endsWithNoCase(p, ".moof")) return false;
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    return !ec && n > 0 && n <= (endsWithNoCase(p, ".moof") ? 32ull << 20 :
        endsWithNoCase(p, ".dart") ? kMaxDartBytes : kMaxFloppyBytes);
}

// ── Image picker ───────────────────────────────────────────────────────────
// Returns true and fills `chosen` when the user picked something this frame.
// The filter is what each row can actually accept: a SuperDrive takes no
// .iso, and a CD drive takes nothing else.
enum class Only { Any, Floppy, Cd, Disk };

bool imageCombo(const char* label, const std::string& current,
                const std::string& nearPath, std::string& chosen,
                Only only = Only::Any) {
    bool picked = false;
    std::string preview = current.empty()
                              ? std::string("<empty>")
                              : current == kCdBayToken
                              ? std::string("<empty CD drive>")
                              : fileName(current) + "  " + sizeLabel(current);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("<empty>", current.empty())) {
            chosen.clear();
            picked = true;
        }
        for (const std::string& d : diskBaysKnownImages(nearPath)) {
            if (only == Only::Floppy && !looksLikeFloppy(d)) continue;
            if (only == Only::Cd && !isCd(d)) continue;
            if (only == Only::Disk && isCd(d)) continue;
            bool sel = samePath(d, current);
            std::string item = fileName(d) + "   " + sizeLabel(d);
            if (isCd(d)) item += "   CD";
            if (isReferenceFixturePath(d)) item += "   reference -> work";
            if (ImGui::Selectable(item.c_str(), sel)) {
                chosen = d;
                picked = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", d.c_str());
        }
        ImGui::EndCombo();
    }
    if (picked) gAddedMessage.clear();
    return picked;
}

// What the guest says about a bay, printed under the host's row. Reads the
// queues the System keeps (GuestScsiView.h); "inconnu" when they are not
// readable yet, never a guess.
void guestBayLine(const DiskBaysHost& host, int id) {
    if (!host.guestView) return;
    const GuestScsiView view = host.guestView();
    const ImVec4 grey = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    if (!view.valid || id < 0 || id >= int(view.bays.size())) {
        ImGui::TextColored(grey, "guest: unknown");
        return;
    }
    const GuestScsiBay& bay = view.bays[size_t(id)];
    if (bay.mounted)
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.35f, 1),
                           "guest: mounted '%s' (drive %d)",
                           bay.volume.c_str(), bay.driveNum);
    else if (bay.driver)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1),
                           "guest: drive %d installed, no volume mounted",
                           bay.driveNum);
    else
        ImGui::TextColored(grey, "guest: no drive - invisible to the System");
}

// A fixed disk may leave the bus live only when the machine offers the
// detach AND the guest's own VCB queue shows no volume on that bay — the
// condition docs/SCSI_HOTPLUG.md § 3 states for a safe detach. No view
// (the queues not readable yet) is « unknown », and unknown stages.
bool canDetachNow(const DiskBaysHost& host, int id) {
    if (!host.detachBay || !host.guestView || gStaged) return false;
    const GuestScsiView v = host.guestView();
    return v.valid && id >= 0 && id < int(v.bays.size()) && !v.bays[size_t(id)].mounted;
}

// Put a fixed disk on the bus now, or stage it when the machine offers no
// live attach. The extras list is what the relaunch line carries, so it is
// updated either way.
void placeDisk(DiskBaysHost& host, int index, const std::string& path) {
    if (host.attachBay && host.extras && host.attachBay(index + 1, path)) {
        while (int(host.extras->size()) <= index) host.extras->emplace_back();
        (*host.extras)[size_t(index)] = path;
        gLastError.clear();
        return;
    }
    beginStaging(host);
    while (int(gStagedExtras.size()) <= index) gStagedExtras.emplace_back();
    gStagedExtras[size_t(index)] = path;
}

void openImageBrowser() {
    std::error_code error;
    const fs::path typed(gPathEntry);
    const fs::path parent = typed.parent_path();
    const fs::path folder = fs::is_directory(parent, error)
        ? parent : fs::current_path(error);
    std::snprintf(gBrowseFolder, sizeof gBrowseFolder, "%s", folder.string().c_str());
    gLastError.clear();
    ImGui::OpenPopup("Add disk image");
}

void drawImageBrowser() {
    ImGui::SetNextWindowSize(ImVec2(540, 430), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Add disk image", nullptr,
                               ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::TextWrapped("Choose an image, then select it in a drive's picker.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##imageFolder", "folder", gBrowseFolder,
                             sizeof gBrowseFolder);
    const fs::path directory(gBrowseFolder);
    if (ImGui::Button("Parent folder")) {
        std::snprintf(gBrowseFolder, sizeof gBrowseFolder, "%s",
                      directory.parent_path().string().c_str());
    }
    struct Entry { fs::path path; bool folder; };
    std::vector<Entry> entries;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const bool folder = it->is_directory(error);
        if (error) break;
        if (folder || (it->is_regular_file(error) && isDiskImage(it->path().string())))
            entries.push_back({it->path(), folder});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.folder != b.folder) return a.folder;
        return a.path.filename() < b.path.filename();
    });
    ImGui::BeginChild("Image files", ImVec2(0, 210), ImGuiChildFlags_Borders);
    for (const Entry& entry : entries) {
        const std::string label = entry.path.filename().string() +
                                  (entry.folder ? " /" : "");
        if (ImGui::Selectable(label.c_str())) {
            if (entry.folder) {
                std::snprintf(gBrowseFolder, sizeof gBrowseFolder, "%s",
                              entry.path.string().c_str());
            } else if (addImageFile(entry.path.string())) {
                ImGui::CloseCurrentPopup();
            }
            break;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.path.string().c_str());
    }
    if (entries.empty() && !error) ImGui::TextDisabled("No disk images in this folder.");
    ImGui::EndChild();
    if (error) ImGui::TextWrapped("Cannot open folder: %s", error.message().c_str());
    ImGui::SetNextItemWidth(-100);
    ImGui::InputTextWithHint("##imagePath", "or enter an image path",
                             gPathEntry, sizeof gPathEntry);
    ImGui::SameLine();
    if (ImGui::Button("Add path") && addImageFile(gPathEntry))
        ImGui::CloseCurrentPopup();
    if (!gLastError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", gLastError.c_str());
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace

// ── Discovery ──────────────────────────────────────────────────────────────

std::vector<std::string> diskBaysKnownImages(const std::string& nearPath) {
    std::vector<std::string> out;
    // Reference fixtures are first-class picker entries. Selecting one is
    // safe: ScsiDisk redirects the writable GUI session to hdv/work/.
    scanInto(probeDir("hdv/ref"), out);
    scanInto(probeDir("hdv/work"), out);
    scanInto(probeDir("hdv"), out);
    // Floppies have the same twin since 2026-09-16: SonyDrive routes a
    // writable insert of disks35/ref/ to disks35/work/.
    scanInto(probeDir("disks35/ref"), out);
    scanInto(probeDir("disks35/work"), out);
    scanInto(probeDir("disks35"), out);
    // `cd/` is to CDs what `disks35/` is to floppies. Before it was scanned,
    // an .iso sitting in the obvious place was invisible to the window and
    // had to be typed in or dropped — the first half of "mounting a CD is
    // complicated" (2026-08-15).
    scanInto(probeDir("cd"), out);
    if (!nearPath.empty()) {
        std::error_code ec;
        scanInto(fs::path(nearPath).parent_path().string(), out);
        (void)ec;
    }
    for (const std::string& s : gSessionImages) out.push_back(s);

    // De-duplicate on the canonical path, but keep the first spelling seen so
    // the list still reads like the paths the user knows.
    std::vector<std::string> uniq;
    std::set<std::string> seen;
    for (const std::string& p : out) {
        std::error_code ec;
        std::string key = fs::weakly_canonical(p, ec).string();
        if (ec || key.empty()) key = p;
        if (seen.insert(key).second) uniq.push_back(p);
    }
    std::sort(uniq.begin(), uniq.end(), [](const std::string& a, const std::string& b) {
        return fileName(a) < fileName(b);
    });
    return uniq;
}

bool diskBaysPathIsCd(const std::string& path) { return isCd(path); }
bool diskBaysPathIsImage(const std::string& path) { return isDiskImage(path); }
bool diskBaysPathIsFloppy(const std::string& path) { return looksLikeFloppy(path); }

// ── Drag and drop ──────────────────────────────────────────────────────────

// The GLFW callback lives in DiskBaysDrop.cpp so that this file, and the
// headless window gate that links it (gui_windows_test), carry no window
// system; the callback feeds each path through here.
bool diskBaysOfferDroppedImage(const std::string& path) {
    if (!isDiskImage(path)) return false;
    rememberImage(path);
    gOpen = true;               // dropping an image is a request to use it
    return true;
}

// ── Menu entry ─────────────────────────────────────────────────────────────

void diskBaysMenuItem(const char* label) {
    if (ImGui::MenuItem(label, nullptr, gOpen))
        gOpen = !gOpen;
}

// ── The window ─────────────────────────────────────────────────────────────

// One floppy drive's row: its disk and « Eject », or the picker. Every
// floppy swaps live, so the three drives share the shape.
static void floppyRow(const char* label, const char* id, const std::string& path,
               const std::function<bool()>& inserted,
               const std::function<void()>& eject,
               const std::function<void(const std::string&)>& insert,
               const std::string& nearPath) {
    ImGui::TextDisabled("%s", label);
    if (inserted && inserted()) {
        ImGui::Text("%s", !path.empty() ? fileName(path).c_str() : "<inserted>");
        ImGui::SameLine();
        const std::string button = std::string("Eject##") + id;
        if (ImGui::SmallButton(button.c_str()) && eject) eject();
    } else {
        ImGui::SetNextItemWidth(-1);
        std::string picked;
        const std::string combo = std::string("##") + id + "pick";
        if (imageCombo(combo.c_str(), std::string(), nearPath, picked, Only::Floppy) &&
            !picked.empty() && insert)
            insert(picked);
    }
    ImGui::Separator();
}

void diskBaysWindow(DiskBaysHost& host) {
    if (!gOpen || pom68k::gui::kioskActive()) return;

    ImGui::SetNextWindowSize(ImVec2(460, 680), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 160),
        ImVec2(FLT_MAX, ImGui::GetMainViewport()->WorkSize.y - 40));
    if (!ImGui::Begin(kDiskWindowTitle, &gOpen)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Add image...")) openImageBrowser();
    ImGui::SameLine();
    ImGui::TextDisabled("or drag a disk image here");
    drawImageBrowser();
    if (!gAddedMessage.empty()) {
        ImGui::TextWrapped("%s", gAddedMessage.c_str());
        if (gScrollToAdded) {
            ImGui::SetScrollHereY(0.0f);
            gScrollToAdded = false;
        }
    }

    const std::string& boot = gStaged ? gStagedBoot : host.bootPath;

    // ── Boot disk. Never hot-swappable: the .pram file follows the boot
    //    volume and the ROM picks its startup device once, at power-on.
    ImGui::SeparatorText("Startup disk");
    ImGui::SetNextItemWidth(-1);
    std::string chosen;
    if (imageCombo("##boot", boot, host.bootPath, chosen)) {
        if (!chosen.empty() && !samePath(chosen, boot)) {
            beginStaging(host);
            gStagedBoot = chosen;
            // The new boot volume must not also sit in a secondary bay.
            gStagedExtras.erase(
                std::remove_if(gStagedExtras.begin(), gStagedExtras.end(),
                               [&](const std::string& e) { return samePath(e, chosen); }),
                gStagedExtras.end());
        }
    }
    ImGui::TextDisabled("Changing this disk requires a restart.");
    guestBayLine(host, 0);

    // Apply is visible before the media list, including in a narrow dock.
    ImGui::Separator();
    if (gStaged) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "Pending restart: disk configuration");
        ImGui::PopTextWrapPos();
        ImGui::BeginDisabled(!host.relaunch);
        if (ImGui::Button("Apply and restart") && host.relaunch) {
            std::vector<std::string> extrasOut;
            for (const std::string& e : gStagedExtras)
                if (!e.empty()) extrasOut.push_back(e);
            // The reserved CD drive: one empty removable target, added only
            // when no CD (with or without disc) is on the bus already.
            if (gReserveEmpty) {
                bool haveCd = false;
                for (const std::string& e : extrasOut)
                    if (e == kCdBayToken || diskBaysPathIsCd(e)) haveCd = true;
                if (!haveCd && int(extrasOut.size()) < kMaxBays)
                    extrasOut.push_back(kCdBayToken);
                gReserveEmpty = false;             // consumed by this apply
            }
            std::string bootOut = gStagedBoot;
            discardStaging();
            host.relaunch(bootOut, extrasOut);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) discardStaging();
    }
    ImGui::BeginDisabled(gStaged || !host.hardReset);
    if (ImGui::Button("Restart machine") && host.hardReset)
        host.hardReset();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restarts the Mac and scans connected disks.\n"
                          "Save your work in the Mac before continuing.");


    ImGui::SeparatorText("Floppies and CDs");
    ImGui::TextDisabled("Insert and eject immediately.");
    // Removable media swap live through the existing host callbacks.
    if (host.hasFloppyDrive)
        floppyRow("Internal floppy (SWIM)", "fd", host.floppyPath,
                  host.floppyInserted, host.ejectFloppy, host.insertFloppy,
                  host.bootPath);
    if (host.hasSecondFloppyDrive)
        floppyRow("Internal floppy 2 (SWIM, second drive)", "fd2",
                  host.secondFloppyPath, host.secondFloppyInserted,
                  host.ejectSecondFloppy, host.insertSecondFloppy, host.bootPath);
    if (host.hasExternalFloppyDrive)
        floppyRow("External floppy (SWIM)", "fdext", host.externalFloppyPath,
                  host.externalFloppyInserted, host.ejectExternalFloppy,
                  host.insertExternalFloppy, host.bootPath);
    // The SE and SE FDHD take a second internal mechanism; the ROM probes
    // it once, at boot, so fitting or removing it relaunches the machine.
    if (host.secondFloppyFittable && host.stageSecondFloppy) {
        bool fitted = host.hasSecondFloppyDrive;
        if (ImGui::Checkbox("Second internal drive (restarts machine)", &fitted))
            host.stageSecondFloppy(fitted);
        ImGui::Separator();
    }

    // ── The CD, deliberately shaped exactly like the floppy above ────────
    // MBunting a disc used to mean understanding the bus: find a free bay,
    // learn that an empty one needs a reboot, discover the « Réserver un
    // lecteur CD vide » checkbox at the bottom of the window, apply, restart,
    // and only then pick the .iso. Every machine that can hold a CD drive now
    // boots with one (`ensureCdDrive`, `DiskBays.h`), so this row is a picker
    // and an « Eject » button — the same two states the floppy has, and no
    // reboot in either direction. The bay list below still shows the same
    // drive, for anyone who wants to see which SCSI id it is.
    {
        int cdBay = -1;
        for (int i = 0; i < kMaxBays; i++)
            if (bayIsLive(host, i)) { cdBay = i; break; }
        if (cdBay >= 0) {
            const std::vector<std::string>& ex = activeExtras(host);
            const std::string cur =
                cdBay < int(ex.size()) ? ex[size_t(cdBay)] : std::string();
            const bool loaded = !cur.empty() && cur != kCdBayToken;
            ImGui::TextDisabled("CD-ROM (SCSI %d)", cdBay + 1);
            if (loaded) {
                ImGui::Text("%s", fileName(cur).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Eject##cd")) {
                    host.ejectBay(cdBay + 1);
                    if (host.extras && cdBay < int(host.extras->size()))
                        (*host.extras)[size_t(cdBay)] = kCdBayToken;
                    gLastError.clear();
                }
            } else {
                ImGui::SetNextItemWidth(-1);
                std::string cd;
                if (imageCombo("##cdpick", std::string(), host.bootPath, cd,
                               Only::Cd) && !cd.empty()) {
                    if (host.insertBay(cdBay + 1, cd)) {
                        if (host.extras && cdBay < int(host.extras->size()))
                            (*host.extras)[size_t(cdBay)] = cd;
                        gLastError.clear();
                    } else {
                        gLastError = "CD drive rejected the image.";
                    }
                }
            }
            ImGui::Separator();
        }
    }

    ImGui::TextDisabled("New hard disk");
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##newdiskname", "volume name",
                             gNewDiskName, sizeof gNewDiskName);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("##newdiskmb",
                          (std::to_string(gNewDiskMb) + " MB").c_str())) {
        for (int mb : {250, 500, 1000}) {
            if (ImGui::Selectable((std::to_string(mb) + " MB").c_str(),
                                  gNewDiskMb == mb))
                gNewDiskMb = mb;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        const int slot = firstEmptyDiskSlot(host);
        if (slot < 0) {
            gLastError = "No free SCSI bay for a hard disk.";
        } else {
            const std::string stem = sanitizeDiskStem(gNewDiskName);
            fs::path out = fs::path(workDiskDir()) / (stem + ".vhd");
            int n = 2;
            while (fs::exists(out)) {
                out = fs::path(workDiskDir()) /
                      (stem + "-" + std::to_string(n++) + ".vhd");
            }
            const std::string vol = gNewDiskName[0] ? std::string(gNewDiskName)
                                                    : stem;
            if (!hfsblank::writeFile(out.string(),
                    hfsblank::build(uint64_t(gNewDiskMb) << 20, vol))) {
                gLastError = "Could not write " + out.string();
            } else {
                gSessionImages.push_back(out.string());
                placeDisk(host, slot, out.string());
                gLastError.clear();
            }
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped(host.attachBay
        ? "Creates an empty HFS volume in hdv/work/ and attaches it to the bus "
          "immediately. Finder mounts fixed disks only at startup: "
          "use Restart machine, or mount it from the guest."
        : "Creates an empty HFS volume in hdv/work/ and attaches it to a "
          "SCSI bay. Finder sees it after Apply and restart.");
    ImGui::PopStyleColor();

    ImGui::Separator();

    // ── Secondary bays.
    ImGui::SeparatorText("Additional disks");

    const std::vector<std::string>& extras = activeExtras(host);
    for (int i = 0; i < kMaxBays; i++) {
        std::string cur = i < int(extras.size()) ? extras[i] : std::string();
        bool live = bayIsLive(host, i);

        ImGui::PushID(i);
        ImGui::Text(live ? "CD (%d)" : "Disk %d", i + 1);
        ImGui::SameLine(100);

        ImGui::SetNextItemWidth(-74);   // leave room for the button
        std::string pick;
        if (imageCombo("##img", cur, host.bootPath, pick,
                       live ? Only::Cd : Only::Disk)) {
            if (samePath(pick, boot) && !pick.empty()) {
                gLastError = "This volume is already the startup disk.";
            } else if (live && !pick.empty() && !diskBaysPathIsCd(pick)) {
                // A live bay is a CD drive; a hard-disk image in it would
                // mount garbage. Staging it wouldn't help either — tell.
                gLastError = "Bay " + std::to_string(i + 1)
                           + ": CD drive - a real CD image (.iso, 2048 bytes/sector) only.";
            } else if (live && host.insertBay && !pick.empty()) {
                // Occupied bay, hooks present: swap the medium on the spot.
                if (host.insertBay(i + 1, pick)) {
                    if (host.extras && i < int(host.extras->size()))
                        (*host.extras)[i] = pick;
                    gLastError.clear();
                } else {
                    gLastError = "Bay rejected the image: " + std::to_string(i + 1);
                }
            } else if (live && host.ejectBay && pick.empty()) {
                // The disc leaves; the DRIVE stays on the bus (that is what
                // makes the next insert hot too), so remember the token.
                host.ejectBay(i + 1);
                if (host.extras && i < int(host.extras->size()))
                    (*host.extras)[i] = kCdBayToken;
                gLastError.clear();
            } else if (!pick.empty() && cur.empty() && !gStaged) {
                // Empty bay: on the bus now when the machine can, staged
                // otherwise. The guest line below says what the System
                // made of it.
                placeDisk(host, i, pick);
            } else {
                // Occupied fixed bay, or a staged session: stage it.
                beginStaging(host);
                while (int(gStagedExtras.size()) <= i) gStagedExtras.emplace_back();
                gStagedExtras[i] = pick;
            }
        }

        if (!cur.empty() && cur != kCdBayToken) {
            ImGui::SameLine();
            if (live) {
                if (ImGui::SmallButton("Eject")) {
                    host.ejectBay(i + 1);
                    if (host.extras && i < int(host.extras->size()))
                        (*host.extras)[i] = kCdBayToken;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Ejects immediately without restart");
            } else if (canDetachNow(host, i + 1)) {
                // The guest holds no volume on this bay: the cable can
                // come out now, and the relaunch line forgets the disk.
                if (ImGui::SmallButton("Remove") && host.detachBay(i + 1)) {
                    if (host.extras && i < int(host.extras->size())) {
                        (*host.extras)[size_t(i)].clear();
                        while (!host.extras->empty() && host.extras->back().empty())
                            host.extras->pop_back();
                    }
                    gLastError.clear();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Detaches from the bus now - the guest has "
                                      "no volume mounted on this target");
            } else {
                if (ImGui::SmallButton("Remove")) {
                    beginStaging(host);
                    if (i < int(gStagedExtras.size())) gStagedExtras[i].clear();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(host.detachBay
                        ? "Takes effect at next restart - a volume "
                          "is mounted: use Unmount or Put Away in "
                          "Finder to remove it without restarting"
                        : "Takes effect at next restart");
            }
        }
        if (!cur.empty() && !live) {
            ImGui::Indent(100);
            guestBayLine(host, i + 1);
            // The guest agent, when it is polling: mount or unmount this
            // bay's volume from inside Mac OS (docs/SCSI_HOTPLUG.md § 3,
            // step 3). The button follows the guest's own view of the bay.
            if (host.agentPresent && host.agentPresent() && host.guestView) {
                const GuestScsiView v = host.guestView();
                const bool mounted = v.valid && v.bays[size_t(i + 1)].mounted;
                ImGui::SameLine();
                if (mounted) {
                    if (ImGui::SmallButton("Unmount") && host.agentUnmount)
                        host.agentUnmount(i + 1);
                } else if (ImGui::SmallButton("Mount") && host.agentMount) {
                    host.agentMount(i + 1);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Through the POM68K agent running inside the Mac");
            }
            ImGui::Unindent(100);
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Help: mounting and disk formats")) {
        // Why some rows swap live and others do not -- stated once, in place.
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped(host.attachBay
            ? "A disk selected here joins the bus immediately; Finder mounts "
              "fixed disks only at startup. Use Restart machine (the "
              "ROM scans the bus again) or mount from the guest (SCSIProbe). "
              "Remove detaches it immediately when no volume is mounted "
              "on it. "
              "The CD-ROM row accepts only real CDs (2048 bytes/sector). A "
              "Toast .toast dump is a disk image, not a CD."
            : "A hard disk appears on the desktop only at startup: "
              "select it here, then Apply and restart. The "
              "CD-ROM row accepts only real CDs (2048 bytes/sector). A Toast "
              ".toast dump is a disk image, not a CD.");
        ImGui::PopStyleColor();
    }
    if (host.bayMessage) {
        const std::string m = host.bayMessage();
        if (!m.empty()) ImGui::TextWrapped("%s", m.c_str());
    }
    if (host.agentPresent && host.agentReport) {
        const bool present = host.agentPresent();
        const ScsiAgentSnapshot rep = host.agentReport();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("Guest agent: %s", present
            ? (rep.pending ? "present, request pending" : "present")
            : "absent (run POM68K Disques inside the Mac)");
        ImGui::PopStyleColor();
        if (rep.reported)
            ImGui::TextWrapped("Agent: %s SCSI %d -> %s%s%s (err %d)",
                               rep.lastKind == 1 ? "mount" : "unmount",
                               rep.lastId, rep.lastText.empty() ? "" : "'",
                               rep.lastText.c_str(), rep.lastText.empty() ? "" : "'",
                               rep.lastErr);
    }
    if (host.supportsEmptyCdDrive) {
        ImGui::Checkbox("Reserve an empty CD drive at startup",
                        &gReserveEmpty);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adds an empty CD drive to the bus at the\n"
                              "next Apply and restart. The System probes it\n"
                              "as a real drive: inserting an .iso/.toast later\n"
                              "mounts live, without restarting.\n"
                              "If a CD drive is already connected, no extra\n"
                              "drive is added.");
        if (gReserveEmpty && !gStaged) beginStaging(host);
    } else {
        gReserveEmpty = false;
    }

    ImGui::Separator();
    if (!gLastError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", gLastError.c_str());

    ImGui::End();
}

} // namespace pom68k
