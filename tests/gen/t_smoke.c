/* smoke test: generate an area through the public API (build with sanitizers) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../src/gen.h"
#include "../../src/blocks.h"
int main(int argc, char **argv) {
    int64_t seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int R = argc > 2 ? atoi(argv[2]) : 3;
    gen_init(seed);
    static uint8_t out[128 * 256];
    long cnt[256] = {0};
    clock_t t0 = clock();
    for (int cz = -R; cz < R; cz++)
        for (int cx = -R; cx < R; cx++) {
            gen_slab(cx, cz, 0, 128, out);
            for (int i = 0; i < 128 * 256; i++) cnt[out[i]]++;
        }
    double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%d chunks, %.2f ms/chunk\n", 4 * R * R, el * 1000 / (4 * R * R));
    for (int b = 0; b < B_COUNT; b++) if (cnt[b]) printf("%d:%ld ", b, cnt[b]);
    printf("\n");
    int x, y, z;
    gen_spawn(&x, &y, &z);
    printf("spawn %d %d %d biome %d\n", x, y, z, gen_biome(x, z));
    return 0;
}
