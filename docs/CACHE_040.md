# 68040 copyback / snooping — blueprint (M0)

*Opened 2026-08-04. Pattern: `IOP_BRINGUP.md` / `DUO_BRINGUP.md` — recon
first, milestones gated, nothing implemented before its observable is
named. Everything asserted below was verified in-tree on 2026-08-04;
citations inline.*

## 0. What the recon changed about this chantier

`TODO.md` § 0 lists "040 copyback/snooping" as the largest remaining CPU
inexactness. Two findings reframe what that work actually is:

1. **There is no oracle.** The vendored WinUAE oracle models a 68030
   data cache (`oracle/uae/upstream/cpummu030.h`,
   `write_dcache030_lrmw_mmu`) but has **no 68040 data-cache model** —
   upstream WinUAE serves 040 data straight from memory too. The house
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
| CINV/CPUSH | `MoiraExec_cpp.h:6647/6667` (`execCinv/execCpush`) | supervisor-checked no-ops — *consistent*, per `POM68K_VENDOR.md`: no cache exists for them to act on |
| CACR | `Cpu040::didChangeCACR` (`src/Cpu040.cpp`) | bit 15/11 drive the **throughput** i-cache overlay + JIT flushAll; no architectural cache |
| CM bits | `Moira::Mmu040AtcEntry.status` (`Moira.h:1597` — "WP\|G\|S\|CM\|M\|R…") | descriptor CM bits ride into every ATC entry already — the probe and M2 read them from there, no new walk work |
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
default off). Model the two caches' tag stores (4 KB, 4-way, 64 sets,
16-byte lines — UM § 4): loads/stores allocate/touch tags per CM mode,
stores mark dirty in copyback pages, CINV/CPUSH invalidate/push
*the modelled tags* (CPUSH "push" is a no-op data-wise — memory is
already current, which is exactly why this milestone is safe), CACR
enable bits gate allocation. **Data is still served by the bus** —
coherent by construction, zero risk to every green gate. Observable:
a synthetic unit gate (`cache040_test`) asserting UM semantics — line
allocation, set indexing, dirty marking, CINV vs CPUSH scope (line/
page/all), the DC/IC enable bits — plus the boot etalons unchanged
with the flag ON.

**M2 — copyback data path** (same flag, opt-in). Stores to copyback
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
