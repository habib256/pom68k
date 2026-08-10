// DiskBays -- the one "Disques" window. See DiskBays.h for the contract.

#include "DiskBays.h"
#include "DockLayout.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
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
char  gPathEntry[512] = {0};
std::string gLastError;

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

bool isCd(const std::string& p) {
    return endsWithNoCase(p, ".iso")  || endsWithNoCase(p, ".cdr")
        || endsWithNoCase(p, ".toast") || endsWithNoCase(p, ".cue")
        || endsWithNoCase(p, ".bin");
}

// Everything the command line accepts. The old menu listed only .vhd/.hda/
// .img, which is why a bootable `System 7.1 HD.dsk` never showed up in it.
bool isDiskImage(const std::string& p) {
    return endsWithNoCase(p, ".vhd") || endsWithNoCase(p, ".hda")
        || endsWithNoCase(p, ".img") || endsWithNoCase(p, ".dsk")
        || endsWithNoCase(p, ".image")
        || isCd(p);
}

bool samePath(const std::string& a, const std::string& b) {
    if (a == b) return true;
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    return fs::equivalent(a, b, ec);
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
        std::snprintf(buf, sizeof buf, "%.1f Go", double(n) / (1024.0 * 1024 * 1024));
    else if (n >= 1024ull * 1024)
        std::snprintf(buf, sizeof buf, "%.0f Mo", double(n) / (1024.0 * 1024));
    else
        std::snprintf(buf, sizeof buf, "%.0f Ko", double(n) / 1024.0);
    return buf;
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

// A SuperDrive holds 400 K, 800 K or 1.44 MB. Anything larger in the list is
// a hard-disk image and has no business being offered to the floppy bay.
// Largest supported DC42: 84-byte header + 1.44 MiB data + 12 tag bytes for
// each of 2880 sectors. Tagged DiskCopy images are larger than raw media.
constexpr unsigned long long kMaxFloppyBytes = 84ull + 1474560ull + 2880ull * 12ull;

bool looksLikeFloppy(const std::string& p) {
    if (isCd(p)) return false;
    if (!endsWithNoCase(p, ".dsk") && !endsWithNoCase(p, ".img")
        && !endsWithNoCase(p, ".image")) return false;
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    return !ec && n > 0 && n <= kMaxFloppyBytes;
}

// ── Image picker ───────────────────────────────────────────────────────────
// Returns true and fills `chosen` when the user picked something this frame.
bool imageCombo(const char* label, const std::string& current,
                const std::string& nearPath, std::string& chosen,
                bool floppyOnly = false) {
    bool picked = false;
    std::string preview = current.empty()
                              ? std::string("<vide>")
                              : current == kCdBayToken
                              ? std::string("<lecteur CD vide>")
                              : fileName(current) + "  " + sizeLabel(current);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("<vide>", current.empty())) {
            chosen.clear();
            picked = true;
        }
        for (const std::string& d : diskBaysKnownImages(nearPath)) {
            if (floppyOnly && !looksLikeFloppy(d)) continue;
            bool sel = samePath(d, current);
            std::string item = fileName(d) + "   " + sizeLabel(d);
            if (isCd(d)) item += "   CD";
            if (ImGui::Selectable(item.c_str(), sel)) {
                chosen = d;
                picked = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", d.c_str());
        }
        ImGui::EndCombo();
    }
    return picked;
}

} // namespace

// ── Discovery ──────────────────────────────────────────────────────────────

std::vector<std::string> diskBaysKnownImages(const std::string& nearPath) {
    std::vector<std::string> out;
    scanInto(probeDir("hdv"), out);
    scanInto(probeDir("disks35"), out);
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

// ── Drag and drop ──────────────────────────────────────────────────────────

void diskBaysInstallDrop(GLFWwindow* window) {
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; i++) {
            std::string p = paths[i];
            if (!isDiskImage(p)) continue;
            bool known = false;
            for (const std::string& s : gSessionImages)
                if (samePath(s, p)) { known = true; break; }
            if (!known) gSessionImages.push_back(p);
            gOpen = true;           // dropping an image is a request to use it
        }
    });
}

// ── Menu entry ─────────────────────────────────────────────────────────────

void diskBaysMenuItem() {
    if (ImGui::MenuItem("Disques...", nullptr, gOpen))
        gOpen = !gOpen;
}

// ── The window ─────────────────────────────────────────────────────────────

void diskBaysWindow(DiskBaysHost& host) {
    if (!gOpen) return;

    if (!ImGui::Begin(kDiskWindowTitle, &gOpen)) {
        ImGui::End();
        return;
    }

    const std::string& boot = gStaged ? gStagedBoot : host.bootPath;

    // ── Floppy first: it is the one bay that always swaps live, so it sits
    //    above the cold (reboot-requiring) hard-disk choices.
    if (host.hasFloppyDrive) {
        ImGui::TextDisabled("Disquette (SWIM)");
        bool in = host.floppyInserted && host.floppyInserted();
        if (in) {
            ImGui::Text("%s", !host.floppyPath.empty()
                                  ? fileName(host.floppyPath).c_str()
                                  : "<insérée>");
            ImGui::SameLine();
            if (ImGui::SmallButton("Éjecter##fd") && host.ejectFloppy)
                host.ejectFloppy();
        } else {
            ImGui::SetNextItemWidth(-1);
            std::string fd;
            if (imageCombo("##fdpick", std::string(), host.bootPath, fd, true)
                && !fd.empty() && host.insertFloppy)
                host.insertFloppy(fd);
        }
        ImGui::Separator();
    }

    // ── Boot disk. Never hot-swappable: the .pram file follows the boot
    //    volume and the ROM picks its startup device once, at power-on.
    ImGui::TextDisabled("Démarrage (SCSI 0)");
    ImGui::SetNextItemWidth(-160);
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
    ImGui::SameLine();
    ImGui::TextDisabled("redémarrage requis");

    ImGui::Separator();

    // ── Secondary bays.
    ImGui::TextDisabled("Baies secondaires (SCSI 1-%d)", kMaxBays);

    const std::vector<std::string>& extras = activeExtras(host);
    for (int i = 0; i < kMaxBays; i++) {
        std::string cur = i < int(extras.size()) ? extras[i] : std::string();
        bool live = bayIsLive(host, i);

        ImGui::PushID(i);
        ImGui::Text("SCSI %d", i + 1);
        ImGui::SameLine(80);

        ImGui::SetNextItemWidth(-74);   // leave room for the button
        std::string pick;
        if (imageCombo("##img", cur, host.bootPath, pick)) {
            if (samePath(pick, boot) && !pick.empty()) {
                gLastError = "Ce volume est déjà le disque de démarrage.";
            } else if (live && !pick.empty() && !diskBaysPathIsCd(pick)) {
                // A live bay is a CD drive; a hard-disk image in it would
                // mount garbage. Staging it wouldn't help either — tell.
                gLastError = "Baie " + std::to_string(i + 1)
                           + ": lecteur CD — image .iso/.toast/.cdr attendue.";
            } else if (live && host.insertBay && !pick.empty()) {
                // Occupied bay, hooks present: swap the medium on the spot.
                if (host.insertBay(i + 1, pick)) {
                    if (host.extras && i < int(host.extras->size()))
                        (*host.extras)[i] = pick;
                    gLastError.clear();
                } else {
                    gLastError = "Image refusée par la baie " + std::to_string(i + 1);
                }
            } else if (live && host.ejectBay && pick.empty()) {
                // The disc leaves; the DRIVE stays on the bus (that is what
                // makes the next insert hot too), so remember the token.
                host.ejectBay(i + 1);
                if (host.extras && i < int(host.extras->size()))
                    (*host.extras)[i] = kCdBayToken;
                gLastError.clear();
            } else {
                // Empty bay, or no hot-swap on this machine: stage it.
                beginStaging(host);
                while (int(gStagedExtras.size()) <= i) gStagedExtras.emplace_back();
                gStagedExtras[i] = pick;
            }
        }

        if (!cur.empty() && cur != kCdBayToken) {
            ImGui::SameLine();
            if (live) {
                if (ImGui::SmallButton("Éjecter")) {
                    host.ejectBay(i + 1);
                    if (host.extras && i < int(host.extras->size()))
                        (*host.extras)[i] = kCdBayToken;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Éjection immédiate, sans redémarrage");
            } else {
                if (ImGui::SmallButton("Retirer")) {
                    beginStaging(host);
                    if (i < int(gStagedExtras.size())) gStagedExtras[i].clear();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Prendra effet au prochain redémarrage");
            }
        }
        ImGui::PopID();
    }

    // Why some rows swap live and others do not -- stated once, in place.
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Une baie occupée au démarrage s'échange à chaud. "
                       "Une baie vide au démarrage demande un redémarrage: "
                       "le ROM ne sonde le bus SCSI qu'une fois, au boot.");
    ImGui::PopStyleColor();
    if (host.supportsEmptyCdDrive) {
        ImGui::Checkbox("Réserver un lecteur CD vide au démarrage",
                        &gReserveEmpty);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ajoute un lecteur CD sans disque sur le bus au\n"
                              "prochain « Appliquer et redémarrer ». Le System le\n"
                              "sonde comme un vrai lecteur: un .iso/.toast inséré\n"
                              "ensuite monte à chaud, sans redémarrage.\n"
                              "Désactivé par défaut: cela modifie le bus, et les\n"
                              "gates de boot sont chronométrés sur le bus actuel.");
        if (gReserveEmpty && !gStaged) beginStaging(host);
    } else {
        gReserveEmpty = false;
    }

    // ── Add an image the scan cannot see.
    ImGui::Separator();
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##path", "chemin d'une image a ajouter",
                             gPathEntry, sizeof gPathEntry);
    ImGui::SameLine();
    if (ImGui::Button("Ajouter")) {
        std::string p = gPathEntry;
        std::error_code ec;
        if (p.empty()) {
            gLastError = "Chemin vide.";
        } else if (!fs::is_regular_file(p, ec)) {
            gLastError = "Introuvable: " + p;
        } else if (!isDiskImage(p)) {
            gLastError = "Extension non reconnue: " + p;
        } else {
            gSessionImages.push_back(p);
            gPathEntry[0] = '\0';
            gLastError.clear();
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("ou glissez-déposez une image sur la fenêtre");
    ImGui::PopStyleColor();

    if (!gLastError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", gLastError.c_str());

    // ── Footer: nothing here reboots without being asked to.
    ImGui::Separator();
    if (gStaged) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "Modifications en attente");
        if (ImGui::Button("Appliquer et redémarrer") && host.relaunch) {
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
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) discardStaging();
        ImGui::SameLine();
    }
    if (ImGui::Button("Redémarrer la machine") && host.hardReset)
        host.hardReset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Power cycle: le ROM re-sonde le bus SCSI,\n"
                          "les médias branchés à chaud apparaissent.");

    ImGui::End();
}

} // namespace pom68k
