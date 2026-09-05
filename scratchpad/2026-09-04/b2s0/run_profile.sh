#!/bin/bash
# Work item B — produce a gperftools CPU profile with FULL call stacks, so
# the TODO § B.2 memory buckets can be split by CALLER rather than by leaf.
#
# Recipe is tools/profile_census.py's header verbatim: LD_PRELOAD shim (Ubuntu's
# gperftools ignores $CPUPROFILE without a ProfilerStart call), and a NO-LTO
# build (identical-code folding under LTO reassigns samples to arbitrary
# sibling symbols and the report lies).
#
# The shim env is scoped to the emulator with `env`, never exported: the first
# cut exported it, so the `cat /proc/loadavg` that stamps the host load was
# ALSO profiled and, exiting last, overwrote the profile with 40 samples of
# /usr/bin/cat. A profiler that profiles the wrong process still produces a
# perfectly well-formed file.
#
#   run_profile.sh <arm>       arm = jit | interp | simcity
set -u
cd "$(dirname "$0")/../../.."          # the worktree root
EV=scratchpad/2026-09-04/b2s0
arm=${1:?usage: run_profile.sh jit|interp|simcity}

SHIM="LD_PRELOAD=./$EV/shim.so CPUPROFILE_FREQUENCY=500"
COMMON="POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1 POM68K_BENCH_FRAMES=2000"

echo "load before: $(cat /proc/loadavg)"
case "$arm" in
  jit)
    env $SHIM CPUPROFILE=./$EV/bench_jit.prof $COMMON \
        POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 \
        ./build-profile/jit_bench_lcii
    ;;
  interp)
    env $SHIM CPUPROFILE=./$EV/bench_interp.prof $COMMON \
        POM68K_CPU_ENGINE=interp \
        ./build-profile/jit_bench_lcii
    ;;
  simcity)
    env $SHIM CPUPROFILE=./$EV/simcity.prof $COMMON \
        POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 \
        ./build-profile/lcii_simcity_census
    ;;
esac
echo "load after:  $(cat /proc/loadavg)"
