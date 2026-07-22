// POM68K — bit-serial ADB device model (AdbLine) unit test.
// Drives full ADB command frames on the wired line exactly as the PIC1654S
// transceiver would, and checks the keyboard/mouse decode + response.

#include "AdbLine.h"
#include <cstdio>
#include <cstdint>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); failures++; } } while (0)

// Cycle durations mirroring AdbLine's internal (PIC-calibrated) constants:
// bit "1" high >= T_BIT(782), bit "0" high < 782; attention low >= T_ATTEN(6000).
static constexpr int64_t kShort = 544, kLong = 1020;
static constexpr int64_t kAtten = 9000, kSync = 600, kGap = 544, kStopHigh = 600;

// Drive one ADB command byte (Attention + sync + 8 bits, MSB first + stop).
static void sendCommand(AdbLine& a, uint8_t cmd) {
    a.setHostDrive(false); a.tick(kAtten);        // attention: long low
    a.setHostDrive(true);  a.tick(kSync);         // rise (attn) + sync high
    a.setHostDrive(false);                        // sync fall -> BIT0
    for (int i = 7; i >= 0; i--) {
        bool bit = (cmd >> i) & 1;
        a.tick(kGap);                             // low gap
        a.setHostDrive(true);                     // rise
        a.tick(bit ? kLong : kShort);             // high duration encodes bit
        a.setHostDrive(false);                    // fall -> sample this bit
    }
    a.tick(kGap);
    a.setHostDrive(true);  a.tick(kStopHigh);     // stop bit (rise) -> adbTalk
}

// Drive a Listen command byte then two data bytes (for R3 reassignment).
static void sendListen(AdbLine& a, uint8_t cmd, uint8_t d0, uint8_t d1) {
    sendCommand(a, cmd);
    // After the command, the device waits T1t then a start bit, then 2 bytes.
    a.tick(3000);                                 // pass T1t
    a.setHostDrive(false); a.tick(kGap);
    a.setHostDrive(true);  a.tick(kSync);         // start bit
    a.setHostDrive(false);
    uint8_t bytes[2] = { d0, d1 };
    for (int n = 0; n < 2; n++) {
        for (int i = 7; i >= 0; i--) {
            bool bit = (bytes[n] >> i) & 1;
            a.tick(kGap); a.setHostDrive(true);
            a.tick(bit ? kLong : kShort); a.setHostDrive(false);
        }
    }
    // Stop bit: hold low longer than Tsync so the even-byte machine ends the
    // listen instead of reading another bit, then release.
    a.tick(kSync + kGap); a.setHostDrive(true); a.tick(kStopHigh);
}

int main() {
    // --- Talk R3 to the mouse (addr 3): identity response ---
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x3F);                     // addr3, talk(11), reg3
        CHECK(a.dbgCommand() == 0x3F, "mouse R3 command decoded");
        CHECK(a.dbgDatasize() == 2, "mouse R3 datasize 2");
        CHECK(a.dbgBuffer(1) == 0x01, "mouse R3 handler byte");
    }

    // --- Talk R0 to the mouse after motion: 2 bytes with the delta ---
    {
        AdbLine a; a.reset();
        a.mouseMove(5, 3); a.mouseButton(true);
        sendCommand(a, 0x3C);                      // addr3, talk, reg0
        CHECK(a.dbgDatasize() == 2, "mouse R0 moved -> datasize 2");
        CHECK((a.dbgBuffer(0) & 0x7F) == 3, "mouse R0 dy=3");
        CHECK((a.dbgBuffer(1) & 0x7F) == 5, "mouse R0 dx=5");
        CHECK((a.dbgBuffer(0) & 0x80) == 0, "mouse R0 button down (bit7=0)");
    }

    // --- Talk R0 to the mouse with no motion: timeout (datasize 0) ---
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x3C);
        CHECK(a.dbgDatasize() == 0, "mouse R0 idle -> timeout");
    }

    // --- Talk R0 to the keyboard with no keys: timeout ---
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x2C);                      // addr2, talk, reg0
        CHECK(a.dbgDatasize() == 0, "kbd R0 idle -> timeout");
    }

    // --- Talk R0 keyboard after a keypress: 2 bytes ---
    {
        AdbLine a; a.reset();
        a.keyEvent(0x24, true);                    // Return down
        sendCommand(a, 0x2C);
        CHECK(a.dbgDatasize() == 2, "kbd R0 key -> datasize 2");
        CHECK(a.dbgBuffer(1) == 0x24, "kbd R0 keycode Return down");
    }

    // --- Talk to an ABSENT address: timeout, back to idle (no phantom) ---
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x4C);                      // addr4 (no device), talk, reg0
        CHECK(a.dbgDatasize() == 0, "absent addr -> timeout");
        CHECK(!a.dbgTransmitting(), "absent addr -> not transmitting");
    }

    // --- Listen R3 relocates the mouse 3 -> 8, then it answers at 8 ---
    {
        AdbLine a; a.reset();
        // Listen R3 to addr 3: data byte0 = new addr 8 (+flags), byte1 = 0xFE (move).
        sendListen(a, 0x3B, 0x08, 0xFE);          // cmd 0x3B: addr3, listen(10), reg3
        CHECK(a.mouseAddr() == 8, "mouse relocated to addr 8");
        sendCommand(a, 0x8F);                      // addr8, talk, reg3
        CHECK(a.dbgDatasize() == 2, "relocated mouse answers R3 at addr 8");
    }

    // --- The device actually transmits after a Talk-with-data ---
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x3F);                      // mouse R3 -> has data
        bool sawLow = false;
        // Release the line and run the send machine; expect line activity.
        a.setHostDrive(true);
        for (int i = 0; i < 400; i++) { a.tick(kShort / 4); if (!a.line()) sawLow = true; }
        CHECK(sawLow, "device drives the line low while transmitting");
    }

    if (failures == 0) std::printf("PASS: adbline device model (all checks)\n");
    return failures ? 1 : 0;
}
