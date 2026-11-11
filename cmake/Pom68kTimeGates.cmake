# POM68K — device-time gates: a device fed less often must stay identical
# to the device fed at every opportunity. One gate per batching rule.

# Scc8530::tick's quiet short-circuit against the complete tick, on a random
# stream of accesses, injections and slices (every serialized byte compared).
add_executable(scc_quiet_tick_test tests/scc_quiet_tick_test.cpp)
target_link_libraries(scc_quiet_tick_test PRIVATE pom68k_core)
add_test(NAME scc_quiet_tick_test COMMAND scc_quiet_tick_test)

# M68hc05 run-ahead (CudaLle::tick): internal instructions executed early
# move no port access, whatever the slicing. Synthetic ROMs, no dump.
add_executable(m68hc05_runahead_test tests/m68hc05_runahead_test.cpp)
target_link_libraries(m68hc05_runahead_test PRIVATE pom68k_core)
add_test(NAME m68hc05_runahead_test COMMAND m68hc05_runahead_test)

# Compact-board deadline (Cpu68k::catchUp, MacMemory::cyclesToNextEvent):
# two Mac Plus machines, one ticked at every bus access and one at its next
# event, compared byte for byte every frame under scripted input. Needs the
# Mac Plus ROM; the floppy pass needs disks35/Disk605.dsk.
add_executable(compact_deadline_etalon tests/compact_deadline_etalon.cpp)
target_link_libraries(compact_deadline_etalon PRIVATE pom68k_core)
add_test(NAME compact_deadline_etalon COMMAND compact_deadline_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Portable / PowerBook 100 slicing (PortableCpu::runCycles): one call per
# frame against random slices from power-on, every serialized byte compared.
add_executable(portable_slicing_etalon tests/portable_slicing_etalon.cpp)
target_link_libraries(portable_slicing_etalon PRIVATE pom68k_core)
add_test(NAME portable_slicing_etalon COMMAND portable_slicing_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
