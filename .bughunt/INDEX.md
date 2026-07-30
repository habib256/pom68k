# POM68K — adversarial bug hunt, 2026-07-27

12 workflow hunts (10 planned + 2 re-runs covering finder crashes).
Each hunt: 3 diverse-lens finders reading the real source → 2 adversarial
refuters per top finding (one "prove the code doesn't say that", one "prove
it's unreachable in a real run"; uncertainty counts as refutation) →
synthesis agent re-checking survivors against the code.

**~190 subagents, ~15.6 M subagent tokens, ~3600 tool calls.**
Nothing in the tree was modified — all analysis was read-only.

## Confirmed by hand (I re-read the code myself for these)

| Sev | Where | Defect |
|---|---|---|
| critical | `SonyDrive.cpp:129` | DC42 `dataSize` guard wraps in 32-bit (`0x54 + dataSize` as `unsigned`), but the *use* on line 131 widens to `ptrdiff_t` and does not wrap → ~4 GB heap over-read from a crafted `.image` |
| high | `SonyDrive.cpp:177` | `selectSide()` has no `doubleSided_` guard; `encodeTrackGcr:288` unbounded → OOB read at track 79/side 1 of a 400K image (and wrong-track data on 0-78) |
| high | `main.cpp:205` | AppleTalk hub ticked with `cpu.getClock()` (boosted ×4) while `hubHz` is the real machine clock → every second-scale timer fires 4× early. `machineClock()` exists on every wrapper for exactly this |
| high | `AdbVia.h:88` | `kCyclesPerPicInsn = 34` hardcoded; correct only at 15.6672 MHz. PIC1654S runs 2.13× fast on Quadra 650, 2× slow on Mac SE/Classic |
| high | `Asc.cpp:187` | `stereo = regs_[0x02] & 1` — MAME's `CONTROL_STEREO = 1` is a *bit index* used via `BIT()`, i.e. mask 0x02. Reads the PWM-output bit instead |
| high | `MacIIMemory.cpp:64` | Synthetic Toby decl ROM piped through `installRaw()`, which expects the reversed MAME dump → returns empty. Fallback installs no card at all (agent proved it by compiling and running the code) |
| medium | `main.cpp:2867` | `kFrame = 416667` hardcoded in the `DafbMachine` template shared by 4 machines at 20/25/33 MHz |
| medium | `main.cpp:3278` | `setenv("POM68K_CENTRIS_FPU")` with no `else unsetenv`; menu relaunches via `execv` → Quadra→Centris boots a full 68040 while printing "68LC040" |
| medium | `main.cpp:2809` | PixMap `pixelSize` read at 0x1C (low half of `vRes`) instead of 0x20 → `pmDepth` always 0, documented depth fallback is dead code |
| medium | `MacMemory.cpp:233` | `case 0xE: case 0xF:` + `addr >= 0xE80000` decodes the VIA over all of $F00000-$FFFFFF; a stray write to $F00200 hits ORA and re-asserts the boot overlay |
| medium | `Valkyrie.cpp:43/69` | `switch (off & 0xFF)` on an 8 KB window → $110 aliases onto the VBL-ack/config register |
| medium | `VaspVideo.h:47` | 1/2/4/8 bpp branches unbounded at 2048-byte pitch; only 16 bpp is guarded |
| medium | `AfpServer.cpp:1015` | FPWrite Offset decoded `uint32_t` though AFP defines it signed; a legal from-EOF `-512` becomes `+4294966784`. The new clamp guards the resource branch only — the data branch `seekp`s unbounded |

## Cross-hunt corroboration

Found independently by hunts that could not see each other — the strongest
signal the harness produced:

- **`main.cpp:2924` `floppyPending_` read/cleared outside `cmdMu_`** — found by
  H4, H10 *and* H9b. Use-after-free when a second floppy is picked while
  `insertDisk()` is doing file I/O.
- **Insert-over-dirty-floppy drops committed writes** (`SonyDrive.cpp:144`) —
  found by H4 and H10.

## Per-hunt reports

| Hunt | Survivors | File |
|---|---|---|
| H1 worktree diff | 0 (3 refuted) | `H1-worktree-diff.md` |
| H1b diff / memory-safety | 2 (+1 kept) | `H1b-worktree-diff-memsafety.md` |
| H2 AppleTalk | 6 | `H2-appletalk-network.md` |
| H3 memory maps | 1 (+5 kept) | `H3-memory-maps.md` |
| H4 storage | 5 | `H4-storage-scsi-floppy.md` |
| H5 MCU firmware LLE | 2 (+4 kept) | `H5-mcu-firmware-lle.md` |
| H6 video | 3 (+6 kept) | `H6-video-framebuffer.md` |
| H7 CPU wrappers | 1 (+3 kept) | `H7-cpu-wrappers.md` |
| H8 sound / timers | 3 (+3 kept) | `H8-sound-timers.md` |
| H9 SCC / serial | 1 (+5 kept) | `H9-serial-input-nubus.md` |
| H9b input / NuBus | 5 | `H9b-input-nubus-declrom.md` |
| H10 main.cpp | 3 (+4 kept) | `H10-app-lifecycle-main.md` |

## Gates that lock in a bug

Several findings note that an existing CTest encodes the defective behaviour,
so fixing the code requires fixing the gate in the same patch:

- `tests/atalk_stack_test.cpp:51` builds the ZIP GetNetInfo request in the same
  wrong layout as the parser it tests.
- `tests/toby_test.cpp:28` writes the RAMDAC at `base+0x90002`, locking the
  wrong address-bit decode.
- `tests/floppy_persist_test.cpp:79` only builds a `tagSize == 0` DC42 header,
  so the malformed tag write-back is never exercised.

## Reliability caveats

- 6 finder agents died on `ECONNRESET` mid-run (H1, H2, H3, H6, H7, H9). Where
  a sibling lens still covered the axis I said so; where it did not (H1, H9) the
  hunt was re-run as H1b / H9b.
- The safety classifier was unavailable for ~15 subagents; the harness flagged
  each. That is why the table above is limited to findings I re-read myself.
- Findings below the top-2 per lens got no adversarial pass — they are marked
  as such in each report, not silently dropped.
