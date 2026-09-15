// POM68K — gate `atalk_hub_test`: AtalkHub's live reconfiguration — the
// AppleTalk window's « Configuration des services » — and the relaunch
// family that carries it (`--atalk-<key>=`, RuntimeConfigNetwork.cpp).
//
// Pins: reconfigure() before attach is the configuration attach uses;
// reconfigure() after attach reaches all three services with the volume
// name derived from the folder when left empty; the toggles are the
// checkboxes' and are not touched by the form; the IPv4/CIDR helpers round
// trip; and the relaunch line reads back into the same hub configuration
// through RuntimeConfig, one --atalk-* per key, ahead of the media.
//
// No ROM, no disk image, no host sockets: always runs.

#include "AtalkHub.h"
#include "RuntimeConfig.h"
#include "Scc8530.h"
#include "atalk_test_util.h"

#include <string>
#include <utility>

int main() {
    // ── before attach: the form is simply what attach() will use ──
    {
        AtalkHub hub;
        AtalkHub::Config next = hub.config();
        next.serverName = "Bureau";
        next.printerName = "Laser";
        next.spoolDir = "/tmp/pom68k_hub_spool";
        hub.reconfigure(next);
        const AtalkHub::Config got = hub.config();
        CHECK(got.serverName == "Bureau" && got.printerName == "Laser" &&
              got.spoolDir == "/tmp/pom68k_hub_spool",
              "reconfigure() before attach stores the identity");
        next.serverName = "";
        next.printerName = "";
        next.spoolDir = "";
        hub.reconfigure(next);
        const AtalkHub::Config blank = hub.config();
        CHECK(blank.serverName == "POM68K" && blank.printerName == "POM68K" &&
              blank.spoolDir == "run/print",
              "empty names fall back to the hub's defaults, never to an empty NBP name");
    }

    // ── after attach: the three services follow, live ──
    {
        struct Machine {
            Scc8530 serial;
            Scc8530& scc() { return serial; }
        } mem;
        AtalkHub hub;
        hub.setDefaultShareDir("/tmp/pom68k_hub_share/Partage");
        hub.attach(mem, 1000000, nullptr);
        hub.tick(1000);
        AtalkHub::Snapshot s = hub.snapshot();
        CHECK(s.attached && s.afp.registered && s.pap.registered && s.macip.registered,
              "attached: the three services are registered");
        CHECK(s.afp.volName == "Partage",
              "an empty volume name is the shared folder's own name");

        AtalkHub::Config next = s.cfg;
        next.serverName = "Bureau";
        next.volName = "Docs";
        next.shareDir = "/tmp/pom68k_hub_share/Autre";
        next.printerName = "Laser";
        next.spoolDir = "/tmp/pom68k_hub_spool";
        CHECK(AtalkHub::parseCidr("10.1.2.1/16", next.gwIp, next.gwMask) &&
              AtalkHub::parseIpv4("10.1.2.53", next.dns), "the form's addresses parse");
        next.afp = false;                       // the form must NOT carry toggles
        hub.reconfigure(next);
        hub.tick(2000);
        s = hub.snapshot();
        CHECK(s.afp.registered && s.afp.serverName == "Bureau" && s.afp.volName == "Docs" &&
              s.afp.dirPath == "/tmp/pom68k_hub_share/Autre",
              "AFP re-registered under the new name, volume and folder");
        CHECK(s.pap.registered && s.pap.printerName == "Laser" &&
              s.pap.spoolDir == "/tmp/pom68k_hub_spool",
              "the printer re-registered under its new name and spool");
        CHECK(s.macip.registered && s.macip.gwIp == "10.1.2.1" && s.macip.dns == "10.1.2.53",
              "the gateway re-registered at its new address");
        CHECK(s.cfg.afp, "the AFP toggle stayed the checkbox's, not the form's");
        CHECK(s.cfg.gwMask == 0xFFFF0000u, "/16 became the mask");

        next = s.cfg;
        next.volName = "";
        hub.reconfigure(next);
        CHECK(hub.snapshot().afp.volName == "Autre",
              "clearing the volume name derives it from the new folder");
    }

    // ── the text helpers ──
    {
        uint32_t ip = 0, mask = 0;
        CHECK(AtalkHub::parseIpv4("192.168.151.1", ip) && ip == 0xC0A89701u, "dotted quad parses");
        CHECK(!AtalkHub::parseIpv4("192.168.151", ip), "three octets refused");
        CHECK(!AtalkHub::parseIpv4("192.168.151.256", ip), "an octet above 255 refused");
        CHECK(!AtalkHub::parseIpv4("192.168.151.1x", ip), "trailing junk refused");
        CHECK(AtalkHub::parseCidr("192.168.151.1", ip, mask) && mask == 0xFFFFFF00u,
              "a bare address means /24");
        CHECK(!AtalkHub::parseCidr("192.168.151.1/0", ip, mask) &&
              !AtalkHub::parseCidr("192.168.151.1/31", ip, mask), "prefix outside 1-30 refused");
        CHECK(AtalkHub::formatCidr(0xC0A89701u, 0xFFFFFF00u) == "192.168.151.1/24" &&
              AtalkHub::formatIpv4(0x08080808u) == "8.8.8.8", "formatting round-trips");
    }

    // ── the relaunch family through RuntimeConfig ──
    {
        using pom68k::StartupSnapshot;
        using pom68k::app::RuntimeConfig;
        auto parse = [](std::vector<std::string> args) {
            std::vector<char*> argv = {const_cast<char*>("POM68K")};
            for (std::string& a : args) argv.push_back(a.data());
            return RuntimeConfig::parse(int(argv.size()), argv.data(), StartupSnapshot{});
        };
        pom68k::app::NetworkConfig net;
        net.shareDirectory = "/srv/share";
        net.serverName = "Bureau";
        net.volumeName = "";
        net.printerName = "Laser";
        net.spoolDirectory = "/srv/spool";
        net.gateway = "10.1.2.1/16";
        net.dns = "10.1.2.53";
        net.etherTalk = "0";
        const std::vector<std::string> line = pom68k::app::atalkArguments(
            {"--atalk-server=Old", "rom.bin", "disk.dsk"}, net);
        CHECK(line.size() == 10 && line[8] == "rom.bin" && line[9] == "disk.dsk",
              "eight --atalk-* arguments, one per key, ahead of the media, the old one gone");
        const RuntimeConfig back = parse(line);
        const pom68k::app::NetworkConfig& n = back.network();
        CHECK(n.shareDirectory == "/srv/share" && n.serverName == "Bureau" &&
              n.volumeName == std::string() && n.printerName == "Laser" &&
              n.spoolDirectory == "/srv/spool" && n.gateway == "10.1.2.1/16" &&
              n.dns == "10.1.2.53" && n.etherTalk == "0",
              "…and read back field for field, an empty volume and the EtherTalk switch included");
        CHECK(back.romPath() == "rom.bin", "the ROM and media arguments are untouched");
        const RuntimeConfig none = parse({"rom.bin"});
        CHECK(!none.network().serverName && none.network().shareDirectory.empty(),
              "without the family, nothing is set");
        pom68k::app::NetworkConfig unknown;
        CHECK(!pom68k::app::applyAtalkArgument(unknown, "--atalk-zone=Bureau") &&
              !pom68k::app::applyAtalkArgument(unknown, "--atalk-server"),
              "an unknown key or a missing '=' is refused, not swallowed");
    }

    if (failures) { std::printf("atalk_hub_test: %d check(s) failed\n", failures); return 1; }
    std::printf("atalk_hub_test: live reconfiguration + relaunch family, gate passed\n");
    return 0;
}
