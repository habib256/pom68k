// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Save states: the archive core ──
// A snapshot is a versioned header followed by a sequence of tag/length
// chunks (see SaveState.cpp). This header holds the two archives that read
// and write them.
//
// The contract: every serializable class implements exactly ONE method
//
//     template <class Ar> void visit(Ar& ar) { ar(field, field, ...); }
//
// and both Writer and Reader instantiate that same body. Save and load
// therefore cannot drift apart — a field added to visit() is immediately
// live on both paths. Hand-written save()/load() pairs are the classic
// source of save-state corruption in emulators (a field added to one and
// forgotten in the other restores as garbage, usually months later); the
// visitor removes the failure mode by construction rather than by review.
//
// What must NOT go through visit():
//   - std::function callbacks and any pointer/reference between devices.
//     They are re-bound by the machine after a restore, never serialized.
//   - Pure caches (Moira's ATC, JIT blocks). Restore flushes them; they are
//     re-derivable from the page tables and RAM.
//   - Host-backed bulk data (ROM image, disk images). The snapshot carries
//     an identity checksum plus whatever the guest has since modified.
//
// Integers are encoded little-endian explicitly, so the format is defined
// rather than accidental. Snapshots are still same-version artifacts:
// the header pins a format version and the machine profile, and a mismatch
// is refused (see SaveState.cpp).

#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <vector>

// GCC 13's -Wstringop-overflow value-range analysis, re-run at LTO link
// time, merges one()'s failure branch with the past-the-end state of an
// enclosing fixed-array loop and reports the DEAD zero-store as an
// overflow of size 0 (23 spurious sites over Egret/AdbLine/Swim2/CudaLle
// in the 2026-09-01 warning census, none reachable). Scoped to this
// header so a real overflow elsewhere still fires; drop when the GCC
// analysis learns the pattern. (clang has no such group and reports zero
// warnings on this tree, hence the guard.)
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

namespace sav {

using u8  = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// ── Type traits used by the archive dispatch ────────────────────────────
template <class T> struct IsStdVector                  : std::false_type {};
template <class T, class A> struct IsStdVector<std::vector<T, A>> : std::true_type {};

template <class T> struct IsStdDeque                  : std::false_type {};
template <class T, class A> struct IsStdDeque<std::deque<T, A>> : std::true_type {};

// Length-prefixed sequence containers: same encoding, and both support the
// assign(n, value) the Reader needs. Device queues (AdbBus key events, SCC
// FIFOs) are deques, so they have to be first-class here.
template <class T>
inline constexpr bool kSizedSeq = IsStdVector<T>::value || IsStdDeque<T>::value;

// std::array is fixed-size: elements only, no length prefix (unlike vector).
template <class T> struct IsStdArray                        : std::false_type {};
template <class T, std::size_t N> struct IsStdArray<std::array<T, N>> : std::true_type {};

template <class T, class Ar>
concept Visitable = requires(T& t, Ar& ar) { t.visit(ar); };

// ── Writer ──────────────────────────────────────────────────────────────
class Writer {
public:
    static constexpr bool loading = false;

    explicit Writer(std::vector<u8>& out) noexcept : b_(out) {}

    template <class... Ts> void operator()(Ts&... xs) { (one(xs), ...); }

    // Raw fixed-size payload (no length prefix) — for buffers whose size is
    // known from context on both sides.
    void bytes(const void* p, std::size_t n) {
        const u8* q = static_cast<const u8*>(p);
        b_.insert(b_.end(), q, q + n);
    }

    // Bulk buffer with zero-run compression. Post-boot Mac RAM is mostly
    // zeros, so this is what keeps a 10 MB LC II (or a 36 MB Quadra)
    // snapshot to a sane size without pulling in a compression library.
    void blob(const std::vector<u8>& v);

    void varint(u64 v);
    bool ok() const noexcept { return true; }
    std::size_t pos() const noexcept { return b_.size(); }

private:
    template <class T> void one(T& x) {
        if constexpr (Visitable<T, Writer>) {
            x.visit(*this);
        } else if constexpr (std::is_enum_v<T>) {
            auto raw = static_cast<std::underlying_type_t<T>>(x);
            one(raw);
        } else if constexpr (std::is_same_v<T, bool>) {
            u8 raw = x ? 1 : 0;
            one(raw);
        } else if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) { auto r = std::bit_cast<std::uint32_t>(x); one(r); }
            else                          { auto r = std::bit_cast<std::uint64_t>(x); one(r); }
        } else if constexpr (std::is_integral_v<T>) {
            auto uv = static_cast<std::make_unsigned_t<T>>(x);
            for (std::size_t i = 0; i < sizeof(T); ++i)
                b_.push_back(static_cast<u8>(uv >> (8 * i)));
        } else if constexpr (std::is_array_v<T> || IsStdArray<T>::value) {
            for (auto& e : x) one(e);
        } else if constexpr (kSizedSeq<T>) {
            varint(x.size());
            for (auto& e : x) one(e);
        } else if constexpr (std::is_same_v<T, std::string>) {
            varint(x.size());
            bytes(x.data(), x.size());
        } else {
            static_assert(sizeof(T) == 0,
                          "sav::Writer: type has no visit() and is not a supported primitive");
        }
    }

    std::vector<u8>& b_;
};

// ── Reader ──────────────────────────────────────────────────────────────
// Every read is bounds-checked. A truncated or corrupt snapshot sets the
// failure flag and yields zeros from then on: loading a bad file must be a
// clean refusal, never undefined behaviour.
class Reader {
public:
    static constexpr bool loading = true;

    Reader(const u8* data, std::size_t len) noexcept : p_(data), n_(len) {}

    template <class... Ts> void operator()(Ts&... xs) { (one(xs), ...); }

    void bytes(void* p, std::size_t n) {
        if (!take(n)) { std::memset(p, 0, n); return; }
        std::memcpy(p, p_ + at_ - n, n);
    }

    void blob(std::vector<u8>& v);

    u64  varint();
    bool ok() const noexcept { return ok_; }
    void fail() noexcept { ok_ = false; }
    std::size_t pos() const noexcept { return at_; }
    std::size_t remaining() const noexcept { return ok_ ? n_ - at_ : 0; }

private:
    // Advances the cursor by n and reports whether the bytes were there.
    bool take(std::size_t n) noexcept {
        if (!ok_ || n > n_ - at_) { ok_ = false; return false; }
        at_ += n;
        return true;
    }

    template <class T> void one(T& x) {
        if constexpr (Visitable<T, Reader>) {
            x.visit(*this);
        } else if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> raw{};
            one(raw);
            x = static_cast<T>(raw);
        } else if constexpr (std::is_same_v<T, bool>) {
            u8 raw{};
            one(raw);
            x = raw != 0;
        } else if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) { std::uint32_t r{}; one(r); x = std::bit_cast<T>(r); }
            else                          { std::uint64_t r{}; one(r); x = std::bit_cast<T>(r); }
        } else if constexpr (std::is_integral_v<T>) {
            std::make_unsigned_t<T> uv = 0;
            if (!take(sizeof(T))) { x = T{}; return; }
            const u8* q = p_ + at_ - sizeof(T);
            for (std::size_t i = 0; i < sizeof(T); ++i)
                uv |= static_cast<std::make_unsigned_t<T>>(q[i]) << (8 * i);
            x = static_cast<T>(uv);
        } else if constexpr (std::is_array_v<T> || IsStdArray<T>::value) {
            for (auto& e : x) one(e);
        } else if constexpr (kSizedSeq<T>) {
            const u64 n = varint();
            // A corrupt length must not turn into a huge allocation.
            if (!ok_ || n > remaining() + 1) { fail(); x.clear(); return; }
            x.assign(static_cast<std::size_t>(n), typename T::value_type{});
            for (auto& e : x) one(e);
        } else if constexpr (std::is_same_v<T, std::string>) {
            const u64 n = varint();
            if (!ok_ || n > remaining()) { fail(); x.clear(); return; }
            x.assign(reinterpret_cast<const char*>(p_ + at_), static_cast<std::size_t>(n));
            take(static_cast<std::size_t>(n));
        } else {
            static_assert(sizeof(T) == 0,
                          "sav::Reader: type has no visit() and is not a supported primitive");
        }
    }

    const u8*   p_;
    std::size_t n_;
    std::size_t at_ = 0;
    bool        ok_ = true;
};

// ── Snapshot container ──────────────────────────────────────────────────
// Chunks are tag(4 chars) + u32 length. The reader skips tags it does not
// know, so a snapshot that gained a device chunk still loads its common
// state in an older build (with the extra chunk reported, not silently
// dropped).
inline constexpr char     kMagic[8]  = {'P','O','M','6','8','K','S','S'};
// v2 adds serialized per-device scheduler debt (Q605/SCC and 53C96). A v1
// Q605 chunk has no value from which that observable elapsed time can be
// reconstructed, so reject it explicitly instead of misreading the tail.
// v3 adds the serialized per-device SCC/53C96 debts on Centris, Q630 and
// Q700 (including the Eclipse second bus). Older readers would otherwise
// accept the longer chunk while silently dropping elapsed device time.
// v4 stamps the session's active HLE modules and strict qualification.
// v5 carries two additions, both real machine state a v4 chunk cannot
// supply: the classic ASC's four wavetable oscillators (phase and
// increment, `AscV8::wtPhase_`/`wtIncr_` — a free-running oscillator is not
// re-derivable from anything else, and dropping it would restore four
// silent voices mid-note), and the six Egret/Cuda platforms' pending warm
// restart (`restartPending_`, the firmware's RESET_SYSTEM latched between
// the MCU callback and the CPU's next run boundary — a snapshot taken in
// that window must not resume without the reset it owes).
// v6 replaces the SWIM1/SWIM2 fixed-window read phase (`cellPhase_`, one
// int) with the FluxPll data separator (window phase, pulled period, flux
// clock — § 1.3 flux plan step 3). A v5 chunk cannot supply a separator
// state, and resuming one mid-sector with a nominal loop would shift every
// following window on non-ideal media, so it is refused, not migrated.
// v7 replaces SWIM1's separator state with the real ISM read engine's
// (§ 1.3 step 4b, MAME swim1.cpp:885-1233): edge clock, LS-pair phase,
// CSM calibration counters and the two correction factors, TSM assembly.
// A snapshot taken mid-calibration must resume with the same error
// counters or the correction factors — which scale every threshold the
// rest of the read uses — come out different. v6 never shipped a release;
// it is refused like every other mismatch, not migrated.
// v9 DROPS a field from the CPU chunk instead of adding one: Moira's `cp`,
// the 68020+ extended-addressing cycle-penalty accumulator, which
// `AVAILABILITY` zeroes at the start of every instruction and which is
// therefore meaningless at the instruction boundary a snapshot is taken on.
// The JIT never touches it, so carrying it made a snapshot's fingerprint
// depend on which engine had run last — `MoiraSnapshot.h` has the receipt.
// v10 drops two disproved ADB-route payloads from machine chunks: the Duo's
// synthetic command-level bus/reply scheduler and Eclipse's unused Egret bus.
// A v9 chunk would otherwise shift every following field while still passing
// the header check, so the incompatible shorter layouts require a hard bump.
inline constexpr u32      kVersion   = 10;  // v10: dead ADB route state removed

struct Header {
    u32 version     = kVersion;
    u32 machineKind = 0;        // MachineKind, pinned: refuse a mismatch
    u32 romChecksum = 0;        // the ROM is not stored, only verified
    u64 ramSize     = 0;
    u64 emuCycles   = 0;        // CPU-cycle stamp (CLAUDE.md: emuCycles everywhere)
    u32 conformance = 0;        // LleSession.h SnapshotFlag + HLE module mask

    template <class Ar> void visit(Ar& ar) {
        ar(version, machineKind, romChecksum, ramSize, emuCycles, conformance);
    }
};

// Writes tag + length around a body, patching the length once the body is
// known. Scope-based: the chunk closes when the object dies.
class Chunk {
public:
    Chunk(std::vector<u8>& out, const char tag[4]);
    ~Chunk();
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

private:
    std::vector<u8>& b_;
    std::size_t      lenAt_;
};

// Reader side of the container: walks tag/length records in order. Returns
// false at the end of the sequence or on a malformed record. Skipping a tag
// the build does not know is what makes the format forward-compatible — so
// an unknown chunk is reported to the caller, never silently dropped.
struct ChunkView {
    char        tag[4] = {};
    const u8*   data   = nullptr;
    std::size_t len    = 0;

    bool is(const char t[4]) const noexcept {
        return tag[0] == t[0] && tag[1] == t[1] && tag[2] == t[2] && tag[3] == t[3];
    }
    // A Reader scoped to this chunk's payload alone: a device that reads
    // past its own chunk fails instead of walking into the next device's.
    Reader reader() const noexcept { return Reader(data, len); }
};

bool nextChunk(const u8*& cur, const u8* end, ChunkView& out) noexcept;

// FNV-1a over a byte range — the gate's cheap state fingerprint.
u64 hash(const void* p, std::size_t n) noexcept;
inline u64 hash(const std::vector<u8>& v) noexcept { return hash(v.data(), v.size()); }

} // namespace sav

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
