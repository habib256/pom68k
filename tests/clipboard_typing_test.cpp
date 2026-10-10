// POM68K — gate `clipboard_typing_test`: host text typed into the guest
// (src/TextTyping.h, MachineHost's TypeText/CancelTyping).
//
//   • planning — US and AZERTY keystrokes for letters, capitals, shifted
//     punctuation, digits, Return (LF, CR LF, CR), Tab and AZERTY's
//     unshifted accented keys; characters with no key (é on US, ü, €, an
//     emoji, malformed UTF-8) skipped, counted and listed once each, in
//     order; a text past kMaxTypedCharacters truncated and said so;
//   • scheduling — polled once per quantum, the typer emits at most one
//     transition per poll, each at least its delay after the previous one
//     in machine time, so a press and its release never share an instant;
//     the outcome does not depend on how often it is polled; cancel drops
//     the rest and releases the Shift it held;
//   • the host — on a real MachineHost the transitions are emitted before
//     quanta and journaled as ordinary `key` events at those clocks, the
//     TypeText request itself is not journaled, the progress atomic counts
//     down to zero, and a cancel mid-character leaves no key down.
//
// No ROM, no image.

#include "Cpu040.h"
#include "InputJournal.h"
#include "MachineHost.h"
#include "Q605Memory.h"
#include "TextTyping.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace pom68k;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// The key-down codes of a plan, Shift marked as 'S'.
std::string downs(const TypingPlan& plan) {
    std::string out;
    char buf[8];
    for (const KeyTransition& s : plan.steps) {
        if (!s.down) continue;
        if (s.code == guestkbd::kShift) { out += "S"; continue; }
        std::snprintf(buf, sizeof buf, "%02X ", s.code);
        out += buf;
    }
    return out;
}

struct FakeAudio {
    bool started() const { return false; }
    size_t buffered() const { return 0; }
    size_t targetBuffered() const { return 1; }
    void pushRaw(std::vector<float>&, int) {}
    void pushFrame(std::vector<float>&, int) {}
    void pushRawStereo(std::vector<float>&, int) {}
    void pushFrameStereo(std::vector<float>&, int) {}
};

// A quantum advances the machine clock by running the 68040 (on an empty
// map: what it executes is irrelevant, the clock is what typing reads).
struct ClockMachine : MachineHost<ClockMachine, Q605Memory, Cpu040, FakeAudio> {
    using Base = MachineHost<ClockMachine, Q605Memory, Cpu040, FakeAudio>;
    using Base::Base;
    static constexpr bool kStereo = false;
    int64_t frameCycles() const { return mem.cpuHz() / 60; }
    void emulateQuantum() { cpu.runCycles(frameCycles()); this->framesRun_++; }
    bool drainAudio() { return false; }
    void renderFrame(std::vector<uint32_t>& fb, int& w, int& h) {
        w = h = 1; fb.assign(1, 0);
    }
    void publishStatus() {}
};

} // namespace

int main() {
    // ── Planning ──────────────────────────────────────────────────────
    {
        const TypingPlan us = planTyping("Hi!\n\tok", GuestLayout::Us);
        check(downs(us) == "S04 22 S12 24 30 1F 28 " && us.characters == 7 && !us.skipped,
              "US: capital, letter, shifted digit, Return, Tab, letters");
        const TypingPlan fr = planTyping("aqzwm,é1àù", GuestLayout::FrenchAzerty);
        check(downs(fr) == "0C 00 0D 06 29 2E 13 S12 1D 27 " && fr.characters == 10,
              "AZERTY: swapped letters, M and comma moved, é, shifted 1, à, ù");
        const TypingPlan lines = planTyping("a\r\nb\rc\n", GuestLayout::Us);
        check(downs(lines) == "00 24 0B 24 08 24 ",
              "CR LF, CR and LF are each one Return");
        const TypingPlan odd = planTyping("éü€😀x\xC3(ü", GuestLayout::Us);
        check(odd.characters == 2 && odd.skipped == 6 &&
                  odd.unrepresentable ==
                      std::vector<std::string>{"é", "ü", "€", "😀", "\xEF\xBF\xBD"},
              "unrepresentable characters are skipped, counted, listed once each in order");
        check(planTyping("é", GuestLayout::FrenchAzerty).skipped == 0 &&
                  planTyping("é", GuestLayout::Us).skipped == 1,
              "the layout is explicit: é has a key on AZERTY only");
        const TypingPlan big = planTyping(std::string(kMaxTypedCharacters + 5, 'a'),
                                          GuestLayout::Us);
        check(big.truncated && big.characters == kMaxTypedCharacters,
              "a text past the bound is truncated and says so");
        bool ordered = true;
        for (std::size_t i = 0; i + 1 < us.steps.size(); ++i)
            ordered &= us.steps[i + 1].delayUs > 0;
        check(ordered && us.steps.front().delayUs == 0,
              "every transition after the first waits a nonzero delay");
    }

    // ── Scheduling in machine time ────────────────────────────────────
    {
        const long long hz = 25000000;
        const TypingPlan plan = planTyping("Ab", GuestLayout::Us);
        auto run = [&](long long step, std::vector<long long>& clocks) {
            TextTyper typer;
            typer.start(plan);
            std::vector<KeyTransition> got;
            for (long long now = 0; now < hz && (typer.active() || got.empty()); now += step)
                if (auto t = typer.poll(now, hz)) { got.push_back(*t); clocks.push_back(now); }
            return got;
        };
        std::vector<long long> fine, coarse;
        const auto a = run(hz / 1000, fine), b = run(hz / 60, coarse);
        bool same = a.size() == plan.steps.size() && b.size() == a.size();
        for (std::size_t i = 0; same && i < a.size(); ++i)
            same = a[i].code == b[i].code && a[i].down == b[i].down;
        check(same, "polled every ms or every frame, the same transitions in the same order");
        bool spaced = true;
        for (std::size_t i = 1; i < coarse.size(); ++i)
            spaced &= coarse[i] > coarse[i - 1] &&
                coarse[i] - coarse[i - 1] >= (long long)plan.steps[i].delayUs * hz / 1000000;
        check(spaced, "each transition at least its delay after the previous, never the same instant");

        TextTyper typer;
        typer.start(planTyping("A", GuestLayout::Us));
        long long now = 0;
        auto first = typer.poll(now, hz);                  // Shift down
        typer.cancel();
        std::vector<KeyTransition> after;
        for (int i = 0; i < 10; ++i, now += hz / 60)
            if (auto t = typer.poll(now, hz)) after.push_back(*t);
        check(first && first->code == guestkbd::kShift && first->down &&
                  after.size() == 1 && after[0].code == guestkbd::kShift && !after[0].down &&
                  !typer.active() && typer.charactersLeft() == 0,
              "cancel drops the rest and releases the Shift it held");
    }

    // ── On a MachineHost: emitted before quanta, journaled as keys ────
    {
        static Q605Memory mem(defaultCoreConfig(), 32u << 20);
        static Cpu040 cpu(mem, jit::defaultResolvedConfig(), defaultCoreConfig().cpu,
                          defaultCoreConfig().diagnostics);
        mem.setCpu(&cpu);
        FakeAudio audio;
        ClockMachine m(mem, cpu, audio);
        m.turbo.store(false);              // one quantum per tick
        m.state.kind = SnapMachine::Q605;
        m.requestRecordingStart("clipboard_typing_test.rec");
        m.requestTyping("Hi", GuestLayout::Us);
        m.stepTick();
        const bool started = m.typingLeft() == 2;
        int ticks = 0;
        while (m.typingLeft() && ticks < 400) { m.stepTick(); ++ticks; }
        check(started && m.typingLeft() == 0, "the progress counts down to zero");
        m.requestTyping("Zz", GuestLayout::Us);
        m.stepTick();                                    // Shift down, at most
        m.requestCancelTyping();
        for (int i = 0; i < 20; ++i) m.stepTick();
        m.requestRecordingStop();
        m.stepTick();
        m.stop();

        InputJournal j;
        std::string err;
        check(loadInputJournal("clipboard_typing_test.rec", j, err), "the journal reads back");
        std::vector<std::pair<int, int>> keys;
        bool onlyKeys = true, distinct = true;
        long long last = -1;
        for (const InputEvent& e : j.events) {
            onlyKeys &= e.type == int(InputEventType::Key);
            distinct &= e.clk > last;
            last = e.clk;
            keys.push_back({e.a, e.b});
        }
        const std::vector<std::pair<int, int>> want = {
            {0x38, 1}, {0x04, 1}, {0x04, 0}, {0x38, 0}, {0x22, 1}, {0x22, 0},
            {0x38, 1}, {0x38, 0}};
        check(onlyKeys, "only `key` events are journaled — the TypeText request is not");
        check(keys == want, "H, i, then Shift down and its release on cancel, in order");
        check(distinct, "every journaled transition at its own machine clock");
        std::remove("clipboard_typing_test.rec");
        std::remove("clipboard_typing_test.rec.pomss");

        // The other way: the guest's TEXT scrap through the debugger's
        // logical read, on the machine thread between quanta. Paused, so
        // no instruction runs over the bytes written here.
        ClockMachine host(mem, cpu, audio);
        host.running.store(false);
        (void)mem.read8(0x40000000u);   // the ROM window clears the boot overlay
        auto put = [&](uint32_t a, std::vector<uint8_t> bytes) {
            for (uint8_t b : bytes) mem.write8(a++, b);
        };
        put(0x0960, {0, 0, 0, 13, 0, 0, 0x20, 0, 0, 3, 0, 1});   // size, handle, count, state
        put(0x0CB2, {1});
        put(0x2000, {0, 1, 0, 0});                              // master pointer → $10000
        put(0x10000, {'T', 'E', 'X', 'T', 0, 0, 0, 5, 'p', 0x8E, 'r', 'e', '\r'});
        const unsigned before = host.guestScrap().second;
        host.requestScrapRead();
        host.stepTick();
        const auto [scrap, reads] = host.guestScrap();
        check(reads == before + 1 && scrap.ok() && scrap.utf8 == "pére\n" && scrap.count == 3,
              "MachineHost reads the guest's TEXT scrap between quanta, MacRoman converted");
        if (!scrap.ok()) std::printf("      status %d: %s (reads %u→%u)\n", int(scrap.status),
                                     scrap.reason.c_str(), before, reads);
        host.stop();
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
