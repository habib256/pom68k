# POM68K — adversarial bug hunt, 2026-07-27 — closing record

Twelve workflow hunts (10 planned + H1b/H9b re-running two axes whose finder
died mid-run). Each hunt: 3 diverse-lens finders reading the real source → 2
adversarial refuters per top finding (one "prove the code doesn't say that",
one "prove it's unreachable in a real run"; uncertainty counts as refutation) →
synthesis re-checking survivors against the code. ~190 subagents, ~15.6 M
tokens, ~3600 tool calls, nothing in the tree modified during the hunt.

**Re-verified against the code 2026-08-12.** 60 surviving findings: **57 fixed**,
**1 retracted by later evidence**, **2 still open** (below). The per-hunt report
files are gone — every fix landed with a source comment restating the defect it
closes, so the reports had become a second, staler copy of what the code now
says. Cite the code, not a report.

## Still open — belongs in TODO.md

### LLAP address-defence ACK cannot use the express path its own comment promises
- **Where:** `src/AtalkStack.cpp:88-96` (the claim), `src/AtalkHub.h:80-90` (the wiring)
- **Defect:** the lapENQ defence comment states *"the express path keeps the ACK
  inside the prober's window, like the cable's synthesized CTS"*, but the only
  production binding of `stack_.sendFrame` appends to `pending_`, and the flush
  at `AtalkHub.h:69` calls `injectRxFrame(0, d, n, /*express=*/false)` — one tick
  later, then further delayed by LLAP's 400 µs inter-dialog gap in `Scc8530`.
  The express mechanism exists and is used, but only for the CTS synth
  (`main.cpp:210`).
- **Consequence:** a guest probing node 128 (the internal node's id) can time out
  before the ACK arrives and take the address. Both nodes then answer to 128:
  `onGuestFrame`'s `if (src != node_)` guard stops recording the guest, and every
  DDP the stack sends to 128 is also the guest's own address. The `enqSeen`
  statistic advertises a defence the wiring cannot deliver.
- **Note:** the hub's deferral is deliberate and *correct* for reply frames — a
  non-express injection during `onTxFrame` is dropped (`Scc8530.cpp` "no ear").
  The bug is that a control frame with a hard deadline shares that path.
- **Fix:** a separate `sendControlFrame` hook wired straight to
  `injectRxFrame(0, d, n, /*express=*/true)`, bypassing `pending_`, mirroring the
  lapRTS→lapCTS synth. At minimum, correct the comment so it stops claiming a
  guarantee the wiring does not provide.

### Machine menu: `strstr` variant match makes "Macintosh II" unclickable again after picking IIx/IIcx
- **Where:** `src/main.cpp:866` — `variantCur = e ? (std::strstr(e, pr.envVal) != nullptr) : pr.dflt;`
- **History:** the original finding (plain Mac II permanently "current" because
  `envKey == nullptr` defaulted `variantCur` to true) was fixed by tagging the row
  `POM68K_MACII_MODEL="ii"` (`main.cpp:805-809`, whose comment records exactly
  that). **The fix is incomplete:** the match is a substring test, and `"ii"` is a
  substring of both `"iix"` and `"iicx"`.
- **Trigger:** pick "Macintosh IIx" from the menu → `POM68K_MACII_MODEL=iix` is
  set and inherited across the `execv` relaunch → from then on
  `strstr("iix","ii")` is non-null, so both the "Macintosh II" and "Macintosh IIx"
  rows compute `isCur = true` and `&& !isCur` swallows the click on the plain
  Mac II. Same from IIcx. No other group overlaps (Q605 2221/2225/222E, AIO
  A55A0100/A55A0101/CC2, Centris c610/c650/q610/q650/q800, Q700 q700/q900/q950,
  LC3_PLUS 0/1, IIVI 0/1 are all substring-safe).
- **Fix:** exact `strcmp`, plus a case-insensitive compare for the hex IDs (the
  runners already accept both cases via `strstr` on their own side).
- **Secondary, same site:** the "Macintosh II" row's envVal `"ii"` is not one of
  the tokens the dispatch at `main.cpp:5544-5548` recognises (`"iicx"`/`"se30"`/
  `"fdhd"`, else IIx) — the plain Mac II is reached by ROM checksum `$9779D2C4`,
  so the `"ii"` token is dead, and `"fdhd"` is a runtime-only token with no
  `kProfiles` row.

## Retracted by later evidence

- **"Classic ASC does not decode `$F09`/`$F29`, returning 0 where hardware returns
  `$01`"** (H8). `AscV8::read` now returns 0 there *deliberately*, and
  `src/Asc.cpp:70-80` carries the reasoning: MAME master's own in-file real-LC and
  real-IIci ASCTester dumps read `$00`, and MAME's `asc_v8_device::read` returning
  1 contradicts its own dump. The comment is marked PIN — do not "fix" toward
  MAME. Adopted during the 2026-08-06 MAME-parity sweep (audit #42).

## Per-hunt outcome

| Hunt | Scope | Findings | Outcome |
|---|---|---|---|
| H1 | worktree diff | 0 | 3 candidates raised, all 3 refuted by the panel (VIA T1 continuous-mode IFR6; freeze probe throwing `MmuBusError` out of the emulation thread; 68HC05 interrupt charged 11 cycles instead of 10). Memory-safety finder died on ECONNRESET → re-run as H1b |
| H1b | diff / memory safety | 3 | all fixed — AFP FPWrite signed offset + both forks bounded (`AfpServer.cpp:1068-1120`); MacIP oversized UDP no longer laundered (`MacIpGateway.cpp:610-618`, 64 KB buffer + `MSG_TRUNC`); freeze-probe window clamps instead of wrapping and compares signed (`main.cpp:3494-3507`) |
| H2 | AppleTalk / network | 9 | 8 fixed, 1 open (LLAP ACK, above). ZIP GetNetInfo zone length read at offset 6 (`AtalkStack.cpp:271-286`, gate `atalk_stack_test.cpp:48-81` rebuilt); `pendingTxns_` given an expiry sweep + PAP drop paths made terminal (`AtalkStack.cpp:185-186, 493`, `PapServer.cpp:63-65`); FPMoveAndRename creates the destination `.AppleDouble` (`AfpServer.cpp:863-867`); `adRead` stats before slurping (`AfpServer.cpp:105-106`); MacIP drops non-first IP fragments (`MacIpGateway.cpp:293-294`); ASP session ids checked for reuse (`AfpServer.cpp:350-360`); MacIP leases reclaimed on `lastSeen` (`MacIpGateway.cpp:655-661`); over-31-byte names skipped in FPEnumerate rather than advertised unopenable (`AfpServer.cpp:694-698`) |
| H3 | memory maps | 5 | all fixed — compact VIA no longer decoded over `$F00000-$FFFFFF` (`MacMemory.cpp:359-366, 416-420`); `peek8` board ID bounded to the I/O select on all four boards (`Q605Memory.cpp:745`, `Q630Memory.cpp:673`, `SonoraMemory.cpp:554`, `VaspMemory.cpp:411`); IOSB/PrimeTime `$A55A2BAD` answers the whole ID window (`Q605Memory.cpp:371-372`, `Q630Memory.cpp:300-301`); Valkyrie switches on the raw offset (`Valkyrie.cpp:81, 113`); `$600000-$7FFFFF` honours `overlay_` and the ADB compacts clear it on the low-RAM *write* like `ram_w_se` (`MacMemory.cpp:314-315, 375-386`) |
| H4 | storage / SCSI / floppy | 8 | all fixed — DC42 `dataSize` guard in 64-bit (`SonyDrive.cpp:161-171`); `selectSide` clamps the head to the media and the GCR encoder bounds each sector (`SonyDrive.cpp:225, 391-393`); DC42 write-back zeroes `tagSize`/`tagChecksum` (`SonyDrive.cpp:784-785`, gate `floppy_persist_test.cpp:110-143`); insert-over-insert flushes first (`SonyDrive.cpp:149`); `floppyPending_` race gone with the `MachineHost` refactor — the path travels inside the `Cmd` under `cmdMu_` (`MachineHost.h:116-119, 337-347`); `CI_PAD` really discards and `CI_COMPLETE` drops residue (`Ncr53c96.cpp:525+`); `CD_SELECT_ATN_STOP` stops after MSG-OUT (`Ncr53c96.cpp:478-486, 656-660`); `runTarget` clears the previous read payload up front (`Ncr53c96.cpp:671`); empty CDB can no longer reach `ScsiDisk::command`, which now honours `cdbLen` (`Ncr5380.cpp:153, 373`, `ScsiDisk.cpp:707-717`) |
| H5 | MCU firmware LLE | 6 | all fixed — PIC1654S clock is a machine parameter on a rational accumulator (`AdbVia.cpp:142-163`, `attach(…, cpuHz)`); `CudaLle::reset` snapshots live PRAM before dropping `pramInstalled_` (`CudaLle.cpp:55-64`); `AdbVia::reset` resets the LLE half too (`AdbVia.cpp:35`); AdbLine inter-byte threshold moved to `T_BIT` with `buffer_` bounded (`AdbLine.cpp:148-154, 177`); keyboard Talk R3 returns flags + handler ID (`AdbLine.cpp:385-392`); M68HC05 one-second timer decoupled from the PLL selector (`M68hc05.cpp:501`) |
| H6 | video / framebuffer | 9 | all fixed — VaspVideo and V8Video reject modes larger than their VRAM (`VaspVideo.h:57`, `V8Video.h:87`); Toby RAMDAC decoded on the 32-bit word index like MAME's `umask32` (`TobyVideo.cpp:49, 105`, gate `toby_test.cpp:32-50` retargeted to `$9001C`/`$90018`); Bt453 lookups high-aligned and the VRAM byte path inverted to match (`TobyVideo.cpp:114-121, 285-305`); host decoder renders 16/24 bpp (`main.cpp:3354, 3395-3400`); PixMap `pixelSize` read at `+0x20` (`main.cpp:3334`); three DAFB clock generators behind the `+$300` window (`Dafb.h:41`, `Dafb.cpp:269-274`); DAFB version is a ctor parameter with the `hres_ == 512 && version_ == 1` quirk (`Dafb.cpp:69-73, 231-233`); Valkyrie RAMDAC read fires once per access (`Q630Memory.cpp:355-359`); 1 bpp goes through the CLUT (`main.cpp:3385-3387`) |
| H7 | CPU wrappers | 4 | all fixed — AppleTalk hub ticked on `machineClock()` (`main.cpp:258`); `Cpu030::runCycles` keeps the boost on the `POM68K_FPU_LOG` path (`Cpu030.cpp:96`); every wrapper overrides `read16Dasm` through `peek8`, removing the bus-error path from disassembly (`Cpu030.cpp:305` + 12 siblings); `MacIIMemory::viaAccess` opens with `flushTicks()` so byte and word accesses see one timeline (`MacIIMemory.cpp:263-269`) |
| H8 | sound / timers | 6 | 5 fixed, 1 retracted (above). Classic ASC stereo reads CONTROL bit 1 (`Asc.cpp:224`); compact RTC second is a cycle accumulator against `kCpuHz`, not 60 video frames (`MacMemory.cpp:80-83`, `MacFrame.h:49-50`); `~MacAudioHost` uninits and nulls the FX slots (`MacAudioHost.h:46-50`); AscSonora forces "FIFO A empty" in playback mode after a write (`Asc.cpp:561-564`); `PseudoVia` has a flavour flag, RBV and VASP take `Base` (`PseudoVia.h:44`, `RbvMemory.h:299`, `VaspMemory.h:248`) |
| H9 | SCC / serial | 6 | all fixed — SDLC Rx overrun holds briefly then loses the byte instead of wedging the wire (`Scc8530.cpp:212-233`); the Rx queue cap is no longer gated on `losslessRx_` (`Scc8530.cpp:368-372`); back-pressure meters sampled on the machine thread inside `tick()` (`AtalkHub.h:139`); RR4-RR14 NMOS aliases + RR12/RR13 (`Scc8530.cpp:393-395, 469-470`); WR reset in `resetChan` (`Scc8530.cpp:716-721`); `DeclRom::installRaw` guards `n < 20` (`DeclRom.cpp:218-221`); LToUDP reads with `MSG_TRUNC` and drops oversized datagrams (`LtoUdp.cpp:97-101`) |
| H9b | input / NuBus / decl ROM | 8 | all fixed — synthetic Toby decl ROM installed directly instead of round-tripping through `installRaw` (`MacIIMemory.cpp:61-67`); `validateFormatBlock` inverts only when the marker says so (`DeclRom.cpp:127-131`); `buildSynthetic` VendorInfo string emitted after the end-of-list (`DeclRom.cpp:141-146`); `floppyPending_` race (see H4); M0110 Inquiry gets its own ~250 ms hold (`MacMemory.cpp:106-128`, `MacMemory.h:217`); Talk to an unconnected address raises the mouse SRQ (`AdbLine.cpp:395-402`); `MacMouse::reset` + a ±256 clamp in `move()` (`MacInput.h:43-57`, `MacMemory.cpp:63`); left/right Shift press-counted per transition code (`main.cpp:451-466`); decl ROM answered only in slot space, never super-slot (`NuBus.cpp:70-84`) |
| H10 | app lifecycle / main.cpp | 11 | 10 fixed, 1 open (Machine menu, above). `floppyPending_` race (see H4); wall-clock RTC seed reaches the *active* MCU through `setRtcSeconds` (`main.cpp:1929, 2373, 2688, 2978, 3697, 4766, 5149`, `V8Memory.h:241-243`); `POM68K_CENTRIS_FPU` unset on the else branch (`main.cpp:3960-3961`); GUI floppy swap flushes the outgoing image (H4); the frame quantum is derived per machine — `const int kFrame = int(mem.cpuHz() / 60);` (`main.cpp:3528`); the LocalTalk byte pace is derived, not literal — `int byteCycles = int(mem.cpuHz() / 28800);` (`main.cpp:135-136`); `MacAudioHost::start` uninits on a failed start (`MacAudioHost.h:35-39`); AppleTalk window reads the machine-thread sample; monitor sense published through `stSense_` (`main.cpp:1774, 2270`); PRAM files tagged per profile on all twelve runners |

## What the harness got right that a single reviewer would not

Three findings were reached independently by hunts that could not see each
other — the strongest signal the method produced, and worth reproducing if it is
ever run again:

- `floppyPending_` read/cleared outside `cmdMu_` — found by H4, H9b *and* H10.
- Insert-over-dirty-floppy dropping committed writes — found by H4 and H10.
- Both were structural, not local: the fix for the first was the `MachineHost`
  CRTP consolidation (`CHANGELOG.md` 2026-08-09), which deleted the five other
  copies of the same race along with it.

Three findings also came with a **gate that locked in the defect**, so the code
fix required fixing the test in the same patch. All three are now correct:
`tests/atalk_stack_test.cpp` (ZIP request layout), `tests/toby_test.cpp` (RAMDAC
address), `tests/floppy_persist_test.cpp` (a DC42 fixture with a real tag block).
When a gate is written from the implementation rather than the oracle, it pins
the bug — check the oracle when a finding lands on tested code.
