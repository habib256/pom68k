/* Defined SoftFloat integer boundaries, independently of a CPU or oracle. */
#include <stdint.h>
#include <stdio.h>
#include "softfloat.h"
#include "softfloat-macros.h"

int main(void)
{
    const uint64_t patterns[][2] = {
        {0, 0}, {0, 1}, {1, 0}, {UINT64_MAX, UINT64_MAX},
        {UINT64_C(0x8000000000000000), 0},
        {0, UINT64_C(0x8000000000000000)},
        {UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210)},
        {UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0x5555555555555555)}
    };
    unsigned checked = 0;
    for (unsigned p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        for (int count = 0; count <= 64; ++count) {
            uint64_t expectedHi = patterns[p][0], expectedLo = patterns[p][1];
            /* Independent reference: repeated one-bit shifts never shift by 64. */
            for (int i = 0; i < count; ++i) {
                expectedHi = (expectedHi << 1) | (expectedLo >> 63);
                expectedLo <<= 1;
            }
            for (int inplace = 0; inplace != 2; ++inplace) {
                uint64_t hi = patterns[p][0], lo = patterns[p][1];
                if (inplace) shortShift128Left(hi, lo, count, &hi, &lo);
                else shortShift128Left(patterns[p][0], patterns[p][1], count, &hi, &lo);
                ++checked;
                if (hi != expectedHi || lo != expectedLo) {
                    fprintf(stderr, "FAIL shift pattern=%u count=%d inplace=%d\n", p, count, inplace);
                    return 1;
                }
            }
        }
    }
    const struct { int32_t input; uint16_t high; uint64_t low; } integers[] = {
        {INT32_MIN, 0xc01e, UINT64_C(0x8000000000000000)},
        {INT32_MAX, 0x401d, UINT64_C(0xfffffffe00000000)},
        {-1, 0xbfff, UINT64_C(0x8000000000000000)},
        {0, 0, 0}, {1, 0x3fff, UINT64_C(0x8000000000000000)}
    };
    for (unsigned i = 0; i < sizeof(integers) / sizeof(integers[0]); ++i) {
        const floatx80 value = int32_to_floatx80(integers[i].input);
        if (value.high != integers[i].high || value.low != integers[i].low) {
            fprintf(stderr, "FAIL int32 conversion case=%u\n", i);
            return 1;
        }
    }
    printf("softfloat_primitives_test: %u shifts, 5 conversions, 0 failures\n", checked);
    return 0;
}
