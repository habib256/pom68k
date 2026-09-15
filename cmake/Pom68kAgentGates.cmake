# « POM68K Disques » as a beyond-boot proof on every platform that boots a
# System with Startup Items (docs/SCSI_HOTPLUG.md § 8, tests/AgentBootProbe.h).
# Each platform's representative boot again, with the agent installed into
# the boot volume's Startup Items in memory before the boot: the same Finder
# verdict as without it, then the Finder must have launched the agent (its
# first mailbox poll) and the agent must mount a blank disk attached live,
# by name — the Process Manager, the SCSI Manager and the File Manager past
# the boot signature. The base etalon's environment and bound are repeated,
# so the variant differs from it by the agent alone. The compacts' System 6
# has no Startup Items and carries no such variant.
function(pom68k_agent_boot name binary timeout)
    set(env POM68K_TEST_AGENT=1 ${ARGN})
    add_test(NAME ${name}_agent_boot_etalon COMMAND ${binary}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${name}_agent_boot_etalon PROPERTIES
                         ENVIRONMENT "${env}" TIMEOUT ${timeout})
endfunction()
# No variant on macii_boot_etalon's own image (HD20SC, System 6, no Startup
# Items); the Mac II's proof rides macii_sys7 below. GISTPERSO is no help on
# the Glue boards: its System 7.5.5 was installed for other machines and
# refuses them on screen — « Le fichier System du disque de démarrage ne
# dispose pas des ressources nécessaires pour ce Macintosh » (2026-09-15).
pom68k_agent_boot(iifx       iifx_boot_etalon       1800)
pom68k_agent_boot(iisi       iisi_boot_etalon       1800)
pom68k_agent_boot(lcii       lcii_boot_etalon       1800)
pom68k_agent_boot(lc3        lc3_boot_etalon        1800)
pom68k_agent_boot(iivx       iivx_boot_etalon       1800)
pom68k_agent_boot(q605       q605_boot_etalon       1800)
pom68k_agent_boot(centris650 centris650_boot_etalon 1800)
pom68k_agent_boot(q700       q700_boot_etalon       1800)
pom68k_agent_boot(q630       q630_boot_etalon       3600)
pom68k_agent_boot(duo230     duo230_boot_etalon     1800)
# The rest of the roster, each on the image its own boot etalon uses when that
# image carries Startup Items (System 7.0+), else the System 7.5.5 GISTPERSO
# volume by the POM68K_BEYOND_IMG override (the 68020/68030 NuBus boards boot
# System 6 by default).
pom68k_agent_boot(macii_sys7 macii_sys7_boot_etalon 1800)
pom68k_agent_boot(lcii_sys7  lcii_sys7_boot_etalon  1800)
pom68k_agent_boot(lc475      lc475_boot_etalon      1800)
pom68k_agent_boot(lc575      lc575_boot_etalon      1800)
pom68k_agent_boot(lc         lc_boot_etalon         1800)
pom68k_agent_boot(classic2   classic2_boot_etalon   1800)
pom68k_agent_boot(cclassic   cclassic_boot_etalon   1800)
pom68k_agent_boot(lc3plus    lc3plus_boot_etalon    1800)
pom68k_agent_boot(lc520      lc520_boot_etalon      1800)
pom68k_agent_boot(lc550      lc550_boot_etalon      1800)
pom68k_agent_boot(cclassic2  cclassic2_boot_etalon  1800)
# The IIx/IIcx on the System 7.0 volume, with the REAL Toby declaration ROM:
# on the synthetic sResource a System 7 boot draws no desktop (2026-09-15,
# TODO § Fidélité), so the etalon soft-skips the variant without the dump.
pom68k_agent_boot(iix        iix_boot_etalon        1800 POM68K_BEYOND_IMG=hdv/System\ 7.0\ HD.dsk)
pom68k_agent_boot(iicx       iix_boot_etalon        1800 POM68K_BEYOND_IMG=hdv/System\ 7.0\ HD.dsk POM68K_IICX=1)
pom68k_agent_boot(se30       se30_boot_etalon       1800 POM68K_BEYOND_IMG=hdv/System\ 7.0\ HD.dsk)
pom68k_agent_boot(iici       iici_boot_etalon       1800)
pom68k_agent_boot(mactv      mactv_boot_etalon      1800)
pom68k_agent_boot(iivi       iivx_boot_etalon       1800 POM68K_IIVI=1)
pom68k_agent_boot(centris610 centris650_boot_etalon 1800 POM68K_CENTRIS610=1)
pom68k_agent_boot(quadra650  centris650_boot_etalon 1800 POM68K_CENTRIS_MODEL=q650)
pom68k_agent_boot(quadra610  centris650_boot_etalon 1800 POM68K_CENTRIS_MODEL=q610)
pom68k_agent_boot(quadra800  centris650_boot_etalon 1800 POM68K_CENTRIS_MODEL=q800)
pom68k_agent_boot(lc580      q630_boot_etalon       3600 POM68K_Q630_ID=A55A225A POM68K_Q630_ROM=lc580)
# The compacts boot System 6 from HD20SC; System 7.0 — the Mac II's volume —
# runs on a Plus with 4 MB and gives them Startup Items. Longer boot.
pom68k_agent_boot(plus       scsi_boot_etalon       2400 POM68K_BEYOND_IMG=hdv/System\ 7.0\ HD.dsk POM68K_FRAMES=9000)
pom68k_agent_boot(se         scsi_boot_etalon       2400 POM68K_BEYOND_IMG=hdv/System\ 7.0\ HD.dsk POM68K_FRAMES=9000 POM68K_COMPACT_MODEL=se)
pom68k_agent_boot(sefdhd     scsi_boot_etalon       2400 POM68K_BEYOND_IMG=hdv/System\ 7.0\ HD.dsk POM68K_FRAMES=9000 POM68K_COMPACT_MODEL=sefdhd)
pom68k_agent_boot(classic    scsi_boot_etalon       2400 POM68K_BEYOND_IMG=hdv/System\ 7.0\ HD.dsk POM68K_FRAMES=9000 POM68K_COMPACT_MODEL=classic)
# The Quadra 900/950 take their model as an argument, not an environment.
foreach(model q900 q950)
    add_test(NAME ${model}_agent_boot_etalon COMMAND q700_boot_etalon ${model}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${model}_agent_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_TEST_AGENT=1" TIMEOUT 1800)
endforeach()
