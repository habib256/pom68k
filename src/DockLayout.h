#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DockLayout -- the docked shell every platform runner shares.
//
// One call per frame turns the main viewport into a dock space and, the very
// first time (or on demand), lays out the two base windows:
//
//     ┌──────────────────────────┬────────────────────┐
//     │                          │                    │
//     │   Screen                 │  Disk Library      │
//     │   (the emulated Mac)     │  (src/DiskBays.*)  │
//     │                          │                    │
//     └──────────────────────────┴────────────────────┘
//
// The split is only a DEFAULT. ImGui persists whatever the user drags into
// imgui.ini afterwards, so the layout survives restarts; dockLayoutReset()
// puts it back.
//
// The screen window's title is the machine profile name and therefore differs
// per runner ("Macintosh LC II", "Quadra 950", ...), so the layout is keyed on
// the name passed in. A rebuild is triggered automatically when that name
// changes -- which is exactly what a machine switch does.
// ─────────────────────────────────────────────────────────────────────────────

namespace pom68k {

// The dock-space title of the disk window. DiskBays uses the same string.
extern const char* kDiskWindowTitle;

// Call once, at start-up, before the first frame: enables docking on the
// ImGui IO flags.
void dockLayoutInit();

// Call once per frame, AFTER the main menu bar (so the dock space starts
// below it) and BEFORE the windows are drawn. machineMenu() calls this for
// every runner, so a platform gets the docked shell for free.
void dockLayoutFrame();

// Tell the layout which window holds the emulated screen. Call immediately
// before that window's ImGui::Begin(). Registering it here rather than
// passing it to dockLayoutFrame() keeps the eleven call sites to a one-liner
// each, and a changed title (i.e. a machine switch) re-splits on its own.
void dockLayoutScreenWindow(const char* title);

// Force the default split back on the next frame (menu: Fenêtres → Réinitialiser).
void dockLayoutReset();

// A "Fenêtres" menu with the reset entry, for the shared menu bar.
void dockLayoutMenu();

} // namespace pom68k
