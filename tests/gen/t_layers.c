/* biome map (Minecraft colours) of a square around 0,0, as a PPM: t_layers SEED [R [FILE]] */
#include "../../src/gen.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    i64 seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int R = argc > 2 ? atoi(argv[2]) : 512; /* half size in blocks */
    gen_init(seed);
    lay_mem = U.lay, lay_cap = LAY_INTS, lay_top = 0;
    int W = 2 * R;
    static uint8_t img[2048 * 2048 * 3];
    static i32 rm[64 * 64];
    static uint8_t bb[64 * 64];
    for (int tz = -R; tz < R; tz += 64)
        for (int tx = -R; tx < R; tx += 64) {
            /* rivermix cells the voronoi of blocks [tx, tx + 64) needs */
            int rx = (tx - 2) >> 2, rz = (tz - 2) >> 2, rw = ((tx + 61) >> 2) - rx + 2, rh = ((tz + 61) >> 2) - rz + 2;
            layers_rivermix(rx, rz, rw, rh, rm);
            voronoi(rm, rx, rz, rw, tx, tz, 64, 64, bb, 64);
            for (int j = 0; j < 64; j++)
                for (int i = 0; i < 64; i++) {
                    u32 c = bio(bb[i + j * 64])->color;
                    int px = tx + R + i, pz = tz + R + j;
                    uint8_t *p = img + 3 * (pz * W + px);
                    p[0] = c >> 16; p[1] = c >> 8; p[2] = c;
                }
        }
    printf("P6 %d %d peak lay ints %d\n", W, W, lay_peak);
    FILE *f = fopen(argc > 3 ? argv[3] : "biomes.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", W, W);
    fwrite(img, 1, (size_t)W * W * 3, f);
    fclose(f);
    return 0;
}
