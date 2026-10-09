// Independent magnetic-edge oracle; no controller serializer used to derive it.
#pragma once
#include <algorithm>

void bitWriteCases() {
    constexpr int64_t u = 2 * FluxPll::kSubCell; // C7M cycle in flux ticks
    auto configure = [](Iwm& i, SonyDrive& d, int mode, bool doubledTick, bool doubledChip) {
        d.reset(); d.insertImage(std::vector<uint8_t>(SonyDrive::kSize800K, 0));
        d.setMotor(true); i.reset(); i.attachDrive(&d, nullptr);
        i.setTickHz(doubledTick ? 15667200 : 7833600);
        i.setChipHz(doubledChip ? 15667200 : 7833600);
        i.read(kQ6On); i.write(kQ7On, uint8_t(mode)); i.read(kQ7Off);
        i.read(kEnableOn); i.write(kQ7On, 0xA5);
    };
    auto expected = [](const std::vector<int64_t>& before, int64_t span,
                       const std::vector<int64_t>& edges) {
        std::vector<int64_t> out;
        for (auto t : before) if (t >= span) out.push_back(t);
        out.insert(out.end(), edges.begin(), edges.end());
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    };
    for (int board = 0; board < 3; ++board) {
        SonyDrive d; Iwm i;
        configure(i, d, board ? 0x17 : 0x1F, board == 2, board != 0);
        const auto before = d.debugFlux();
        const int scale = board == 2 ? 2 : 1;
        i.tick(6 * scale);
        check(bool(i.read(kQ6Off) & 0x80) == (board != 0),
              "seven-chip-clock load deadline follows board wiring");
        i.tick(124 * scale);
        i.read(kQ7Off);
        check(d.debugFlux() == expected(before, 130 * u, {8*u, 40*u, 88*u, 120*u}),
              "Plus/SE/II serialize A5 at physical cell midpoints");
    }
    {
        SonyDrive d; Iwm i; configure(i, d, 0x1F, false, false);
        const auto before = d.debugFlux();
        i.tick(20); i.read(kQ7Off);
        check(d.debugFlux() == expected(before, 20*u, {8*u}),
              "write exit preserves a partial byte and erases only elapsed arc");
    }
    {
        SonyDrive d; Iwm i; configure(i, d, 0x03, false, false);
        const auto before = d.debugFlux();
        i.tick(230); i.read(kQ7Off);
        check(d.debugFlux() == expected(before, 230*u, {14*u, 70*u, 154*u, 210*u}),
              "slow mode uses 28 chip clocks per written cell");
    }
    {
        SonyDrive d; Iwm i; configure(i, d, 0x1F, false, false);
        d.setMotor(false); const auto before = d.debugFlux();
        i.tick(130); i.read(kQ7Off);
        check(d.debugFlux() == before && !d.dirty(), "motor-off serializer leaves the medium untouched");
    }
    {
        SonyDrive a, b; Iwm ia, ib;
        configure(ia, a, 0x1F, false, false); configure(ib, b, 0x1F, false, false);
        ia.tick(1000); for (int n = 0; n < 1000; ++n) ib.tick(1);
        check(a.debugFlux() == b.debugFlux() && !(ia.read(kQ6Off) & 0x40),
              "underrun write splice is independent of scheduler batch size");
    }
    {
        SonyDrive d; Iwm i; configure(i, d, 0x1D, false, false);
        const auto before = d.debugFlux();
        i.tick(16); i.write(kQ6On, 0x80); i.tick(16); i.read(kQ7Off);
        check(d.debugFlux() == expected(before, 32*u, {8*u, 24*u}),
              "synchronous data writes replace the live shifter immediately");
    }
    {
        SonyDrive d; Iwm i; configure(i, d, 0x1F, false, false);
        const auto before0 = d.debugFlux();
        d.fluxAngleTicks(true); const auto before1 = d.debugFlux(); d.fluxAngleTicks(false);
        d.tick(20); i.tick(20); i.setSel(true);
        const int64_t origin1 = d.fluxAngleTicks(true);
        d.tick(25); i.tick(25); i.read(kQ7Off);
        std::vector<int64_t> expected1{origin1 + 20*u};
        for (auto t : before1) if (t < origin1 || t >= origin1 + 25*u) expected1.push_back(t);
        std::sort(expected1.begin(), expected1.end());
        check(d.debugFlux() == expected1, "head switch continues the shifter on the new physical face");
        d.fluxAngleTicks(false);
        check(d.debugFlux() == expected(before0, 20*u, {8*u}),
              "head switch closes only the old face's elapsed write arc");
    }
    {
        SonyDrive d; Iwm i; configure(i, d, 0x1F, false, false);
        const auto before = d.debugFlux();
        i.tick(10); i.read(10); i.tick(10); i.reset();
        check(d.debugFlux() == expected(before, 20*u, {8*u}),
              "unchanged drive-select preserves the arc and reset closes partial bits");
    }
    {
        SonyDrive a; Iwm ia; configure(ia, a, 0x1F, false, false);
        ia.tick(23);
        std::vector<sav::u8> snapshot;
        { sav::Writer w(snapshot); a.visit(w); ia.visit(w); }
        SonyDrive b; Iwm ib; b.reset(); ib.reset(); ib.attachDrive(&b, nullptr);
        { sav::Reader r(snapshot.data(), snapshot.size()); b.visit(r); ib.visit(r);
          check(r.ok(), "fresh controller restores the partial magnetic write"); }
        ia.tick(107); ib.tick(107); ia.read(kQ7Off); ib.read(kQ7Off);
        std::vector<sav::u8> sa, sb;
        { sav::Writer w(sa); a.visit(w); ia.visit(w); }
        { sav::Writer w(sb); b.visit(w); ib.visit(w); }
        check(sa == sb, "mid-byte restore resumes identical medium and controller state");
    }
}
