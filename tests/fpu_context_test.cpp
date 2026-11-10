// CPU-instance arithmetic state must survive another CPU's execution.
#include "Moira.h"
extern "C" {
#include "softfloat.h"
}
#include <array>
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#include <atomic>
#include <thread>
#endif
#include <cstdio>
#include <type_traits>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}
template<class Fn> auto capture(Fn fn, const float_status* status) {
    if constexpr (std::is_invocable_v<Fn, const float_status*>) return fn(status);
    else return fn(); // pre-change interface, for the negative control
}
struct Intermediates {
    std::array<floatx80, 6> values;
    uint64_t grs;
};
Intermediates intermediates(const float_status* status) {
    return {{{capture(getFloatInternalOverflow, status),
              capture(getFloatInternalUnderflow, status),
              capture(getFloatInternalRoundedAll, status),
              capture(getFloatInternalRoundedSome, status),
              capture(getFloatInternalUnrounded, status),
              capture(getFloatInternalFloatx80, status)}},
            capture(getFloatInternalGRS, status)};
}
bool same(const Intermediates& a, const Intermediates& b) {
    if (a.grs != b.grs) return false;
    for (size_t i = 0; i < a.values.size(); ++i)
        if (a.values[i].high != b.values[i].high || a.values[i].low != b.values[i].low)
            return false;
    return true;
}
void intermediateContexts() {
    float_status a{}, b{};
    for (auto* status : {&a, &b}) {
        set_floatx80_rounding_precision(80, status);
        set_float_rounding_mode(float_round_nearest_even, status);
    }
    const floatx80 largest{0x7ffe, 0xffffffffffffffffull};
    const floatx80 two{0x4000, 0x8000000000000000ull};
    const floatx80 tiny{1, 0x8000000000000000ull};
    (void)floatx80_mul(largest, two, &a);
    const auto expected = intermediates(&a);
    const auto flags = get_float_exception_flags(&a);
    (void)floatx80_mul(tiny, tiny, &b);
    check(same(intermediates(&a), expected),
          "all seven intermediate-result getters belong to their status context");
    check(get_float_exception_flags(&a) == flags && (flags & float_flag_overflow),
          "the overflow context retains its own exception flags");
}

class Cpu : public moira::Moira {
public:
    std::vector<moira::u8> mem = std::vector<moira::u8>(1 << 20);
    mutable Cpu* nested = nullptr;
    bool hookWrite = false;
    mutable unsigned hooks = 0;
    void word(uint32_t a, uint16_t v) { mem[a] = uint8_t(v >> 8); mem[a+1] = uint8_t(v); }
    void lng(uint32_t a, uint32_t v) { word(a, uint16_t(v >> 16)); word(a+2, uint16_t(v)); }
    uint32_t lng(uint32_t a) const {
        return uint32_t(mem[a]) << 24 | uint32_t(mem[a+1]) << 16 | uint32_t(mem[a+2]) << 8 | mem[a+3];
    }
    void prepare(moira::Model model, uint16_t ext, uint32_t fpcr, bool onePointFive) {
        setModel(model);
        setFPUModel(model == moira::Model::M68040 ? moira::FPUModel::M68040 : moira::FPUModel::M68882);
        lng(0, 0x80000); lng(4, 0x1000);
        word(0x1000, 0xf210); word(0x1002, ext);
        word(0x1004, 0x4e71); word(0x1006, 0x4e71);
        reset(); setSR(0x2700); setA(0, 0x4000); setFPCR(fpcr);
        const moira::u32 fp[3] = {0x3fff0000, onePointFive ? 0xc0000000u : 0x80000000u, 0};
        setFP(0, fp);
    }
private:
    void trigger(uint32_t a, bool write) const {
        if (nested && a == 0x4000 && write == hookWrite) {
            Cpu* other = nested; nested = nullptr; ++hooks;
            other->execute();
        }
    }
    moira::u8 read8(moira::u32 a) const override {
        trigger(a, false); return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override {
        trigger(a, false); return moira::u16(mem[a & 0xfffff] << 8 | mem[(a+1) & 0xfffff]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        trigger(a, true); const_cast<Cpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        trigger(a, true); const_cast<Cpu*>(this)->word(a & 0xfffff, v);
    }
};
void nestedInstruction(moira::Model model, bool output) {
    Cpu control, outer, inner;
    for (Cpu* c : {&control, &outer}) {
        c->prepare(model, output ? 0x6000 : 0x4422, output ? 0 : 0x70, output);
        c->lng(0x4000, 0x33800000); // half a single-precision ULP at 1.0
    }
    inner.prepare(model, 0x4422, output ? 0 : 0x50, false);
    inner.lng(0x4000, output ? 0x3f800000 : 0x33800000);
    control.execute();
    outer.nested = &inner; outer.hookWrite = output;
    outer.execute();
    std::array<moira::u32,3> expected{}, actual{};
    control.getFP(0, expected.data()); outer.getFP(0, actual.data());
    std::printf("model=%u output=%d FP=%08x:%08x:%08x/%08x:%08x:%08x FPSR=%08x/%08x\n",
                unsigned(model), output, expected[0], expected[1], expected[2],
                actual[0], actual[1], actual[2], control.getFPSR(), outer.getFPSR());
    check(outer.hooks == 1 && !outer.isHalted() && inner.getPC0() == 0x1004,
          "the nested CPU executed during the operand callback");
    check(actual == expected && outer.getFPSR() == control.getFPSR() &&
          outer.lng(0x4000) == control.lng(0x4000) && outer.getPC0() == control.getPC0(),
          "nested execution preserves the outer CPU's precision, rounding and exception flags");
}
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
void parallelInstructions(moira::Model model) {
    constexpr unsigned iterations = 512;
    Cpu up, truncate;
    for (auto* c : {&up, &truncate}) {
        c->prepare(model, 0x4422, c == &up ? 0x70 : 0x50, false);
        c->lng(0x4000, 0x33800000);
        for (unsigned i = 0; i < iterations; ++i) {
            c->word(0x1000 + i * 4, 0xf210);
            c->word(0x1002 + i * 4, 0x4422);
        }
        c->word(0x1000 + iterations * 4, 0x4e71);
        c->word(0x1002 + iterations * 4, 0x4e71);
    }
    std::atomic<unsigned> ready{0};
    bool okay[2] = {true, true};
    auto worker = [&](Cpu& c, unsigned index) {
        ready.fetch_add(1, std::memory_order_release);
        while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
        for (unsigned i = 0; i < iterations; ++i) {
            const moira::u32 one[3] = {0x3fff0000, 0x80000000, 0};
            c.setFP(0, one); c.setFPSR(0); c.execute();
            moira::u32 fp[3]; c.getFP(0, fp);
            // [B] MC68881UM-1989 §§2.2, 2.3: single precision, directed
            // rounding of 1 + half-ULP; inexact current and accrued bits.
            okay[index] &= fp[0] == 0x3fff0000 && fp[1] == (index ? 0x80000000u : 0x80000100u)
                && fp[2] == 0 && c.getFPSR() == 0x208 && !c.isHalted()
                && c.getPC0() == 0x1004 + i * 4;
        }
    };
    std::thread a(worker, std::ref(up), 0), b(worker, std::ref(truncate), 1);
    a.join(); b.join();
    check(okay[0] && okay[1], "two host threads retain separate rounding and flags for 1024 instructions");
}
#endif

}
int main() {
    intermediateContexts();
    for (auto model : {moira::Model::M68030, moira::Model::M68040}) {
        nestedInstruction(model, false); nestedInstruction(model, true);
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
        parallelInstructions(model);
#endif
    }
    std::printf("fpu_context_test: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
