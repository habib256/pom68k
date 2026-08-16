# Auto mode — standing brief

Read this at every wake-up. It is the whole instruction: the queue below, in
order, under the rules below. It is harness configuration, not project
documentation — the project's own record stays in `CLAUDE.md` / `DEV.md` /
`TODO.md` / `CHANGELOG.md`, and auto mode is expected to write to those.

---

## 1. Hard rules

These are not style. Every one of them was paid for; the citation is where.

- **A green `ctest` is only worth the freshness of its binaries.** `ctest` does
  not build. `make -j4 <target>` first, always, and quote the build exit code —
  not a grep of its output (the toolchain is French: "Erreur", not "error").
  A 0-byte gate binary is a link killed mid-flight, not a test failure.
- **Never two builds or two ctests at once.** The etalons are contention
  sensitive; overlap invents failures. Serial, always. And **no source edits
  while a build or a gate is running** — a mixed tree produces phantom results
  in both directions. Docs-only during a run.
- **Read the gate's `ASSET` preamble before its diff.** `roms/` and `hdv/` are
  mutable and unversioned. `drVolAtrb` bit 8 moves under the gates in both
  directions and changes how much work the guest does — that is what took
  three boot gates red for two days on 2026-08-14 (`CHANGELOG.md` 2026-08-15).
- **Never move a threshold to make a gate pass.** If a criterion no longer
  discriminates, replace it with one that does and prove both directions —
  green on the passing case, red on a deliberately broken one. See
  `tests/FinderSignature.h` for the shape of that argument.
- **A phantom failure gets investigated; a phantom pass gets quoted.** Prefer
  being red and right.
- **Pick the narrowest tier.** Never iterate on a full `ctest` (~4 h) or a full
  `make`. Working loops:
  - `make -j4 jitdev && ctest -L smoke` — 2.5 min
  - one gate: `make -j4 <gate> && ctest -R '^<gate>$'` — 1-2 min for boot
    etalons, up to 30 min for a `*_persist_etalon` (their CTest TIMEOUT is 1800)
  - pre-commit: `ctest -L etalon-core` — 12 gates, ~31 min
- **Never drive the GUI blind** (`import -window root` first, POM68K captures
  the pointer, two missed clicks = stop). Auto mode should not need the GUI at
  all; if an item does, stop and hand it back.
- **Docs are part of "done".** A landed item = one line in `TODO.md`, a dated
  entry prepended to `CHANGELOG.md` with its `<a id>` anchor on the line
  immediately above the heading, one line in Index by date, then
  `python3 tools/changelog_index.py`, then re-check the entry counts quoted in
  `CLAUDE.md` / `TODO.md` / `CHANGELOG.md` and run `docs_test`.

## 2. Queue

Work the queue in order. One item per wake-up unless an item finishes early.

### Q1 — `duo_persist_etalon` (registered CTest, red, pre-existing)

```
make -j4 duo_beyond_etalon
POM68K_BEYOND=persist ./build/duo_beyond_etalon      # cwd = repo root
```

Recorded symptom (2026-08-14): *"NO candidate folder name appeared, image
UNCHANGED"* before the reboot, while the post-reboot desktop counts **thirteen**
`untitled folder`s. The day's flux work cannot reach it — the Duo has no floppy
drive.

Controls to run before theorising, each closing a known failure mode of this
exact gate family:

1. **Accumulated state on the shared image.** Thirteen folders are already
   there. `FolderProbe`'s rule is that the signal is the count that *changes*,
   not the biggest count — verify the probe is reading `grew()`, not `count()`.
2. **A dialog is eating the gesture.** Dark ratios pass with a modal alert up.
   `POM68K_DUMP` a frame at the gesture's peak and read `lightRun` against
   `kDialogRun`; `BeyondBoot.h` carries the reasoning.
3. **The flush, not the creation.** System 7.x holds a created folder forever
   at 8 MB and writes it instantly at 4 MB. Check the RAM size this gate
   configures and judge on the `ScsiDisk` write counters, not on elapsed time.
4. **The gate drives `mem.mouseButton()` directly**, so it is blind to the
   `MachineHost` button folding — if the gesture depends on that, it is testing
   a path the GUI no longer uses.

Exit: `ctest -R '^duo_persist_etalon$'` green on a freshly built binary, **and**
`duo_soak_etalon` still green, both quoted with the build exit code.

### Q2 — `tests/finder_boot_matrix.cpp:207`

Its Mac II leg asserts `scsi().commands > 500` — the same fixture-reading trap
just removed from the three boot etalons. Not a registered CTest, so nothing is
red; it was deliberately **not** fixed blind, because re-calibrating it means
running the sweep.

```
make -j4 finder_boot_matrix
./build/finder_boot_matrix <plus|macii|lcii|q605> <rom> <disk>
```

Images that exist today: `HD20SC.vhd`, `GISTPERSO-boot.vhd`, `boot.vhd`,
`MacOS-8.1-boot.vhd`, `RaSCSI-Boot-7.5.3.hdv`, `Supplement.vhd`,
`System 4.1.dsk`. **`MacOS-7.6-boot.vhd` is gone** — only its `.pram` files
remain — so any 7.6 cell in the historical record is not reproducible.

Do: run every (machine × image) cell that exists, record the table, then give
the Mac II leg `FinderSignature.h` (`CurApName == "Finder"` + the menu-bar light
run) in place of the count. Exit: the table is in `CHANGELOG.md`, and the leg
passes on the cells it passed on before.

While in that file, `TODO.md` already carries two neighbouring items — the
missing matrix cells for the profiles added since Phase C, and the Plus
System 4.1 floppy cell (`bootPlus` only does `attachScsi`). Note them, do not
start them.

### Q3 — (open)

Nothing queued. On reaching Q3, **stop and report** rather than choosing new
work from `TODO.md`.

## 3. Cadence and stopping

- One wake-up = one item, or one honest increment of one item.
- **Stop and hand back** on: an item needing the GUI; an item needing an asset
  that is not in the tree; a third consecutive wake-up with no progress on the
  same item; anything that would need a threshold moved; the queue running dry.
- Do not start a task whose gate cannot finish inside the wake-up — split it,
  and leave the state in `TODO.md`.

## 4. Reporting

Per wake-up, short and falsifiable:

- what ran, with the **build exit code** and the gate's own printed line;
- what changed, as a file list;
- what is left, and the next concrete step;
- anything believed but not measured, said as such.

No claim of a green suite without a fresh build behind it.

## 5. Commit policy

<!-- decided with the user; see the session that created this file -->

**TBD** — see the questions asked when this file was drafted.
