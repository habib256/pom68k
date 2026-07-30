// POM68K — save-state archive core unit test (src/SaveState.h/.cpp).
// Proves the four properties every chunk of every device relies on:
//   1. Writer and Reader agree — a visit() body round-trips exactly.
//   2. The wire format is defined (little-endian, LEB128), not accidental.
//   3. The zero-run codec is exact AND actually compresses; that is the
//      only reason a 10 MB LC II snapshot is a sane size with no external
//      compression library in the build.
//   4. A truncated or corrupt snapshot is refused cleanly — no crash, no
//      wild allocation. Loading a bad file is a user-facing path, so this
//      is a correctness requirement, not a nicety.

#include "SaveState.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); failures++; } } while (0)

// ── Fixtures exercising every branch of the archive dispatch ────────────
enum class Flavor : std::uint16_t { Egret = 3, Cuda = 7 };

struct Nested {
    std::uint8_t a = 0;
    std::int32_t b = 0;
    template <class Ar> void visit(Ar& ar) { ar(a, b); }
};

struct All {
    std::uint8_t  u8v  = 0;   std::int8_t   i8v  = 0;
    std::uint16_t u16v = 0;   std::int16_t  i16v = 0;
    std::uint32_t u32v = 0;   std::int32_t  i32v = 0;
    std::uint64_t u64v = 0;   std::int64_t  i64v = 0;
    bool          bt = false, bf = false;
    Flavor        fl = Flavor::Egret;
    float         f = 0;      double        d = 0;
    std::uint8_t  carr[4] = {};
    std::array<std::uint16_t, 3> sarr {};
    std::vector<std::uint32_t>   vec;
    std::string   str;
    Nested        nest;

    template <class Ar> void visit(Ar& ar) {
        ar(u8v, i8v, u16v, i16v, u32v, i32v, u64v, i64v,
           bt, bf, fl, f, d, carr, sarr, vec, str, nest);
    }
};

static All populated() {
    All a;
    a.u8v = 0xA5;                 a.i8v  = -128;
    a.u16v = 0xBEEF;              a.i16v = -32768;
    a.u32v = 0xDEADBEEF;          a.i32v = -2147483647 - 1;
    a.u64v = 0x0123456789ABCDEFull;
    a.i64v = -9223372036854775807ll - 1;
    a.bt = true;                  a.bf = false;
    a.fl = Flavor::Cuda;
    a.f = -3.5f;                  a.d = 1.0e-300;
    a.carr[0] = 1; a.carr[1] = 2; a.carr[2] = 3; a.carr[3] = 255;
    a.sarr = {0x1111, 0x2222, 0x3333};
    a.vec = {1, 0xFFFFFFFFu, 42};
    a.str = "GISTPERSO";
    a.nest.a = 0x5A;              a.nest.b = -1234567;
    return a;
}

static bool same(const All& x, const All& y) {
    return x.u8v == y.u8v && x.i8v == y.i8v && x.u16v == y.u16v && x.i16v == y.i16v
        && x.u32v == y.u32v && x.i32v == y.i32v && x.u64v == y.u64v && x.i64v == y.i64v
        && x.bt == y.bt && x.bf == y.bf && x.fl == y.fl
        && x.f == y.f && x.d == y.d
        && std::memcmp(x.carr, y.carr, sizeof x.carr) == 0
        && x.sarr == y.sarr && x.vec == y.vec && x.str == y.str
        && x.nest.a == y.nest.a && x.nest.b == y.nest.b;
}

// Deterministic filler — no <random>, so the gate is reproducible verbatim.
static std::uint32_t lcg(std::uint32_t& s) { return s = s * 1664525u + 1013904223u; }

// ── 1. Round-trip ───────────────────────────────────────────────────────
static void testRoundTrip() {
    All src = populated();
    std::vector<sav::u8> buf;
    { sav::Writer w(buf); w(src); }

    All dst;
    sav::Reader r(buf.data(), buf.size());
    r(dst);
    CHECK(r.ok(), "round-trip: reader reported failure");
    CHECK(r.remaining() == 0, "round-trip: reader did not consume the whole buffer");
    CHECK(same(src, dst), "round-trip: a field did not survive save+load");

    // Save -> load -> save must be byte-identical. This is the cheap
    // invariant that catches a visit() reading a different field order
    // than it writes.
    std::vector<sav::u8> buf2;
    { sav::Writer w(buf2); w(dst); }
    CHECK(buf == buf2, "round-trip: re-save is not byte-identical");
}

// ── 2. Wire format is defined, not accidental ───────────────────────────
static void testWireFormat() {
    std::vector<sav::u8> buf;
    { sav::Writer w(buf);
      std::uint32_t v = 0x12345678;
      w(v); }
    CHECK(buf.size() == 4, "wire: u32 is not 4 bytes");
    CHECK(buf[0] == 0x78 && buf[1] == 0x56 && buf[2] == 0x34 && buf[3] == 0x12,
          "wire: u32 is not little-endian");

    // LEB128: 300 = 0xAC 0x02
    std::vector<sav::u8> vb;
    { sav::Writer w(vb); w.varint(300); }
    CHECK(vb.size() == 2 && vb[0] == 0xAC && vb[1] == 0x02, "wire: varint(300) wrong");

    // Varints round-trip across every width boundary.
    const std::uint64_t probes[] = {0, 1, 127, 128, 16383, 16384,
                                    0xFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull};
    for (std::uint64_t p : probes) {
        std::vector<sav::u8> b;
        { sav::Writer w(b); w.varint(p); }
        sav::Reader r(b.data(), b.size());
        const std::uint64_t got = r.varint();
        CHECK(r.ok() && got == p, "wire: varint round-trip failed");
    }
}

// ── 3. Zero-run codec: exact, and it must actually compress ─────────────
static void testBlob() {
    struct Case { const char* name; std::vector<sav::u8> data; };
    std::vector<Case> cases;

    cases.push_back({"empty", {}});
    cases.push_back({"all zeros", std::vector<sav::u8>(64 * 1024, 0)});

    std::vector<sav::u8> dense(64 * 1024);
    std::uint32_t s = 12345;
    for (auto& b : dense) b = static_cast<sav::u8>(lcg(s) >> 24) | 1;  // never 0
    cases.push_back({"no zeros", dense});

    // Short gaps below the run threshold must stay literal, not fragment
    // the stream into thousands of two-varint records.
    std::vector<sav::u8> gaps(32 * 1024, 0x77);
    for (std::size_t i = 0; i < gaps.size(); i += 8) { gaps[i] = 0; gaps[i + 1] = 0; }
    cases.push_back({"short gaps", gaps});

    // The realistic shape: mostly zeros with scattered live data, i.e. Mac
    // RAM after boot.
    std::vector<sav::u8> sparse(1u << 20, 0);
    s = 999;
    for (int i = 0; i < 4000; ++i) sparse[lcg(s) % sparse.size()] = 0xEE;
    cases.push_back({"sparse", sparse});

    cases.push_back({"single byte", {0x01}});
    cases.push_back({"leading zeros", {0, 0, 0, 0, 0, 0, 9, 9}});
    cases.push_back({"trailing zeros", {9, 9, 0, 0, 0, 0, 0, 0}});

    for (auto& c : cases) {
        std::vector<sav::u8> buf;
        { sav::Writer w(buf); w.blob(c.data); }

        std::vector<sav::u8> out;
        sav::Reader r(buf.data(), buf.size());
        r.blob(out);

        char msg[128];
        std::snprintf(msg, sizeof msg, "blob '%s': round-trip mismatch", c.name);
        CHECK(r.ok() && out == c.data, msg);
        std::snprintf(msg, sizeof msg, "blob '%s': reader left bytes unconsumed", c.name);
        CHECK(r.remaining() == 0, msg);
    }

    // The point of the codec: a mostly-zero megabyte must not cost a
    // megabyte. Without this assertion the codec could silently degrade to
    // "store verbatim" and every gate would still pass.
    {
        std::vector<sav::u8> buf;
        { sav::Writer w(buf); w.blob(cases[1].data); }          // all zeros
        CHECK(buf.size() < 32, "blob: an all-zero buffer did not compress");
    }
    {
        std::vector<sav::u8> buf;
        { sav::Writer w(buf); w.blob(cases[4].data); }          // sparse 1 MB
        CHECK(buf.size() < cases[4].data.size() / 8,
              "blob: sparse RAM-shaped buffer compressed poorly");
    }
    {
        std::vector<sav::u8> buf;
        { sav::Writer w(buf); w.blob(cases[2].data); }          // no zeros
        CHECK(buf.size() < cases[2].data.size() + 64,
              "blob: incompressible buffer paid too much overhead");
    }
}

// ── 4. Chunk container: order, payload isolation, unknown-tag skip ──────
static void testChunks() {
    std::vector<sav::u8> buf;
    {
        { sav::Chunk c(buf, "CPU "); sav::Writer w(buf); std::uint32_t pc = 0x40801234; w(pc); }
        { sav::Chunk c(buf, "XXXX"); sav::Writer w(buf); std::uint64_t junk = 0; w(junk); }
        { sav::Chunk c(buf, "VIA1"); sav::Writer w(buf); std::uint8_t ifr = 0x82; w(ifr); }
    }

    const sav::u8* cur = buf.data();
    const sav::u8* end = buf.data() + buf.size();
    sav::ChunkView cv;
    int seen = 0, skipped = 0;
    std::uint32_t pc = 0; std::uint8_t ifr = 0;

    while (sav::nextChunk(cur, end, cv)) {
        ++seen;
        if (cv.is("CPU ")) { auto r = cv.reader(); r(pc);  CHECK(r.ok(), "chunk: CPU read failed"); }
        else if (cv.is("VIA1")) { auto r = cv.reader(); r(ifr); CHECK(r.ok(), "chunk: VIA1 read failed"); }
        else ++skipped;                       // an unknown tag is skippable
    }
    CHECK(seen == 3, "chunk: wrong number of chunks walked");
    CHECK(skipped == 1, "chunk: unknown tag was not reported as unknown");
    CHECK(pc == 0x40801234u, "chunk: CPU payload wrong");
    CHECK(ifr == 0x82, "chunk: VIA1 payload wrong — chunks are out of order or overlapping");

    // A chunk reader must not be able to walk into the next chunk.
    cur = buf.data();
    CHECK(sav::nextChunk(cur, end, cv), "chunk: first walk failed");
    { auto r = cv.reader();
      std::uint32_t a = 0, b = 0;
      r(a, b);                                  // second read is past the chunk
      CHECK(!r.ok(), "chunk: reader ran past its own payload"); }
}

// ── 5. Corrupt / truncated input is refused, never fatal ────────────────
static void testRobustness() {
    All src = populated();
    std::vector<sav::u8> buf;
    { sav::Writer w(buf); w(src); }

    // Every strict prefix must fail cleanly. If any prefix "succeeds" the
    // reader is not bounds-checking somewhere.
    for (std::size_t n = 0; n < buf.size(); ++n) {
        All dst;
        sav::Reader r(buf.data(), n);
        r(dst);
        if (r.ok()) {
            std::printf("FAIL: truncation at %zu/%zu bytes was accepted\n", n, buf.size());
            failures++;
            break;
        }
    }

    // A corrupt container length must not be believed.
    {
        std::vector<sav::u8> bad = {'C','P','U',' ', 0xFF, 0xFF, 0xFF, 0x7F, 0x00};
        const sav::u8* cur = bad.data();
        sav::ChunkView cv;
        CHECK(!sav::nextChunk(cur, bad.data() + bad.size(), cv),
              "robust: an over-long chunk length was accepted");
    }

    // A corrupt vector length must fail instead of attempting a huge
    // allocation (the varint below claims ~4 G elements).
    {
        std::vector<sav::u8> bad;
        { sav::Writer w(bad); w.varint(0xFFFFFFFFull); }
        std::vector<std::uint32_t> v;
        sav::Reader r(bad.data(), bad.size());
        r(v);
        CHECK(!r.ok() && v.empty(), "robust: absurd vector length was accepted");
    }

    // Same for a bulk buffer.
    {
        std::vector<sav::u8> bad;
        { sav::Writer w(bad); w.varint(0xFFFFFFFFFFFFull); }
        std::vector<sav::u8> v;
        sav::Reader r(bad.data(), bad.size());
        r.blob(v);
        CHECK(!r.ok() && v.empty(), "robust: absurd blob length was accepted");
    }

    // A blob whose records never advance must not loop forever.
    {
        std::vector<sav::u8> bad;
        { sav::Writer w(bad); w.varint(16); w.varint(0); w.varint(0); }
        std::vector<sav::u8> v;
        sav::Reader r(bad.data(), bad.size());
        r.blob(v);
        CHECK(!r.ok(), "robust: a non-advancing blob record was accepted");
    }
}

// ── 6. The header pins identity ─────────────────────────────────────────
static void testHeader() {
    sav::Header h;
    h.machineKind = 12; h.romChecksum = 0x350EACF0; h.ramSize = 10u << 20;
    h.emuCycles = 123456789012ull;

    std::vector<sav::u8> buf;
    { sav::Writer w(buf); w(h); }

    sav::Header g;
    sav::Reader r(buf.data(), buf.size());
    r(g);
    CHECK(r.ok(), "header: read failed");
    CHECK(g.version == sav::kVersion && g.machineKind == 12
          && g.romChecksum == 0x350EACF0u && g.ramSize == (10u << 20)
          && g.emuCycles == 123456789012ull, "header: field mismatch");

    // The fingerprint the etalon compares before/after a restore: stable on
    // identical bytes, and sensitive to a single flipped bit anywhere.
    const sav::u64 h0 = sav::hash(buf);
    CHECK(h0 == sav::hash(buf), "hash: not deterministic");
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] ^= 1;
        const bool differs = sav::hash(buf) != h0;
        buf[i] ^= 1;
        if (!differs) { std::printf("FAIL: hash blind to bit 0 of byte %zu\n", i); failures++; break; }
    }
}

int main() {
    testRoundTrip();
    testWireFormat();
    testBlob();
    testChunks();
    testRobustness();
    testHeader();

    if (failures) { std::printf("savestate_test: %d failure(s)\n", failures); return 1; }
    std::printf("savestate_test: OK\n");
    return 0;
}
