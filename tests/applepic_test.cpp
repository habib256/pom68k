// Apple PIC IOP gate (docs/IOP_BRINGUP.md milestone 2).
//
// Exercises `ApplePic` end to end the way the IIfx ROM will: the host
// uploads a hand-assembled 65C02 program through the shared-RAM window
// (vectors included — the whole internal map is RAM), releases /RSTPIC,
// and the two sides talk. Proven here:
//
//   1. reset hold — the 65C02 does not run until /RSTPIC is written 1;
//   2. window upload + readback, auto-increment on and off;
//   3. IOP → host interrupt ($F035 INTHST0 → hint callback → host ack);
//   4. host → IOP interrupt (control bit 3 → IRQ_HOST → the program's ISR);
//   5. timer: one-shot latch*8+12, then continuous-mode cadence
//      (latch+2)*8 measured over 100 periods;
//   6. DMA both directions (I/O→RAM and RAM→I/O), completion interrupt
//      flags and DMAEN auto-clear;
//   7. bypass mode: host reaches the peripheral registers directly, and
//      device-reg reads return 0 while NOT in bypass.
//
// The guest program: mask host+timer ints, CLI, raise INTHST0, arm the
// timer one-shot, switch bypass on, then heartbeat INC $10 forever. Its
// ISR records the (masked) flags at $11, acks them, counts at $12.

#include "ApplePic.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond, msg, ...)                                                \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__);          \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

void setAddr(ApplePic& pic, uint16_t addr)
{
    pic.hostWrite(0, static_cast<uint8_t>(addr >> 8));
    pic.hostWrite(1, static_cast<uint8_t>(addr));
}

void upload(ApplePic& pic, uint16_t addr, const std::vector<uint8_t>& bytes)
{
    setAddr(pic, addr);
    for (uint8_t b : bytes)
        pic.hostWrite(4, b);   // data port, auto-increment
}

// The guest program (hand-assembled, entry $0200, ISR $0280).
const std::vector<uint8_t> kMain = {
    0xA9, 0x30,             // LDA #$30      ; int mask: host($10) + timer($20)
    0x8D, 0x33, 0xF0,       // STA $F033
    0x58,                   // CLI
    0xA9, 0x04,             // LDA #$04      ; INTHST0
    0x8D, 0x35, 0xF0,       // STA $F035
    0xA9, 0x40,             // LDA #$40
    0x8D, 0x10, 0xF0,       // STA $F010     ; timer latch lo = $40
    0xA9, 0x00,             // LDA #$00
    0x8D, 0x11, 0xF0,       // STA $F011     ; latch hi = 0 → ARM ($40*8+12)
    0xA9, 0x01,             // LDA #$01
    0x8D, 0x30, 0xF0,       // STA $F030     ; host bypass ON
    0xE6, 0x10,             // INC $10       ; heartbeat
    0x4C, 0x1A, 0x02,       // JMP $021A
};
const std::vector<uint8_t> kIsr = {
    0xAD, 0x34, 0xF0,       // LDA $F034     ; flags & mask
    0x85, 0x11,             // STA $11       ; record
    0x8D, 0x34, 0xF0,       // STA $F034     ; ack (write-1-to-clear)
    0xE6, 0x12,             // INC $12       ; count
    0x40,                   // RTI
};

} // namespace

int main()
{
    ApplePic pic;

    // Fake peripheral + wire logs.
    int hostIntEdges = 0;
    bool hostIntLevel = false;
    pic.hostInt = [&](bool level) {
        if (level != hostIntLevel) ++hostIntEdges;
        hostIntLevel = level;
    };
    uint8_t dmaPattern[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    int dmaReadSeq = 0;
    std::vector<uint8_t> periphWrites;
    pic.readPeriph = [&](int reg) -> uint8_t {
        if (reg == 6) return dmaPattern[dmaReadSeq++ & 7];
        return static_cast<uint8_t>(0xA0 | reg);
    };
    pic.writePeriph = [&](int reg, uint8_t v) {
        if (reg == 7) periphWrites.push_back(v);
    };

    pic.reset();

    // 1. Held in reset; device regs unreachable outside bypass.
    CHECK(pic.cpuHeld(), "CPU must be held after reset");
    CHECK(pic.hostRead(0x15) == 0, "device-reg read must return 0 in non-bypass");

    // 2. Upload through the window (auto-increment), then verify readback.
    pic.hostWrite(2, 0x02);                       // auto-increment on, still held
    upload(pic, 0x0200, kMain);
    upload(pic, 0x0280, kIsr);
    upload(pic, 0xFFFC, {0x00, 0x02, 0x80, 0x02}); // RESET=$0200, IRQ=$0280
    setAddr(pic, 0x0200);
    bool rbOk = true;
    for (uint8_t b : kMain)
        rbOk = rbOk && (pic.hostRead(4) == b);
    CHECK(rbOk, "window readback of the uploaded program");
    CHECK(pic.ramByte(0x7FFC) == 0x00 && pic.ramByte(0x7FFD) == 0x02,
          "vector upload must land in the $7800-$7FFF mirror");
    // Auto-increment off: the same byte twice.
    pic.hostWrite(2, 0x00);
    setAddr(pic, 0x0200);
    CHECK(pic.hostRead(4) == kMain[0] && pic.hostRead(4) == kMain[0],
          "auto-increment off must re-read the same byte");
    CHECK(pic.cpuHeld(), "upload traffic must not release the CPU");

    // 3. Release /RSTPIC; the program raises INTHST0 and arms the timer.
    pic.hostWrite(2, 0x06);                       // run + auto-increment
    CHECK(!pic.cpuHeld(), "CPU must run after /RSTPIC=1");
    pic.tick(2000);
    CHECK(hostIntLevel, "INTHST0 must raise the host interrupt line");
    CHECK((pic.hostRead(2) & 0x10) != 0, "status must show INTHST0 pending");
    pic.hostWrite(2, 0x16);                       // ack INTHST0, keep running
    CHECK(!hostIntLevel, "host ack must drop the interrupt line");
    CHECK(hostIntEdges == 2, "host int line must have pulsed exactly once (saw %d edges)",
          hostIntEdges);

    // Heartbeat proves the main loop is alive.
    const uint8_t hb0 = pic.ramByte(0x10);
    pic.tick(800);
    CHECK(pic.ramByte(0x10) != hb0, "heartbeat $10 must advance");

    // 5a. The one-shot timer fired exactly once, ISR saw the timer flag.
    CHECK(pic.ramByte(0x12) == 1, "one IRQ so far (timer one-shot), got %d",
          pic.ramByte(0x12));
    CHECK(pic.ramByte(0x11) == 0x20, "ISR must have seen the timer flag, got $%02X",
          pic.ramByte(0x11));

    // 4. Host-triggered IOP interrupt.
    pic.hostWrite(2, 0x0E);                       // bit3 = INTPIC, keep running
    pic.tick(400);
    CHECK(pic.ramByte(0x12) == 2, "host-int IRQ must be serviced, count=%d",
          pic.ramByte(0x12));
    CHECK(pic.ramByte(0x11) == 0x10, "ISR must have seen the host flag, got $%02X",
          pic.ramByte(0x11));

    // 7. Bypass mode is on now (the program wrote $F030=1).
    CHECK((pic.hostRead(2) & 0x01) != 0, "bypass status bit must read back");
    CHECK(pic.hostRead(0x15) == 0xA5, "bypass read must reach the peripheral");

    // 5b. Continuous mode: period (latch+2)*8 = $42*8 = 528 clocks. Host
    // programs $F032 and re-arms through the window ($F011 write arms).
    setAddr(pic, 0xF032);
    pic.hostWrite(4, 0x01);
    setAddr(pic, 0xF011);
    pic.hostWrite(4, 0x00);
    const uint8_t irqBefore = pic.ramByte(0x12);
    pic.tick(528 * 100);
    const int periods = static_cast<uint8_t>(pic.ramByte(0x12) - irqBefore);
    CHECK(periods >= 95 && periods <= 105,
          "continuous timer must tick ~100 periods, got %d", periods);
    // Stop the cadence for the DMA phase below.
    setAddr(pic, 0xF032);
    pic.hostWrite(4, 0x00);
    pic.tick(1200);   // drain the last armed period

    // 6a. DMA I/O→RAM on channel A: io reg 6, dest $1000, 8 bytes.
    setAddr(pic, 0xF020);
    pic.hostWrite(4, 0x65);   // ctrl: io=6, DIR=I/O→RAM, EN
    pic.hostWrite(4, 0x00);   // map lo
    pic.hostWrite(4, 0x10);   // map hi = $1000
    pic.hostWrite(4, 0x08);   // tc lo = 8
    pic.hostWrite(4, 0x00);   // tc hi
    pic.reqaW(true);
    pic.tick(200);
    bool dmaInOk = true;
    for (int i = 0; i < 8; i++)
        dmaInOk = dmaInOk && (pic.ramByte(0x1000 + i) == dmaPattern[i]);
    CHECK(dmaInOk, "DMA I/O→RAM must land the pattern at $1000");
    CHECK((pic.intFlags() & 0x02) != 0, "DMA1 completion flag must be set");
    setAddr(pic, 0xF020);
    CHECK((pic.hostRead(4) & 0x01) == 0, "DMAEN must auto-clear at tc=0");
    pic.reqaW(false);

    // 6b. DMA RAM→I/O on channel B: io reg 7, src $1000, 8 bytes.
    setAddr(pic, 0xF028);
    pic.hostWrite(4, 0x71);   // ctrl: io=7, DIR=RAM→I/O, EN
    pic.hostWrite(4, 0x00);
    pic.hostWrite(4, 0x10);
    pic.hostWrite(4, 0x08);
    pic.hostWrite(4, 0x00);
    pic.reqbW(true);
    pic.tick(200);
    bool dmaOutOk = periphWrites.size() == 8;
    for (size_t i = 0; dmaOutOk && i < periphWrites.size(); i++)
        dmaOutOk = periphWrites[i] == dmaPattern[i];
    CHECK(dmaOutOk, "DMA RAM→I/O must replay the pattern (%zu bytes)",
          periphWrites.size());
    CHECK((pic.intFlags() & 0x04) != 0, "DMA2 completion flag must be set");
    pic.reqbW(false);

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
}
