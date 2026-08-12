# 68040 copyback / snooping — blueprint (M0-M1, chantier CLOSED at M1)

*Opened 2026-08-04; M1 landed, was hardened, and the chantier closed
at M1 on 2026-08-05 (§ 3 — the decision and its reopening conditions).
Pattern: `IOP_BRINGUP.md` / `DUO_BRINGUP.md` — recon first, milestones
gated, nothing implemented before its observable is named. Citations
re-checked against the tree on **2026-08-12**: the flag, the tag store,
the CINV/CPUSH path and the 44-check gate are all where this file says
they are.*

**Read this if you are here to open M2**: § 3 is the whole file. §§ 0-2
are how M1 was reasoned and what it costs to redo.

## 0. What the recon changed about this chantier

`TODO.md` § 0 lists "040 copyback/snooping" as the largest remaining CPU
inexactness. Two findings reframe what that work actually is:

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
- Per `extern/moira/POM68K_VENDOR.md` ("Genuinely remaining on the 040,
  in the order that would bite"), the bigger wall for a *full 68040*
  claim is **the native FPU opmodes $40-$7F**, ranked above caches.
  This chantier does not displace that one.

## 1. Existing seams (all verified)

| Seam | Where | State |
|---|---|---|
| CINV/CPUSH | `MoiraExec_cpp.h` (`execCinv/execCpush`) | **since M1**: act on the modelled tags when `POM68K_040_DCACHE` is armed (`pomCacheOp040`); no-ops otherwise |
| CACR | `Cpu040::didChangeCACR` (`src/Cpu040.cpp`) | bit 15/11 drive the **throughput** i-cache overlay + JIT flushAll; **since M1** DE/IE also gate the tag model (read at touch time, no hook needed) |
| CM bits | `Moira::Mmu040AtcEntry.status` (`Moira.h:1777-1783` — "WP\|G\|S\|CM\|M\|R…") | descriptor CM bits ride into every ATC entry already — the probe and M2 read them from there, no new walk work |
| TTR cache fields | `mmu040MatchTTR` | TTR CM bits reachable the same way |
| JIT contract | `src/jit/POM68K_JIT.md` § derived state; `pomJitAtcEvict` | "derived state dies with the ATC entry" — a data cache is a THIRD state layer the JIT's inline DTLB must never bypass; M2 gates the JIT OFF or excludes cached pages from the DTLB |
| Snoop hook (future) | none today | first client = IIfx SCSIDMA true DMA (deferred) or any future bus master; the hook lands with the client, not before |

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

**M2 — copyback data path** (same flag, opt-in) — **NOT OPENED,
decision § 3.** Stores to copyback
pages land in the modelled line; reads hit dirty lines; eviction and
CPUSH write back. Requirements proven before merge: CM honoured from
the ATC entry status (non-cacheable/serial bypass — the display seam),
MOVE16 semantics (UM: bypasses allocation), the JIT excluded or
DTLB-fenced on cached pages, and a synthetic staleness gate (a
deliberately-undisciplined access pattern must now observe stale data
exactly where the UM says it would). Soak + persist etalons with the
flag ON are the exit gates.

**M3 — snooping.** Blocked on a first true bus master (IIfx SCSIDMA is
the named candidate). The hook is specified here so M2 doesn't paint
over it: every non-CPU write to guest RAM calls a `snoop(phys, len)`
that invalidates (SC=01) or updates (SC=10) matching lines per the
snoop-control bits the master presents. Do not build it before a
client exists — the IOP lesson (`IOP_BRINGUP.md` § M4) is that
A/UX-only machinery stays deferred and LOUD.

**Timing** (cache-hit vs miss cycles) is a separate thread feeding the
existing throughput overlay; it must not be entangled with the
correctness milestones above.

## 3. Order of work and the exit condition

M0 (one sitting: probe + numbers into this doc) → M1 (`cache040_test`
green + all `m040` etalons green with the flag on) → decision point:
M2 only if M0's numbers and a concrete motivation (a guest, a
diagnostic, a timing goal) justify the exposure. The chantier's honest
exit may well be "M1 + documented decision not to serve data from the
model until a client demands it" — that would still close the
`TODO.md` § 4 line, because CINV/CPUSH would finally act on real
architectural state instead of nothing.

**Decision — 2026-08-05: the chantier CLOSES at M1.** The sweep M1
owed is paid: after the one-time GUI cleanup of the 8.1 volume
(drVolAtrb back to $0100), `ctest -L m040` ran **33/33 green with the
flag ON on freshly relinked binaries** (2 h 00 wall, partly under a
concurrent session's load — passes stand, only failures would have
needed serial reruns). That 33 was the whole `m040` tier **as it stood
that day**; the tier has since grown to 41, so re-running the sweep is
a superset, not a repeat. The decision point then asks for a concrete
motivation to open M2's data path, and none exists:

- **No guest, no diagnostic.** Every profile boots and runs on the
  cache-less model; nothing in the roster observes cache CONTENT —
  M1's tags already give CINV/CPUSH real state to act on.
- **Not timing.** Timing is a separate thread feeding the existing
  throughput overlay (§ 2, last paragraph); it needs cycle numbers,
  not a data path.
- **Not coherency.** M3's first snoop client (IIfx SCSIDMA true DMA)
  is itself deferred and A/UX-only.

Against that stands M2's full exposure: the JIT DTLB fence across the
entire 040 fleet, the display seam, MOVE16, staleness gates. So the
chantier takes its named honest exit — **M1 plus this decision**.

Reopen M2 when one of these lands, and not before:

1. a real guest or diagnostic observed to depend on cache **content**
   (not tags) — the observable must be named first, per the house
   pattern;
2. the IIfx SCSIDMA true-DMA client (which reopens M3, and with it
   the whole coherency question);
3. a timing-accuracy goal the throughput overlay cannot meet without
   real line state.

For a *full 68040* claim, the wall ranked above caches is unchanged:
the native FPU opmodes $40-$7F (`extern/moira/POM68K_VENDOR.md`).
