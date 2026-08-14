// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "V8Memory.h"
#include "LleSession.h"
#include "Cpu030.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

namespace {

class DeviceHashArchive {
public:
    static constexpr bool loading = false;

    template <class... Ts> void operator()(Ts&... xs) { (one(xs), ...); }

    // RAM and VRAM are checked by the lockstep separately. Hash their sizes
    // so a topology mismatch is still visible without walking megabytes at
    // every peripheral deadline.
    void blob(const std::vector<uint8_t>& v) {
        mixByte(0xB1);
        mixInt(uint64_t(v.size()));
    }
    void bytes(const void* p, std::size_t n) {
        mixByte(0xB2);
        mixInt(uint64_t(n));
        const auto* b = static_cast<const uint8_t*>(p);
        for (std::size_t i = 0; i < n; ++i) mixByte(b[i]);
    }
    void varint(uint64_t v) { mixByte(0xB3); mixInt(v); }
    bool ok() const { return true; }
    void fail() {}
    uint64_t value() const { return hash_; }

private:
    template <class T>
    static constexpr bool visitable = requires(T& x, DeviceHashArchive& ar) {
        x.visit(ar);
    };

    template <class T> void one(T& x) {
        if constexpr (visitable<T>) {
            x.visit(*this);
        } else if constexpr (std::is_enum_v<T>) {
            mixInt(uint64_t(static_cast<std::underlying_type_t<T>>(x)));
        } else if constexpr (std::is_same_v<T, bool>) {
            mixByte(x ? 1 : 0);
        } else if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) mixInt(std::bit_cast<uint32_t>(x));
            else mixInt(std::bit_cast<uint64_t>(x));
        } else if constexpr (std::is_integral_v<T>) {
            using U = std::make_unsigned_t<T>;
            mixInt(uint64_t(U(x)), sizeof(T));
        } else if constexpr (std::is_array_v<T>) {
            for (auto& e : x) one(e);
        } else if constexpr (requires { x.size(); x.begin(); x.end(); }) {
            mixInt(uint64_t(x.size()));
            for (auto& e : x) one(e);
        } else {
            static_assert(sizeof(T) == 0, "unsupported device-hash field");
        }
    }

    void mixByte(uint8_t v) {
        hash_ ^= v;
        hash_ *= 1099511628211ull;
    }
    void mixInt(uint64_t v, std::size_t bytes = sizeof(uint64_t)) {
        for (std::size_t i = 0; i < bytes; ++i) mixByte(uint8_t(v >> (i * 8)));
    }

    uint64_t hash_ = 1469598103934665603ull;
};

}  // namespace

V8Memory::V8Memory(uint32_t totalRam, Model model, int64_t cpuHz)
    : ram_(totalRam, 0), rom_(kRomSize, 0), vram_(kVramSize, 0),
      egret_(via_, model == Model::ColorClassic || model == Model::MacTv,
             int(cpuHz)),
      egretLle_(via_, cpuHz, model == Model::ColorClassic
                                     || model == Model::MacTv
                                 ? CudaLle::Flavor::Cuda
                                 : CudaLle::Flavor::Egret),
      totalRam_(totalRam), model_(model), cpuHz_(cpuHz),
      viaDiv_(int(cpuHz / kViaHz)),
      mbRam_(model == Model::Lc ? 0x200000 : kMbRamSize) {
    // Pseudo-VIA machine hooks (v8.cpp:328-352): reg 1 = RAM config
    // (reads back config | 0x04), reg $10 read = monitor sense on bits
    // 3-5, port B bit 3 = HMMU enable (68020 LC only, see addrMask_).
    pvia_.onConfigRead = [this] { return uint8_t(config_ | 0x04); };
    pvia_.onConfigWrite = [this](uint8_t v) { config_ = v; applyRamConfig(v); };
    if (model_ == Model::Lc)                 // PB3 LOW = HMMU 24-bit mode
        pvia_.onPortB = [this](uint8_t v) {
            addrMask_ = (v & 0x08) ? 0x80FFFFFF : 0x00FFFFFF;
        };
    // Eagle has no monitor sense — the Classic II display is built in
    // (v8.cpp:662-665 via2_video_config_r returns 0). The Spice's
    // Trinitron is built in too but reports the fixed 512×384 sense 2
    // (v8.cpp:760-763 returns 0x02 << 3).
    if (model_ == Model::ClassicII)
        pvia_.onVideoRead = [] { return uint8_t(0); };
    else if (model_ == Model::ColorClassic)
        pvia_.onVideoRead = [] { return uint8_t(0x02 << 3); };
    else if (model_ == Model::MacTv)         // Tinker Bell: fixed 13" 640×480
        pvia_.onVideoRead = [] { return uint8_t(0x06 << 3); };
    else
        pvia_.onVideoRead = [this] { return uint8_t((montype_ << 3) & 0x38); };
    if (model_ == Model::MacTv) montype_ = 6;   // built-in 640×480 CRT
    if (model_ == Model::MacTv) simmLoc_ = 0x400000;  // SIMM bus base (v8.cpp:1093)
    pvia_.onVideoWrite = [this](uint8_t v) { videoConfig_ = v; };
    egret_.setAdbBus(&adb_);
    // ASC IRQ is LEVEL-triggered into pseudo-VIA IFR bit 4 (v8.cpp:119-122).
    // The Spice carries the Sonora-class EASC ($BC) instead of the V8 ASC
    // (v8.cpp:717-720 ASC_SONORA replace) — only one of the two is wired.
    asc_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    ascSonora_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    // Egret firmware LLE (real 341S0850, MAME egret.cpp:74) — the DEFAULT
    // since 2026-07-24 (TODO step 6 closed): the instruction-slaved ADB
    // wire (CudaLle::mcu_.onCycles) fixed the batch-frozen receive that
    // starved the mouse to ~1.5% delivery; lcii_mouse_trace now saturates
    // the screen exactly like the HLE. POM68K_EGRET_LLE=0 forces the HLE,
    // a missing dump falls back silently (the Cuda rollout pattern).
    {
        // Color Classic: Cuda MCU, factory part 341S0417 (Cuda 2.35,
        // maclc.cpp:480) — the DEFAULT since 2026-07-29. The old "0417
        // wedges the M68hc05" note was a missing DEVICE, not a core bug:
        // the CC carries a DFAC2 on the Cuda's I2C (maclc.cpp:505) and
        // the 2.35 requires its ACK (CudaLle::setI2cDfac); un-ACKed it
        // took a DFAC error path that muted the next host VIA session.
        // 341S0788 (2.37) stays as the no-0417 fallback.
        const bool cudaMcu = hasCudaMcu();
        if (model_ == Model::ColorClassic) egretLle_.setI2cDfac(true);
        // (Mac TV: no DFAC at all — maclc.cpp mactv device_remove("dfac"),
        // nothing re-added — so its Cuda I2C bus stays empty.)
        const char* e = std::getenv(cudaMcu ? "POM68K_CUDA_LLE"
                                            : "POM68K_EGRET_LLE");
        const bool want = !e || std::atoi(e) != 0;
        static constexpr const char* kEgretFw[] = {
            "roms/egret/341s0850.bin", "../roms/egret/341s0850.bin", nullptr };
        static constexpr const char* kCudaFw[] = {
            "roms/cuda/341s0417.bin", "../roms/cuda/341s0417.bin",
            "roms/cuda/341s0788.bin", "../roms/cuda/341s0788.bin", nullptr };
        // Mac TV: the factory Cuda is 341s0789 (Cuda 2.38, cuda.cpp:48);
        // fall back to the AIO-proven 2.40 then the CC's 2.37.
        static constexpr const char* kTvFw[] = {
            "roms/cuda/341s0789.bin", "../roms/cuda/341s0789.bin",
            "roms/cuda/341s0060.bin", "../roms/cuda/341s0060.bin",
            "roms/cuda/341s0788.bin", "../roms/cuda/341s0788.bin", nullptr };
        // Diag/bring-up override: POM68K_CUDA_FW=<path> forces a specific
        // MCU dump ahead of the per-model list (how the factory 341S0417
        // is exercised against the M68hc05 without rebuilding).
        const char* fwOverride = std::getenv("POM68K_CUDA_FW");
        const char* const* fwList = model_ == Model::MacTv ? kTvFw
                                     : cudaMcu ? kCudaFw : kEgretFw;
        std::string loadedFw;
        if (want) {
            if (fwOverride && *fwOverride) {
                std::ifstream in(fwOverride, std::ios::binary);
                std::vector<uint8_t> fw((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
                if (in && egretLle_.loadFirmware(fw)) {
                    egretLleOn_ = true;
                    loadedFw = fwOverride;
                } else std::fprintf(stderr, "V8: POM68K_CUDA_FW=%s unusable\n",
                                    fwOverride);
            }
            for (const char* const* p = fwList; !egretLleOn_ && *p; p++) {
                std::ifstream in(*p, std::ios::binary);
                if (!in) continue;
                std::vector<uint8_t> fw((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
                if (egretLle_.loadFirmware(fw)) {
                    egretLleOn_ = true;
                    loadedFw = *p;
                    break;
                }
            }
            // The fallback stays (MCU dumps are user-provided and not
            // distributable) but it is never silent: the HLE byte-model is
            // a documented NON-CONFORMANT substitute (LLE_VS_HLE §2).
            if (!egretLleOn_)
                std::fprintf(stderr, "V8: no MCU firmware dump under roms/%s/ "
                             "— running the NON-CONFORMANT HLE ADB substitute "
                             "(docs/LLE_VS_HLE.md §2)\n",
                             cudaMcu ? "cuda" : "egret");
        } else {
            std::fprintf(stderr, "V8: %s=0 — NON-CONFORMANT HLE ADB "
                         "substitute forced\n",
                         cudaMcu ? "POM68K_CUDA_LLE" : "POM68K_EGRET_LLE");
        }
        // One report carries the whole outcome to the Périphériques window;
        // the HLE branch is what used to be the bare activateHle() call.
        std::vector<std::string> cands;
        for (const char* const* p = fwList; *p; p++) cands.emplace_back(*p);
        pom68k::lle::reportFirmwareDevice(
            pom68k::lle::HleEgretCuda,
            cudaMcu ? "Cuda — MCU ADB / PRAM / horloge"
                    : "Egret — MCU ADB / PRAM / horloge",
            cudaMcu ? "POM68K_CUDA_LLE" : "POM68K_EGRET_LLE",
            egretLleOn_, want, loadedFw, std::move(cands));
    }
    // Firmware RESET_SYSTEM ($11) — the Finder's "Restart". DEFERRED on
    // purpose: this fires from inside viaWrite(), i.e. from a memory
    // callback under the CPU, and V8Memory::reset() would reset the very
    // MCU that is mid-instruction issuing it. Latch here, act in
    // Cpu030::runCycles. Only the address map comes back — the devices,
    // the PRAM and the MCU keep running, which is what the /RESET line
    // does on the board (the gate array and the CPU are on it; the Egret
    // is the one pulling it). The overlay's own clear re-runs
    // applyRamConfig, so dropping the RAM windows here reproduces the
    // power-on order exactly.
    egretLle_.onCpuReset = [this] {
        overlay_ = true;
        simmMapped_ = mbMapped_ = false;
        jitMapChanged();
        restartPending_ = true;
    };
    reset();
}

uint64_t V8Memory::debugDeviceHash() {
    DeviceHashArchive ar;
    visit(ar);
    return ar.value();
}

// 512 KB flat image; the stored big-endian word checksum (bytes 4…end)
// is verified against the header — warn-only, so patched/homebrew ROMs
// still load (the real LC II ROM is $35C28F5F, docs/LCII_HARDWARE.md).
bool V8Memory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize && data.size() != 0x100000) return false;
    rom_ = data;
    romSize_ = uint32_t(data.size());        // Spice ROMs (Color Classic
    romMask_ = romSize_ - 1;                 // $ECD99DC0) are 1 MB

    // Classic II ROM ($3193670E): genuine ROM bug — a JMP through a
    // boxflag table indexes PAST the table and lands mid-instruction in
    // 32-bit mode. MAME patches the bad JMP to an RTS (the IIvx/IIvi ROM
    // shows the intended entry is a no-op) and fixes the checksum to
    // match (maclc.cpp:614-630 ROM_FILL). Same patch here.
    if (rom_[0] == 0x31 && rom_[1] == 0x93 && rom_[2] == 0x67 && rom_[3] == 0x0E) {
        rom_[0x43B6E] = 0x4E; rom_[0x43B6F] = 0x75;  // JMP (table) → RTS
        rom_[0x00002] = 0x66; rom_[0x00003] = 0x88;  // checksum compensation
    }

    uint32_t stored = uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
                    | uint32_t(rom_[2]) << 8 | rom_[3];
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom_.size(); i += 2)
        sum += uint32_t(rom_[i] << 8 | rom_[i + 1]);
    if (sum != stored)
        std::fprintf(stderr, "V8Memory: ROM checksum $%08X != header $%08X\n",
                     sum, stored);
    return true;
}

void V8Memory::reset() {
    overlay_ = true;
    restartPending_ = false;         // a cold reset supersedes a warm one
    config_ = 0;
    videoConfig_ = 0;
    sccIrq_ = false;
    scc_.reset();
    scc_.setClocks(cpuHz_, 7833600);         // SCC async-baud LLE: PCLK =
                                             // C7M (LCII_HARDWARE.md:44)
    scc_.setAbortIdle(true);                 // no hardwired LocalTalk peer
                                             // (O6.10). The abort is a LINE
                                             // state: a virgin line reads
                                             // clean (OT binds .MPP), the
                                             // abort exists once the line has
                                             // carried a frame, a live peer
                                             // suppresses it —
                                             // Scc8530::openLine (steps 7+8)
    viaPhase_ = 0;
    tickAcc_ = 0;
    addrMask_ = 0x80FFFFFF;                  // HMMU disabled at reset (MAME
                                             // m68kcpu.cpp:1147; PB3 rewrites)
    simmMapped_ = mbMapped_ = false;         // no RAM until the overlay drops
    via_.reset();
    pvia_.reset();
    ariel_.reset();
    egret_.reset();
    egret_.factoryDefaults();                // SPConfig XPRAM $13 = $22
    egretLle_.reset();
    if (egretLleOn_)                         // stage the same battery
        for (int i = 0; i < 256; i++)
            egretLle_.setPram(i, egret_.pram(i));
    adb_.reset();
    asc_.reset();
    ascSonora_.reset();
    scsi_.reset();
    if (spiceClass()) {                      // Spice/Tinker Bell: SWIM2
        swim2_.reset();
        swim2_.attachDrive(&drive_, nullptr);
    } else {
        swim_.reset();
        swim_.attachDrive(&drive_, nullptr);
    }
    drive_.setSpinClockHz(15667200);         // devices tick in the C15M domain
    framePos_ = 0;
    c15Acc_ = 0;
    // Frame/VBL geometry in CPU cycles: V8-class scans 640×407 dots at
    // C15M (VBL = lines 384-406); Tinker Bell scans 800×525 @ 25.175 MHz
    // with 480 active lines (v8.cpp:936 set_raw).
    if (model_ == Model::MacTv) {
        frameCycles_ = int64_t(800) * 525 * cpuHz_ / 25175000;
        vblStart_ = int64_t(800) * 480 * cpuHz_ / 25175000;
        frameTotalLines_ = 525;
    } else {
        frameCycles_ = int64_t(640) * 407 * cpuHz_ / kCpuHz;
        vblStart_ = int64_t(640) * 384 * cpuHz_ / kCpuHz;
        frameTotalLines_ = 407;
    }
    vblState_ = false;
    // VIA1 port A input = V8-family machine ID | diag bit: $D4 for the
    // V8 proper (LC/LC II, v8.cpp:249-252), $92 for the Classic II's
    // Eagle (v8.cpp:657-660). The Color Classic's Spice reads a plain
    // $82 — no diag-bit OR, no config port at all (v8.cpp:703-704,
    // 755-758); PB3 = Egret XCVR_SESSION, idle high. PB0-PB2
    // (legacy RTC lines) and PB6-PB7 keep the 6522 pull-up default 1
    // (review 2026-07-16: they read 0 before, incl. to the ROM's
    // old-clock probe). PB4/PB5 (VIA_FULL/SYS_SESSION) are HOST-driven
    // handshake lines and must idle LOW here: portB() is fed into
    // Egret::portBChanged, whose HLE is edge-triggered — pulled-up 1s
    // while DDRB is still 0 at reset read as a phantom session rise and
    // wedge the transport (validated: pull-ups on PB4/PB5 black-screen
    // the boot etalon).
    // Tinker Bell reads a plain $84 (v8.cpp:946-949, no diag bit OR).
    via_.setInA(model_ == Model::ClassicII     ? 0x93
                : model_ == Model::ColorClassic ? 0x82
                : model_ == Model::MacTv        ? 0x84
                                                : 0xD5);
    via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
}

// MAME v8.cpp:354-422 ram_size(). SIMM bank (bank A) always maps at 0,
// motherboard (bank B) after the CONFIGURED SIMM size; the first 2 MB of
// motherboard RAM are always aliased at $800000 (handled in ramIndex) —
// except on the Mac TV's Tinker Bell, which has its own layout below.
// ram_ layout mirrors MAME's contiguous RAM device: motherboard at
// offset 0, SIMM at +4 MB — except the 10 MB config, where the SIMM is
// 8 MB at +2 MB and the soldered bank's upper 2 MB are the wasted ones
// (v8.cpp:363-369).
void V8Memory::applyRamConfig(uint8_t config) {
    if (overlay_) return;

    // Tinker Bell override (v8.cpp:1066-1101 tinkerbell_device::ram_size):
    // motherboard 4 MB ALWAYS at 0, SIMM ALWAYS at $400000 when any SIMM
    // bit is set, no 2 MB truncate bit, and NO $800000 motherboard image
    // (ramIndex). The $C0 the ROM writes — "8 MB SIMM, alias only" on V8 —
    // means 4 MB motherboard + 4 MB SIMM here (v8.cpp:1068-1070).
    if (model_ == Model::MacTv) {
        simmPhys_ = totalRam_ > mbRam_ ? totalRam_ - mbRam_ : 0;
        simmOff_ = mbRam_;                   // ram_: motherboard first
        simmMapped_ = simmPhys_ > 0 && (config & 0xC0) != 0;
        mbLoc_ = 0;                          // v8.cpp:1084-1085
        mbSize_ = mbRam_;
        mbMapped_ = true;
        jitMapChanged();
        return;
    }

    simmPhys_ = totalRam_ > mbRam_ ? totalRam_ - mbRam_ : 0;
    simmOff_ = mbRam_;
    if (totalRam_ == 0xA00000) { simmPhys_ = 0x800000; simmOff_ = 0x200000; }

    simmMapped_ = simmPhys_ > 0 && (config & 0xC0) != 0;

    static constexpr uint32_t kSimmCfg[4] = { 0, 0x200000, 0x400000, 0x800000 };
    mbLoc_ = simmMapped_ ? kSimmCfg[(config >> 6) & 3] : 0;

    mbMapped_ = (config & 0xC0) != 0xC0;     // 8 MB SIMM ⇒ only the $800000 alias
    mbSize_ = (config & 0x20) ? 0x200000 : mbRam_;

    // The pseudo-VIA can rewrite this mapping at ANY time (onConfigWrite),
    // and a bank remap is exactly the "address map moved" case the JIT
    // window cannot detect from a write. Rare enough to be unconditional.
    jitMapChanged();
}


void V8Memory::busError() const {
    // Machine-less access (unit tests poking the map): open bus would be
    // silent — throw via the CPU when wired, else report loudly.
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

int V8Memory::iplLevel() const {
    if (sccIrq_) return 4;                   // v8.cpp:287-315
    if (pvia_.irqAsserted()) return 2;
    if (via_.irqAsserted()) return 1;
    return 0;
}

void V8Memory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

// v8.cpp:462-483 — the CPU is stalled to the 783.36 kHz VIA E-clock on
// every VIA1 access: start at via_cycle+1, end half a VIA cycle later.
// With cpuClk/viaClk = viaDiv_ (20 at C15M, 40 on the Mac TV's C32M):
// main = (via_cycle*2 + 3) * viaDiv_/2 + 1.
// Machine cycles, not core-clock cycles — aligning to a boost-fast phantom
// E-clock shrinks every VIA-paced pulse by cacheBoost_ (CHANGELOG 2026-07-25).
void V8Memory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->machineClock();
    int64_t viaCycle = c / viaDiv_;
    int64_t target = (viaCycle * 2 + 3) * (viaDiv_ / 2) + 1;
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t V8Memory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();            // word path skips read8's flush
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // $200 stride (v8.cpp:434-460)
    if (write) {
        via_.write(reg, v);
        // Port B outputs carry the Egret handshake (PB4 VIA_FULL,
        // PB5 SYS_SESSION, maclc.cpp:425-433)
        if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            if (egretLleOn_) egretLle_.portBChanged(via_.portB());
            else             egret_.portBChanged(via_.portB());
        }
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB)                 // PB3 = XCVR_SESSION, live
        via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
    uint8_t d = via_.read(reg);
    updateIrq();
    return d;
}


// Bring-up (POM68K_V8_IOHOLE): name the code touching a map hole. On the
// FIRST access from each PC, dump the low-memory globals the ROM's
// DecoderInfo copies its hardware bases into, plus a window of code around
// that PC — the accessing routine can then be disassembled offline instead
// of guessed at (the Classic II $F18000 question, TODO § 4).
void V8Memory::holeDump(uint32_t addr) const {
    if ((addr & 0xFFF000) != 0xF18000) return;
    static std::vector<uint32_t> seen;
    const uint32_t pc = cpu_ ? uint32_t(cpu_->getPC()) : 0u;
    for (uint32_t p : seen) if (p == pc) return;
    seen.push_back(pc);
    auto lmg = [&](uint32_t a) {
        return uint32_t(peek8(a)) << 24 | uint32_t(peek8(a + 1)) << 16
             | uint32_t(peek8(a + 2)) << 8 | peek8(a + 3);
    };
    std::fprintf(stderr, "[iohole] NEW PC $%08X (hole $%06X); "
                 "LMG $0B0A=$%08X $0312=$%08X $01E0=$%08X $0C00=$%08X\n",
                 pc, addr, lmg(0x0B0A), lmg(0x0312), lmg(0x01E0), lmg(0x0C00));
    const uint32_t base = (pc & ~1u) - 48;
    std::fprintf(stderr, "[iohole]   code $%08X:", base);
    for (int i = 0; i < 96; i++) {
        if (i && !(i % 16)) std::fprintf(stderr, "\n[iohole]   +%02X       ", i);
        std::fprintf(stderr, " %02X", peek8(base + uint32_t(i)));
    }
    std::fprintf(stderr, "\n");
}

uint8_t V8Memory::read8(uint32_t addr) {
    addr &= addrMask_;                       // A31 + A23-A0 (maclc.cpp:181)
    if (addr & 0x80000000) {                 // PDS slot $E: no card
        if (model_ == Model::ClassicII) return 0xFF;  // no PDS, open bus
        busError();
    }

    if (addr < 0xA00000) {                   // RAM space (ROM under overlay)
        if (overlay_) {
            if (addr < romSize_) return rom_[addr];
            return 0xFF;                     // unmapped while booting
        }
        uint32_t i = ramIndex(addr);
        return i != 0xFFFFFFFF ? ram_[i] : 0xFF;
    }

    if (addr < 0xB00000) {                   // ROM, 512K mirrored ×2 or 1 MB
        if (overlay_) {                      // rom_switch_r (v8.cpp:225-235):
            overlay_ = false;                // any read clears the overlay,
            applyRamConfig(0xC0);            // default "full SIMM + full MB"
            jitMapChanged();                 // the whole map just moved
        }
        return rom_[addr & romMask_];
    }

    if (addr < 0xF00000) return 0xFF;        // $B00000-$EFFFFF: open bus

    // ── I/O space $F00000-$FFFFFF (docs/LCII_HARDWARE.md § Address map) ──
    if (addr >= 0xF40000 && addr < 0xFC0000) return vram_[addr - 0xF40000];
    if (cpu_) cpu_->flushTicks();            // registers see current time
    if (addr < 0xF02000) return viaAccess8(addr, false, 0);
    if (addr >= 0xF04000 && addr < 0xF06000) {
        // Z85C30 (O6.10: real Scc8530, dc_ab decode — maclc.cpp:114:
        // A1 = channel, A2 = data/ctl). Ext/status interrupts drive
        // IPL 4 (the LAP manager's carrier sense, Scc8530.h).
        int ch = (addr >> 1) & 1;
        uint8_t d = ((addr >> 2) & 1) ? scc_.readData(ch)
                                      : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        if (onSccAccess) onSccAccess(addr, false, d);
        return d;
    }
    // SCSI pseudo-DMA windows: DRQ-handshaked, a hung DRQ bus-errors
    // (maclc.cpp:222-266; macscsi.cpp:5-52 — the SCSI Manager's blind
    // transfers catch the BERR to end the loop)
    if ((addr >= 0xF06000 && addr < 0xF08000) ||
        (addr >= 0xF12000 && addr < 0xF14000)) return scsiDma_();
    if (addr >= 0xF10000 && addr < 0xF12000) {
        // 53C80 registers, stride $10; pseudo-DMA read = reg 6 at +$260
        // (maclc.cpp:206-212)
        int reg = (addr >> 4) & 7;
        if (reg == 6 && (addr & 0xFFF) == 0x260) return scsiDma_();
        uint8_t d = scsi_.read(reg);
        scsiDrq(scsi_.drqActive());
        return d;
    }
    if (addr >= 0xF14000 && addr < 0xF16000)
        return spiceClass() ? ascSonora_.read(addr - 0xF14000)
                            : asc_.read(addr - 0xF14000);
    if (addr >= 0xF16000 && addr < 0xF18000) {
        // SWIM1 in its IWM-compatible GCR mode (O6.7): reg = A9-A12,
        // +5 CPU cycles per access (maclc.cpp:268-287); HDSEL = VIA1 PA5.
        // Spice: the integrated SWIM2 instead (v8.cpp:699,874-882 — same
        // reg decode + wait states, HDSEL from the SWIM2 mode register).
        if (cpu_) cpu_->stall(5);
        if (spiceClass())
            return swim2_.read((addr >> 9) & 0xF);
        swim_.setSel((via_.portA() & 0x20) != 0);
        return swim_.read((addr >> 9) & 0xF);
    }
    // Ariel RAMDAC. MAME gives it the same 8 KB window (v8.cpp:93,
    // map(0x524000,0x525fff)) but ariel_device::read/write switch on the
    // raw offset and only decode 0-3 (ariel.cpp:62-94), so on master the
    // band from +4 up reads 0. We decode A0-A1 and mirror — an 8 KB chip
    // select on a 4-register RAMDAC is partial decode, and MAME's zero
    // band is a consequence of using the map offset as a register index.
    // Cosmetic (nothing shipped touches +4 and up); the argument in full,
    // and the identical VASP case, are in VaspMemory.cpp read8.
    if (addr >= 0xF24000 && addr < 0xF26000) return ariel_.read(addr & 3);
    if (addr >= 0xF26000 && addr < 0xF28000) {
        uint8_t d = pvia_.read(addr - 0xF26000);
        updateIrq();
        return d;
    }

    // Unmapped I/O: BERR on the V8 proper — the LC/LC II ROM map probes
    // rely on it (AddrMapFlags $773F, LCII_HARDWARE.md:78). The Classic
    // II's Eagle bus is FORGIVING instead (MAME macclas2: only the SCSI
    // helper timeout raises /BERR; every map hole is open bus): its ROM
    // dereferences $50F18038 and pokes through wild pointers with no
    // catcher installed — any BERR there lands on a zero vector → DS 1.
    // Bring-up eyes (POM68K_V8_IOHOLE=1): what does the guest actually DO
    // in the map holes? The Classic II's ROM is known to dereference
    // $50F18038; the block behind it has never been identified, and the
    // only way to name it is to watch the access pattern (TODO § 4).
    if (const char* e = std::getenv("POM68K_V8_IOHOLE")) {
        static const long cap = std::atol(e) > 1 ? std::atol(e) : 200;
        static long n = 0;
        if (n++ < cap)
            std::fprintf(stderr, "[iohole] rd $%06X pc=$%08X\n", addr,
                         cpu_ ? unsigned(cpu_->getPC()) : 0u);
        holeDump(addr);
    }
    if (model_ == Model::ClassicII) {
        // POM68K_V8_HOLEVAL=<hex>: what a map hole reads back on the
        // Eagle's forgiving bus. MAME answers 0 here, but that is its
        // address_space DEFAULT, not a modelled decision (the Sonora's 0
        // IS modelled — `iosb.cpp:54-65` says so in a comment). Until an
        // observable separates them, the value is a knob, not a fact.
        static const int hv = [] {
            const char* h = std::getenv("POM68K_V8_HOLEVAL");
            return h ? int(std::strtoul(h, nullptr, 16)) : 0xFF;
        }();
        return uint8_t(hv);
    }
    busError();                              // unmapped I/O: ROM map probe
}

const uint8_t* V8Memory::codeSpan(uint32_t phys, uint32_t& len) const {
    len = 0;
    phys &= addrMask_;
    if (phys & 0x80000000) return nullptr;       // PDS: open bus / BERR
    if (overlay_) return nullptr;                // any ROM-space read flips it
    if (phys < 0xA00000) {
        // RAM through the bank remap. The remap is piecewise linear, so the
        // span stops at the current piece's edge: the fixed 2 MB alias, the
        // motherboard bank, or the SIMM bank (ramIndex's three cases).
        const uint32_t i = ramIndex(phys);
        if (i == 0xFFFFFFFF) return nullptr;
        uint32_t edge;
        if (phys >= 0x800000)                    edge = 0xA00000;
        else if (mbMapped_ && phys >= mbLoc_ && phys < mbLoc_ + mbSize_)
                                                 edge = mbLoc_ + mbSize_;
        else                                     edge = simmLoc_ + simmPhys_;
        len = edge - phys;
        if (uint64_t(i) + len > ram_.size()) len = uint32_t(ram_.size() - i);
        return ram_.data() + i;
    }
    if (phys < 0xB00000) {                       // ROM, mirrored
        const uint32_t o = phys & romMask_;
        len = romMask_ + 1 - o;
        return rom_.data() + o;
    }
    return nullptr;                              // open bus, VRAM, I/O
}

uint8_t* V8Memory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
    len = 0;
    phys &= addrMask_;
    if (phys & 0x80000000) return nullptr;
    if (overlay_) return nullptr;
    if (phys < 0xA00000) {
        const uint32_t i = ramIndex(phys);
        if (i == 0xFFFFFFFF) return nullptr;
        uint32_t edge;
        if (phys >= 0x800000)                    edge = 0xA00000;
        else if (mbMapped_ && phys >= mbLoc_ && phys < mbLoc_ + mbSize_)
                                                 edge = mbLoc_ + mbSize_;
        else                                     edge = simmLoc_ + simmPhys_;
        len = edge - phys;
        if (uint64_t(i) + len > ram_.size()) len = uint32_t(ram_.size() - i);
        return ram_.data() + i;
    }
    if (!write && phys < 0xB00000) {
        const uint32_t o = phys & romMask_;
        len = romMask_ + 1 - o;
        return rom_.data() + o;
    }
    return nullptr;
}

uint32_t V8Memory::jitAliasCodeMask(uint32_t physSlice,
                                    const uint8_t* pageMap,
                                    uint32_t pages) const {
    if (!pageMap || overlay_) return 0;
    uint32_t mask = 0;
    constexpr uint32_t kShift = jit::CodeGuard::kShift;
    constexpr uint32_t kSlices = 4096u >> kShift;
    for (uint32_t bit = 0; bit < kSlices; ++bit) {
        const uint32_t bus = (physSlice + (bit << kShift)) & addrMask_;
        const uint32_t backing = ramIndex(bus);
        if (backing == 0xFFFFFFFF) continue;

        uint32_t alias = 0xFFFFFFFF;
        if (bus >= 0x800000 && bus < 0xA00000 && mbMapped_ &&
            (bus & 0x1FFFFF) < mbSize_) {
            alias = mbLoc_ + (bus & 0x1FFFFF);
        } else if (model_ != Model::MacTv && bus < 0x800000 &&
                   backing < 0x200000) {
            alias = 0x800000 | backing;
        }
        const uint32_t slice = alias >> kShift;
        if (alias != 0xFFFFFFFF && slice < pages && pageMap[slice])
            mask |= 1u << bit;
    }
    return mask;
}

void V8Memory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    if (cpu_) cpu_->pomJitDtlbFlush();
}

void V8Memory::write8(uint32_t addr, uint8_t v) {
    addr &= addrMask_;
    if (writeObserver_)
        observeWrite(cpu_, addr, 1, v, cpu_ ? cpu_->getPC0() : 0, 0);
    if (addr & 0x80000000) {                 // PDS slot $E: no card
        if (model_ == Model::ClassicII) return;       // no PDS, open bus
        busError();
    }

    if (addr < 0xA00000) {
        if (overlay_) return;                // ROM overlay: writes unmapped
        uint32_t i = ramIndex(addr);
        if (i != 0xFFFFFFFF) {
            // The guard is keyed on the BUS address — the same key space the
            // window probe reports — and the fixed 2 MB alias means one RAM
            // byte can carry TWO bus addresses. Note BOTH: a write through
            // either view must kill a block recorded through the other.
            if (jitGuard_) {
                jitGuard_->note(addr, 1);
                if (addr >= 0x800000 && mbMapped_ && (addr & 0x1FFFFF) < mbSize_)
                    jitGuard_->note(mbLoc_ + (addr & 0x1FFFFF), 1);
                else if (model_ != Model::MacTv && addr < 0x800000 && i < 0x200000)
                    jitGuard_->note(0x800000 | i, 1);  // no alias on Tinker Bell
            }
            ram_[i] = v;
        }
        return;
    }

    if (addr < 0xF00000) return;             // ROM + open bus: ignore

    if (addr >= 0xF40000 && addr < 0xFC0000) { vram_[addr - 0xF40000] = v; return; }
    if (cpu_) cpu_->flushTicks();            // registers see current time
    if (addr < 0xF02000) { viaAccess8(addr, true, v); return; }
    if (addr >= 0xF04000 && addr < 0xF06000) {              // SCC (O6.10)
        int ch = (addr >> 1) & 1;
        if (onSccAccess) onSccAccess(addr, true, v);
        if ((addr >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccIrqLine(scc_.irqAsserted());
        return;
    }
    if ((addr >= 0xF06000 && addr < 0xF08000) ||
        (addr >= 0xF12000 && addr < 0xF14000)) { scsiDmaW_(v); return; }
    if (addr >= 0xF10000 && addr < 0xF12000) {
        // pseudo-DMA write = reg 0 at +$200 (maclc.cpp:214-220)
        int reg = (addr >> 4) & 7;
        if (reg == 0 && (addr & 0xFFF) == 0x200) { scsiDmaW_(v); return; }
        scsi_.write(reg, v);
        scsiDrq(scsi_.drqActive());
        return;
    }
    if (addr >= 0xF14000 && addr < 0xF16000) {
        if (spiceClass()) ascSonora_.write(addr - 0xF14000, v);
        else              asc_.write(addr - 0xF14000, v);
        return;
    }
    if (addr >= 0xF16000 && addr < 0xF18000) {               // SWIM1 (O6.7)
        if (cpu_) cpu_->stall(5);
        if (spiceClass()) {                                  // Spice SWIM2
            swim2_.write((addr >> 9) & 0xF, v);
            return;
        }
        swim_.setSel((via_.portA() & 0x20) != 0);
        swim_.write((addr >> 9) & 0xF, v);
        return;
    }
    // Analogue CRT brightness/contrast DAC ($F18000, v8.cpp:700,925-929).
    // Accepted and dropped: the emulated CRT has no analogue stage.
    //
    // **Also on the Classic II** — identified 2026-08-02, and it had been
    // sitting in `TODO.md` as "identify the real block" since the machine
    // landed. Three pieces of evidence, none of them a guess:
    //   * `rominfo --universal` on the $3193670E ROM: the Classic II record
    //     ($003D14, Gestalt 23) has its own DecoderInfo $04C7A6 whose
    //     **decoder[6] = $50F18000**. That entry is ZERO on every LC / LC II
    //     record in the same table — the Classic II is the only machine here
    //     that declares a device at this base.
    //   * the ROM routine at $A51350 disassembles as a DAC feed: a 0-255
    //     setting scaled `×$2B >> 8` to 0-42, de-scrambled through a 43-entry
    //     table, then shifted out **370 times at stride 2** from $50F18000 —
    //     the write burst POM68K_V8_IOHOLE traces at PC $A51374.
    //   * MAME maps exactly this address as `bright_contrast_w` on Spice.
    // The Classic II is an all-in-one with a CRT, so it carries the same
    // cell. Behaviour is unchanged (the writes were already discarded by the
    // Eagle's forgiving-bus tail); what changes is that they are now a NAMED
    // device rather than an unexplained map hole.
    if ((spiceClass() || model_ == Model::ClassicII) &&
        addr >= 0xF18000 && addr < 0xF18400)
        return;
    if (addr >= 0xF24000 && addr < 0xF26000) { ariel_.write(addr & 3, v); return; }
    if (addr >= 0xF26000 && addr < 0xF28000) {
        pvia_.write(addr - 0xF26000, v);
        updateIrq();
        return;
    }

    if (const char* e = std::getenv("POM68K_V8_IOHOLE")) {
        static const long cap = std::atol(e) > 1 ? std::atol(e) : 200;
        static long n = 0;
        if (n++ < cap)
            std::fprintf(stderr, "[iohole] wr $%06X = $%02X pc=$%08X\n", addr, v,
                         cpu_ ? unsigned(cpu_->getPC()) : 0u);
        holeDump(addr);
    }
    if (model_ == Model::ClassicII) return;  // Eagle: forgiving bus (read8)
    busError();
}

uint16_t V8Memory::read16(uint32_t addr) {
    addr &= addrMask_;
    // POM68K perf (2026-07-17): word fast paths for RAM / ROM / VRAM —
    // the profile showed every 16-bit access splitting into two full
    // read8 decode cascades (1.8G calls at the Finder). Side-effect-free
    // regions only; the overlay case falls through to read8 (whose ROM
    // read clears the overlay).
    if (addr < 0xA00000) [[likely]] {                    // RAM space
        if (!overlay_) {
            uint32_t i0 = ramIndex(addr), i1 = ramIndex(addr + 1);
            return uint16_t(((i0 != 0xFFFFFFFF ? ram_[i0] : 0xFF) << 8)
                          |  (i1 != 0xFFFFFFFF ? ram_[i1] : 0xFF));
        }
    } else if (addr >= 0xF40000 && addr < 0xFBFFFF) {    // VRAM window
        return uint16_t(vram_[addr - 0xF40000] << 8 | vram_[addr - 0xF3FFFF]);
    } else if (addr >= 0xA00000 && addr < 0xB00000 && !overlay_) {
        uint32_t o = addr & romMask_;                    // ROM, mirrored
        return uint16_t(rom_[o] << 8 | rom_[(o + 1) & romMask_]);
    }
    // VIA1 reads mirror the byte on both lanes (v8.cpp:434-447)
    if (addr >= 0xF00000 && addr < 0xF02000 && !(addr & 0x80000000)) {
        uint16_t d = viaAccess8(addr, false, 0);
        return uint16_t(d | (d << 8));
    }
    // SCC word fast path: one ctl/data side-effect, byte mirrored on both
    // lanes. Falling through to two read8() would double-advance ptr_.
    if (addr >= 0xF04000 && addr < 0xF06000 && !(addr & 0x80000000)) {
        if (cpu_) cpu_->flushTicks();
        int ch = (addr >> 1) & 1;
        uint8_t d = ((addr >> 2) & 1) ? scc_.readData(ch)
                                      : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        if (onSccAccess) onSccAccess(addr, false, d);
        return uint16_t(d | (d << 8));
    }
    // Two sequenced statements: the operands of `|` are unsequenced in
    // C++ and read8 has side effects on device space (the SCSI
    // pseudo-DMA windows pop one FIFO byte per access) — a right-first
    // compiler would byte-swap every 16-bit blind transfer.
    const uint16_t hi = read8(addr);
    return uint16_t(hi << 8) | read8(addr + 1);
}

void V8Memory::write16(uint32_t addr, uint16_t v) {
    addr &= addrMask_;
    if (writeObserver_)
        observeWrite(cpu_, addr, 2, v, cpu_ ? cpu_->getPC0() : 0, 0);
    // POM68K perf (2026-07-17): word fast paths for RAM / VRAM (see
    // read16) — side-effect-free regions only.
    if (addr < 0xA00000) [[likely]] {                    // RAM space
        if (!overlay_) {
            uint32_t i0 = ramIndex(addr), i1 = ramIndex(addr + 1);
            if (jitGuard_) {
                // Both bus views of the byte pair, as in write8: the fixed
                // 2 MB alias means one RAM byte carries two bus addresses.
                jitGuard_->note(addr, 2);
                if (addr >= 0x800000 && mbMapped_ && (addr & 0x1FFFFF) < mbSize_)
                    jitGuard_->note(mbLoc_ + (addr & 0x1FFFFF), 2);
                else if (model_ != Model::MacTv && addr < 0x800000
                         && i0 != 0xFFFFFFFF && i0 < 0x200000)
                    jitGuard_->note(0x800000 | i0, 2);  // no alias on Tinker Bell
            }
            if (i0 != 0xFFFFFFFF) ram_[i0] = uint8_t(v >> 8);
            if (i1 != 0xFFFFFFFF) ram_[i1] = uint8_t(v);
            return;
        }
        return;                                          // overlay: unmapped
    }
    if (addr >= 0xF40000 && addr < 0xFBFFFF) {           // VRAM window
        vram_[addr - 0xF40000] = uint8_t(v >> 8);
        vram_[addr - 0xF3FFFF] = uint8_t(v);
        return;
    }
    // VIA1 word writes hit the register once per byte lane, low lane
    // first (v8.cpp:449-460 ACCESSING_BITS order)
    if (addr >= 0xF00000 && addr < 0xF02000 && !(addr & 0x80000000)) {
        viaAccess8(addr, true, uint8_t(v));
        viaAccess8(addr, true, uint8_t(v >> 8));
        return;
    }
    // SCC: one side-effect (high byte), matching the read16 mirror rule.
    if (addr >= 0xF04000 && addr < 0xF06000 && !(addr & 0x80000000)) {
        if (cpu_) cpu_->flushTicks();
        int ch = (addr >> 1) & 1;
        uint8_t b = uint8_t(v >> 8);
        if ((addr >> 2) & 1) scc_.writeData(ch, b);
        else                 scc_.writeCtl(ch, b);
        sccIrqLine(scc_.irqAsserted());
        if (onSccAccess) onSccAccess(addr, true, b);
        return;
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t V8Memory::peek8(uint32_t addr) const {
    addr &= addrMask_;
    if (addr & 0x80000000) return 0xFF;
    if (addr < 0xA00000) {
        if (overlay_) return addr < romSize_ ? rom_[addr] : 0xFF;
        uint32_t i = ramIndex(addr);
        return i != 0xFFFFFFFF ? ram_[i] : 0xFF;
    }
    if (addr < 0xB00000) return rom_[addr & romMask_];
    if (addr >= 0xF40000 && addr < 0xFC0000) return vram_[addr - 0xF40000];
    return 0xFF;
}

// DRQ-gated pseudo-DMA byte: without DRQ the V8 withholds /DSACK until
// the ~16 µs timeout raises /BERR — functionally, no data = bus error
// (macscsi.cpp:19-23; the blind-transfer MOVE.L loops rely on it)
uint8_t V8Memory::scsiDma_() {
    if (!scsi_.drqActive()) busError();
    uint8_t d = scsi_.dmaRead();
    scsiDrq(scsi_.drqActive());
    return d;
}

void V8Memory::scsiDmaW_(uint8_t v) {
    if (!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);
    scsiDrq(scsi_.drqActive());
}

// VIA1 timers at φ2 = CPU/20, plus the free-running 60.15 Hz tick timer
// into CA1 (v8.cpp:198-199,243-247 — NOT the video VBL; that one lands
// on pseudo-VIA slot bit $40 in O6.4). 60.15 Hz = 1203/20 Hz exactly,
// Bresenham on 20 × kCpuHz / 1203.
void V8Memory::tick(int cpuCycles) {
    viaPhase_ += cpuCycles;
    int viaCycles = viaPhase_ / viaDiv_;
    viaPhase_ %= viaDiv_;
    if (viaCycles && via_.tick(viaCycles)) updateIrq();

    tickAcc_ += int64_t(cpuCycles) * 1203;
    if (tickAcc_ >= cpuHz_ * 20) {
        tickAcc_ -= cpuHz_ * 20;
        via_.raiseCa1();
    }

    // Real video VBL → pseudo-VIA slot bit $40 (v8.cpp:106-108); geometry
    // reduced to CPU cycles at reset (Tinker Bell scans 800×525).
    framePos_ += cpuCycles;
    // Count completed frames as they roll over: the position alone is
    // modulo, so a raster decoder sampling once per frame at a fixed phase
    // could not otherwise tell a whole frame from no time at all
    // (VideoBeam::setPos).
    if (framePos_ >= frameCycles_) {
        frameCount_ += uint64_t(framePos_ / frameCycles_);
        framePos_ %= frameCycles_;
    }
    bool vbl = framePos_ >= vblStart_;
    if (vbl != vblState_) {
        vblState_ = vbl;
        pvia_.slotIrq(PseudoVia::VBL, vbl);
    }

    if (egretLleOn_) egretLle_.tick(cpuCycles);   // firmware + AdbLine
    else             egret_.tick(cpuCycles);      // may load the SR (SHIFT IRQ)
    // ASC drain, SWIM cells and the floppy spindle live in the C15M domain
    // (1:1 on every V8 machine but the Mac TV's C32M — the VASP pattern).
    c15Acc_ += int64_t(cpuCycles) * kCpuHz;
    int c15 = int(c15Acc_ / cpuHz_);
    c15Acc_ -= int64_t(c15) * cpuHz_;
    if (c15) {
        if (spiceClass()) {
            ascSonora_.tick(c15);            // Sonora EASC drain (Spice)
            swim2_.tick(c15);                // Spice/Tinker Bell SWIM2
        } else {
            asc_.tick(c15);                  // FIFO drain at 22 257 Hz
            swim_.tick(c15);                 // IWM nibbles / ISM cell engine
        }
        drive_.tick(c15);                    // spindle/tach time (was frozen)
    }
    scc_.tick(cpuCycles);                    // open-line Break/Abort stream (O6.11)
    sccIrq_ = scc_.irqAsserted();            // bidirectional — a de-asserted SCC
                                             // must lower the line too (updateIrq
                                             // below applies it); was latch-high only
    updateIrq();
}
