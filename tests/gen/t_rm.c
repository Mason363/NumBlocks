/* writes the 1:4 GenLayerRiverMix map for block window [x0,x0+w) x [z0,z0+h) as raw ids (1 byte per block) */
#include "../../src/gen.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    (void)argc;
    i64 seed = strtoll(argv[1], 0, 10);
    int x0 = atoi(argv[2]), z0 = atoi(argv[3]), w = atoi(argv[4]), h = atoi(argv[5]);
    gen_init(seed);
    lay_mem = U.lay, lay_cap = LAY_INTS, lay_top = 0;
    int cx0 = x0 >> 2, cz0 = z0 >> 2, cw = ((x0 + w - 1) >> 2) - cx0 + 1, ch = ((z0 + h - 1) >> 2) - cz0 + 1;
    static i32 rm[64*64];
    static uint8_t cells[1024*1024];
    for (int tz = 0; tz < ch; tz += 16) for (int tx = 0; tx < cw; tx += 16) {
        int tw = cw - tx < 16 ? cw - tx : 16, th = ch - tz < 16 ? ch - tz : 16;
        layers_rivermix(cx0 + tx, cz0 + tz, tw, th, rm);
        for (int j = 0; j < th; j++) for (int i = 0; i < tw; i++) cells[(tz + j) * cw + tx + i] = rm[i + j * tw];
    }
    FILE *f = fopen(argv[6], "wb");
    for (int z = z0; z < z0 + h; z++) for (int x = x0; x < x0 + w; x++) fputc(cells[((z >> 2) - cz0) * cw + (x >> 2) - cx0], f);
    fclose(f);
    printf("peak %d\n", lay_peak);
    /* palette dump */
    f = fopen(argv[7], "w");
    for (int i = 0; i < NBIOMES; i++) fprintf(f, "%d %u\n", BIOMES[i].id, BIOMES[i].color);
    fclose(f);
    return 0;
}
