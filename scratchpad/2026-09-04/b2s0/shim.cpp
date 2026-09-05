// gperftools only starts profiling when ProfilerStart is called; Ubuntu's
// build ignores $CPUPROFILE on its own. This six-line LD_PRELOAD shim does
// it, and is the recipe documented in tools/profile_census.py's header.
//   g++ -shared -fPIC shim.cpp -o shim.so -l:libprofiler.so.0
//   LD_PRELOAD=./shim.so CPUPROFILE=out.prof CPUPROFILE_FREQUENCY=500 <run>
// The child writes CPUPROFILE_<pid>, not CPUPROFILE.
#include <cstdlib>
extern "C" int ProfilerStart(const char*);
extern "C" void ProfilerStop();
__attribute__((constructor)) static void s() {
    if (const char* p = std::getenv("CPUPROFILE")) ProfilerStart(p);
}
__attribute__((destructor)) static void e() { ProfilerStop(); }
