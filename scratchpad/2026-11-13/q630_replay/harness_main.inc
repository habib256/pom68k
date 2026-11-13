
// TEMP HARNESS (not for the tree): snapshot / replay of phase 1.
#include <unistd.h>
static long total_ = 0;
int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    const std::string dir = argc > 2 ? argv[2] : ".";
    const int fromStep = argc > 3 ? std::atoi(argv[3]) : -1;
    const int nSteps = argc > 4 ? std::atoi(argv[4]) : 1000;
    const long every = std::getenv("EVERY") ? std::atol(std::getenv("EVERY")) : 300;
    const bool doHash = !std::getenv("NOHASH");
    std::string romPath = testasset::findAny({
        "roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM" });
    std::string diskPath = testasset::findAny({ "hdv/MacOS-8.1-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) { std::printf("SKIP\n"); return 2; }
    const std::string idePath = "q630_ide_boot.img";
    if (mode == "snap") {
        std::ofstream f(idePath, std::ios::binary);
        f.seekp(std::streamoff(330) * 1024 * 1024 - 1);
        f.put('\0');
    } else {
        std::remove(idePath.c_str()); std::remove((idePath + ".pomundo").c_str());
        char cmd[1024];
        std::snprintf(cmd, sizeof cmd, "cp -c %s/s%d.img %s && (cp -c %s/s%d.img.pomundo %s.pomundo 2>/dev/null || true)",
                      dir.c_str(), fromStep, idePath.c_str(), dir.c_str(), fromStep, idePath.c_str());
        if (std::system(cmd)) return 3;
    }
    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    pom68k::CoreConfig core;
    Q630Memory mem(core, 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath) || !mem.attachIde(idePath, true)) return 1;
    jit::ResolvedConfig jc = jit::defaultResolvedConfig();
    if (const char* e = std::getenv("ENG")) {
        if (!std::strcmp(e, "interp")) { jc.engine = jit::EngineKind::Interp; jc.engineExplicit = true; }
        else { jc.backend = e; }
    }
    if (const char* e = std::getenv("DENY")) {
        unsigned a = 0, b = 0; std::sscanf(e, "%x:%x", &a, &b); jc.denyFrom = a; jc.denyTo = b;
    }
    Q630Cpu cpu(mem, jc, core.cpu);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);
    int stuck = 0;
    auto runFrames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) {
            cpu.runCycles(kFrameCycles);
            ++total_;
            const uint32_t pc = cpu.getPC0();
            if (pc >= 0x40891800 && pc < 0x408919AE) stuck++;
            if (total_ % every == 0) {
                uint64_t h = 1469598103934665603ull;
                if (doHash) for (uint32_t a = 0; a < (32u << 20); a += 8) {
                    uint64_t w = 0;
                    for (int i = 0; i < 8; i++) w = w << 8 | mem.peek8(a + i);
                    h = (h ^ w) * 1099511628211ull;
                }
                std::printf("TMPHASH p1 f=%ld clk=%lld pc=%08X ram=%016llx ata=%ld/%ld stuck=%d\n", total_,
                            (long long)cpu.getClock(), pc, (unsigned long long)h,
                            (long)mem.ide().commands, (long)mem.ide().sectorsWritten, stuck);
            }
        }
    };
    auto clickAt = [&](int tx, int ty) {
        for (int it = 0; it < 900; it++) {
            const int px = int16_t(mem.peek8(0x832) << 8 | mem.peek8(0x833));
            const int py = int16_t(mem.peek8(0x830) << 8 | mem.peek8(0x831));
            const int dx = tx - px, dy = ty - py;
            if (!dx && !dy) break;
            auto step = [](int d) {
                int s = d / 2; if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                return std::max(-8, std::min(8, s));
            };
            mem.mouseMove(step(dx), step(dy));
            runFrames(2);
        }
        runFrames(10);
        mem.mouseButton(true);  runFrames(5);
        mem.mouseButton(false); runFrames(5);
    };
    auto open = [&] {
        runFrames(30);
        mem.keyEvent(0x37, true);  runFrames(4);
        mem.keyEvent(0x1F, true);  runFrames(4);
        mem.keyEvent(0x1F, false); runFrames(4);
        mem.keyEvent(0x37, false); runFrames(4);
    };
    auto chunks = [&](long n) { return [&, n] { runFrames(n); }; };
    std::vector<std::function<void()>> steps;
    steps.push_back(chunks(16000));
    steps.push_back([&] { clickAt(364, 230); });
    steps.push_back(chunks(600));
    steps.push_back([&] { clickAt(590, 50); clickAt(590, 50); });
    steps.push_back(chunks(900));
    steps.push_back([&] { clickAt(63, 305); open(); });
    steps.push_back(chunks(900));
    steps.push_back([&] { clickAt(383, 233); open(); });
    for (int i = 0; i < 10; i++) steps.push_back(chunks(300));
    steps.push_back([&] { clickAt(72, 107); });
    steps.push_back(chunks(300));
    steps.push_back([&] { clickAt(256, 251); });
    for (int i = 0; i < 3; i++) steps.push_back(chunks(300));
    steps.push_back([&] { clickAt(278, 249); });
    for (int i = 0; i < 40; i++) steps.push_back(chunks(300));

    int first = 0;
    if (mode == "replay") {
        std::ifstream sf(dir + "/s" + std::to_string(fromStep) + ".state", std::ios::binary);
        std::vector<uint8_t> st((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
        std::ifstream tf(dir + "/s" + std::to_string(fromStep) + ".frame");
        tf >> total_;
        std::string err;
        if (!pom68k::load(mem, cpu, pom68k::SnapMachine::Q630, st.data(), st.size(), err)) {
            std::printf("load failed: %s\n", err.c_str()); return 4;
        }
        first = fromStep + 1;
    }
    for (int k = first; k < int(steps.size()) && k < first + nSteps; k++) {
        steps[size_t(k)]();
        if (mode == "snap") {
            std::vector<uint8_t> st;
            pom68k::save(mem, cpu, pom68k::SnapMachine::Q630, st);
            const std::string b = dir + "/s" + std::to_string(k);
            std::ofstream(b + ".state", std::ios::binary).write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
            std::ofstream(b + ".frame") << total_ << "\n";
            char cmd[1024];
            std::snprintf(cmd, sizeof cmd, "cp -c %s %s.img && (cp -c %s.pomundo %s.img.pomundo 2>/dev/null || true)",
                          idePath.c_str(), b.c_str(), idePath.c_str(), b.c_str());
            std::system(cmd);
            std::printf("SNAP step %d frame %ld\n", k, total_);
        }
    }
    std::printf("END f=%ld clk=%lld pc=%08X ata=%ld/%ld stuck=%d\n", total_, (long long)cpu.getClock(),
                cpu.getPC0(), (long)mem.ide().commands, (long)mem.ide().sectorsWritten, stuck);
    std::remove(idePath.c_str()); std::remove((idePath + ".pomundo").c_str());
    return 0;
}
