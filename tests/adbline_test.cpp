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

// Let the device finish transmitting whatever the previous command asked
// for and fall back to idle — the real host waits for the reply before the
// next Attention, and the receive machine only decodes from LST_IDLE.
static void settle(AdbLine& a) {
    a.setHostDrive(true);
    for (int i = 0; i < 20000 && a.busy(); i++) a.tick(64);
}

// Drive one ADB command byte (Attention + sync + 8 bits, MSB first + stop).
static void sendCommand(AdbLine& a, uint8_t cmd) {
    settle(a);
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

    // ── Extended Mouse Protocol (handler 4): the SECOND BUTTON ──────────
    // A one-button Apple mouse holds bit 7 of the second report byte at 1
    // forever; button 2 only exists once a driver switches the device to
    // handler 4 with a Listen R3.
    {
        AdbLine a; a.reset();
        CHECK(a.mouseHandlerId() == 1, "mouse resets to handler 1");

        a.mouseMove(1, 1); a.mouseButton(true, 1);   // right button down
        sendCommand(a, 0x3C);                        // Talk R0, still handler 1
        CHECK(a.dbgDatasize() == 2, "handler 1 R0 -> 2 bytes");
        CHECK((a.dbgBuffer(1) & 0x80) != 0, "handler 1 hides button 2 (bit7=1)");

        sendListen(a, 0x3B, 0x63, 0x04);             // Listen R3, activator 4
        CHECK(a.mouseHandlerId() == 4, "Listen R3 activator 4 -> handler 4");
        CHECK(a.mouseAddr() == 3, "handler switch leaves the address alone");

        a.mouseMove(1, 1);
        sendCommand(a, 0x3C);
        CHECK(a.dbgDatasize() == 2, "handler 4 R0 -> 2 bytes");
        CHECK((a.dbgBuffer(1) & 0x80) == 0, "handler 4 reports button 2 (bit7=0)");
        CHECK((a.dbgBuffer(0) & 0x80) != 0, "button 1 still up");

        a.mouseButton(false, 1);
        a.mouseMove(1, 1);
        sendCommand(a, 0x3C);
        CHECK((a.dbgBuffer(1) & 0x80) != 0, "button 2 release reported");

        // Register 1 is the extended identifier block: 'appl' + 300 dpi +
        // class 1 (mouse) + 2 buttons.
        sendCommand(a, 0x3D);                        // Talk R1
        CHECK(a.dbgDatasize() == 8, "handler 4 R1 -> 8-byte identifier");
        CHECK(a.dbgBuffer(0) == 'a' && a.dbgBuffer(3) == 'l', "R1 says 'appl'");
        CHECK(a.dbgBuffer(4) == 0x01 && a.dbgBuffer(5) == 0x2C, "R1 300 dpi");
        CHECK(a.dbgBuffer(7) == 0x02, "R1 declares 2 buttons");

        sendCommand(a, 0x3F);                        // Talk R3
        CHECK(a.dbgBuffer(1) == 0x04, "R3 reports the live handler ID");

        // SendReset returns the device to its power-on protocol.
        sendCommand(a, 0x00);
        CHECK(a.mouseHandlerId() == 1, "SendReset restores handler 1");
    }

    // --- A standard mouse has no register 1 (timeout, not a stale reply) ---
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x3D);                        // Talk R1, handler 1
        CHECK(a.dbgDatasize() == 0, "handler 1 R1 -> timeout");
    }

    // ── Keyboard handler IDs ────────────────────────────────────────────
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x2F);                        // Talk R3
        CHECK(a.dbgBuffer(1) == 0x01, "kbd resets to handler 1 (standard)");

        // A STANDARD keyboard must refuse the extended protocol — that
        // refusal is how a driver tells the two apart.
        sendListen(a, 0x2B, 0x62, 0x03);
        CHECK(a.keyboardHandlerId() == 1, "handler 1 refuses activator 3");

        sendListen(a, 0x2B, 0x62, 0x02);             // become an Extended II
        CHECK(a.keyboardHandlerId() == 2, "activator 2 -> Extended Keyboard II");
        sendListen(a, 0x2B, 0x62, 0x03);             // now 3 is accepted
        CHECK(a.keyboardHandlerId() == 3, "handler 2 accepts activator 3");
        sendCommand(a, 0x2F);
        CHECK(a.dbgBuffer(1) == 0x03, "R3 reports handler 3");

        // Under handler 3 the right-hand modifiers keep their own key
        // codes; under any other handler they fold onto the left ones.
        a.keyEvent(0x7B, true);                      // right Shift down
        sendCommand(a, 0x2C);
        CHECK(a.dbgBuffer(1) == 0x7B, "handler 3 reports right Shift as $7B");
        sendListen(a, 0x2B, 0x62, 0x01);             // back to standard
        CHECK(a.keyboardHandlerId() == 1, "activator 1 -> standard keyboard");
        a.keyEvent(0x7B, true);
        sendCommand(a, 0x2C);
        CHECK(a.dbgBuffer(1) == 0x38, "handler 1 folds right Shift onto $38");
    }

    // --- The R2 modifier bitmap sees the right-hand keys too ---
    {
        AdbLine a; a.reset();
        a.keyEvent(0x7B, true);                      // right Shift down
        sendCommand(a, 0x2E);                        // Talk R2
        CHECK((a.dbgBuffer(0) & 0x04) == 0, "right Shift clears the Shift bit");
        a.keyEvent(0x7B, false);
        sendCommand(a, 0x2E);
        CHECK((a.dbgBuffer(0) & 0x04) != 0, "right Shift release sets it back");
    }

    // ── Keyboard LEDs: Listen R2 latches them, Talk R2 reads them back ──
    {
        AdbLine a; a.reset();
        sendCommand(a, 0x2E);                        // Talk R2, untouched
        CHECK(a.dbgBuffer(1) == 0xFF, "virgin R2 second byte is $FF");
        CHECK(a.keyboardLeds() == 0x07, "virgin LEDs all dark (active low)");

        sendListen(a, 0x2A, 0x00, 0x05);             // Listen R2: LEDs %101
        CHECK(a.keyboardLeds() == 0x05, "Listen R2 latches the LED bits");
        sendCommand(a, 0x2E);
        CHECK(a.dbgBuffer(1) == 0xFD, "Talk R2 reports LEDs, keys still released");
        CHECK(a.dbgBuffer(0) == 0xFF, "Listen R2 leaves the modifier bitmap alone");

        sendCommand(a, 0x00);                        // SendReset
        CHECK(a.keyboardLeds() == 0x07, "SendReset darkens the LEDs");
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
