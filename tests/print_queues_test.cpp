// POM68K — gate `print_queues_test`: the host half of the in-process
// LaserWriter (PrintQueues.h). Pins: the lpstat parsers on the shapes CUPS
// 2.x prints under LC_ALL=C; papd's status wording; the destination's text
// form ('' / "#file" / queue) round-tripping; and, end to end against FAKE
// `lp`/`lpstat` scripts, the spooler's poll, its `lp -d … -o …` hand-over,
// its file fallback when lp refuses, and the PAP consequences — SendStatus
// carries the queue's state, and a queue that rejects jobs answers OpenConn
// busy, as papd does.
//
// The fakes live in a private temp folder: no gate ever reaches the host's
// real CUPS or prints on its printer. Needs /bin/sh; always runs on POSIX.

#include "PapServer.h"
#include "PrintQueues.h"
#include "atalk_test_util.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void spit(const fs::path& p, const std::string& s) {
    std::ofstream(p, std::ios::binary) << s;
}
bool waitFor(const std::function<bool()>& pred) {
    for (int i = 0; i < 500; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

// lpstat: -e / -d fixed, -p Q / -a Q from files the gate rewrites.
// lp: records its arguments and the job; exits with lp_rc when present.
void writeFakes(const fs::path& dir) {
    const std::string d = dir.string();
    spit(dir / "lpstat",
         "#!/bin/sh\n"
         "case \"$1\" in\n"
         "  -e) printf 'Bureau-Laser\\nAtelier\\n' ;;\n"
         "  -d) echo 'system default destination: Atelier' ;;\n"
         "  -p|-a) f=\"" + d + "/${1#-}_$2\"; [ -f \"$f\" ] || exit 1; cat \"$f\" ;;\n"
         "esac\n");
    spit(dir / "lp",
         "#!/bin/sh\n"
         "echo \"$@\" > \"" + d + "/lp_args\"\n"
         "cat > \"" + d + "/lp_job\"\n"
         "[ -f \"" + d + "/lp_rc\" ] && exit $(cat \"" + d + "/lp_rc\")\n"
         "exit 0\n");
    fs::permissions(dir / "lpstat", fs::perms::owner_all);
    fs::permissions(dir / "lp", fs::perms::owner_all);
}

std::vector<uint8_t> openConn(Wire& w, uint16_t tid) {
    w.clear();
    w.atpReq(47, 180, 131, tid, { 1, 1, 0, 0, 180, 8, 0, 0 });
    auto r = w.atpResps(tid, 180);
    return r.empty() ? std::vector<uint8_t>{} : r[0];
}
std::string sendStatus(Wire& w, uint16_t tid) {
    w.clear();
    w.atpReq(47, 100, 131, tid, { 0, 8, 0, 0 });
    auto r = w.atpResps(tid, 100);
    return (r.empty() || r[0].size() < 9) ? std::string()
                                          : std::string(r[0].begin() + 9, r[0].end());
}

} // namespace

int main() {
    // ── the parsers, on CUPS's own shapes ──
    {
        CHECK((lpstat::parseList("HP-LaserJet-M101-M106\nAtelier\n") ==
               std::vector<std::string>{ "HP-LaserJet-M101-M106", "Atelier" }),
              "lpstat -e: one queue per line");
        CHECK(lpstat::parseDefault("system default destination: Atelier\n") == "Atelier" &&
              !lpstat::parseDefault("no system default destination\n"),
              "lpstat -d: the default, or none");
        QueueState s = lpstat::parseState("Q",
            "printer Q is idle.  enabled since Fri Mar 20 23:38:37 2026\n",
            "Q accepting requests since Fri Mar 20 23:38:37 2026\n");
        CHECK(s.phase == QueueState::Phase::Idle && s.accepting, "idle and accepting");
        s = lpstat::parseState("Q", "printer Q now printing Q-12.  enabled since x\n",
                               "Q accepting requests since x\n");
        CHECK(s.phase == QueueState::Phase::Processing, "now printing = processing");
        s = lpstat::parseState("Q", "printer Q disabled since x -\n\tPaper jam\n",
                               "Q not accepting requests since x -\n\tMaintenance\n");
        CHECK(s.phase == QueueState::Phase::Stopped && !s.accepting && s.reason == "Paper jam",
              "disabled + not accepting, the reason line kept");
        s = lpstat::parseState("Q", "printer QQ is idle.  enabled since x\n", "");
        CHECK(s.phase == QueueState::Phase::Unknown && !s.accepting,
              "another queue's line is not this queue's state");
    }

    // ── papd's words ──
    {
        QueueState s{ QueueState::Phase::Idle, true, {} };
        CHECK(papStatusLine("Q", s) == "status: idle; info: \"Q\" is ready ; ", "ready");
        s.accepting = false;
        CHECK(papStatusLine("Q", s).starts_with("status: busy; info: \"Q\" is rejecting jobs"),
              "rejecting");
        s = { QueueState::Phase::Stopped, true, "Paper jam" };
        CHECK(papStatusLine("Q", s) ==
                  "status: idle; info: \"Q\" is stopped, accepting jobs ; Paper jam",
              "stopped, with its reason");
        s = {};
        CHECK(papStatusLine("Q", s) == "status: busy; info: \"Q\" appears to be offline.",
              "unknown = offline");
    }

    // ── the destination's text form, and lp -o words ──
    {
        using K = PrintDestination::Kind;
        CHECK(PrintDestination::parse("").kind == K::CupsDefault &&
              PrintDestination::parse("#file").kind == K::File &&
              PrintDestination::parse("Atelier").queue == "Atelier",
              "'' / #file / a queue");
        for (const char* t : { "", "#file", "Atelier" })
            CHECK(PrintDestination::parse(t).format() == t, "format() round-trips");
        CHECK((splitPrintOptions("  media=A4   sides=two-sided ") ==
               std::vector<std::string>{ "media=A4", "sides=two-sided" }),
              "options split on whitespace");
    }

    const fs::path dir = fs::temp_directory_path() /
                         ("pom68k_print_queues_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    writeFakes(dir);
    const PrintSpooler::Commands fakes{ (dir / "lp").string(), (dir / "lpstat").string() };
    spit(dir / "p_Bureau-Laser", "printer Bureau-Laser is idle.  enabled since x\n");
    spit(dir / "a_Bureau-Laser", "Bureau-Laser accepting requests since x\n");

    // ── the spooler against the fakes ──
    {
        PrintSpooler sp;
        sp.setCommands(fakes);
        sp.setDestination(PrintDestination::parse("Bureau-Laser"), "media=A4 sides=two-sided");
        sp.start();
        CHECK(waitFor([&] { return sp.view().polled; }), "the first poll completes");
        PrintSpooler::View v = sp.view();
        CHECK(v.cups && v.queues.size() == 2 && v.defaultQueue == "Atelier" &&
              v.activeQueue == "Bureau-Laser" && v.state.phase == QueueState::Phase::Idle &&
              v.state.accepting,
              "queues, default and the chosen queue's state");

        sp.submit(7, { '%', '!', 'P', 'S' }, (dir / "spool").string());
        sp.drain();
        v = sp.view();
        CHECK(slurp(dir / "lp_args") == "-d Bureau-Laser -o media=A4 -o sides=two-sided -s -- -\n",
              "lp -d <queue> -o <each option> -s, the job on stdin");
        CHECK(slurp(dir / "lp_job") == "%!PS", "the job reached lp intact");
        CHECK(v.lastSerial == 7 && v.lastResult == "CUPS (Bureau-Laser)" && v.pending == 0,
              "the hand-over is reported");

        spit(dir / "lp_rc", "1\n");
        sp.submit(8, { '%', '!' }, (dir / "spool").string());
        sp.drain();
        v = sp.view();
        CHECK(v.lastSerial == 8 && fs::exists(v.lastResult) &&
                  slurp(v.lastResult) == "%!",
              "lp refused: the job lands in the spool folder instead");
        fs::remove(dir / "lp_rc");

        sp.setDestination(PrintDestination::parse(""), "");
        CHECK(waitFor([&] { return sp.view().activeQueue == "Atelier" &&
                                   sp.view().state.phase == QueueState::Phase::Unknown; }),
              "the CUPS default resolves to lpstat -d's queue");
    }

    // ── PAP: the printer speaks for its queue ──
    {
        Wire w;
        PapServer pap(w.st);
        pap.configure("POM68K", (dir / "spool").string());
        pap.setPrintCommands(fakes);
        pap.setDestination(PrintDestination::parse("Bureau-Laser"), "");
        pap.setEnabled(true);
        CHECK(waitFor([&] { return pap.status().host.polled; }), "the printer polls its queue");
        CHECK(sendStatus(w, 0x5000) == "status: idle; info: \"Bureau-Laser\" is ready ; ",
              "SendStatus carries the queue's state in papd's words");

        spit(dir / "a_Bureau-Laser",
             "Bureau-Laser not accepting requests since x -\n\tMaintenance\n");
        pap.setPrintCommands(fakes);                        // re-poll now
        CHECK(waitFor([&] { return !pap.status().host.state.accepting; }),
              "the rejection is polled");
        std::vector<uint8_t> r = openConn(w, 0x5001);
        CHECK(r.size() > 9 && r[1] == 2 && r[2] == 0xFF && r[3] == 0xFF,
              "a rejecting queue answers OpenConn busy");
        CHECK(r.size() > 9 && std::string(r.begin() + 9, r.end())
                                  .find("is rejecting jobs; Maintenance") != std::string::npos,
              "…and says why");
        CHECK(!pap.status().busy, "no connection was opened");

        spit(dir / "a_Bureau-Laser", "Bureau-Laser accepting requests since x\n");
        pap.setPrintCommands(fakes);
        CHECK(waitFor([&] { return pap.status().host.state.accepting; }),
              "acceptance is polled back");
        r = openConn(w, 0x5002);
        CHECK(r.size() > 9 && r[2] == 0 && r[3] == 0, "OpenConn accepted again");
        std::vector<uint8_t> rq;
        const int tid = w.findTReq(47, 180, rq);         // before sendStatus clears
        CHECK(tid >= 0, "the server pulls the job");
        CHECK(sendStatus(w, 0x5003) == "status: busy; source: AppleTalk",
              "while a job is received, the printer is busy with it");
        const char* ps = "%!PS-Adobe-3.0\nshowpage\n";
        std::vector<uint8_t> p = { 1, 4, 1, 0 };
        p.insert(p.end(), ps, ps + std::strlen(ps));
        w.atpRespond(47, 180, 131, uint16_t(tid), { p });
        pap.drainSpooler();
        CHECK(slurp(dir / "lp_job") == ps && slurp(dir / "lp_args") == "-d Bureau-Laser -s -- -\n",
              "the job went to the chosen queue");
        CHECK(pap.status().jobs == 1 && pap.status().lastJob == "CUPS (Bureau-Laser)",
              "the window learns where it went");
    }

    fs::remove_all(dir);
    if (failures) { std::printf("print_queues_test: %d failure(s)\n", failures); return 1; }
    std::printf("print_queues_test OK\n");
    return 0;
}
