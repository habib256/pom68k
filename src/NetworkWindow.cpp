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
//   • LIVE — the AppleTalk services, the printer's destination and the
//     card's cable. Each goes through `AtalkHub::setService` or
//     `setPrintDestination`, hub-owned; no GUI path dereferences the
//     machine (AtalkHub.h).
//   • STAGED + RELAUNCH — the card's presence and SCSI ID. A Mac probes its
//     bus once, at boot (DiskBays.h), so the choice is serialized as
//     `--daynaport=<id>` on the relaunch line (RuntimeConfig.h) and applied
//     by an explicit button, the Disques / Peripherals contract.
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
// « Apply » sends the whole form back. The relaunch line carries the
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
            error = "Gateway: expected a.b.c.d/n (n from 1 to 30)";
        else if (!AtalkHub::parseIpv4(dns, out.dns))
            error = "DNS: expected a.b.c.d";
        else if (out.shareDir.empty())
            error = "Shared folder: empty";
        return error.empty();
    }
};
ServiceForm gForm;

void drawServiceConfiguration(GuiNetworkState& state,
                              const AtalkHub::Snapshot& snapshot) {
    ImGui::SeparatorText("Configure sharing");
    if (!gForm.editing) gForm.load(snapshot.cfg);
    ImGui::PushItemWidth(260);
    bool touched = false;
    touched |= ImGui::InputText("Server name", gForm.server, sizeof gForm.server);
    touched |= ImGui::InputTextWithHint("Volume", "empty = shared folder name",
                                        gForm.volume, sizeof gForm.volume);
    touched |= ImGui::InputText("Shared folder", gForm.share, sizeof gForm.share);
    ImGui::SameLine();
    if (ImGui::SmallButton("Show folder##share")) revealFolder(snapshot.cfg.shareDir);
    if (ImGui::CollapsingHeader("Printer and MacIP settings")) {
        touched |= ImGui::InputText("Printer (NBP name)", gForm.printer, sizeof gForm.printer);
        touched |= ImGui::InputText("Spool folder", gForm.spool, sizeof gForm.spool);
        ImGui::SameLine();
        if (ImGui::SmallButton("Show folder##spool")) revealFolder(snapshot.cfg.spoolDir);
        touched |= ImGui::InputText("MacIP gateway (a.b.c.d/n)", gForm.gateway,
                                    sizeof gForm.gateway);
        touched |= ImGui::InputText("DNS", gForm.dns, sizeof gForm.dns);
    }
    ImGui::PopItemWidth();
    if (touched) gForm.editing = true;
    if (!gForm.editing) {
        ImGui::TextWrapped("Active: these settings belong to the current session.");
        return;
    }
    ImGui::TextWrapped("Pending changes: Apply reconnects services and "
                       "interrupts ongoing transfers, print jobs, and "
                       "MacIP connections. The Mac does not restart.");
    if (ImGui::Button("Apply")) {
        AtalkHub::Config next = snapshot.cfg;
        if (gForm.build(next)) {
            state.atalk.reconfigure(next);
            gForm.editing = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        gForm.editing = false;
        gForm.error.clear();
    }
    if (!gForm.error.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1), "%s", gForm.error.c_str());
}

// The card for the NEXT boot: which bus the relaunched machine gets. Staged
// here and applied by an explicit relaunch, exactly like the Disques and
// Peripherals windows, because a Mac probes its SCSI bus once, at boot
// (DiskBays.h): a live "add the card" control would be a lie in the UI. The
// IDs a disk already holds are greyed out — attachDaynaPort would let the
// card displace the disk, aloud, and the window should not offer that.
// nullopt = nothing staged; the inner nullopt = "no card".
std::optional<std::optional<int>> gStagedDayna;

void daynaChoiceLabel(std::optional<int> choice, char* out, std::size_t size) {
    if (choice) std::snprintf(out, size, "ID SCSI %d", *choice);
    else std::snprintf(out, size, "No card");
}

void drawDaynaPortSelector(GuiNetworkState& state) {
    const std::optional<int> current =
        state.ethernetScsiId >= 0 ? std::optional<int>(state.ethernetScsiId)
                                  : std::nullopt;
    const std::optional<int> choice = gStagedDayna ? *gStagedDayna : current;
    char preview[32];
    daynaChoiceLabel(choice, preview, sizeof preview);
    if (ImGui::BeginCombo("Card at next startup", preview)) {
        if (ImGui::Selectable("No card", !choice))
            gStagedDayna = std::optional<int>();
        for (int id = 2; id <= 6; ++id) {
            const bool held = state.scsiOccupied & (1u << id);
            char item[48];
            std::snprintf(item, sizeof item, "ID SCSI %d%s", id,
                          held ? "  (occupied by a disk)" : "");
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
                       "Pending restart: network card");
    ImGui::BeginDisabled(!state.relaunchWithDaynaPort);
    if (ImGui::Button("Apply and restart")) {
        const std::optional<int> staged = *gStagedDayna;
        gStagedDayna.reset();
        state.relaunchWithDaynaPort(staged);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) gStagedDayna.reset();
}

// The DaynaPort SCSI/Link's own line. Everything displayed here is the
// machine-thread sample the hub took in tick() (AtalkHub::DaynaMeter): no GUI
// path dereferences the card. Two things are deliberately NOT offered live:
//   • the guest's ENABLE INTERFACE bit is shown, never written — it belongs
//     to the guest's driver and travels in save states (DaynaPort.h);
//   • presence and SCSI ID change only staged + relaunch (the selector
//     above) — a Mac probes its SCSI bus once, at boot (DiskBays.h), so a
//     live "remove the card" button would be a lie in the UI, the same
//     reason the Peripherals window stages and relaunches.
// What the host really owns live is the CABLE, below.
void drawEthernetSection(GuiNetworkState& state,
                         const AtalkHub::Snapshot& snapshot) {
    const AtalkHub::DaynaMeter& card = snapshot.ether;
    ImGui::SeparatorText("Ethernet (DaynaPort SCSI/Link card)");
    if (!card.present) {
        ImGui::TextDisabled("No card on the SCSI bus.");
        drawDaynaPortSelector(state);
        return;
    }
    char line[96];
    if (state.ethernetScsiId >= 0)
        std::snprintf(line, sizeof line, "Card present, SCSI ID %d",
                      state.ethernetScsiId);
    else
        std::strcpy(line, "Card present on the SCSI bus");
    statusDot(true, line);
    statusDot(card.enabled,
              card.enabled ? "Guest driver: interface enabled"
                           : "Guest driver: interface disabled");
    bool cable = snapshot.cfg.ethernetCable;
    if (ImGui::Checkbox("Network cable connected", &cable))
        state.atalk.setService("ethernet", cable);
    ImGui::TextDisabled(
        "Disconnected, the card stays on the bus but carries no traffic.");
    bool etalk = snapshot.cfg.ethertalk;
    if (ImGui::Checkbox("AppleTalk on card (EtherTalk)", &etalk))
        state.atalk.setService("ethertalk", etalk);
    ImGui::TextDisabled(
        "POM68K also routes Ethernet traffic: a guest that "
        "selects EtherTalk joins its zone.");
    if (ImGui::CollapsingHeader("Ethernet diagnostics and PCAP capture") ||
        snapshot.capture.busy || !snapshot.capture.error.empty()) {
        ImGui::Text("Guest -> network: %ld frames (%ld bytes)   ·   network -> guest: "
                    "%ld frames (%ld bytes)",
                    card.framesFromGuest, card.bytesFromGuest,
                    card.framesToGuest, card.bytesToGuest);
        ImGui::Text("SCSI commands served: %ld   ·   awaiting read: %zu",
                    card.commands, card.queued);
        if (card.framesDropped)
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1),
                               "Frames lost (ring full): %ld",
                               card.framesDropped);
        const auto& capture = snapshot.capture;
        ImGui::SeparatorText("Capture PCAP");
        ImGui::BeginDisabled(capture.busy || !snapshot.captureAvailable);
        ImGui::InputText("File PCAP", state.capturePath.data(),state.capturePath.size());
        ImGui::BeginDisabled(!state.capturePath[0]);
        if (ImGui::Button("Start capture") && state.capturePath[0])
            state.atalk.startEthernetCapture(state.capturePath.data());
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (capture.accepting && ImGui::Button("Stop capture"))
            state.atalk.stopEthernetCapture();
        if (capture.busy && !capture.accepting) ImGui::TextDisabled("Finishing write...");
        ImGui::Text("Frames written: %llu · observations lost: %llu",
            (unsigned long long)capture.written,(unsigned long long)capture.dropped);
        if (!capture.path.empty()) ImGui::TextWrapped("%s",capture.path.c_str());
        if (!capture.error.empty())
            ImGui::TextColored(ImVec4(0.95f,0.5f,0.35f,1),"%s",capture.error.c_str());
        ImGui::TextDisabled("Machine time; metadata in the .pcap.tsv file.");
    }
    drawDaynaPortSelector(state);
}

// ── Where the LaserWriter's jobs go ──
// LIVE, like the checkboxes: AtalkHub::setPrintDestination keeps the
// printer registered and an open job open. The queue list and the queue's
// state come from the spooler's own thread (PrintQueues.h) — this window
// never runs lpstat. The status line is exactly what the Mac's
// LaserWriter driver is told (SendStatus), so what the dialog on the Mac
// says can be read here.
char gPrintOptions[128] = "";
bool gPrintOptionsEditing = false;

void drawPrintDestination(GuiNetworkState& state, const AtalkHub::Snapshot& snapshot) {
    state.atalk.watchPrintQueues();
    const PapServer::Status& pap = snapshot.pap;
    const PrintSpooler::View& host = pap.host;
    using Kind = PrintDestination::Kind;
    auto choose = [&](const std::string& queue) {
        state.atalk.setPrintDestination(queue, snapshot.cfg.printOptions);
    };

    std::string current;
    switch (pap.destination.kind) {
    case Kind::File: current = "Save .ps files only"; break;
    case Kind::CupsQueue: current = "CUPS: " + pap.destination.queue; break;
    case Kind::CupsDefault:
        current = host.defaultQueue.empty() ? std::string("CUPS: default queue")
                                            : "CUPS: default (" + host.defaultQueue + ")";
        break;
    }
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("Destination", current.c_str())) {
        if (ImGui::Selectable("CUPS: default queue", pap.destination.kind == Kind::CupsDefault))
            choose("");
        for (const std::string& q : host.queues) {
            const bool on = pap.destination.kind == Kind::CupsQueue && pap.destination.queue == q;
            if (ImGui::Selectable(("CUPS: " + q).c_str(), on)) choose(q);
        }
        if (ImGui::Selectable("Save .ps files only", pap.destination.kind == Kind::File))
            choose(std::string(PrintDestination::kFile));
        ImGui::EndCombo();
    }

    if (!gPrintOptionsEditing)
        std::snprintf(gPrintOptions, sizeof gPrintOptions, "%s", pap.printOptions.c_str());
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputTextWithHint("Options lp -o", "media=A4 sides=two-sided",
                                 gPrintOptions, sizeof gPrintOptions))
        gPrintOptionsEditing = true;
    if (gPrintOptionsEditing) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply##lpopts")) {
            state.atalk.setPrintDestination(snapshot.cfg.printQueue, gPrintOptions);
            gPrintOptionsEditing = false;
        }
    }

    if (pap.destination.kind == Kind::File) {
        ImGui::TextDisabled("Each job -> %s/job_<n>_<i>.ps", pap.spoolDir.c_str());
    } else if (!host.polled) {
        ImGui::TextDisabled("Querying CUPS...");
    } else if (!host.cups && pap.destination.kind == Kind::CupsDefault) {
        statusDot(false, "CUPS unavailable (lpstat): jobs go to the spool folder");
    } else {
        const bool ready = host.state.phase != QueueState::Phase::Unknown &&
                           (host.state.accepting ||
                            host.state.phase == QueueState::Phase::Processing);
        statusDot(ready, ready ? "Queue accepts jobs"
                               : "Queue rejects jobs: the Mac sees the printer as busy");
        ImGui::TextDisabled("If CUPS rejects a job, it goes to %s/", pap.spoolDir.c_str());
    }
    ImGui::TextWrapped("Status sent to Mac: %s", pap.statusLine.c_str());
    if (host.pending)
        ImGui::Text("Jobs pending delivery to CUPS: %ld", host.pending);
}

} // namespace

const char* kNetworkWindowTitle = "AppleTalk / Ethernet";

void drawAppleTalkWindow(GuiNetworkState& state) {
    if (!state.showWindow) return;
    const AtalkHub::Snapshot snapshot = state.atalk.snapshot();
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 120),
        ImVec2(800, ImGui::GetMainViewport()->WorkSize.y - 40));
    if (!ImGui::Begin(kNetworkWindowTitle, &state.showWindow,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!snapshot.attached || !state.appleTalkEnabled) {
        ImGui::TextUnformatted(
            "Internal AppleTalk stack disabled (POM68K_APPLETALK=0).");
        // The card is on the SCSI bus regardless of AppleTalk, and carries
        // IPv4 through the NAT without it (DaynaPortBus.h). With no hub
        // attached the meter reads "no card", and the selector still stages
        // one for the next boot.
        drawEthernetSection(state, snapshot);
        ImGui::End();
        return;
    }

    bool stackOn = snapshot.cfg.stack;
    if (ImGui::Checkbox("AppleTalk network active", &stackOn))
        state.atalk.setService("stack", stackOn);
    ImGui::SameLine();
    ImGui::TextDisabled(snapshot.cableUp ? "(LToUDP cable: connected)"
                                        : "(LToUDP cable: local)");

    ImGui::SeparatorText("File sharing (AppleShare / AFP)");
    bool afpOn = snapshot.cfg.afp;
    if (ImGui::Checkbox("Enable AppleShare", &afpOn))
        state.atalk.setService("afp", afpOn);
    statusDot(snapshot.afp.registered,
              snapshot.afp.registered ? "Server visible in Mac Chooser"
                                      : "Server unavailable in Mac Chooser");
    statusDot(snapshot.afp.dirOk && snapshot.afp.catalogError.empty(),
              !snapshot.afp.catalogError.empty() ? snapshot.afp.catalogError.c_str() :
              snapshot.afp.dirOk ? "Shared folder is writable"
                                 : "Shared folder is not writable");
    ImGui::Text("Server name: %s", snapshot.afp.serverName.c_str());
        ImGui::Text("Volume: %s", snapshot.afp.volName.c_str());
    ImGui::TextWrapped("On the Mac: open Chooser, select "
                       "AppleShare, then this server.");
    if (!snapshot.afp.dirOk)
        ImGui::TextWrapped("Choose an existing folder below that is "
                           "writable, then click Apply.");
    ImGui::TextWrapped("Host folder: %s",
                       snapshot.afp.dirPath.empty()
                           ? "(unset)"
                           : snapshot.afp.dirPath.c_str());
    if (ImGui::CollapsingHeader("Sharing activity")) {
        ImGui::Text("Sessions: %d%s   ·   user: %s",
                    snapshot.afp.sessions,
                    snapshot.afp.volMounted ? " (volume mounted)" : "",
                    snapshot.afp.lastUser.empty() ? "-"
                                                  : snapshot.afp.lastUser.c_str());
        ImGui::Text("Last command: %s   ·   read %ld bytes / wrote %ld bytes",
                    snapshot.afp.lastCmd.empty() ? "-"
                                                 : snapshot.afp.lastCmd.c_str(),
                    snapshot.afp.bytesRead, snapshot.afp.bytesWritten);
    }
    // A refused command is the one thing a guest cannot tell you about: the
    // client swallows kErrNoOp and simply does less. The live gate has
    // printed this count since it existed; the window never did, so a
    // session with an older System — a Mac OS 7 client asks for things 8.1
    // does not — could quietly lose a feature with nothing said. Silent at
    // zero, which is where every session so far has sat.
    if (snapshot.afp.refusedCount)
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1),
                           "Rejected commands: %ld   ·   last: %s",
                           snapshot.afp.refusedCount,
                           snapshot.afp.lastRefused.empty()
                               ? "-" : snapshot.afp.lastRefused.c_str());

    drawServiceConfiguration(state, snapshot);
    if (ImGui::CollapsingHeader("Printing")) {
        ImGui::SeparatorText("Printer (LaserWriter / PAP)");
        bool papOn = snapshot.cfg.pap;
        if (ImGui::Checkbox("Enable printer", &papOn))
            state.atalk.setService("pap", papOn);
        statusDot(snapshot.pap.registered,
                  "Visible in Chooser (NBP LaserWriter)");
        ImGui::Text("Name: %s", snapshot.pap.printerName.c_str());
        ImGui::Text("State: %s%s", snapshot.pap.state.c_str(),
                    snapshot.pap.busy ? "  (busy)" : "");
        ImGui::Text("Jobs printed: %ld   ·   last: %s",
                    snapshot.pap.jobs,
                    snapshot.pap.lastJob.empty() ? "-"
                                                 : snapshot.pap.lastJob.c_str());
        if (papOn) drawPrintDestination(state, snapshot);

    }
    if (ImGui::CollapsingHeader("Internet via MacIP")) {
        ImGui::SeparatorText("Internet (MacIP / IP-in-DDP)");
        bool ipOn = snapshot.cfg.macip;
        if (ImGui::Checkbox("Enable MacIP gateway", &ipOn))
            state.atalk.setService("macip", ipOn);
        statusDot(snapshot.macip.registered,
                  "Gateway visible (NBP IPGATEWAY)");
        const bool ipWorks = snapshot.macip.registered && snapshot.macip.leases > 0;
        statusDot(ipWorks, ipWorks ? "MacIP working (lease assigned)"
                                   : "MacIP waiting (no guest connected)");
        ImGui::Text("Gateway: %s   ·   DNS: %s",
                    snapshot.macip.gwIp.c_str(), snapshot.macip.dns.c_str());
        ImGui::Text("Leases: %d   ·   last: %s   ·   UDP flows %d / TCP %d",
                    snapshot.macip.leases,
                    snapshot.macip.lastLease.empty()
                        ? "-"
                        : snapshot.macip.lastLease.c_str(),
                    snapshot.macip.udpFlows, snapshot.macip.tcpConns);
        ImGui::Text("Guest IP -> network %ld   -   network -> guest %ld",
                    snapshot.macip.ipFromGuest, snapshot.macip.ipToGuest);
        ImGui::TextDisabled("HTTP only (modern TLS unavailable) - "
                            "frogfind.com, theoldnet.com");

    }
    if (ImGui::CollapsingHeader("AppleTalk diagnostics")) {
        ImGui::SeparatorText("Node / router");
        char guest[16];
        if (snapshot.net.guestNode)
            std::snprintf(guest, sizeof guest, "%u", snapshot.net.guestNode);
        else
            std::strcpy(guest, "none");
        char routerLine[120];
        if (snapshot.routerNode)
            std::snprintf(routerLine, sizeof routerLine,
                          "Network %u learned from router %u, server node %u, "
                          "zone \"%s\"", snapshot.netNumber, snapshot.routerNode,
                          snapshot.node, snapshot.zone.c_str());
        else
            std::snprintf(routerLine, sizeof routerLine,
                          "Network %u, server node %u, zone \"%s\" (this node "
                          "routes)", snapshot.netNumber, snapshot.node,
                          snapshot.zone.c_str());
        statusDot(snapshot.cfg.stack, routerLine);
        ImGui::Text("Guest seen: %s   -   frames received %ld / sent %ld", guest,
                    snapshot.net.framesIn, snapshot.net.framesOut);
        ImGui::Text("NBP lookups served: %ld   -   ATP transactions: %ld",
                    snapshot.net.nbpLookups, snapshot.net.atpReqIn);
        if (snapshot.net.atpDupReqs || snapshot.net.atpDupPending) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.75f, 0.3f, 1),
                "Client retries: %ld  (last delay %ld ms, max %ld ms)",
                snapshot.net.atpDupReqs, snapshot.net.atpDupLagLastMs,
                snapshot.net.atpDupLagMaxMs);
            if (snapshot.net.atpDupPending)
                ImGui::TextDisabled(
                    "  %ld during service (slow server, not network loss)",
                    snapshot.net.atpDupPending);
            ImGui::TextDisabled(
                "  injection queue %zu (max %zu)  -  max wait %ld ms  -  "
                "POM68K_ATALK_DEBUG=1 for details",
                snapshot.wire.backlog, snapshot.wire.backlogMax,
                snapshot.wireHoldMaxMs);
        } else {
            ImGui::TextDisabled("Client retries: 0 (no network loss)");
        }
        if (snapshot.wire.drops)
            ImGui::TextColored(
                ImVec4(0.95f, 0.5f, 0.35f, 1),
                "Network overflow: %ld frames  (guest stopped listening "
                "long enough to fill the queue)",
                snapshot.wire.drops);

    }
    drawEthernetSection(state, snapshot);
    ImGui::End();
}

} // namespace pom68k::gui
