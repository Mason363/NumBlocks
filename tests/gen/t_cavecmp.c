#include "../../src/gen.c"
#include <stdio.h>
#include <stdlib.h>
#include "ref_terrain.h"
#include "t_init.h"
static ColSum csum[256];
int main(int argc, char **argv) {
    i64 seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int n = argc > 2 ? atoi(argv[2]) : 50;
    test_init(seed); ref_init(seed); r_sin_init(); bflags_init();
    { JRand r; jr_seed(&r, seed); cave_mul_x = jr_long(&r); cave_mul_z = jr_long(&r); }
    static uint8_t ref[65536], mine[65536];
    long diff = 0, tot = 0, cdiff = 0, carved = 0, wdiff = 0;
    JRand pick; jr_seed(&pick, 7);
    for (int c = 0; c < n; c++) {
        int cx = jr_int(&pick, 2000) - 1000, cz = jr_int(&pick, 2000) - 1000;
        ref_chunk(cx, cz, ref);
        uint8_t b16[256]; memcpy(b16, cb16, 256);
        ref_caves(cx, cz, ref, b16);
        chunk_terrain(cx, cz, mine, 0, 256, csum);
        memcpy(c_b16, b16, 256);
        CaveCtx ctx = {0}; ctx.mode = CAVE_OUT; ctx.tcx = cx; ctx.tcz = cz; ctx.sum = csum; ctx.out = mine; ctx.y0 = 0; ctx.h = 256;
        ctx.rx0 = 0; ctx.rx1 = 16; ctx.rz0 = 0; ctx.rz1 = 16; ctx.ry0 = 0; ctx.ry1 = 256;
        tg[4] = &ctx;
        memset(c_top, 255, 256), memset(c_sl, 255, 256); /* no column tracking */
        caves_run(1u << 4);
        int d = 0;
        for (int y = 0; y < 256; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            int a = ref[(y * 16 + z) * 16 + x], b = mine[y * 256 + z * 16 + x];
            if (a == B_AIR && y < 60) carved++;
            if (a != b) { if (d < 40) printf("  chunk %d,%d (%d,%d,%d): ref %d mine %d\n", cx, cz, x, y, z, a, b); d++; }
        }
        diff += d; tot += 65536; if (d) cdiff++;
        /* the same carve seen through 24-high windows (as the game asks): the rows must match
         * the full carve but for out-of-window effects (rare) */
        static uint8_t win[24 * 256], full[65536];
        memcpy(full, mine, sizeof full);
        for (int y0 = 0; y0 <= 104; y0 += 8) {
            chunk_terrain(cx, cz, win, y0, 24, csum);
            CaveCtx w = ctx;
            w.out = win, w.y0 = y0, w.h = 24;
            tg[4] = &w;
            caves_run(1u << 4);
            for (int i = 0; i < 24 * 256; i++) wdiff += win[i] != full[y0 * 256 + i];
        }
    }
    printf("seed %lld: %ld / %ld blocks differ in %ld of %d chunks (air below 60 in ref: %ld); "
           "24-high windows vs full carve: %ld blocks differ\n", (long long)seed, diff, tot, cdiff, n, carved, wdiff);
    return 0;
}
