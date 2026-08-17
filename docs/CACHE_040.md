# 68040 cache content, copyback, snooping and timing — M0-M3 complete

*Opened 2026-08-04; M1 landed and was hardened on 2026-08-05. The
initial decision to stop at tags was superseded by the explicit request
to complete the 68040 model; M2/M3 and timing landed on 2026-08-16.
Pattern: `IOP_BRINGUP.md` / `DUO_BRINGUP.md` — recon first, milestones
gated, nothing implemented before its observable is named.*

## 0. What the recon changed about this chantier

The chantier opened because "040 copyback/snooping" was then billed as the
largest remaining CPU inexactness. Two findings reframe what that work
actually is (and are why `TODO.md` § 4 now records it closed rather than
pending):

1. **There is no oracle.** The vendored WinUAE oracle models a 68030
   data cache (`oracle/uae/upstream/cpummu030.h`,
   `write_dcache030_lrmw_mmu`) but has **no 68040 data-cache model** —
   upstream WinUAE serves 040 data straight from memory too. Do not be
   fooled by `struct cache040` / `fill_icache040`
   (`oracle/uae/upstream/newcpu.h:160,726-738`): that is the
   **instruction** cache; there is no `*_dcache040` anywhere. The house
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
| CACR | `Cpu040::didChangeCACR` (`src/Cpu040.cpp`) | bit 15/11 drive the **throughput** i-cache overlay + JIT flushAll; **since M1** DE/IE also gate the tag model (read at touch time, no hook needed) |
| CM bits | `Moira::Mmu040AtcEntry.status` (`Moira.h:1777-1783` — "WP\|G\|S\|CM\|M\|R…") | descriptor CM bits ride into every ATC entry already — the probe and M2 read them from there, no new walk work |
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
  both tag stores.
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
a four-beat line fill and each dirty longword push. Defaults are 0, 8 and
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
while cache-on CPU/JIT locksteps are the integration gate. Save states keep
the project convention: derived cache contents are flushed rather than
serialized.
