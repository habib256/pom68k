# The DaynaPort SCSI/Link on every platform's bus (TODO § 2, 2026-09-12).
# `daynaport_test` pins the card's command set and `q605_dayna_driver_etalon`
# runs Dayna's own driver on the Quadra 605; these gates carry the card to the
# other eleven memory maps (src/DaynaPortBus.h).

# The card through both real SCSI controllers, the way a guest drives it —
# the NCR 5380 of eight platforms and the 53C96 of four. Asset-free.
add_executable(daynaport_bus_test tests/daynaport_bus_test.cpp)
target_link_libraries(daynaport_bus_test PRIVATE pom68k_core)
add_test(NAME daynaport_ncr5380_test COMMAND daynaport_bus_test 5380)
add_test(NAME daynaport_ncr53c96_test COMMAND daynaport_bus_test 53c96)

# Each platform's representative boot (POM68K_ETALON_CORE, with the compacts'
# SCSI boot standing in for their floppy one) again, with the card at ID 4
# (tests/DaynaBootProbe.h): the same Finder verdict as without it, and the card
# must have answered the guest. The base etalon's own environment and bound
# are repeated, so the variant differs from it by the card alone.
function(pom68k_dayna_boot name binary timeout)
    set(env POM68K_TEST_DAYNAPORT=4 ${ARGN})
    add_test(NAME ${name}_dayna_boot_etalon COMMAND ${binary}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${name}_dayna_boot_etalon PROPERTIES
                         ENVIRONMENT "${env}" TIMEOUT ${timeout})
endfunction()
pom68k_dayna_boot(se         scsi_boot_etalon       1800 POM68K_COMPACT_MODEL=se)
pom68k_dayna_boot(macii      macii_boot_etalon      1800)
pom68k_dayna_boot(iifx       iifx_boot_etalon       1800)
pom68k_dayna_boot(iisi       iisi_boot_etalon       1800)
pom68k_dayna_boot(lcii       lcii_boot_etalon       1800)
pom68k_dayna_boot(lc3        lc3_boot_etalon        1800)
pom68k_dayna_boot(iivx       iivx_boot_etalon       1800)
pom68k_dayna_boot(q605       q605_boot_etalon       1800)
pom68k_dayna_boot(centris650 centris650_boot_etalon 1800)
pom68k_dayna_boot(q700       q700_boot_etalon       1800)
pom68k_dayna_boot(q630       q630_boot_etalon       3600)
pom68k_dayna_boot(duo230     duo230_boot_etalon     1800)
