# 68040 cache content, copyback, snooping and timing — M0-M3 complete

*Opened 2026-08-04; M1 landed and was hardened on 2026-08-05. The
initial decision to stop at tags was superseded by the explicit request
to complete the 68040 model; M2/M3 and timing landed on 2026-08-16.
Pattern: `IOP_BRINGUP.md` / `DUO_BRINGUP.md` — recon first, milestones
gated, nothing implemented before its observable is named.*

## 0. What the recon changed about this chantier

The chantier opened because "040 copyback/snooping" was then billed as the
largest remaining CPU inexactness. Two findings reframe what that work
actually is (and are why it is no longer a pending item in `TODO.md`):

1. **There is no oracle.** The vendored WinUAE oracle models a 68030
   data cache (`oracle/uae/upstream/cpummu030.h`,
   `write_dcache030_lrmw_mmu`) and serves 040 data straight from memory
   as the harness drives it. *Correction (2026-09-12): the 2026-08-04
   recon was wrong that no `*_dcache040` exists — `newcpu.c` does carry
   an 040/060 data-cache model (`oracle/uae/upstream/newcpu.c:11091`,
   `read_dcache040` at `:11125`, `write_dcache040` at `:11172`). It is
   reached only through the accessors WinUAE installs under
   `currprefs.cpu_data_cache` (`:1255`), a flag the POM68K harness
   clears (`oracle/uae/glue.c:98`) — so the ruling below is unchanged,
   only its reason is.* Do not be fooled by `struct cache040` /
   `fill_icache040` (`oracle/uae/upstream/newcpu.h:160,726`) either:
   that pair is the **instruction** cache. The house
   rule "on spec/oracle conflict, the oracle wins" has no oracle to
   defer to here: **the MC68040UM is the spec**, and gates must be
   synthetic self-consistency tests built from its pseudocode.

2. **Snooping has no client on the current 040 fleet.** Every SCSI
   transfer on the Q605/Centris/Q700/Q630 boards is *CPU-driven*
   pseudo-DMA: the driver spins on DRQ and moves bytes through the
   handshake window (`Q605Memory.cpp` § turboscsi, MAME
   `iosb.cpp:498-591` pattern; `Ncr53c96::dmaRead/dmaWrite` are called
   from the CPU's own load/store path). The IIfx SCSIDMA true-DMA is
   deferred and A/UX-only (`docs/IOP_BRINGUP.md` § M4). The IOPs and
   DAFB never write guest RAM behind the CPU. **No alternate bus master
   exists**, so a copyback cache would stay coherent through the CPU's
   own accesses — snooping (SC bits, MI handshake) is a model with
   nothing to observe until a true DMA master lands.

Consequences, stated plainly:

- A full copyback data path changes **no observable** for a guest whose
  cache discipline is correct — and Mac OS on 040 runs correctly on
  every cache-less emulator. What it changes: behaviour of *buggy*
  guest code (a cache-less model is strictly more forgiving), cache
  *probing* diagnostics, and timing.
- The **display seam is the real hazard**, not SCSI: the host-side
  decoders read framebuffer memory directly. A dirty copyback line
  covering framebuffer bytes would never reach what the decoder reads.
  On the 040 fleet the framebuffer is DAFB/Valkyrie VRAM — mapped
  through pages whose **CM bits** Mac OS sets non-cacheable — so
  honouring CM is the load-bearing correctness requirement of M2, and
  measuring what Mac OS actually maps is M0's probe.
- The integrated FPU and the cache-content wall both closed on
  2026-08-16. The cache model is architectural and bus-beat timed; it
  does not attempt to expose individual BCLK/TA pin transitions.

## 1. Existing seams (all verified)

| Seam | Where | State |
|---|---|---|
| CINV/CPUSH | `MoiraExec_cpp.h` (`execCinv/execCpush`) | push dirty bytes to physical memory and invalidate the selected line/page/cache |
| CACR | `Cpu040::didChangeCACR` (`src/Cpu040.cpp`) | any write flushes the JIT (`flushAll`); **since M1** DE/IE also gate the tag model (read at touch time, no hook needed) |
| CM bits | `Moira::Mmu040AtcEntry::status` (`Moira.h:2594-2600` — "WP\|G\|S\|CM\|M\|R…") | descriptor CM bits ride into every ATC entry already — the probe and M2 read them from there, no new walk work |
| TTR cache fields | `mmu040MatchTTR` | TTR CM bits reachable the same way |
| JIT contract | `pomJitFetch`, `pomJitProbeCode/Data`, `pomJitData` | page-to-host-RAM data windows are refused; validated sole reads and write-authorized copyback hits may use the resident physical D-cache line; native writes publish the exact dirty-longword mask; every fetched byte and a native block's whole embedded range require resident byte-for-byte I-cache identity; native links return to that guard between blocks |
| Snoop hook | `pomSnoop040Read/Write` | alternate-master read supply, invalidate and write-sink/MI behaviour; callable even while DC is disabled |

## 2. Milestones

**M0 — measure, don't guess (the probe) — DONE 2026-08-04.** The
env-gated histogram (`POM68K_040_CM_STATS=1`, one counter in
`mmu040AtcFill`, printed at exit) over the full Q605 Mac OS 8.1 boot:

| fills | writethrough | copyback | serial-NC | nonserial-NC |
|---|---|---|---|---|
| instruction | 0 | 10 298 847 | 0 | 0 |
| data | **0** | 17 128 484 (98.9 %) | 28 023 | 136 932 |

Read: Mac OS maps essentially *everything* copyback and uses the two
non-cacheable modes exactly where the display/I-O seam needs them —
the M2 safety requirement is real AND honoured by the guest. **No
writethrough anywhere**: M2 has no gentle middle mode to hide behind;
it is copyback-or-nothing. (Fill counts are ATC-thrash counts, not
working-set sizes — the 32-entry ATC recycles; the MODE distribution is
what the probe was for.)

**M1 — architectural cache STATE, no data effect** (`POM68K_040_DCACHE`,
default off) — **DONE 2026-08-05.** Model the two caches' tag stores
(4 KB, 4-way, 64 sets, 16-byte lines — UM § 4): loads/stores
allocate/touch tags per CM mode, stores mark dirty in copyback pages,
CINV/CPUSH invalidate/push *the modelled tags* (CPUSH "push" is a no-op
data-wise — memory is already current, which is exactly why this
milestone is safe), CACR enable bits gate allocation. **Data is still
served by the bus** — coherent by construction, zero risk to every
green gate.

**Operand correction, 2026-11-08:** the M1 logical resolver described
below was wrong. MC68040UM-1993 § 4.2 specifies a physical address in An;
`pomCacheOp040` now takes it directly. The sole-use resolver/peek walk are
removed. The expanded `cache040_test` checks 128 opcode combinations with
separate physical/translated lines, physical writeback and no descriptor
access, including cache-disabled operation. The old translation-accepting
checks were replaced; the M1 account below is historical.

As landed:

- `extern/moira/Moira/MoiraCache040.h` — the self-contained tag store
  (`Cache040`), per-longword dirty bits, invalid-ways-first + 2-bit
  counter replacement, push/invalidate scopes. Pure logic, no clock,
  no registers: `cache040_test` part 1 drives it bare.
- The touch rides the three `mmu040Translate` return paths: TTR match
  (CM lifted from the matched TTR — `mmu040MatchTTR` grew an optional
  out-param), MMU-off (default = cachable/writethrough, UM § 3.5.1),
  and ATC/walk (CM from descriptor status bits 6-5, same field the M0
  probe read). CACR DE (bit 31) / IE (bit 15) gate lookup AND
  allocation; a disabled cache keeps its contents (UM § 4.4).
- `execCinv`/`execCpush` decode cache/scope/register and act on the
  tags. Line/page operands resolve through a **non-faulting** data-side
  translation (`pomCache040Phys`: TTR → ATC scan → `mmu040PeekWalk`, a
  read-only walk with no U/M descriptor write-back) — flag ON may not
  disturb state flag OFF would not have touched. An unmapped operand
  skips the op — and since the 2026-08-05 bughunt, so does a
  **bus-erroring descriptor chain**: the walk's fetches ride the bus
  like a real table search, and a garbage-but-resident chain landing in
  unmapped space is caught (`MmuBusError`) and treated as unmapped,
  never surfaced to the guest. Nothing to lose while memory is
  current; M2 revisits.
- **/BERR rollback** (bughunt 2026-08-05): the tag touch runs at
  translate time, before the bus access it describes — on an external
  bus error the stamped span is invalidated in `extBusError040`, since
  a real 040 line fill terminated by TEA leaves no valid line
  (UM § 7). The stamp is cleared by non-allocating touches and by the
  peek walk, so a /BERR can never roll back an older, legitimate span.
- Save states keep the house convention (caches flushed, never
  serialized): `pomFlushAtcs` — the restore seam — now also empties
  both tag stores. **Superseded for data-bearing caches by format v30,
  2026-11-09:** the M1 tag-only rule loses architectural bytes in M2;
  the current contract below preserves them.
- Two accepted M1 approximations, both to revisit in M2: split
  sub-accesses reuse the full access's SSW size, so a page-crossing
  misaligned write can over-mark one longword dirty; and the JIT's
  inline DTLB fast path bypasses `mmu040Translate`, so under the JIT
  the tag state is approximate. Zero data effect either way — M2 must
  fence the DTLB before serving data (see the seam table).

Gates: `cache040_test` (44 checks — struct semantics incl. the pinned
replacement victims, PA[9] indexing and top-of-address-space spans;
then CACR/TTR/CINV/CPUSH, MMU-on ATC/peek-walk/unmapped-skip resolver
paths, disabled-cache retention, and a /BERR descriptor chain through
a bare 68040 Moira with a bus hole), `sst68040` (7 200 pinned vectors)
and the JIT lockstep gates — the five that existed then — green with the
flag ON, 2026-08-05. The
same day's adversarial bughunt (4 finders + per-finding refuters)
found and fixed three real defects — an infinite touch loop at
0xFFFFFFFF, the faultable peek walk, phantom lines after /BERR — and
four gate holes, all pinned by the checks above.

**M2 — copyback data path — DONE 2026-08-16.** Each line now carries
16 data bytes and four dirty-longword bits. Reads perform requested-
longword-first, four-beat fills; copyback stores allocate and remain
stale in backing memory until dirty replacement or CPUSH; writethrough
stores update memory and do not allocate on a miss. Serial/nonserial NC,
locked RMW, MOVES and MOVE16 bypass allocation and first resolve dirty
aliases. A failed fill cannot leave a partial valid line. The model is
enabled with `POM68K_040_DCACHE=1`. It remains opt-in because the necessarily
cache-aware JIT path does not yet meet the real-ROM product-speed budget;
this is a performance/default-policy limit, not a missing cache behaviour.
The JIT may compile/run a block only while its complete embedded byte range
is resident and byte-identical in I-cache, and returns between blocks to
recheck it. Ordinary page-to-RAM data mappings remain disabled. Sole reads
may now hit a published physical D-cache line directly, but only after
validating the logical+privilege tag, data-ATC epoch, live valid bit and
physical tag; every miss and every store still crosses the exact D-cache
model.

**M3 — snooping — DONE 2026-08-16.** `pomSnoop040Read` can supply a
dirty hit and optionally invalidate it. `pomSnoop040Write` implements
invalidate or write-sink/MI semantics, including disabled-cache hits and
cross-line ranges. The shipped 040 boards still have no true DMA master,
so the API is pinned synthetically and is ready for the deferred IIfx
SCSIDMA rather than wired to CPU-driven pseudo-DMA.

**Timing — DONE 2026-08-16.** `PomCache040Timing` separates a cache hit,
a four-beat line fill and each transferred longword of a push. Defaults are 0, 8 and
2 core cycles respectively, matching zero-wait external bus beats; board
callbacks continue to add their own memory/device wait states. This is an
architectural transaction model, not a pin-level BCLK/TA waveform model.

**Performance follow-up — 2026-08-16.** A short Q605 profile disproved the
obvious host-side optimization: a last-I/D-line tag memo made the Release
gate **60.63 s -> 63.29 s** (+4.4 %) and was removed. The real repeated work
was the native data TLB asking on every access whether it could bypass the
D-cache. Cache-active refusal is now stored as a tagged null entry, so later
accesses go directly to the exact cache-aware thunk: the full boot's refusal
counter fell **321,187,389 -> 727,456** (-99.77 %), with identical guest
state (`SCSI=2329`, same screen statistics). The paired non-verbose Release
run improved **60.63 s -> 59.19 s** (-2.4 %), useful but nowhere near the
cacheless **11.03 s** gate. The model therefore remains opt-in. The next
material lever is a native physical D-cache-line hit path; more tag-lookup
micro-optimization is explicitly not the answer.

**Native physical line reads — 2026-08-16.** That next lever is now in both
native backends. A successful exact access publishes its fixed
`Cache040::Line*`; generated sole-access loads validate logical privilege,
the DATA-ATC generation, `valid`, the physical tag and the 16-byte boundary
before reading the line's big-endian bytes. ATC eviction/map changes advance
the generation in O(1); replacement, CINV, CPUSH and snoops fail the live-line
checks.

The dedicated AArch64 cache-on lockstep executed **18,576,390 native line
reads** over **131,823,105 JIT instructions** and remained CPU/RAM/device
identical to the interpreter for five million 50-cycle checkpoints. On the
paired Release Q605 fixed-budget run, disabling only this path took
**61.94 s / SCSI=2329**; enabling it took **48.78 s / SCSI=2064** (-21.2 %).
The cache-on interpreter took **111.28 s / SCSI=2062**, so the new path is
2.28x faster while its peripheral progress closely follows the reference.
This still does not reach the Finder inside the calibrated five-billion-cycle
gate (nor approach the cacheless 11.03 s host cost), so
`POM68K_040_DCACHE` remains opt-in. `POM68K_JIT_040_LINE_READ=0` is the
attribution control; `POM68K_JIT_040_LINE_STATS=1` makes lockstep require and
report an exercised native hit path.

**Native copyback write hits — 2026-08-16.** Sole-access stores now have a
separate publication proof in both native backends. `pomJitCache040W` is
filled only after an exact write proves permission, descriptor M state,
CM=copyback, a resident line and the expected dirty bits. A generated hit
revalidates the same logical/ATC/physical/line invariants as a read, stores
the big-endian bytes, then ORs exactly the first and last covered longword
bits. The write table is deliberately not populated by reads, and a
write-through alias cannot borrow another alias's dirty state.

`jit_copyback_write_040_test` is the dedicated gate: a misaligned long store
must set dirty mask `$3` while backing RAM stays stale, then the already
compiled block is redirected to a /BERR hole. Its complete 60-byte format-$7
frame is compared with the interpreter for both last-write (next PC/final
CCR) and restart (instruction PC/restored CCR and predecrement EA) paths.
`POM68K_JIT_040_LINE_WRITE=0` keeps the exact-write attribution control, and
`jit_copyback_write_040_control_test` now runs it as a gate: A64 must replay
the exact cache-aware instruction, never expose backing RAM through the
ordinary DTLB.

The MOVE-only path removed 4,402,477 fallbacks but measured below noise on
two order-reversed 5 G-cycle pairs (49.87 -> 49.81 s average). The useful
next consumer was BSR's sole return-address push: 3,933,940 additional
fallbacks disappear when A64 uses the same write proof x64 already reached.
`jit_copyback_bsr_040_test` pins its stack-line bytes, exact dirty longword,
stale backing RAM and /BERR format-$7 frame across the user-to-supervisor
stack switch. The combined write path averages **49.93 -> 49.49 s** (-0.88 %)
with identical guest state.

The next bounded consumer is the hot longword pair `MOVE.L abs.W,-(A7)` /
`MOVE.L (A7)+,abs.W`. Both native backends prove the source R line and
destination W line before either access becomes visible; if the second proof
misses, the complete untouched instruction is replayed. The dedicated ON/OFF
pair gates pin exact bytes, dirty bit, flags/EA and the second-line /BERR
frame. `POM68K_JIT_040_LINE_PAIR=0` isolates this path while leaving ordinary
line reads/writes enabled. It removes **7,523,969** additional fallbacks and
measures **46.92 -> 46.63 s** (-0.62 %) over two order-reversed 5 G-cycle
pairs, with identical guest state. The full A64 cache-on lockstep now
exercises **5,896,026 native copyback writes plus 21,303,835 reads** over
131,823,105 JIT instructions and remains CPU/RAM/device-identical for all
five million checkpoints.

## 3. Closure and gates

The 2026-08-05 M1-only decision was sound for the then-current guest
observable, but the later explicit requirement was broader: complete the
architectural cache behaviour itself. That request supplies the reopening
condition, and M2/M3 are now implemented.

`cache040_test` pins geometry and replacement, CACR/CM/TTR/MMU paths,
CINV/CPUSH scopes, stale copyback memory, dirty eviction, disabled-cache
snooping, write-sink/invalidate behaviour, cross-line snoops and exact
configured hit/miss charges. `sst68040` remains the ISA/MMU regression
wall; the standard real-ROM etalons retain their calibrated cacheless mode,
while cache-on CPU/JIT locksteps are the integration gate.

**Save states, 2026-11-09 (format v30).** `Cache040::visit` preserves the
replacement phase and every valid line's tag, dirty mask and 16 bytes in
both caches; `Moira::pomVisitState` also carries the cache-model mode.
Inactive retained lines matter: switching CACR or the host model off need
not discard their bytes. Invalid data and diagnostic counters are omitted.
Saving leaves RAM, dirty bits and guest time untouched; loading first
drops old ATC/JIT state, then restores the saved caches. Draining only dirty
lines would lose clean stale instruction bytes and alter the bus/clock.
Versions through v29 are refused before mutation.

The initial six bare-CPU regressions (68LC040/68040, active/CACR-off/model-off)
failed 24 checks before this fix. They resume stale instruction fetches and
copyback reads, then force the same dirty victim as an uninterrupted CPU.
`moira_state_test` also holds invalid-slot canonicalization.
`savestate_040_test` covers full containers and both engines on Q605,
Centris, Q700, Q900 and Q630, including continued execution and transactional
rollback after a truncated CPU payload. This is a persistence contract,
not evidence for the remaining hardware replacement/timing questions.


**Implicitly noncacheable operands, 2026-11-09.** The primary contract is
MC68040UM-1993 §§4.3.2, 7.4.1, 7.4.3 and especially 7.4.5 (PDF p. 168):
RMW starts after a matching dirty line has been pushed; a clean match is
invalidated. Setting `pomCache040Enabled=false` for MOVES/TAS/CAS suppressed
that step as well as cache service. `pomCache040Touch` now retains the CACR
enable state and sets `pomCache040Cm=CM_NC` for these accesses. No new
allocation is allowed, and the next ordinary reference gets its own CM.
CAS2 now arms the same locked marker before either operand read, including
on the cacheless path, so write permission and SSW.LK follow TAS/CAS.

The initial **630 executed sequences fail 1 351 checks** before the fix:
EC/LC/full 040; transparent copyback or 4K/8K paging aliases; missing, clean
or dirty physical lines; MOVES.B/W/L reads/writes with SFC/DFC 1/2/5/6;
TAS and successful CAS.B/W/L or CAS2.W/L. Exact bus traces pin the push of
a line with two dirty longwords before operand transfers, preservation of dirty
neighbours, clean-line invalidation, no new allocation, and resumed normal
caching on the next instruction. Three CACR-off/on controls distinguish
retention from access inhibition. Sixteen CAS2 protection cases cover either
operand, both page sizes and both cache-model modes, including comparisons
that would fail without a write-permission fault.

The failed-comparison terminal transfer and dirty-push fault stamping are
covered below. The CACR-disabled locked-hit/next-miss quirk in §4.3.3
remains open. No LOCK/LOCKE waveform fidelity is claimed.


**Failed-comparison terminal transfer, 2026-11-09.** The §7.4.5 bus-protocol
item above is implemented: CAS.B/W/L writes the operand read; either failed
CAS2.W/L comparison writes only its second operand. Both use the regular
`writeM` path before compare-register updates, restricted to EC/LC/full 040.
A longword is still two host `write16` callbacks; this is not a new pin-level
bus implementation. The initial 1 814 sequences fail 3 752 checks, while all
78 legacy no-write controls pass. The 1 736 040 cases cover both privilege
modes, flat/4K/8K mappings, cache off/missing/clean/dirty state, CAS auto-update
addressing, both CAS2 mismatch branches and terminal-write /BERR on each
host beat. The handler witness completes WB3 then RTEs without re-reading
the operands. Current flags, compare-register update order, logical frame
addresses and original read-value WB3 data are held alongside bus traces.

WinUAE @ Hatari e77819f7 writes before Dc on failed CAS and marks it as a
final store; the fork follows that reading. Its CAS2 generator omits the
write entirely, so the fork follows the primary manual there (D31 in
`oracle/README.md`). CAS2's final-store/next-PC and deferred-Dc fault policy
is an analogy with CAS, explicitly F pending a real 040 write-fault capture.
The data-transfer requirement has primary evidence; no silicon pipeline or
LOCK/LOCKE waveform fidelity is inferred from the software tests.


**MOVE16 fault descriptors and successful pushes, 2026-11-09.**
`mmu040GetMove16/PutMove16` now own the aligned logical line descriptor
before translating or touching the host bus: data space, source/read or
destination/write, line size and initial line address. The destination also
sets the EA needed by the existing WB2 capture. Reusing a fetch stamp
previously mislabeled external errors as program reads and left WB2 with
stale addresses. `pomCache040Writeback` separately saves/restores the
caller's five stamp fields on normal return, so a subsequent MOVES/TAS
read fault no longer looks like the preceding physical push. A push that
itself faults still needs a full physical-TM/WB1A/PD implementation.

The primary stamp rules are MC68040UM-1993 §§8.4.6.2/8.4.6.4 (PDF pp.
237/239). The existing WB2/PD/restart convention remains the fork's
WinUAE-derived pipeline model. The initial 3 810 MOVE16 sequences and 70
post-push read faults fail 5 230 checks: all five MOVE16 forms, unaligned
cursors, EC/LC/full 040, both privileges, flat/4K/8K mappings, instruction
window and cache-miss paths, every external host beat and both MMU fault
stages. Traces pin completed prefixes, frames retain the initial logical
line and destination WB2/PD data, and safe-RAM repair/RTE retries complete
with postincrements once. MOVES.B/W/L and TAS witnesses distinguish a
successful dirty push from the following operand read's /BERR.

No cached MOVE16 hit is admitted by this matrix. The §4.3.3 read-hit rule
(cache supplies valid/dirty data) and write-hit invalidation need separate
coherency witnesses. Write-allocate unused-beat delivery and simultaneous
pipeline buffers also remain open. Retry tests use safe RAM;
they do not establish whether a peripheral tolerates repeated MOVE16 reads.


**Read-fill faults, 2026-11-09.** MC68040UM-1993 §§4.6.1/7.6.1 distinguish
requested longwords from unused fill beats; §§8.4.6.2/8.4.6.4 retain the
original size and logical initial address in a demanded fault. The initial
4 872 data-read plus 224 program-fetch sequences fail 7 936 checks: physical
longword stamps, unconditional exceptions and premature dirty pushes all
violate those contracts.

`pomCache040Fill` now receives the caller's requested chunk and scratch
line. A private probe sentinel makes `extBusError040` raise an internal
abort before constructing any frame or consuming continuation state. The
fill controller restores the size immediately, invalidates the new line,
and restores a dirty victim. It constructs a read fault only if the aborted
longword intersects the requested chunk. Otherwise, completed bytes survive
in the caller's scratch buffer while no architectural cache line is valid.
The scratch copy precedes victim restoration so it cannot return the old
victim's data. Failed fills cannot publish a resident native read line.

For read replacement, the new line is read before pushing a dirty victim
(§4.6.2). An aborted read therefore retains dirty cache bytes and stale RAM
instead of prematurely writing the victim. The data matrix covers B/W/L,
misalignment within and across lines, wrapped fills, either host half,
missing/clean/dirty victims, WT/CB mappings, both privileges and 4K/8K aliases.
The instruction matrix holds word-size program faults and unused beats
under the fork's existing two-word lookahead. Exact bus order, returned
bytes, logical frames and repair/RTE on safe RAM are checked.

The private sentinel adds no CPU member and is never live at a snapshot
boundary. Fixed fill-latency charges stay approximate. Push-fault frames
and new-line retention are covered by the following push milestone.
Deferred instruction-prefetch fault delivery/branch cancellation remains
open. Ordinary page-split MA qualification is covered below.

**Write-allocate pending stores, 2026-11-09.** The initial 14 616 sequences
fail 35 112 checks. The same private probes now
protect the original store descriptor during a write-miss fill. A reported
read error keeps its logical initial address and original B/W/L size,
sets SSW.RW and leaves WB1/WB2 invalid. WB3 contains the pending store's
size/space, logical address and register-aligned value. This follows the
permitted read-error frame combination in MC68040UM-1993 §8.4.6/Table 8-6
(PDF pp. 242–244); assigning this synchronous model's store to WB3 remains
F until a real write-miss frame is captured. The existing final-store
next-PC/CCR/EA policy is unchanged.

Write replacements also read before pushing (§4.6.2). An aborted fill
restores a dirty victim without modifying its RAM; successful fills leave
the store descriptor intact for subsequent transfers. `cache040_test`
holds 14 616 MOVE.B/W/L sequences, including `(An)`, `(An)+`, `-(An)`,
line crossings, either host word, missing/clean/dirty victims, both
privileges and flat/4K/8K aliases. Software consumes the actual WB3 value,
then RTE reaches the next instruction without another operand read or
repeated EA adjustment. The cache-visible operand and untouched neighbours
are checked together, including a first-line store before a second-line
fault. These are safe-RAM completion tests.

Every write-fill beat error is still reported conservatively. Suppression
of unused write-fill errors and completion of the outstanding store when
the allocation aborts without an exception remain open; the read-fill
scratch-buffer rule alone cannot preserve an outstanding write. The next
milestone supplies push-fault PD frames and new-line retention. No CPU
member or snapshot format change was required by the pending-store correction.


**Push faults and transfer width, 2026-11-09.** The initial 5 733 sequences
fail 15 848 checks. Pushes were ordinary FC=1/5 writes with only dirty
longwords transferred, no full PD image, no physical WB1A, and a shared
FA/WB3A. A push error also restored a displaced dirty victim over a new
line already read successfully. CPUSH incorrectly retained its faulted line.

MC68040UM-1993 §4.6.2 requires a longword push for one dirty longword and
a full-line push for two or more. §§8.4.6.2/8.4.6.4–8.4.6.6 and Table 8-6
require write/TM=0/TT=0, physical FA=WB1A and all four PD words even for
a single-longword push, with WB1 invalid. §7.6.1 retains a new replacement
line on push failure and invalidates a line explicitly selected by CPUSH.
The reviewed PDF locators include p. 180 and pp. 239–240/244.

A private push probe catches the host /BERR before ordinary fault handling
can replace a logical pending store with a physical beat. The controller
keeps the existing continuation/LK/pending-store policy, then constructs
the push SSW, physical addresses and complete victim PD image. WB3A now
has its own fault-to-frame scratch field. It is consumed before the
instruction boundary, so it adds no snapshot field or format version.
A completed fill is not rolled back after a subsequent push fault.
CPUSH/NC drains invalidate first while passing a local saved line to the
sink; the frame owns the dirty image after a fault.

The expanded 12 285 sequences cover CPUSH line/page/all, replacement
reads and B/W/L writes, NC reads and B/W/L writes, locked TAS, every
host half, each single-dirty position and multi-dirty masks, both privileges
and flat/4K/8K aliases. Software consumes the stacked PD first, then
completes WB3 through architectural memory, including a retained new
copyback line. RTE advances a final store or retries a read/CPUSH without
another completed push or replacement fill. Bytes, neighbours, flags and
single EA updates are checked together on safe RAM. Earlier coherency
matrices now expect full-line transfers for multi-dirty pushes.

The base-first full-line push order is a model convention; initial-beat
selection needs a hardware capture before any pin-level admission.
This is not a new asynchronous pipeline: the ordinary store's WB3 stage
assignment remains the fork's model choice. Simultaneous WB2/WB3 and
pending MOVE16 writebacks need separate witnesses. Invalidation on a
faulted NC-hit push follows the synchronous drain policy; the manual's
explicit fault-state text names replacement and CPUSH, so an NC hardware
capture remains open. Ordinary data/fill page-split MA is qualified below.


**Split-page MA, 2026-11-09.** The initial 8 592 sequences fail 2 016
frame checks: a later physical data/fill fault incorrectly sets MA.
MC68040UM-1993 §8.4.6.2 (PDF p. 238) reserves MA for an ATC fault on the
second page. §8.4.6.4 (PDF p. 239) separately keeps the initial logical FA
for every misaligned fault. `mmu040Fault` therefore still restores FA
unconditionally, while only its MA assignment is gated by `atc`.

This is a deliberate primary-manual ruling, D32, over the pinned WinUAE
`misalignednotfirst` helper, which sets MA on every later-fragment error.
No oracle patch or corpus exception is added. The matrix covers .W/.L
reads/stores, all boundary-crossing alignments and aligned first-page
controls, `(An)`, `(An)+`, `-(An)`, both privileges, EC040 flat controls and
LC/full 040 flat/4K/8K aliases. Cache-off, active NC, active WT reads and
CB write allocation distinguish physical data/fill errors from nonresident,
write-protected and supervisor-only ATC faults. Exact transfer prefixes,
trap origins, frames, partial store visibility, software WB3 completion,
RTE, flags, single EA updates and untouched neighbours are held together.
Completion targets safe RAM.

The change introduces no CPU/snapshot state and leaves the existing
WB3/final-store/restart policy intact. The next milestone qualifies ordinary
descriptor-cycle /BERR; the new ATC controls here use validity/protection faults. Unused write-fill delivery and
asynchronous push-buffer combinations also remain open.


**Descriptor bus errors and history order, 2026-11-09.** The initial
13 632 cases fail 30 144 checks, covering original-access ATC/MA frames,
negative ATC retention, table prefixes and history already committed.
The expanded 14 112-case matrix adds resident-M=0 write re-walk controls.
It can be run alone with `build/cache040_test --descriptor-faults`.

Ordinary translation now scopes raw descriptor reads/writes as a table
search. A private /BERR abort is caught before touching the operand's
logical descriptor; the context is cleared, an R=0 ATC entry is created,
and the normal builder reports the original access with ATC and, when
appropriate, MA. PTEST still returns MMUSR.B with no frame. The search
context is cleared on every exit. A subsequent TTR-served physical /BERR
before any other walk proves it cannot leak into operand callbacks.

MC68040UM-1993 §3.5 (PDF p. 79) describes retry through an ATC entry whose
failed table search aborts the access. §8.4.6.2 supplies the ATC/frame
qualification. Figure 3-10 (PDF p. 60) explicitly places a scheduled
table-descriptor U write after reading the next descriptor. The walker
now retains the previous descriptor and follows that order, unlike the
eager writes in the pinned oracle (D33). Physical table longword reads
also sequence high/low host words so an early abort cannot read ahead.

The cases cover LC/full 040, 4K/8K pages, both privileges, cache-off/NC/
WT-read/CB-write paths, B/W/L reads/stores, actual program fetches and
PTEST R/W in both domains. Direct/indirect descriptors, every read/U/M
write half, cold and post-split faults, and resident M=0 re-walks are held
with exact table traces and partial history. No operand/code transfer can
reach the faulted page. Repair without PFLUSH must retain the failure;
after flush, PTEST or software WB3 completion/RTE succeeds. The diagnostic
cleanup test discards only its own nested frame, leaving the original
stacked frame to drive the actual safe-RAM recovery.

The ordinary pending-store/restart policy stays unchanged. Descriptor
LOCK/RMW pin waveforms remain separate. Ordinary validity/protection
failure retention is covered next. No new CPU/snapshot state or native admission.


**Validity/protection failure retention, 2026-11-09.** The initial 8 192
sequences fail 33 488 checks. Ordinary cold faults did not publish the
walk result, so a descriptor repair could discard R/W/S denial or change
the cached PA without PFLUSH. Even PTEST-created S=1/M=0 entries could be
discarded by a user write before the S check. Initial fault frames and
allowed M controls already passed; the failures include consequences of
incorrectly permitted diagnostic writes during repair/recovery.

MC68040UM-1993 §§3.3/3.5 require entry creation before retry through its
attributes. The normal walk now fills the ATC before validity/protection
checks. A protected resident page retains its old readable mapping and
W/S bits; an invalid root/pointer/page/indirect chain retains R=0. Both
last-entry and scanned lookup now check S denial before an M=0 write
re-walk. Only a permitted unmodified write can re-walk, update M and
observe a repaired descriptor; a modified write retains the old mapping.

`build/mmu040_atc_test --failure-retention` runs the focused matrix. Its
LC/full 040, 4K/8K, cache-off/NC/WT/CB and privilege cases cover B/W/L data,
actual code fetches, PTEST data/code R/W and locked TAS. Descriptor repairs
point to different physical pages. Unrelated accesses exercise scanned
lookup, and the opposite FC2 domain must observe its own repaired mapping.
Cached denials emit no table cycles or operand writes; WP still permits
reads from its original PA. After PFLUSH, the original frame drives safe-RAM
WB3/RTE completion or read/TAS/code retry with one EA update. The complete
registered ATC gate includes the matrix and all earlier cases.

The existing pending-store and restart model is preserved, including its
hardware-capture limitations. Descriptor LOCK/RMW waveforms remain separate.
No new CPU/snapshot field, history order, cache replacement rule, corpus
exception or native admission is involved.

## Access-counted replacement (2026-11-10)

MC68040UM-1993 §4.1 increments each cache's two-bit counter "for each access
to the cache": per half-line accessed in the instruction cache; in the data
cache per half-line accessed during reads, per full line accessed during
copyback writes and per bus transfer of a write-through write. The fork had
advanced it on replacement only. Now `Cache040::allocate` reads `rot` and the
callers count. `pomCache040ReadValue` counts half-lines for I and D,
`pomCache040WriteValue` counts copyback lines or write-through transfers (§7.3
aligned split), and a miss counts after selecting its victim, even when its
fill aborts. `Cache040::touch` counts the same way.

Three paths serve cache accesses without those functions and count them
explicitly. The interpreter's fetch window counts one I access per fetched
word, the granularity of `mmu040FetchWord`. Native code adds each retired
instruction's `fetchWords040` to the I counter. Native line hits add their
D delta: half-lines for a read, one for a copyback write. Cache-only probes
whose hits are published later stage the delta in the native frame.

Three more rules keep native code exact. A conditional branch commits the
stream words of the path it runs (`branchFetchWords040`,
`dbccFetchWords040`). An instruction with exact thunks stages its count in
`PomJitAtcReplay040::cacheFetches`, so a thunk's own I-side access (a JSR
target, a program-space read) sees it first, and a replay restores it. A
block's residency proof includes the lookahead word after its last
instruction.

Gates: `cache040_test` part 5 pins each rule's delta and the victim choice.
`jit_atc040_cases` compares both counters between the bare interpreter and
the window, threaded and native strategies (A64 and x64). Disabling either
native count fails it. The real-boot `jit_lockstep_a64_coarse_test` compares
both counters at every checkpoint (`POM68K_JIT_LOCKSTEP_CACHE=1` adds every
line). Fill and push fault cases pin `rot` before the access
under test, so their victim stays way 0. Open [F] questions: the victim/count
order, word-granular instruction fetch and the greedy transfer split. A
hardware replacement trace would settle all three.
