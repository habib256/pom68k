# CTest tier derivation, contract validation, manifests and convenience targets.
# This must remain the final test module so it sees every registered gate.

# ─── Test tiers (`ctest -L <label>`) ─────────────────────────────────
# A bare `ctest` runs every gate and takes hours on a busy host — far too
# slow to develop against, and the boot etalons are contention-sensitive
# so `-j` is not an option either. Every gate therefore carries labels,
# so a working loop can pick the smallest set that actually proves the
# change at hand:
#
#   ctest -L smoke        ~35 s   ONE machine, BOTH engines — the JIT loop
#   ctest -L unit         ~1 min  everything needing no ROM or disk image
#   ctest -L jit          ~8 min  every JIT gate, all four 68040 machines
#   ctest -L m040         ~25 min the 68040 family on both engines
#   ctest -L etalon-core          ONE profile per platform — 12 of the 119
#   ctest                         everything (the release gate)
#
# `etalon` on its own is 119 gates / ~3 h 45, which is a release gate and
# not a pre-commit check — so the temptation to skip it grows with every
# machine added. `etalon-core` is the answer to "does a change break a
# PLATFORM": one representative profile from each of the 12 memory-map
# implementations, chosen as the profile that platform was brought up on.
# A change that survives it can still break a sibling PROFILE (a clock, a
# model ID, an MCU), which is what the full label is for.
# `-L etalon` still matches all 119: CTest matches labels by regex, and the
# core gates carry both.
set(POM68K_ETALON_CORE
    system_boot_etalon        # 68000 compacts   — MacMemory
    macii_boot_etalon         # GLUE + NuBus     — MacIIMemory
    iifx_boot_etalon          # OSS + 2 IOPs     — IIfxMemory
    iisi_boot_etalon          # RBV              — RbvMemory
    lcii_boot_etalon          # V8               — V8Memory
    lc3_boot_etalon           # Sonora           — SonoraMemory
    iivx_boot_etalon          # VASP             — VaspMemory
    q605_boot_etalon          # MEMCjr+PrimeTime — Q605Memory
    centris650_boot_etalon    # djMEMC + IOSB    — CentrisMemory
    q700_boot_etalon          # discrete 040     — Q700Memory
    q630_boot_etalon          # F108 + Valkyrie  — Q630Memory
    duo230_boot_etalon)       # MSC + PG&E       — MscMemory
#
# Labels are DERIVED from test names rather than declared per gate, so a
# gate registered tomorrow is classified the moment it is added.
# ── The file-size ratchet ────────────────────────────────────────────
# CLAUDE.md's first convention is "one concern per file", and until now
# nothing enforced it: `src/main.cpp` reached 5990 lines by accreting one
# ~350-line GUI runner per platform, twelve of them, ~85 % identical text.
# A convention nothing measures is a convention that loses. This gate is
# the measurement — a ratchet over `tools/file_size_budget.txt`: a ceiling
# may fall freely, never rise, and a new file over the limit with no
# ceiling fails outright. Growing one now means editing the budget in the
# same commit, which is the moment to ask whether the code belongs in a
# new translation unit. Reads the source tree, links nothing.
add_test(NAME file_size_budget_test
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_file_sizes.sh"
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(file_size_budget_test PROPERTIES TIMEOUT 45)

get_property(pom68k_tests DIRECTORY PROPERTY TESTS)
foreach(t IN LISTS pom68k_tests)
    set(lbl "")
    if(t STREQUAL "gui_smoke_test")
        list(APPEND lbl gui)
    elseif(t MATCHES "etalon$")
        list(APPEND lbl etalon)
        if(t IN_LIST POM68K_ETALON_CORE)
            list(APPEND lbl etalon-core)
        endif()
    else()
        list(APPEND lbl unit)
    endif()
    if(t MATCHES "^jit_")
        list(APPEND lbl jit)
    endif()
    # The 68040 family — the JIT's entire blast radius today.
    if(t MATCHES "q605|lc475|lc575|centris|quadra6|quadra8|q630|lc580|q700|q900|q950|turboscsi|swim2")
        list(APPEND lbl m040)
    endif()
    # The 68030 family — the six platforms behind `Cpu030`, `SonoraCpu`,
    # `VaspCpu`, `RbvCpu`, `IIfxCpu` and `MscCpu`. Added 2026-08-18 for
    # the same reason `m040` exists: an engine-policy change on one CPU
    # family needs ONE command to prove, not a hand-written alternation
    # of forty gate names. The absence of this label is a large part of
    # why a 68030 default measured on 2026-08-10 was still unflipped a
    # week later — the doubt was cheap, the proof was not.
    # `lc` alone would catch lc475/lc575/lc580, which are 68040 boards.
    if(t MATCHES "lcii|lc_boot|lc3|lc520|lc550|classic2|cclassic|mactv|iivx|iivi|iisi|iici|iifx|duo|rbv|sonora|vasp|msc_")
        list(APPEND lbl m030)
    endif()
    if(POM68K_PRODUCT_LLE_GATES AND
       (t MATCHES "^lle_a64_" OR
        t MATCHES "^(jit_|interp_)(q605|centris650|q630|q700)_boot_etalon$" OR
        t STREQUAL "jit_lockstep_a64_coarse_test" OR
        t STREQUAL "savestate_040_test"))
        list(APPEND lbl product lle a64-oracle)
    endif()
    # MERGE, never overwrite. A gate that set LABELS explicitly at its
    # registration did so because the derivation cannot see what it knows:
    # `quadra_event_scheduler_test` asked for m040 at :880 and silently lost
    # it here for two weeks, because this line used to assign — and the
    # m040 regex below matches `quadra6|quadra8`, not `quadra_`. Nothing
    # reported it: `ctest -L m040` simply ran one gate fewer than the docs
    # claimed. Its sibling `q605_event_scheduler_test` survived only by the
    # accident of re-deriving the same pair. (2026-08-12)
    get_test_property(${t} LABELS pom68k_prior_lbl)
    if(pom68k_prior_lbl)
        list(APPEND lbl ${pom68k_prior_lbl})
        list(REMOVE_DUPLICATES lbl)
    endif()
    set_tests_properties(${t} PROPERTIES LABELS "${lbl}")
endforeach()

# A name in POM68K_ETALON_CORE that is not a registered gate would drop a
# PLATFORM out of the pre-commit tier silently — and a tier that quietly
# covers eleven platforms while claiming twelve is worse than no tier.
# Renaming a gate must therefore break the configure, not the coverage.
foreach(core IN LISTS POM68K_ETALON_CORE)
    if(NOT core IN_LIST pom68k_tests)
        message(FATAL_ERROR
            "POM68K_ETALON_CORE lists '${core}', which is not a registered "
            "test. Update cmake/Pom68kGatePolicy.cmake: it is the one place "
            "that names a representative profile per platform.")
    endif()
endforeach()


# The working loop. jit_lockstep_test is the real proof — it compares the
# JIT against the interpreter register by register at every instruction
# boundary — and the two q605 etalons show the machine still reaches the
# Finder on the accelerated DEFAULT and the explicit interpreter oracle.
# Anything a JIT/default-policy change can break shows up here.
# The x64-pinned lockstep pair is host-conditional (2026-09-03, the
# PRODUCT_LLE impostor audit) — on other hosts it lives in the absent
# roster, and a set_property on an unregistered TEST is a configure error.
set(pom68k_smoke_gates jit_backend_test jit_lockstep_test
    jit_lockstep_blocks_test jit_lockstep_noaccess_test
    q605_boot_etalon interp_q605_boot_etalon)
if(POM68K_JIT_NATIVE_BACKEND STREQUAL "x64")
    list(APPEND pom68k_smoke_gates jit_lockstep_x64_test jit_lockstep_x64_fine_test)
endif()
set_property(TEST ${pom68k_smoke_gates} APPEND PROPERTY LABELS smoke)
if(TEST jit_lockstep_a64_coarse_test)
    set_property(TEST jit_lockstep_a64_coarse_test APPEND PROPERTY LABELS smoke)
endif()

# Fast, asset-free native-JIT tier. Every registration here must fail,
# never soft-skip, on the A64/x64 CI hosts: jit_backend_test requires the
# compiled host generator, the copyback trio executes the IR-derived
# single/pair proof protocols, and the two standalone checkers keep the
# environment/document surfaces coherent. TIMEOUT is the deliberately
# coarse CI performance budget; semantic budgets (zero fallback, native
# cache-hit/fallback counters, bounded IR size) live inside the gates.
set_property(TEST jit_backend_test
                  jit_asset_free_lockstep_test
                  jit_copyback_write_040_test
                  jit_copyback_bsr_040_test
                  jit_copyback_pair_040_test
                  docs_test config_test store_inventory_test
             APPEND PROPERTY LABELS jit-fast)
set_tests_properties(jit_backend_test
                     jit_asset_free_lockstep_test
                     jit_copyback_write_040_test
                     jit_copyback_bsr_040_test
                     jit_copyback_pair_040_test
                     docs_test config_test store_inventory_test
                     PROPERTIES TIMEOUT 45)
# One measured exception, set AFTER the blanket that would overwrite it
# (a TIMEOUT set at add_test time in Pom68kJitGates.cmake was silently
# stomped here — nightly 33683370177 timed the gate out at 45.04 s
# TWICE): the full synthetic lockstep runs ~6 s native but 38.7 s under
# AppleClang ASan and past 45 s under GCC ASan on a CI runner. 300 is
# the instrumentation's honest ×7-8 with margin, not a loosened budget
# for the native tier, whose other gates stay within the blanket.
set_tests_properties(jit_asset_free_lockstep_test PROPERTIES TIMEOUT 300)

# ── Every gate is BOUNDED, declared or derived ───────────────────────
# A gate with no TIMEOUT is not a gate, it is a bet — and CTest takes no
# side of it here: this tree calls `enable_testing()` without
# `include(CTest)`, so there is no `DartConfiguration.tcl` and no default
# ceiling. An undeclared gate that wedges runs until a human notices.
#
# The cost of learning that, 2026-08-29: `ctest -L m030`, under a Moira
# change that let generated code into the pre-MMU 68030 boot for the first
# time, wedged twenty gates — `lc3plus_`, `lc550_`, `cclassic2_`, `lc520_`,
# `iici_`, `duo230_`, `iisi_`, `iivx_`, `lc3_`, `mactv_`, `classic2_`,
# `cclassic_`, `lcii_sys7_`, `lcii_savestate_`, `iifx_post_`… — and because
# not one of them declared a TIMEOUT they held sixteen cores at load 20 for
# ELEVEN HOURS. In the same run the gates that DID declare one reported
# `***Timeout` and freed their slot: the difference between a red and a
# hostage was one property.
#
# Derived, never overriding: a gate that declares its own ceiling keeps it
# (`jit-fast` at 45 s just above, the locksteps at 1800, the `jit_*` boot
# etalons at 3600). The derived values are deliberately generous — this is
# a STOP, not a performance budget; budgets live in
# `performance_budgets.tsv` and inside the gates.
set(POM68K_GATE_TIMEOUT_ETALON 1800)   # the value the etalons that declare
set(POM68K_GATE_TIMEOUT_OTHER   600)   # one already use; ~2× the slowest
                                       # non-etalon gate measured here
foreach(t IN LISTS pom68k_tests)
    get_test_property(${t} TIMEOUT pom68k_declared_timeout)
    if(NOT pom68k_declared_timeout)
        if(t MATCHES "etalon$")
            set_tests_properties(${t} PROPERTIES
                                 TIMEOUT ${POM68K_GATE_TIMEOUT_ETALON})
        else()
            set_tests_properties(${t} PROPERTIES
                                 TIMEOUT ${POM68K_GATE_TIMEOUT_OTHER})
        endif()
    endif()
endforeach()
# …and the invariant, checked rather than trusted: after the loop above no
# registered gate may be unbounded. This cannot fail today by construction,
# which is the point — it fails the day someone registers a gate outside
# this file's reach.
foreach(t IN LISTS pom68k_tests)
    get_test_property(${t} TIMEOUT pom68k_final_timeout)
    if(NOT pom68k_final_timeout)
        message(FATAL_ERROR
            "gate '${t}' carries no TIMEOUT. Every gate must be bounded — "
            "see the eleven-hour m030 wedge of 2026-08-29 in "
            "cmake/Pom68kGatePolicy.cmake.")
    endif()
endforeach()

# Gate contract manifest. `unit` predates the asset model and only means
# "not named *_etalon"; it is intentionally retained for compatibility,
# but CI and tooling now consume these orthogonal, exhaustive dimensions.
# The short exception list names component gates whose useful path needs a
# user-provided corpus, firmware or disk despite not being an etalon.
#
# A gate belongs here the moment its useful path reads a private byte —
# `asset-none` is a CLAIM the CI holds to account with
# `tools/gate_execution_census.py --fail-on-skip`, not a default. Three
# gates carried the wrong claim until 2026-09-02 and made that step
# unsatisfiable on any host without the private inputs: `ncr5380_test`
# needs `hdv/HD20SC.vhd`, and `cuda_restart_test`/`m68hc05_test` need the
# Cuda MCU dump `roms/cuda/341s0788.bin`. Each soft-skipped loudly on a
# bare GitHub runner while sitting in the tier that forbids soft-skips, so
# the Linux job had been red on every push since 2026-08-27. The defect was
# the label, never the runner: a gate that needs a private asset is not
# asset-free, and the census was right to refuse.
set(POM68K_OPTIONAL_ASSET_GATES
    sst68000 sst68030 sst68040
    cuda_lle_test egret_lle_test m68hc05_test cuda_restart_test
    scsi_disk_test ncr5380_test ncr53c96_test scsi_pdma_test
    scsi_hfs_facade_test sound_test
    savestate_030_test savestate_68k_test)
# ── What a gate COSTS, so `ctest -j` can schedule the suite ──────────
# Same shape as performance_budgets.tsv: a versioned, per-host, reviewed
# manifest rather than literals scattered through the registry. Read
# `gate_resource_budgets.tsv` for why RAM is the binding resource and why
# PROCESSORS is used rather than the more precise RESOURCE_GROUPS.
# A gate with no row is ONE slot — today's behaviour exactly — and the
# manifest records that as `assumed`, so an uncalibrated tier can never
# read as a cheap one.
set(POM68K_GATE_SLOT_KB 262144)          # 256 MiB per scheduling slot
set(POM68K_GATE_BUDGET_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/gate_resource_budgets.tsv")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/gate_resource_budgets.tsv")
file(STRINGS "${POM68K_GATE_BUDGET_FILE}" pom68k_gate_budget_lines)
foreach(line IN LISTS pom68k_gate_budget_lines)
    if(line MATCHES "^[ \t]*#" OR line STREQUAL "")
        continue()
    endif()
    string(REPLACE "\t" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 4)
        message(FATAL_ERROR
            "gate_resource_budgets.tsv: expected four columns: ${line}")
    endif()
    list(GET fields 0 budget_gate)
    list(GET fields 1 budget_host)
    list(GET fields 2 budget_kb)
    if(NOT budget_kb MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "gate_resource_budgets.tsv: non-integer peak_rss_kb: ${line}")
    endif()
    set("POM68K_GATE_RAM_${budget_gate}_${budget_host}" "${budget_kb}")
endforeach()

set(pom68k_gate_manifest
    "name\tassets\thost\tscope\ttier\tslots\tslots_src\n")
foreach(t IN LISTS pom68k_tests)
    if(t MATCHES "etalon$" OR
       (t MATCHES "^jit_lockstep" AND
        NOT t STREQUAL "jit_asset_free_lockstep_test"))
        set(gate_assets required)
    elseif(t IN_LIST POM68K_OPTIONAL_ASSET_GATES)
        set(gate_assets optional)
    else()
        set(gate_assets none)
    endif()

    if(t MATCHES "(^|_)a64(_|$)" OR t MATCHES "^lle_a64_")
        set(gate_host a64)
    elseif(t MATCHES "(^|_)x64(_|$)")
        set(gate_host x64)
    elseif(t MATCHES "^jit_(asset_free|copyback)")
        set(gate_host native)
    else()
        set(gate_host any)
    endif()

    get_test_property(${t} LABELS gate_contract_labels)
    if(t MATCHES "etalon$")
        set(gate_scope profile)
    elseif(t STREQUAL "docs_test" OR t STREQUAL "config_test" OR
           t STREQUAL "store_inventory_test")
        set(gate_scope repository)
    elseif(t MATCHES "^jit_")
        set(gate_scope engine)
    else()
        set(gate_scope component)
    endif()
    if("jit-fast" IN_LIST gate_contract_labels OR gate_assets STREQUAL "none")
        set(gate_tier daily)
    elseif("etalon-core" IN_LIST gate_contract_labels)
        set(gate_tier platform)
    else()
        set(gate_tier full)
    endif()

    # Scheduling cost. Prefer this host's own measurement, fall back to
    # a host-agnostic row, and otherwise assume one slot.
    set(gate_ram_key "POM68K_GATE_RAM_${t}_${POM68K_PERF_HOST}")
    set(gate_ram_any "POM68K_GATE_RAM_${t}_any")
    if(DEFINED ${gate_ram_key})
        set(gate_ram_kb "${${gate_ram_key}}")
        set(gate_slots_src measured)
    elseif(DEFINED ${gate_ram_any})
        set(gate_ram_kb "${${gate_ram_any}}")
        set(gate_slots_src measured)
    else()
        set(gate_ram_kb 0)
        set(gate_slots_src assumed)
    endif()
    # ceil(kb / slot) with integer arithmetic, floored at one slot.
    math(EXPR gate_slots
         "(${gate_ram_kb} + ${POM68K_GATE_SLOT_KB} - 1) / ${POM68K_GATE_SLOT_KB}")
    if(gate_slots LESS 1)
        set(gate_slots 1)
    endif()

    set_property(TEST ${t} APPEND PROPERTY
                 LABELS "asset-${gate_assets}" "host-${gate_host}"
                        "scope-${gate_scope}" "tier-${gate_tier}")
    set_property(TEST ${t} PROPERTY POM68K_ASSETS ${gate_assets})
    set_property(TEST ${t} PROPERTY POM68K_HOST ${gate_host})
    set_property(TEST ${t} PROPERTY POM68K_SCOPE ${gate_scope})
    set_property(TEST ${t} PROPERTY POM68K_TIER ${gate_tier})
    set_property(TEST ${t} PROPERTY PROCESSORS ${gate_slots})
    string(APPEND pom68k_gate_manifest
           "${t}\t${gate_assets}\t${gate_host}\t${gate_scope}\t${gate_tier}\t${gate_slots}\t${gate_slots_src}\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/pom68k_gate_manifest.tsv
     "${pom68k_gate_manifest}")

# The registry, as a file `docs_test` can read: one line per gate,
# "<name>\t<label>,<label>". CTest has no way to hand a test the roster,
# and a documentation gate that cannot see the roster can only check the
# docs against themselves.
set(pom68k_gate_list "")
foreach(t IN LISTS pom68k_tests)
    get_test_property(${t} LABELS tlabels)
    if(NOT tlabels)
        set(tlabels "")
    endif()
    string(REPLACE ";" "," tlabels "${tlabels}")
    string(APPEND pom68k_gate_list "${t}\t${tlabels}\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates.tsv "${pom68k_gate_list}")
# Empty on the AArch64 dev host, two rows anywhere else — see the guard
# above. docs_test adds these back before comparing with the docs.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/pom68k_gates_absent.tsv
     "${pom68k_absent_gates}")

# `make jitdev` builds ONLY what `ctest -L smoke` needs. The tree-wide LTO
# makes a full `make` relink ~90 binaries after any core change; this is
# three. Pair them: `make -j4 jitdev && ctest -L smoke`.
add_custom_target(jitdev DEPENDS
                  jit_backend_test jit_lockstep_test q605_boot_etalon)

# Pair with `ctest -L jit-fast`: eight registrations, six small binaries,
# no ROM/disk/SST corpus and no GUI dependency.
add_custom_target(jitfast DEPENDS
                  jit_backend_test jit_asset_free_lockstep_test
                  jit_copyback_write_040_test
                  docs_test config_test store_inventory_test)

