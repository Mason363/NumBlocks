#include "../../src/gen.c"
#include <stdio.h>
#include <stdlib.h>
#include "ref_terrain.h"
#include "t_init.h"
int main(int argc, char **argv) {
    i64 seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int n = argc > 2 ? atoi(argv[2]) : 50;
    int X0 = argc > 3 ? atoi(argv[3]) : 0, Z0 = argc > 4 ? atoi(argv[4]) : 0;
    test_init(seed);
    ref_init(seed);
    static uint8_t ref[65536], mine[256 * 256];
    long diff = 0, tot = 0, cdiff = 0;
    JRand pick; jr_seed(&pick, 99);
    for (int c = 0; c < n; c++) {
        int cx = X0 + jr_int(&pick, 2000) - 1000, cz = Z0 + jr_int(&pick, 2000) - 1000;
        if (c < 4) { cx = X0 + c; cz = Z0; }
        if (argc > 5) { /* only chunks containing biome class argv[5] */
            int want = atoi(argv[5]), ok = 0, tries = 0;
            while (!ok && tries++ < 200000) {
                cx = X0 + jr_int(&pick, 20000) - 10000; cz = Z0 + jr_int(&pick, 20000) - 10000;
                chunk_biomes(cx, cz);
                for (int q = 0; q < 256; q++) if (want < 100 ? bio(cb16[q])->cls == want : cb16[q] == want) ok = 1;
            }
        }
        ref_chunk(cx, cz, ref);
        chunk_terrain(cx, cz, mine, 0, 256, 0);
        int d = 0;
        for (int y = 0; y < 256; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            int a = ref[(y * 16 + z) * 16 + x], b = mine[y * 256 + z * 16 + x];
            if (a != b) { if (d < 5) printf("  chunk %d,%d (%d,%d,%d): ref %d mine %d\n", cx, cz, x, y, z, a, b); d++; }
        }
        diff += d; tot += 65536; if (d) cdiff++;
    }
    printf("seed %lld: %ld / %ld blocks differ in %ld of %d chunks\n", (long long)seed, diff, tot, cdiff, n);
    return 0;
}
