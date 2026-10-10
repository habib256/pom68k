// POM68K — gate `guest_scrap_test`: the guest's TEXT scrap read from the
// host (src/GuestScrap.h), on a flat memory image built here.
//
//   • a scrap with a PICT entry of odd length (RIFF-like padding) before
//     its TEXT, MacRoman high characters and CR line ends: the TEXT is
//     found and converted to UTF-8 with LF;
//   • the handle as it is now — a block moved between two reads is found
//     through its master pointer, a purged one (nil master pointer) is
//     refused; 24-bit master pointers lose their flag byte;
//   • refusals by name: scrap on disk, uninitialized, no handle, no TEXT,
//     an entry longer than the scrap, an odd handle, an unreadable range;
//   • bounds — a TEXT past the ceiling is truncated and says so;
//   • the whole MacRoman table round-trips with no unmapped byte.
//
// No ROM, no image.

#include "GuestScrap.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace pom68k;
using Status = GuestScrapText::Status;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

struct Image {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(1u << 20, 0);
    std::uint32_t holeFrom = 0, holeTo = 0;   // reads in [from, to) fail
    void put32(std::uint32_t a, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) ram[a + i] = std::uint8_t(v >> (24 - 8 * i));
    }
    void put16(std::uint32_t a, std::uint16_t v) { ram[a] = v >> 8; ram[a + 1] = v & 0xFF; }
    bool read(std::uint32_t a, std::uint8_t* out, std::size_t n) const {
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t x = a + std::uint32_t(i);
            if (x >= ram.size() || (x >= holeFrom && x < holeTo)) return false;
            out[i] = ram[x];
        }
        return true;
    }
    GuestScrapText scrap(std::size_t max = kMaxGuestScrapText) const {
        return readGuestScrapText(
            [this](std::uint32_t a, std::uint8_t* o, std::size_t n) { return read(a, o, n); },
            max);
    }
};

constexpr std::uint32_t kHandle = 0x2000, kBlock = 0x10000;

// ScrapState 1, MMU32Bit 1, ScrapCount 7; entries written at `block`.
std::uint32_t buildScrap(Image& m, std::uint32_t block,
                         const std::vector<std::uint8_t>& text) {
    std::uint32_t off = 0;
    m.put32(block + off, 0x50494354u); m.put32(block + off + 4, 3);  // 'PICT', 3
    m.ram[block + 8] = 1; m.ram[block + 9] = 2; m.ram[block + 10] = 3;
    off = 8 + 3 + 1;                                                   // padded
    m.put32(block + off, 0x54455854u); m.put32(block + off + 4, std::uint32_t(text.size()));
    std::memcpy(&m.ram[block + off + 8], text.data(), text.size());
    off += 8 + std::uint32_t(text.size()) + (text.size() & 1);
    m.put32(0x0960, off);
    m.put32(0x0964, kHandle);
    m.put16(0x0968, 7);
    m.put16(0x096A, 1);
    m.ram[0x0CB2] = 1;
    m.put32(kHandle, block);
    return off;
}

} // namespace

int main() {
    const std::vector<std::uint8_t> mac = {'C', 'a', 'f', 0x8E, '\r', 'n', 0xA5, 'e', 0xDB};
    {
        Image m;
        buildScrap(m, kBlock, mac);
        const GuestScrapText s = m.scrap();
        check(s.ok() && s.utf8 == "Café\nn•e¤" && s.macBytes == 9 && s.count == 7 &&
                  !s.truncated,
              "TEXT found after an odd-length PICT, MacRoman → UTF-8, CR → LF");

        // The Memory Manager moves the block; the master pointer follows.
        buildScrap(m, 0x30000, {'m', 'o', 'v', 'e', 'd'});
        m.put32(kHandle, 0x30000);
        check(m.scrap().utf8 == "moved", "a moved block is found through its master pointer");
        m.put32(kHandle, 0);
        check(m.scrap().status == Status::Purged, "a purged block is refused");

        buildScrap(m, kBlock, mac);
        m.ram[0x0CB2] = 0;
        m.put32(kHandle, 0x80000000u | kBlock);                      // locked flag
        check(m.scrap().ok(), "24-bit: the master pointer's flag byte is masked off");
        m.ram[0x0CB2] = 1;
        check(!m.scrap().ok(), "32-bit: the same high byte is an address, not a flag");
    }
    {
        Image m;
        buildScrap(m, kBlock, mac);
        m.put16(0x096A, 0);
        check(m.scrap().status == Status::OnDisk, "a scrap on disk is refused by name");
        m.put16(0x096A, 0xFFFF);
        check(m.scrap().status == Status::Uninitialized, "an uninitialized scrap is refused");
        m.put16(0x096A, 1);
        m.put32(0x0964, 0);
        check(m.scrap().status == Status::NoScrap, "no handle: no scrap");
        m.put32(0x0964, kHandle + 1);
        check(m.scrap().status == Status::Malformed, "an odd handle is malformed");
        m.put32(0x0964, kHandle);
        m.put32(kBlock + 12 + 4, 0x1000);                             // TEXT length
        check(m.scrap().status == Status::Malformed,
              "an entry longer than the scrap is malformed");
        buildScrap(m, kBlock, mac);
        m.holeFrom = kBlock + 20; m.holeTo = kBlock + 21;
        check(m.scrap().status == Status::Unreadable,
              "a byte the logical read refuses makes the scrap unreadable");
        m.holeFrom = m.holeTo = 0;
        m.put32(kBlock + 12, 0x7374796Cu);                            // 'styl'
        check(m.scrap().status == Status::NoText, "a scrap without TEXT says so");
        buildScrap(m, kBlock, mac);
        const GuestScrapText cut = m.scrap(4);
        check(cut.ok() && cut.truncated && cut.utf8 == "Café" && cut.macBytes == 9,
              "a TEXT past the ceiling is truncated and says so");
        check(cut.reason.empty() && m.scrap().reason.empty(),
              "a successful read carries no refusal");
        m.put16(0x096A, 0);
        check(!m.scrap().reason.empty(), "every refusal carries its reason");
    }
    {
        std::vector<std::uint8_t> all;
        for (int c = 0x80; c <= 0xFF; ++c) all.push_back(std::uint8_t(c));
        const std::string text = macRomanToUtf8(all.data(), all.size());
        bool mapped = true;
        std::size_t points = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto b = std::uint8_t(text[i]);
            if ((b & 0xC0) != 0x80) ++points;
            mapped &= b != 0;
        }
        check(mapped && points == 128 && text.find("\xEF\xA3\xBF") != std::string::npos,
              "all 128 MacRoman high bytes map, the Apple logo to U+F8FF");
    }
    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
