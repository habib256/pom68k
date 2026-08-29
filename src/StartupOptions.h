// POM68K — typed schema for legacy process-environment startup options.
// Each spelling is declared exactly once here. Runtime decoders consume the
// typed constants; ProcessEnvironment derives its capture list from kAll.

#pragma once

#include "StartupValuePolicy.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace pom68k {

enum class StartupDomain : std::uint8_t {
    Product = 1,
    Core = 2,
    Machine = 4,
    Jit = 8,
};

constexpr StartupDomain operator|(StartupDomain left, StartupDomain right) {
    return StartupDomain(std::uint8_t(left) | std::uint8_t(right));
}

constexpr bool startupDomainIncludes(StartupDomain domains,
                                     StartupDomain requested) {
    return (std::uint8_t(domains) & std::uint8_t(requested)) != 0;
}

struct StartupOptionSpec {
    std::string_view name;
    StartupDomain domains;
    StartupValuePolicy value;
};

template <StartupDomain Domains, StartupValuePolicy Value>
struct StartupOption {
    std::string_view name;
    static constexpr StartupDomain domains = Domains;
    static constexpr StartupValuePolicy value = Value;
};

template <class Option>
concept StartupOptionType = requires {
    std::remove_cvref_t<Option>::domains;
    std::remove_cvref_t<Option>::value;
};

template <class Option, StartupDomain Domain>
concept StartupOptionFor =
    StartupOptionType<Option> &&
    startupDomainIncludes(std::remove_cvref_t<Option>::domains, Domain);

template <class Option>
concept BooleanStartupOption =
    StartupOptionType<Option> &&
    startupValueIsBoolean(std::remove_cvref_t<Option>::value.kind);

template <class Option>
concept IntegerStartupOption =
    StartupOptionType<Option> &&
    startupValueIsInteger(std::remove_cvref_t<Option>::value.kind);

#define STARTUP_OPTION_SCHEMA(X)                                             \
    X(NoFpu, "POM68K_NOFPU", StartupDomain::Product,                        \
      startup_policy::Presence)                                              \
    X(Q605NoFpu, "POM68K_Q605_NOFPU",                                      \
      StartupDomain::Product | StartupDomain::Core, startup_policy::Custom)  \
    X(AppleTalk, "POM68K_APPLETALK",                                       \
      StartupDomain::Product | StartupDomain::Core,                          \
      startup_policy::ExactZeroBoolean)                                      \
    X(LtoUdp, "POM68K_LTOUDP", StartupDomain::Product,                     \
      startup_policy::Presence)                                              \
    X(AppleTalkWireBoost, "POM68K_ATALK_WIRE_BOOST",                       \
      StartupDomain::Product, startup_policy::NonnegativeDecimalInteger)     \
    X(ShareDirectory, "POM68K_SHARE_DIR", StartupDomain::Product,          \
      startup_policy::Text)                                                  \
    X(Audio, "POM68K_AUDIO", StartupDomain::Product,                       \
      startup_policy::ExactZeroBoolean)                                      \
    X(DriveSounds, "POM68K_DRIVE_SFX", StartupDomain::Product,             \
      startup_policy::LeadingZeroBoolean)                                    \
    X(FloppyReadOnly, "POM68K_FLOPPY_RO", StartupDomain::Product,          \
      startup_policy::Presence)                                              \
    X(Floppy, "POM68K_FLOPPY", StartupDomain::Product,                     \
      startup_policy::Text)                                                  \
    X(Monitor, "POM68K_MONITOR", StartupDomain::Product,                   \
      startup_policy::DecimalInteger)                                        \
    X(FpuLog, "POM68K_FPU_LOG", StartupDomain::Product,                    \
      startup_policy::Text)                                                  \
    X(KeyTrace, "POM68K_KEY_TRACE", StartupDomain::Product,                \
      startup_policy::Presence)                                              \
    X(FreezeProbe, "POM68K_FREEZE_PROBE", StartupDomain::Product,          \
      startup_policy::Presence)                                              \
    X(SpeedLog, "POM68K_SPEED_LOG", StartupDomain::Product,                \
      startup_policy::EnabledBoolean)                                        \
    X(SpeedLogSkip, "POM68K_SPEED_LOG_SKIP", StartupDomain::Product,       \
      startup_policy::NonnegativeDecimalInteger)                             \
    X(SpeedLogCount, "POM68K_SPEED_LOG_COUNT", StartupDomain::Product,     \
      startup_policy::NonnegativeDecimalInteger)                             \
    X(LleAarch64Full, "POM68K_LLE_AARCH64_FULL", StartupDomain::Product,   \
      startup_policy::EnabledBoolean)                                        \
    X(LleAarch64CheckOnly, "POM68K_LLE_AARCH64_CHECK_ONLY",                \
      StartupDomain::Product, startup_policy::EnabledBoolean)                \
    X(MacIiModel, "POM68K_MACII_MODEL", StartupDomain::Machine,            \
      startup_policy::Text)                                                  \
    X(Lc3Plus, "POM68K_LC3_PLUS", StartupDomain::Machine,                  \
      startup_policy::NumericBoolean)                                        \
    X(IIvi, "POM68K_IIVI", StartupDomain::Machine,                         \
      startup_policy::NumericBoolean)                                        \
    X(AioId, "POM68K_AIO_ID", StartupDomain::Machine, startup_policy::Text)\
    X(Q605Id, "POM68K_Q605_ID",                                            \
      StartupDomain::Core | StartupDomain::Machine, startup_policy::HexInteger)\
    X(CentrisModel, "POM68K_CENTRIS_MODEL", StartupDomain::Machine,        \
      startup_policy::Text)                                                  \
    X(Centris610, "POM68K_CENTRIS610", StartupDomain::Machine,             \
      startup_policy::Presence)                                              \
    X(Q700Model, "POM68K_Q700_MODEL", StartupDomain::Machine,              \
      startup_policy::Text)                                                  \
    X(Q630Id, "POM68K_Q630_ID",                                            \
      StartupDomain::Core | StartupDomain::Machine, startup_policy::HexInteger)\
    X(CacheBoost, "POM68K_CACHE_BOOST", StartupDomain::Core,               \
      startup_policy::autoBounded(1, 64))                                    \
    X(ICacheMiss, "POM68K_ICACHE_MISS", StartupDomain::Core,               \
      startup_policy::autoBounded(0, 64))                                    \
    X(FloppyBoostGate, "POM68K_FLOPPY_BOOST_GATE", StartupDomain::Core,    \
      startup_policy::NumericBoolean)                                        \
    X(Jit030CacrFlush, "POM68K_JIT_030_CACR_FLUSH", StartupDomain::Core,   \
      startup_policy::NumericBoolean)                                        \
    X(MacIiEvent, "POM68K_MACII_EVENT", StartupDomain::Core,               \
      startup_policy::NumericBoolean)                                        \
    X(DuoEvent, "POM68K_DUO_EVENT", StartupDomain::Core,                   \
      startup_policy::NumericBoolean)                                        \
    X(Mmu040Walk, "POM68K_MMU040_WALK", StartupDomain::Core,               \
      startup_policy::Presence)                                              \
    X(Q605CacheBoost, "POM68K_Q605_CACHE_BOOST", StartupDomain::Core,      \
      startup_policy::autoBounded(1, 64))                                    \
    X(Q605ICacheMiss, "POM68K_Q605_ICACHE_MISS", StartupDomain::Core,      \
      startup_policy::autoBounded(0, 64))                                    \
    X(CentrisFpu, "POM68K_CENTRIS_FPU", StartupDomain::Core,               \
      startup_policy::Presence)                                              \
    X(CentrisBareFpu, "POM68K_CENTRIS_BAREFPU", StartupDomain::Core,       \
      startup_policy::Presence)                                              \
    X(CentrisCacheBoost, "POM68K_CENTRIS_CACHE_BOOST", StartupDomain::Core,\
      startup_policy::autoBounded(1, 64))                                    \
    X(Q630Lc040, "POM68K_Q630_LC040", StartupDomain::Core,                 \
      startup_policy::Presence)                                              \
    X(Q630BareFpu, "POM68K_Q630_BAREFPU", StartupDomain::Core,             \
      startup_policy::Presence)                                              \
    X(Q630CacheBoost, "POM68K_Q630_CACHE_BOOST", StartupDomain::Core,      \
      startup_policy::autoBounded(1, 64))                                    \
    X(Q700Lc040, "POM68K_Q700_LC040", StartupDomain::Core,                 \
      startup_policy::Presence)                                              \
    X(Q700BareFpu, "POM68K_Q700_BAREFPU", StartupDomain::Core,             \
      startup_policy::Presence)                                              \
    X(Q700CacheBoost, "POM68K_Q700_CACHE_BOOST", StartupDomain::Core,      \
      startup_policy::autoBounded(1, 64))                                    \
    X(ScsiLatency, "POM68K_SCSI_LAT", StartupDomain::Core,                 \
      startup_policy::AutoBaseInteger)                                       \
    X(Q605EventScc, "POM68K_Q605_EVENT_SCC", StartupDomain::Core,          \
      startup_policy::NumericBoolean)                                        \
    X(Q605EventScsi, "POM68K_Q605_EVENT_SCSI", StartupDomain::Core,        \
      startup_policy::NumericBoolean)                                        \
    X(DaynaPort, "POM68K_DAYNAPORT", StartupDomain::Core,                  \
      startup_policy::Custom)                                                \
    X(V8IoHole, "POM68K_V8_IOHOLE", StartupDomain::Core,                   \
      startup_policy::TraceLimit)                                            \
    X(V8HoleValue, "POM68K_V8_HOLEVAL", StartupDomain::Core,               \
      startup_policy::HexInteger)                                            \
    X(Q900IopBreak, "POM68K_Q900_IOPBRK", StartupDomain::Core,             \
      startup_policy::Presence)                                              \
    X(Q900IopWatch, "POM68K_Q900_IOPWATCH", StartupDomain::Core,           \
      startup_policy::HexInteger)                                            \
    X(Q900IopTrace, "POM68K_Q900_IOP_TRACE", StartupDomain::Core,          \
      startup_policy::TraceLimit)                                            \
    X(NoCdBay, "POM68K_NO_CDBAY", StartupDomain::Core,                     \
      startup_policy::Presence)                                              \
    X(FluxJitter, "POM68K_FLUX_JITTER", StartupDomain::Core,               \
      startup_policy::autoBounded(0, 45))                                    \
    X(ScsiDdmTemplate, "POM68K_SCSI_DDM_TEMPLATE", StartupDomain::Core,    \
      startup_policy::Text)                                                  \
    X(ScsiTrace, "POM68K_SCSI_TRACE", StartupDomain::Core,                 \
      startup_policy::Presence)                                              \
    X(CdTrace, "POM68K_CD_TRACE", StartupDomain::Core,                     \
      startup_policy::Presence)                                              \
    X(ScsiInquiry, "POM68K_SCSI_INQUIRY", StartupDomain::Core,             \
      startup_policy::Text)                                                  \
    X(AdbLle, "POM68K_ADB_LLE", StartupDomain::Core,                       \
      startup_policy::NumericBoolean)                                        \
    X(EgretLle, "POM68K_EGRET_LLE", StartupDomain::Core,                   \
      startup_policy::NumericBoolean)                                        \
    X(CudaLle, "POM68K_CUDA_LLE", StartupDomain::Core,                     \
      startup_policy::NumericBoolean)                                        \
    X(AdbFirmware, "POM68K_ADB_FW", StartupDomain::Core, startup_policy::Text)\
    X(CudaFirmware, "POM68K_CUDA_FW", StartupDomain::Core, startup_policy::Text)\
    X(FirmwareRoot, "POM68K_FIRMWARE_ROOT", StartupDomain::Core,           \
      startup_policy::Text)                                                  \
    X(AdbKeyboardId, "POM68K_ADB_KBD_ID", StartupDomain::Core,             \
      startup_policy::autoBounded(1, 3))                                     \
    X(AdbLleTrace, "POM68K_ADB_LLE_TRACE", StartupDomain::Core,            \
      startup_policy::Presence)                                              \
    X(AdbPicTrace, "POM68K_ADB_PIC_TRACE", StartupDomain::Core,            \
      startup_policy::Presence)                                              \
    X(SeViaTrace, "POM68K_SE_VIA_TRACE", StartupDomain::Core,              \
      startup_policy::Presence)                                              \
    X(RtcDebug, "RTCDBG", StartupDomain::Core, startup_policy::Presence)   \
    X(SccDebug, "SCCDBG", StartupDomain::Core, startup_policy::Presence)   \
    X(EgretCommandLog, "EGRET_CMD_LOG", StartupDomain::Core,               \
      startup_policy::Presence)                                              \
    X(DafbClockTrace, "POM68K_DAFB_CLOCK_TRACE", StartupDomain::Core,      \
      startup_policy::Presence)                                              \
    X(IIfxIoTrace, "POM68K_IIFX_IO_TRACE", StartupDomain::Core,            \
      startup_policy::Presence)                                              \
    X(IIfxAdbTrace, "POM68K_IIFX_ADB_TRACE", StartupDomain::Core,          \
      startup_policy::Presence)                                              \
    X(IIfxScsiTrace, "POM68K_IIFX_SCSI_TRACE", StartupDomain::Core,        \
      startup_policy::Presence)                                              \
    X(PgeTrace, "POM68K_PGE_TRACE", StartupDomain::Core,                   \
      startup_policy::Presence)                                              \
    X(PgeHandshake, "POM68K_PGE_HSHAKE", StartupDomain::Core,              \
      startup_policy::Presence)                                              \
    X(PgeAdbTrace, "POM68K_PGE_ADBTRACE", StartupDomain::Core,             \
      startup_policy::Presence)                                              \
    X(PgeSpiBytes, "POM68K_PGE_SPIBYTES", StartupDomain::Core,             \
      startup_policy::Presence)                                              \
    X(PgeTrackballTrace, "POM68K_PGE_TBTRACE", StartupDomain::Core,        \
      startup_policy::Presence)                                              \
    X(PgeSpinUs, "POM68K_PGE_SPINUS", StartupDomain::Core,                 \
      startup_policy::DecimalInteger)                                        \
    X(PgeTrap, "POM68K_PGE_TRAP", StartupDomain::Core,                     \
      startup_policy::HexInteger)                                            \
    X(PgePcCount, "POM68K_PGE_PCCOUNT", StartupDomain::Core,               \
      startup_policy::Custom)                                                \
    X(PgePcWindow, "POM68K_PGE_PCWIN", StartupDomain::Core,                \
      startup_policy::Custom)                                                \
    X(PgePcHistogram, "POM68K_PGE_PCHIST", StartupDomain::Core,            \
      startup_policy::Custom)                                                \
    X(AppleTalkDebug, "POM68K_ATALK_DEBUG", StartupDomain::Core,           \
      startup_policy::Presence)                                              \
    X(MacIpDebug, "POM68K_MACIP_DEBUG", StartupDomain::Core,               \
      startup_policy::Presence)                                              \
    X(PeripheralStats, "POM68K_PERIPH_STATS", StartupDomain::Core,         \
      startup_policy::Presence)                                              \
    X(JitProfile, "POM68K_JIT_PROFILE", StartupDomain::Jit,                \
      startup_policy::NonemptyText)                                          \
    X(JitBackend, "POM68K_JIT_BACKEND", StartupDomain::Jit,                \
      startup_policy::NonemptyText)                                          \
    X(CpuEngine, "POM68K_CPU_ENGINE", StartupDomain::Jit,                  \
      startup_policy::NonemptyText)                                          \
    X(JitUnsafeBackend, "POM68K_JIT_UNSAFE_BACKEND", StartupDomain::Jit,   \
      startup_policy::JitBoolean)                                            \
    X(JitFetch, "POM68K_JIT_FETCH", StartupDomain::Jit,                    \
      startup_policy::JitBoolean)                                            \
    X(JitBlocks, "POM68K_JIT_BLOCKS", StartupDomain::Jit,                  \
      startup_policy::JitBoolean)                                            \
    X(JitBlockMax, "POM68K_JIT_BLOCK_MAX", StartupDomain::Jit,             \
      startup_policy::decimalBounded(1, 256))                                \
    X(JitHot, "POM68K_JIT_HOT", StartupDomain::Jit,                        \
      startup_policy::decimalBounded(1, 1 << 20))                            \
    X(JitAccessThunk, "POM68K_JIT_ACCESS_THUNK", StartupDomain::Jit,       \
      startup_policy::decimalBounded(0, 2))                                  \
    X(Jit040LineRead, "POM68K_JIT_040_LINE_READ", StartupDomain::Jit,      \
      startup_policy::JitBoolean)                                            \
    X(Jit040LineWrite, "POM68K_JIT_040_LINE_WRITE", StartupDomain::Jit,    \
      startup_policy::JitBoolean)                                            \
    X(Jit040LinePair, "POM68K_JIT_040_LINE_PAIR", StartupDomain::Jit,      \
      startup_policy::JitBoolean)                                            \
    X(Jit040LineStats, "POM68K_JIT_040_LINE_STATS", StartupDomain::Jit,    \
      startup_policy::JitBoolean)                                            \
    X(JitLinks, "POM68K_JIT_LINKS", StartupDomain::Jit,                    \
      startup_policy::JitBoolean)                                            \
    X(JitPackedCcr, "POM68K_JIT_PACKED_CCR", StartupDomain::Jit,           \
      startup_policy::JitBoolean)                                            \
    X(JitRegisterCache, "POM68K_JIT_REG_CACHE", StartupDomain::Jit,        \
      startup_policy::JitBoolean)                                            \
    X(JitEdgeCells, "POM68K_JIT_EDGE_CELLS", StartupDomain::Jit,           \
      startup_policy::JitBoolean)                                            \
    X(JitDynamicBitfield, "POM68K_JIT_DYNAMIC_BITFIELD", StartupDomain::Jit,\
      startup_policy::JitBoolean)                                            \
    X(JitParanoid, "POM68K_JIT_PARANOID", StartupDomain::Jit,              \
      startup_policy::JitBoolean)                                            \
    X(JitHistogram, "POM68K_JIT_HISTO", StartupDomain::Jit,                \
      startup_policy::JitBoolean)                                            \
    X(JitMaxBlocks, "POM68K_JIT_MAX_BLOCKS", StartupDomain::Jit,           \
      startup_policy::decimalBounded(64, 1 << 20))                           \
    X(JitWindowKill, "POM68K_JIT_WINDOW_KILL", StartupDomain::Jit,         \
      startup_policy::decimalBounded(0, 1 << 24))                            \
    X(JitICacheEmit, "POM68K_JIT_ICACHE_EMIT", StartupDomain::Jit,         \
      startup_policy::JitBoolean)                                            \
    X(JitVerbose, "POM68K_JIT_VERBOSE", StartupDomain::Jit,                \
      startup_policy::JitBoolean)                                            \
    X(JitVerboseBlocks, "POM68K_JIT_VERBOSE_BLOCKS", StartupDomain::Jit,   \
      startup_policy::decimalBounded(0, 1 << 24))                            \
    X(JitWatchOpcode, "POM68K_JIT_WATCH_OPCODE", StartupDomain::Jit,       \
      startup_policy::NonemptyText)                                          \
    X(JitMinNative, "POM68K_JIT_MIN_NATIVE", StartupDomain::Jit,           \
      startup_policy::decimalBounded(0, 100))                                \
    X(JitProfitScore, "POM68K_JIT_PROFIT_SCORE", StartupDomain::Jit,       \
      startup_policy::decimalBounded(0, 1 << 30))                            \
    X(JitArmBackoff, "POM68K_JIT_ARM_BACKOFF", StartupDomain::Jit,         \
      startup_policy::decimalBounded(0, 4096))                               \
    X(DataWindow, "POM68K_DATA_WINDOW", StartupDomain::Jit,                \
      startup_policy::JitBoolean)                                            \
    X(JitRestartBase, "POM68K_JIT_RESTART_BASE", StartupDomain::Jit,       \
      startup_policy::JitBoolean)                                            \
    X(JitBsrWide, "POM68K_JIT_BSRW", StartupDomain::Jit,                   \
      startup_policy::JitBoolean)                                            \
    X(JitMemBitfield030, "POM68K_JIT_030_MEMBF", StartupDomain::Jit,       \
      startup_policy::JitBoolean)                                            \
    X(JitA64Pacing, "POM68K_JIT_A64_PACING", StartupDomain::Jit,           \
      startup_policy::JitBoolean)                                            \
    X(JitRequireNative, "POM68K_JIT_REQUIRE_NATIVE", StartupDomain::Jit,   \
      startup_policy::Presence)                                              \
    X(JitDispatchRing, "POM68K_JIT_DISPATCH_RING", StartupDomain::Jit,     \
      startup_policy::NumericBoolean)                                        \
    X(JitDenyFrom, "POM68K_JIT_DENY_FROM", StartupDomain::Jit,             \
      startup_policy::NonemptyHexInteger)                                    \
    X(JitDenyTo, "POM68K_JIT_DENY_TO", StartupDomain::Jit,                 \
      startup_policy::NonemptyHexInteger)                                    \
    X(JitTraceBlock, "POM68K_JIT_TRACE_BLOCK", StartupDomain::Jit,         \
      startup_policy::NonemptyHexInteger)

namespace startup_option {

#define DECLARE_STARTUP_OPTION(symbol, spelling, domain, valuePolicy) \
    inline constexpr StartupOption<domain, valuePolicy> symbol{spelling};
STARTUP_OPTION_SCHEMA(DECLARE_STARTUP_OPTION)
#undef DECLARE_STARTUP_OPTION

inline constexpr StartupOptionSpec kAll[] = {
#define LIST_STARTUP_OPTION(symbol, spelling, domain, valuePolicy) \
    StartupOptionSpec{spelling, domain, valuePolicy},
STARTUP_OPTION_SCHEMA(LIST_STARTUP_OPTION)
#undef LIST_STARTUP_OPTION
};

consteval bool validSchema() {
    constexpr std::size_t count = sizeof(kAll) / sizeof(kAll[0]);
    for (std::size_t i = 0; i < count; ++i) {
        if (kAll[i].name.empty() || std::uint8_t(kAll[i].domains) == 0)
            return false;
        const auto kind = kAll[i].value.kind;
        const bool bounded =
            kind == StartupValueKind::AutoBaseBoundedInteger ||
            kind == StartupValueKind::DecimalBoundedInteger;
        if (bounded && kAll[i].value.minimum > kAll[i].value.maximum)
            return false;
        if (!bounded && (kAll[i].value.minimum != 0 ||
                         kAll[i].value.maximum != 0))
            return false;
        for (std::size_t j = i + 1; j < count; ++j)
            if (kAll[i].name == kAll[j].name) return false;
    }
    return true;
}

static_assert(validSchema(), "startup option schema must be unique and typed");
static_assert(StartupOptionFor<decltype(NoFpu), StartupDomain::Product> &&
              !StartupOptionFor<decltype(NoFpu), StartupDomain::Core>);
static_assert(StartupOptionFor<decltype(AppleTalk), StartupDomain::Product> &&
              StartupOptionFor<decltype(AppleTalk), StartupDomain::Core>);
static_assert(StartupOptionFor<decltype(JitBackend), StartupDomain::Jit> &&
              !StartupOptionFor<decltype(JitBackend), StartupDomain::Product>);
static_assert(BooleanStartupOption<decltype(AppleTalk)> &&
              !IntegerStartupOption<decltype(AppleTalk)> &&
              IntegerStartupOption<decltype(CacheBoost)> &&
              !BooleanStartupOption<decltype(CacheBoost)> &&
              startup_option::CacheBoost.value.minimum == 1 &&
              startup_option::CacheBoost.value.maximum == 64);

} // namespace startup_option

#undef STARTUP_OPTION_SCHEMA

} // namespace pom68k
