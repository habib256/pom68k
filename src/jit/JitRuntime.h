// POM68K — helpers shared by generated-code backends.

#pragma once

#include <cstdint>
#include "Moira.h"

namespace moira { class Moira; }

// Completes a 68040 JSR after generated EA calculation.
// Zero means the post-push fault has already been processed by Moira.
extern "C" int pom68kJitJsr040(moira::Moira* cpu, uint32_t instructionPc,
                               uint32_t returnAddress, uint32_t target,
                               uint32_t opcode,
                               moira::Moira::PomJitAtcReplay040* replay) noexcept;

// Exact accesses shared by both native emitters. Failure requests replay.
extern "C" int pom68kJitStep(moira::Moira* cpu, moira::Moira::PomJitAtcReplay040* replay) noexcept;
extern "C" int pom68kJitRead(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                             uint32_t* out, uint64_t pcWords, moira::Moira::PomJitAtcReplay040* replay) noexcept;
extern "C" int pom68kJitWrite(moira::Moira* cpu, uint32_t addr, uint32_t bytes,
                              uint32_t val, uint64_t pcWords, moira::Moira::PomJitAtcReplay040* replay) noexcept;
extern "C" int pom68kJitReadProg(moira::Moira* cpu, uint32_t addr,
                                 uint32_t* out, moira::Moira::PomJitAtcReplay040* replay) noexcept;
