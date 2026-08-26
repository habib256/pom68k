# On-demand diagnostic, trace and benchmark executables.
# These targets are intentionally excluded from the default build.

# Dev tool (not a gate): trace a real ROM boot — hot PCs, overlay, screen.
# EXCLUDE_FROM_ALL: dev tools are not built by `make`/`ctest` (they'd relink
# the whole core with LTO each time — slow); build on demand: `make dasm`.
add_executable(boot_trace EXCLUDE_FROM_ALL tests/boot_trace.cpp)
target_link_libraries(boot_trace PRIVATE pom68k_core)

# Dev tool: disassemble ROM ranges (Moira dasm).
add_executable(dasm EXCLUDE_FROM_ALL tests/dasm.cpp)
target_link_libraries(dasm PRIVATE pom68k_core)

# Dev harness: ROM×disk Finder matrix (not a CTest gate).
add_executable(finder_boot_matrix EXCLUDE_FROM_ALL tests/finder_boot_matrix.cpp)
target_link_libraries(finder_boot_matrix PRIVATE pom68k_core)

# Dev harness: fixed guest-cycle stopwatch for the two engines (and a
# screen dump when a boot etalon's signature needs eyes on it). Not a
# gate — it measures, it does not judge. See src/jit/POM68K_JIT.md § 3.
add_executable(jit_bench EXCLUDE_FROM_ALL tests/jit_bench.cpp)
target_link_libraries(jit_bench PRIVATE pom68k_core)

# The same stopwatch on the LC II — the machine `TODO.md` § 0·A names as
# the speed objective ("~×1,3 temps réel en turbo"), and the one a
# Quadra-only bench could say nothing about. Body shared with jit_bench:
# tests/BenchHarness.h.
add_executable(jit_bench_lcii EXCLUDE_FROM_ALL tests/jit_bench_lcii.cpp)
target_link_libraries(jit_bench_lcii PRIVATE pom68k_core)

# Complete the fixed-cycle Macintosh family matrix. These are measurement
# harnesses, not boot gates: equal cycle budgets + fingerprints make host
# ratios comparable without mistaking time-to-Finder for throughput.
add_executable(jit_bench_plus EXCLUDE_FROM_ALL tests/jit_bench_plus.cpp)
target_link_libraries(jit_bench_plus PRIVATE pom68k_core)
add_executable(jit_bench_macii EXCLUDE_FROM_ALL tests/jit_bench_macii.cpp)
target_link_libraries(jit_bench_macii PRIVATE pom68k_core)

# A/B protocol (docs/MEASURING.md § R2): "below the noise floor there is
# no claim" needs the floor to be a NUMBER, and one that was measured on
# this host by a null experiment rather than guessed by whoever is
# reading the delta. It reaches the benches the same way every other
# budget in this tree reaches a test — through the reviewed manifest, so
# a change of policy is a diff and not a literal edited in a header.
foreach(b jit_bench jit_bench_lcii jit_bench_plus jit_bench_macii)
    pom68k_perf_budget(${b} host_wallclock any noise_floor_permille
                       POM68K_BENCH_NOISE_FLOOR_PERMILLE)
endforeach()

# Dev harness: the drawing-phase fallback census (POM68K_JIT.md § 3.5 —
# the indexed-mode question refuses idle-Finder numbers). Boots the
# Q605, launches dev/mac-rogue off the SWIM2 and plays it, dumping the
# census per phase. A measurement, not a gate.
add_executable(q605_rogue_census EXCLUDE_FROM_ALL tests/q605_rogue_census.cpp)
target_link_libraries(q605_rogue_census PRIVATE pom68k_core)

# All dev tools below are EXCLUDE_FROM_ALL — not built by `make`/`ctest`
# (each relinks the core with LTO = slow); build on demand by name,
# e.g. `make q605_trace`.
add_executable(sony_trace EXCLUDE_FROM_ALL tests/sony_trace.cpp)
target_link_libraries(sony_trace PRIVATE pom68k_core)

# Dev tool (not a gate): real LC II ROM boot trace on the O6 machine.
add_executable(lcii_trace EXCLUDE_FROM_ALL tests/lcii_trace.cpp)

# Dev tool (not a gate): .Sony driver give-up trace on the LC II —
# the sony_trace pattern aimed at the TODO §1 boosted-030 mount bug.
add_executable(lcii_sony_trace EXCLUDE_FROM_ALL tests/lcii_sony_trace.cpp)
target_link_libraries(lcii_sony_trace PRIVATE pom68k_core)

# Dev tool (not a gate): LC 475 / Quadra 605 ROM boot trace (Q5).
add_executable(q605_trace EXCLUDE_FROM_ALL tests/q605_trace.cpp)
target_link_libraries(q605_trace PRIVATE pom68k_core)

# Dev tool (not a gate): PowerBook Duo ROM boot trace (platform #11
# milestone 1, docs/DUO_BRINGUP.md).
add_executable(duo_trace EXCLUDE_FROM_ALL tests/duo_trace.cpp)
target_link_libraries(duo_trace PRIVATE pom68k_core)

# Dev tool (not a gate): Mac IIfx ROM POST trace (platform #12
# milestone 3, docs/IOP_BRINGUP.md).
add_executable(iifx_trace EXCLUDE_FROM_ALL tests/iifx_trace.cpp)
target_link_libraries(iifx_trace PRIVATE pom68k_core)

target_link_libraries(lcii_trace PRIVATE pom68k_core)

add_executable(macii_trace EXCLUDE_FROM_ALL tests/macii_trace.cpp)
target_link_libraries(macii_trace PRIVATE pom68k_core)

add_executable(macii_mm_probe EXCLUDE_FROM_ALL tests/macii_mm_probe.cpp)
target_link_libraries(macii_mm_probe PRIVATE pom68k_core)

add_executable(lcii_mouse_trace EXCLUDE_FROM_ALL tests/lcii_mouse_trace.cpp)
target_link_libraries(lcii_mouse_trace PRIVATE pom68k_core)

# Dev tool (not a gate): where an ADB keystroke dies, on any
# (machine, image) pair — POM68K_PROBE_MACHINE/_IMG/_FRAMES.
add_executable(adb_key_probe EXCLUDE_FROM_ALL tests/adb_key_probe.cpp)
target_link_libraries(adb_key_probe PRIVATE pom68k_core)

add_executable(macii_hmmu_trace EXCLUDE_FROM_ALL tests/macii_hmmu_trace.cpp)
target_link_libraries(macii_hmmu_trace PRIVATE pom68k_core)

# Dev tool (not a gate): offline Mac ROM introspection (header,
# resources, trap table, UniversalInfo) — Basilisk II-derived parsers,
# see docs/BASILISK_ROM_NOTES.md. Standalone, no emulator core.
add_executable(rominfo EXCLUDE_FROM_ALL tools/rominfo.cpp)

