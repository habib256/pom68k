// POM68K — PIC1654S core unit test (instruction-set sanity).
// Hand-assembled micro-programs exercise the ALU, flags, skips, stack and
// the RTCC timer against known results. Gate for the LLE ADB core.

#include "Pic1654s.h"
#include <cstdio>
#include <vector>
#include <cstdint>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); failures++; } } while (0)

// Assemble a 512-word ROM: code at 0x000, a GOTO to 0x000 at the reset
// vector (top word 0x1FF) so reset lands on our code.
static std::vector<uint8_t> rom(const std::vector<uint16_t>& code) {
    std::vector<uint16_t> words(512, 0x000);          // fill with NOP
    for (size_t i = 0; i < code.size(); i++) words[i] = code[i];
    words[0x1FF] = uint16_t(0xA00 | 0x000);           // GOTO 0x000
    std::vector<uint8_t> bytes(words.size() * 2);
    for (size_t i = 0; i < words.size(); i++) {        // little-endian
        bytes[i * 2] = uint8_t(words[i] & 0xFF);
        bytes[i * 2 + 1] = uint8_t(words[i] >> 8);
    }
    return bytes;
}

// opcode helpers
static uint16_t MOVLW(uint8_t k){ return uint16_t(0xC00 | k); }
static uint16_t MOVWF(uint8_t f){ return uint16_t(0x020 | (f & 0x1F)); }
static uint16_t ADDWF(uint8_t f,int d){ return uint16_t(0x1C0 | (d?0x20:0) | (f&0x1F)); }
static uint16_t SUBWF(uint8_t f,int d){ return uint16_t(0x080 | (d?0x20:0) | (f&0x1F)); }
static uint16_t INCF (uint8_t f,int d){ return uint16_t(0x280 | (d?0x20:0) | (f&0x1F)); }
static uint16_t BSF  (uint8_t f,int b){ return uint16_t(0x500 | (b<<5) | (f&0x1F)); }
static uint16_t BTFSS(uint8_t f,int b){ return uint16_t(0x700 | (b<<5) | (f&0x1F)); }
static uint16_t SWAPF(uint8_t f,int d){ return uint16_t(0x380 | (d?0x20:0) | (f&0x1F)); }
static uint16_t GOTO (uint16_t a){ return uint16_t(0xA00 | (a & 0x1FF)); }
static uint16_t CALL (uint8_t k){ return uint16_t(0x900 | k); }
static uint16_t RETLW(uint8_t k){ return uint16_t(0x800 | k); }

int main() {
    // --- Test 1: MOVLW/MOVWF/ADDWF, result and carry ---
    {
        Pic1654s p;
        auto img = rom({
            MOVLW(0x05), MOVWF(0x10),      // reg10 = 5
            MOVLW(0x03), ADDWF(0x10, 1),   // reg10 = 5+3 = 8
            MOVLW(0xFF), ADDWF(0x10, 1),   // reg10 = 8+0xFF = 7, carry set
            GOTO(0x006),                    // spin
        });
        p.loadRom(img.data(), img.size());
        p.run(200);
        CHECK(p.reg(0x10) == 0x07, "ADDWF result with wrap");
        CHECK((p.status() & 0x01) != 0, "ADDWF carry set");
    }

    // --- Test 2: SUBWF borrow/no-borrow (C = no borrow) ---
    {
        Pic1654s p;
        auto img = rom({
            MOVLW(0x10), MOVWF(0x11),      // reg11 = 0x10
            MOVLW(0x05), SUBWF(0x11, 1),   // reg11 = 0x10 - 5 = 0x0B, C=1 (no borrow)
            GOTO(0x004),
        });
        p.loadRom(img.data(), img.size());
        p.run(100);
        CHECK(p.reg(0x11) == 0x0B, "SUBWF result");
        CHECK((p.status() & 0x01) != 0, "SUBWF C=1 no borrow");
    }
    {
        Pic1654s p;
        auto img = rom({
            MOVLW(0x05), MOVWF(0x11),
            MOVLW(0x10), SUBWF(0x11, 1),   // 5 - 0x10 → borrow, C=0
            GOTO(0x004),
        });
        p.loadRom(img.data(), img.size());
        p.run(100);
        CHECK((p.status() & 0x01) == 0, "SUBWF C=0 borrow");
    }

    // --- Test 3: bit set + skip-if-set (BTFSS) branch flow ---
    {
        Pic1654s p;
        auto img = rom({
            BSF(0x12, 3),                  // reg12 bit3 = 1
            BTFSS(0x12, 3),                // set → skip next
            MOVLW(0xAA),                   // (skipped)
            MOVLW(0x55), MOVWF(0x13),      // reg13 = 0x55
            GOTO(0x005),
        });
        p.loadRom(img.data(), img.size());
        p.run(100);
        CHECK(p.reg(0x13) == 0x55, "BTFSS skipped the AA load");
    }

    // --- Test 4: CALL / RETLW, 2-level stack ---
    {
        Pic1654s p;
        auto img = rom({
            CALL(0x080),                   // call sub at 0x080
            MOVWF(0x14),                   // reg14 = W returned
            GOTO(0x002),
        });
        // place subroutine at 0x080
        std::vector<uint16_t> words(512, 0x000);
        words[0] = CALL(0x080); words[1] = MOVWF(0x14); words[2] = GOTO(0x002);
        words[0x080] = MOVLW(0x42); words[0x081] = RETLW(0x99);
        words[0x1FF] = GOTO(0x000);
        std::vector<uint8_t> bytes(words.size() * 2);
        for (size_t i = 0; i < words.size(); i++) {
            bytes[i * 2] = uint8_t(words[i] & 0xFF);
            bytes[i * 2 + 1] = uint8_t(words[i] >> 8);
        }
        p.loadRom(bytes.data(), bytes.size());
        p.run(100);
        CHECK(p.reg(0x14) == 0x99, "CALL/RETLW returned literal");
    }

    // --- Test 5: SWAPF nibble swap ---
    {
        Pic1654s p;
        auto img = rom({
            MOVLW(0x3C), MOVWF(0x15),
            SWAPF(0x15, 1),                // 0x3C -> 0xC3
            GOTO(0x003),
        });
        p.loadRom(img.data(), img.size());
        p.run(100);
        CHECK(p.reg(0x15) == 0xC3, "SWAPF");
    }

    // --- Test 6: RTCC falling edges increment TMR0 ---
    {
        Pic1654s p;
        auto img = rom({ GOTO(0x000) });
        p.loadRom(img.data(), img.size());
        p.run(10);
        uint8_t before = p.reg(0x01);
        p.setRtcc(true); p.setRtcc(false);   // one falling edge
        p.setRtcc(true); p.setRtcc(false);   // another
        CHECK(uint8_t(p.reg(0x01) - before) == 2, "RTCC falling edges -> TMR0 +2");
    }

    if (failures == 0) std::printf("PASS: pic1654s core (%d checks)\n", 6);
    return failures ? 1 : 0;
}
