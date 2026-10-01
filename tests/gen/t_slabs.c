/* Consistency: gen_slab(0..128) against the same chunk built from four 32-high slabs
 * (the engine asks for 32-high slabs). Prints the differing blocks per kind of difference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/blocks.h"
#include "../../src/gen.h"

int main(int argc, char **argv) {
    int64_t seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int n = argc > 2 ? atoi(argv[2]) : 10;
    static uint8_t full[128 * 256], part[128 * 256];
    gen_init(seed);
    long diff = 0, chunks = 0;
    for (int cz = -n / 2; cz < n - n / 2; cz++)
        for (int cx = -n / 2; cx < n - n / 2; cx++) {
            gen_slab(cx, cz, 0, 128, full);
            for (int s = 0; s < 4; s++) gen_slab(cx, cz, s * 32, 32, part + s * 32 * 256);
            int d = 0;
            for (int i = 0; i < 128 * 256; i++)
                if (full[i] != part[i]) {
                    if (diff + d < 12)
                        printf("chunk %d %d: x %d y %d z %d full %d slabs %d\n", cx, cz, i & 15, i >> 8, (i >> 4) & 15,
                               full[i], part[i]);
                    d++;
                }
            diff += d;
            chunks += d > 0;
        }
    printf("seed %lld: %ld blocks differ in %ld of %d chunks\n", (long long)seed, diff, chunks, n * n);
    return 0;
}
