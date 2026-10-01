/* genview: renders maps of the NumBlocks world generator for a seed.
 *   genview SEED [CX0 CZ0 NCHUNKS OUTDIR]
 * writes OUTDIR/biomes.png (Minecraft biome colours), OUTDIR/top.png (height-shaded top view
 * with water, trees and plants), OUTDIR/section.png (vertical cut along z = middle of the area:
 * caves, ores, dungeons), and prints timings. PNGs are written uncompressed (stored deflate). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/blocks.h"
#include "../../src/gen.h"

/* ---- minimal PNG writer ---- */
static uint32_t crc_tab[256];
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static uint32_t crc(uint32_t c, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ b[i]) & 255] ^ (c >> 8);
    return c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(data, 1, n, f);
    uint32_t c = crc(0xFFFFFFFFu, (const uint8_t *)type, 4);
    c = crc(c, data, n) ^ 0xFFFFFFFFu;
    be32(h, c);
    fwrite(h, 1, 4, f);
}
static void write_png(const char *path, const uint8_t *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    size_t raw = (size_t)h * (1 + 3 * (size_t)w);
    size_t nblk = (raw + 65534) / 65535;
    uint8_t *z = malloc(2 + raw + nblk * 5 + 4);
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint8_t *line = malloc(raw);
    for (int y = 0; y < h; y++) {
        line[y * (1 + 3 * w)] = 0;
        memcpy(line + y * (1 + 3 * w) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw; i++) { a = (a + line[i]) % 65521; b = (b + a) % 65521; }
    for (size_t p = 0; p < raw; p += 65535) {
        size_t n = raw - p < 65535 ? raw - p : 65535;
        z[o++] = p + n == raw;
        z[o++] = n & 255; z[o++] = n >> 8; z[o++] = ~n & 255; z[o++] = (~n >> 8) & 255;
        memcpy(z + o, line + p, n);
        o += n;
    }
    be32(z + o, (b << 16) | a);
    o += 4;
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", 0, 0);
    fclose(f);
    free(z);
    free(line);
}

/* ---- colours ---- */
static uint32_t biome_color(int id) {
    switch (id) {
    case 0: return 0x000070; case 1: return 0x8DB360; case 2: return 0xFA9418; case 3: return 0x606060;
    case 4: return 0x056621; case 5: return 0x0B6659; case 6: return 0x07F9B2; case 7: return 0x0000FF;
    case 10: return 0x9090A0; case 11: return 0xA0A0FF; case 12: return 0xFFFFFF; case 13: return 0xA0A0A0;
    case 14: return 0xFF00FF; case 15: return 0xA000FF; case 16: return 0xFADE55; case 17: return 0xD25F12;
    case 18: return 0x22551C; case 19: return 0x163933; case 20: return 0x72789A; case 21: return 0x537B09;
    case 22: return 0x2C4205; case 23: return 0x628B17; case 24: return 0x000030; case 25: return 0xA2A284;
    case 26: return 0xFAF0C0; case 27: return 0x307444; case 28: return 0x1F5F32; case 29: return 0x40511A;
    case 30: return 0x31554A; case 31: return 0x243F36; case 32: return 0x596651; case 33: return 0x454F3E;
    case 34: return 0x507050; case 35: return 0xBDB25F; case 36: return 0xA79D64; case 37: return 0xD94515;
    case 38: return 0xB09765; case 39: return 0xCA8C65;
    }
    if (id >= 128) { /* mutated: lighter parent */
        uint32_t c = biome_color(id - 128);
        uint32_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        r = r + 40 > 255 ? 255 : r + 40; g = g + 40 > 255 ? 255 : g + 40; b = b + 40 > 255 ? 255 : b + 40;
        return r << 16 | g << 8 | b;
    }
    return 0xFF0000;
}

static uint32_t block_color(int b) {
    switch (b) {
    case B_STONE: return 0x7F7F7F; case B_GRANITE: return 0x9A6B57; case B_DIORITE: return 0xC8C8C8; case B_ANDESITE: return 0x888888;
    case B_GRASS: return 0x5E9D34; case B_GRASS_SNOWED: return 0xF0F8F8; case B_DIRT: return 0x86603F; case B_COARSE_DIRT: return 0x7A5A3A; case B_PODZOL: return 0x5A3D17;
    case B_COBBLESTONE: return 0x6E6E6E; case B_MOSSY_COBBLESTONE: return 0x587058; case B_BEDROCK: return 0x303030;
    case B_WATER: case B_FLOWING_WATER: return 0x2A50D8; case B_LAVA: case B_FLOWING_LAVA: return 0xF07010;
    case B_SAND: return 0xDCD29A; case B_RED_SAND: return 0xC06A2A; case B_GRAVEL: return 0x8A8280; case B_CLAY: return 0xA0A6B4;
    case B_GOLD_ORE: return 0xF0D040; case B_IRON_ORE: return 0xD8AF93; case B_COAL_ORE: return 0x202020; case B_LAPIS_ORE: return 0x2040C0;
    case B_DIAMOND_ORE: return 0x40F0F0; case B_REDSTONE_ORE: return 0xE01010; case B_EMERALD_ORE: return 0x10E040; case B_MONSTER_EGG_STONE: return 0x7F7F90;
    case B_SANDSTONE: return 0xD8CC94; case B_RED_SANDSTONE: return 0xB05A20;
    case B_SNOW: case B_SNOW_LAYER: return 0xFAFAFA; case B_ICE: return 0x90B0FF; case B_PACKED_ICE: return 0x7090E0;
    case B_MYCELIUM: return 0x786878; case B_HARDENED_CLAY: return 0x985E43; case B_CACTUS: return 0x107018; case B_SUGAR_CANE: return 0x80C060;
    case B_PUMPKIN: return 0xE08010; case B_MELON: return 0x70A020; case B_MOB_SPAWNER: return 0x1A2A3A; case B_CHEST: return 0xA07028;
    case B_LILY_PAD: return 0x208020; case B_VINE: return 0x307020; case B_TALL_GRASS: case B_FERN: case B_DOUBLE_GRASS_LOWER: case B_LARGE_FERN_LOWER: return 0x4C8C2C;
    case B_DEAD_BUSH: return 0x946428;
    }
    if (b >= B_STAINED_CLAY_WHITE && b <= B_STAINED_CLAY_BLACK) {
        static const uint32_t C[16] = {0xD1B2A1, 0xA15325, 0x95576C, 0x706C8A, 0xBA8523, 0x677534, 0xA14E4E, 0x392A23,
                                       0x876A61, 0x575B5B, 0x764656, 0x4A3B5B, 0x4D3323, 0x4C532A, 0x8F3D2E, 0x251610};
        return C[b - B_STAINED_CLAY_WHITE];
    }
    if (b >= B_LOG_OAK && b <= B_LOG_JUNGLE_BARK) return 0x6B5030;
    if (b >= B_LOG_ACACIA && b <= B_LOG_DARK_OAK_BARK) return 0x5A4028;
    if (b == B_LEAVES_OAK || b == B_LEAVES_JUNGLE || b == B_LEAVES_ACACIA || b == B_LEAVES_DARK_OAK) return 0x3A7A1A;
    if (b == B_LEAVES_SPRUCE) return 0x2E5A3A;
    if (b == B_LEAVES_BIRCH) return 0x6A9A3A;
    if (b >= B_DANDELION && b <= B_OXEYE_DAISY) return b == B_DANDELION ? 0xF0F020 : 0xE03030;
    if (b == B_BROWN_MUSHROOM || b == B_RED_MUSHROOM) return 0xB04030;
    if (b >= B_BROWN_MUSHROOM_CAP && b <= B_RED_MUSHROOM_STEM) return b <= B_BROWN_MUSHROOM_STEM ? 0x8D6A4D : 0xB02020;
    if (b >= B_SUNFLOWER_LOWER && b <= B_PEONY_UPPER) return 0xD060B0;
    return 0xFF00FF;
}

int main(int argc, char **argv) {
    int64_t seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int cx0 = argc > 2 ? atoi(argv[2]) : -8, cz0 = argc > 3 ? atoi(argv[3]) : -8;
    int n = argc > 4 ? atoi(argv[4]) : 16;
    const char *dir = argc > 5 ? argv[5] : ".";
    crc_init();
    gen_init(seed);
    int W = n * 16;
    uint8_t *top = calloc((size_t)W * W, 3), *bm = calloc((size_t)W * W, 3);
    int *hgt = calloc((size_t)W * W, sizeof(int));
    uint8_t *sec = calloc((size_t)W * 128, 3);
    static uint8_t out[128 * 256];
    int zsec = n / 2 * 16 + 8; /* section row (local) */
    double tmax = 0, tsum = 0;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            clock_t t0 = clock();
            gen_slab(cx0 + i, cz0 + j, 0, 128, out);
            double t = (double)(clock() - t0) / CLOCKS_PER_SEC * 1000;
            tsum += t;
            if (t > tmax) tmax = t;
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++) {
                    int px = i * 16 + x, pz = j * 16 + z;
                    int y = 127;
                    while (y > 0 && out[y * 256 + z * 16 + x] == B_AIR) y--;
                    int b = out[y * 256 + z * 16 + x];
                    int wy = y;
                    if (b == B_WATER) { while (wy > 0 && out[wy * 256 + z * 16 + x] == B_WATER) wy--; }
                    hgt[pz * W + px] = y;
                    uint32_t c = block_color(b);
                    float f = 0.6f + (y - 40) / 110.0f;
                    if (b == B_WATER) { f = 0.5f + (wy - 30) / 80.0f; uint32_t g = block_color(out[wy * 256 + z * 16 + x]); c = ((c & 0xFEFEFE) >> 1) + ((g & 0xFCFCFC) >> 2); }
                    if (f > 1.25f) f = 1.25f;
                    if (f < 0.3f) f = 0.3f;
                    uint8_t *p = top + 3 * ((size_t)pz * W + px);
                    int r = (int)(((c >> 16) & 255) * f), g = (int)(((c >> 8) & 255) * f), bb = (int)((c & 255) * f);
                    p[0] = r > 255 ? 255 : r; p[1] = g > 255 ? 255 : g; p[2] = bb > 255 ? 255 : bb;
                    uint32_t bc = biome_color(gen_biome(cx0 * 16 + px, cz0 * 16 + pz));
                    uint8_t *q = bm + 3 * ((size_t)pz * W + px);
                    q[0] = bc >> 16; q[1] = bc >> 8; q[2] = bc;
                    if (pz == zsec)
                        for (int yy = 0; yy < 128; yy++) {
                            uint32_t sc = block_color(out[yy * 256 + z * 16 + x]);
                            if (out[yy * 256 + z * 16 + x] == B_AIR) sc = yy < hgt[pz * W + px] ? 0x101010 : 0xB0D0FF;
                            uint8_t *s = sec + 3 * ((size_t)(127 - yy) * W + px);
                            s[0] = sc >> 16; s[1] = sc >> 8; s[2] = sc;
                        }
                }
        }
    /* hill shading */
    for (int pz = 0; pz < W; pz++)
        for (int px = 1; px < W; px++) {
            int d = hgt[pz * W + px] - hgt[pz * W + px - 1];
            float f = 1.0f + d * 0.06f;
            if (f < 0.7f) f = 0.7f;
            if (f > 1.3f) f = 1.3f;
            uint8_t *p = top + 3 * ((size_t)pz * W + px);
            for (int k = 0; k < 3; k++) { int v = (int)(p[k] * f); p[k] = v > 255 ? 255 : v; }
        }
    char path[512];
    snprintf(path, sizeof path, "%s/top.png", dir); write_png(path, top, W, W);
    snprintf(path, sizeof path, "%s/biomes.png", dir); write_png(path, bm, W, W);
    snprintf(path, sizeof path, "%s/section.png", dir); write_png(path, sec, W, 128);
    int sx, sy, sz;
    clock_t t0 = clock();
    gen_spawn(&sx, &sy, &sz);
    double ts = (double)(clock() - t0) / CLOCKS_PER_SEC * 1000;
    printf("seed %lld: %d chunks, gen_slab avg %.2f ms, max %.2f ms; spawn (%d, %d, %d) biome %d (%.0f ms)\n",
           (long long)seed, n * n, tsum / (n * n), tmax, sx, sy, sz, gen_biome(sx, sz), ts);
    return 0;
}
