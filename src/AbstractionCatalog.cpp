// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See AbstractionCatalog.h. Every class below is the one docs/LLE_VS_HLE.md
// gives (its master table lists the same ids, abstraction_catalog_test
// compares the two); the evidence and the reopening conditions live there.
//
// The strings are drawn by the window with ImGui's default font, which has
// no em-dash, no arrows and no typographic quotes: keep them ASCII
// (gui_windows_test scans this file for glyphs the font lacks).

#include "AbstractionCatalog.h"

#include <cstring>

namespace pom68k::abstraction {

const char* levelBadge(Level level) {
    switch (level) {
    case Level::PureLle: return "LLE";
    case Level::LleSimplified: return "LLE~";
    case Level::HleReplacement: return "HLE";
    case Level::HleHack: return "HACK";
    case Level::HostConvenience: return "HOST";
    }
    return "?";
}

const char* levelName(Level level) {
    switch (level) {
    case Level::PureLle: return "Pure LLE";
    case Level::LleSimplified: return "LLE simplified";
    case Level::HleReplacement: return "HLE replacement";
    case Level::HleHack: return "HLE hack";
    case Level::HostConvenience: return "Host convenience";
    }
    return "?";
}

bool levelIsLle(Level level) {
    return level == Level::PureLle || level == Level::LleSimplified;
}

namespace {

using P = PlatformKind;
constexpr PlatformMask bits(std::initializer_list<PlatformKind> kinds) {
    PlatformMask m = 0;
    for (PlatformKind k : kinds) m |= platformBit(k);
    return m;
}

constexpr PlatformMask kNoCompact68000 =
    kAllPlatforms & ~bits({P::Compact, P::Portable});
constexpr PlatformMask k040Boards =
    bits({P::MemcJr, P::DjMemc, P::Spike, P::F108});
constexpr PlatformMask kEgretCudaBoards =
    bits({P::V8, P::Rbv, P::Sonora, P::Vasp, P::MemcJr, P::F108, P::Spike});
constexpr PlatformMask kPowerBook030 =
    bits({P::Jaws, P::Niagra, P::Pangola});

const std::vector<SnapMachine> kNoAdb = {
    SnapMachine::Mac128K, SnapMachine::Mac512K, SnapMachine::Mac512Ke,
    SnapMachine::Plus};
const std::vector<SnapMachine> kNoScsi = {SnapMachine::Mac128K,
                                          SnapMachine::Mac512K};

} // namespace

// Grouped for the window, not in the document's order. A group must be one
// contiguous run (the window prints a heading whenever it changes).
const std::vector<Entry>& catalogue() {
    static const std::vector<Entry> kCatalogue = {
    // ── CPU ──────────────────────────────────────────────────────────────
    {.id = "cpu-68000", .group = "CPU", .subsystem = "68000 core (Moira)",
     .level = Level::PureLle,
     .modelled = "Cycle-exact 68000 with RAM contention; a VIA access is a "
                 "/VPA cycle landed on the E clock, peripherals synced "
                 "before every bus access.",
     .whyNot = "This is the floor: bus-cycle timing is the silicon's own "
               "(compact_timing_etalon).",
     .files = "extern/moira/Moira/Moira.h src/Cpu68k.h src/Cpu68k.cpp "
              "src/PortableCpu.h",
     .platforms = bits({P::Compact, P::Portable}), .docSection = "1.2"},
    {.id = "cpu-680x0", .group = "CPU",
     .subsystem = "68020 / 68030 / 68040 cores (Moira)",
     .level = Level::LleSimplified,
     .modelled = "Instruction-exact cores; peripherals advance on a "
                 "device-derived deadline (8 boards) or a fixed batch "
                 "(Mac II family, IIfx, Duo).",
     .whyNot = "Not bus-cycle exact: IRQ jitter up to a few microseconds "
               "where a batch remains, and no gate can see it yet.",
     .files = "src/Cpu020.h src/Cpu030.h src/Cpu040.h src/IIfxCpu.h "
              "src/RbvCpu.h src/SonoraCpu.h src/VaspCpu.h src/CentrisCpu.h "
              "src/Q700Cpu.h src/Q630Cpu.h src/MscCpu.h src/Pb030Cpu.h "
              "src/Moira040Cpu.h src/Moira030Cpu.h",
     .platforms = kNoCompact68000, .docSection = "1.2"},
    {.id = "cpu-mmu", .group = "CPU",
     .subsystem = "68030 PMMU / 68040 MMU and ATC",
     .level = Level::PureLle,
     .modelled = "Table walks, ATC with exact flush semantics, faults and "
                 "their stack frames.",
     .whyNot = "Architectural model; the JIT refuses the big soft TLB a "
               "classic 68k JIT uses.",
     .files = "extern/moira/Moira/MoiraExecMMU_cpp.h "
              "extern/moira/Moira/MMU/MMU030.cpp src/Mmu030Walk.h",
     .platforms = kNoCompact68000, .docSection = "1.11"},
    {.id = "cpu-fpu", .group = "CPU", .subsystem = "68881 / 68882 / 68040 FPU",
     .level = Level::PureLle,
     .modelled = "Extended-precision arithmetic, FSAVE/FRESTORE frames and "
                 "exceptions, checked against the differential oracle.",
     .whyNot = "Architectural model at the instruction boundary.",
     .files = "extern/moira/Moira/MoiraExecFPU_cpp.h",
     .platforms = kNoCompact68000, .docSection = "4.2"},
    {.id = "cpu-cache040", .group = "CPU", .subsystem = "68040 caches",
     .level = Level::LleSimplified,
     .modelled = "Default: the i-cache is a throughput scaler on Moira "
                 "cycles. Opt-in architectural I/D cache (POM68K_040_DCACHE=1): "
                 "lines, copyback, snooping, CPUSH/CINV.",
     .whyNot = "The architectural cache stays opt-in until its native paths "
               "are measured (docs/CACHE_040.md).",
     .files = "src/Cpu040.h docs/CACHE_040.md",
     .platforms = k040Boards, .docSection = "1.2"},
    {.id = "cpu-engine", .group = "CPU",
     .subsystem = "Execution engine (interpreter / JIT)",
     .level = Level::PureLle,
     .modelled = "The Moira interpreter, or the accelerated engine: bit-exact "
                 "against it (registers, stacks, cycle clock) at every "
                 "instruction boundary.",
     .whyNot = "Not a hardware deviation: the interpreter is the accuracy "
               "oracle and every backend is gated in lockstep with it.",
     .files = "src/jit/JitEngine.h src/jit/POM68K_JIT.md",
     .platforms = kAllPlatforms, .docSection = "1.11",
     .live = LiveSource::CpuEngine},

    // ── Board logic ──────────────────────────────────────────────────────
    {.id = "memory-map", .group = "Board logic",
     .subsystem = "Memory controllers and address decode",
     .level = Level::PureLle,
     .modelled = "Each board's map, ROM overlay, bus errors and wait states "
                 "(GLUE, OSS, V8, RBV, Sonora, VASP, MEMCjr, djMEMC, F108, MSC, "
                 "Jaws/Niagra, Portable).",
     .whyNot = "The registers and maps the ROM programs, as the board has them.",
     .files = "src/MacMemory.h src/MacIIMemory.h src/IIfxMemory.h "
              "src/V8Memory.h src/RbvMemory.h src/SonoraMemory.h "
              "src/VaspMemory.h src/Q605Memory.h src/CentrisMemory.h "
              "src/Q700Memory.h src/Q630Memory.h src/MscMemory.h "
              "src/Pb030Memory.h src/PortableMemory.h",
     .platforms = kAllPlatforms, .docSection = "3"},
    {.id = "via", .group = "Board logic", .subsystem = "VIA 6522 and E clock",
     .level = Level::PureLle,
     .modelled = "Timers, shift register (including the external-CB1 ADB "
                 "shift), handshakes, on the exact 783.36 kHz E-clock grid.",
     .whyNot = "This is the floor.",
     .files = "src/Via6522.h src/Via6522.cpp src/ViaEClock.h",
     .platforms = kAllPlatforms, .docSection = "3"},
    {.id = "pseudo-via", .group = "Board logic",
     .subsystem = "Pseudo-VIA (integrated VIA2)",
     .level = Level::PureLle,
     .modelled = "The gate arrays' VIA2 subset: slot and ASC interrupts, "
                 "level-triggered, decode as the chip has it.",
     .whyNot = "This is the floor.",
     .files = "src/PseudoVia.h src/PseudoVia.cpp src/Q605Memory.h",
     .platforms = bits({P::V8, P::Rbv, P::Sonora, P::Vasp, P::MemcJr,
                        P::F108, P::Msc, P::Jaws, P::Niagra, P::Pangola}),
     .docSection = "3"},
    {.id = "nubus", .group = "Board logic",
     .subsystem = "NuBus slots and declaration ROMs",
     .level = Level::LleSimplified,
     .modelled = "Slot windows, declaration ROM at the top of each slot, "
                 "slot IRQs to VIA2.",
     .whyNot = "No arbitration or timeout cycles: closing it needs a second "
               "card that contends for the bus.",
     .files = "src/NuBus.h src/NuBus.cpp src/DeclRom.h src/DeclRom.cpp",
     .platforms = bits({P::Glue, P::Oss}), .docSection = "1.8"},
    {.id = "iop", .group = "Board logic",
     .subsystem = "Apple PIC I/O processors (SCC / SWIM-ADB IOPs)",
     .level = Level::PureLle,
     .modelled = "R65C02 core, timer, DMA, host mailbox; the 65C02 firmware "
                 "is downloaded by the machine ROM and always runs.",
     .whyNot = "The real firmware executes on a real core.",
     .files = "src/ApplePic.h src/ApplePic.cpp src/R65c02.cpp docs/IOP_BRINGUP.md",
     .platforms = bits({P::Oss, P::Spike}), .docSection = "1.2"},

    // ── Serial and networking ────────────────────────────────────────────
    {.id = "scc", .group = "Serial and networking", .subsystem = "SCC 8530",
     .level = Level::LleSimplified,
     .modelled = "Full SDLC/LLAP and async engines, baud rates from the "
                 "machine clocks, RTS/DTR pins, Rx FCS.",
     .whyNot = "Byte-granular engines: no bit sampling, TRxC and DPLL clock "
               "sources unmodelled. Only worth it with a line-timed transport.",
     .files = "src/Scc8530.h src/Scc8530.cpp",
     .platforms = kAllPlatforms, .docSection = "1.4"},
    {.id = "daynaport", .group = "Serial and networking",
     .subsystem = "DaynaPort SCSI/Link Ethernet",
     .level = Level::LleSimplified,
     .modelled = "The SCSI command set of SLINKCMD.TXT; frames reach the "
                 "in-process gateway.",
     .whyNot = "Multicast filtering and the dropped-packet report are "
               "omitted: no driver is observed to depend on them.",
     .files = "src/DaynaPort.h src/DaynaPort.cpp",
     .platforms = kAllPlatforms, .docSection = "1.5",
     .exceptProfiles = kNoScsi},
    {.id = "appletalk-stack", .group = "Serial and networking",
     .subsystem = "In-process AppleTalk peer (AFP, PAP)",
     .level = Level::LleSimplified,
     .modelled = "The network's other end at the SCC wire: real LLAP frames, "
                 "DDP/RTMP/ZIP/NBP/ATP, file and print servers.",
     .whyNot = "lapCTS is synthesized locally instead of crossing the wire: "
               "only meaningful with a second live node.",
     .files = "src/AtalkStack.h src/AfpServer.h src/PapServer.h",
     .platforms = kAllPlatforms, .docSection = "1.10"},
    {.id = "network-host", .group = "Serial and networking",
     .subsystem = "Host network bridges (LToUDP, MacIP / Ethernet NAT)",
     .level = Level::HostConvenience,
     .modelled = "Carry LLAP frames to other emulators and IP to the host "
                 "network; the guest only ever sees a peer on its wire.",
     .whyNot = "No Macintosh hardware is replaced: these sit past the "
               "emulated cable or card.",
     .files = "src/LtoUdp.h src/MacIpGateway.h src/EtherLink.h "
              "src/EtherTalkLink.h",
     .platforms = kAllPlatforms, .docSection = "3"},

    // ── Storage ──────────────────────────────────────────────────────────
    {.id = "floppy-iwm", .group = "Storage",
     .subsystem = "IWM / SWIM (ISM) floppy controller",
     .level = Level::LleSimplified,
     .modelled = "IWM read and write modes on a window state machine over "
                 "flux; the SWIM ISM's LS-pair/CSM/TSM engine.",
     .whyNot = "Three simplifications with reopening conditions, none "
               "symptom-backed (off-rate flux leaves with the head).",
     .files = "src/Iwm.h src/Iwm.cpp src/Swim1.h src/Swim1.cpp",
     .platforms = bits({P::Compact, P::Glue, P::Oss, P::V8, P::Rbv, P::Vasp,
                        P::Spike, P::Jaws, P::Niagra, P::Pangola,
                        P::Portable, P::Msc}),
     .docSection = "1.3"},
    {.id = "floppy-swim2", .group = "Storage",
     .subsystem = "SWIM2 floppy controller",
     .level = Level::LleSimplified,
     .modelled = "MFM and GCR bit engines with serial CRC, over a FluxPll "
                 "data separator.",
     .whyNot = "As the IWM: the store keeps only the live track.",
     .files = "src/Swim2.h src/Swim2.cpp",
     .platforms = bits({P::V8, P::Sonora, P::MemcJr, P::DjMemc, P::F108}),
     .docSection = "1.3"},
    {.id = "floppy-drive", .group = "Storage",
     .subsystem = "Sony drive and flux store",
     .level = Level::LleSimplified,
     .modelled = "Tracks stored as flux transitions with rotational latency; "
                 "only CRC-valid sectors commit.",
     .whyNot = "Off-rate flux does not survive the head leaving the cylinder.",
     .files = "src/SonyDrive.h src/SonyDrive.cpp src/SonyDriveFlux.cpp",
     .platforms = kAllPlatforms, .docSection = "1.3"},
    {.id = "scsi-5380", .group = "Storage", .subsystem = "SCSI NCR 5380",
     .level = Level::LleSimplified,
     .modelled = "Register and phase engine, pseudo-DMA handshake per byte.",
     .whyNot = "Phase-faithful but not wire-paced.",
     .files = "src/Ncr5380.h src/Ncr5380.cpp",
     .platforms = bits({P::Compact, P::Glue, P::Oss, P::V8, P::Rbv,
                        P::Sonora, P::Vasp, P::Msc, P::Jaws, P::Niagra,
                        P::Pangola, P::Portable}),
     .docSection = "1.5", .exceptProfiles = kNoScsi},
    {.id = "scsi-53c96", .group = "Storage", .subsystem = "SCSI NCR 53C96",
     .level = Level::LleSimplified,
     .modelled = "Scheduled selection and transfer delays, FIFO content, "
                 "PrimeTime TurboSCSI wait states.",
     .whyNot = "Four audited omissions (FIFO pacing, SDTR, 16-bit widths, "
               "target mode); no Mac driver observes them.",
     .files = "src/Ncr53c96.h src/Ncr53c96.cpp",
     .platforms = k040Boards, .docSection = "1.5"},
    {.id = "scsi-target", .group = "Storage",
     .subsystem = "SCSI disk and CD-ROM targets",
     .level = Level::LleSimplified,
     .modelled = "The SCSI-2 surface a formatter reads, mode pages, CD TOC "
                 "and audio.",
     .whyNot = "MODE SELECT is accepted and discarded; the page-4 geometry "
               "is invented (an image has no platters).",
     .files = "src/ScsiDisk.h src/ScsiDisk.cpp src/CdImage.h",
     .platforms = kAllPlatforms, .docSection = "1.5",
     .exceptProfiles = kNoScsi},
    {.id = "ata", .group = "Storage", .subsystem = "ATA (IDE) disk",
     .level = Level::LleSimplified,
     .modelled = "Task-file registers, software reset and signature, "
                 "IDENTIFY, PIO READ/WRITE SECTORS; the ROM's ATA Manager runs.",
     .whyNot = "PIO only: no DMA and no ATAPI.",
     .files = "src/AtaDisk.h",
     .platforms = bits({P::F108, P::Msc}), .docSection = "-"},
    {.id = "media-formats", .group = "Storage",
     .subsystem = "Media formats and the flat-HFS facade",
     .level = Level::HostConvenience,
     .modelled = "Disk and CD image formats, and a synthesized partition map "
                 "and driver in front of bare HFS images.",
     .whyNot = "A media-format adapter: the guest sees a valid, consistent "
               "disk and its writes round-trip.",
     .files = "src/ScsiDisk.cpp src/CdImage.h src/FloppyFileImage.h",
     .platforms = kAllPlatforms, .docSection = "3"},

    // ── Input and microcontrollers ───────────────────────────────────────
    {.id = "adb-transceiver", .group = "Input and microcontrollers",
     .subsystem = "ADB transceiver (PIC1654S)",
     .level = Level::PureLle,
     .modelled = "The 342S0440-B firmware on a PIC1654S core, bit-serial on "
                 "the ADB wire through the VIA shift register.",
     .whyNot = "Real firmware on a real core. Without the dump: a byte-level "
               "state machine on VIA SR (2-byte Listen, clamped deltas).",
     .files = "src/Pic1654s.h src/Pic1654s.cpp src/AdbVia.h src/AdbVia.cpp",
     .platforms = bits({P::Compact, P::Glue, P::Rbv, P::DjMemc, P::Spike}),
     .docSection = "2", .live = LiveSource::Firmware,
     .module = lle::HleAdbModem, .fallback = Level::HleReplacement,
     .hasFallback = true, .exceptProfiles = kNoAdb,
     .manifest = {"ADB PIC1654S 342s0440-b"}},
    {.id = "adb-slot-shift-hack", .group = "Input and microcontrollers",
     .subsystem = "Mac II Slot Manager phantom SHIFT",
     .level = Level::HleHack,
     .modelled = "On a VIA1 ORB write with shift-in armed and the ADB "
                 "driver's flag set, raise a shift-complete for the HLE "
                 "ADB POST wait.",
     .whyNot = "Exists only for the byte-model fallback and is gated off "
               "under firmware LLE; it dies with the fallback.",
     .files = "src/MacIIMemory.cpp",
     .platforms = bits({P::Glue}), .docSection = "2",
     .live = LiveSource::AdbFallback, .module = lle::HleAdbModem},
    {.id = "adb-devices", .group = "Input and microcontrollers",
     .subsystem = "ADB keyboard and mouse",
     .level = Level::PureLle,
     .modelled = "Register 0-3 devices: handler IDs, extended keyboard and "
                 "mouse protocols, LEDs, live modifier bitmap.",
     .whyNot = "Remaining fidelity is device timing under load, not the "
               "protocol.",
     .files = "src/AdbLine.h src/AdbLine.cpp",
     .platforms = kAllPlatforms, .docSection = "1.6",
     .exceptProfiles = kNoAdb},
    {.id = "egret-cuda", .group = "Input and microcontrollers",
     .subsystem = "Egret / Cuda (ADB, PRAM, clock, power)",
     .level = Level::PureLle,
     .modelled = "The factory 341S0xxx firmware on an M68HC05 core: ADB "
                 "autopoll, PRAM, one-second tick, reset line, DFAC/I2C.",
     .whyNot = "Real firmware on a real core. Without the dump: a "
               "command-level model with real framing and per-byte schedule.",
     .files = "src/M68hc05.h src/CudaLle.h src/CudaLle.cpp src/Egret.h "
              "src/Egret.cpp src/AdbBus.h",
     .platforms = kEgretCudaBoards, .docSection = "1.9",
     .live = LiveSource::Firmware, .module = lle::HleEgretCuda,
     .fallback = Level::HleReplacement, .hasFallback = true,
     .manifest = {"Cuda 341s0788", "Cuda 341s0060", "Egret 341s0851"}},
    {.id = "pmu-m50753", .group = "Input and microcontrollers",
     .subsystem = "M50753 Power Manager (Portable, PowerBook 1x0)",
     .level = Level::PureLle,
     .modelled = "The PMU firmware on an M50753 core: keyboard, trackball, "
                 "power, battery.",
     .whyNot = "Real firmware on a real core; required, there is no "
               "substitute.",
     .files = "src/M50753.h src/M50753.cpp src/Pb030Pmu.h src/PortablePmu.h",
     .platforms = kPowerBook030 | bits({P::Portable}), .docSection = "-",
     .live = LiveSource::Firmware, .module = lle::PmuFirmware},
    {.id = "pmu-pge", .group = "Input and microcontrollers",
     .subsystem = "PG&E Power Manager (Duo, PowerBook 150)",
     .level = Level::PureLle,
     .modelled = "The M68HC05PGE boot mask ROM plus the main firmware the "
                 "machine ROM uploads over SPI: keyboard matrix, trackball, "
                 "clock, PRAM, sleep.",
     .whyNot = "Real firmware on a real core. Needs roms/pge/pge_boot.bin; "
               "without it the ROM stalls at the PMU handshake.",
     .files = "src/M68hc05Pge.h src/PgePmu.h src/PgePmu.cpp docs/DUO_BRINGUP.md",
     .platforms = bits({P::Msc}), .docSection = "-",
     .live = LiveSource::Firmware, .module = lle::PmuFirmware},
    {.id = "rtc", .group = "Input and microcontrollers",
     .subsystem = "RTC / PRAM chip",
     .level = Level::PureLle,
     .modelled = "The serial clock/PRAM protocol, with factory PRAM seeded "
                 "at reset only.",
     .whyNot = "This is the floor.",
     .files = "src/Rtc.h src/Rtc.cpp",
     .platforms = bits({P::Compact, P::Glue, P::Oss, P::Rbv, P::DjMemc,
                        P::Spike, P::Jaws, P::Niagra, P::Pangola}),
     .docSection = "3"},

    // ── Video ────────────────────────────────────────────────────────────
    {.id = "video-compact", .group = "Video", .subsystem = "Compact 512x342 video",
     .level = Level::LleSimplified,
     .modelled = "Row-by-row decode against the beam.",
     .whyNot = "No VRAM arbitration model; no oracle for one.",
     .files = "src/MacVideo.h src/VideoBeam.h",
     .platforms = bits({P::Compact}), .docSection = "1.1"},
    {.id = "video-toby", .group = "Video",
     .subsystem = "Toby video card (Mac II, IIfx)",
     .level = Level::LleSimplified,
     .modelled = "CLUT, monitor sense, beam-raster decode.",
     .whyNot = "No VRAM arbitration model; no oracle for one.",
     .files = "src/TobyVideo.h src/TobyVideo.cpp src/VideoBeam.h",
     .platforms = bits({P::Glue, P::Oss}), .docSection = "1.1"},
    {.id = "toby-declrom", .group = "Video",
     .subsystem = "Toby declaration ROM (342-0008-a)",
     .level = Level::PureLle,
     .modelled = "The card's own declaration ROM and its video driver.",
     .whyNot = "The real ROM. Without the dump: a synthetic ROM describing "
               "the frame buffer (System 7.5.5 stops at Welcome).",
     .files = "src/TobyDeclChoice.h src/DeclRom.h src/DeclRom.cpp",
     .platforms = bits({P::Glue, P::Oss}), .docSection = "3",
     .live = LiveSource::Firmware, .module = lle::HleTobyDeclRom,
     .fallback = Level::HleReplacement, .hasFallback = true},
    {.id = "video-se30", .group = "Video", .subsystem = "SE/30 internal video",
     .level = Level::LleSimplified,
     .modelled = "Beam-raster decode of the built-in 1-bit display.",
     .whyNot = "No VRAM arbitration model; no oracle for one.",
     .files = "src/Se30Video.h",
     .platforms = bits({P::Glue}), .docSection = "1.1"},
    {.id = "video-v8", .group = "Video", .subsystem = "V8 video and Ariel RAMDAC",
     .level = Level::LleSimplified,
     .modelled = "Registers, Ariel CLUT, monitor sense, beam-raster decode.",
     .whyNot = "Frame geometry pinned to the 12-inch modeline; no VRAM "
               "arbitration.",
     .files = "src/V8Video.h src/Ariel.h",
     .platforms = bits({P::V8}), .docSection = "1.1"},
    {.id = "video-rbv", .group = "Video", .subsystem = "RBV video (IIsi, IIci)",
     .level = Level::LleSimplified,
     .modelled = "Frame buffer in physical low RAM through the Bt478 CLUT.",
     .whyNot = "No VRAM arbitration model; no oracle for one.",
     .files = "src/RbvVideo.h",
     .platforms = bits({P::Rbv}), .docSection = "1.1"},
    {.id = "video-sonora", .group = "Video", .subsystem = "Sonora video",
     .level = Level::LleSimplified,
     .modelled = "Registers, CLUT, monitor sense, beam-raster decode.",
     .whyNot = "No VRAM arbitration model; no oracle for one.",
     .files = "src/SonoraVideo.h",
     .platforms = bits({P::Sonora}), .docSection = "1.1"},
    {.id = "video-vasp", .group = "Video", .subsystem = "VASP video",
     .level = Level::LleSimplified,
     .modelled = "2048-byte row pitch, CLUT, beam-raster decode.",
     .whyNot = "No VRAM arbitration model; no oracle for one.",
     .files = "src/VaspVideo.h",
     .platforms = bits({P::Vasp}), .docSection = "1.1"},
    {.id = "video-dafb", .group = "Video", .subsystem = "DAFB",
     .level = Level::LleSimplified,
     .modelled = "CRTC, the three clock generators, CLUT, beam-raster decode.",
     .whyNot = "VBL line hard-coded at 480 (as MAME); no TurboSCSI cell of "
               "its own; no VRAM arbitration.",
     .files = "src/Dafb.h src/Dafb.cpp",
     .platforms = bits({P::MemcJr, P::DjMemc, P::Spike}), .docSection = "1.1"},
    {.id = "video-valkyrie", .group = "Video", .subsystem = "Valkyrie",
     .level = Level::LleSimplified,
     .modelled = "Timing-number modes, depth register, RAMDAC, I2C pixel "
                 "clock programmed by the Cuda, live scan line.",
     .whyNot = "No VRAM arbitration model; no oracle for one.",
     .files = "src/Valkyrie.h src/Valkyrie.cpp",
     .platforms = bits({P::F108}), .docSection = "1.1"},
    {.id = "video-csc", .group = "Video",
     .subsystem = "CSC / GSC LCD controllers (Duo)",
     .level = Level::LleSimplified,
     .modelled = "Panel registers and pixel formats.",
     .whyNot = "Not yet audited in the video section; whole-panel decode.",
     .files = "src/CscVideo.h src/MscMemory.h",
     .platforms = bits({P::Msc}), .docSection = "1.1"},
    {.id = "video-pb030", .group = "Video",
     .subsystem = "Niagra / Ponti / GSC LCD (PowerBook 1x0)",
     .level = Level::LleSimplified,
     .modelled = "Register layout and grayscale conversion.",
     .whyNot = "Not yet audited in the video section; whole-panel decode.",
     .files = "src/Pb030Niagra.h src/Pb030Memory.h",
     .platforms = bits({P::Niagra, P::Pangola}), .docSection = "1.1"},
    {.id = "video-lcd", .group = "Video",
     .subsystem = "1-bit LCD frame buffers (Portable, PowerBook 140/170)",
     .level = Level::LleSimplified,
     .modelled = "The panel's dedicated VRAM decoded to the host surface.",
     .whyNot = "Not yet audited in the video section; whole-panel decode.",
     .files = "src/Pb030Memory.h src/PortableMemory.h",
     .platforms = bits({P::Jaws, P::Portable}), .docSection = "1.1"},
    {.id = "video-pangola", .group = "Video",
     .subsystem = "Pangola / WD90C26 color LCD and its declaration ROM",
     .level = Level::PureLle,
     .modelled = "The color panel's own declaration ROM drives the "
                 "WD90C26 path.",
     .whyNot = "The real ROM; required, there is no substitute.",
     .files = "src/PangolaVideo.h src/PangolaVideo.cpp "
              "src/Pb030ColorFirmware.cpp",
     .platforms = bits({P::Pangola}), .docSection = "-",
     .live = LiveSource::Firmware, .module = lle::PbVideoRom},

    // ── Sound ────────────────────────────────────────────────────────────
    {.id = "asc", .group = "Sound", .subsystem = "ASC / EASC family",
     .level = Level::LleSimplified,
     .modelled = "FIFO and wavetable modes, IRQ variants, $807 clock rate; "
                 "classic, V8, Sonora, IOSB and EASC identities.",
     .whyNot = "Fixed drain rate through accumulators; the classic idle IRQ "
               "is an unsourced addition kept until a consumer is named.",
     .files = "src/Asc.h src/Asc.cpp",
     .platforms = kAllPlatforms & ~bits({P::Compact}), .docSection = "1.7"},
    {.id = "dfac", .group = "Sound", .subsystem = "DFAC audio stage",
     .level = Level::LleSimplified,
     .modelled = "The Egret-driven three-wire stage: input gate and volume "
                 "table.",
     .whyNot = "The analog filters are not modelled (upstream leaves them "
               "open too).",
     .files = "src/Dfac.h src/Dfac.cpp",
     .platforms = bits({P::V8}), .docSection = "1.7"},

    // ── Host side ────────────────────────────────────────────────────────
    {.id = "host-agent", .group = "Host side",
     .subsystem = "Guest disk agent (Disques) and its SCSI mailbox",
     .level = Level::HostConvenience,
     .modelled = "An optional guest program that mounts and ejects volumes "
                 "through a POM68K-defined SCSI mailbox.",
     .whyNot = "Opt-in host integration with no Apple hardware behind it; "
               "the machine never needs it.",
     .files = "share/POM68KDisques.bin src/ScsiAgentMailbox.h "
              "src/ScsiAgentTarget.h",
     .platforms = kAllPlatforms, .docSection = "3",
     .exceptProfiles = kNoScsi},
    {.id = "host-text", .group = "Host side",
     .subsystem = "Clipboard read-back and typed text",
     .level = Level::HostConvenience,
     .modelled = "Reads the guest's desk scrap from low memory; types host "
                 "text as real key presses.",
     .whyNot = "Runs no guest code and injects nothing the keyboard could "
               "not send.",
     .files = "src/GuestScrap.h src/TextTyping.h",
     .platforms = kAllPlatforms, .docSection = "3"},
    {.id = "host-audio", .group = "Host side",
     .subsystem = "Host audio, drive sounds",
     .level = Level::HostConvenience,
     .modelled = "Resamples the guest stream to the host DAC; mechanical "
                 "drive sounds.",
     .whyNot = "After the guest's last audio stage.",
     .files = "src/MacAudioHost.h src/HostAudioResampler.h src/FloppySound.h",
     .platforms = kAllPlatforms, .docSection = "3"},
    {.id = "host-state", .group = "Host side",
     .subsystem = "Save states and PRAM files",
     .level = Level::HostConvenience,
     .modelled = "Snapshots of the whole machine; PRAM persisted per "
                 "image and profile.",
     .whyNot = "Pure caches are flushed, never saved; disks stay host-owned.",
     .files = "src/SaveState.h src/DiskTimeline.h",
     .platforms = kAllPlatforms, .docSection = "1.11"},
    };
    return kCatalogue;
}

const Entry* find(std::string_view id) {
    for (const Entry& e : catalogue())
        if (id == e.id) return &e;
    return nullptr;
}

const MachineProfile* profileFor(SnapMachine machine) {
    for (const MachineProfile& p : kMachineProfiles)
        if (p.snapshot == machine) return &p;
    return nullptr;
}

bool presentOn(const Entry& entry, SnapMachine machine) {
    const MachineProfile* profile = profileFor(machine);
    if (!profile) return false;
    if (!(entry.platforms & platformBit(profile->platform))) return false;
    for (SnapMachine m : entry.exceptProfiles)
        if (m == machine) return false;
    return true;
}

namespace {

const lle::Device* reported(const SessionView& view, lle::Module module) {
    for (const lle::Device& d : view.devices)
        if (d.module == module) return &d;
    return nullptr;
}

std::string fileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

LiveRow resolve(const Entry& entry, const SessionView& view) {
    LiveRow row;
    row.active = entry.level;
    row.present = !view.machine || presentOn(entry, *view.machine);

    switch (entry.live) {
    case LiveSource::None:
        break;
    case LiveSource::Firmware: {
        // The device's report decides presence too: a platform can carry a
        // board without the part (the Quadra 700 has no Egret), and the
        // registry lists what this machine actually built.
        if (!view.machine) break;
        const lle::Device* d = reported(view, entry.module);
        row.present = d != nullptr;
        if (!d) break;
        row.known = true;
        if (d->mode == lle::Mode::Lle) {
            row.active = entry.level;
            row.state = "firmware";
            row.detail = d->firmware.empty()
                ? std::string("original firmware running")
                : "running " + fileName(d->firmware);
        } else if (d->why == lle::Why::MissingRequired) {
            row.active = entry.level;
            row.degraded = true;
            row.state = "firmware missing";
            row.detail = "required dump absent: the device does not run";
            if (!d->candidates.empty())
                row.hint = "Place " + d->candidates.front() + " and restart.";
        } else {
            row.active = entry.hasFallback ? entry.fallback : entry.level;
            row.degraded = true;
            row.state = "HLE fallback";
            row.detail = d->why == lle::Why::HleForced
                ? "substitute forced by configuration"
                : "no firmware dump found";
            if (!d->candidates.empty())
                row.hint = "Place " + d->candidates.front() +
                           " and restart to run the original firmware.";
        }
        break;
    }
    case LiveSource::CpuEngine:
        if (view.engine < 0) break;
        row.known = true;
        row.state = view.engine == 0 ? "interpreter"
                                     : "JIT " + (view.backend.empty()
                                                     ? std::string("?")
                                                     : view.backend);
        row.detail = view.engine == 0 ? "Moira interpreter, the accuracy oracle"
                                      : "accelerated engine, lockstep-gated "
                                        "against the interpreter";
        if (view.strictRequested && view.strictQualified)
            row.detail += "; locked by strict LLE mode";
        break;
    case LiveSource::AdbFallback: {
        if (!view.machine || !row.present) break;
        const lle::Device* d = reported(view, entry.module);
        row.known = true;
        row.engaged = d && d->mode == lle::Mode::Hle;
        row.degraded = row.engaged;
        row.state = row.engaged ? "ENGAGED" : "dormant";
        row.detail = row.engaged
            ? "the ADB transceiver runs its HLE fallback"
            : "gated off: the ADB transceiver runs its firmware";
        if (row.engaged && d && !d->candidates.empty())
            row.hint = "Place " + d->candidates.front() +
                       " and restart: the firmware path retires this hack.";
        break;
    }
    }
    if (!row.present) {
        row.state = "not on this machine";
        row.known = false;
    }
    return row;
}

} // namespace pom68k::abstraction
