#include <stdio.h>
#include <stdint.h>
#include <time.h>
// Does x18 survive in a user thread on this host? Set it, spin, read it back.
int main(void) {
    uint64_t lost = 0, iters = 0;
    time_t end = time(0) + 20;
    while (time(0) < end) {
        uint64_t out;
        __asm__ volatile(
            "mov x18, #0x1234\n"
            "mov x9, #0x4000000\n"
            "1: subs x9, x9, #1\n"
            "b.ne 1b\n"
            "mov %0, x18\n" : "=r"(out) : : "x9", "cc");
        iters++;
        if (out != 0x1234) { lost++; if (lost < 4) printf("x18 read back %#llx\n", (unsigned long long)out); }
    }
    printf("iterations %llu, x18 lost %llu\n", (unsigned long long)iters, (unsigned long long)lost);
    return 0;
}
