// POM68K — JeDI panel, SWIM and ATA transactions on the real MSC decode.
#pragma once

static void testPb150Bus() {
    MscMemory mem(pom68k::defaultCoreConfig(), 8u << 20,
                  MscMemory::kCpuHz230, MscMemory::kIdPb150);
    MscCpu cpu(mem, jit::defaultResolvedConfig(), pom68k::defaultCoreConfig().cpu);
    mem.setCpu(&cpu);
    cpu.stall(100);
    const auto beforeMotor = cpu.machineClock();
    mem.internalDrive().setMotor(true);
    cpu.stall(100); cpu.flushTicks();
    check(cpu.machineClock() - beforeMotor == 100,
          "JeDI floppy gate enters boost 1 without a machine-clock jump");
    const auto duringMotor = cpu.machineClock();
    mem.internalDrive().setMotor(false);
    cpu.stall(100); cpu.flushTicks();
    check(cpu.machineClock() - duringMotor == 100,
          "JeDI floppy gate restores throughput without a machine-clock jump");
    check(mem.read8(0x50F20001) == 4 && mem.screenHeight() == 480 &&
          cpu.getFPUModel() == moira::FPUModel::NONE,
          "JeDI selects panel 4, 480 lines and no coprocessor");
    mem.write8(0x50F20004, 0x21);
    mem.write8(0x60000000 + 479 * 160, 0x1B);
    std::vector<uint32_t> pixels;
    mem.decodeScreen(pixels);
    check(pixels.size() == 640 * 480 && pixels[479 * 640] == 0xFFFFFF &&
          pixels[479 * 640 + 1] == 0xBFBFBF &&
          pixels[479 * 640 + 2] == 0x7F7F7F && pixels[479 * 640 + 3] == 0x3F3F3F,
          "JeDI decodes all 480 lines at four gray levels");
    const std::string path = (std::filesystem::temp_directory_path() /
        "pom68k-pb150-bus.img").string();
    std::vector<uint8_t> image(4096);
    for (size_t i = 0; i < image.size(); i++) image[i] = uint8_t(i);
    { std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(image.data()), image.size()); }
    check(mem.attachIde(path), "JeDI attaches its internal ATA drive");
    {
        uint8_t cdb[6] = {0x12, 0, 0, 0, 36, 0};
        std::vector<uint8_t> inquiry;
        check(mem.scsi().target(0) && !mem.scsiDisk().present() &&
              mem.scsi().target(0)->command(cdb, 6, inquiry, {}) == 0 && inquiry[0] == 3,
              "IDE-only JeDI exposes a media-free agent endpoint, not a SCSI boot disk");
    }
    constexpr uint32_t ata = 0x50F1A000;
    auto command = [&](uint8_t op) {
        mem.write8(ata + 24, 0x40);
        mem.write8(ata + 8, 1);
        mem.write8(ata + 12, 2);
        mem.write8(ata + 16, 0);
        mem.write8(ata + 20, 0);
        mem.write8(ata + 28, op);
    };
    command(0x20);
    check(mem.read16(ata) == 0x0001 && mem.read16(ata) == 0x0203,
          "JeDI ATA data lands in natural byte order on the 68k bus");
    std::vector<uint8_t> state;
    pom68k::save(mem, cpu, pom68k::SnapMachine::Pb150, state);
    const uint16_t next = mem.read16(ata);
    command(0x30);
    for (int i = 0; i < 256; i++) mem.write16(ata, 0xABCD);
    check(mem.ide().sectorsWritten == 1, "JeDI completes a sector write through the MSC bus");
    std::string error;
    check(pom68k::load(mem, cpu, pom68k::SnapMachine::Pb150,
                       state.data(), state.size(), error) && mem.read16(ata) == next,
          "JeDI state restores a pending ATA transfer at its exact word");
    command(0x20);
    check(mem.read16(ata) == 0x0001,
          "JeDI restore rewinds the ATA sector content after a later write");
    check(!pom68k::load(mem, cpu, pom68k::SnapMachine::Duo230,
                        state.data(), state.size(), error),
          "JeDI state cannot load as a GSC Duo");
    mem.ide().close();
    MscMemory duo(pom68k::defaultCoreConfig());
    check(!duo.attachIde(path) && duo.read8(ata + 28) == 0,
          "GSC Duo has no ATA decode or drive attachment");
    std::filesystem::remove(path);
    mem.read8(0x50F17A00); // Q6 on
    for (uint8_t value : {0x57, 0x17, 0x57, 0x57}) mem.write8(0x50F17E00, value);
    check(mem.swim().ism() && !duo.swim().ism(),
          "JeDI SWIM personality switch is reached only on its wired bus");
    mem.write8(0x50F16E00, 0x20);
    check(mem.swim().iwm().sel(), "JeDI SWIM HDSEL output reaches the drive");
    mem.write8(0x50F16C00, 0x20);
    check(!mem.swim().iwm().sel(), "JeDI SWIM HDSEL clear reaches both personalities");
}
