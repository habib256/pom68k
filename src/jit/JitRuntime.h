// POM68K — helpers shared by generated-code backends.

#pragma once

#include <cstdint>

namespace moira { class Moira; }

// Completes a cache-active 68040 JSR after generated EA calculation.
// Zero means the post-push fault has already been processed by Moira.
extern "C" int pom68kJitJsr040(moira::Moira* cpu, uint32_t instructionPc,
                               uint32_t returnAddress, uint32_t target,
                               uint32_t opcode) noexcept;
