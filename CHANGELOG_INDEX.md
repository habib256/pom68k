# CHANGELOG — index by subsystem

**Generated** by `tools/changelog_index.py` from the 254 dated entries in `CHANGELOG.md`. Do not edit by hand: regenerate. `docs_test` § 7 only compares the entry COUNT, so a stale hook or a wrong anchor here passes it — regenerate after every CHANGELOG edit.

`CHANGELOG.md` carries two indexes of its own — [by date](CHANGELOG.md#index-by-date), newest first, and [by topic](CHANGELOG.md#index-by-topic), phrased as the question a reader arrives with. This third one answers a different question: *everything that ever happened to one subsystem*.

Grouping is a keyword heuristic over each entry's hook. An entry filed under the wrong heading is a bug in the table in `tools/changelog_index.py` — add a keyword, regenerate; never edit this file.

| Subsystem | Entries |
|---|---:|
| [JIT — the second execution engine](#jit--the-second-execution-engine) | 22 |
| [CPU cores, MMU, FPU and the WinUAE oracle](#cpu-cores-mmu-fpu-and-the-winuae-oracle) | 33 |
| [MCU firmware LLE — Egret, Cuda, PIC, PG&E](#mcu-firmware-lle--egret-cuda-pic-pge) | 28 |
| [Storage — SCSI, IWM, SWIM, media](#storage--scsi-iwm-swim-media) | 37 |
| [Video — decoders, the raster beam, DAFB](#video--decoders-the-raster-beam-dafb) | 13 |
| [Sound](#sound) | 6 |
| [Serial, LocalTalk and AppleTalk](#serial-localtalk-and-appletalk) | 10 |
| [Save states](#save-states) | 6 |
| [Machine bring-ups](#machine-bring-ups) | 41 |
| [Build, packaging and release](#build-packaging-and-release) | 7 |
| [Tests, gates and measurement](#tests-gates-and-measurement) | 14 |
| [Documentation, audits and reviews](#documentation-audits-and-reviews) | 5 |
| [Cross-cutting](#cross-cutting) | 32 |

---

## JIT — the second execution engine

- **2026-07-27** — [A second execution engine: the multi-target JIT (J0 + J1)](CHANGELOG.md#2026-07-27--a-second-execution-engine-the-multi-target-jit-j0--j1)
- **2026-07-28** — [The x86-64 code generator (J2), and what it measured](CHANGELOG.md#2026-07-28--the-x86-64-code-generator-j2-and-what-it-measured)
- **2026-07-28 (fifth pass)** — [The "PGO divergence" was the U bit all along](CHANGELOG.md#2026-07-28-fifth-pass--the-pgo-divergence-was-the-u-bit-all-along)
- **2026-07-28 (fourth pass)** — [The data window and PGO: the interpreter's turn](CHANGELOG.md#2026-07-28-fourth-pass--the-data-window-and-pgo-the-interpreters-turn)
- **2026-07-28 (sixth pass)** — [The JIT reaches the 68030: the V8 family](CHANGELOG.md#2026-07-28-sixth-pass--the-jit-reaches-the-68030-the-v8-family)
- **2026-07-29 (late)** — [PGO across all four CPU families (−26 % on the LC II); the dispatch-table item measured and dropped](CHANGELOG.md#2026-07-29-pgo-four-cpu-families)
- **2026-07-30** — [JIT measured honestly: x64 wins both regimes; the next lever is 5 opcodes](CHANGELOG.md#2026-07-30--jit-measured-honestly-x64-wins-both-regimes-the-next-lever-is-5-opcodes)
- **2026-07-30** — [A JIT backend is valid per GUEST family, not just per host](CHANGELOG.md#2026-07-30--a-jit-backend-is-valid-per-guest-family-not-just-per-host)
- **2026-08-04** — [Hot floppy swap reaches every runner; release CI for four OS targets; the x64 dynamic-link regression found and fixed](CHANGELOG.md#2026-08-04-floppy-ci)
- **2026-08-04** — [AArch64 Finder gate green and fast: hidden-state lockstep plus two host-side bottlenecks removed](CHANGELOG.md#2026-08-04-a64-green-fast)
- **2026-08-06 (evening)** — [The JIT reaches the last two families, and the compacts are the first guest where the window is not free](CHANGELOG.md#2026-08-06-jit-020-000)
- **2026-08-06 (night)** — [The cycle-exact lockstep, and the same trap twice in one day: a green gate that meant "nothing ran"](CHANGELOG.md#2026-08-06-lockstep-68000)
- **2026-08-08 (third)** — [A Pi package built for ONE core: the `-mcpu` half of NeoST's workflow ports, the PGO half still cannot](CHANGELOG.md#2026-08-08-pi400-ci)
- **2026-08-10** — [Conformant-JIT chantier: phase 0 and phase A landed, and the 68030 blocker finally named](CHANGELOG.md#2026-08-10-jit-chantier-phase-a)
- **2026-08-10 (fifth)** — [The 68030 JIT block boundary now carries the exact terminal IRD/IRC](CHANGELOG.md#2026-08-10-jit-030-terminal-queue)
- **2026-08-10 (third)** — [Apple Silicon makes the first native 68030 backend provable](CHANGELOG.md#2026-08-10-jit-030-a64)
- **2026-08-16 (third)** — [JIT copyback writes cross the native boundary, with dirty-longword and format-$7 proofs attached](CHANGELOG.md#2026-08-16-jit-copyback-write)
- **2026-08-17** — [A64 and x64 stop decoding semantics behind the IR](CHANGELOG.md#2026-08-17-jit-ir-semantics)
- **2026-08-17 (later)** — [Two feature probes that each said yes, and a tree that did not build at all on x86-64](CHANGELOG.md#2026-08-17-lto-lld-combination)
- **2026-08-18** — [The indexed modes were never 167 k, and the idle Finder understates the JIT's fallback pressure 6×](CHANGELOG.md#2026-08-18-drawing-census)
- **2026-08-18 (later)** — [The 68030's JIT spends its life re-compiling: 26 544 of 28 816 whole-cache flushes are one wrapper hint](CHANGELOG.md#2026-08-18-030-flush-storm)
- **2026-08-18 (third)** — [Native residency is a symptom, not the lock: forcing it up makes the 68030 JIT 37 % slower](CHANGELOG.md#2026-08-18-residency-trap)

## CPU cores, MMU, FPU and the WinUAE oracle

- **2026-07-15** — [O6.9 resolved: GISTPERSO's vector-2 storm — RTE honors a cleared SSW.DF](CHANGELOG.md#2026-07-15--o69-resolved-gistpersos-vector-2-storm--rte-honors-a-cleared-sswdf)
- **2026-07-15** — [O5 follow-ups: 68882 timing + FRESTORE frame acceptance](CHANGELOG.md#2026-07-15--o5-follow-ups-68882-timing--frestore-frame-acceptance)
- **2026-07-15** — [Musashi oracle retired: the loop is WinUAE-solo](CHANGELOG.md#2026-07-15--musashi-oracle-retired-the-loop-is-winuae-solo)
- **2026-07-15** — [O5 slice 2: 68882 FPU execution in Moira](CHANGELOG.md#2026-07-15--o5-slice-2-68882-fpu-execution-in-moira)
- **2026-07-15** — [O4 slice 3: the 68030 MMU bus layer (Moira translates)](CHANGELOG.md#2026-07-15--o4-slice-3-the-68030-mmu-bus-layer-moira-translates)
- **2026-07-15** — [Phase 2 live: two 68030 oracles + arbitration turn 1](CHANGELOG.md#2026-07-15--phase-2-live-two-68030-oracles--arbitration-turn-1)
- **2026-07-15** — [O4 slice 1: Moira executes the 68030 MMU instructions](CHANGELOG.md#2026-07-15--o4-slice-1-moira-executes-the-68030-mmu-instructions)
- **2026-07-16** — [SimCity 2000 crash fixed: 68030 i-cache throughput model](CHANGELOG.md#2026-07-16--simcity-2000-crash-fixed-68030-i-cache-throughput-model)
- **2026-07-17** — [i-cache overlay folded into Moira's fetch path (-15%)](CHANGELOG.md#2026-07-17--i-cache-overlay-folded-into-moiras-fetch-path--15)
- **2026-07-17** — [Lode Runner launch freeze: odd-SP interrupt frames were corrupt (vendored Moira fix) + sound tempo locked to the host DAC](CHANGELOG.md#2026-07-17--lode-runner-launch-freeze-odd-sp-interrupt-frames-were-corrupt-vendored-moira-fix--sound-tempo-locked-to-the-host-dac)
- **2026-07-17** — [68030 instruction-cache timing overlay (replaces the flat boost)](CHANGELOG.md#2026-07-17--68030-instruction-cache-timing-overlay-replaces-the-flat-boost)
- **2026-07-17** — [retire the adaptive cache boost for a constant ratio](CHANGELOG.md#2026-07-17--retire-the-adaptive-cache-boost-for-a-constant-ratio)
- **2026-07-17** — [adaptive cache boost (fixes big-city SimCity crash)](CHANGELOG.md#2026-07-17--adaptive-cache-boost-fixes-big-city-simcity-crash)
- **2026-07-18** — [Q5.1c: the fatal `_sReadStruct` fully anatomised; DrHW pick proven correct; full-machine oracle blocked (round 2)](CHANGELOG.md#2026-07-18--q51c-the-fatal-_sreadstruct-fully-anatomised-drhw-pick-proven-correct-full-machine-oracle-blocked-round-2)
- **2026-07-18** — [Q3: the 68040 MMU translates in Moira — full grid 7 200/7 200 pinned, the LC 475 CPU side is complete](CHANGELOG.md#2026-07-18--q3-the-68040-mmu-translates-in-moira--full-grid-7-2007-200-pinned-the-lc-475-cpu-side-is-complete)
- **2026-07-18** — [Q2+Q4: the 68LC040 integer core executes in Moira, WinUAE-differential (5 400/5 400), no-FPU F-line included](CHANGELOG.md#2026-07-18--q2q4-the-68lc040-integer-core-executes-in-moira-winuae-differential-5-4005-400-no-fpu-f-line-included)
- **2026-07-20** — [Q8.7: 040 I/D ATC + Cpu040 throughput overlay](CHANGELOG.md#2026-07-20--q87-040-id-atc--cpu040-throughput-overlay)
- **2026-07-20** — [Q8.5: 68LC040 NOFPU path (soft FPU; bare NONE = dsNoFPU 90)](CHANGELOG.md#2026-07-20--q85-68lc040-nofpu-path-soft-fpu-bare-none--dsnofpu-90)
- **2026-07-20** — [Q6.6 RESOLVED: Mac OS 8.1 boots the Quadra 605 (68LC040) to the Finder desktop — two blockers, the FPU trap and a DMA-final-chunk STATUS race](CHANGELOG.md#2026-07-20--q66-resolved-mac-os-81-boots-the-quadra-605-68lc040-to-the-finder-desktop--two-blockers-the-fpu-trap-and-a-dma-final-chunk-status-race)
- **2026-07-21** — [Bare no-FPU solved: _FP68K binds the integer PACK 4 (Cuda XPRAM echo bug)](CHANGELOG.md#2026-07-21--bare-no-fpu-solved-_fp68k-binds-the-integer-pack-4-cuda-xpram-echo-bug)
- **2026-07-21** — [LLE step 5: UniversalInfo FPU masking deleted; bare no-FPU fully mapped](CHANGELOG.md#2026-07-21--lle-step-5-universalinfo-fpu-masking-deleted-bare-no-fpu-fully-mapped)
- **2026-07-24** — [Phase C: Quadra 650 + Quadra 610 (full 68040 on the djMEMC+IOSB machine)](CHANGELOG.md#2026-07-24--phase-c-quadra-650--quadra-610-full-68040-on-the-djmemciosb-machine)
- **2026-07-24** — [Phase C: Macintosh LC (68020) and Classic II (Eagle) boot to the Finder](CHANGELOG.md#2026-07-24--phase-c-macintosh-lc-68020-and-classic-ii-eagle-boot-to-the-finder)
- **2026-07-25** — [The i-cache boost was accelerating the VIA bus: LC III / LC III+ / IIvx fixed, and the IIsi's boost restored](CHANGELOG.md#2026-07-25--the-i-cache-boost-was-accelerating-the-via-bus-lc-iii--lc-iii--iivx-fixed-and-the-iisis-boost-restored)
- **2026-08-05 (ninth)** — [The floppy boost gate: freeze the i-cache boost to 1 while the motor runs, and the LC II mounts its first GCR floppy ever](CHANGELOG.md#2026-08-05-floppy-boost-gate)
- **2026-08-07 (later)** — [RaSCSI read as an oracle: our disk was invisible to every tool running inside the guest, and the SCSI bus could only hold disks](CHANGELOG.md#2026-08-07-rascsi-oracle)
- **2026-08-09 (later)** — [Moira is a fork, and the file now says so: the upstream exit had expired without anyone choosing it](CHANGELOG.md#2026-08-09-moira-fork)
- **2026-08-10 (eighth)** — [The fastest conformant engine becomes the 68040 default](CHANGELOG.md#2026-08-10-jit-040-default)
- **2026-08-10 (fourth)** — [The 68030 trace cost stops being a guess: base, i-cache and post-exception are separate](CHANGELOG.md#2026-08-10-jit-030-trace-cost)
- **2026-08-10 (second)** — [the 68030 blocker was two bugs, and the first one was never 68030-specific](CHANGELOG.md#2026-08-10-jit-030-blocker-two-bugs)
- **2026-08-10 (seventh)** — [The successful postincrement oracle names the first hidden RAM divergence](CHANGELOG.md#2026-08-10-jit-030-pi-success)
- **2026-08-16 (later)** — [020/030/040 closure: integrated FPU, module calls, caches and interruptible 6888x](CHANGELOG.md#2026-08-16-020-030-040-closure)
- **2026-08-19** — [The 68030 code generator beats `threaded` at the default budget: the blocker was never coverage, it was an uncharge that assumed a re-run](CHANGELOG.md#2026-08-19-030-codegen-parity)

## MCU firmware LLE — Egret, Cuda, PIC, PG&E

- **2026-07-16** — [O6.11 RESOLVED: GISTPERSO boots to the Finder — Egret XPRAM protocol fix makes AppleTalk genuinely inactive](CHANGELOG.md#2026-07-16--o611-resolved-gistperso-boots-to-the-finder--egret-xpram-protocol-fix-makes-appletalk-genuinely-inactive)
- **2026-07-17** — [SC2K "coprocesseur absent" ROOT-CAUSED AND FIXED: Egret mid-flight packet retraction manufactured ghost ADB sessions](CHANGELOG.md#2026-07-17--sc2k-coprocesseur-absent-root-caused-and-fixed-egret-mid-flight-packet-retraction-manufactured-ghost-adb-sessions)
- **2026-07-19** — [Q6.5: the boot restart loop is ACTUALLY resolved — the ROM's POST XPRAM validity read uses a THIRD Cuda framing (direct-driver GetPram)](CHANGELOG.md#2026-07-19--q65-the-boot-restart-loop-is-actually-resolved--the-roms-post-xpram-validity-read-uses-a-third-cuda-framing-direct-driver-getpram)
- **2026-07-19** — [Q6.4 + Q6.2 BOTH RESOLVED: the boot restart loop AND the block-0 loop were one coupled Cuda-reply-framing bug; the System now loads](CHANGELOG.md#2026-07-19--q64--q62-both-resolved-the-boot-restart-loop-and-the-block-0-loop-were-one-coupled-cuda-reply-framing-bug-the-system-now-loads)
- **2026-07-19** — [Q6.4 re-localized: it is a System-launch HANDOFF failure, NOT a Cuda reply-framing bug (the prior "completion ISR buffer-smash" lead is disproven; no fix landed yet)](CHANGELOG.md#2026-07-19--q64-re-localized-it-is-a-system-launch-handoff-failure-not-a-cuda-reply-framing-bug-the-prior-completion-isr-buffer-smash-lead-is-disproven-no-fix-landed-yet)
- **2026-07-19** — [Q6.2 RESOLVED: the block-0 re-read loop was a Cuda ReadXPram reply-framing divergence — the boot now loads the driver, partition map and System (progresses to a new SCSI blocker)](CHANGELOG.md#2026-07-19--q62-resolved-the-block-0-re-read-loop-was-a-cuda-readxpram-reply-framing-divergence--the-boot-now-loads-the-driver-partition-map-and-system-progresses-to-a-new-scsi-blocker)
- **2026-07-21** — [LLE step 4: Mac II EvQ soft-post deleted — alerts dismissed over real ADB](CHANGELOG.md#2026-07-21--lle-step-4-mac-ii-evq-soft-post-deleted--alerts-dismissed-over-real-adb)
- **2026-07-22** — [LLE step 7: Cuda/Egret wire-model redo (the per-reader hacks are gone)](CHANGELOG.md#2026-07-22--lle-step-7-cudaegret-wire-model-redo-the-per-reader-hacks-are-gone)
- **2026-07-22** — [Mac II LLE ADB default: mouse moves; three bugs, none where predicted](CHANGELOG.md#2026-07-22--mac-ii-lle-adb-default-mouse-moves-three-bugs-none-where-predicted)
- **2026-07-22** — [Mac II ADB goes LLE: real PIC1654S transceiver (opt-in)](CHANGELOG.md#2026-07-22--mac-ii-adb-goes-lle-real-pic1654s-transceiver-opt-in)
- **2026-07-23** — [ADB Talk R0 answers on PENDING data, not on changed bytes](CHANGELOG.md#2026-07-23--adb-talk-r0-answers-on-pending-data-not-on-changed-bytes)
- **2026-07-23** — [LC II: Egret firmware LLE back to OPT-IN (mouse starvation)](CHANGELOG.md#2026-07-23--lc-ii-egret-firmware-lle-back-to-opt-in-mouse-starvation)
- **2026-07-23** — [The LC II runs the real Egret firmware too (same day, same glue)](CHANGELOG.md#2026-07-23--the-lc-ii-runs-the-real-egret-firmware-too-same-day-same-glue)
- **2026-07-23** — [The real Cuda firmware is the Quadra's DEFAULT (blueprint step 4)](CHANGELOG.md#2026-07-23--the-real-cuda-firmware-is-the-quadras-default-blueprint-step-4)
- **2026-07-23** — [Mac OS 8.1 boots to the Finder on the REAL Cuda firmware (blueprint step 3)](CHANGELOG.md#2026-07-23--mac-os-81-boots-to-the-finder-on-the-real-cuda-firmware-blueprint-step-3)
- **2026-07-23** — [M68HC05E1 core: the real Cuda firmware executes (step 10 groundwork)](CHANGELOG.md#2026-07-23--m68hc05e1-core-the-real-cuda-firmware-executes-step-10-groundwork)
- **2026-07-24** — [Phase C: Mac Centris 650 + Centris 610 (djMEMC + IOSB, PIC1654S LLE)](CHANGELOG.md#2026-07-24--phase-c-mac-centris-650--centris-610-djmemc--iosb-pic1654s-lle)
- **2026-07-24** — [Phase C: Macintosh LC 520 — the EDE66CBD all-in-one family boots (Cuda 341S0060 LLE)](CHANGELOG.md#2026-07-24--phase-c-macintosh-lc-520--the-ede66cbd-all-in-one-family-boots-cuda-341s0060-lle)
- **2026-07-24** — [Phase C: LC 475 (68LC040 + Cuda LLE) and LC III+ (33 MHz Sonora + Egret LLE)](CHANGELOG.md#2026-07-24--phase-c-lc-475-68lc040--cuda-lle-and-lc-iii-33-mhz-sonora--egret-lle)
- **2026-07-24** — [Phase C: Color Classic (Spice + Cuda LLE) and LC III (Sonora + Egret LLE)](CHANGELOG.md#2026-07-24--phase-c-color-classic-spice--cuda-lle-and-lc-iii-sonora--egret-lle)
- **2026-07-24** — [Event-driven ADB wire: the Egret firmware LLE is the LC II DEFAULT](CHANGELOG.md#2026-07-24--event-driven-adb-wire-the-egret-firmware-lle-is-the-lc-ii-default)
- **2026-07-29** — [The Color Classic "0417 wedge" was a missing DFAC2, not a core bug; both factory Cudas land](CHANGELOG.md#2026-07-29--the-color-classic-0417-wedge-was-a-missing-dfac2-not-a-core-bug-both-factory-cudas-land)
- **2026-07-31 (night)** — [The Duo 230 boots the Finder: /PMU_INT is a LEVEL, and $E1 re-uploads the PMU firmware](CHANGELOG.md#2026-07-31-duo-finder)
- **2026-08-01 (night)** — [The Mac IIfx boots the Finder — ADB bit-banged by the IOP's own 65C02 firmware against AdbLine](CHANGELOG.md#2026-08-01-iifx-finder)
- **2026-08-02** — [The ADB device model and two SCC pins: closing the LLE gaps that needed no new hardware](CHANGELOG.md#2026-08-02-lle-devices)
- **2026-08-02 (fifth)** — [Two LLE gaps closed: the Cuda's I2C bus gets a second slave, and SWIM1 gets its DMA request line](CHANGELOG.md#2026-08-02-i2c-dat1byte)
- **2026-08-03** — [Event deadlines close the Cuda phase accommodation; extended ADB input reaches every GUI runner](CHANGELOG.md#2026-08-03-event-deadlines)
- **2026-08-14 (later)** — [The Eclipse towers run the real Egret firmware, and the input gate that came with it found they had never had a working mouse](CHANGELOG.md#2026-08-14-eclipse-egret-lle)

## Storage — SCSI, IWM, SWIM, media

- **2026-07-14** — [M5: System 6.0.5 boots to the Finder from floppy](CHANGELOG.md#2026-07-14--m5-system-605-boots-to-the-finder-from-floppy)
- **2026-07-15** — [M7: System 6 boots from a SCSI hard disk](CHANGELOG.md#2026-07-15--m7-system-6-boots-from-a-scsi-hard-disk)
- **2026-07-16** — [SCSI write-back (persist guest disk writes)](CHANGELOG.md#2026-07-16--scsi-write-back-persist-guest-disk-writes)
- **2026-07-17** — [LC II runs on a dedicated machine thread; boot & secondary SCSI volumes selectable from a "Disques" menu](CHANGELOG.md#2026-07-17--lc-ii-runs-on-a-dedicated-machine-thread-boot--secondary-scsi-volumes-selectable-from-a-disques-menu)
- **2026-07-18** — [Q6.1: 53C96 pseudo-DMA reads work — the Mac OS 8.1 SCSI driver now transfers full 512-byte blocks off the disk](CHANGELOG.md#2026-07-18--q61-53c96-pseudo-dma-reads-work--the-mac-os-81-scsi-driver-now-transfers-full-512-byte-blocks-off-the-disk)
- **2026-07-18** — [Q5.1d: Q5 Slot-Manager blocker RESOLVED — the missing MEMCjr DAFB bus-holding split; boot now drives the SCSI bus](CHANGELOG.md#2026-07-18--q51d-q5-slot-manager-blocker-resolved--the-missing-memcjr-dafb-bus-holding-split-boot-now-drives-the-scsi-bus)
- **2026-07-18** — [Q6: NCR 53C96 wired into the Quadra 605 + the sReadWord producer chain pinned (boot-integration round 1)](CHANGELOG.md#2026-07-18--q6-ncr-53c96-wired-into-the-quadra-605--the-sreadword-producer-chain-pinned-boot-integration-round-1)
- **2026-07-19** — [Q6.5d RESOLVED: dsBadPatch(99) was a 53C96 FIFO-count lie that sent the OS SCSI Manager's resource read into its DISCARD engine](CHANGELOG.md#2026-07-19--q65d-resolved-dsbadpatch99-was-a-53c96-fifo-count-lie-that-sent-the-os-scsi-managers-resource-read-into-its-discard-engine)
- **2026-07-19** — [Q6.5b/c: the async SCSI SIM crash + the SCC/reselection spin are BOTH fixed — the boot loads System, applies patches, stops at dsBadPatch](CHANGELOG.md#2026-07-19--q65bc-the-async-scsi-sim-crash--the-sccreselection-spin-are-both-fixed--the-boot-loads-system-applies-patches-stops-at-dsbadpatch)
- **2026-07-19** — [Q6.3 RESOLVED: SCSI multi-block read — the polled ($10) Transfer Info needed the DATA IN bus-service interrupt](CHANGELOG.md#2026-07-19--q63-resolved-scsi-multi-block-read--the-polled-10-transfer-info-needed-the-data-in-bus-service-interrupt)
- **2026-07-20** — [SCSI flat-HFS façade](CHANGELOG.md#2026-07-20--scsi-flat-hfs-façade)
- **2026-07-20** — [Mac II: prefer SCSI boot over empty floppy](CHANGELOG.md#2026-07-20--mac-ii-prefer-scsi-boot-over-empty-floppy)
- **2026-07-20** — [Mac II: StartBoot wantType=$FF was skipping Apple_HFS](CHANGELOG.md#2026-07-20--mac-ii-startboot-wanttypeff-was-skipping-apple_hfs)
- **2026-07-20** — [Q8.6: SWIM2 SuperDrive media (MFM 1.44 + GCR)](CHANGELOG.md#2026-07-20--q86-swim2-superdrive-media-mfm-144--gcr)
- **2026-07-20** — [Q8.4: SWIM2 register/FIFO core replaces the zero stub](CHANGELOG.md#2026-07-20--q84-swim2-registerfifo-core-replaces-the-zero-stub)
- **2026-07-21** — [Q605 Sys 7.5.5 / 7.6 → Finder (53C96 polled WRITE)](CHANGELOG.md#2026-07-21-q605-sys755-76-finder)
- **2026-07-22** — [dir2hfs: host folder → desktop volume (data-only flat-HFS façade)](CHANGELOG.md#2026-07-22-dir2hfs)
- **2026-07-23** — [IWM write engine + GCR write-back: floppies are writable](CHANGELOG.md#2026-07-23--iwm-write-engine--gcr-write-back-floppies-are-writable)
- **2026-07-23** — [Mechanical drive sounds (floppy + SCSI hard disk)](CHANGELOG.md#2026-07-23--mechanical-drive-sounds-floppy--scsi-hard-disk)
- **2026-07-23** — [SWIM2: the real cell engines (MFM cell timing + CRC)](CHANGELOG.md#2026-07-23--swim2-the-real-cell-engines-mfm-cell-timing--crc)
- **2026-07-23** — [LLE step 9 (partial): TurboSCSI wait-state cell + 53C96 scheduled delays](CHANGELOG.md#2026-07-23--lle-step-9-partial-turboscsi-wait-state-cell--53c96-scheduled-delays)
- **2026-07-24** — [Floppy write persistence (gate `floppy_persist_test`)](CHANGELOG.md#2026-07-24--floppy-write-persistence-gate-floppy_persist_test)
- **2026-07-25** — [Macintosh Quadra 700: the 27th machine, and the DAFB TurboSCSI cell](CHANGELOG.md#2026-07-25--macintosh-quadra-700-the-27th-machine-and-the-dafb-turboscsi-cell)
- **2026-07-29 (later)** — [SCSI CD-ROM support, a guest-level floppy gate, and the LLE inventory re-synced](CHANGELOG.md#2026-07-29-later--scsi-cd-rom-support-a-guest-level-floppy-gate-and-the-lle-inventory-re-synced)
- **2026-08-04 (soir)** — [The IIfx SCSI mirror mounted one volume seven times; CDs hot-mount under 8.1; the MacIP window opens up](CHANGELOG.md#2026-08-04-iifx-mirror-cd-hot)
- **2026-08-05 (eighth)** — [What the driver gives up ON: badDCksum on the MDB, because the boost compresses Apple's denibble path below the IWM's 14-tick hold and the mid-group poll re-reads the same nibble](CHANGELOG.md#2026-08-05-sony-giveup)
- **2026-08-05 (fifth)** — [Beyond-boot reaches a second machine: Quadra 605 soak + persist, and the 53C96 finally takes a real guest WRITE](CHANGELOG.md#2026-08-05-q605-beyond)
- **2026-08-05 (fourth)** — [IWM/SWIM bughunt: the Q700 spindle ran 1.6x fast, and the IWM personality was half-speed-blind on C15M hosts](CHANGELOG.md#2026-08-05-iwm-swim-bughunt)
- **2026-08-05 (seventh)** — [The floppy refusal is boost-triggered: at boost 1-2 the LC II mounts, at 3-4 it calls the disk unreadable (mechanism still open)](CHANGELOG.md#2026-08-05-boost-floppy)
- **2026-08-05 (sixth)** — [The LC II floppy "mount" was the init dialog all along; SWIM1-IWM never mounts, and Cmd-N was pressing [Eject]](CHANGELOG.md#2026-08-05-lcii-floppy-dialog)
- **2026-08-09 (seventh)** — [The GUI pass: the Quadra booted System 6.0.5 off a floppy nobody asked for, and no gate could ever have seen it](CHANGELOG.md#2026-08-09-gui-pass)
- **2026-08-13 (seventh)** — [The IIfx dirty-volume refusal was a swallowed VBL disable, and "address 1" was open bus wearing a wrapped PC](CHANGELOG.md#2026-08-13-iifx-toby-vbl-disable)
- **2026-08-14** — [The Duo's last beyond-boot leg: a power flag that never let it reboot, a trackball that was never wired, and a volume this machine will not flush on its own](CHANGELOG.md#2026-08-14-duo-beyond-boot)
- **2026-08-14 (eighth)** — [The flux plan is finished: the medium stops being a cell grid, and the IWM reads transitions](CHANGELOG.md#2026-08-14-flux-store-iwm)
- **2026-08-14 (fifth)** — [SWIM1's ISM read engine is MAME's real one: LS-pair classification, the Correction State Machine live, and the param RAM becomes load-bearing](CHANGELOG.md#2026-08-14-ism-csm)
- **2026-08-14 (fourth)** — [The SWIM read engines get their data separator: FluxPll over a flux view of the Sony track, and the off-rate gate is the one that bites](CHANGELOG.md#2026-08-14-flux-separator)
- **2026-08-15 (third)** — [The IWM's cell window was counted in the wrong clock, and no floppy had mounted anywhere but the compacts since](CHANGELOG.md#2026-08-15-iwm-window-clock)

## Video — decoders, the raster beam, DAFB

- **2026-07-15** — [O6: the LC II ROM boots to the blinking-? screen](CHANGELOG.md#2026-07-15--o6-the-lc-ii-rom-boots-to-the-blinking--screen)
- **2026-07-16** — [LC II GUI showed a black screen (texture alpha)](CHANGELOG.md#2026-07-16--lc-ii-gui-showed-a-black-screen-texture-alpha)
- **2026-07-18** — [Q5.1a: the Slot-Manager blocker re-localized — the DAFB-sense theory is disproven, the fault is a decl-ROM parse](CHANGELOG.md#2026-07-18--q51a-the-slot-manager-blocker-re-localized--the-dafb-sense-theory-is-disproven-the-fault-is-a-decl-rom-parse)
- **2026-07-20** — [Q8.1: DAFB stride/depth model and 256-color host rendering](CHANGELOG.md#2026-07-20--q81-dafb-stridedepth-model-and-256-color-host-rendering)
- **2026-07-21** — [DAFB extracted into Dafb.h/.cpp (one concern per file)](CHANGELOG.md#2026-07-21--dafb-extracted-into-dafbhcpp-one-concern-per-file)
- **2026-07-21** — [LLE step 6: DAFB toward MAME parity (Swatch CRTC, Gazelle, sense)](CHANGELOG.md#2026-07-21--lle-step-6-dafb-toward-mame-parity-swatch-crtc-gazelle-sense)
- **2026-07-23** — [Toby: CRTC-derived frame clock + the register file actually writes](CHANGELOG.md#2026-07-23--toby-crtc-derived-frame-clock--the-register-file-actually-writes)
- **2026-07-25** — [Macintosh Quadra 630 / LC 580: F108 + Valkyrie, the last 68k desktop board](CHANGELOG.md#2026-07-25--macintosh-quadra-630--lc-580-f108--valkyrie-the-last-68k-desktop-board)
- **2026-07-27** — [Three DAFB clock generators, the pseudo-VIA's second flavour, two GUI races](CHANGELOG.md#2026-07-27--three-dafb-clock-generators-the-pseudo-vias-second-flavour-two-gui-races)
- **2026-07-31 (late night, later)** — [`duo230_boot_etalon` GREEN: milestone 3 gated, the GSC decoder lands](CHANGELOG.md#2026-07-31-duo-gate)
- **2026-08-02 (later)** — [The raster beam: nine video decoders stop painting the whole frame at once](CHANGELOG.md#2026-08-02-beam)
- **2026-08-14 (ninth)** — [The Toby CLUT stored a grey per write: a red boot etalon that was a real bug, and a gate that pinned it](CHANGELOG.md#2026-08-14-toby-clut-mouse)
- **2026-08-14 (third)** — [The Eclipse gets a beyond-boot pair of its own, on the argument that a second profile is a different machine past the boot screen](CHANGELOG.md#2026-08-14-eclipse-beyond-boot)

## Sound

- **2026-07-15** — [M6: the startup chime plays](CHANGELOG.md#2026-07-15--m6-the-startup-chime-plays)
- **2026-07-17** — [Performance pass: 0.40× → 1.91× realtime at the Finder (the sound stutter was the emulator falling behind real time)](CHANGELOG.md#2026-07-17-performance-pass-realtime)
- **2026-07-17** — [app sound reaches the ASC (pseudo-VIA ASC IRQ was edge-only)](CHANGELOG.md#2026-07-17--app-sound-reaches-the-asc-pseudo-via-asc-irq-was-edge-only)
- **2026-07-20** — [Classic ASC idle IRQ (Mac II)](CHANGELOG.md#2026-07-20--classic-asc-idle-irq-mac-ii)
- **2026-07-20** — [Q8.2: Quadra 605 PrimeTime/IOSB ASC stereo](CHANGELOG.md#2026-07-20--q82-quadra-605-primetimeiosb-asc-stereo)
- **2026-07-20** — [Q7: Quadra 605 GUI profile, audio and ROM discovery](CHANGELOG.md#2026-07-20--q7-quadra-605-gui-profile-audio-and-rom-discovery)

## Serial, LocalTalk and AppleTalk

- **2026-07-16** — [O6.11: LocalTalk LAP — SCC abort stream + HLE watchdog](CHANGELOG.md#2026-07-16--o611-localtalk-lap--scc-abort-stream--hle-watchdog)
- **2026-07-20** — [Mac II Sys7 → Finder (AppleTalk alert dismiss)](CHANGELOG.md#2026-07-20-macii-sys7-finder)
- **2026-07-20** — [O6.13: SCC word fast path + LC II NOFPU diagnosis](CHANGELOG.md#2026-07-20--o613-scc-word-fast-path--lc-ii-nofpu-diagnosis)
- **2026-07-21** — [LLE step 3: real LLAP carrier sense — LocalTalk watchdogs deleted](CHANGELOG.md#2026-07-21--lle-step-3-real-llap-carrier-sense--localtalk-watchdogs-deleted)
- **2026-07-22** — [AppleShare bridge vendored: netatalk 2.4.9 + TashRouter](CHANGELOG.md#2026-07-22--appleshare-bridge-vendored-netatalk-249--tashrouter)
- **2026-07-22** — [LLAP two-System etalon: real address acquisition between two Systems](CHANGELOG.md#2026-07-22--llap-two-system-etalon-real-address-acquisition-between-two-systems)
- **2026-07-22** — [LLAP milestone 1: SCC receive path + LToUDP virtual cable](CHANGELOG.md#2026-07-22--llap-milestone-1-scc-receive-path--ltoudp-virtual-cable)
- **2026-07-23** — [SCC Tx/Rx engine: the wire gets a real transmitter (Medium tier)](CHANGELOG.md#2026-07-23--scc-txrx-engine-the-wire-gets-a-real-transmitter-medium-tier)
- **2026-07-23** — [SCC async-baud machinery: the guest programs the wire pace now](CHANGELOG.md#2026-07-23--scc-async-baud-machinery-the-guest-programs-the-wire-pace-now)
- **2026-07-24** — [AppleTalk moves in-process: node/router + AppleShare + LaserWriter + MacIP, one GUI window](CHANGELOG.md#2026-07-24--appletalk-moves-in-process-noderouter--appleshare--laserwriter--macip-one-gui-window)

## Save states

- **2026-07-30** — [`q605_savestate_etalon`: real-OS restore determinism on the 040 side](CHANGELOG.md#2026-07-30--q605_savestate_etalon-real-os-restore-determinism-on-the-040-side)
- **2026-07-30** — [Save states in the GUI: « Sauver / Restaurer l'état »](CHANGELOG.md#2026-07-30-save-states-gui)
- **2026-07-30** — [Save states survive the real Finder: `lcii_savestate_etalon`](CHANGELOG.md#2026-07-30--save-states-survive-the-real-finder-lcii_savestate_etalon)
- **2026-07-30** — [Save states: the archive core + the whole LC II tree](CHANGELOG.md#2026-07-30--save-states-the-archive-core--the-whole-lc-ii-tree)
- **2026-08-01 (late night)** — [The IIfx is the 34th profile: GUI, save states, and an input gate whose thresholds were measured, not invented](CHANGELOG.md#2026-08-01-iifx-profile)
- **2026-08-12 (later)** — [The red savestate gate was the engine default flip, and the leak was a 68010 frame buffer](CHANGELOG.md#2026-08-12-savestate-writebuffer)

## Machine bring-ups

- **2026-07-14** — [M4 complete: cycle-accurate boot hardware](CHANGELOG.md#2026-07-14--m4-complete-cycle-accurate-boot-hardware)
- **2026-07-14** — [M0–M3.5 + first real-ROM boot](CHANGELOG.md#2026-07-14-m0-m35-first-rom-boot)
- **2026-07-15** — [O6: **Mac LC II boots to the Finder desktop**](CHANGELOG.md#2026-07-15--o6-mac-lc-ii-boots-to-the-finder-desktop)
- **2026-07-15** — [O6 (LC II machine): first six slices](CHANGELOG.md#2026-07-15--o6-lc-ii-machine-first-six-slices)
- **2026-07-16** — [LC II keyboard: arrow keys + numeric keypad](CHANGELOG.md#2026-07-16--lc-ii-keyboard-arrow-keys--numeric-keypad)
- **2026-07-16** — [LC II color (8 bpp by default) + peripheral-tick batching](CHANGELOG.md#2026-07-16--lc-ii-color-8-bpp-by-default--peripheral-tick-batching)
- **2026-07-16** — [review fixes (8-angle bug hunt) + UI: mouse capture, drag fix, machine menu](CHANGELOG.md#2026-07-16--review-fixes-8-angle-bug-hunt--ui-mouse-capture-drag-fix-machine-menu)
- **2026-07-17** — [LC II GUI defaults to 640×480](CHANGELOG.md#2026-07-17-lcii-gui-640x480)
- **2026-07-18** — [GISTPERSO (7.5) boot hang: heap corruption racing an app launch at Finder startup — NOT the pending changes, NOT the disk](CHANGELOG.md#2026-07-18--gistperso-75-boot-hang-heap-corruption-racing-an-app-launch-at-finder-startup--not-the-pending-changes-not-the-disk)
- **2026-07-19** — [Q6.4 deeply localized: the console divert is a periodic boot-RESTART loop, not a fault — several candidates ruled out (no fix yet)](CHANGELOG.md#2026-07-19--q64-deeply-localized-the-console-divert-is-a-periodic-boot-restart-loop-not-a-fault--several-candidates-ruled-out-no-fix-yet)
- **2026-07-20** — [LC II Sys 7.1 / 7.5.5 → Finder (SPConfig clamp)](CHANGELOG.md#2026-07-20-lcii-sys71-755-finder)
- **2026-07-20** — [Mac II boots System 6 to the Finder](CHANGELOG.md#2026-07-20--mac-ii-boots-system-6-to-the-finder)
- **2026-07-20** — [Q8.3: Quadra 605 whole-machine boot gate](CHANGELOG.md#2026-07-20--q83-quadra-605-whole-machine-boot-gate)
- **2026-07-21** — [LLE step 2: per-tick SPConfig clamps removed (all three machines)](CHANGELOG.md#2026-07-21--lle-step-2-per-tick-spconfig-clamps-removed-all-three-machines)
- **2026-07-21** — [LLE step 1: Mac II boots an UNMODIFIED ROM (RTC was mute, then bit-shifted)](CHANGELOG.md#2026-07-21--lle-step-1-mac-ii-boots-an-unmodified-rom-rtc-was-mute-then-bit-shifted)
- **2026-07-21** — [Finder matrix Phase A complete (all four machines)](CHANGELOG.md#2026-07-21--finder-matrix-phase-a-complete-all-four-machines)
- **2026-07-24** — [Beyond-boot gates on the LC II + a clock-drift bug they caught](CHANGELOG.md#2026-07-24--beyond-boot-gates-on-the-lc-ii--a-clock-drift-bug-they-caught)
- **2026-07-24** — [Phase C: LC 550 and Color Classic II — the AIO family fans out](CHANGELOG.md#2026-07-24--phase-c-lc-550-and-color-classic-ii--the-aio-family-fans-out)
- **2026-07-25** — [Macintosh SE, SE FDHD and Classic: three machines for one enum](CHANGELOG.md#2026-07-25--macintosh-se-se-fdhd-and-classic-three-machines-for-one-enum)
- **2026-07-25** — [Quadra 800 (26th machine), the 040 boost ceiling lifted, and the PIC co-step un-boosted](CHANGELOG.md#2026-07-25--quadra-800-26th-machine-the-040-boost-ceiling-lifted-and-the-pic-co-step-un-boosted)
- **2026-07-25** — [Doc sync + status pass: 25 machines, 90 gates, what is actually left](CHANGELOG.md#2026-07-25--doc-sync--status-pass-25-machines-90-gates-what-is-actually-left)
- **2026-07-25** — [Five more machines: Mac TV, IIsi, IIci, IIx, IIcx](CHANGELOG.md#2026-07-25--five-more-machines-mac-tv-iisi-iici-iix-iicx)
- **2026-07-27** — [The Macintosh TV boots again: a 2 % MCU shift is a deadlock](CHANGELOG.md#2026-07-27--the-macintosh-tv-boots-again-a-2--mcu-shift-is-a-deadlock)
- **2026-07-29 (evening)** — [A CD mounts in the guest; .cue/.bin; and why 8.6 cannot boot](CHANGELOG.md#2026-07-29-evening--a-cd-mounts-in-the-guest-cuebin-and-why-86-cannot-boot)
- **2026-07-30** — [Save-state fan-out: all 10 machine families serialize](CHANGELOG.md#2026-07-30--save-state-fan-out-all-10-machine-families-serialize)
- **2026-07-31 (late night)** — [The SE/30 lands as the 33rd profile: wiring only, Finder on the first run](CHANGELOG.md#2026-07-31-se30)
- **2026-08-01** — [The IIfx/Quadra-900 IOP brick opens: the R65C02 core lands, and it needed no dump](CHANGELOG.md#2026-08-01-r65c02)
- **2026-08-01 (M7)** — [Quadra 900: the Eclipse platform lands and both IOPs run — the wall is a BRK inside byte-perfect firmware](CHANGELOG.md#2026-08-01-q900)
- **2026-08-01 (evening)** — [IOP M3: the IIfx POSTs — both IOP firmwares upload byte-perfect, and the boot scan reads the disk](CHANGELOG.md#2026-08-01-iifx-post)
- **2026-08-02 (fourth)** — [Both Quadra towers boot the Finder: the IOP's BRK was a 68k word READ, not a 65C02 bug](CHANGELOG.md#2026-08-02-q900-finder)
- **2026-08-02 (sixth)** — [The "Quadra modifier bug" retracted: same machine, other image, works](CHANGELOG.md#2026-08-02-cmdn-retracted)
- **2026-08-06 (late night)** — [The IIfx closes the set: every CPU wrapper carries an engine — and the "regression" was a corrupted disk image](CHANGELOG.md#2026-08-06-jit-iifx)
- **2026-08-06 (later)** — [The PRAM finally survives the session on all eleven boards, and the Duo becomes the 37th profile — the first laptop](CHANGELOG.md#2026-08-06-duo-profile)
- **2026-08-09 (ninth)** — [The LC II never was at ×1.3, and the window exit nobody had priced costs 43 ns](CHANGELOG.md#2026-08-09-speed-baseline)
- **2026-08-09 (third)** — [Six copies of the GUI ↔ machine-thread contract became one, and the thing that had never been testable got a gate](CHANGELOG.md#2026-08-09-machinehost)
- **2026-08-13 (fifth)** — [Beyond-boot for the whole roster: sixteen gates, one engine, and the campaign paid twice before it was even green](CHANGELOG.md#2026-08-13-beyond-boot-roster)
- **2026-08-13 (fourth)** — [The first RBV machine past boot: the missing piece was an instrument, and its first run validated itself](CHANGELOG.md#2026-08-13-iisi-beyond-boot)
- **2026-08-13 (sixth)** — [The beyond-boot reds were six different causes wearing one hypothesis, and the Duo was dead rather than slow](CHANGELOG.md#2026-08-13-beyond-boot-reds)
- **2026-08-15** — [Three red boot gates and one bit: the fixture was cleanly unmounted, and the criterion was a fixture reading](CHANGELOG.md#2026-08-15-hd20sc-clean-bit)
- **2026-08-15 (later)** — [The Duo's persist leg had been gesturing at Stickies, and the guest said so all along](CHANGELOG.md#2026-08-15-duo-stickies-front)
- **2026-08-19 (third)** — [The C.5 flip is written, and its first tier run stopped it: the IIsi dies under the generator — code the `jit_*` 030 gates never ran](CHANGELOG.md#2026-08-19-c5-blocked-iisi)

## Build, packaging and release

- **2026-08-08** — [NeoST's Pi recipe, ported: LTO had been coupled to `-march=native`, so every released binary shipped without it](CHANGELOG.md#2026-08-08-raspberry-pi)
- **2026-08-08 (fourth)** — [`-mcpu=cortex-a72` produced byte-identical code to `-mtune=cortex-a72`: the Pi package's whole premise, measured and mostly refuted](CHANGELOG.md#2026-08-08-mcpu-identical)
- **2026-08-08 (later)** — [The AppImage ignored a `roms/` folder sitting right next to it — the launcher had already chdir'd elsewhere](CHANGELOG.md#2026-08-08-appimage-datadir)
- **2026-08-15 (fourth)** — [Two things the GUI was not being told: that the guest ejected the disk, and where the user put the windows](CHANGELOG.md#2026-08-15-gui-media-and-dock)
- **2026-08-17 (fifth)** — [Nothing was linking the core under LTO, and the shape the release ships takes 87 minutes](CHANGELOG.md#2026-08-17-nightly-lto-core)
- **2026-08-17 (third)** — [The 2026-08-08 knob split was half done: LTO's *default* still followed NATIVE](CHANGELOG.md#2026-08-17-lto-default-on)
- **2026-08-19 (second)** — [A full-parallel LTO make froze the host: with LTO the memory spike is the LINK, and an interrupted make leaves binaries that lie](CHANGELOG.md#2026-08-19-make-lto-freeze)

## Tests, gates and measurement

- **2026-07-14** — [M4.5: SingleStepTests/680x0 — 1 000 058 / 1 000 060](CHANGELOG.md#2026-07-14--m45-singlesteptests680x0--1-000-058--1-000-060)
- **2026-07-21** — [Plus keyboard regression (6522 SR auto-shift) + nofpu gate floor](CHANGELOG.md#2026-07-21--plus-keyboard-regression-6522-sr-auto-shift--nofpu-gate-floor)
- **2026-07-28 (third pass)** — [The density work, and what it finally measured](CHANGELOG.md#2026-07-28-third-pass--the-density-work-and-what-it-finally-measured)
- **2026-07-29** — [Input-delivery gates for the 030 families; loud HLE fallbacks (and a retracted "bug")](CHANGELOG.md#2026-07-29--input-delivery-gates-for-the-030-families-loud-hle-fallbacks-and-a-retracted-bug)
- **2026-07-31** — [The ten-month red gate was Slow Keys: the GUEST was rejecting the keys](CHANGELOG.md#2026-07-31-slow-keys)
- **2026-08-03** — [Three items closed by measurement — and a GREEN ctest that proved nothing](CHANGELOG.md#2026-08-03-three-items)
- **2026-08-05 (later)** — [The M1 bughunt: three real defects the gates were green over](CHANGELOG.md#2026-08-05-cache040-bughunt)
- **2026-08-07** — [162/162 on a fully rebuilt tree: the first complete run since the gate count went from 143 to 162](CHANGELOG.md#2026-08-07-full-run)
- **2026-08-09** — [A red gate can now say for itself whether the image moved: 60 gates print a SHA-256 and drVolAtrb before booting](CHANGELOG.md#2026-08-09-asset-fingerprint)
- **2026-08-09 (eighth)** — [One folder probe instead of three, and a gate for the thing three gates judge on](CHANGELOG.md#2026-08-09-folderprobe)
- **2026-08-09 (fifth)** — [Four gates carried no label at all, and the folder the persist gate said was never created was there all along](CHANGELOG.md#2026-08-09-tiers-and-gates)
- **2026-08-16** — [Ten red gates, five causes, and the two that were never going to be found by reading](CHANGELOG.md#2026-08-16-ten-red-gates)
- **2026-08-18 (fourth)** — [The measurement method measured itself: the floor was 6× too loose, the freshness guard cried wolf, and the A/B is now ABBA in one process](CHANGELOG.md#2026-08-18-method-audit)
- **2026-08-19 (fourth)** — [A doc/code consistency pass, and the half of it that is now a gate](CHANGELOG.md#2026-08-19-doc-code-consistency)

## Documentation, audits and reviews

- **2026-07-17** — [adversarial subsystem audit #2: 9 correctness fixes](CHANGELOG.md#2026-07-17--adversarial-subsystem-audit-2-9-correctness-fixes)
- **2026-07-17** — [adversarial subsystem audit: 3 correctness fixes](CHANGELOG.md#2026-07-17--adversarial-subsystem-audit-3-correctness-fixes)
- **2026-07-22** — [`docs/LLE_VS_HLE.md` third pass: inventory re-synced to the live tree](CHANGELOG.md#2026-07-22--docslle_vs_hlemd-third-pass-inventory-re-synced-to-the-live-tree)
- **2026-07-23** — [LLE audit: step 9 closed, the quick wins are exhausted](CHANGELOG.md#2026-07-23--lle-audit-step-9-closed-the-quick-wins-are-exhausted)
- **2026-08-13** — [Four audit closures, and two of them were not what the audit said they were](CHANGELOG.md#2026-08-13-f2-f5-closures)

## Cross-cutting

- **2026-07-14** — [M5.5: the Finder is drivable (keyboard + mouse)](CHANGELOG.md#2026-07-14--m55-the-finder-is-drivable-keyboard--mouse)
- **2026-07-15** — [Basilisk II knowledge applied: rominfo, XPRAM defaults](CHANGELOG.md#2026-07-15--basilisk-ii-knowledge-applied-rominfo-xpram-defaults)
- **2026-07-15** — [O4 slice 4: integer-family arbitration (O4 complete)](CHANGELOG.md#2026-07-15--o4-slice-4-integer-family-arbitration-o4-complete)
- **2026-07-16** — [Selectable resolution (512×384 / 640×480) + per-monitor depth](CHANGELOG.md#2026-07-16-selectable-resolution)
- **2026-07-17** — [Lode Runner "dead arrow keys": not a bug — the game binds the numeric keypad by default](CHANGELOG.md#2026-07-17--lode-runner-dead-arrow-keys-not-a-bug--the-game-binds-the-numeric-keypad-by-default)
- **2026-07-19** — [[superseded within the day] Q6.4 root-caused, un-masking Q6.2](CHANGELOG.md#2026-07-19--superseded-within-the-day-q64-root-caused-un-masking-q62)
- **2026-07-20** — [Mac II: overlay is a one-way latch](CHANGELOG.md#2026-07-20--mac-ii-overlay-is-a-one-way-latch)
- **2026-07-20** — [Mac II: PDMA $50F060xx must decode A0..A1](CHANGELOG.md#2026-07-20--mac-ii-pdma-50f060xx-must-decode-a0a1)
- **2026-07-20** — [Q8.8: CACHE_BOOST calibration (default stays 1)](CHANGELOG.md#2026-07-20--q88-cache_boost-calibration-default-stays-1)
- **2026-07-21** — [Q605 Sys 7.5 / GISTPERSO Finder at 1bpp; 7.5.5/7.6 hang](CHANGELOG.md#2026-07-21--q605-sys-75--gistperso-finder-at-1bpp-75576-hang)
- **2026-07-28** — [LLE step 7: the virgin line reads clean; `POM68K_SCC_CLEANLINE` retired](CHANGELOG.md#2026-07-28--lle-step-7-the-virgin-line-reads-clean-pom68k_scc_cleanline-retired)
- **2026-07-28 (eighth pass)** — [O(1) probes, per-space eviction, and the 020 seam](CHANGELOG.md#2026-07-28-eighth-pass--o1-probes-per-space-eviction-and-the-020-seam)
- **2026-07-28 (later)** — [Block linking, and LINK/UNLK/NOP out of the exclusion list](CHANGELOG.md#2026-07-28-later--block-linking-and-linkunlknop-out-of-the-exclusion-list)
- **2026-07-28 (seventh pass)** — [All four 030 families under the engine](CHANGELOG.md#2026-07-28-seventh-pass--all-four-030-families-under-the-engine)
- **2026-07-30** — [The five opcodes, same day: MOVEM + DBcc + JMP compiled](CHANGELOG.md#2026-07-30--the-five-opcodes-same-day-movem--dbcc--jmp-compiled)
- **2026-07-30** — [Engine re-baseline (idle host) + the CPU menu reaches the 030s](CHANGELOG.md#2026-07-30--engine-re-baseline-idle-host--the-cpu-menu-reaches-the-030s)
- **2026-07-31** — [Two negative results, recorded on purpose](CHANGELOG.md#2026-07-31-two-negative-results)
- **2026-07-31** — [The window-churn investigation ends on one deleted line: −23 to −33 %](CHANGELOG.md#2026-07-31-window-churn-dtlb-flush)
- **2026-08-01 (later)** — [IOP M2: the Apple PIC device lands — a window-uploaded 65C02 program talks both mailbox directions](CHANGELOG.md#2026-08-01-applepic)
- **2026-08-02 (third)** — [Two rates that were rounded, and one that was only ever a price](CHANGELOG.md#2026-08-02-eclock-asc)
- **2026-08-04** — [Event deadlines reach six more platforms: min(MCU bound, historical batch)](CHANGELOG.md#2026-08-04-deadlines-six)
- **2026-08-05** — [Cache 040 M1: CINV and CPUSH finally act on real state, and the tags cost nothing](CHANGELOG.md#2026-08-05-cache040-m1)
- **2026-08-05 (third)** — [The m040 sweep is paid and the cache chantier closes at M1](CHANGELOG.md#2026-08-05-cache040-closed)
- **2026-08-06** — [A chip-by-chip parity sweep against MAME master: 94 findings worked, and the three bugs it found were the ones nobody wrote down](CHANGELOG.md#2026-08-06-mame-parity-sweep)
- **2026-08-10 (sixth)** — [Restartable destinations split: predecrement and brief index land; postincrement stays closed](CHANGELOG.md#2026-08-10-jit-030-restart-ea)
- **2026-08-12** — [One opcode clears the conservative AArch64 store guard](CHANGELOG.md#2026-08-12-a64-b592-store)
- **2026-08-13 (later)** — [The peripheral deadline reaches the last two wrappers, measures worse than the batch, and ships off](CHANGELOG.md#2026-08-13-periph-deadline-optin)
- **2026-08-13 (third)** — [writeBuffer has no siblings, the determinism check is why, and an engine diff would have said so wrongly](CHANGELOG.md#2026-08-13-chunk-asymmetry-audit)
- **2026-08-14 (seventh)** — [Which dump, not only which side: one firmware search for all eight devices, and a per-device picker in the window](CHANGELOG.md#2026-08-14-firmware-picker)
- **2026-08-14 (sixth)** — ["Never silent" was only true on stderr: the Périphériques window makes every LLE/HLE fallback visible, and manually selectable](CHANGELOG.md#2026-08-14-peripheral-window)
- **2026-08-15 (fifth)** — [Mounting a CD stops being a procedure, and the discs that never mounted finally say why](CHANGELOG.md#2026-08-15-cd-like-a-floppy)
- **2026-08-17 (fourth)** — [The suite is sequential by habit, not by constraint, and now says what it costs](CHANGELOG.md#2026-08-17-gate-scheduling-cost)

