// POM68K — the input journal end to end: format round-trip, and REPLAY
// DETERMINISM on a live machine.
//
// The property this gate exists for is the second one: from one snapshot,
// replaying the same journal twice must produce byte-identical machines
// (sav::hash over the serialized state), and replaying it at all must
// produce a DIFFERENT machine than not replaying it — otherwise "identical"
// would be vacuous, the trap `savestate_v8_test` documents for its own
// determinism check. Same fixture as that gate: a synthetic 512 KB LC II
// ROM whose stub increments a RAM counter forever, so the machine keeps
// evolving without any ROM/disk asset.
//
// The journal ↔ MachineHost::Cmd enum pairing is pinned by name in
// machinehost_test, which records through the real applyCmds() tap.

#include "Cpu030.h"
#include "InputJournal.h"
#include "InputReplay.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
#include "V8Memory.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// Same synthetic ROM as savestate_v8_test: reset PC → $A00010, ADDQ.W/BRA.S
// counter loop, stored checksum over bytes 4…end.
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(V8Memory::kRomSize, 0);
    auto w16 = [&](uint32_t a, uint16_t v) {
        rom[a] = uint8_t(v >> 8); rom[a + 1] = uint8_t(v);
    };
    auto w32 = [&](uint32_t a, uint32_t v) {
        w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v));
    };
    w32(4, 0x00A00010);
    w16(0x10, 0x5279); w32(0x12, 0x00002000);
    w16(0x16, 0x60F8);
    for (uint32_t i = 0x20; i < rom.size(); i++) rom[i] = uint8_t(i * 7);
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom.size(); i += 2)
        sum += uint32_t(rom[i] << 8 | rom[i + 1]);
    w32(0, sum);
    return rom;
}

using Blob = std::vector<uint8_t>;

struct Machine {
    V8Memory mem{pom68k::defaultCoreConfig(), 0xA00000};
    Cpu030   cpu{mem, jit::defaultResolvedConfig(),
                 pom68k::defaultCoreConfig().cpu};

    explicit Machine(const std::vector<uint8_t>& rom) {
        mem.loadRom(rom);
        mem.setCpu(&cpu);
        cpu.hardReset();
    }
    Blob save() {
        Blob b;
        pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, b);
        return b;
    }
    bool load(const Blob& b, std::string& err) {
        return pom68k::load(mem, cpu, pom68k::SnapMachine::LcII,
                            b.data(), b.size(), err);
    }
};
}  // namespace

int main() {
    std::printf("input_journal_test — journal format + replay determinism\n");

    // ── 1. Format round-trip through a real file ────────────────────────
    const std::string jpath = "input_journal_test.journal";
    {
        pom68k::InputJournalWriter w;
        check(w.open(jpath), "writer: opens its file");
        w.note("profile", "lcii");
        w.note("cpuhz", "15667200");
        w.note("media", "hdv/un chemin avec espaces.vhd");
        w.begin(1000);
        using ET = pom68k::InputEventType;
        w.event(1500, int(ET::MouseMove), 3, -2, {});
        w.event(1500, int(ET::MouseButton), 0, 1, {});
        w.event(2000, int(ET::Key), 0x37, 1, {});
        w.event(2500, int(ET::InsertFloppy), 0, 0,
                "disks35/un autre chemin.dsk");
        w.event(3000, int(ET::Sense), 6, 0, {});
        w.finish(9999);

        pom68k::InputJournal j;
        std::string err;
        check(pom68k::loadInputJournal(jpath, j, err),
              ("reader: accepts the writer's file " + err).c_str());
        check(j.startClk == 1000 && j.endClk == 9999 && j.complete,
              "reader: start/end clocks survive the round trip");
        check(j.note("profile") == "lcii" && j.note("cpuhz") == "15667200",
              "reader: notes survive the round trip");
        check(j.note("media") == "hdv/un chemin avec espaces.vhd",
              "reader: a note value keeps its spaces");
        check(j.events.size() == 5, "reader: all five events are there");
        check(j.events[0].clk == 1500 && j.events[0].a == 3 &&
              j.events[0].b == -2 &&
              j.events[0].type == int(ET::MouseMove),
              "reader: a mouse move survives, negative delta included");
        check(j.events[3].path == "disks35/un autre chemin.dsk",
              "reader: a media path keeps its spaces");

        // A crashed session has no `end`: still usable, marked incomplete.
        pom68k::InputJournalWriter w2;
        check(w2.open(jpath + ".crash"), "writer: opens the crash fixture");
        w2.note("profile", "lcii");
        w2.begin(50);
        w2.event(75, int(ET::Key), 1, 1, {});
        // no finish(): destructor closes the stream mid-session
    }
    {
        pom68k::InputJournal j;
        std::string err;
        check(pom68k::loadInputJournal(jpath + ".crash", j, err),
              "reader: a journal without `end` is still readable");
        check(!j.complete && j.endClk == 75,
              "reader: it is marked incomplete, end = last event");

        // Refusals must be refusals, with a reason.
        std::FILE* f = std::fopen((jpath + ".bad").c_str(), "wb");
        std::fprintf(f, "# POM68K input journal v1\nstart 100\n"
                        "ev 200 key 1 1\nev 150 key 1 0\n");
        std::fclose(f);
        check(!pom68k::loadInputJournal(jpath + ".bad", j, err) &&
              !err.empty(),
              "reader: a backwards clock is refused with a reason");
        f = std::fopen((jpath + ".bad").c_str(), "wb");
        std::fprintf(f, "not a journal\n");
        std::fclose(f);
        check(!pom68k::loadInputJournal(jpath + ".bad", j, err),
              "reader: a foreign file is refused");
    }

    // ── 2. Replay determinism from one snapshot ─────────────────────────
    const auto rom = makeRom();
    Blob start;
    long long c0 = 0;
    {
        Machine a(rom);
        a.cpu.runCycles(200000);        // clear the overlay, spin the loop
        check(!a.mem.overlay(), "setup: the stub cleared the boot overlay");
        start = a.save();
        c0 = a.cpu.machineClock();
    }
    const std::string spath = "input_journal_test.session";
    {
        using ET = pom68k::InputEventType;
        pom68k::InputJournalWriter w;
        check(w.open(spath), "session: journal written");
        w.begin(c0);
        w.event(c0 +  5000, int(ET::MouseMove), 4, 3, {});
        w.event(c0 + 10000, int(ET::MouseButton), 0, 1, {});
        w.event(c0 + 10000, int(ET::MouseMove), -2, 1, {});
        w.event(c0 + 22000, int(ET::MouseButton), 0, 0, {});
        w.event(c0 + 40000, int(ET::Key), 0x00, 1, {});
        w.event(c0 + 52000, int(ET::Key), 0x00, 0, {});
        w.finish(c0 + 150000);
    }
    pom68k::InputJournal session;
    std::string err;
    check(pom68k::loadInputJournal(spath, session, err),
          "session: journal loads");

    auto replayHash = [&](bool withEvents, Blob& out) -> bool {
        Machine m(rom);
        std::string e;
        if (!m.load(start, e)) return false;
        if (withEvents) {
            if (!replayJournal(m.mem, m.cpu, session, [&] {
                    m.cpu.runCycles(1000);
                    return m.cpu.machineClock();
                }))
                return false;
        } else {
            while (m.cpu.machineClock() < session.endClk)
                m.cpu.runCycles(1000);
        }
        out = m.save();
        return true;
    };

    Blob r1, r2, quiet;
    check(replayHash(true, r1), "replay: first run reaches the end clock");
    check(replayHash(true, r2), "replay: second run reaches the end clock");
    check(sav::hash(r1) == sav::hash(r2) && r1 == r2,
          "replay: two replays of one journal are byte-identical");
    check(replayHash(false, quiet), "replay: the no-input arm runs too");
    check(sav::hash(quiet) != sav::hash(r1),
          "replay: the journal's input visibly changes the machine");
    if (r1 != r2) {
        size_t i = 0;
        const size_t n = std::min(r1.size(), r2.size());
        while (i < n && r1[i] == r2[i]) i++;
        std::printf("    first divergence at byte %zu of %zu\n", i, n);
    }

    // ── 3. A mid-session restore stops a replay ─────────────────────────
    {
        using ET = pom68k::InputEventType;
        pom68k::InputJournalWriter w;
        check(w.open(spath + ".restore"), "restore fixture written");
        w.begin(c0);
        w.event(c0 + 5000, int(ET::StateRestore), 0, 0, {});
        w.finish(c0 + 50000);
        pom68k::InputJournal jr;
        check(pom68k::loadInputJournal(spath + ".restore", jr, err),
              "restore fixture loads");
        Machine m(rom);
        std::string e;
        check(m.load(start, e), "restore fixture: snapshot restores");
        check(!replayJournal(m.mem, m.cpu, jr, [&] {
                  m.cpu.runCycles(1000);
                  return m.cpu.machineClock();
              }),
              "replay: refuses to cross a mid-session state restore");
    }

    if (gFails) {
        std::printf("input_journal_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("input_journal_test: OK\n");
    return 0;
}
