// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The disk-bay BINDINGS, outside GL.
//
// `gui_windows_test` draws the Disques window against a hand-built
// `DiskBaysHost`: it proves what the window does with the hooks. What
// nothing reached was the other side — the code that fills those hooks from
// a runner's machine. It lived inside six runner functions that cannot be
// instantiated without a GL context, so five near-identical copies of a
// contract went unchecked (TODO § Preuve).
//
// They are one function now (`diskBaysHostFor`), and this is the gate on it:
// every hook is called and the machine is asked what it received. The
// machine here is a recorder, which is the point — the binding's job is to
// turn a window gesture into exactly one request on exactly one bay.

#include "GuiFloppyBays.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

namespace {

// Everything the bindings are allowed to ask of a machine, and nothing else:
// if this compiles, the binding touched no wider surface.
struct RecordingMachine {
    struct Cmd { enum T { HardReset } t; };
    std::vector<int> pushed;
    void push(Cmd c) { pushed.push_back(int(c.t)); }

    // Bays
    bool cdAt3 = true;
    bool bayIsCdrom(int id) const { return cdAt3 && id == 3; }
    int insertedBay = -1, ejectedBay = -1, attachedBay = -1, detachedBay = -1;
    std::string insertedPath, attachedPath;
    void requestInsertBay(int id, const std::string& p) { insertedBay = id; insertedPath = p; }
    void requestEjectBay(int id) { ejectedBay = id; }
    void requestAttachDisk(int id, const std::string& p) { attachedBay = id; attachedPath = p; }
    void requestDetachDisk(int id) { detachedBay = id; }

    // Floppies
    bool inserted[2] = { false, false };
    std::string paths[2];
    bool floppyInserted(int d) const { return inserted[d & 1]; }
    std::string floppyPath(int d) const { return paths[d & 1]; }
    void requestInsertFloppy(const std::string& p, int d) {
        inserted[d & 1] = true; paths[d & 1] = p;
    }
    void requestEjectFloppy(int d) { inserted[d & 1] = false; paths[d & 1].clear(); }

    // The guest's own view and the agent
    pom68k::GuestScsiView view;
    pom68k::GuestScsiView guestScsiView() const { return view; }
    std::string message = "rien";
    std::string bayMessage() const { return message; }
    struct Agent { bool present = true; pom68k::ScsiAgentSnapshot mailbox; };
    Agent agentState;
    const Agent& agent() const { return agentState; }
    int mounted = -1, unmounted = -1;
    void requestAgentMount(int id) { mounted = id; }
    void requestAgentUnmount(int id) { unmounted = id; }
};

} // namespace

int main() {
    std::printf("Disk-bay bindings: one window gesture, one request\n");

    RecordingMachine machine;
    std::vector<std::string> extras = { "b.vhd", "c.vhd" };
    pom68k::DiskBaysHost host =
        pom68k::gui::diskBaysHostFor<RecordingMachine>(machine, &extras);

    check(host.extras == &extras, "the window sees the runner's own extras list");
    check(host.hasFloppyDrive && host.hasExternalFloppyDrive,
          "a board with floppies offers both drives");
    check(host.supportsEmptyCdDrive, "and an empty CD bay");

    // ── Machine control ─────────────────────────────────────────────────
    host.hardReset();
    check(machine.pushed.size() == 1 &&
          machine.pushed[0] == int(RecordingMachine::Cmd::HardReset),
          "hardReset pushes exactly one HardReset");

    // ── Removable bays ──────────────────────────────────────────────────
    check(host.bayIsCd(3) && !host.bayIsCd(4),
          "bayIsCd answers from the machine, per id");
    check(host.insertBay(3, "disc.iso") && machine.insertedBay == 3 &&
          machine.insertedPath == "disc.iso",
          "insertBay reaches the CD bay with the path");
    check(!host.insertBay(4, "disc.iso") && machine.insertedBay == 3,
          "and REFUSES a bay that is not a CD, without touching it");
    host.ejectBay(3);
    check(machine.ejectedBay == 3, "ejectBay names the bay it was given");

    // ── Live attach and detach ──────────────────────────────────────────
    check(host.attachBay(1, "d.vhd") && machine.attachedBay == 1 &&
          machine.attachedPath == "d.vhd", "attachBay queues the disk");
    check(!host.attachBay(0, "d.vhd") && !host.attachBay(7, "d.vhd") &&
          !host.attachBay(2, "") && machine.attachedBay == 1,
          "ids outside 1-6 and an empty path are refused before the machine");
    check(host.detachBay(6) && machine.detachedBay == 6, "detachBay queues it");
    check(!host.detachBay(7) && machine.detachedBay == 6,
          "and refuses an id that is not a bay");

    // ── Floppies ────────────────────────────────────────────────────────
    check(!host.floppyInserted() && !host.externalFloppyInserted(),
          "both drives start empty");
    host.insertFloppy("boot.dsk");
    host.insertExternalFloppy("data.dsk");
    check(host.floppyInserted() && host.externalFloppyInserted(),
          "each insert lands in its own drive");
    check(machine.paths[0] == "boot.dsk" && machine.paths[1] == "data.dsk",
          "internal is drive 0, external is drive 1 — not swapped");
    host.ejectFloppy();
    check(!host.floppyInserted() && host.externalFloppyInserted(),
          "ejecting one leaves the other alone");
    pom68k::gui::refreshFloppyBays(host, machine);
    check(host.floppyPath.empty() && host.externalFloppyPath == "data.dsk",
          "and the refresh reports what the machine holds");

    // ── The guest's view and the agent ──────────────────────────────────
    machine.message = "SCSI 1: monté";
    check(host.bayMessage() == "SCSI 1: monté", "bayMessage passes through");
    check(host.agentPresent(), "agentPresent reports the machine's agent");
    host.agentMount(2);
    host.agentUnmount(5);
    check(machine.mounted == 2 && machine.unmounted == 5,
          "agent mount and unmount carry their own bay");
    host.guestView();
    check(true, "guestView is readable without a machine thread");

    // ── A board with neither floppies nor a live bay (the Duo) ──────────
    {
        RecordingMachine duo;
        std::vector<std::string> duoExtras;
        pom68k::DiskBaysHost h = pom68k::gui::diskBaysHostFor<RecordingMachine>(
            duo, &duoExtras, { false, false });
        check(!h.hasFloppyDrive && !h.hasExternalFloppyDrive,
              "a board with no floppy drive offers none");
        check(!h.supportsEmptyCdDrive, "nor an empty CD bay");
        check(!h.insertBay && !h.ejectBay && !h.bayIsCd,
              "and no live-swap hooks at all: null, not a silent no-op");
        check(h.attachBay && h.guestView,
              "but fixed disks still attach, which is how a Duo gets one");
    }

    std::printf(failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
