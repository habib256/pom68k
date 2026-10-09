# Snow and POM68K comparison

The most useful additions suggested by Snow are a usable debugger, preservation
formats for floppy media, more complete CD source handling, and reusable machine
sessions. The comparison also exposes a higher priority correctness issue in
POM68K's ATA save states. New machine families and a 68851 are larger projects
with separate admission criteria.

This comparison is pinned to Snow `23a41e787e7c4f58fc0915da8d6e35b61faabff8`
and POM68K `10700aa76b988e4e965e4974afc6dacebe0d39a3`, reviewed on
2026-10-08. “Present” means an identifiable implementation exists, not that
every application works. The ATA findings below have a synthetic executable
reproducer; the other comparisons are source and test-contract analysis.
There is no measured cross-emulator speed ranking or comprehensive runtime
compatibility ranking. The implementation sequence is in
[SNOW_IMPLEMENTATION_PLAN.md](SNOW_IMPLEMENTATION_PLAN.md).


## Implemented after the comparison (2026-10-08)

The source findings below describe the pinned baseline. The subsequent changes
are restricted to state present in real devices and differences between real
Macintosh profiles:

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
to its source bytes. Native track preservation and MOOF remain separate work.

The disk base remains the user's working file. Since the media timeline
increment (below), restoring a SCSI or ATA state rewinds both memory and a
write-back file to the state's exact content, including in a later process,
or refuses by name. A full IDE *application* rewind scenario on the Q630
remains separate work. The first six increments introduced no new CPU, machine
profile or fictional peripheral. Other items are proposals, not adopted features.


A seventh increment adds the real 512Ke and II FDHD profiles with append-only
snapshot IDs 40/41. The 512Ke has 512K soldered RAM, the Plus's 128K ROM
repeated through `$4FFFFF`, an always-decoded `$600000` RAM alias, M0110 input,
an 800K mechanism and no SCSI. The II FDHD keeps a 68020 and the original II
VIA identity pins, with SWIM/SuperDrive independently of CPU type and the
shared `97221136` ROM. Both use the catalogue-driven menu and typed relaunch.
The GLUE SWIM aperture now performs a single device access on word transfers,
reflecting one read on both lanes and giving the low write lane priority as
MAME's `iwm_r/iwm_w` specifies. The previous implementation consumed two FIFO
or parameter entries. `early_profile_test` exercises this through MMIO,
including an MFM address/data field write and sector read-back. Canonical HD
tracks also keep clocked MFM 4E gap4 bytes through the index boundary; the
old zero-cell padding raised a false long-gap error once per revolution.
The new gate checks cyclic flux spacing and reads past a whole revolution
using the ROM's own parameter table. The II FDHD now boots the immutable
1.44 MB System 6.0.8 Startup image to Finder and accepts ADB input.
`storage_profile_test` covers all 41 catalogue rows; `savestate_68k_test`
adds fresh-device deterministic continuation and sibling-identity rejection.
The new 512Ke gates boot System 3.3 in either drive and exercise M0110 input;
a Plus control run distinguishes the older system's mouse gain from a board
regression. New II FDHD gates cover real-ROM SCSI/floppy boot and ADB input.
This qualifies these fixtures, not an exhaustive application compatibility
claim. Native all-track storage and MOOF remain the next media project.

An eighth increment replaces live-track-only retention with `FloppyTrackMedium`.
Modified sector-backed tracks and all native MOOF tracks survive head/cylinder
changes, mode switches, reset and fresh restoration. `MoofImage` follows the
[Applesauce specification](https://applesaucefdc.com/moof-reference/) for MSB-first
exact-length bitstreams, FLUX-over-TMAP precedence, 255 continuation intervals,
optional zero CRC and 125 ns timing. It validates chunk bounds, absolute TRKS
extents and map indexes instead of reproducing Snow's unchecked slices/off-by-one
entry guard. Native writes retain physical transitions even for invalid fields.
Export preserves phase on a 125 ns bit grid and retains opaque chunks, without
sector reconstruction; representation choice and original compression size are
not retained. Guest write timing is quantized to the format's resolution.
Snapshot v26 carries the whole retained medium and the IWM write origin.
A pre-existing MOOF boots the real Plus ROM in both drives, and an independent
capture writer wraps the real HD startup disk for the II FDHD. A public mixed
bit/flux Oids capture round-trips all 160 tracks exactly under sanitizers.
These prove the medium lifecycle, not execution of every copy-protected title;
individual IWM write cells are now covered by the eleventh increment below.

A ninth increment adds weak-region read-amplifier pulses at the shared
SonyDrive flux output. Snow's IWM injects random bits after repeated zeroes;
the real mechanism belongs upstream of the controller, as explained in
[MAME's floppy hardware reference](https://docs.mamedev.org/techspecs/floppy.html).
The implementation adopts the approximate 16 us gain delay and 50%/4 us
distribution from [MAME 0.285](https://github.com/mamedev/mame/blob/mame0285/src/devices/imagedev/floppy.cpp),
with deterministic time-slot hashing instead of process-global randomness.
Later MAME source uses a different approximate distribution; neither supplies
a measured Sony analog response. Long transition-free gaps and unrecorded
tracks now produce variable IWM bytes; reads leave physical data and exports
unchanged. A fresh drive/controller restore resumes identical bytes. This
qualifies the read-channel mechanism, not a protected application's checks.

A tenth increment adds the missing application consumer: the original Oids
v1.4 capture (SHA-256 `63580ede7817cbf72bd8c053736707dcb8784f0eff2ac8de314606e173bb5a6f`)
from [CiderPress's MOOF corpus](https://github.com/fadden/CiderPress2/tree/7a055a200e31f752f3a92bb9fe6ae6f67cd55534/TestData/moof).
A real Plus ROM and System 6 boot independently, Finder launches OIDS, the
game loads a galaxy, and the production M0110 Option transition causes thrust.
Displayed pixels are accumulated across a second and compared with a neutral
branch at identical elapsed time. Fresh-machine replay must reproduce both
the display and complete state; the source capture remains immutable.
Interpreter and JIT have separate required-asset gates. Removing the only
FLUX-map track from a private copy prevents Finder mounting it, demonstrating
that this scenario needs the native path; it does not isolate a game protection
routine. This qualifies this nonstandard capture's launch/flight/replay, while
wider protection coverage still requires more independently identified cases.

## Decisions supported by the comparison

| Area | Snow | POM68K | Decision |
|---|---|---|---|
| CPU families | Own Rust 68000/020/030 interpreter | Moira 68000/020/030/040 plus threaded and native acceleration | Keep POM68K's CPU architecture |
| 68851 on Mac II | Explicit optional 020 plus PMMU configuration | 020 without external PMMU; 030/040 MMUs implemented | Separate compatibility project |
| Hardware timing | Bus wait states and device ticking; several explicit approximations | Machine clocks, event/batch scheduling, timing gates; open calibration work | Compare observables, not architecture slogans |
| Firmware controllers | Command/state models for ADB and Portable PMgr | Firmware paths for ADB, Egret/Cuda, IOPs and Duo PMU, with declared fallbacks | Preserve firmware qualification |
| Interactive debugger | Registers, memory, disassembly, stepping, stops, histories, symbols | Machine-thread service: pause, step, PC breakpoints, registers, logical/physical memory and disassembly without device reads; no editing, access/exception stops, histories or symbols yet | Basic service implemented; extend by stop type |
| Peripheral inspection | Common nested register/state view | Product controls, provenance, network/JIT statistics, specialized diagnostics | Add inspection snapshots, retain existing controls |
| Floppy preservation | DART, MOOF, A2R, PFI, PRI and optional Fluxfox formats | Raw/DC42 import; flux model behind IWM/SWIM | Confirmed format gap with a medium-model prerequisite |
| Floppy writeback | MOOF export/writeback; imported flux protected from writes | Raw/DC42 atomic persistence; current-track flux in states | Add native track preservation, retain existing sector exports |
| Compact sound | PCM conversion plus high-pass filtering | Line-latched PCM approximation | Snow is not a true PWM reference |
| CD sources | Per-track/file mapping, binary/WAVE, gaps, physical Windows drive | ISO/raw and single-source CUE/BIN, CD-DA | Extend source mapping, not CD audio from scratch |
| SCSI state portability | Embeds eligible disk/CD image payloads, restores temporary files | Content digest plus `.pomundo` reverse journal: exact rewind of memory and write-back file across processes, or a named refusal | Portable bundles remain optional |
| ATA snapshots | No corresponding ATA platform in inspected tree | PIO buffer restored (v21); same disk timeline as SCSI (v28) | Done |
| Session files | Workspace paths, machine options, windows, serial links | Typed startup, relaunch state, ImGui layout; no combined workspace file | Add a versioned session file |
| Ethernet NAT | smoltcp/socket implementation | Existing MacIP gateway reused behind EtherLink | Keep current gateway |
| MacTCP automatic setup | RARP and ICMP address-mask helper | RFC 950 mask and RFC 903 RARP services implemented, shared address pool | Server address setup, local ICMP and controlled guest DNS/TCP qualified; router configured separately |
| External Ethernet | Linux TAP; raw bridge marked broken | Internal EtherTalk and LToUDP external path, no direct DaynaPort TAP backend | Optional backend on a concrete topology |
| Network capture | PCAP capture on Ethernet device | Passive Dayna PCAP in both directions, bounded worker queue, machine-clock metadata and GUI start/stop | Guest-state-independent observation; losses/errors visible |
| Serial | Terminal and TCP on supported hosts, Unix PTY, GUI control | TCP/PTY on Unix, startup configuration, Windows transport stubs | Terminal plus Windows TCP are distinct tasks |
| Host file transfer | BlueSCSI Toolbox; temporary ISO from host files | AFP, HFS tools, guest disk agent | Already useful; Toolbox is optional interoperability |
| Printing | SCSI LaserWriter IISC raster output | AppleTalk LaserWriter PostScript/CUPS | Different printer classes, not a missing print function |
| Clipboard | Host text as keystrokes; guest TEXT scrap extraction | Input delivery and test typing helpers, no equivalent product bridge found | Useful after debugger inspection and input scheduling |
| Machine coverage | Includes 512Ke, II FDHD and Portable/PB100 paths | Broader desktop/040 catalogue and Duo 230; those profiles absent | Small profiles first, portable family separately |
| Host coverage | Includes Windows ARM64 build job | Windows x64, Linux, macOS Universal and WASM path | ARM64 Windows only if a target user needs it |
| Evidence | CPU tests, device tests, coverage, image/control-frame runner | Asset census, profile gates, pixel pins, guest oracles, JIT locksteps, sanitizers | Use both; a feature count is not a fidelity score |

## CPU and memory management

Snow owns its instruction decoder, execution, disassembler, FPU and PMMU in
[cpu_m68k][snow-cpu]. Its types explicitly distinguish a 68020 with a 68881
from a 68020 with both a 68851 and a 68881. The model-selection dialog exposes
the external PMMU, and the emulator constructs the corresponding Mac II bus.
This is more than a name in the README. Conversely, the inspected CPU types
stop at the 68030; there is no corresponding 68040 execution path.

POM68K uses its permanent Moira fork through [MoiraCpu.h](../src/MoiraCpu.h)
and board-specific wrappers. [Cpu020.cpp](../src/Cpu020.cpp) selects an 020 or
030 and a matching optional FPU. Its 020 JIT code probe explicitly assumes
no MMU; the 030 and 040 have translation, fault, cache and accelerated-path
contracts. A disassembler accepting 68851 opcodes does not establish an
external 68851 execution implementation.

The actionable gap is **Mac II with an optional external 68851**, not “add an
MMU”. A correct addition must distinguish 68851 registers, function codes,
table formats, protection, ATC operations and fault/restart behavior from
the 68030. Snow's [PMMU operations][snow-pmmu] still contain a specific-flush
TODO and an unknown-opcode fallback; it is an independent comparison partner,
not a complete specification. Start with a named program that requires the
configuration and synthetic architectural cases. Do not advertise A/UX
compatibility from translation tests alone.

Both emulators implement FPUs. Snow uses `arpfloat`; POM68K has its Moira FPU
fork and FPU/vector gates. Neither the language nor the arithmetic library
settles rounding, exception or transcendental compatibility. Compare exact
architectural outputs on shared cases before proposing an FPU replacement.

POM68K's JIT correctness is a separate strength: instruction boundaries,
exceptions, write restart and peripheral timing are exercised against its
interpreter. Snow adds a second implementation useful for triangulation,
especially on 000/020/030. It cannot replace that lockstep contract, and a
fast uncapped Snow session cannot validate POM68K's guest clock calibration.

## Clocks and fidelity

Snow's [CPU bus accesses][snow-cpu-bus] can retry a wait state, advance cycles
and synchronize the bus. Its compact bus advances devices in single-tick
increments to avoid missed horizontal/vertical transitions. Its Mac II bus
uses board dividers and explicitly leaves VIA wait states as a TODO. The
020+ bus code also documents a simplification of port widths and access costs.
These are concrete contracts, not a general claim of cycle exactness.

POM68K's [deviation inventory](LLE_VS_HLE.md) describes remaining timing
simplifications rather than hiding them. [Cpu020.cpp](../src/Cpu020.cpp)
retains a measured fixed-batch default with an event-driven opt-in; other
boards have their own deadlines. [TODO.md](../TODO.md) names open CPU-rate
calibration, VIA T1/IACK and SCC synchronization work. New features must use
machine time and must not accidentally substitute host pacing for device time.

The useful experiment is a shared guest probe of TimeDBRA, VIA timers,
TimeSCCDB, VBL and interrupt ordering with identical ROM, board, FPU and media.
A disagreement identifies a question. A hardware measurement, Apple's ROM
path or a relevant specification decides it. Copying Snow's clock constants
or replacing POM68K's scheduler wholesale would bypass the existing evidence.

## Firmware and machine coverage

Snow has three principal board groupings in the inspected source: compact,
Mac II and Portable. Its catalogue includes distinct 512Ke and Mac II FDHD
entries, along with an optional Portable RAM modification. The PB100 ROM
maps into the Portable path rather than a separate `MacModel` enum member.
Sources: [model definitions][snow-models] and [Portable PMgr][snow-pmgr].

POM68K's [MachineCatalog.h](../src/MachineCatalog.h) covers many more board
families, including 040 desktops, and the Duo 230. The catalogue is the
authority for both profile identity and storage capabilities. The 512Ke,
Mac II FDHD, Macintosh Portable and PB100 identities are absent at the pinned
revision. This supports two different projects:

- Add 512Ke and II FDHD as carefully gated extensions of existing platforms.
- Add a Normandy/LCD/Power Manager family for Portable/PB100. This is new
  board work, not two catalogue lines on the Duo.

Snow's PMgr interprets commands and owns handshake/battery/ADB state.
`sleep_request` recognizes the `MATT` signature but leaves entering sleep as
a TODO. Consequently, it does not provide a ready-made implementation for
POM68K's open Duo sleep/wake issue, and it does not demonstrate M50753
firmware execution. An optional command-level model in POM68K must be named
and reported as such; firmware qualification stays a separate goal.

The secondary-drive topology also deserves a targeted review. Snow's
[model drive table][snow-models] distinguishes two internal mechanisms plus
an external drive on the SE, whereas POM68K's product storage capabilities
and GUI expose internal/external rows. Confirm the actual board wiring and
drive select behavior before generalizing a third drive to every profile.

## Debugger and inspection

Snow's [command protocol][snow-comm] carries step, step over/out, breakpoint
changes, register edits, memory edits, disassembly and history requests. Its
frontend contains dedicated widgets for registers, memory, disassembly,
watch values, traps and peripherals. The emulator publishes status and
inspection events. This ownership boundary is the most useful design to
adapt: the GUI operates on commands and captured state rather than live CPU
objects.

POM68K already has:

- [Moira debugger primitives](../extern/moira/Moira/MoiraDebugger.h): address
  guards, exception catchpoints, stepping and a bounded instruction log.
- Side-effect-free disassembly reads in [MoiraCpu.h](../src/MoiraCpu.h).
- [MachineHost.h](../src/MachineHost.h): a machine-thread command boundary,
  safe media control and captured product status.
- Specialized traces and guest probes, plus GUI controls and diagnostic
  windows through [GuiSessionState.h](../src/GuiSessionState.h).

*Since 2026-10-09 the basic service exists* (`DebugSession.h`,
`DebugCpuTarget.h`, `DEV.md` § 6 *The debugger*): pause at a quantum
boundary, breakpoint and step stops held inside the quantum, registers,
TT/table-translated memory and disassembly read only through `dataSpan`,
and the effective engine. The paragraphs below describe the pinned
baseline and the parts still open.

The gap is the product service that connects these pieces. Existing
`peek8`/disassembly helpers still need an explicit logical-versus-physical
contract on MMU machines. A debugger must not read an IWM or SCC register
through the live bus just to display it. Likewise, modifying RAM must
invalidate accelerated code through the same mechanisms as guest writes.

Start with pause, a coherent register/memory snapshot, one instruction,
disassembly and a PC stop. Then add safe edits, access stops, exception/trap
stops, step over/out and bounded histories. “Watch a value” and “stop on bus
access” are distinct operations even though some interfaces call both
watchpoints. Define whether each stop is before execution, after retirement
or after fault delivery, and show that reason to the user.

For the first version, force the interpreter while debugger stops or full
instruction history are armed, and display the effective engine. POM68K's
[JitEngine.cpp](../src/jit/JitEngine.cpp) already treats debug state as a reason
to delegate to Moira; verify rather than assuming all new stop types are
covered. Native debugging without missed stops is a later admission.

Snow also supplies [ROM/low-memory symbol maps][snow-disassembly] and a
searchable global-variable catalog. POM68K has ROM analysis tools but no
equivalent symbol-oriented product view. Bind symbols to the ROM identity,
not only the marketing model. Nested peripheral inspection should retain
POM68K's LLE/HLE provenance window and network controls; those serve different
purposes.

## Floppy media and preservation

Snow separates a [FloppyImage][snow-floppy] from the controller and mechanism,
and has independent loaders for DART, DC42, raw, MOOF, A2R, PRI and PFI.
Its [media manual][snow-floppy-manual] distinguishes imported flux, bitstream
and sector images. Imported flux is currently write-protected, while MOOF
is the writable export/writeback format. Fluxfox broadens imports and is
enabled by the floppy crate's default features.

POM68K's [SonyDrive::insert](../src/SonyDrive.cpp) accepts raw sector images
of its supported sizes and DC42. DC42 tag data is removed from the imported
sector payload, and [floppy_persist_test.cpp](../tests/floppy_persist_test.cpp)
records writing tagged DC42 back without its tag block. This is a specific
preservation limitation, not proof that ordinary Mac OS floppy use fails.

POM68K already has a real flux store, controller timing and
[FluxPll.h](../src/FluxPll.h). The missing layer is **persistent native media
per track and side**: `SonyDrive` reconstructs the selected track from
`image_` when it changes track or resets. Importing a protected bitstream and
then synthesizing the next track from sectors would discard the property
the import was supposed to preserve. The existing current-track flux
snapshot does not constitute an all-track preservation container.

A useful progression is DART/DC42 tag preservation first, then a medium
object retaining sector, bitstream and flux tracks, then MOOF read/write,
then A2R/PFI/PRI imports as demonstrated by an actual corpus. Preserve the
existing raw/DC42 writes for representable media and explicitly refuse lossy
export of nonstandard tracks unless the user chooses a conversion.

Snow's own boundaries matter: its IWM flux path logs unsupported flux writes,
its ISM has a flux-track TODO, and its weak-bit path introduces host RNG
noise. POM68K's deterministic jitter and save-state/replay contract should
be retained. Weak regions and multiple revolutions need explicit semantics
and a persisted seed; importing a file extension alone proves neither.
Sources: [Snow IWM][snow-iwm] and [Snow ISM][snow-ism].

## Video and audio

Snow implements Toby, SE/30 internal video and a [Display Card 8-24
model][snow-mdc]. The latter has monitor sense, programmable base/stride,
palette and 24-bit direct colour. POM68K has [TobyVideo](../src/TobyVideo.h),
SE/30 video and the integrated video devices of its much broader desktop
catalogue, including DAFB, Sonora and Valkyrie paths. Consequently, “add
colour or high resolutions” is not the gap. The missing capability is that
particular slot-card/monitor configuration on the Mac II family.

Toby is already installable through [MacIIMemory](../src/MacIIMemory.cpp),
which contains distinct model wiring; adding an 8-24 card should reuse
[NuBusDevice](../src/NuBus.h) and expose typed card selection. A second card
also makes arbitration and multi-display ownership meaningful. Snow's
[Mac II bus][snow-macii-bus] has a multiple-card renderer TODO and non-video
card TODO, so it does not establish complete general NuBus/multi-monitor
support. In both projects test slot IRQ, declarations, palette accesses,
stride, raster timing and save/load through the guest's driver.

Both projects emit compact audio from line-rate samples and implement ASC
audio on their supported newer boards. POM68K's [MacAudio](../src/MacAudio.h)
explicitly approximates one-bit PWM as linear PCM. Snow's
[compact audio][snow-compact-audio] also converts the byte linearly; its
[filter][snow-audio-filter] is a 10 Hz high-pass filter. It is not a model
of PWM pulse widths and the physical integration circuit. This may be a
useful DC-removal option, but does not close POM68K's existing PWM fidelity
task. Retain POM68K's line-latching and guest-clock timing.

For sound, record guest-produced PCM at the device output separately from
host resampling/mixing. Compare the chime, a sustained tone, rapid volume
changes and CD play/pause. A hardware recording or circuit response is
needed to justify changing the compact transfer function; subjective
speaker output from two differently configured hosts is insufficient.

## SCSI and CD handling

Both emulators implement SCSI disks and CD audio. POM68K additionally has
53C96/TurboSCSI, formatter commands, hot attachment with a guest mount agent,
bare-HFS partition facades and ATA for F108 boards. These are existing
capabilities, with evidence in [ScsiDisk](../src/ScsiDisk.h),
[SCSI_HOTPLUG.md](SCSI_HOTPLUG.md) and the corresponding gates.

Snow's [CUE backend][snow-cue] builds source mappings for individual files,
supports BINARY and WAVE, and parses gaps/indexes. POM68K's `cueDataFile`
retains one file string as it scans the sheet, ultimately keeping the last
`FILE`; track indexes are then interpreted against that source. A two-file
CUE therefore cannot be represented correctly by that model. It also has
no comparable WAVE decoding/source map. This is a source-level format gap
despite both projects truthfully supporting some CUE/BIN discs.

Extract a `CdImage`/track-source owner before extending the SCSI commands.
Generate tiny two-file CUE and BINARY/WAVE fixtures; assert absolute LBA,
TOC, index-zero/gap behavior and actual PCM. Keep CD transport on machine
time. Existing mono-source discs and the CD game gate must remain unchanged.

Snow also has a physical-CD backend on Windows and can generate a temporary
ISO from host files. Those are convenience options. POM68K already offers
AFP and HFS construction, so physical media or ISO authoring needs a named
workflow before becoming a core dependency.

## Save states and disk timelines

Snow's [state writer][snow-save] serializes configuration, uses Zstd and
includes eligible SCSI disk/CD image payloads. Loading recreates backing
files in a temporary directory; a branch operation commits the chosen disk
to a new file. Its [manual][snow-save-manual] also describes thumbnails,
temporary quick slots and a cross-version compatibility warning. This gives
the user a coherent restored disk timeline at a potentially large I/O cost.

POM68K uses versioned chunks, one shared `visit` method, identity checks and
zero-run compression in [SaveState.h](../src/SaveState.h). Host resources and
compiled code are rebound or regenerated. This architecture need not change.
[SonyDrive.h](../src/SonyDrive.h) carries floppy data and the current flux.
[ScsiDisk.h](../src/ScsiDisk.h) carries changed blocks and uses an in-memory
original-block log to undo later writes during restoration.

There are two distinct limitations:

1. **SCSI persistence policy.** The code explicitly says restoring does not
   undo writes already sent to the host file. A new process lacks the earlier
   process's original-block log. A same-process, writeback-off gate therefore
   cannot establish arbitrary rewind across a process restart with a modified
   backing image. The requested behavior must be decided and tested.
2. **ATA correctness.** `AtaDisk::visit` records the transfer offset, task file
   and flags but omits `buffer_` and any disk-content undo/delta mechanism.
   `Q630Memory::visit` calls that visitor, so this is on the product state path.

The ATA omissions were reproduced with a synthetic two-sector image, using
the actual `AtaDisk` and `sav::Writer`/`Reader` implementations:

```text
mid-read restore: expected=5A5A actual=0000 archive_ok=1
disk rewind: expected=5A5A actual=A5A5 archive_ok=1
```

The first sequence opens a disk filled with `0x5A`, starts READ SECTORS,
consumes one word, saves, and restores into a freshly opened drive. The next
word is zero instead of `0x5A5A`. The second saves before writing an entire
sector as `0xA5A5`, restores in the same instance and reads it: the later write
survives. Neither case needs an Apple asset. Build the reproducer with
`clang++ -std=c++20 -Isrc <probe.cpp> src/SaveState.cpp`; the session's probe
is `/tmp/pom68k-snow-ata-snapshot-probe.cpp`.

Correct the ATA transfer state first. Then decide the shared SCSI/ATA disk
timeline contract: immutable base plus versioned overlay, or complete image
snapshot/temporary clone. An image's size/path alone is insufficient to bind
a state to its original contents. Thumbnails and quick slots come after the
correctness contract, not before it.

Both limitations are now resolved. The ATA transfer state has been restored
since format v21. Since v28, `DiskTimeline` binds every SCSI/ATA state to its
content digest and rewinds a write-back file through its `.pomundo` reverse
journal, or refuses by name (`media_timeline_test`,
[DEV.md § 1.4](../DEV.md)).

## Networking and serial interfaces

Snow's [NAT implementation][snow-nat] uses smoltcp plus host sockets; the
helpers implement RARP and ICMP address-mask replies. Its [Ethernet
device][snow-ethernet] supports TAP and PCAP capture. The raw-bridge Cargo
feature is explicitly marked broken, so it should not be treated as a
working portability solution. The optional [HTTPS adapter][snow-https]
connects an HTTP guest flow to a TLS host flow using the request's host.

POM68K already has TCP/UDP NAT and DNS via [MacIpGateway](../src/MacIpGateway.h),
Ethernet/ARP via [EtherLink](../src/EtherLink.h), and internal EtherTalk via
[EtherTalkLink](../src/EtherTalkLink.h). LToUDP plus the netatalk/TashRouter
tools connects external AppleTalk networks. A TAP attached directly to
DaynaPort is nevertheless a different missing topology; internal EtherTalk
is not a general external Ethernet bridge.

The bounded additions are RARP/address-mask setup and PCAP capture. Add a
TAP backend only when a concrete LAN/bridge workflow justifies it. Maintain
the existing NAT by default, and avoid duplicating the TCP engine. HTTPS
adaptation is a separate host service experiment: encryption ends on the
host, and modern JavaScript/layout/compression can still defeat the guest
browser. A successful TLS handshake does not mean the page works.

Snow's serial [bridge module][snow-serial] has a platform-neutral TCP backend
and a Unix PTY backend; its frontend includes per-port terminals and bridge
status. POM68K's [SerialHostTransport](../src/SerialHostTransport.cpp) supports
nonblocking Unix TCP/PTY but stubs both on Windows. Its rate/FIFO constraints
and LocalTalk ownership policy are already deliberate. Add a terminal sink
and endpoint control around the existing transport; port TCP to Winsock as
a separate platform task. Do not bypass the SCC receive FIFO or let a
terminal and LocalTalk independently drive printer-port state.

## Usability and host integration

Snow's [workspace][snow-workspace] stores machine/assets, display settings,
windows and link configuration with paths relative to the workspace.
POM68K has typed startup configuration, staged relaunch, persistent PRAM and
ImGui layout, but no equivalent named file covering the whole machine setup.
A session file should feed the existing startup decoder, with a documented
precedence and missing-file diagnostics; it must not become a second machine
catalogue or a save-state substitute.

Snow's [frontend][snow-app] also types host clipboard text as key events and
extracts guest TEXT scrap for copying back. POM68K can deliver input and
already tests guest typing, but no product clipboard bridge was found.
The easy direction is queued typing. It still needs keyboard-layout,
MacRoman/newline, cancellation and guest-clock pacing rules. Guest-to-host
copy requires bounded handle/pointer inspection, including MMU translation,
and should report unsupported/nonresident scrap instead of manipulating
Mac OS memory silently.

Snow's [BlueSCSI protocol][snow-toolbox] is an interoperability option for
guest tools; POM68K's AFP is already the normal shared-folder workflow.
Its [SCSI printer][snow-printer] implements LaserWriter IISC raster commands,
while POM68K's PAP printer receives PostScript and can send it to CUPS.
Implementing the former serves particular drivers, not a generic lack of
printing. Each requires a named consumer before expanding SCSI surface.

## Testing and delivery

Snow's [test workflow][snow-ci] covers formatting/lint, feature combinations,
CPU/device tests and coverage; its [guest runner][snow-runner] records frames
and compares a control frame while replaying input. Its Windows build matrix
includes x64 and ARM64. POM68K's registry, census and recorded runs are in
[STATUS.md](../STATUS.md), including asset requirements and host-specific
JIT gates. The roadmap explicitly acknowledges missing asset-runner and
Windows execution evidence.

These are complementary evidence systems. Do not compare their raw test
counts: generated vectors, test functions and profile gates count different
units. Nor does an ARM64 Windows build prove device correctness on that host.
The immediately useful improvement is to make every new gap executable on
asset-free CI where possible, and to publish executed/skipped counts for
asset-backed comparisons. Keep existing gates that Snow's smaller hardware
scope cannot exercise.

The largest unresolved comparison is application compatibility at identical
hardware settings. The next experiments in the plan resolve that with a
shared corpus and guest observables. Source inspection gives implementation
scope; it does not establish a winner for every game or a measured speedup.

## Snow source references

All links below refer to the reviewed commit, so later upstream changes do
not silently alter the comparison.

[snow-cpu]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/cpu_m68k/mod.rs
[snow-pmmu]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/cpu_m68k/pmmu/ops.rs
[snow-cpu-bus]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/cpu_m68k/bus.rs
[snow-models]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/mod.rs
[snow-pmgr]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/portable/pmgr.rs
[snow-comm]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/emulator/comm.rs
[snow-disassembly]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/frontend_egui/src/widgets/disassembly.rs
[snow-floppy]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/floppy/src/lib.rs
[snow-floppy-manual]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/docs/src/manual/media/floppies.md
[snow-iwm]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/swim/iwm.rs
[snow-ism]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/swim/ism.rs
[snow-mdc]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/nubus/mdc12.rs
[snow-macii-bus]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/macii/bus.rs
[snow-compact-audio]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/compact/audio.rs
[snow-audio-filter]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/audio_filter.rs
[snow-cue]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/scsi/cdrom/backends/cuesheet.rs
[snow-save]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/emulator/save.rs
[snow-save-manual]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/docs/src/manual/savestates.md
[snow-nat]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/nat/src/lib.rs
[snow-ethernet]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/scsi/ethernet.rs
[snow-https]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/nat/src/https_stripping.rs
[snow-serial]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/serial_bridge.rs
[snow-workspace]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/frontend_egui/src/workspace.rs
[snow-app]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/frontend_egui/src/app.rs
[snow-toolbox]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/scsi/toolbox.rs
[snow-printer]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/core/src/mac/scsi/printer.rs
[snow-ci]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/.github/workflows/tests.yml
[snow-runner]: https://github.com/twvd/snow/blob/23a41e787e7c4f58fc0915da8d6e35b61faabff8/testrunner/src/bin/single/main.rs

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

A twelfth increment adds the existing Ethernet gateway's ICMP address-mask
service. [RFC 950](https://www.rfc-editor.org/rfc/rfc950.html#appendix-I), rather
than Snow's unconditional IPv4 broadcast reply, decides the destination:
unicast for an addressed client, broadcast for source zero. Packet bounds,
IPv4/ICMP checksums, fragment flags, card MAC and local addressing are checked.
Configured masks, identifier/sequence, padding, IPv4 options, machine-time
latency and unplug behavior are covered through DaynaPort SCSI WRITE/READ,
followed by a normal gateway echo round trip. No NAT lease is assigned by
mask discovery. RFC 903 RARP now reserves addresses in the shared MacIP/
Ethernet pool. Requests are bounded and checked against the attached card;
gateway, DNS and occupied addresses are excluded. Unlike Snow's fixed
`gateway + 1` rule, a MacIP client occupying .2 makes RARP choose .3.
The MAC binding survives ordinary IP traffic and idle learned-lease expiry:
RARP provides no renewal exchange. Detach/reconfiguration releases it.
`q605_dayna_rarp_etalon` qualifies actual MacTCP 2.0.6 Server configuration,
RARP assignment and subsequent local ICMP using the original Dayna driver.
`q605_dayna_rarp_network_etalon` also runs NetProbe's own UDP A-record client
and TCP exchange through real MacTCP calls and controlled host sockets.
The router is separately configured before selecting Server; no automatic
router discovery or Apple DNR/control-panel DNS-list coverage is claimed.
