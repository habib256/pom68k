# Performance-budget support, CPU/JIT gates and product qualification.
# Included after machine gates so interpreter/JIT variants can reuse targets.

# Versioned performance budgets. CMake validates the manifest and turns
# each consumed value into a compile-time constant; tests therefore fail
# against reviewed policy rather than private literals in their source.
set(POM68K_PERF_BUDGET_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/performance_budgets.tsv")
# file(STRINGS) does NOT register a configure dependency, so editing the
# manifest changed nothing until some unrelated CMakeLists edit forced a
# reconfigure — a policy file whose edits are silently inert. Caught on
# 2026-08-18 by the bench's own provenance line: a run printed the OLD
# noise floor next to a build stamp minutes older than the TSV
# (docs/MEASURING.md § 2, why the binary must identify itself).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${POM68K_PERF_BUDGET_FILE}")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" POM68K_PERF_HOST)
if(POM68K_PERF_HOST MATCHES "^(arm64|aarch64)$")
    set(POM68K_PERF_HOST "aarch64")
elseif(POM68K_PERF_HOST MATCHES "^(x86_64|amd64)$")
    set(POM68K_PERF_HOST "x86_64")
else()
    set(POM68K_PERF_HOST "other")
endif()
file(STRINGS "${POM68K_PERF_BUDGET_FILE}" pom68k_perf_budget_lines)
foreach(line IN LISTS pom68k_perf_budget_lines)
    if(line MATCHES "^[ \t]*#" OR line STREQUAL "")
        continue()
    endif()
    string(REPLACE "\t" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 6)
        message(FATAL_ERROR
            "performance_budgets.tsv: expected six columns: ${line}")
    endif()
    list(GET fields 0 workload)
    list(GET fields 1 cpu_family)
    list(GET fields 2 perf_host)
    list(GET fields 3 metric)
    list(GET fields 4 value)
    if(NOT value MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "performance_budgets.tsv: non-integer value: ${line}")
    endif()
    set("POM68K_PERF_${workload}_${cpu_family}_${perf_host}_${metric}"
        "${value}")
endforeach()
function(pom68k_perf_budget target workload cpu_family metric macro)
    set(key "POM68K_PERF_${workload}_${cpu_family}_${POM68K_PERF_HOST}_${metric}")
    set(fallback "POM68K_PERF_${workload}_${cpu_family}_any_${metric}")
    if(DEFINED ${key})
        set(value "${${key}}")
    elseif(DEFINED ${fallback})
        set(value "${${fallback}}")
    else()
        message(FATAL_ERROR
            "performance_budgets.tsv: missing ${workload}/${cpu_family}/"
            "${POM68K_PERF_HOST}/${metric}")
    endif()
    target_compile_definitions(${target} PRIVATE
        "${macro}=${value}")
endfunction()

# Daily native lockstep with no Apple assets: a deterministic flat-RAM
# 68040 program compares complete architectural boundaries and RAM, then
# injects both restart and last-write bus faults. `jit-fast` makes native
# host execution mandatory on the A64 and x64 CI runners.
add_executable(jit_asset_free_lockstep_test
               tests/jit_asset_free_lockstep_test.cpp)
target_link_libraries(jit_asset_free_lockstep_test PRIVATE pom68k_core)
pom68k_perf_budget(jit_asset_free_lockstep_test
    synthetic_68040_lockstep 68040 min_blocks_compiled
    POM68K_PERF_MIN_BLOCKS_COMPILED)
pom68k_perf_budget(jit_asset_free_lockstep_test
    synthetic_68040_lockstep 68040 min_blocks_run
    POM68K_PERF_MIN_BLOCKS_RUN)
pom68k_perf_budget(jit_asset_free_lockstep_test
    synthetic_68040_lockstep 68040 min_native_share_permille
    POM68K_PERF_MIN_NATIVE_SHARE_PERMILLE)
pom68k_perf_budget(jit_asset_free_lockstep_test
    synthetic_68040_lockstep 68040 max_slow_instrs
    POM68K_PERF_MAX_SLOW_INSTRUCTIONS)
add_test(NAME jit_asset_free_lockstep_test COMMAND jit_asset_free_lockstep_test)
set_tests_properties(jit_asset_free_lockstep_test PROPERTIES LABELS "jit;unit")

# J4 write half: one generated copyback hit must publish the precise
# dirty-longword mask, while misses retain the 68040 format-$7
# last-write/restart dichotomy. Runs on whichever native host backend is
# available and soft-skips on hosts that only have the threaded engine.
add_executable(jit_copyback_write_040_test
               tests/jit_copyback_write_040_test.cpp)
target_link_libraries(jit_copyback_write_040_test PRIVATE pom68k_core)
pom68k_perf_budget(jit_copyback_write_040_test
    synthetic_68040_copyback 68040 max_slow_instrs
    POM68K_PERF_COPYBACK_MAX_SLOW_INSTRUCTIONS)
pom68k_perf_budget(jit_copyback_write_040_test
    synthetic_68040_copyback 68040 max_native_ratio_permille
    POM68K_PERF_COPYBACK_MAX_RATIO_PERMILLE)
pom68k_perf_budget(jit_copyback_write_040_test
    synthetic_68040_copyback 68040 native_slack_microseconds
    POM68K_PERF_COPYBACK_SLACK_US)
add_test(NAME jit_copyback_write_040_test COMMAND jit_copyback_write_040_test)
set_tests_properties(jit_copyback_write_040_test PROPERTIES LABELS "jit;unit")
# The attribution switch is itself contractual: OFF must replay the exact
# cache-aware write rather than exposing stale backing RAM through DTLB.
add_test(NAME jit_copyback_write_040_control_test
         COMMAND jit_copyback_write_040_test --disabled)
set_tests_properties(jit_copyback_write_040_control_test PROPERTIES
                     LABELS "jit;unit")
# Highest-yield real consumer after the paired census: BSR's sole
# return-address push, including dirty publication and /BERR stack switch.
add_test(NAME jit_copyback_bsr_040_test
         COMMAND jit_copyback_write_040_test --bsr)
set_tests_properties(jit_copyback_bsr_040_test PROPERTIES
                     LABELS "jit;unit")
# The remaining hot MOVE pair needs two cache proofs before either
# access becomes visible. The OFF twin pins whole-instruction replay.
add_test(NAME jit_copyback_pair_040_test
         COMMAND jit_copyback_write_040_test --pair)
set_tests_properties(jit_copyback_pair_040_test PROPERTIES
                     LABELS "jit;unit")
add_test(NAME jit_copyback_pair_040_control_test
         COMMAND jit_copyback_write_040_test --pair-disabled)
set_tests_properties(jit_copyback_pair_040_control_test PROPERTIES
                     LABELS "jit;unit")

# AArch64 68040 gate for opcode-local removal of the conservative store
# fallback. It proves both halves of the contract: mask-null ordinary RAM
# is direct, while a true overlap with translated code is observed by the
# memory map and precisely invalidates the cached block. Soft-skips away
# from AArch64.
add_executable(jit_store_guard_a64_test tests/jit_store_guard_a64_test.cpp)
target_link_libraries(jit_store_guard_a64_test PRIVATE pom68k_core)
add_test(NAME jit_store_guard_a64_test COMMAND jit_store_guard_a64_test)
set_tests_properties(jit_store_guard_a64_test PROPERTIES LABELS "jit;unit")

# M1-M3 gate (docs/CACHE_040.md): 68040 cache geometry and line data,
# CACR/CM/TTR policy, copyback/CPUSH/CINV, snooping and hit/fill timing.
add_executable(cache040_test tests/cache040_test.cpp)
target_link_libraries(cache040_test PRIVATE moira)
add_test(NAME cache040_test COMMAND cache040_test)

# O5 gate: hand-computed 68882 smoke cases (FMOVECR/FADD/DZ/OPERR/
# FCMP/FMOVEM, format-$9 IRQ + BUSY resume, detached F-line). The corpus
# through sst68030 (tests/data/sst68030_fpu, generated by a parallel
# fuzz run) once available.
add_executable(fpu_sanity tests/fpu_sanity.cpp)
target_link_libraries(fpu_sanity PRIVATE moira)
add_test(NAME fpu_sanity COMMAND fpu_sanity)

add_executable(fpu040_test tests/fpu040_test.cpp)
target_link_libraries(fpu040_test PRIVATE moira)
add_test(NAME fpu040_test COMMAND fpu040_test)

add_executable(callm_rtm_test tests/callm_rtm_test.cpp)
target_link_libraries(callm_rtm_test PRIVATE moira)
add_test(NAME callm_rtm_test COMMAND callm_rtm_test)

# ── JIT gates (src/jit/POM68K_JIT.md) ────────────────────────────────
# The portability seam: backend selection, W^X executable memory, and
# the block classifier's safety rules. No assets, runs everywhere.
add_executable(jit_backend_test tests/jit_backend_test.cpp)
target_link_libraries(jit_backend_test PRIVATE pom68k_core)
add_test(NAME jit_backend_test COMMAND jit_backend_test)

# Backend acceptance parity over the whole opcode space (CHANGELOG.md
# 2026-08-28 third / 2026-09-01). Both generators compile on ANY host ISA —
# they only emit bytes into buffers, and this gate never executes them — so
# the sweep runs
# everywhere. The core library already holds this host's native backend;
# only the missing translation unit(s) are added here, or the factories
# would be defined twice.
set(POM68K_PARITY_EXTRA "")
if(NOT POM68K_JIT_NATIVE_BACKEND STREQUAL "x64")
    list(APPEND POM68K_PARITY_EXTRA src/jit/backends/JitBackendX64.cpp)
endif()
if(NOT POM68K_JIT_NATIVE_BACKEND STREQUAL "a64")
    list(APPEND POM68K_PARITY_EXTRA src/jit/backends/JitBackendA64.cpp)
endif()
add_executable(jit_backend_parity_test tests/jit_backend_parity_test.cpp
               ${POM68K_PARITY_EXTRA})
target_link_libraries(jit_backend_parity_test PRIVATE pom68k_core)
add_test(NAME jit_backend_parity_test COMMAND jit_backend_parity_test)

# The decisive one: two Quadra 605 machines from the same ROM, one
# interpreted and one JIT-driven, compared register by register at every
# instruction boundary. Fails if the JIT never actually ran a block, so
# a silent fallback cannot masquerade as a green gate.
add_executable(jit_lockstep_test tests/jit_lockstep_test.cpp)
target_link_libraries(jit_lockstep_test PRIVATE pom68k_core)
add_test(NAME jit_lockstep_test COMMAND jit_lockstep_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_test PROPERTIES TIMEOUT 1800)

# The portable backend with its block path forced on: `auto` runs it
# WITHOUT blocks (measured faster that way), so this is the only gate
# that exercises block discovery on the floor every platform has.
add_test(NAME jit_lockstep_blocks_test COMMAND jit_lockstep_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_blocks_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1"
                     TIMEOUT 1800)

# ── the cycle-exact lockstep (2026-08-06) ────────────────────────────
# The 68000 twin, and it asks a different question. On Core::C68020 —
# every other guest the engine reaches — SYNC(x) expands to nothing, so
# a windowed fetch cannot move the clock and the gates above compare
# architectural state. On Core::C68000 SYNC is real, and
# Moira::pomJitFetch000 has to re-charge two SYNC(2)s, POLL_IPL and the
# Mac Plus bus contention by hand. `clock` is therefore compared at
# every instruction boundary, alongside the VIA/IWM/drive/SCC state a
# cycle drift would move first.
#
# Coarse-then-fine, because the two halves cover different things. At
# one cycle per comparison the machine only reaches 26 M guest cycles in
# a couple of seconds — still inside the ROM's RAM test, where the
# window arms FOUR times and never dies. A 256-cycle budget covers 666 M
# cycles in eleven seconds: the whole POST, the GCR floppy read, the
# System load and the Finder, with 83 000 window arms. FINE_AT then
# drops the last 100 000 checkpoints to one cycle each, so the sharpest
# comparison happens in live Finder code rather than in a boot loop.
add_executable(jit_lockstep_68000_test tests/jit_lockstep_68000_test.cpp)
target_link_libraries(jit_lockstep_68000_test PRIVATE pom68k_core)
add_test(NAME jit_lockstep_68000_test COMMAND jit_lockstep_68000_test 2500000
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_68000_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_JIT_LOCKSTEP_FINE_AT=2400000"
                     TIMEOUT 1800)

# …and with the block path forced on. Recorded blocks are safe on a
# cycle-exact core only because the `threaded` backend replays through
# pomJitExecOne() — Moira's own handlers, charging Moira's own cycles —
# and never replays the recorded Instr::cycles. That is a property worth
# a gate, because a code generator doing the arithmetic itself would be
# wrong here in a way no architectural comparison would catch.
add_test(NAME jit_lockstep_68000_blocks_test COMMAND jit_lockstep_68000_test 2500000
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_68000_blocks_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_JIT_LOCKSTEP_FINE_AT=2400000"
                     TIMEOUT 1800)

# ── the 68030 lockstep (docs/JIT_BRINGUP.md § C.1) ───────────────────
# Every gate above runs two Quadra 605s, so until this one the 68030
# side of the engine had no instruction-level differential coverage at
# all — and that gap has already been paid for: on 2026-07-30 `auto`
# handed the x86-64 generator the 030 machines and jit_lcii_boot_etalon
# timed out after an HOUR, with nothing to say beyond "it did not
# arrive". A boot etalon cannot name an instruction; this can.
#
# It also compares the three PomIcache counters, which is the half a
# Quadra gate cannot have: Moira charges the 030 i-cache overlay inside
# mmuFetchWord, before the JIT window hook, so today both engines charge
# it identically — and a code generator emitting 030 blocks fetches no
# instructions and would charge nothing at all.
add_executable(jit_lockstep_030_test tests/jit_lockstep_030_test.cpp)
target_link_libraries(jit_lockstep_030_test PRIVATE pom68k_core)
add_test(NAME jit_lockstep_030_test COMMAND jit_lockstep_030_test 120000
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# The backend is PINNED to the host's native generator (2026-08-29): the
# harness used to reach it through `auto`, and the day the x64 030
# promotion was withdrawn this gate silently became a second threaded
# lockstep — green, and proving nothing blocks_test does not. The IIsi
# lesson verbatim: an auto gate that quietly runs `threaded` is how a
# generator defect stays invisible.
if(POM68K_JIT_NATIVE_BACKEND)
    set(pom68k_lockstep_030_backend
        "POM68K_JIT_BACKEND=${POM68K_JIT_NATIVE_BACKEND};")
else()
    set(pom68k_lockstep_030_backend "")
endif()
set_tests_properties(jit_lockstep_030_test PROPERTIES
                     ENVIRONMENT "${pom68k_lockstep_030_backend}POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000"
                     TIMEOUT 1800)

# …and with the block path forced on, same argument as the 68000 pair
# above: `threaded` replays through pomJitExecOne(), so it charges
# Moira's own cycles and the i-cache along with them. A future 030 code
# generator will not, which is exactly what this variant exists to
# notice.
add_test(NAME jit_lockstep_030_blocks_test COMMAND jit_lockstep_030_test 120000
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_030_blocks_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000"
                     TIMEOUT 1800)

# Apple-Silicon release gate. A coarse budget exercises complete native
# blocks, linked exits and peripheral synchronization; five million
# comparisons cover every late-boot A64 regression found during bring-up.
# This is also the permanent architectural-cache/J4 gate: the diagnostic
# counter makes a green run with zero native physical-line hits fail.
if(POM68K_JIT_NATIVE_BACKEND STREQUAL "a64")
    add_test(NAME jit_lockstep_a64_coarse_test COMMAND jit_lockstep_test 5000000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_a64_coarse_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=a64;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=50;POM68K_040_DCACHE=1;POM68K_JIT_040_LINE_STATS=1"
                         TIMEOUT 1800)

    # Apple-Silicon 68030 gate: unlike the portable pair above, this one
    # exercises generated arm64 code with production HOT=1. Its budget
    # is one real LC II frame: the old 8192-cycle cadence hid a native
    # two-memory MOVE corruption by changing the outer run boundary.
    # Six thousand frames also pin the fixed-cycle benchmark fingerprint.
    add_test(NAME jit_lockstep_030_a64_experimental_test COMMAND jit_lockstep_030_test 6000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_030_a64_experimental_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=a64;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=260480"
                         TIMEOUT 1800)

    # The AArch64 twin of the x64 alignment gate (2026-08-22, JIT_BRINGUP
    # § C.4nonies): the 120k lockstep at the fine-grained cadence with
    # BOTH § C.4nonies admissions explicitly on. pom68kA64Read/Write
    # carry the access-clock bias since the same day (replacing the
    # guardIcacheHits replay) and the backend declares it, so the
    # experimental gate above runs these admissions by DEFAULT; this one
    # pins the explicit-knob road beside it, as on x86-64.
    # POM68K_JIT_030_MEMBF joined the pinned explicit knobs on 2026-08-28:
    # 030 memory bitfields through the shared emission path, first proved on
    # this gate's configuration (120k identical, i-cache counters identical)
    # and promoted after the cross-backend E9D4 tail/no-tail oracle. This
    # explicit-1 registration remains the standing non-default configuration.
    add_test(NAME jit_lockstep_030_a64_alignment_test COMMAND jit_lockstep_030_test 120000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_030_a64_alignment_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=a64;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000;POM68K_JIT_RESTART_BASE=1;POM68K_JIT_BSRW=1;POM68K_JIT_030_MEMBF=1"
                         TIMEOUT 1800)
else()
    # The a64 pair are host-conditional gates: the registry has more
    # than one size and the docs cannot state one number. Record what
    # this host is missing, with the labels the derivation loop below
    # would have given them (both: `unit` + `jit`; the coarse one also
    # takes `smoke` explicitly), so `docs_test` can hold the documented
    # totals to account on any host instead of failing everywhere else.
    # Adding another conditional gate means adding it here — and
    # docs_test says so in its own failure text. (2026-08-12)
    set(pom68k_absent_gates
        "jit_lockstep_a64_coarse_test\tunit,jit,smoke\njit_lockstep_030_a64_experimental_test\tunit,jit\njit_lockstep_030_a64_alignment_test\tunit,jit\n")
endif()

# The x86-64 twin of the 030 experimental gate — first green 2026-08-18,
# after the taken-short-branch IRC fix and the 030 base-charge rule; it
# exists so that state cannot silently rot the way its absence let the
# IRC defect sit unregistered. Same shape: generated x64 code selected
# explicitly; 030 correctness is declared, while automatic selection is
# still the separate, measured C.5 decision.
if(POM68K_JIT_NATIVE_BACKEND STREQUAL "x64")
    add_test(NAME jit_lockstep_030_x64_experimental_test COMMAND jit_lockstep_030_test 120000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_030_x64_experimental_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=x64;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000"
                         TIMEOUT 1800)

    # Packed XNZVC crosses every helper boundary. Its first real 030 audit
    # found two independent defects hidden by the 040-only synthetic proof:
    # the helper wrapper clobbered RAX's fault result, then exact device
    # reads skipped the post-instruction pacing rendezvous. Keep the same
    # 120k LC II oracle permanently attached to this experimental layout.
    add_test(NAME jit_lockstep_030_x64_packed_ccr_test
             COMMAND jit_lockstep_030_test 120000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_030_x64_packed_ccr_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=x64;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_PACKED_CCR=1;POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000"
                         TIMEOUT 1800)

    # The peripheral-phase alignment gate (2026-08-21, JIT_BRINGUP
    # § C.4nonies): the same 120k lockstep with BOTH admissions
    # explicitly on — the restartable-write base cost and BSR.W. Each
    # used to diverge on delivery-boundary alignment (steps
    # 19658/16097); the access-thunk clock bias closed the class, and
    # this gate keeps it closed. Since the 2026-08-22 per-backend flip
    # the admissions are also x64's DEFAULT (caps().accessClockBias),
    # so the experimental gate above now runs the same admissions via
    # the default path and the two gates pin both roads to the same
    # 120k-identical answer.
    add_test(NAME jit_lockstep_030_x64_alignment_test COMMAND jit_lockstep_030_test 120000
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_lockstep_030_x64_alignment_test PROPERTIES
                         ENVIRONMENT "POM68K_JIT_BACKEND=x64;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1;POM68K_JIT_LOCKSTEP_BUDGET=8192;POM68K_JIT_LOCKSTEP_FINE_AT=110000;POM68K_JIT_RESTART_BASE=1;POM68K_JIT_BSRW=1"
                         TIMEOUT 1800)
else()
    string(APPEND pom68k_absent_gates
        "jit_lockstep_030_x64_experimental_test\tunit,jit\n")
    string(APPEND pom68k_absent_gates
        "jit_lockstep_030_x64_packed_ccr_test\tunit,jit\n")
    string(APPEND pom68k_absent_gates
        "jit_lockstep_030_x64_alignment_test\tunit,jit\n")
endif()

# ── the x86-64 code generator ────────────────────────────────────────
# Every gate below names x64 so the generator remains covered even when
# the build or host changes its automatic backend choice.
# A WIDE cycle budget between checkpoints is deliberate: at one cycle a
# compiled block can never be more than one instruction long, and the
# interesting half of a code generator — long blocks, and a loop closing
# on itself entirely inside generated code — would go unexercised behind
# a green light.
# The wide x64 variant carries the same cache-on native-line proof; the
# fine and no-access variants below retain the default cacheless paths.
add_test(NAME jit_lockstep_x64_test COMMAND jit_lockstep_test 1000000
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_x64_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_BACKEND=x64;POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_040_DCACHE=1;POM68K_JIT_040_LINE_STATS=1"
                     TIMEOUT 1800)

# The same, one cycle at a time: the sharpest check there is, and the
# one that catches a wrong flag or a wrong cycle count immediately.
add_test(NAME jit_lockstep_x64_fine_test COMMAND jit_lockstep_test 200000
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_x64_fine_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_BACKEND=x64" TIMEOUT 1800)

# The conservative data path: no per-access thunk, so every address the
# inline TLB cannot serve hands the whole instruction back to Moira.
# One environment variable away from being the shipping behaviour, so it
# has to keep working.
add_test(NAME jit_lockstep_noaccess_test COMMAND jit_lockstep_test 400000
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_lockstep_noaccess_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_BACKEND=x64;POM68K_JIT_LOCKSTEP_BUDGET=256;POM68K_JIT_ACCESS_THUNK=0"
                     TIMEOUT 1800)

# The same boot etalons, same executables, driven by the JIT instead of
# the interpreter — the established "one binary, another environment"
# idiom used by iivi/iicx/centris610/lc580/quadra800 above. Green means
# the machine reached the identical Finder signature under the JIT.
foreach(gate q605_boot_etalon centris650_boot_etalon
             q630_boot_etalon q700_boot_etalon
             # 030 extension (2026-07-28): the V8 family through the same
             # engine — mmuFetchWord is the single 030 fetch choke point.
             # mactv is here because it is the phase-fragile one: if the
             # window ever perturbed guest cycles, Tinker Bell's Cuda
             # transport would deadlock long before a signature failed.
             lcii_boot_etalon mactv_boot_etalon
             # …and one gate per additional 030 family (Sonora / VASP /
             # RBV): same engine, each map's own span/guard plumbing.
             lc3_boot_etalon iivx_boot_etalon iisi_boot_etalon
             # The 020 seam's guards. The JIT is measurably NOT worth
             # turning on for a plain 68020 (nothing to save: no MMU,
             # one cheap fetch per instruction) — the LC (V8 map +
             # as020) and the Mac II (GLUE map) are here so the seam
             # stays CORRECT for the day a code generator gives it
             # something to win with.
             lc_boot_etalon macii_boot_etalon
             # …and the Mac II family's 030 half, which is the case
             # that DOES pay: same ATC probe and mmuFetchWord choke
             # point as the V8/Sonora/VASP/RBV 030s, on the GLUE map.
             # se30 rather than iix because it also exercises the
             # pseudo-slot video path while the window is armed.
             se30_boot_etalon
             # The compacts (2026-08-06). These are the gates that
             # matter most of the whole list, because this is the one
             # family where the window is NOT free of cycle accounting:
             # Moira::pomJitFetch000 has to charge what read<> would,
             # contention included. A Mac Plus whose fetch cycles drifted
             # would not fail with a wrong pixel, it would fail with a
             # sound tempo or an IWM hold — hence both models: the Plus
             # (VIA PA4 overlay, M0110) and — just below, because it is
             # the one gate whose binary is shared — the Classic (ADB,
             # and the overlay drops on the first low-RAM WRITE).
             system_boot_etalon
             # The IIfx (2026-08-06), which completes the set: every CPU
             # wrapper in the tree now carries an engine. Its map is the
             # easiest the window has met — 32-bit clean, no HMMU, no
             # GLUE remap — but its overlay drops on a ROM-region READ,
             # which is the one map move a write guard cannot see, so
             # this gate is really about that.
             iifx_boot_etalon)
    add_test(NAME jit_${gate} COMMAND ${gate}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(jit_${gate} PROPERTIES
                         ENVIRONMENT "POM68K_CPU_ENGINE=jit" TIMEOUT 3600)
endforeach()

# Unlike the generic engine gates above, these are the product evidence
# for the 68030 CODE GENERATOR. Pin the compiled host backend explicitly
# and make a selection/W^X fallback fatal: an `auto` gate that quietly ran
# `threaded` is how the IIsi generator crash remained invisible.
if(POM68K_JIT_NATIVE_BACKEND)
    foreach(gate lcii_boot_etalon mactv_boot_etalon lc3_boot_etalon
                 iivx_boot_etalon iisi_boot_etalon se30_boot_etalon
                 iifx_boot_etalon)
        set_tests_properties(jit_${gate} PROPERTIES
            ENVIRONMENT
                "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=${POM68K_JIT_NATIVE_BACKEND};POM68K_JIT_REQUIRE_NATIVE=1")
    endforeach()
endif()

# The plain 68040 etalons now exercise the accelerated default. Preserve
# one explicit interpreter oracle per 68040 platform: a default is only
# conformant while the old reference remains independently runnable and
# green on the exact same executable and assets.
# The same debt came due on 2026-08-18, when `threaded` became the 68030
# default: one interpreter oracle per 68030 PLATFORM (six of them —
# V8, Sonora, VASP, RBV, IIfx, MSC), not per profile, because what the
# oracle pins is a memory map and an engine policy, not a clock.
foreach(gate q605_boot_etalon centris650_boot_etalon
             q630_boot_etalon q700_boot_etalon
             lcii_boot_etalon lc3_boot_etalon iivx_boot_etalon
             iisi_boot_etalon iifx_boot_etalon duo230_boot_etalon)
    add_test(NAME interp_${gate} COMMAND ${gate}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(interp_${gate} PROPERTIES
                         ENVIRONMENT "POM68K_CPU_ENGINE=interp" TIMEOUT 3600)
endforeach()

# Product qualification is intentionally separate from the public
# soft-skip etalons: enabling it promises that CI provisioned every
# private ROM and firmware dump. Missing assets are hard failures.
if(POM68K_PRODUCT_LLE_GATES)
    if(NOT TARGET POM68K)
        message(FATAL_ERROR "POM68K_PRODUCT_LLE_GATES requires the GUI executable")
    endif()
    if(NOT POM68K_JIT_NATIVE_BACKEND STREQUAL "a64")
        message(FATAL_ERROR "POM68K_PRODUCT_LLE_GATES requires the native AArch64 backend")
    endif()
    set(lle_q605_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM")
    set(lle_centris_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM")
    set(lle_q700_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM")
    set(lle_q630_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM")
    set(lle_boot_disk "${CMAKE_CURRENT_SOURCE_DIR}/hdv/MacOS-8.1-boot.vhd")
    foreach(asset "${lle_q605_rom}" "${lle_centris_rom}" "${lle_q700_rom}" "${lle_q630_rom}"
                  "${lle_boot_disk}"
                  "${CMAKE_CURRENT_SOURCE_DIR}/roms/cuda/341s0788.bin"
                  "${CMAKE_CURRENT_SOURCE_DIR}/roms/cuda/341s0060.bin"
                  "${CMAKE_CURRENT_SOURCE_DIR}/roms/adbmodem/342s0440-b.bin")
        if(NOT EXISTS "${asset}")
            message(FATAL_ERROR "LLE product asset missing: ${asset}")
        endif()
    endforeach()
    # q900 joined this list on 2026-08-14, when the Eclipse gained its
    # Egret 341S0851 firmware LLE: until then it was the one profile
    # product mode refused on principle (`lle_a64_q900_refused`), because
    # its ADB/RTC MCU was a command-level model no dump could replace.
    foreach(pair
            "q605|${lle_q605_rom}|"
            "centris|${lle_centris_rom}|POM68K_CENTRIS_MODEL=c650"
            "q700|${lle_q700_rom}|POM68K_Q700_MODEL=q700"
            "q900|${lle_q700_rom}|POM68K_Q700_MODEL=q900"
            "q630|${lle_q630_rom}|POM68K_Q630_ID=A55A2252")
        string(REPLACE "|" ";" fields "${pair}")
        list(GET fields 0 profile)
        list(GET fields 1 rom_path)
        add_test(NAME lle_a64_${profile}_preflight
                 COMMAND POM68K --lle-aarch64-check "${rom_path}")
        list(LENGTH fields field_count)
        if(field_count GREATER 2)
            list(GET fields 2 profile_env)
            if(profile_env)
                set_tests_properties(lle_a64_${profile}_preflight PROPERTIES
                                     ENVIRONMENT "${profile_env}")
            endif()
        endif()
        set_tests_properties(lle_a64_${profile}_preflight PROPERTIES
                             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                             LABELS "product;lle;a64" TIMEOUT 60)
    endforeach()
    add_test(NAME lle_a64_forced_hle_refused
             COMMAND POM68K --lle-aarch64-check "${lle_q605_rom}")
    set_tests_properties(lle_a64_forced_hle_refused PROPERTIES
                         ENVIRONMENT "POM68K_CUDA_LLE=0" WILL_FAIL TRUE
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                         LABELS "product;lle;a64" TIMEOUT 60)
    add_test(NAME lle_a64_missing_firmware_refused
             COMMAND POM68K --lle-aarch64-check "${lle_q605_rom}")
    set_tests_properties(lle_a64_missing_firmware_refused PROPERTIES
                         ENVIRONMENT "POM68K_FIRMWARE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/no-firmware"
                         WILL_FAIL TRUE WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                         LABELS "product;lle;a64" TIMEOUT 60)
    # The Eclipse's own forced-HLE refusal — the successor to
    # `lle_a64_q900_refused`, which asserted the whole profile was
    # unqualifiable. Now that its Egret runs real firmware, what must
    # stay refused is the FALLBACK: POM68K_EGRET_LLE=0 on the tower.
    add_test(NAME lle_a64_q900_forced_hle_refused
             COMMAND POM68K --lle-aarch64-check "${lle_q700_rom}")
    set_tests_properties(lle_a64_q900_forced_hle_refused PROPERTIES
                         ENVIRONMENT "POM68K_Q700_MODEL=q900;POM68K_EGRET_LLE=0"
                         WILL_FAIL TRUE
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                         LABELS "product;lle;a64" TIMEOUT 60)

    # Full interpreted and native boots consume the same executable and
    # assert the same Finder signature. The A64 variants name the backend
    # explicitly; `auto` is not accepted as product evidence.
    foreach(gate q605_boot_etalon centris650_boot_etalon
                 q630_boot_etalon q700_boot_etalon)
        set_tests_properties(jit_${gate} PROPERTIES
                             ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=a64")
        set_property(TEST jit_${gate} interp_${gate}
                     APPEND PROPERTY LABELS product lle a64-oracle)
    endforeach()
    set_property(TEST jit_lockstep_a64_coarse_test savestate_040_test
                 APPEND PROPERTY LABELS product lle a64-oracle)
endif()

# The ADB compact under the JIT. Out of the loop above because the
# compact family is "one binary, POM68K_COMPACT_MODEL picks the
# sibling" — the test NAME is not an executable name here, and the
# model variable has to ride along with the engine one.
add_test(NAME jit_classic_boot_etalon COMMAND compact_boot_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_classic_boot_etalon PROPERTIES
                     ENVIRONMENT "POM68K_COMPACT_MODEL=classic;POM68K_CPU_ENGINE=jit"
                     TIMEOUT 3600)
