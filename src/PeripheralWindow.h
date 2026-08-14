#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PeripheralWindow -- the one "Périphériques (LLE / HLE)" window, on every
// machine.
//
// What it answers, and nothing else answered before it: *which side is each
// peripheral of THIS machine actually running on, and why?*  Until now the
// answer existed only as stderr lines scrolling past at startup — a user who
// launched from a desktop icon never saw them, and "am I conformant?" had no
// visible answer short of reading the terminal.
//
// Three rules it is built on:
//
//   1. **It renders reports, it never re-derives.**  Every row comes from
//      `pom68k::lle::devices()`, filled by the device itself at construction
//      (LleSession.h).  A window that re-ran the "is the dump there / is the
//      knob set" logic would be a second copy free to drift from the first,
//      which is the failure this tree has paid for repeatedly.
//   2. **It lists what this machine HAS.**  A Centris has no Egret; the
//      compacts have no ADB transceiver.  The list is per-session, not a
//      fixed table of every device in the tree, so an empty list is a real
//      answer ("this machine has no LLE/HLE choice to make") rather than a
//      bug.
//   3. **Changes are staged, then applied by an explicit relaunch.**  The
//      devices are constructed once, from getenv, before the first
//      instruction runs — there is no live toggle to offer and pretending
//      otherwise would be a lie in the UI.  Applying sets the knobs and
//      re-execs the process, exactly as the Machine menu does for a profile
//      change (`gSwitchArgs` + `relaunchIfSwitched`).
//
// Selecting LLE is refused, with the reason shown, when no dump is present:
// the row then displays the paths the device searched, so "where do I put
// the file" has an answer in the window instead of in the documentation.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>

namespace pom68k {

// What the window needs from the shell. `relaunch` re-execs the process on
// its own command line, so the machine comes back identical apart from the
// environment the window has just set.  Null = the shell cannot relaunch,
// and the window says so instead of offering a button that does nothing.
struct PeripheralHost {
    std::function<void()> relaunch;
};

// "Périphériques (LLE / HLE)..." — for the Machine menu.
void peripheralMenuItem();

// Draws the window when open. Safe to call every frame on every machine.
void peripheralWindow(const PeripheralHost& host);

} // namespace pom68k
