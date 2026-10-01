/* Checksums of gen_slab outputs (full height and 32-high slabs, in the engine's
 * row order and in a scattered order) and of gen_top / gen_biome, for regression checks:
 *   t_sum SEED [N]   prints one line per chunk and a final combined hash */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/gen.h"

static uint32_t hash(const uint8_t *p, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

int main(int argc, char **argv) {
    int64_t seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int n = argc > 2 ? atoi(argv[2]) : 6;
    int verbose = argc > 3;
    static uint8_t out[128 * 256];
    gen_init(seed);
    uint32_t all = 0;
    for (int cz = -n / 2; cz < n - n / 2; cz++)
        for (int cx = -n / 2; cx < n - n / 2; cx++) {
            gen_slab(cx, cz, 0, 128, out);
            uint32_t h = hash(out, sizeof out);
            for (int i = 0; i < 256; i++) {
                uint8_t t = (uint8_t)gen_top(cx * 16 + (i & 15), cz * 16 + (i >> 4));
                uint8_t b = (uint8_t)gen_biome(cx * 16 + (i & 15), cz * 16 + (i >> 4));
                h = (h ^ t) * 16777619u;
                h = (h ^ b) * 16777619u;
            }
            if (verbose) printf("%d %d %08x\n", cx, cz, h);
            all = all * 31 + h;
        }
    /* 32-high slabs in a scattered order (cache misses) */
    for (int k = 0; k < n * 2; k++) {
        int cx = (k * 7) % (n + 3) - 4, cz = (k * 5) % (n + 2) - 3, y0 = (k % 4) * 32;
        gen_slab(cx, cz, y0, 32, out);
        uint32_t h = hash(out, 32 * 256);
        if (verbose) printf("slab %d %d %d %08x\n", cx, cz, y0, h);
        all = all * 31 + h;
    }
    int x, y, z;
    gen_spawn(&x, &y, &z);
    printf("seed %lld: hash %08x spawn %d %d %d\n", (long long)seed, all, x, y, z);
    return 0;
}
