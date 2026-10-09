# POM68K implementation plan from the Snow comparison

ATA transfer restoration, GCR tags and DART sector import are now implemented.
The 512Ke and II FDHD hardware profiles, native physical-track retention and
MOOF bit/flux import/export are also implemented, and so is the basic
machine-thread debugger service (pause, step, PC breakpoints, registers,
side-effect-free memory and disassembly). Its extended stops, editing and
histories remain; session and network improvements can be selected
independently.
Keep new hardware projects conditional on a named consumer and reproducible
evidence. This order improves correctness and makes subsequent bring-up easier
without replacing POM68K's existing CPU, firmware or network architecture.

The evidence and source links are in [SNOW_COMPARISON.md](SNOW_COMPARISON.md),
pinned to the two revisions reviewed on 2026-10-08. This is a proposed sequence,
not a replacement of [TODO.md](../TODO.md)'s accepted milestones. Costs below
are relative engineering scope: **S** is a focused component change, **M** spans
a component and its product bindings, **L** introduces a cross-platform or
multi-device contract, **XL** is a new hardware/media family. They are not
calendar commitments. Proposed gate names do not refer to existing tests.


## Implemented after the comparison (2026-10-08)

The source findings below describe the pinned baseline. The subsequent changes
are restricted to behavior present in real devices and named Macintosh
hardware profiles:

- ATA PIO transfer buffers, selected CHS geometry and guest-written sectors now
  restore together. `ata_disk_test` covers fresh-device continuation, partial
  multi-sector writes, repeated rewind, IRQ, READ/WRITE BUFFER and rejection of
  truncated payloads. Format v21 deliberately rejects older snapshots.
- `CdCueSheet` maps file-relative positions to disc LBAs. One-session BINARY
  CUE discs with AUDIO and one MODE1/2048 or MODE1/2352 data track can use
  multiple files. Stored INDEX 00 gaps are retained; the data extent stops at
  the next track's INDEX 00. READ TOC, READ(10) and audio use that same layout.
  Encoded audio, synthetic PREGAP/POSTGAP, a stored track-1 pregap, multiple
  data tracks, FLAGS, indexes above 01 and explicit later sessions are refused
  rather than guessed.

A second hardware-focused increment preserves every physical 12-byte GCR
sector tag on 400K/800K media. `Dc42Image` separates data and tags; the existing
Sony encoder and write decoder now retain both, including seek/reset and
snapshots (format v22). DC42 writes preserve the tag block and regenerate its
checksum, excluding the first sector's tags as Disk Copy requires. Nonzero tags
on raw media remain in memory and snapshots; raw-file write-back retains its
sector-data-only contract. Durable tag preservation requires DC42. `gcr_test` checks decoded tags on both geometries, every zone and head;
`floppy_persist_test` checks tag-only writes, states, reopen, protection and
malformed tag lengths. DART and native track preservation remain separate work.

A third hardware-focused increment fixes CD-DA clock drift. Snow declares
44,100 stereo frames and 75 sectors per second in
`core/src/mac/scsi/cdrom/mod.rs`. The physical requirement is independently
specified by [T10 97-104R0 §5.1–5.2.1](https://www.t10.org/ftp/t10/document.97/97-104r0.pdf).
POM68K formerly rounded every sector to 13,333 µs, emitting 75 sectors in
999,975 µs. It now accumulates rational sector phase using guest time, with
no per-sector rounding. Format v23 preserves that phase through pause and
restore. `cd_audio_test` checks deadlines at 40 ms, one and two seconds,
fresh-device restore and identical output under irregular ticks. Snow's
host-audio-driven pumping policy is not imported: POM68K retains its own
machine-time contract.

A fourth hardware-focused increment corrects the CD address space exposed to
the guest. READ CAPACITY now returns the absolute lead-out LBA minus one for
mixed discs, including their audio extents, instead of the data-track count
minus one. READ SUB-CHANNEL adds the 150-frame bias only to absolute MSF;
track-relative MSF starts at zero. PLAY AUDIO (10/MSF) identifies the track
containing the start sector, refusing a later data track even if an earlier
track is audio, with ILLEGAL REQUEST / ILLEGAL MODE FOR THIS TRACK. Rejected
commands leave the running transport unchanged. These rules match Snow's
capacity/track/Q-channel model and the real
[Sony CDU-541 SCSI interface manual §5.2.17, §5.2.21](https://bitsavers.trailing-edge.com/pdf/sony/cdrom/CDU541-25_AppleCD_150/Sony_CDU-541_SCSI_Interface_Manual_Mar1990.pdf)
and [T10 CD model §5.1, §5.5](https://www.t10.org/ftp/t10/document.97/97-104r0.pdf).
The two CD component gates reproduce the previous failures and verify the
corrected replies. The archive remains v23; the serialized layout is unchanged.

A fifth hardware-focused increment retains each stored INDEX 00 extent in
the mounted track table. PLAY can start in an audio track's stored pause;
READ SUB-CHANNEL reports the upcoming track, INDEX 00, decreasing positive
MSF time and negative track-relative LBA, then INDEX 01 at its start. TOC
addresses continue to identify INDEX 01. Snow's `QSub::new_mode1` explains the
countdown; its CUE backend still has an INDEX-00 TODO, so that implementation
is not used as the oracle. The independent hardware contract is
[T10 97-104R0 §5.1, §5.5](https://www.t10.org/ftp/t10/document.97/97-104r0.pdf).
`cd_audio_test` verifies same-file and multi-file stored pauses, raw audio
bytes at the boundary and fresh-device restoration within INDEX 00. The
Quadra 605 guest fixture includes a two-second stored pause between its tones.
The track table remains host-owned and is rebuilt from the same CUE on restore;
no archive layout changes. Synthetic gaps and track-1 negative-LBA import
remain outside this increment.

The sixth hardware-focused increment adds Apple DART data-fork import for
Macintosh GCR 400K/800K and MFM 1.44 MB media: stored, word-oriented RLE and
headerless LZH chunks. Lisa, Apple II and DOS type identifiers are refused.
GCR tags feed the existing Sony encoder; nonzero MFM tag padding is refused.
`FloppyFileImage` retains raw/DC42/DART provenance, including snapshots (v24),
and DART write-back keeps the archive type using fast-mode stored chunks.
Resource-fork metadata and CKSM checksums are outside this contract.
`dart_image_test` uses independently encoded LZH vectors and checks malformed
input, all geometries, the physical GCR stream, writes and fresh restoration.
The two `plus_system33_dart_*_boot_etalon` gates synthesize temporary DART
media from the immutable reference and boot the real Plus ROM to the Finder
in either drive. Apple DART 1.5.3's own best-mode sample also decodes exactly
to its source bytes. At that stage, native track preservation and MOOF remained separate work.

The seventh increment adds the real 512Ke and II FDHD variants, with separate
catalogue and snapshot identities. The 512Ke retains 512 KB RAM, mirrored
128 KB Plus ROM, 800K mechanisms and no SCSI. II FDHD retains the 68020 and
adds SWIM/SuperDrive rather than inheriting the IIx's 68030. Both boot original
ROMs to the Finder and accept guest input. GLUE word SWIM accesses now consume
one controller transaction, and formatted MFM gap4 remains clocked at index
wrap. Whole-revolution reads, MMIO sector writes and fresh-state round trips
cover those hardware paths; application coverage remains separate.

The eighth increment implements native physical-track retention and MOOF.
`FloppyTrackMedium` owns native tracks and modified sector-backed tracks;
seeks, side/mode changes and reset no longer discard them. `MoofImage` imports
400K/800K/1.44 MB bitstreams and flux maps using the Applesauce specification,
with bounded chunk/extent/index validation, optional CRC32 and rational timing.
Native writes retain transitions without requiring a sector checksum. Atomic
MOOF export uses the format's 125 ns bit grid and preserves cross-track phase,
metadata and unknown chunks. Original timings survive exactly; guest-written
positions round to the nearest sample. Snapshot v26 carries all retained tracks,
source metadata and the IWM write origin. A real MOOF boots the Plus in both
bays; II FDHD boots a temporary HD capture. Full-medium restoration compares
all 80 tracks on both faces.

The ninth increment models read-amplifier noise at SonyDrive's shared flux
output, rather than injecting random bits in the IWM shifter as Snow does.
The physical explanation follows MAME's floppy technical reference; its
0.285 implementation supplies the approximate 16 us delay and 50%/4 us
distribution. Long gaps and unrecorded tracks produce deterministic pulses,
varying with time and replaying after a fresh drive/controller restore.
Stored transitions and MOOF exports remain unchanged by reads.
Measured Sony analog response remains separate work.

The tenth increment adds interpreter/JIT application gates for the original
mixed bit/flux Oids v1.4 capture: Finder launch, galaxy loading, flight input,
an equal-time neutral branch and byte-identical fresh-machine restoration.
The SHA-256 pins the exact original image and reads leave it immutable.
Removing its only FLUX-map track prevents Finder mounting; that negative
control rejects a missing native-data path but does not isolate a protection
routine. This fulfills the selected nonstandard-program interaction gate;
broader copy-protection claims still require independently identified cases.

The disk base remains the user's working file. Since the media timeline
increment (below), restoring a SCSI or ATA state rewinds both memory and a
write-back file to the state's exact content, including in a later process,
or refuses by name. A full IDE *application* rewind scenario on the Q630
remains separate work. No new CPU or fictional peripheral was introduced.
Other items in this document are proposals, not adopted features.

## Work selection

| Order | Work | Evidence | Scope | Completion condition |
|---|---|---|---|---|
| 0 | ATA transfer and media restoration — implemented | Regressions pass | M | Exact continuation and rewind, including a fresh process |
| 1 | SCSI and ATA backing-image policy — implemented | Content digest, since-open log, `.pomundo` reverse journal | Done | A state cannot silently combine old RAM with later disk data (`media_timeline_test`) |
| 2 | Debugger service and basic views — implemented | `debug_session_test` on 000/020/030/040 rigs, `debug_inspection_test`, GUI window | Done | Pause, inspect, step and PC stop on representative CPU families |
| 3 | Extended debugger and device inspection | Basic service, register/RAM editing, access/exception stops, step over/out and histories landed; symbols, device snapshots and MMU/cache register edits absent | L | Defined stop semantics, bounded history, no inspection side effects |
| 4 | Session files | Startup/relaunch data present, combined file absent | M | Reopen the same configured machine with validated paths |
| 5 | CD track/source mapping | Single-source parser versus per-source Snow mapping | M | Two-file CUE, WAVE and gaps produce correct TOC/data/audio |
| 6 | Sector import preservation — implemented | DART stored/RLE/LZH and DC42 tags tested | M | Physical tags, persistence, states and real Plus boot validated |
| 7 | Native floppy medium and MOOF — implemented | Track/face storage, bit/flux import, atomic 125 ns export and v26 states | Done | Native lifecycle, weak-read replay and original Oids launch/flight/replay gates |
| 8 | Serial terminal and Windows TCP | Unix transport exists; terminal/Windows missing | M | Guest communication through SCC, including backpressure |
| 9 | Clipboard typing and scrap inspection | Input path exists, product bridge absent | M | Visible guest text, reproducible scheduling, bounded reads |
| 10 | RARP and ICMP address-mask helper | RARP Server setup, ICMP and controlled guest DNS/TCP qualified | S to M | MacTCP automatic address setup followed by a guest network transaction |
| 11 | Ethernet PCAP | Implemented: passive card observer, bounded background writer and live GUI controls | Done | Format/lifecycle gate and actual MacTCP RARP/DNS/TCP capture, independently decoded |
| 12 | 512Ke and II FDHD profiles | Implemented: own catalogue/state identities, real memory/controller differences | Done | Finder, input, media and deterministic snapshot gates |
| Conditional | TAP Ethernet, Portable/PB100, 68851, SCSI printer, Toolbox | Different capabilities or new hardware | M to XL | Named consumer plus the admission gates below |

Orders 4–12 can be selected independently after their stated prerequisites.
If archival floppy compatibility is the immediate product goal, move orders
6–7 ahead of session/clipboard work. If hardware bring-up is the goal, keep
the debugger and comparative probes ahead of convenience features.

## Correct save states before adding state conveniences

### ATA continuation

**Owners:** [AtaDisk.h](../src/AtaDisk.h), [Q630Memory.h](../src/Q630Memory.h),
the existing archive format and ATA component gates.

1. Add the two reproduced cases to an asset-free `ata_savestate_test`. Extend
   them to IDENTIFY, partial WRITE, multi-sector READ/WRITE, IRQ and SRST.
2. Serialize the sector/IDENTIFY buffer alongside its offset. Validate buffer
   lengths, offsets and flags on load; a malformed state must fail cleanly.
3. Give ATA the same guest-visible disk rewind guarantee required of SCSI.
   Either introduce an original-block/dirty-block owner or use the common
   timeline owner chosen below; retain task-file state separately.
4. Increment the state format when the payload contract changes. Prove
   continuation into a newly opened `AtaDisk`, not just into the object
   whose unsaved buffer remains in memory.
5. Add a Q630 IDE application save/load scenario after the synthetic gate:
   write a file, save, mutate it, restore, read it and continue execution.

**Acceptance:** the next data word, transferred sector bytes, IRQ behavior
and later guest reads match uninterrupted execution. Correct archives with
wrong media content are failures, even if the Finder screenshot is unchanged.

### Backing images and rewind

Use a synthetic SCSI/ATA disk to compare three cases: restoration in the same
process, restoration after reopening a writeback-modified file, and restoration
with a same-size different file. Assert the content through guest-visible
sector reads and inspect the actual host file. This resolves the current
writeback caveat before selecting a representation.

Recommended long-term contract: a state identifies an immutable disk base and
an overlay revision. All subsequent writes go to an overlay until the user
explicitly commits/exports that timeline. This supports cheap rewind and avoids
rewriting the base during load. A simpler first version can restore a complete
private clone, like Snow; its cost is image-sized I/O and storage. Choose one
from measured save size/time and the intended user workflow, not from the
presence of Zstd in Snow.

Proposed `media_timeline_test` must cover changed-base rejection, missing
files, fresh-process restore, failed clone/overlay creation and preserving
reference fixtures. Keep the media owner outside the serializer's CPU/device
visitor. Portable bundles, quick slots and thumbnails are follow-up features.

**Implemented (format v28).** The selected representation is neither an
overlay nor a clone: the working image stays the user's write-back file and
`DiskTimeline` keeps a *reverse* history beside it (`<image>.pomundo`). Each
save appends an epoch with the content digest; each later first write per
block per epoch appends the block's previous bytes before the image changes.
Cost is proportional to guest writes, with no image-sized I/O at save or load,
and the user's file never needs an explicit commit. A restore plans, then
applies through the disk's write path: the since-open log plus the state's
blocks, or the journal from the latest epoch with the state's digest. Memory
and backing file both land on the exact digest, or the load is refused by
name and rolled back. `media_timeline_test` covers same-process and
fresh-process rewinds, alternating states, a same-size changed base, deleted,
torn, compacted and uncreatable journals, topology refusals and ATA. Reference
fixtures stay immutable because `routeWritableOpen` journals only beside the
work clone. The journal is capped at 256 MB, cut at an epoch boundary.
A Q630 IDE *application* rewind scenario, portable bundles, quick slots and
thumbnails remain follow-ups.

## Build a debugger at the machine ownership boundary

### Basic service

Introduce focused `DebugSession`/`DebugSnapshot` modules and a small CPU/bus
adapter, exposed through the existing machine runtime. Keep
[MachineHost.h](../src/MachineHost.h) as a thin dispatch/binding point; do not
grow it into a debugger implementation or add a second platform registry.

The machine thread owns debugger mutations. The GUI sends typed commands with
request IDs and receives bounded immutable snapshots: registers, clocks,
stop reason, disassembly and requested memory ranges. Pause acknowledgment
means the CPU has stopped at an architectural boundary, not that a GUI boolean
has changed. Inspection must still work while the machine is paused.

The first UI has run/pause, one instruction, register and memory inspection,
disassembly and PC breakpoint management. Reuse Moira's primitives and
disassembler. Explicitly choose physical or logical address inspection,
report untranslated/unmapped ranges and avoid live device reads. While active
debug stops/history require the interpreter, show that effective choice and
restore the user's requested engine when the requirement is removed.

**Proposed gates:** `debug_session_test` on synthetic 000/020/030/040 rigs,
`debug_inspection_test` on an MMU map and destructive-read device register,
plus GUI model/headless rendering checks using the existing test pattern.
Verify breakpoint insertion/removal during run, acknowledgment ordering,
reset/load/relaunch, and that repeated inspection preserves device hashes.

### Editing and extended stops

Add register/RAM editing only while paused. Define PC/SR edits and refresh
prefetch, MMU and JIT-derived state appropriately. Define device edits as
separate operations; editing a memory view must not casually write through an
I/O register with unrelated side effects.

Then implement execution, read/write access, A-line, unhandled F-line,
exception and interrupt stops. State the address space, access width and
delivery point in the API. Catching an interrupt pin transition differs from
catching accepted interrupt-vector delivery. Test both if both are exposed.

Step over/out must handle BSR/JSR, nested calls, A-line traps, RTE, faults and
STOP; allow cancellation. A temporary PC alone can stop in an unrelated
recursive invocation, and a raw stack-pointer heuristic does not define all
exception-return behavior. Proposed `debug_step_test` exercises those cases.

### Histories and peripherals

Add opt-in bounded rings for retired instructions and traps, with cycles,
PC/opcode, register changes and exception entries. Export a documented
format carrying machine/ROM identity. Never silently drop entries without
an overflow indicator. Symbols and low-memory labels must be tied to ROM
identity and documented provenance.

Publish typed device snapshots for VIA, SCC, IWM/SWIM, SCSI, ADB and the board's
video device; extend by consumer. The window reads snapshots, not live device
pointers. Preserve the existing firmware provenance and network controls.
Measure disabled overhead against the same binary and guest workload; no
always-on logging or allocations in the instruction loop.

## Make sessions reproducible

Add a versioned session file containing the product profile, asset paths,
device topology, supported firmware choices, engine preference, network/serial
setup and display/window preferences. Keep guest execution state in `.pomss`.

The loader feeds [RuntimeConfig](../src/RuntimeConfig.h) and
[MachineFactory](../src/MachineFactory.cpp); it does not construct boards
directly. Specify precedence once: explicit command-line overrides win,
session values fill the chosen session, and legacy environment/default
behavior is retained only where the schema says it applies. Resolve relative
paths against the session file, never the shell's current directory.

Use stable profile IDs/slugs from the catalogue, validate unsupported
options, and report missing assets before relaunch. Proposed
`session_config_test` covers round trips, moved directories, spaces/Unicode,
missing ROM, unsupported version and overrides. A GUI smoke scenario opens
two sessions with different media and confirms the resulting topology.

## Extend media handling through focused owners

### CD sources

Extract a `CdImage` owner with track type, backing source, source offset,
absolute disc start, indexes/gaps and sector framing. Give `ScsiDisk` TOC,
sector and PCM operations without embedding source parsing in the SCSI
command switch.

Start with synthetic two-file CUE/BIN, then uncompressed WAVE audio and
INDEX 00/PREGAP/POSTGAP. Validate unsupported encodings and malformed sheets
explicitly. Proposed `cd_image_test` checks track boundaries, nonzero indexes,
short backing files, TOC/lead-out and exact PCM. Re-run `scsi_cdrom_test`,
`cd_audio_test` and the existing CD game/application etalon with its assets.
MODE2 and physical CD passthrough remain consumer-driven extensions.

### Sector formats and native tracks

DART is a bounded loader task; use its format/decompression definition and
generated or redistributable examples. Preserve DC42 tags as distinct
metadata rather than quietly synthesizing zero tags. Prove checksums and
tags on load/write/export while retaining existing plain DC42 behavior.

For preservation formats introduce a `FloppyMedium` owner keyed by track
and side. Track data must retain representation, duration, bit count or
transitions, metadata, write protection and any weak/multiple-revolution
information actually supported. `SonyDrive` retains mechanism/rotation and
the controllers retain separation/timing. Sector-backed media may synthesize
tracks lazily; native imported tracks must survive selection changes.

MOOF bit/flux import, physical writes, snapshots and atomic export now exist.
Export represents all tracks on a 125 ns bit grid instead of regenerating
sectors; it does not preserve the original bitstream-versus-FLUX choice.
Add A2R/PFI/PRI only as the selected corpus requires. A Rust FFI
dependency on Snow/Fluxfox is an alternative to a focused C++ loader, but
adds build, packaging, ownership and error-boundary work; select it only if
its format breadth earns that cost.

**Existing gates:** `moof_image_test`, `moof_media_state_etalon`, both
`plus_moof_*_boot_etalon` consumers and `maciifdhd_moof_boot_etalon` cover
track/head changes, reset, snapshots, protected writes and export/reimport.
`moof_image_test` additionally qualifies unrecorded/long-gap read noise,
actual IWM byte framing, polling-independent pulses, export isolation and
fresh drive/controller replay. The distribution remains an approximation.
**Application gates:** `interp_plus_oids_moof_etalon` and
`jit_plus_oids_moof_etalon` launch the original mixed bit/flux Oids v1.4 capture,
load a galaxy, prove thrust against an equal-time neutral branch and replay
the same input/state in a fresh machine. The exact capture digest is pinned;
the image remains external and immutable. This qualifies one nonstandard
program scenario. A larger corpus and independently identified protection
checks are still needed before general copy-protection compatibility claims.
Keep references immutable and make native-media exports atomic.

## Add serial and clipboard product controls

A serial window shows endpoint, link ownership, connection state, RX/TX
counters and a bounded terminal. It sends input through the existing SCC
queue with backpressure. Opening a display must not consume bytes owed to an
external bridge. Decide explicitly whether the terminal is an observer or
the active endpoint. Persist configured endpoints in sessions and report
port conflicts. Keep printer-port arbitration with AppleTalk/LToUDP.

Add Winsock TCP to `SerialHostTransport` as an isolated platform backend;
PTY remains Unix-specific. Extend `scc_serial_host_test` to disconnect,
reconnect, partial writes and saturation, then execute on Windows rather
than accepting only a successful Windows build.

Clipboard typing should reuse queued key events with machine-time press/release
spacing and a cancel operation. Provide a bounded text preview and report
unrepresentable characters; select keyboard-layout semantics explicitly.
Typing cannot depend on a GPU frame rate or inject down/up at the same guest
instant. Record generated events through the existing input journal.

Guest-to-host TEXT scrap uses the debugger's safe logical memory service,
bounded lengths and MacRoman conversion. Test moved/purged handles and
nonresident/disc-backed scrap; refuse unsupported cases visibly. Proposed
`clipboard_typing_test` verifies event scheduling, while TeachText/SimpleText
guest scenarios verify the displayed and copied text.

## Improve the existing Ethernet path

### Automatic MacTCP configuration

ICMP address-mask handling is implemented in `EtherLink` using the configured
gateway mask and RFC 950 unicast/zero-source broadcast semantics. Independent
`daynaport_test` fixtures cross SCSI WRITE/READ, validate both checksums and
identifier/sequence, reject malformed/fragmented/unrelated requests, verify
latency/unplug behavior and complete a subsequent gateway echo transaction.
Those mask fixtures qualify the wire service. RARP assignment
now shares the gateway's bounded address pool with MacIP.
`q605_dayna_rarp_etalon` installs the real SCSI/Link driver, selects MacTCP
2.0.6 "Server" without typing a guest address, reboots, then proves RARP,
IPv4 from the assigned .2 address, five successful gateway pings, an incoming
echo response and cable reconnection. `q605_dayna_rarp_network_etalon` adds
NetProbe, a real Retro68 MacTCP application launched by the Finder. Its own
bounded UDP A-record query resolves `pom68k.test`; the returned address selects
a TCP connection that exchanges exact request/reply bytes and closes normally.
Ordinary host sockets verify both conversations. The guest address comes from
RARP; the router is separately entered in the control panel before selecting
Server. No guest address, route state or transport result is injected.

`EtherLink` answers RFC 903 RARP only for its attached card. It checks Ethernet
and protocol address lengths, identifiers, opcode and both hardware addresses.
Reservations avoid MacIP, gateway and DNS addresses, preserve their MAC binding
through IP traffic and idle learned-lease expiry, and retire with their link.
`daynaport_test` covers malformed frames, collisions, exhaustion, cable latency
and subsequent guest echo.
No additional TCP NAT engine is used.
Check frame/packet lengths, protocol identifiers and checksums; do not
answer for unrelated MACs or create duplicate guest addresses. Address-mask
service does not depend on privileged outgoing ICMP support.

This qualifies the selected DNS/TCP conversation after automatic address setup.
Apple's DNR library, control-panel DNS list, automatic router discovery, BOOTP
and public Internet consumers remain separate work. NetProbe must be built
with the user-provided Retro68 toolchain; a missing app is an explicit soft-skip.

### Capture

`EthernetCapture` observes both directions at the Dayna card boundary before
IP/EtherTalk demultiplexing, with normalized frames excluding FCS. A fixed
256-frame queue and nonwaiting producer isolate all file I/O on a worker.
Queue overflow/contention is reported as lost observations, independently
of guest packet loss. The network window starts/stops capture and reports
output, losses and errors. PCAP timestamps use the actual board clock;
the `.tsv` companion records clock Hz, cycles, direction and final counts.
Existing files are refused and a reset/restore moving time backwards ends
capture visibly. `ethernet_capture_test` checks exact bytes, machine timestamps,
start/stop, independent transport delivery, bounded pressure, concurrent stop,
file refusal/I/O errors and backwards time. The real MacTCP network gate also
independently decodes its capture to find RARP, DNS and both TCP messages.

### External link and HTTPS options

Only after a named external topology is selected, add a Linux TAP backend
behind a link abstraction. Keep NAT/internal services as the default and
prevent two incompatible links from consuming the same frames. Prove bridge
addressing, EtherTalk multicast/AARP, disconnect/reconnect and blocked I/O
without running the whole emulator as root. Existing DaynaPort filtering
simplifications must be revisited for an actual shared LAN.

An HTTP-to-HTTPS adapter belongs in a separate opt-in host tool first. Use a
controlled modern TLS site to test Host/SNI, certificate failure, redirects,
chunking and encoding. Then prove a real vintage browser can render a useful
page. Rewrite-only experiments or valid TLS sessions do not establish web
compatibility. Keep this work out of the CPU and AppleTalk protocol engines.

## Admit new hardware through guest evidence

| Candidate | Implementation seam | Required evidence | Recommendation |
|---|---|---|---|
| 512Ke | Implemented compact profile: 512K RAM, repeated Plus ROM, no SCSI, 800K | Internal/external System 3.3 Finder, M0110/quadrature input, deterministic state | Implemented; broader application corpus remains separate |
| Mac II FDHD | Implemented GLUE profile: 68020 independently of SWIM/SuperDrive | Finder, ADB input, MMIO MFM write/read-back, deterministic state | Implemented; broader application corpus remains separate |
| Third SE drive | Implemented core: VIA1 PA4 internal-connector select, optional second SE / SE FDHD mechanism | Three distinct guest volumes; single-floppy SE and Classic keep PA4 high empty | Implemented in the core; startup option and disk-window row remain |
| Display Card 8-24 | New `NuBusDevice`, external declaration ROM, board card selection | Sense lines, CLUT/direct colour, modes/stride, slot IRQ, guest driver and state | Useful if NuBus video is the chosen goal |
| 68851 | Explicit external-MMU configuration and Moira execution/translation seam | Table/ATC/fault vectors plus the selected guest consumer | Do after debugger and conformance probes |
| Portable/PB100 | Normandy/LCD/VIA/PMgr platform family | ROM identity, Finder, input, power protocol, storage, snapshots; sleep separately | XL project, align with portable milestone |
| SCSI LaserWriter IISC | Focused raster-printer `ScsiTarget` | Real driver, page bounds, blank/graphics/text/multipage output | Only for that driver/workflow |
| BlueSCSI Toolbox | Optional vendor-command owner on SCSI target/controller seam | Real transfer tool, filename/size bounds, forks/encoding, CD switching if needed | Only if AFP/guest agent does not meet the need |

For every profile, append stable save-state identities rather than renumbering
the catalogue. Extend GUI, relaunch, storage capabilities and manifests from
the same profile owner. Private ROM/firmware/media remain external inputs.
For a command-level Portable PMgr prototype, expose its provenance and do not
call it M50753 LLE; a firmware path is separately gated.

## Run comparisons that can change the decisions

Freeze ROM/media SHA-256, CPU/FPU/MMU, RAM, video card, monitor, system and
boot budget for each case. Use separate writable clones. The same marketing
model with a different FPU, RAM size or video ROM is not a controlled pair.

| Experiment | Shared cases | Recorded result | Decision it resolves |
|---|---|---|---|
| Architectural CPU differential | Small 000/020/030 integer, exception, FPU and MMU vectors | Registers, memory effects, fault frames and agreed cycle boundary | Whether a specific CPU correction is needed |
| Timing probe | Plus and Mac II family, controlled guest probe | TimeDBRA, timers, SCC/VBL ordering | Whether clock/bus calibration should change |
| Application corpus | 512K/Plus, SE FDHD, IIx or SE/30 with matching video | Boot, launch, input, file round-trip and screenshots | Actual compatibility advantage on shared hardware |
| Floppy preservation | Sector, bitstream and protected/flux images | Boot/program interaction plus track/export identity | Which loader/native-medium feature is worth implementing |
| Snapshot continuation | Synthetic disks, then matching SCSI guest setup | Next transfer bytes, disk reads, guest filesystem round-trip | Rewind policy and serializer omissions |
| Network guest probe | Same Dayna driver/MacTCP, local test services | ARP/setup, DNS, TCP/UDP, captured frames | Setup/helpers versus transport defects |
| Throughput | Same host/settings/guest workload; POM interp and JIT reported separately | Host CPU time, elapsed time, guest work and correctness fingerprint | Whether any optimization is justified |

Snow differences are hypotheses to triangulate with Moira, MAME, Apple's ROM
behavior, specifications or hardware. Keep disputed cases as reproducible
fixtures. A comparator is not automatically the correct side of a mismatch.
POM68K's accelerated paths still require their interpreter locksteps.

## Deliver changes in reviewable increments

The first three implementation changes should be:

1. **ATA state regression and fix:** buffer restoration, disk rewind contract
   for the selected scope, archive-version update, component and Q630 proof.
2. **Media timeline policy and regression:** exact fresh-process SCSI/ATA
   behavior with writeback, changed-base detection and the selected clone or
   overlay owner. Keep any broad policy migration separate from the ATA fix.
3. **Debugger service MVP:** machine-thread commands/snapshots, pause/step/PC
   stop and basic views; opt-in interpreter execution with explicit UI status.

Then select CD source support, session files or native floppy media according
to the intended product use. Leave 68851 and Portable/PB100 as named projects,
not incidental extensions of a debugger PR.

Each change builds its touched targets, runs its regression and appropriate
label tier, and reports executed versus skipped asset gates. Extend the
existing GUI model/smoke tests for product controls. Update gate budgets and
regenerate `STATUS.md` when registration changes; record durable findings
in the changelog. Review file-size budgets and keep new responsibilities in
focused modules. No performance or compatibility promotion rests on a boot
screenshot or a soft-skipped test.

The eleventh increment replaces the IWM whole-byte writer with a magnetic
cell serializer grounded in [MAME 0.285's IWM model](https://github.com/mamedev/mame/blob/mame0285/src/devices/machine/iwm.cpp).
The shifter loads after seven chip clocks in asynchronous mode, emits each
transition at a half-window midpoint, and stops at the actual underrun deadline.
Mode and board clock select the spacing; synchronous writes replace the live
shifter. Partial-byte exit preserves the elapsed magnetic arc, including invalid
native fields. Motor-off transfers leave the medium unchanged. Snapshot v26
retains the live shifter, deadline and pending arc. Exact-edge, sector write-back
and fresh mid-byte replay tests cover Plus/SE/II clock wiring. This is a digital
controller qualification, not a measurement of Sony analog write electronics.

The twelfth increment implements the Ethernet ICMP address-mask half of order
10, using [RFC 950 Appendix I](https://www.rfc-editor.org/rfc/rfc950.html#appendix-I)
as the protocol authority. Snow always uses an IPv4 broadcast destination;
POM68K unicasts when the source address is known and broadcasts only for zero
source. Requests do not create address leases. The service uses the real
DaynaPort's existing packet path; no new peripheral or NAT engine is introduced.
At that increment RARP and guest automatic address setup were still open.

The thirteenth increment implements RFC 903 RARP on the existing DaynaPort
wire. The shared pool avoids MacIP, gateway and DNS collisions; MAC-bound
reservations survive IP refresh and idle time because RARP has no renewal
exchange. The real Dayna installer/MacTCP Server gate qualifies assignment,
local ICMP, EtherTalk AFP transfer and cable recovery. Unlike Snow's fixed
`gateway + 1` assignment, occupied addresses are skipped. BOOTP and guest
DNS/TCP after Server setup remain open; no device or state-format change.

The fourteenth increment qualifies that selected DNS/TCP follow-up with
NetProbe, a real 68k application using MacTCP Device Manager calls. The guest
validates an A-record response, uses the resolved address for TCP, exchanges
exact bytes and closes/releases the stream; real host socket peers corroborate
the traffic and EOF. RARP supplies the address, while the router is separately
configured in the panel before Server selection. A first attempt without that
router failed with MacTCP `ipRouteErr`, which is preserved rather than hidden
behind an emulator workaround. Apple's DNR, general DNS/public Internet
consumers, automatic routing and BOOTP are not qualified by this increment.

The fifteenth increment adds passive bidirectional Dayna PCAP capture with a
fixed observation queue, background I/O, visible losses/errors, machine-clock
metadata and GUI start/stop. Unlike Snow's unbounded host-time capture, it
keeps observational loss separate from guest transport and never persists
capture state in a guest snapshot. The real MacTCP gate records 741 frames
with zero observation loss and independently finds RARP, DNS and both TCP
payloads; tcpdump reads the same file. No hardware or BOOTP service is invented.

The sixteenth increment wires the SE board's third floppy mechanism. The SE,
SE FDHD and Classic ROMs' DiskSelect (B2E362A8 `$35316`, B306E171 `$35562`
and `$35CBE`, A49F9914 `$3F806`) drive VIA1 PA4 high for physical slot 1 and
low for slot 2 before ENABLE1 or the ISM drive-1 enable; slot 3 is ENABLE2.
POM68K had ignored PA4, so both slots reached one mechanism and all three
profiles' Finders mounted the boot floppy twice. Snow's
`get_selected_drive_idx` models the same line, and MAME `mac128.cpp` names
PA4 low/high upper/lower. PA4 low reaches the existing internal mechanism. A
second SE / SE FDHD mechanism on PA4 high is a board option, off by default
like the single-floppy SE; the Classic's PA4-high connector is always empty.
With it fitted, the ROM numbers PA4 high drive 1, tries it first and ejects a
non-startup disk there; three distinct floppies then mount as drives 1–3.
Snapshot v27 carries the line and mechanism. The startup option and disk
window row are separate product work; no new controller behavior is invented.

The seventeenth increment implements order 2, the basic debugger service
(`DebugSession`, `DebugCpuTarget`, « Fenêtres → Débogueur »). The GUI posts
typed commands with ids and reads immutable snapshots; the machine thread
owns every mutation. Pause is applied between two quanta, an architectural
boundary that needs no CPU support. A breakpoint or step stops from Moira's
own end-of-instruction check and holds the machine thread inside its
quantum without unwinding it, so the platform frame loops (vblank edges,
beam slices) resume unchanged. Memory is read only through the JIT
data-TLB `dataSpan` contract, so device registers are refused rather than
read; logical addresses use TT registers and side-effect-free 030/040
table walks. Any armed stop routes every instruction through the
interpreter (`!pomJitIdle()`), verified by the engine's retired-instruction
counter; the snapshot shows requested and effective engines. The debugger's
request bits no longer enter save states. `debug_session_test` runs a real
machine thread on 68000, 68020, 68030 and 68040 rigs (030/040 also with
the accelerated engine requested); `debug_inspection_test` proves the
table walks, untouched descriptors and byte-identical machine state after
inspecting I/O. Editing, access/exception stops, step over/out, histories,
symbols and device snapshots are order 3. Emscripten refuses stops.

The eighteenth increment opens order 3 with editing while stopped
(`SetRegister`, `WriteMemory`). Edits are refused while running. A PC
edit reloads the prefetch queue from the debugger's side-effect-free
logical read rather than through `Debugger::jump`'s timed bus refill; an
SR edit swaps stacks but is not an SR-writing instruction, so it arms no
trace or IRQ delay; registers a model lacks, odd or unreadable PCs, ROM
and device writes are refused. A memory edit writes all bytes through the
writable data span or none, and drops every translated block. The gate
edits a loop the accelerated 68030/68040 engines had already translated
and checks its invariant across JIT-run quanta; without the flush both
fail. MMU/cache register edits remain with the rest of order 3.

The nineteenth increment adds access and exception stops. Moira's
watchpoint sites now report width, direction and program/data space
(Moira row 35); the adapter decides there, mid-instruction, and arms a
soft stop, so a watchpoint stops after the accessing instruction retired
and an exception stop at the handler's first instruction with its frame
stacked. Watchpoints are logical and data-space only; an exception stop
names a vector, and vector 10 can be narrowed to one Toolbox/OS trap with
its flag bits ignored. Interrupt stops are accepted-vector stops (24–31);
an IPL pin transition is not exposed. `Debugger::reset` now keeps
`CHECK_CP` armed. Mutations of the program-space filter, the trap match
and the reset fix each fail the gate.

The twentieth increment adds step over and step out, judged at every
instruction boundary on the stack that was active when they were armed,
so a deeper recursive invocation never satisfies them (a temporary PC
would). Step over treats BSR, JSR, TRAP, A-line and F-line as calls and
also stops when the stack rises above its start (auto-pop traps); step out
stops after a return that began at the start depth. Breakpoints, access and
exception stops end a run; Pause cancels one. Ending a run consumes its
re-armed soft stop, which otherwise survived dormant and surfaced later as
an unrequested step — the gate's first draft missed that, because removing
the last breakpoint had masked it.

The twenty-first increment adds opt-in bounded histories: a 16 384-entry
instruction ring (clock, PC, opcode, SR, D/A registers at each boundary)
and a 1 024-entry exception ring (vector, stacked PC, A-line word), with
totals that make every drop visible, and a documented text export
(`src/DebugHistory.h`) headed by the session identity. Recording uses the
per-instruction soft stop and catchpoint guards, so a disabled history
adds nothing to the instruction loop. A new `maintain()` re-asserts that
soft stop after each command batch and at each quantum boundary: removing
the last breakpoint (or a reset) recomputed `CHECK_BP` without it.

A follow-up correction keeps the IWM reader and writer on one head angle.
The reader re-parks when the drive's revolution length changes (a zone seek or
a 400K PWM speed adoption); previously the 128K/512K could match a sector
header in a stale frame and write that sector's data field into another
slot. `iwm_read_test` compares a reader that spans the speed change with a
freshly parked one; both MFS write gates pass again.
