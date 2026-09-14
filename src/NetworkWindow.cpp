// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ─────────────────────────────────────────────────────────────────────────────
// NetworkWindow -- the one "AppleTalk / Ethernet" window (menu Réseau), on
// every machine. Out of GuiShell.cpp since 2026-09-13, when the DaynaPort
// selector took it past the shell's size ceiling: the window is one concern
// (render the hub's snapshot, toggle its services, stage the card) and the
// shell another (menus, docking, the machine window).
//
// Two kinds of control live here, and the window keeps them apart:
//   • LIVE — the AppleTalk services and the card's cable. Each goes through
//     `AtalkHub::setService`, hub-owned, applied on the machine thread; no
//     GUI path dereferences the machine (AtalkHub.h).
//   • STAGED + RELAUNCH — the card's presence and SCSI ID. A Mac probes its
//     bus once, at boot (DiskBays.h), so the choice is serialized as
//     `--daynaport=<id>` on the relaunch line (RuntimeConfig.h) and applied
//     by an explicit button, the Disques / Périphériques contract.
// Everything displayed is `AtalkHub::snapshot()`, a copy taken under the
// hub's lock — the window never reads a device.
// ─────────────────────────────────────────────────────────────────────────────

#include "NetworkWindow.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace pom68k::gui {

void statusDot(bool ok, const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                         ok ? ImVec4(0.3f, 0.85f, 0.35f, 1)
                            : ImVec4(0.9f, 0.4f, 0.35f, 1));
    ImGui::Bullet();
    ImGui::PopStyleColor();
    ImGui::TextUnformatted(label);
}

namespace {

// Open a host folder in the desktop's file manager — the "reveal the spool"
// the backlog asked for, and the shared folder beside it. Fire-and-forget:
// the launcher's own exit status says nothing about the folder.
void revealFolder(const std::string& path) {
    std::string quoted = "'";
    for (const char c : path) quoted += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
#if defined(_WIN32)
    const std::string command = "explorer " + quoted;
#elif defined(__APPLE__)
    const std::string command = "open " + quoted + " >/dev/null 2>&1 &";
#else
    const std::string command = "xdg-open " + quoted + " >/dev/null 2>&1 &";
#endif
    // GCC's warn_unused_result on system() survives a (void) cast; the
    // desktop opener reports its own failures, so the status is read and
    // deliberately dropped.
    const int status = std::system(command.c_str());
    if (status != 0) return;
}

// ── The services' identity, edited LIVE ──
// Names, folders and addresses go through AtalkHub::reconfigure, which
// restarts each service (the AFP sessions drop, an open print job closes,
// MacIP leases are retired) and re-registers the NBP names. The buffers
// below are the form; they follow the hub until the user starts typing, and
// « Appliquer » sends the whole form back. The relaunch line carries the
// result (`--atalk-<key>=`), so the edit survives a disk swap.
struct ServiceForm {
    bool editing = false;
    char server[64] = "", volume[64] = "", share[512] = "";
    char printer[64] = "", spool[512] = "", gateway[32] = "", dns[24] = "";
    std::string error;

    void load(const AtalkHub::Config& cfg) {
        std::snprintf(server, sizeof server, "%s", cfg.serverName.c_str());
        std::snprintf(volume, sizeof volume, "%s", cfg.volName.c_str());
        std::snprintf(share, sizeof share, "%s", cfg.shareDir.c_str());
        std::snprintf(printer, sizeof printer, "%s", cfg.printerName.c_str());
        std::snprintf(spool, sizeof spool, "%s", cfg.spoolDir.c_str());
        std::snprintf(gateway, sizeof gateway, "%s",
                      AtalkHub::formatCidr(cfg.gwIp, cfg.gwMask).c_str());
        std::snprintf(dns, sizeof dns, "%s", AtalkHub::formatIpv4(cfg.dns).c_str());
    }
    // The form as a Config, or an explanation of what does not parse.
    bool build(AtalkHub::Config& out) {
        error.clear();
        out.serverName = server;
        out.volName = volume;
        out.shareDir = share;
        out.printerName = printer;
        out.spoolDir = spool;
        if (!AtalkHub::parseCidr(gateway, out.gwIp, out.gwMask))
            error = "Passerelle : attendu a.b.c.d/n (n de 1 à 30)";
        else if (!AtalkHub::parseIpv4(dns, out.dns))
            error = "DNS : attendu a.b.c.d";
        else if (out.shareDir.empty())
            error = "Dossier partagé : vide";
        return error.empty();
    }
};
ServiceForm gForm;

void drawServiceConfiguration(GuiNetworkState& state,
                              const AtalkHub::Snapshot& snapshot) {
    ImGui::SeparatorText("Configuration des services");
    if (!gForm.editing) gForm.load(snapshot.cfg);
    ImGui::PushItemWidth(260);
    bool touched = false;
    touched |= ImGui::InputText("Serveur AFP (nom NBP)", gForm.server, sizeof gForm.server);
    touched |= ImGui::InputTextWithHint("Volume", "vide = nom du dossier partagé",
                                        gForm.volume, sizeof gForm.volume);
    touched |= ImGui::InputText("Dossier partagé", gForm.share, sizeof gForm.share);
    ImGui::SameLine();
    if (ImGui::SmallButton("Révéler##share")) revealFolder(snapshot.cfg.shareDir);
    touched |= ImGui::InputText("Imprimante (nom NBP)", gForm.printer, sizeof gForm.printer);
    touched |= ImGui::InputText("Dossier de spool", gForm.spool, sizeof gForm.spool);
    ImGui::SameLine();
    if (ImGui::SmallButton("Révéler##spool")) revealFolder(snapshot.cfg.spoolDir);
    touched |= ImGui::InputText("Passerelle MacIP (a.b.c.d/n)", gForm.gateway,
                                sizeof gForm.gateway);
    touched |= ImGui::InputText("DNS", gForm.dns, sizeof gForm.dns);
    ImGui::PopItemWidth();
    if (touched) gForm.editing = true;
    if (!gForm.editing) {
        ImGui::TextDisabled("Appliqué à chaud ; les sessions AFP, le travail "
                            "d'impression en cours et les baux MacIP sont coupés.");
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                       "Modifications en attente — appliquées à chaud, ce qui "
                       "coupe les sessions AFP, le travail d'impression en "
                       "cours et les baux MacIP.");
    if (ImGui::Button("Appliquer")) {
        AtalkHub::Config next = snapshot.cfg;
        if (gForm.build(next)) {
            state.atalk.reconfigure(next);
            gForm.editing = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Annuler")) {
        gForm.editing = false;
        gForm.error.clear();
    }
    if (!gForm.error.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1), "%s", gForm.error.c_str());
}

// The card for the NEXT boot: which bus the relaunched machine gets. Staged
// here and applied by an explicit relaunch, exactly like the Disques and
// Périphériques windows, because a Mac probes its SCSI bus once, at boot
// (DiskBays.h): a live "add the card" control would be a lie in the UI. The
// IDs a disk already holds are greyed out — attachDaynaPort would let the
// card displace the disk, aloud, and the window should not offer that.
// nullopt = nothing staged; the inner nullopt = "no card".
std::optional<std::optional<int>> gStagedDayna;

void daynaChoiceLabel(std::optional<int> choice, char* out, std::size_t size) {
    if (choice) std::snprintf(out, size, "ID SCSI %d", *choice);
    else std::snprintf(out, size, "Aucune carte");
}

void drawDaynaPortSelector(GuiNetworkState& state) {
    const std::optional<int> current =
        state.ethernetScsiId >= 0 ? std::optional<int>(state.ethernetScsiId)
                                  : std::nullopt;
    const std::optional<int> choice = gStagedDayna ? *gStagedDayna : current;
    char preview[32];
    daynaChoiceLabel(choice, preview, sizeof preview);
    if (ImGui::BeginCombo("Carte au prochain démarrage", preview)) {
        if (ImGui::Selectable("Aucune carte", !choice))
            gStagedDayna = std::optional<int>();
        for (int id = 2; id <= 6; ++id) {
            const bool held = state.scsiOccupied & (1u << id);
            char item[48];
            std::snprintf(item, sizeof item, "ID SCSI %d%s", id,
                          held ? "  (occupé par un disque)" : "");
            ImGui::BeginDisabled(held);
            if (ImGui::Selectable(item, choice == id))
                gStagedDayna = std::optional<int>(id);
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (!gStagedDayna) return;
    if (*gStagedDayna == current) {      // back where the bus already is
        gStagedDayna.reset();
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                       "Modification en attente — le Mac ne sonde le bus "
                       "qu'au démarrage");
    ImGui::BeginDisabled(!state.relaunchWithDaynaPort);
    if (ImGui::Button("Appliquer et redémarrer")) {
        const std::optional<int> staged = *gStagedDayna;
        gStagedDayna.reset();
        state.relaunchWithDaynaPort(staged);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Annuler")) gStagedDayna.reset();
}

// The DaynaPort SCSI/Link's own line. Everything displayed here is the
// machine-thread sample the hub took in tick() (AtalkHub::DaynaMeter): no GUI
// path dereferences the card. Two things are deliberately NOT offered live:
//   • the guest's ENABLE INTERFACE bit is shown, never written — it belongs
//     to the guest's driver and travels in save states (DaynaPort.h);
//   • presence and SCSI ID change only staged + relaunch (the selector
//     above) — a Mac probes its SCSI bus once, at boot (DiskBays.h), so a
//     live "remove the card" button would be a lie in the UI, the same
//     reason the Périphériques window stages and relaunches.
// What the host really owns live is the CABLE, below.
void drawEthernetSection(GuiNetworkState& state,
                         const AtalkHub::Snapshot& snapshot) {
    const AtalkHub::DaynaMeter& card = snapshot.ether;
    ImGui::SeparatorText("Ethernet (carte DaynaPort SCSI/Link)");
    if (!card.present) {
        ImGui::TextDisabled("Aucune carte sur le bus SCSI.");
        drawDaynaPortSelector(state);
        return;
    }
    char line[96];
    if (state.ethernetScsiId >= 0)
        std::snprintf(line, sizeof line, "Carte présente, ID SCSI %d",
                      state.ethernetScsiId);
    else
        std::strcpy(line, "Carte présente sur le bus SCSI");
    statusDot(true, line);
    statusDot(card.enabled,
              card.enabled ? "Pilote invité : interface activée"
                           : "Pilote invité : interface désactivée");
    bool cable = snapshot.cfg.ethernetCable;
    if (ImGui::Checkbox("Câble réseau branché", &cable))
        state.atalk.setService("ethernet", cable);
    ImGui::TextDisabled(
        "Débranché, la carte reste sur le bus et ne porte plus rien.");
    ImGui::Text("Invité → réseau : %ld trames (%ld o)   ·   réseau → invité : "
                "%ld trames (%ld o)",
                card.framesFromGuest, card.bytesFromGuest,
                card.framesToGuest, card.bytesToGuest);
    ImGui::Text("Commandes SCSI servies : %ld   ·   en attente de lecture : %zu",
                card.commands, card.queued);
    if (card.framesDropped)
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1),
                           "Trames perdues (anneau plein) : %ld",
                           card.framesDropped);
    drawDaynaPortSelector(state);
}

} // namespace

const char* kNetworkWindowTitle = "AppleTalk / Ethernet";

void drawAppleTalkWindow(GuiNetworkState& state) {
    if (!state.showWindow) return;
    const AtalkHub::Snapshot snapshot = state.atalk.snapshot();
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kNetworkWindowTitle, &state.showWindow,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!snapshot.attached || !state.appleTalkEnabled) {
        ImGui::TextUnformatted(
            "Pile AppleTalk interne désactivée (POM68K_APPLETALK=0).");
        // The card is on the SCSI bus regardless of AppleTalk, and carries
        // IPv4 through the NAT without it (DaynaPortBus.h). With no hub
        // attached the meter reads "no card", and the selector still stages
        // one for the next boot.
        drawEthernetSection(state, snapshot);
        ImGui::End();
        return;
    }

    bool stackOn = snapshot.cfg.stack;
    if (ImGui::Checkbox("Réseau AppleTalk actif", &stackOn))
        state.atalk.setService("stack", stackOn);
    ImGui::SameLine();
    ImGui::TextDisabled(snapshot.cableUp ? "(câble LToUDP: relié)"
                                        : "(câble LToUDP: local)");

    ImGui::SeparatorText("Noeud / routeur");
    char guest[16];
    if (snapshot.net.guestNode)
        std::snprintf(guest, sizeof guest, "%u", snapshot.net.guestNode);
    else
        std::strcpy(guest, "aucun");
    char routerLine[80];
    std::snprintf(routerLine, sizeof routerLine,
                  "Reseau 2, noeud serveur %u, zone \"%s\"", snapshot.node,
                  snapshot.zone.c_str());
    statusDot(snapshot.cfg.stack, routerLine);
    ImGui::Text("Invite vu : %s   -   trames recues %ld / emises %ld", guest,
                snapshot.net.framesIn, snapshot.net.framesOut);
    ImGui::Text("Recherches NBP servies : %ld   -   transactions ATP : %ld",
                snapshot.net.nbpLookups, snapshot.net.atpReqIn);
    if (snapshot.net.atpDupReqs || snapshot.net.atpDupPending) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.75f, 0.3f, 1),
            "Retransmissions client : %ld  (dernier retard %ld ms, max %ld ms)",
            snapshot.net.atpDupReqs, snapshot.net.atpDupLagLastMs,
            snapshot.net.atpDupLagMaxMs);
        if (snapshot.net.atpDupPending)
            ImGui::TextDisabled(
                "  dont %ld pendant le service (serveur lent, pas le fil)",
                snapshot.net.atpDupPending);
        ImGui::TextDisabled(
            "  file d'injection %zu (max %zu)  -  attente max %ld ms  -  "
            "POM68K_ATALK_DEBUG=1 pour le detail",
            snapshot.wire.backlog, snapshot.wire.backlogMax,
            snapshot.wireHoldMaxMs);
    } else {
        ImGui::TextDisabled("Retransmissions client : 0 (fil sans perte)");
    }
    if (snapshot.wire.drops)
        ImGui::TextColored(
            ImVec4(0.95f, 0.5f, 0.35f, 1),
            "Debordement du fil : %ld trames  (l'invite a cesse d'ecouter "
            "assez longtemps pour saturer la file)",
            snapshot.wire.drops);

    ImGui::SeparatorText("Partage de fichiers (AppleShare / AFP)");
    bool afpOn = snapshot.cfg.afp;
    if (ImGui::Checkbox("Activer AppleShare", &afpOn))
        state.atalk.setService("afp", afpOn);
    statusDot(snapshot.afp.registered,
              "Visible dans le Sélecteur (NBP AFPServer)");
    statusDot(snapshot.afp.dirOk && snapshot.afp.catalogError.empty(),
              !snapshot.afp.catalogError.empty() ? snapshot.afp.catalogError.c_str() :
              snapshot.afp.dirOk ? "Dossier partagé accessible en écriture"
                                 : "Dossier partagé INTROUVABLE / lecture seule");
    ImGui::Text("Nom serveur : %s", snapshot.afp.serverName.c_str());
    ImGui::Text("Volume : %s", snapshot.afp.volName.c_str());
    ImGui::TextWrapped("Dossier hôte : %s",
                       snapshot.afp.dirPath.empty()
                           ? "(non défini)"
                           : snapshot.afp.dirPath.c_str());
    ImGui::Text("Sessions : %d%s   ·   utilisateur : %s",
                snapshot.afp.sessions,
                snapshot.afp.volMounted ? " (volume monté)" : "",
                snapshot.afp.lastUser.empty() ? "-"
                                              : snapshot.afp.lastUser.c_str());
    ImGui::Text("Dernière commande : %s   ·   lu %ld o / écrit %ld o",
                snapshot.afp.lastCmd.empty() ? "-"
                                             : snapshot.afp.lastCmd.c_str(),
                snapshot.afp.bytesRead, snapshot.afp.bytesWritten);

    ImGui::SeparatorText("Imprimante (LaserWriter / PAP)");
    bool papOn = snapshot.cfg.pap;
    if (ImGui::Checkbox("Activer l'imprimante", &papOn))
        state.atalk.setService("pap", papOn);
    statusDot(snapshot.pap.registered,
              "Visible dans le Sélecteur (NBP LaserWriter)");
    ImGui::Text("Nom : %s", snapshot.pap.printerName.c_str());
    ImGui::Text("État : %s%s", snapshot.pap.state.c_str(),
                snapshot.pap.busy ? "  (occupée)" : "");
    ImGui::Text("Travaux imprimés : %ld   ·   dernier : %s",
                snapshot.pap.jobs,
                snapshot.pap.lastJob.empty() ? "-"
                                             : snapshot.pap.lastJob.c_str());
    ImGui::TextDisabled("Spool → CUPS (lp) si présent, sinon %s/",
                        snapshot.pap.spoolDir.c_str());

    ImGui::SeparatorText("Internet (MacIP / IP-in-DDP)");
    bool ipOn = snapshot.cfg.macip;
    if (ImGui::Checkbox("Activer la passerelle MacIP", &ipOn))
        state.atalk.setService("macip", ipOn);
    statusDot(snapshot.macip.registered,
              "Passerelle visible (NBP IPGATEWAY)");
    const bool ipWorks = snapshot.macip.registered && snapshot.macip.leases > 0;
    statusDot(ipWorks, ipWorks ? "MacIP fonctionne (bail attribué)"
                               : "MacIP en attente (aucun invité connecté)");
    ImGui::Text("Passerelle : %s   ·   DNS : %s",
                snapshot.macip.gwIp.c_str(), snapshot.macip.dns.c_str());
    ImGui::Text("Baux : %d   ·   dernier : %s   ·   flux UDP %d / TCP %d",
                snapshot.macip.leases,
                snapshot.macip.lastLease.empty()
                    ? "-"
                    : snapshot.macip.lastLease.c_str(),
                snapshot.macip.udpFlows, snapshot.macip.tcpConns);
    ImGui::Text("IP invite -> net %ld   -   net -> invite %ld",
                snapshot.macip.ipFromGuest, snapshot.macip.ipToGuest);
    ImGui::TextDisabled("HTTP uniquement (TLS 2026 hors d'atteinte) — "
                        "frogfind.com, theoldnet.com");

    drawServiceConfiguration(state, snapshot);
    drawEthernetSection(state, snapshot);
    ImGui::End();
}

} // namespace pom68k::gui
