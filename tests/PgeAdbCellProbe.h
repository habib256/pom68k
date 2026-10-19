// POM68K — an HC05 program receives two bytes from an attached ADB device.
#pragma once
static void testPgeAdbCell() {
    std::vector<uint8_t> rom(512, 0x9D);
    const uint8_t code[] = {
        0xA6,0x81,0xB7,0x18,              // enable transmitter
        0xA6,0x3F,0xB7,0x1A,              // Talk mouse register 3
        0xB6,0x19,0xA4,0x80,0x27,0xFA,    // wait TDRE
        0xA6,0x49,0xB7,0x18,              // receive + completion, as the real ISR
        0xB6,0x19,0xA4,0x08,0x27,0xFA,    // wait RDRF
        0xB6,0x1A,0xB7,0x80,
        0xB6,0x19,0xB7,0x82,              // TC must still be clear
        0xB6,0x19,0xA4,0x08,0x27,0xFA,
        0xB6,0x1A,0xB7,0x81,0x20,0xFE
    };
    std::copy(std::begin(code), std::end(code), rom.begin());
    rom[510] = 0xFE; rom[511] = 0;
    M68hc05Pge cell;
    MscPb150Devices devices;
    devices.reset();
    cell.adbCommand = [&](uint8_t command, const std::vector<uint8_t>& data) {
        return devices.adbCommand(command, data);
    };
    cell.loadBootRom(rom); cell.reset();
    cell.run(20000);
    check(!cell.illegal() && cell.ramByte(0x40) == 0x63 && cell.ramByte(0x41) == 1,
          "PG&E executes Talk R3 and receives the attached trackball's two bytes");
    check(!(cell.adbsr() & 8), "PG&E RDRF clears when the firmware reads the data register");
    check(!(cell.ramByte(0x42) & 0x40) && (cell.adbsr() & 0x40),
          "PG&E completes the transaction after receiving both device bytes");
    M68hc05Pge unwired;
    unwired.loadBootRom(rom); unwired.reset(); unwired.run(20000);
    check(!unwired.ramByte(0x40) && !(unwired.adbsr() & 8),
          "unconnected Duo ADB cell never invents received bytes");
    check(devices.adbCommand(0x2F, {}).empty(),
          "JeDI's external bus does not invent an ADB keyboard beside its matrix");
    devices.adbCommand(0x3B, {0x0F, 0xFE});
    check(devices.adbCommand(0x3F, {}).empty() &&
          devices.adbCommand(0xFF, {}) == std::vector<uint8_t>({0x6F, 1}),
          "JeDI trackball answers at the address assigned by Listen R3");
    devices.mouse.mouseMove(8, 8);
    cell.adbServiceRequest = [&](uint8_t command) {
        return (command >> 4) != devices.mouse.mouseAddr() && devices.mouse.mousePending();
    };
    rom[5] = 0x2C; // poll another device while the trackball has motion
    cell.loadBootRom(rom); cell.reset(); cell.run(20000);
    check((cell.adbsr() & 0x30) == 0x30, "PG&E latches an SRQ event and condition from the trackball");
}
