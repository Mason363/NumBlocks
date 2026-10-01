/* Literal double-precision transcription of ChunkProviderGenerate's terrain + surface
 * (NoiseGeneratorOctaves/Perlin, NoiseGenerator3/Handler, BiomeBase.b and overrides) used
 * as a reference to test gen.c's optimised float/fixed-point version. Include after gen.c. */
#include <math.h>
typedef struct { int d[512]; double a, b, c; } RPerlin;
/* nextDouble in double (gen.c only keeps the exact numerator) */
static double jr_double(JRand *r) { return (double)jr_double_bits(r) * (1.0 / 9007199254740992.0); }

static void rperlin_init(RPerlin *p, JRand *r) {
    p->a = jr_double(r) * 256.0; p->b = jr_double(r) * 256.0; p->c = jr_double(r) * 256.0;
    for (int i = 0; i < 256; i++) p->d[i] = i;
    for (int i = 0; i < 256; i++) { int j = jr_int(r, 256 - i) + i; int k = p->d[i]; p->d[i] = p->d[j]; p->d[j] = k; p->d[i + 256] = p->d[i]; }
}
static const double RE[16] = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0}, RF[16] = {1,1,-1,-1,0,0,0,0,1,-1,1,-1,1,-1,1,-1}, RG[16] = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
static double rlerp(double t, double a, double b) { return a + t * (b - a); }
static double rgrad2(int i, double x, double z) { int j = i & 15; return RE[j] * x + RG[j] * z; }
static double rgrad3(int i, double x, double y, double z) { int j = i & 15; return RE[j] * x + RF[j] * y + RG[j] * z; }
static void rperlin_arr(RPerlin *P, double *arr, double d0, double d1, double d2, int i, int j, int k, double d3, double d4, double d5, double d6) {
    const int *d = P->d;
    if (j == 1) {
        int i2 = 0; double d13 = 1.0 / d6;
        for (int k2 = 0; k2 < i; ++k2) {
            double d7 = d0 + (double)k2 * d3 + P->a; int l2 = (int)d7; if (d7 < (double)l2) --l2; int i3 = l2 & 255; d7 -= (double)l2;
            double d8 = d7 * d7 * d7 * (d7 * (d7 * 6.0 - 15.0) + 10.0);
            for (int j1 = 0; j1 < k; ++j1) {
                double d9 = d2 + (double)j1 * d5 + P->c; int k1 = (int)d9; if (d9 < (double)k1) --k1; int l1 = k1 & 255; d9 -= (double)k1;
                double d10 = d9 * d9 * d9 * (d9 * (d9 * 6.0 - 15.0) + 10.0);
                int l = d[i3] + 0, j3 = d[l] + l1, k3 = d[i3 + 1] + 0, i1 = d[k3] + l1;
                double d11 = rlerp(d8, rgrad2(d[j3], d7, d9), rgrad3(d[i1], d7 - 1.0, 0.0, d9));
                double d12 = rlerp(d8, rgrad3(d[j3 + 1], d7, 0.0, d9 - 1.0), rgrad3(d[i1 + 1], d7 - 1.0, 0.0, d9 - 1.0));
                double d14 = rlerp(d10, d11, d12);
                arr[i2++] += d14 * d13;
            }
        }
    } else {
        int l = 0; double d15 = 1.0 / d6; int i1 = -1; double d16 = 0, d7 = 0, d17 = 0, d8 = 0;
        for (int j1 = 0; j1 < i; ++j1) {
            double d9 = d0 + (double)j1 * d3 + P->a; int k1 = (int)d9; if (d9 < (double)k1) --k1; int l1 = k1 & 255; d9 -= (double)k1;
            double d10 = d9 * d9 * d9 * (d9 * (d9 * 6.0 - 15.0) + 10.0);
            for (int l3 = 0; l3 < k; ++l3) {
                double d18 = d2 + (double)l3 * d5 + P->c; int i4 = (int)d18; if (d18 < (double)i4) --i4; int j4 = i4 & 255; d18 -= (double)i4;
                double d19 = d18 * d18 * d18 * (d18 * (d18 * 6.0 - 15.0) + 10.0);
                for (int k4 = 0; k4 < j; ++k4) {
                    double d20 = d1 + (double)k4 * d4 + P->b; int l4 = (int)d20; if (d20 < (double)l4) --l4; int i5 = l4 & 255; d20 -= (double)l4;
                    double d21 = d20 * d20 * d20 * (d20 * (d20 * 6.0 - 15.0) + 10.0);
                    if (k4 == 0 || i5 != i1) {
                        i1 = i5;
                        int j5 = d[l1] + i5, k5 = d[j5] + j4, l5 = d[j5 + 1] + j4, i6 = d[l1 + 1] + i5, i2 = d[i6] + j4, j6 = d[i6 + 1] + j4;
                        d16 = rlerp(d10, rgrad3(d[k5], d9, d20, d18), rgrad3(d[i2], d9 - 1.0, d20, d18));
                        d7 = rlerp(d10, rgrad3(d[l5], d9, d20 - 1.0, d18), rgrad3(d[j6], d9 - 1.0, d20 - 1.0, d18));
                        d17 = rlerp(d10, rgrad3(d[k5 + 1], d9, d20, d18 - 1.0), rgrad3(d[i2 + 1], d9 - 1.0, d20, d18 - 1.0));
                        d8 = rlerp(d10, rgrad3(d[l5 + 1], d9, d20 - 1.0, d18 - 1.0), rgrad3(d[j6 + 1], d9 - 1.0, d20 - 1.0, d18 - 1.0));
                    }
                    double d22 = rlerp(d21, d16, d7), d23 = rlerp(d21, d17, d8), d24 = rlerp(d19, d22, d23);
                    arr[l++] += d24 * d15;
                }
            }
        }
    }
}
static long long rmh_floor_l(double d) { long long i = (long long)d; return d < (double)i ? i - 1 : i; }
static void roct(RPerlin *P, int n, double *arr, int i, int j, int k, int l, int i1, int j1, double d0, double d1, double d2) {
    for (int a = 0; a < l * i1 * j1; a++) arr[a] = 0;
    double d3 = 1.0;
    for (int l1 = 0; l1 < n; ++l1) {
        double d4 = (double)i * d3 * d0, d5 = (double)j * d3 * d1, d6 = (double)k * d3 * d2;
        long long i2 = rmh_floor_l(d4), j2 = rmh_floor_l(d6);
        d4 -= (double)i2; d6 -= (double)j2; i2 %= 16777216LL; j2 %= 16777216LL; d4 += (double)i2; d6 += (double)j2;
        rperlin_arr(&P[l1], arr, d4, d5, d6, l, i1, j1, d0 * d3, d1 * d3, d2 * d3, d3);
        d3 /= 2.0;
    }
}
typedef struct { int f[512]; double b, c, d; } RSimplex;
static void rsimplex_init(RSimplex *p, JRand *r) {
    p->b = jr_double(r) * 256.0; p->c = jr_double(r) * 256.0; p->d = jr_double(r) * 256.0;
    for (int i = 0; i < 256; i++) p->f[i] = i;
    for (int i = 0; i < 256; i++) { int j = jr_int(r, 256 - i) + i; int k = p->f[i]; p->f[i] = p->f[j]; p->f[j] = k; p->f[i + 256] = p->f[i]; }
}
static const int RSG[12][3] = {{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
static int rsfl(double d) { return d > 0.0 ? (int)d : (int)d - 1; }
static double rsimplex_pt(RSimplex *p, double d0, double d1) {
    double SQ3 = sqrt(3.0), d2 = 0.5 * (SQ3 - 1.0), d3 = (d0 + d1) * d2;
    int i = rsfl(d0 + d3), j = rsfl(d1 + d3); double d4 = (3.0 - SQ3) / 6.0, d5 = (double)(i + j) * d4;
    double d6 = (double)i - d5, d7 = (double)j - d5, d8 = d0 - d6, d9 = d1 - d7; int b0, b1;
    if (d8 > d9) { b0 = 1; b1 = 0; } else { b0 = 0; b1 = 1; }
    double d10 = d8 - b0 + d4, d11 = d9 - b1 + d4, d12 = d8 - 1.0 + 2.0 * d4, d13 = d9 - 1.0 + 2.0 * d4;
    int k = i & 255, l = j & 255;
    int i1 = p->f[k + p->f[l]] % 12, j1 = p->f[k + b0 + p->f[l + b1]] % 12, k1 = p->f[k + 1 + p->f[l + 1]] % 12;
    double d15, d17, d19, d14 = 0.5 - d8 * d8 - d9 * d9;
    if (d14 < 0) d15 = 0; else { d14 *= d14; d15 = d14 * d14 * (RSG[i1][0] * d8 + RSG[i1][1] * d9); }
    double d16 = 0.5 - d10 * d10 - d11 * d11;
    if (d16 < 0) d17 = 0; else { d16 *= d16; d17 = d16 * d16 * (RSG[j1][0] * d10 + RSG[j1][1] * d11); }
    double d18 = 0.5 - d12 * d12 - d13 * d13;
    if (d18 < 0) d19 = 0; else { d18 *= d18; d19 = d18 * d18 * (RSG[k1][0] * d12 + RSG[k1][1] * d13); }
    return 70.0 * (d15 + d17 + d19);
}
static void rsimplex_arr(RSimplex *p, double *arr, double d0, double d1, int i, int j, double d2, double d3, double d4) {
    double SQ3 = sqrt(3.0), G = 0.5 * (SQ3 - 1.0), H = (3.0 - SQ3) / 6.0;
    int k = 0;
    for (int l = 0; l < j; ++l) {
        double d5 = (d1 + (double)l) * d3 + p->c;
        for (int i1 = 0; i1 < i; ++i1) {
            double d6 = (d0 + (double)i1) * d2 + p->b, d7 = (d6 + d5) * G;
            int j1 = rsfl(d6 + d7), k1 = rsfl(d5 + d7); double d8 = (double)(j1 + k1) * H, d9 = (double)j1 - d8, d10 = (double)k1 - d8, d11 = d6 - d9, d12 = d5 - d10;
            int b0, b1; if (d11 > d12) { b0 = 1; b1 = 0; } else { b0 = 0; b1 = 1; }
            double d13 = d11 - b0 + H, d14 = d12 - b1 + H, d15 = d11 - 1.0 + 2.0 * H, d16 = d12 - 1.0 + 2.0 * H;
            int l1 = j1 & 255, i2 = k1 & 255;
            int j2 = p->f[l1 + p->f[i2]] % 12, k2 = p->f[l1 + b0 + p->f[i2 + b1]] % 12, l2 = p->f[l1 + 1 + p->f[i2 + 1]] % 12;
            double d18, d20, d22, d17 = 0.5 - d11 * d11 - d12 * d12;
            if (d17 < 0) d18 = 0; else { d17 *= d17; d18 = d17 * d17 * (RSG[j2][0] * d11 + RSG[j2][1] * d12); }
            double d19 = 0.5 - d13 * d13 - d14 * d14;
            if (d19 < 0) d20 = 0; else { d19 *= d19; d20 = d19 * d19 * (RSG[k2][0] * d13 + RSG[k2][1] * d14); }
            double d21 = 0.5 - d15 * d15 - d16 * d16;
            if (d21 < 0) d22 = 0; else { d21 *= d21; d22 = d21 * d21 * (RSG[l2][0] * d15 + RSG[l2][1] * d16); }
            arr[k++] += 70.0 * (d18 + d20 + d22) * d4;
        }
    }
}
static RPerlin r_min[16], r_max[16], r_main[8], r_depth[16];
static RSimplex r_surf[4], r_ae, r_af, r_mesaH, r_mesaF[4], r_mesaG;
static int r_bands[64];
static double r_q[25];
static void ref_init(i64 seed) {
    JRand r; jr_seed(&r, seed);
    for (int i = 0; i < 16; i++) rperlin_init(&r_min[i], &r);
    for (int i = 0; i < 16; i++) rperlin_init(&r_max[i], &r);
    for (int i = 0; i < 8; i++) rperlin_init(&r_main[i], &r);
    for (int i = 0; i < 4; i++) rsimplex_init(&r_surf[i], &r);
    RPerlin dummy; for (int i = 0; i < 10; i++) rperlin_init(&dummy, &r);
    for (int i = 0; i < 16; i++) rperlin_init(&r_depth[i], &r);
    for (int j = -2; j <= 2; ++j) for (int k = -2; k <= 2; ++k) r_q[j + 2 + (k + 2) * 5] = (float)(10.0f / (float)sqrt((double)((float)(j * j + k * k) + 0.2F)));
    jr_seed(&r, 1234); rsimplex_init(&r_ae, &r);
    jr_seed(&r, 2345); rsimplex_init(&r_af, &r);
    /* mesa */
    jr_seed(&r, seed); rsimplex_init(&r_mesaH, &r);
    for (int j = 0; j < 64; j++) r_bands[j] = B_HARDENED_CLAY;
    int j;
    for (j = 0; j < 64; ++j) { j += jr_int(&r, 5) + 1; if (j < 64) r_bands[j] = B_STAINED_CLAY_ORANGE; }
    j = jr_int(&r, 4) + 2;
    for (int k = 0; k < j; ++k) { int l = jr_int(&r, 3) + 1, i1 = jr_int(&r, 64); for (int j1 = 0; i1 + j1 < 64 && j1 < l; ++j1) r_bands[i1 + j1] = B_STAINED_CLAY_YELLOW; }
    int k = jr_int(&r, 4) + 2;
    for (int l = 0; l < k; ++l) { int i1 = jr_int(&r, 3) + 2, j1 = jr_int(&r, 64); for (int k1 = 0; j1 + k1 < 64 && k1 < i1; ++k1) r_bands[j1 + k1] = B_STAINED_CLAY_BROWN; }
    int l = jr_int(&r, 4) + 2;
    for (int i1 = 0; i1 < l; ++i1) { int j1 = jr_int(&r, 3) + 1, k1 = jr_int(&r, 64); for (int l1 = 0; k1 + l1 < 64 && l1 < j1; ++l1) r_bands[k1 + l1] = B_STAINED_CLAY_RED; }
    int i1 = jr_int(&r, 3) + 3, j1 = 0;
    for (int k1 = 0; k1 < i1; ++k1) { j1 += jr_int(&r, 16) + 4; for (int i2 = 0; j1 + i2 < 64 && i2 < 1; ++i2) { r_bands[j1 + i2] = B_STAINED_CLAY_WHITE; if (j1 + i2 > 1 && jr_bool(&r)) r_bands[j1 + i2 - 1] = B_STAINED_CLAY_SILVER; if (j1 + i2 < 63 && jr_bool(&r)) r_bands[j1 + i2 + 1] = B_STAINED_CLAY_SILVER; } }
    jr_seed(&r, seed);
    for (int o = 0; o < 4; o++) rsimplex_init(&r_mesaF[o], &r);
    rsimplex_init(&r_mesaG, &r);
}
static double r_oct3_pt(RSimplex *p, int n, double x, double z) { double d2 = 0, d3 = 1; for (int i = 0; i < n; i++) { d2 += rsimplex_pt(&p[i], x * d3, z * d3) / d3; d3 /= 2.0; } return d2; }
static float r_temp(const Biome *b, int x, int y, int z) {
    if (y > 64) { float f = (float)(rsimplex_pt(&r_ae, (double)x * 1.0 / 8.0, (double)z * 1.0 / 8.0) * 4.0); return b->temp - (f + (float)y - 64.0F) * 0.05F / 30.0F; }
    return b->temp;
}
/* snapshot index (x, y, z) -> [y][z][x] in our arrays: chunk[(y*16+z)*16+x] */
#define RS(x, y, z) chunk[((y) * 16 + (z)) * 16 + (x)]
static void r_b(uint8_t *chunk, const Biome *bb, JRand *random, int i, int j, double d0, int ak, int al) {
    int k = 63; int top = ak, fill = al; int l = -1;
    int i1 = (int)(d0 / 3.0 + 3.0 + jr_double(random) * 0.25);
    int j1 = i & 15, k1 = j & 15;
    for (int l1 = 255; l1 >= 0; --l1) {
        if (l1 <= jr_int(random, 5)) { RS(k1, l1, j1) = B_BEDROCK; continue; }
        int b2 = RS(k1, l1, j1);
        if (b2 == B_AIR) l = -1;
        else if (b2 == B_STONE) {
            if (l == -1) {
                if (i1 <= 0) { top = -1; fill = B_STONE; }
                else if (l1 >= k - 4 && l1 <= k + 1) { top = ak; fill = al; }
                if (l1 < k && (top == -1 || top == B_AIR)) top = r_temp(bb, i, l1, j) < 0.15F ? B_ICE : B_WATER;
                l = i1;
                if (l1 >= k - 1) RS(k1, l1, j1) = top < 0 ? B_AIR : top;
                else if (l1 < k - 7 - i1) { top = -1; fill = B_STONE; RS(k1, l1, j1) = B_GRAVEL; }
                else RS(k1, l1, j1) = fill;
            } else if (l > 0) {
                --l; RS(k1, l1, j1) = fill;
                if (l == 0 && (fill == B_SAND || fill == B_RED_SAND)) { l = jr_int(random, 4) + (l1 - 63 > 0 ? l1 - 63 : 0); fill = fill == B_RED_SAND ? B_RED_SANDSTONE : B_SANDSTONE; }
            }
        }
    }
}
static int r_band(int i, int j) { int l = (int)llround(floor(rsimplex_pt(&r_mesaH, (double)i * 1.0 / 512.0, (double)i * 1.0 / 512.0) * 2.0 + 0.5)); return r_bands[(j + l + 64) % 64]; }
static void r_mesa(uint8_t *chunk, const Biome *bb, JRand *random, int i, int j, double d0) {
    double d1 = 0.0; int k, l;
    if (bb->mode & 1) {
        k = (i & -16) + (j & 15); l = (j & -16) + (i & 15);
        double d2 = fmin(fabs(d0), r_oct3_pt(r_mesaF, 4, (double)k * 0.25, (double)l * 0.25));
        if (d2 > 0.0) { double d3 = 0.001953125, d4 = fabs(r_oct3_pt(&r_mesaG, 1, (double)k * d3, (double)l * d3)); d1 = d2 * d2 * 2.5; double d5 = ceil(d4 * 50.0) + 14.0; if (d1 > d5) d1 = d5; d1 += 64.0; }
    }
    k = i & 15; l = j & 15; int i1 = 63; int top = B_STAINED_CLAY_WHITE, fill = bb->filler;
    int j1 = (int)(d0 / 3.0 + 3.0 + jr_double(random) * 0.25); int flag = cos(d0 / 3.0 * 3.141592653589793) > 0.0;
    int k1 = -1, flag1 = 0;
    for (int l1 = 255; l1 >= 0; --l1) {
        if (RS(l, l1, k) == B_AIR && l1 < (int)d1) RS(l, l1, k) = B_STONE;
        if (l1 <= jr_int(random, 5)) { RS(l, l1, k) = B_BEDROCK; continue; }
        int b2 = RS(l, l1, k);
        if (b2 == B_AIR) k1 = -1;
        else if (b2 == B_STONE) {
            if (k1 == -1) {
                flag1 = 0;
                if (j1 <= 0) { top = -1; fill = B_STONE; } else if (l1 >= i1 - 4 && l1 <= i1 + 1) { top = B_STAINED_CLAY_WHITE; fill = bb->filler; }
                if (l1 < i1 && (top == -1 || top == B_AIR)) top = B_WATER;
                k1 = j1 + (l1 - i1 > 0 ? l1 - i1 : 0);
                if (l1 >= i1 - 1) {
                    if ((bb->mode & 2) && l1 > 86 + j1 * 2) RS(l, l1, k) = flag ? B_COARSE_DIRT : B_GRASS;
                    else if (l1 > i1 + 3 + j1) RS(l, l1, k) = (l1 >= 64 && l1 <= 127) ? (flag ? B_HARDENED_CLAY : r_band(i, l1)) : B_STAINED_CLAY_ORANGE;
                    else { RS(l, l1, k) = bb->top; flag1 = 1; }
                } else { RS(l, l1, k) = fill; if (fill == B_STAINED_CLAY_WHITE) RS(l, l1, k) = B_STAINED_CLAY_ORANGE; }
            } else if (k1 > 0) { --k1; RS(l, l1, k) = flag1 ? B_STAINED_CLAY_ORANGE : r_band(i, l1); }
        }
    }
}
/* full 16x256x16 chunk [y][z][x] */
static void ref_chunk(int cx, int cz, uint8_t *chunk) {
    /* biomes via gen.c layers (already verified) */
    chunk_biomes(cx, cz);
    uint8_t b4[100], b16[256]; memcpy(b4, cb4, 100); memcpy(b16, cb16, 256);
    static double g[25], dd[825], e[825], f[825], p[825];
    int i = cx * 4, k = cz * 4;
    /* depth: this.b.a(this.g, i, k, 5, 5, e, f, g) -> a(arr, i, 10, k, 5, 1, 5, 200, 1.0, 200) */
    roct(r_depth, 16, g, i, 10, k, 5, 1, 5, (double)200.0f, 1.0, (double)200.0f);
    float F = 684.412F, F1 = 684.412F;
    roct(r_main, 8, dd, i, 0, k, 5, 33, 5, (double)(F / 80.0F), (double)(F1 / 160.0F), (double)(F / 80.0F));
    roct(r_min, 16, e, i, 0, k, 5, 33, 5, (double)F, (double)F1, (double)F);
    roct(r_max, 16, f, i, 0, k, 5, 33, 5, (double)F, (double)F1, (double)F);
    int l = 0, i1 = 0;
    for (int j1 = 0; j1 < 5; ++j1) for (int k1 = 0; k1 < 5; ++k1) {
        float f2 = 0, f3 = 0, f4 = 0; const Biome *bc = bio(b4[j1 + 2 + (k1 + 2) * 10]);
        for (int l1 = -2; l1 <= 2; ++l1) for (int i2 = -2; i2 <= 2; ++i2) {
            const Biome *b1 = bio(b4[j1 + l1 + 2 + (k1 + i2 + 2) * 10]);
            float f5 = 0.0F + b1->depth * 1.0F, f6 = 0.0F + b1->scale * 1.0F;
            float f7 = (float)r_q[l1 + 2 + (i2 + 2) * 5] / (f5 + 2.0F);
            if (b1->depth > bc->depth) f7 /= 2.0F;
            f2 += f6 * f7; f3 += f5 * f7; f4 += f7;
        }
        f2 /= f4; f3 /= f4; f2 = f2 * 0.9F + 0.1F; f3 = (f3 * 4.0F - 1.0F) / 8.0F;
        double d0 = g[i1] / 8000.0; if (d0 < 0) d0 = -d0 * 0.3; d0 = d0 * 3.0 - 2.0;
        if (d0 < 0) { d0 /= 2.0; if (d0 < -1) d0 = -1; d0 /= 1.4; d0 /= 2.0; } else { if (d0 > 1) d0 = 1; d0 /= 8.0; }
        ++i1; double d1 = f3, d2 = f2; d1 += d0 * 0.2; d1 = d1 * (double)8.5F / 8.0; double d3 = (double)8.5F + d1 * 4.0;
        for (int j2 = 0; j2 < 33; ++j2) {
            double d4 = ((double)j2 - d3) * (double)12.0F * 128.0 / 256.0 / d2; if (d4 < 0) d4 *= 4.0;
            double d5 = e[l] / (double)512.0F, d6 = f[l] / (double)512.0F, d7 = (dd[l] / 10.0 + 1.0) / 2.0;
            double d8 = (d7 < 0 ? d5 : (d7 > 1 ? d6 : d5 + (d6 - d5) * d7)) - d4;
            if (j2 > 29) { double d9 = (double)((float)(j2 - 29) / 3.0F); d8 = d8 * (1.0 - d9) + -10.0 * d9; }
            p[l] = d8; ++l;
        }
    }
    memset(chunk, B_AIR, 65536);
    for (int kk = 0; kk < 4; ++kk) { int ll = kk * 5, ii1 = (kk + 1) * 5;
        for (int jj1 = 0; jj1 < 4; ++jj1) { int kk1 = (ll + jj1) * 33, ll1 = (ll + jj1 + 1) * 33, ii2 = (ii1 + jj1) * 33, jj2 = (ii1 + jj1 + 1) * 33;
            for (int k2 = 0; k2 < 32; ++k2) {
                double d0 = 0.125, d1 = p[kk1 + k2], d2 = p[ll1 + k2], d3 = p[ii2 + k2], d4 = p[jj2 + k2];
                double d5 = (p[kk1 + k2 + 1] - d1) * d0, d6 = (p[ll1 + k2 + 1] - d2) * d0, d7 = (p[ii2 + k2 + 1] - d3) * d0, d8 = (p[jj2 + k2 + 1] - d4) * d0;
                for (int l2 = 0; l2 < 8; ++l2) { double d9 = 0.25, d10 = d1, d11 = d2, d12 = (d3 - d1) * d9, d13 = (d4 - d2) * d9;
                    for (int i3 = 0; i3 < 4; ++i3) { double d14 = 0.25, d15 = (d11 - d10) * d14, d16 = d10 - d15;
                        for (int j3 = 0; j3 < 4; ++j3) { int y = k2 * 8 + l2; if ((d16 += d15) > 0.0) RS(kk * 4 + i3, y, jj1 * 4 + j3) = B_STONE; else if (y < 63) RS(kk * 4 + i3, y, jj1 * 4 + j3) = B_WATER; }
                        d10 += d12; d11 += d13; }
                    d1 += d5; d2 += d6; d3 += d7; d4 += d8; }
            } } }
    /* surface */
    static double t[256]; for (int a = 0; a < 256; a++) t[a] = 0;
    { double d0 = 0.03125; double dd6 = 1.0, dd7 = 1.0; for (int o = 0; o < 4; o++) { rsimplex_arr(&r_surf[o], t, (double)(cx * 16), (double)(cz * 16), 16, 16, d0 * 2.0 * dd7 * dd6, d0 * 2.0 * dd7 * dd6, 0.55 / dd6); dd7 *= 1.0; dd6 *= 0.5; } }
    JRand rr; jr_seed(&rr, (i64)((u64)(i64)cx * 341873128712ULL + (u64)(i64)cz * 132897987541ULL));
    for (int kk = 0; kk < 16; ++kk) for (int ll = 0; ll < 16; ++ll) {
        const Biome *bb = bio(b16[ll + kk * 16]); int ii = cx * 16 + kk, jj = cz * 16 + ll; double d0 = t[ll + kk * 16];
        int ak = bb->top, al = bb->filler;
        switch (bb->surf) {
        case S_HILLS: ak = B_GRASS; al = B_DIRT; if ((d0 < -1.0 || d0 > 2.0) && bb->mode == 2) ak = al = B_GRAVEL; else if (d0 > 1.0 && bb->mode != 1) ak = al = B_STONE; break;
        case S_TAIGA_MEGA: ak = B_GRASS; al = B_DIRT; if (d0 > 1.75) ak = B_COARSE_DIRT; else if (d0 > -0.95) ak = B_PODZOL; break;
        case S_SAVANNA_M: ak = B_GRASS; al = B_DIRT; if (d0 > 1.75) ak = al = B_STONE; else if (d0 > -0.5) ak = B_COARSE_DIRT; break;
        case S_SWAMP: { double d1 = rsimplex_pt(&r_af, (double)ii * 0.25, (double)jj * 0.25);
            if (d1 > 0.0) { int k2 = ii & 15, l2 = jj & 15; for (int y = 255; y >= 0; --y) if (RS(l2, y, k2) != B_AIR) { if (y == 62 && RS(l2, y, k2) != B_WATER) { RS(l2, y, k2) = B_WATER; if (d1 < 0.12) RS(l2, y + 1, k2) = B_LILY_PAD; } break; } }
            break; }
        case S_MESA: r_mesa(chunk, bb, &rr, ii, jj, d0); continue;
        }
        r_b(chunk, bb, &rr, ii, jj, d0, ak, al);
    }
}

/* ---- literal caves/canyons (double, recursive) on a [y][z][x] chunk ---- */
static float r_sintab[65536];
__attribute__((unused)) static void r_sin_init(void) { for (int i = 0; i < 65536; i++) r_sintab[i] = (float)sin((double)i * 3.141592653589793 * 2.0 / 65536.0); }
static float r_sin(float f) { return r_sintab[(int)(f * 10430.378F) & 65535]; }
static float r_cos(float f) { return r_sintab[(int)(f * 10430.378F + 16384.0F) & 65535]; }
static uint8_t *rc_chunk; static const uint8_t *rc_b16; static float rc_d[1024];
static int r_floor(double d) { int i = (int)d; return d < (double)i ? i - 1 : i; }
static void rcave_a(long long i, int j, int k, double d0, double d1, double d2, float f, float f1, float f2, int l, int i1, double d3) {
    uint8_t *chunk = rc_chunk;
    double d4 = (double)(j * 16 + 8), d5 = (double)(k * 16 + 8); float f3 = 0, f4 = 0; JRand random; jr_seed(&random, i);
    if (i1 <= 0) { int j1 = 8 * 16 - 16; i1 = j1 - jr_int(&random, j1 / 4); }
    int flag = 0; if (l == -1) { l = i1 / 2; flag = 1; }
    int k1 = jr_int(&random, i1 / 2) + i1 / 4;
    for (int flag1 = jr_int(&random, 6) == 0; l < i1; ++l) {
        double d6 = 1.5 + (double)(r_sin((float)l * 3.1415927F / (float)i1) * f * 1.0F), d7 = d6 * d3;
        float f5 = r_cos(f2), f6 = r_sin(f2);
        d0 += (double)(r_cos(f1) * f5); d1 += (double)f6; d2 += (double)(r_sin(f1) * f5);
        if (flag1) f2 *= 0.92F; else f2 *= 0.7F;
        f2 += f4 * 0.1F; f1 += f3 * 0.1F; f4 *= 0.9F; f3 *= 0.75F;
        { float a = jr_float(&random), b = jr_float(&random), c = jr_float(&random); f4 += (a - b) * c * 2.0F; }
        { float a = jr_float(&random), b = jr_float(&random), c = jr_float(&random); f3 += (a - b) * c * 4.0F; }
        if (!flag && l == k1 && f > 1.0F && i1 > 0) {
            long long s1 = jr_long(&random); float w1 = jr_float(&random) * 0.5F + 0.5F;
            rcave_a(s1, j, k, d0, d1, d2, w1, f1 - 1.5707964F, f2 / 3.0F, l, i1, 1.0);
            long long s2 = jr_long(&random); float w2 = jr_float(&random) * 0.5F + 0.5F;
            rcave_a(s2, j, k, d0, d1, d2, w2, f1 + 1.5707964F, f2 / 3.0F, l, i1, 1.0);
            return;
        }
        if (flag || jr_int(&random, 4) != 0) {
            double d8 = d0 - d4, d9 = d2 - d5, d10 = (double)(i1 - l), d11 = (double)(f + 2.0F + 16.0F);
            if (d8 * d8 + d9 * d9 - d10 * d10 > d11 * d11) return;
            if (d0 >= d4 - 16.0 - d6 * 2.0 && d2 >= d5 - 16.0 - d6 * 2.0 && d0 <= d4 + 16.0 + d6 * 2.0 && d2 <= d5 + 16.0 + d6 * 2.0) {
                int l1 = r_floor(d0 - d6) - j * 16 - 1, i2 = r_floor(d0 + d6) - j * 16 + 1, j2 = r_floor(d1 - d7) - 1, k2 = r_floor(d1 + d7) + 1, l2 = r_floor(d2 - d6) - k * 16 - 1, i3 = r_floor(d2 + d6) - k * 16 + 1;
                if (l1 < 0) l1 = 0;
                if (i2 > 16) i2 = 16;
                if (j2 < 1) j2 = 1;
                if (k2 > 248) k2 = 248;
                if (l2 < 0) l2 = 0;
                if (i3 > 16) i3 = 16;
                int flag2 = 0;
                for (int k3 = l1; !flag2 && k3 < i2; ++k3) for (int j3 = l2; !flag2 && j3 < i3; ++j3) for (int l3 = k2 + 1; !flag2 && l3 >= j2 - 1; --l3) if (l3 >= 0 && l3 < 256) {
                    int b = RS(k3, l3, j3); if (b == B_WATER || b == B_FLOWING_WATER) flag2 = 1;
                    if (l3 != j2 - 1 && k3 != l1 && k3 != i2 - 1 && j3 != l2 && j3 != i3 - 1) l3 = j2; }
                if (!flag2) {
                    for (int j3 = l1; j3 < i2; ++j3) { double d12 = ((double)(j3 + j * 16) + 0.5 - d0) / d6;
                        for (int i4 = l2; i4 < i3; ++i4) { double d13 = ((double)(i4 + k * 16) + 0.5 - d2) / d6; int flag3 = 0;
                            if (d12 * d12 + d13 * d13 < 1.0) for (int j4 = k2; j4 > j2; --j4) { double d14 = ((double)(j4 - 1) + 0.5 - d1) / d7;
                                if (d14 > -0.7 && d12 * d12 + d14 * d14 + d13 * d13 < 1.0) {
                                    int b1 = RS(j3, j4, i4), b2 = j4 + 1 < 256 ? RS(j3, j4 + 1, i4) : B_AIR;
                                    if (b1 == B_GRASS || b1 == B_MYCELIUM) flag3 = 1;
                                    int carv = b1 == B_STONE || b1 == B_DIRT || b1 == B_COARSE_DIRT || b1 == B_PODZOL || b1 == B_GRASS || IS_CLAY(b1) || b1 == B_SANDSTONE || b1 == B_RED_SANDSTONE || b1 == B_MYCELIUM || b1 == B_SNOW_LAYER || ((b1 == B_SAND || b1 == B_RED_SAND || b1 == B_GRAVEL) && !(b2 == B_WATER || b2 == B_FLOWING_WATER));
                                    if (carv) {
                                        if (j4 - 1 < 10) RS(j3, j4, i4) = B_LAVA;
                                        else { RS(j3, j4, i4) = B_AIR; if (b2 == B_SAND) RS(j3, j4 + 1, i4) = B_SANDSTONE; else if (b2 == B_RED_SAND) RS(j3, j4 + 1, i4) = B_RED_SANDSTONE;
                                            int bd = RS(j3, j4 - 1, i4);
                                            if (flag3 && (bd == B_DIRT || bd == B_COARSE_DIRT || bd == B_PODZOL)) { int t = bio(rc_b16[j3 + i4 * 16])->top; if (t == B_PODZOL || t == B_COARSE_DIRT) t = B_DIRT; RS(j3, j4 - 1, i4) = t; } }
                                    } } } } }
                    if (flag) break;
                }
            }
        }
    }
}
static void rcanyon_a(long long i, int j, int k, double d0, double d1, double d2, float f, float f1, float f2, int l, int i1, double d3) {
    uint8_t *chunk = rc_chunk;
    JRand random; jr_seed(&random, i); double d4 = (double)(j * 16 + 8), d5 = (double)(k * 16 + 8); float f3 = 0, f4 = 0;
    if (i1 <= 0) { int j1 = 8 * 16 - 16; i1 = j1 - jr_int(&random, j1 / 4); }
    int flag = 0; if (l == -1) { l = i1 / 2; flag = 1; }
    float f5 = 1.0F;
    for (int k1 = 0; k1 < 256; ++k1) { if (k1 == 0 || jr_int(&random, 3) == 0) { float a = jr_float(&random), b = jr_float(&random); f5 = 1.0F + a * b * 1.0F; } rc_d[k1] = f5 * f5; }
    for (; l < i1; ++l) {
        double d6 = 1.5 + (double)(r_sin((float)l * 3.1415927F / (float)i1) * f * 1.0F), d7 = d6 * d3;
        d6 *= (double)jr_float(&random) * 0.25 + 0.75; d7 *= (double)jr_float(&random) * 0.25 + 0.75;
        float f6 = r_cos(f2), f7 = r_sin(f2);
        d0 += (double)(r_cos(f1) * f6); d1 += (double)f7; d2 += (double)(r_sin(f1) * f6);
        f2 *= 0.7F; f2 += f4 * 0.05F; f1 += f3 * 0.05F; f4 *= 0.8F; f3 *= 0.5F;
        { float a = jr_float(&random), b = jr_float(&random), c = jr_float(&random); f4 += (a - b) * c * 2.0F; }
        { float a = jr_float(&random), b = jr_float(&random), c = jr_float(&random); f3 += (a - b) * c * 4.0F; }
        if (flag || jr_int(&random, 4) != 0) {
            double d8 = d0 - d4, d9 = d2 - d5, d10 = (double)(i1 - l), d11 = (double)(f + 2.0F + 16.0F);
            if (d8 * d8 + d9 * d9 - d10 * d10 > d11 * d11) return;
            if (d0 >= d4 - 16.0 - d6 * 2.0 && d2 >= d5 - 16.0 - d6 * 2.0 && d0 <= d4 + 16.0 + d6 * 2.0 && d2 <= d5 + 16.0 + d6 * 2.0) {
                int l1 = r_floor(d0 - d6) - j * 16 - 1, i2 = r_floor(d0 + d6) - j * 16 + 1, j2 = r_floor(d1 - d7) - 1, k2 = r_floor(d1 + d7) + 1, l2 = r_floor(d2 - d6) - k * 16 - 1, i3 = r_floor(d2 + d6) - k * 16 + 1;
                if (l1 < 0) l1 = 0;
                if (i2 > 16) i2 = 16;
                if (j2 < 1) j2 = 1;
                if (k2 > 248) k2 = 248;
                if (l2 < 0) l2 = 0;
                if (i3 > 16) i3 = 16;
                int flag1 = 0;
                for (int k3 = l1; !flag1 && k3 < i2; ++k3) for (int j3 = l2; !flag1 && j3 < i3; ++j3) for (int l3 = k2 + 1; !flag1 && l3 >= j2 - 1; --l3) if (l3 >= 0 && l3 < 256) {
                    int b = RS(k3, l3, j3); if (b == B_WATER || b == B_FLOWING_WATER) flag1 = 1;
                    if (l3 != j2 - 1 && k3 != l1 && k3 != i2 - 1 && j3 != l2 && j3 != i3 - 1) l3 = j2; }
                if (!flag1) {
                    for (int j3 = l1; j3 < i2; ++j3) { double d12 = ((double)(j3 + j * 16) + 0.5 - d0) / d6;
                        for (int i4 = l2; i4 < i3; ++i4) { double d13 = ((double)(i4 + k * 16) + 0.5 - d2) / d6; int flag2 = 0;
                            if (d12 * d12 + d13 * d13 < 1.0) for (int j4 = k2; j4 > j2; --j4) { double d14 = ((double)(j4 - 1) + 0.5 - d1) / d7;
                                if ((d12 * d12 + d13 * d13) * (double)rc_d[j4 - 1] + d14 * d14 / 6.0 < 1.0) {
                                    int b1 = RS(j3, j4, i4); if (b1 == B_GRASS) flag2 = 1;
                                    if (b1 == B_STONE || b1 == B_DIRT || b1 == B_COARSE_DIRT || b1 == B_PODZOL || b1 == B_GRASS) {
                                        if (j4 - 1 < 10) RS(j3, j4, i4) = B_LAVA;
                                        else { RS(j3, j4, i4) = B_AIR; int bd = RS(j3, j4 - 1, i4); if (flag2 && (bd == B_DIRT || bd == B_COARSE_DIRT || bd == B_PODZOL)) RS(j3, j4 - 1, i4) = bio(rc_b16[j3 + i4 * 16])->top; }
                                    } } } } }
                    if (flag) break;
                }
            }
        }
    }
}
__attribute__((unused)) static void ref_caves(int i, int j, uint8_t *chunk, const uint8_t *b16) {
    rc_chunk = chunk; rc_b16 = b16;
    JRand b; jr_seed(&b, g_seed); long long l = jr_long(&b), i1 = jr_long(&b);
    for (int pass = 0; pass < 2; pass++)
    for (int j1 = i - 8; j1 <= i + 8; ++j1) for (int k1 = j - 8; k1 <= j + 8; ++k1) {
        long long l1 = (long long)((unsigned long long)(long long)j1 * (unsigned long long)l), i2 = (long long)((unsigned long long)(long long)k1 * (unsigned long long)i1);
        jr_seed(&b, l1 ^ i2 ^ g_seed);
        if (pass == 0) {
            int n = jr_int(&b, jr_int(&b, jr_int(&b, 15) + 1) + 1); if (jr_int(&b, 7) != 0) n = 0;
            for (int c = 0; c < n; ++c) {
                double d0 = (double)(j1 * 16 + jr_int(&b, 16)), d1 = (double)jr_int(&b, jr_int(&b, 120) + 8), d2 = (double)(k1 * 16 + jr_int(&b, 16));
                int kk = 1;
                if (jr_int(&b, 4) == 0) { long long s = jr_long(&b); float w = 1.0F + jr_float(&b) * 6.0F; rcave_a(s, i, j, d0, d1, d2, w, 0, 0, -1, -1, 0.5); kk += jr_int(&b, 4); }
                for (int l1b = 0; l1b < kk; ++l1b) {
                    float f = jr_float(&b) * 3.1415927F * 2.0F, f1 = (jr_float(&b) - 0.5F) * 2.0F / 8.0F, f2 = jr_float(&b) * 2.0F + jr_float(&b);
                    if (jr_int(&b, 10) == 0) { float a = jr_float(&b), c2 = jr_float(&b); f2 *= a * c2 * 3.0F + 1.0F; }
                    rcave_a(jr_long(&b), i, j, d0, d1, d2, f2, f, f1, 0, 0, 1.0);
                }
            }
        } else if (jr_int(&b, 50) == 0) {
            double d0 = (double)(j1 * 16 + jr_int(&b, 16)), d1 = (double)(jr_int(&b, jr_int(&b, 40) + 8) + 20), d2 = (double)(k1 * 16 + jr_int(&b, 16));
            float f = jr_float(&b) * 3.1415927F * 2.0F, f1 = (jr_float(&b) - 0.5F) * 2.0F / 8.0F, f2 = (jr_float(&b) * 2.0F + jr_float(&b)) * 2.0F;
            rcanyon_a(jr_long(&b), i, j, d0, d1, d2, f2, f, f1, 0, 0, 3.0);
        }
    }
}
