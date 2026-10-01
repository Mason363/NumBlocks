#include "../../src/gen.c"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
static void tinit(i64 seed) {
    jr_jump_init();
    g_seed = seed;
    memset(biome_index, 255, sizeof biome_index);
    for (int i = 0; i < NBIOMES; i++) biome_index[BIOMES[i].id] = (uint8_t)i;
    for (int i = 0; i < NLSEEDS; i++) lay_wseed[i] = layer_world(LSEEDS[i]);
    vor_c = layer_world(10);
    JRand r; jr_seed(&r, seed);
    for (int i = 0; i < 16; i++) { oc_min[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 16; i++) { oc_max[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 8; i++) { oc_main[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 4; i++) { oc_surf[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 10; i++) perm_skip(&r);
    for (int i = 0; i < 16; i++) { oc_depth[i].st = r.s; perm_skip(&r); }
    SC_LIMIT = FX(684.412f); SC_MAINXZ = FX(684.412f / 80.0f); SC_MAINY = FX(684.412f / 160.0f); SC_DEPTH = FX(200.0f);
    for (int j = -2; j <= 2; j++) for (int k = -2; k <= 2; k++) bweights[j + 2 + (k + 2) * 5] = 10.0f / (float)sqrt((double)((float)(j * j + k * k) + 0.2f));
    JRand t; jr_seed(&t, 1234); perm_build(t.s, perm_temp, 0);
    jr_seed(&t, 2345); perm_build(t.s, perm_grass, 0);
}
int main(int argc, char **argv) {
    i64 seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int R = argc > 2 ? atoi(argv[2]) : 8; /* chunks radius */
    int X0 = argc > 3 ? atoi(argv[3]) : 0, Z0 = argc > 4 ? atoi(argv[4]) : 0;
    tinit(seed);
    int W = 2 * R * 16;
    uint8_t *img = calloc((size_t)W * W, 3);
    static ColSum sum[256];
    clock_t t0 = clock();
    for (int cz = -R; cz < R; cz++) for (int cx = -R; cx < R; cx++) {
        chunk_terrain(cx + X0, cz + Z0, 0, 0, 0, sum);
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            ColSum *s = &sum[x + z * 16];
            int px = (cx + R) * 16 + x, pz = (cz + R) * 16 + z;
            uint8_t *p = img + 3 * (pz * W + px);
            int r = 128, g = 128, b = 128;
            switch (cs_blk(s)) {
            case B_GRASS: r = 90; g = 160; b = 60; break;
            case B_SAND: r = 220; g = 210; b = 150; break;
            case B_RED_SAND: r = 200; g = 110; b = 40; break;
            case B_WATER: case B_LILY_PAD: r = 40; g = 70; b = 200; break;
            case B_ICE: r = 160; g = 190; b = 255; break;
            case B_STONE: r = 120; g = 120; b = 120; break;
            case B_GRAVEL: r = 140; g = 130; b = 130; break;
            case B_SNOW: r = 250; g = 250; b = 250; break;
            case B_MYCELIUM: r = 120; g = 90; b = 120; break;
            case B_PODZOL: r = 110; g = 80; b = 40; break;
            case B_COARSE_DIRT: case B_DIRT: r = 130; g = 95; b = 60; break;
            default: r = 180; g = 120; b = 90; break;
            }
            int hgt = cs_top(s);
            if (cs_blk(s) == B_WATER || cs_blk(s) == B_LILY_PAD) { float d = (63 - s->y) / 40.0f; /* floor */ if (d > 1) d = 1; r *= 1 - d * 0.6f; g *= 1 - d * 0.6f; b *= 1 - d * 0.4f; }
            else { float f = 0.55f + (hgt - 50) / 100.0f; if (f > 1.3f) f = 1.3f; r *= f; g *= f; b *= f; }
            /* hillshade */
            if (x > 0) { int dh = (int)cs_top(s) - (int)cs_top(&sum[x - 1 + z * 16]); float f = 1 + dh * 0.08f; if (f < 0.6f) f = 0.6f; if (f > 1.4f) f = 1.4f; r *= f; g *= f; b *= f; }
            p[0] = r > 255 ? 255 : r; p[1] = g > 255 ? 255 : g; p[2] = b > 255 ? 255 : b;
        }
    }
    double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%d chunks in %.3f s (%.3f ms/chunk)\n", 4 * R * R, el, el * 1000 / (4 * R * R));
    FILE *f = fopen("/tmp/claude-0/-home-user-NumPlay/3af2eb1c-20d9-554a-acfb-eb19ce4fa977/scratchpad/gen/terrain.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", W, W);
    fwrite(img, 1, (size_t)W * W * 3, f);
    fclose(f);
    return 0;
}
