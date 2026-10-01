/* The ARM bench's calls (tests/gen/arm/bench.c) on the host: 24-high slabs at y0 (default 56)
 * in the game's pattern, printing the same checksums (they must match the bench's, which
 * checks that the host and the Cortex-M7 builds compute the same blocks).
 *   t_pattern [SEED [Y0]] */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/gen.h"

static uint8_t out[24 * 256];
static uint32_t sums[64];
static int k, y0 = 56;

static void slab(int cx, int cz) {
    gen_slab(cx, cz, y0, 24, out);
    uint32_t s = 0;
    for (unsigned i = 0; i < sizeof out; i++) s = s * 31 + out[i];
    for (int i = 0; i < 256; i++) s = s * 31 + (uint32_t)gen_top(cx * 16 + (i & 15), cz * 16 + (i >> 4));
    if (k < 64) sums[k] = s;
    k++;
}

int main(int argc, char **argv) {
    int64_t seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    if (argc > 2) y0 = atoi(argv[2]);
    gen_init(seed);
    for (int cz = -2; cz <= 0; cz++)
        for (int cx = -2; cx <= 1; cx++) slab(cx, cz);
    for (int cx = 2; cx <= 4; cx++)
        for (int cz = -2; cz <= 0; cz++) slab(cx, cz);
    for (int cz = 1; cz <= 2; cz++)
        for (int cx = 1; cx <= 4; cx++) slab(cx, cz);
    printf("checksums:");
    for (int i = 0; i < 16; i++) printf(" %08x", sums[i]);
    uint32_t all = 0;
    for (int i = 0; i < k && i < 64; i++) all = all * 31 + sums[i];
    printf("\nall %d calls: %08x\n", k, all);
    return 0;
}
