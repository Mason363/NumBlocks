/* The 3D view: one ray per pixel of a 160 x 120 picture (shown at 2x), walked
 * through the block cache a block at a time until it meets something.
 *
 * Like Minecraft 1.8 with Fast graphics and smooth lighting off: each face is
 * lit by the light of the block in front of it, times its side's shade (top
 * 1.0, bottom 0.5, north/south 0.8, east/west 0.6); grass and leaves take
 * their biome's colour; linear fog fades to the sky's horizon colour at the
 * edge of the view. Leaves are solid, their holes black, as Fast graphics
 * draws them; glass, plants and water let the ray go on through their
 * see-through texels. The sky has its gradient, the sun or the moon, and
 * Minecraft's flat clouds at y = 128. */
#include <math.h>
#include "nb.h"

#define SR 4                       /* picture rows per strip (8 screen rows) */
static uint16_t strip[SCREEN_W * SR * 2] __attribute__((aligned(4)));
static uint16_t cbuf[SR][RW];      /* a strip of the picture */
static float zbuf[SR][RW];         /* and how far each pixel's ray went (entities are hidden behind) */

/* per frame */
static float ox, oy, oz;           /* camera, cache coordinates */
static float fwx, fwy, fwz, rgx, rgz, upx, upy, upz;
static float fog0, fog1;           /* fog start and end (blocks) */
static int fog_r, fog_g, fog_b, sky_r, sky_g, sky_b;   /* 0..255 */
static uint32_t sky_grad[129];   /* the horizon to the sky, by elevation 0..0.4 (r << 20 | g << 10 | b) */
static int sky_top;               /* the cache is empty from this row up: rays going up end there */
static int trace_top = -1;        /* (>= 0) rays going down start on this plane (y): see render_frame */
static int shade[4][16];           /* face shade x sky light level -> 0..32 */
static int shade_blk[4][16];       /* face shade x block light level -> 0..32 */
static float sun_x, sun_y, sun_z;  /* towards the sun */
static float sun_tx, sun_ty;       /* the way the sun moves */
static float cos_c, sin_c;         /* the sky's turn (the celestial angle) */
static int moon_phase, star_k;     /* the moon's phase (0 full), how bright the stars are (0..255) */
static int ss_r, ss_g, ss_b;       /* the sunrise or sunset colour (0..255) */
static float ss_a, ss_side;        /* how strong it is, and on which side (+1 east, -1 west) */
static float cloud_off;
static int cloud_r, cloud_g, cloud_b;   /* the clouds' colour (0..255) */
static float sun_a;                     /* how much of the sun or moon shows (rain hides it) */
static bool night;
static int under_water;
static int cam_x, cam_y, cam_z, cam_i, cam_b;   /* the camera's cell, its index (-1 outside the cache), its block */
static int cam_mi, cam_liq;      /* its region, the liquid it is in (liquid[], 255: none) */
static float cam_fr[3][2];       /* from the eye to its cell's sides: [axis][0 low side, 1 high] */

static const float TAN_V = 0.70020754f;   /* 70 degree vertical field of view */
#define TAN_H (TAN_V * (float)RW / (float)RH)
#define MAX_T 19.0f

static inline int r5(uint16_t c) { return (c >> 11) << 3 | (c >> 13); }
static inline int g6(uint16_t c) { return ((c >> 5) & 63) << 2 | ((c >> 9) & 3); }
static inline int b5(uint16_t c) { return (c & 31) << 3 | ((c >> 2) & 7); }
static inline uint16_t pack(int r, int g, int b) {
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
}

#ifdef HOST
unsigned long st_steps, st_texels, st_jumps, st_pixels, st_dis, st_k0, st_fpfail, st_fpok, st_sky, st_wok;
#define ST(x) (x)
#else
#define ST(x) ((void)0)
#endif

/* ---------------------------------------------------------------- texels */
static inline int texel(int tex, int u, int v) {
  uint8_t p = tex_px[tex][(v * 16 + u) >> 1];
  return (u & 1) ? p >> 4 : p & 15;
}

static uint16_t tint_of(int b, int col) {
  int biome = vbiome[col];
  switch (blk_flags[b] & BF_TINT) {
    case 1: return biome_grass[biome];
    case 2: return biome_foliage[biome];
    case 3: return 0x64CC;   /* spruce 0x619961 */
    case 4: return 0x852A;   /* birch 0x80A755 */
    case 5: return biome_water[biome];
    case 6: return 0x2406;   /* lily pad 0x208030 */
  }
  return 0xFFFF;
}

/* the colour of a texel (RGB565), or -1 if it is see-through */
static inline __attribute__((always_inline)) int texel565(int b, int tex, int u, int v, int col) {
  ST(st_texels++);
  int t = texel(tex, u, v);
  int fl = tex_flags[tex];
  if (!t && (fl & 0x20)) return -1;
  uint16_t c = tex_pal[tex][t];
  if (t >= (fl & 0x1F)) {
    uint16_t k = tint_of(b, col);
    c = pack(r5(c) * r5(k) >> 8, g6(c) * g6(k) >> 8, b5(c) * b5(k) >> 8);
  }
  return c;
}
static inline int texel_rgb(int b, int tex, int u, int v, int col, int *r, int *g, int *bl) {
  int c = texel565(b, tex, u, v, col);
  if (c < 0) return -1;
  *r = r5((uint16_t)c);
  *g = g6((uint16_t)c);
  *bl = b5((uint16_t)c);
  return 0;
}

/* ---------------------------------------------------------------- the sky */
static __attribute__((noinline)) uint16_t sky(float dx, float dy, float dz, float len) {
  float e = dy / len;   /* elevation, -1..1 */
  /* horizon (fog colour) to the sky's colour above; below the horizon, fog */
  uint32_t gc = sky_grad[e <= 0 ? 0 : e >= 0.4f ? 128 : (int)(e * 320)];
  int r = (int)(gc >> 20), g = (int)(gc >> 10) & 1023, b = (int)gc & 1023;
  if (under_water) return pack(r / 4, g / 3, b / 2 + 40);
  /* the sunrise and sunset glow (RenderGlobal's fan): towards the sun, on the horizon, fading above
   * it and below it alike (the fan straddles it: cut there, it hung over a line in the air) */
  if (ss_a > 0) {
    float hl = sqrtf(dx * dx + dz * dz) + 1e-6f, w1 = dx * ss_side / hl, e = fabsf(dy) / len;
    float w2 = 1 - e / (0.31f * ss_a + 0.05f);
    if (w1 > 0 && w2 > 0) {
      int a = (int)(ss_a * w1 * w2 * 256);
      r += ((ss_r - r) * a) >> 8, g += ((ss_g - g) * a) >> 8, b += ((ss_b - b) * a) >> 8;
    }
  }
  /* RenderGlobal.renderSky: the sun, the moon in its phase and the stars turn with the
   * sky; their textures add light (and rain hides them) */
  if (sun_a > 0 && dy > 0) {
    float sd = dx * sun_x + dy * sun_y + dz * sun_z;
    float tx = dx * sun_tx + dy * sun_ty;   /* along the sky's turn; dz across it */
    int sp = -1;
    float u = 0, v = 0;
    if (sd > 0.9f * len) sp = SP_SUN, u = (dz / sd / 0.3f + 1) * 16, v = (tx / sd / 0.3f + 1) * 16;
    else if (sd < -0.95f * len)
      sp = SP_MOON_0 + moon_phase, u = 32 - (dz / -sd / 0.2f + 1) * 16, v = (tx / -sd / 0.2f + 1) * 16;
    if (sp >= 0) {
      int c0 = sp == SP_SUN ? 2 : 8, w = spr_w[sp];
      int iu = (int)u - c0, iv = (int)v - c0;
      if ((unsigned)iu < (unsigned)w && (unsigned)iv < (unsigned)w) {
        uint8_t q = spr_px[spr_off[sp] + iv * ((w + 1) / 2) + (iu >> 1)];
        uint16_t c = spr_pal[sp][(iu & 1) ? q >> 4 : q & 15];
        r += (int)(r5(c) * sun_a), g += (int)(g6(c) * sun_a), b += (int)(b5(c) * sun_a);
      }
    }
    if (star_k > 0) {
      /* renderStars: about 800 white dots over the whole sky (here a pixel each), seen at night */
      float sx = dz, sy = dy * cos_c + -dx * sin_c, sz = -dy * sin_c + -dx * cos_c;
      float ax = fabsf(sx), ay = fabsf(sy), az = fabsf(sz), m = ax > ay ? (ax > az ? ax : az) : (ay > az ? ay : az);
      int face = m == ax ? (sx > 0) : m == ay ? 2 + (sy > 0) : 4 + (sz > 0);
      float p = m == ax ? sy : sx, q = m == az ? sy : sz;
      int cu = (int)((p / m + 1) * 78), cv = (int)((q / m + 1) * 78);
      uint32_t h = ((uint32_t)face * 73856093u) ^ ((uint32_t)cu * 19349663u) ^ ((uint32_t)cv * 83492791u);
      h = (h ^ (h >> 13)) * 1274126177u;
      if (((h ^ (h >> 16)) & 1023) < 6) r += star_k, g += star_k, b += star_k;
    }
  }
  /* clouds: Minecraft's 12-block cells at y = 128.33 */
  if (dy > 0.01f && opt.clouds) {
    float t = (128.33f - (oy + vc_y0)) / dy;
    float cx = (ox + vc_x0) + dx * t + cloud_off, cz = (oz + vc_z0) + dz * t;
    float dist = t * len;
    if (t > 0 && dist < 200) {
      int ix = (int)floorf(cx / 12) & 255, iz = (int)floorf(cz / 12) & 255;
      extern const uint8_t clouds[256 * 256 / 8];
      if (clouds[(iz * 256 + ix) >> 3] & (1 << (ix & 7))) {
        float a = 0.8f * (1 - dist / 200);
        r += (int)((cloud_r - r) * a);
        g += (int)((cloud_g - g) * a);
        b += (int)((cloud_b - b) * a);
      }
    }
  }
  return pack(r, g, b);
}

/* The sky along one picture row, by day (no sunset, no stars): the ray's height part is the
 * row's, so the clouds' plane is as far for all its pixels, and what changes across it is
 * linear in su (the ray's sideways part) */
typedef struct {
  float dy, len2, sd0, sds, tx0, txs, cx0, cxs, cz0, czs, ct;
} SkyRow;
static bool sky_fast(void) { return ss_a <= 0 && star_k <= 0; }
static void sky_row(SkyRow *k, const float *row) {
  k->dy = row[1];
  k->len2 = row[0] * row[0] + row[1] * row[1] + row[2] * row[2];   /* (+ su^2: rg is square to the row) */
  k->sd0 = row[0] * sun_x + row[1] * sun_y + row[2] * sun_z, k->sds = rgx * sun_x + rgz * sun_z;
  k->tx0 = row[0] * sun_tx + row[1] * sun_ty, k->txs = rgx * sun_tx;
  k->ct = row[1] > 0.01f ? (128.33f - (oy + vc_y0)) / row[1] : 0;
  k->cx0 = (ox + vc_x0) + row[0] * k->ct + cloud_off, k->cxs = rgx * k->ct;
  k->cz0 = (oz + vc_z0) + row[2] * k->ct, k->czs = rgz * k->ct;
}
static inline __attribute__((always_inline)) uint16_t sky_px(const SkyRow *k, float su, float dz) {
  /* (what depends on the ray's length compared squared: its root only where it is needed) */
  float l2 = k->len2 + su * su, len = 0, dy = k->dy;
  int gi = 128;
  if (dy <= 0) gi = 0;
  else if (dy * dy < 0.16f * l2) len = sqrtf(l2), gi = (int)(dy / len * 320);
  uint32_t gc = sky_grad[gi];
  int r = (int)(gc >> 20), g = (int)(gc >> 10) & 1023, b = (int)gc & 1023;
  if (under_water) return pack(r / 4, g / 3, b / 2 + 40);
  if (sun_a > 0) {
    float sd = k->sd0 + k->sds * su, tx = k->tx0 + k->txs * su, s2 = sd * sd;
    int sp = -1;
    float u = 0, v = 0;
    if (sd > 0 && s2 > 0.81f * l2) sp = SP_SUN, u = (dz / sd / 0.3f + 1) * 16, v = (tx / sd / 0.3f + 1) * 16;
    else if (sd < 0 && s2 > 0.9025f * l2)
      sp = SP_MOON_0 + moon_phase, u = 32 - (dz / -sd / 0.2f + 1) * 16, v = (tx / -sd / 0.2f + 1) * 16;
    if (sp >= 0) {
      int c0 = sp == SP_SUN ? 2 : 8, w = spr_w[sp];
      int iu = (int)u - c0, iv = (int)v - c0;
      if ((unsigned)iu < (unsigned)w && (unsigned)iv < (unsigned)w) {
        uint8_t q = spr_px[spr_off[sp] + iv * ((w + 1) / 2) + (iu >> 1)];
        uint16_t c = spr_pal[sp][(iu & 1) ? q >> 4 : q & 15];
        r += (int)(r5(c) * sun_a), g += (int)(g6(c) * sun_a), b += (int)(b5(c) * sun_a);
      }
    }
  }
  if (dy > 0.01f && k->ct > 0 && opt.clouds && k->ct * k->ct * l2 < 200 * 200) {
    int ix = (int)floorf((k->cx0 + k->cxs * su) / 12) & 255, iz = (int)floorf((k->cz0 + k->czs * su) / 12) & 255;
    extern const uint8_t clouds[256 * 256 / 8];
    if (clouds[(iz * 256 + ix) >> 3] & (1 << (ix & 7))) {
      if (!len) len = sqrtf(l2);
      float a = 0.8f * (1 - k->ct * len / 200);
      r += (int)((cloud_r - r) * a);
      g += (int)((cloud_g - g) * a);
      b += (int)((cloud_b - b) * a);
    }
  }
  return pack(r, g, b);
}

/* ---------------------------------------------------------------- tracing */
/* blocks with a front (furnaces, pumpkins, chests) turn it to the first open
 * side: south, north, east, west; beds lie towards their other half */
static int front_face(int x, int y, int z) {
  static const int8_t fx[4] = {0, 0, 1, -1}, fz[4] = {1, -1, 0, 0}, ff[4] = {3, 2, 5, 4};
  for (int k = 0; k < 4; k++) {
    int nx = x + fx[k], nz = z + fz[k];
    if ((unsigned)nx >= VCX || (unsigned)nz >= VCZ || !(blk_flags[vc[VC_I(nx, y, nz)]] & BF_OPAQUE)) return ff[k];
  }
  return 3;
}
/* a bed's way (the face its head end points to: 2 -z, 3 +z, 4 -x, 5 +x) */
static int bed_way(int b, int x, int y, int z) {
  static const int8_t fx[4] = {0, 0, -1, 1}, fz[4] = {-1, 1, 0, 0};
  int other = b == B_BED_FOOT ? B_BED_HEAD : B_BED_FOOT;
  for (int k = 0; k < 4; k++) {
    int nx = x + fx[k], nz = z + fz[k];
    if ((unsigned)nx < VCX && (unsigned)nz < VCZ && vc[VC_I(nx, y, nz)] == other) return b == B_BED_FOOT ? 2 + k : (2 + k) ^ 1;
  }
  return 2;
}
static inline int face_tex(int b, int f, int x, int y, int z) {
  if (!blk_front[b] || f < 2) return blk_tex[b][f];
  if (b == B_BED_FOOT || b == B_BED_HEAD) return ((bed_way(b, x, y, z) ^ f) & ~1) == 0 ? blk_front[b] : blk_tex[b][f];
  return f == front_face(x, y, z) ? blk_front[b] : blk_tex[b][f];
}

typedef struct {
  float t;
  int face;        /* 0 -Y, 1 +Y, 2 -Z, 3 +Z, 4 -X, 5 +X */
  int u, v;
} Hit;

/* the ray against a box (sixteenths) inside the cell at (cx, cy, cz), local ray origin p, dir d */
/* the ray from (px, py, pz) (in the cell, its direction's signed inverses iv) meets box bx
 * before leaving the cell (at tl)? */
static bool hit_box(const int8_t *bx, float px, float py, float pz, const float *d, const float *iv, float tl, Hit *h) {
  float t0 = -1e9f, t1 = 1e9f;
  int f0 = 0;
  float p[3] = {px, py, pz};
  static const int axis_face[3][2] = {{4, 5}, {0, 1}, {2, 3}};   /* entering through min / max side */
  for (int a = 0; a < 3; a++) {
    float ta = (bx[a] * (1 / 16.0f) - p[a]) * iv[a], tb = (bx[a + 3] * (1 / 16.0f) - p[a]) * iv[a];
    int fa = axis_face[a][0], fb = axis_face[a][1];
    if (ta > tb) {
      float t = ta;
      ta = tb;
      tb = t;
      int f = fa;
      fa = fb;
      fb = f;
    }
    if (ta > t0) t0 = ta, f0 = fa;
    if (tb < t1) t1 = tb;
  }
  if (t0 > t1 || t1 < 0 || t0 > tl) return false;
  if (t0 < 0) t0 = 0;
  float hx = px + d[0] * t0, hy = py + d[1] * t0, hz = pz + d[2] * t0;
  h->t = t0;
  h->face = f0;
  int ux, vy;
  switch (f0) {
    case 0: case 1: ux = (int)(hx * 16); vy = (int)(hz * 16); break;
    case 2: ux = 15 - (int)(hx * 16); vy = 15 - (int)(hy * 16); break;
    case 3: ux = (int)(hx * 16); vy = 15 - (int)(hy * 16); break;
    case 4: ux = (int)(hz * 16); vy = 15 - (int)(hy * 16); break;
    default: ux = 15 - (int)(hz * 16); vy = 15 - (int)(hy * 16); break;
  }
  h->u = ux < 0 ? 0 : ux > 15 ? 15 : ux;
  h->v = vy < 0 ? 0 : vy > 15 ? 15 : vy;
  return true;
}

/* the two crossed planes of a plant, local ray origin p and direction d, between t0 and t1 */
static bool hit_cross(int tex, float px, float py, float pz, float dx, float dy, float dz, float t0, float t1, Hit *h) {
  float best = 1e9f;
  for (int k = 0; k < 2; k++) {
    /* plane x = z (k 0) or x = 1 - z (k 1) */
    float den = k ? dx + dz : dx - dz;
    if (fabsf(den) < 1e-6f) continue;
    float t = k ? (1 - px - pz) / den : (pz - px) / den;
    if (t < t0 || t > t1 || t >= best) continue;
    float hx = px + dx * t, hy = py + dy * t;
    if (hx < 0 || hx >= 1 || hy < 0 || hy >= 1) continue;
    int u = (int)(hx * 16), v = 15 - (int)(hy * 16);
    if (v < 0 || v > 15) continue;
    if (!texel(tex, u, v) && (tex_flags[tex] & 0x20)) continue;
    best = t;
    h->t = t;
    h->face = 3;
    h->u = u;
    h->v = v;
  }
  return best < 1e9f;
}

/* shade a face texel: face shade, light, fog. Colours are shaded in RGB565
 * directly, red and blue in one multiply, green in another (s: 0..32) */
static float fog_k;   /* 1 / (fog1 - fog0) */
static inline uint16_t shade565(uint16_t c, int s) {
  uint32_t rb = ((uint32_t)(c & 0xF81F) * (uint32_t)s >> 5) & 0xF81F, g = ((uint32_t)(c & 0x07E0) * (uint32_t)s >> 5) & 0x07E0;
  return (uint16_t)(rb | g);
}
static inline uint16_t mix565(uint16_t a, uint16_t b, int k) {   /* a towards b by k/32 */
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81F, B = (b | (uint32_t)b << 16) & 0x07E0F81F;
  uint32_t m = (A + (((B - A) * (uint32_t)k) >> 5)) & 0x07E0F81F;
  return (uint16_t)(m | m >> 16);
}
static uint16_t fog565;
/* the shade (0..32) of a face side lit by a light byte (sky | block << 4): the brighter of the two */
static inline int light_shade(int side, int lb) {
  int a = shade[side][lb & 15], b = shade_blk[side][lb >> 4];
  return a > b ? a : b;
}
static inline uint16_t lit565(uint16_t c, int face, int light, float dist) {
  static const uint8_t side[6] = {3, 0, 1, 1, 2, 2};   /* 0.5, 1.0, 0.8, 0.6 */
  c = shade565(c, light_shade(side[face], light));
  if (dist > fog0) {
    int k = dist >= fog1 ? 32 : (int)((dist - fog0) * fog_k * 32);
    c = mix565(c, fog565, k);
  }
  return c;
}
static inline uint16_t lit(int r, int g, int b, int face, int light, float dist) {
  return lit565(pack(r, g, b), face, light, dist);
}

/* what the last trace met: 1 a face (hit_f, of plane hit_p) of a whole block
 * or a block of one box, 2 the sky, 3 a plant (cell index hit_i), 4 a face of
 * a whole block seen through the top of water (hit_w), 0 anything else */
static int hit_kind, hit_f, hit_i;
static int hit_l;   /* 1: the light the face was lit with (its cell: hit_i) */
static int hit_p;   /* 1, 4: the face's plane, its coordinate along the face's axis (sixteenths) */
static int hit_w;   /* 4: a face seen through the top of water, whose plane is this (sixteenths) */
static float last_t;   /* where the last pixel's ray stopped (in lengths of its direction), 1e9 for the sky */
static int brk_i = -1, brk_stage;   /* the block being broken (cache index) and its crack stage */
static inline int cracked(int tc, int i, int u, int v) {
  if (i == brk_i && (cracks[brk_stage][(v * 16 + u) >> 3] >> (u & 7) & 1)) tc = shade565((uint16_t)tc, 13);
  return tc;
}
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* a block that is one box, the same wherever it is (its faces are planes like a cube's) */
static inline bool one_box(int b) {
  int m = blk_model[b];
  return m == M_SLAB || m == M_LAYER || m == M_CACTUS || m == M_CHEST;
}

/* the cell a ray came from into cell i (x, y, z) through its face `face` (-1: none, it starts
 * there); i itself if that is outside the cache */
static inline int came_from(int i, int face, int x, int y, int z) {
  switch (face) {
    case 0: return y > 0 ? i - VCX * VCZ : i;
    case 1: return y < VCY - 1 ? i + VCX * VCZ : i;
    case 2: return z > 0 ? i - VCX : i;
    case 3: return z < VCZ - 1 ? i + VCX : i;
    case 4: return x > 0 ? i - 1 : i;
    case 5: return x < VCX - 1 ? i + 1 : i;
  }
  return i;
}

/* a ray: its direction, and a see-through layer (water) it went through: its colour and how much */
typedef struct {
  float dx, dy, dz, len, ivx, ivy, ivz;
  int wr, wg, wb, wa, inside;
} Ray;

/* what the ray meets in cell i (x, y, z) of block b, entered at t through `face`, with tl to go
 * to the cell's far side: its colour, or -1 to go on (kept out of the trace's loop, whose
 * registers it would take) */
static __attribute__((noinline)) int cell_hit(Ray *R, int b, int i, int x, int y, int z, int face, float t, float tl) {
  int m = blk_model[b];
  int col = z * VCX + x;
  float lx = ox + R->dx * t - x, ly = oy + R->dy * t - y, lz = oz + R->dz * t - z;
  if (m == M_CUBE || m == M_LEAVES || m == M_GLASS) {
    /* the face the ray came in through */
    float hx = lx, hy = ly, hz = lz;
    int u, v, f = face < 0 ? 1 : face;
    switch (f) {
      case 0: case 1: u = (int)(hx * 16); v = (int)(hz * 16); break;
      case 2: u = 15 - (int)(hx * 16); v = 15 - (int)(hy * 16); break;
      case 3: u = (int)(hx * 16); v = 15 - (int)(hy * 16); break;
      case 4: u = (int)(hz * 16); v = 15 - (int)(hy * 16); break;
      default: u = 15 - (int)(hz * 16); v = 15 - (int)(hy * 16); break;
    }
    u &= 15;
    v &= 15;
    int tc = texel565(b, blk_front[b] ? face_tex(b, f, x, y, z) : blk_tex[b][f], u, v, col);
    if (tc < 0 && m == M_LEAVES && !opt.fancy) tc = 0;   /* Fast graphics: the holes are black */
    if (tc >= 0) {
      tc = cracked(tc, i, u, v);
      last_t = t;
      int lt = vl[came_from(i, face, x, y, z)];
      uint16_t c = lit565((uint16_t)tc, f, lt, t * R->len);
      if (R->wa) {
        c = pack((r5(c) * (256 - R->wa) + R->wr * R->wa) >> 8, (g6(c) * (256 - R->wa) + R->wg * R->wa) >> 8,
                 (b5(c) * (256 - R->wa) + R->wb * R->wa) >> 8);
        if (face >= 0 && hit_w >= 0) hit_kind = 4, hit_f = f;
      } else if (face >= 0) hit_kind = 1, hit_f = f, hit_i = i, hit_l = lt;
      hit_p = (((f >> 1) == 0 ? y : (f >> 1) == 1 ? z : x) + (f & 1)) * 16;
      return c;
    }
  } else if (m == M_CROSS) {
    Hit h = {0};
    float t1 = tl;
    if (hit_cross(blk_tex[b][3], lx, ly, lz, R->dx, R->dy, R->dz, 0, t1, &h)) {
      int tc = texel565(b, blk_tex[b][3], h.u, h.v, col);   /* (hit_cross met a texel that is there) */
      last_t = t + h.t;
      uint16_t c = lit565((uint16_t)tc, 1, vl[i], (t + h.t) * R->len);
      if (R->wa) c = pack((r5(c) * (256 - R->wa) + R->wr * R->wa) >> 8, (g6(c) * (256 - R->wa) + R->wg * R->wa) >> 8,
                       (b5(c) * (256 - R->wa) + R->wb * R->wa) >> 8);
      else hit_kind = 3, hit_i = i;
      return c;
    }
  } else {
    int8_t shp[5][6];
    const int8_t (*bxs)[6] = shp;   /* the shape: its boxes */
    int nbx;
    if (m == M_LIQUID) {
      /* BlockLiquid: (8 - level) / 9 high, full when falling or under the same liquid */
      static const int8_t LIQH[9] = {14, 12, 11, 9, 7, 5, 4, 2, 16};
      int lv = blk_meta[b] > 8 ? 8 : blk_meta[b];
      int above = y + 1 < VCY ? vc[i + VCX * VCZ] : B_AIR;
      if (is_water(b) ? is_water(above) : is_lava(above)) lv = 8;
      shp[0][0] = shp[0][1] = shp[0][2] = 0, shp[0][3] = shp[0][5] = 16, shp[0][4] = LIQH[lv];
      nbx = 1;
    } else if (m == M_LADDER || m == M_VINE || m == M_FENCE || m == M_PANE || m == M_DOOR)
      nbx = block_boxes(b, x + vc_x0, y + vc_y0, z + vc_z0, shp);   /* (shaped by what is around) */
    else nbx = blk_nbox[b], bxs = blk_box[b];
    Hit h = {0}, hk;
    h.t = 1e9f;
    float dd[3] = {R->dx, R->dy, R->dz}, iv[3] = {R->ivx, R->ivy, R->ivz}, tl0 = tl;
    for (int k = 0; k < nbx; k++)
      if (hit_box(bxs[k], lx, ly, lz, dd, iv, tl0 + 1e-4f, &hk) && hk.t < h.t) h = hk;
    if (h.t < 1e9f) {
      int tex = face_tex(b, h.face, x, y, z);
      if (m == M_BED && h.face == 1) {
        /* the bed's top turned so the pillow is at its head */
        int w = bed_way(b, x, y, z), u = h.u, v = h.v;
        if (w == 3) h.u = 15 - u, h.v = 15 - v;
        else if (w == 5) h.u = v, h.v = 15 - u;
        else if (w == 4) h.u = 15 - v, h.v = u;
      }
      int tc = texel565(b, tex, h.u, h.v, col);
      if (tc >= 0) {
        /* (a face R->inside the cell is lit by the cell's light, one on its side by the cell in front) */
        int lt = vl[m == M_LIQUID || m == M_TORCH || h.t > 0 ? i : came_from(i, face, x, y, z)];
        uint16_t c = lit565((uint16_t)tc, h.face, lt, (t + h.t) * R->len);
        if (!R->wa && one_box(b) && i != cam_i) {
          /* a face of a block of one box (slab, snow layer...): the pixels between can find it too */
          int a = h.face >> 1, k = a == 0 ? 1 : a == 1 ? 2 : 0;
          hit_kind = 1, hit_f = h.face, hit_i = i, hit_l = lt;
          hit_p = (a == 0 ? y : a == 1 ? z : x) * 16 + bxs[0][k + ((h.face & 1) ? 3 : 0)];
        }
        if (m == M_LIQUID && !R->wa) {
          /* water: see through it (lava is opaque) */
          if (is_water(b)) {
            R->wr = r5(c), R->wg = g6(c), R->wb = b5(c), R->wa = 150;
            R->inside = b;
            hit_w = h.face == 1 ? y * 16 + shp[0][4] : -1;
            return -1;
          }
        }
        if (R->wa) c = pack((r5(c) * (256 - R->wa) + R->wr * R->wa) >> 8, (g6(c) * (256 - R->wa) + R->wg * R->wa) >> 8,
                         (b5(c) * (256 - R->wa) + R->wb * R->wa) >> 8);
        last_t = t + h.t;
        return c;
      }
    }
  }
  return -1;
}

/* the top of each block's shape, sixteenths (16: whole, or shaped by its place): a ray going
 * over a lower one (a snow layer, a slab) has nothing to meet in its cell */
static uint8_t shape_top[B_COUNT];
static uint8_t liquid[B_COUNT];   /* 1 water, 2 lava (a ray in one goes through more of it) */
static void shape_tops(void) {
  for (int b = 0; b < B_COUNT; b++) {
    liquid[b] = blk_model[b] != M_LIQUID ? 0 : is_water(b) ? 1 : is_lava(b) ? 2 : 0;
    int m = blk_model[b], top = 16;
    if (blk_nbox[b] && !(m == M_LIQUID || m == M_LADDER || m == M_VINE || m == M_FENCE || m == M_PANE || m == M_DOOR)) {
      top = 0;
      for (int k = 0; k < blk_nbox[b]; k++)
        if (blk_box[b][k][4] > top) top = blk_box[b][k][4];
    }
    shape_top[b] = (uint8_t)top;
  }
}

static uint16_t trace(float dx, float dy, float dz) {
  hit_kind = 0;
  hit_w = -1;
  last_t = 1e9f;
  ST(st_pixels++);
  float len = sqrtf(dx * dx + dy * dy + dz * dz);
  int x = cam_x, y = cam_y, z = cam_z;
  int bx = dx > 0, by = dy > 0, bz = dz > 0, sx = 2 * bx - 1, sy = 2 * by - 1, sz = 2 * bz - 1;
  /* signed inverses: the ray reaches plane x = X at t = (X - ox) * ivx */
  /* (a 0 component steps the negative way: its inverse must be "minus infinity", or the
   * boundaries behind the camera would come first) */
  float ivx = dx != 0 ? 1 / dx : -1e9f, ivy = dy != 0 ? 1 / dy : -1e9f, ivz = dz != 0 ? 1 / dz : -1e9f;
  float idx = fabsf(ivx), idy = fabsf(ivy), idz = fabsf(ivz);
  float tx = cam_fr[0][bx] * ivx, ty = cam_fr[1][by] * ivy, tz = cam_fr[2][bz] * ivz;
  float tmax = MAX_T / len, t = 0;
  /* the camera above the cache (high in the air, the cache kept on the ground): rays going down come
   * in through its top; any other way out of it, only the sky */
  int top = trace_top;
  if (cam_i < 0) {
    if (dy >= 0 || oy < VCY || cam_x < 0 || cam_x >= VCX || cam_z < 0 || cam_z >= VCZ) return hit_kind = 2, sky(dx, dy, dz, len);
    if (top < 0) top = VCY;
  }
  int i = cam_i;
  Ray R;   /* (in the block the camera is in: water...) */
  R.dx = dx, R.dy = dy, R.dz = dz, R.len = len, R.ivx = ivx, R.ivy = ivy, R.ivz = ivz, R.wa = 0, R.inside = cam_b;
  /* steps through the cache and its regions (mi: the region of cell i) along each axis; a
   * step into a region's first cell (ex0...) checks the region */
  const int ex0 = 3 - 3 * bx, ey0 = 3 - 3 * by, ez0 = 3 - 3 * bz;
  const int fx = 5 - bx, fy = 1 - by, fz = 3 - bz;
  const int iy = sy * VCX * VCZ, iz = sz * VCX, my = sy * MCZ * MCX, mz = sz * MCX;
  int mi = cam_mi;
  /* the face a cell was entered through is the axis crossed last (the latest of the sides
   * behind), except where the ray starts (-1) or comes out of a jump (jf, at t = tj) */
  float tj = 0;
  int jf = -1;
  int in_liq = cam_liq;   /* (the liquid the ray is in: cells of it are skipped) */
  if (top >= 0 && dy < 0) {
    /* start where the ray comes down through plane y = top, in through the top of the cell below */
    float t0 = (top - oy) * ivy;
    if (t0 > 0 || cam_i < 0) {
      if (t0 > tmax) goto out;
      int x0 = (int)floorf(ox + dx * t0), z0 = (int)floorf(oz + dz * t0);
      if ((unsigned)x0 < VCX && (unsigned)z0 < VCZ && (unsigned)(top - 1) < VCY) {
        x = x0, y = top - 1, z = z0;
        t = tj = t0, jf = 1;
        tx = (x + bx - ox) * ivx, ty = (y - oy) * ivy, tz = (z + bz - oz) * ivz;
        i = VC_I(x, y, z), mi = MC_I(x, y, z);
      } else if (cam_i < 0)
        goto out;
    }
  }
  if (!vmac[mi] || (vmac[mi] == 2 && in_liq == 1)) goto jump;
  for (;;) {
    ST(st_steps++);
    int b = vc[i];
    if (b != B_AIR && liquid[b] != in_liq) {
      float te = fminf(fminf(tx, ty), tz);
      /* (over a low shape the whole way through its cell: nothing there) */
      if (shape_top[b] >= 16 || oy + dy * (dy > 0 ? t : te) - y <= shape_top[b] * (1 / 16.0f)) {
        float px = tx - idx, py = ty - idy, pz = tz - idz;
        int face = t == tj ? jf : px > py ? (px > pz ? fx : fz) : (py > pz ? fy : fz);
        int c = cell_hit(&R, b, i, x, y, z, face, t, te - t);
        if (c >= 0) return (uint16_t)c;
        in_liq = liquid[R.inside] ? liquid[R.inside] : 255;   /* (it may have gone into water) */
      }
    }
    /* the next cell */
    if (tx < ty && tx < tz) {
      t = tx;
      tx += idx;
      x += sx;
      if ((unsigned)x >= VCX) break;
      i += sx;
      if ((x & 3) != ex0) goto next;
      mi += sx;
    } else if (ty < tz) {
      t = ty;
      ty += idy;
      y += sy;
      if ((unsigned)y >= VCY) break;
      i += iy;
      if ((y & 3) != ey0) goto next;
      mi += my;
    } else {
      t = tz;
      tz += idz;
      z += sz;
      if ((unsigned)z >= VCZ) break;
      i += iz;
      if ((z & 3) != ez0) goto next;
      mi += mz;
    }
    if (!vmac[mi] || (vmac[mi] == 2 && in_liq == 1)) {
    jump:;
      /* an empty 4 x 4 x 4 region (or all water, seen from in the water): on from region to
       * region (the times the ray crosses their sides) while they are empty */
      bool wet = in_liq == 1;
      int rx = x >> 2, ry = y >> 2, rz = z >> 2, ax, reg;
      float ex = ((rx + bx) * 4 - ox) * ivx, ey = ((ry + by) * 4 - oy) * ivy, ez = ((rz + bz) * 4 - oz) * ivz, te;
      for (;;) {
        ST(st_jumps++);
        if (ex < ey && ex < ez) {
          te = ex, ax = 0, ex += 4 * idx, rx += sx, mi += sx;
          if ((unsigned)rx >= MCX) goto out;
        } else if (ey < ez) {
          te = ey, ax = 1, ey += 4 * idy, ry += sy, mi += my;
          if ((unsigned)ry >= MCY || (sy > 0 && ry * 4 >= sky_top)) goto out;
        } else {
          te = ez, ax = 2, ez += 4 * idz, rz += sz, mi += mz;
          if ((unsigned)rz >= MCZ) goto out;
        }
        if (te > tmax) goto out;
        reg = vmac[mi];
        if (reg && !(reg == 2 && wet)) break;
      }
      t = tj = te;
      jf = ax == 0 ? fx : ax == 1 ? fy : fz;
      /* the cell just past the side: on the crossing axis the region's first; on the others
       * where the ray is, kept in the region (rounding must not put it in another) */
      float hx = ox + dx * te, hy = oy + dy * te, hz = oz + dz * te;
      x = ax == 0 ? rx * 4 + (bx ? 0 : 3) : clampi((int)hx, rx * 4, rx * 4 + 3);   /* ((int): the clamp does the floor) */
      y = ax == 1 ? ry * 4 + (by ? 0 : 3) : clampi((int)hy, ry * 4, ry * 4 + 3);
      z = ax == 2 ? rz * 4 + (bz ? 0 : 3) : clampi((int)hz, rz * 4, rz * 4 + 3);
      tx = (x + bx - ox) * ivx;
      ty = (y + by - oy) * ivy;
      tz = (z + bz - oz) * ivz;
      i = VC_I(x, y, z);
      continue;
    }
  next:
    if (t > tmax) break;
  }
out:;
  uint16_t c = sky(dx, dy, dz, len);
  int wa = R.wa;
  if (!wa) hit_kind = 2;
  if (wa) c = pack((r5(c) * (256 - wa) + R.wr * wa) >> 8, (g6(c) * (256 - wa) + R.wg * wa) >> 8, (b5(c) * (256 - wa) + R.wb * wa) >> 8);
  return c;
}

/* a pixel between samples that all met faces of the same plane (same side,
 * same coordinate): the plane gives the point, and the block there is drawn
 * if it really shows that face; otherwise (-1) the pixel is traced */
static int face_pixel(float dx, float dy, float dz, int plane, int f, bool wet) {
  int a = f >> 1;
  float t = (plane * (1 / 16.0f) - (a == 0 ? oy : a == 1 ? oz : ox)) / (a == 0 ? dy : a == 1 ? dz : dx);
  if (!(t > 0)) return -1;
  float hx = ox + dx * t, hy = oy + dy * t, hz = oz + dz * t;
  if (hx < 0 || hy < 0 || hz < 0) return -1;
  int x = (int)hx, y = (int)hy, z = (int)hz;
  /* the block the face is of: on the plane's near side for faces 1 3 5 (its max side), far for 0 2 4 */
  int c = (f & 1) ? (plane - 1) >> 4 : plane >> 4;
  if (a == 0) y = c;
  else if (a == 1) z = c;
  else x = c;
  if ((unsigned)x >= VCX || (unsigned)y >= VCY || (unsigned)z >= VCZ) return -1;
  int i = VC_I(x, y, z), b = vc[i], m = blk_model[b];
  float lx = hx - x, ly = hy - y, lz = hz - z;
  int edge = plane - c * 16;   /* where the face is in its cell (0, 16: on its side) */
  bool cube = m == M_CUBE || m == M_LEAVES || m == M_GLASS;
  if (!cube) {
    /* a block of one box: the face must be its face, and the point on it */
    if (!one_box(b)) return -1;
    const int8_t *q = blk_box[b][0];
    int k = a == 0 ? 1 : a == 1 ? 2 : 0;
    if (q[k + ((f & 1) ? 3 : 0)] != edge) return -1;
    float u16 = (a == 2 ? lz : lx) * 16, v16 = (a == 0 ? lz : ly) * 16;
    int ku = a == 2 ? 2 : 0, kv = a == 0 ? 2 : 1;
    if (u16 < q[ku] || u16 > q[ku + 3] || v16 < q[kv] || v16 > q[kv + 3]) return -1;
  } else if (edge & 15) return -1;
  int light;
  if ((edge & 15) == 0) {
    /* on the cell's side: lit by the cell in front, which must be clear */
    static const int8_t nx[6] = {0, 0, 0, 0, -1, 1}, ny[6] = {-1, 1, 0, 0, 0, 0}, nz[6] = {0, 0, -1, 1, 0, 0};
    int px = x + nx[f], py = y + ny[f], pz = z + nz[f];
    light = 15;
    if ((unsigned)px < VCX && (unsigned)py < VCY && (unsigned)pz < VCZ) {
      int fi = VC_I(px, py, pz);
      /* hidden, or something stands in front: trace (wet: water in front is clear) */
      if (vc[fi] != B_AIR && !(wet && is_water(vc[fi]))) return -1;
      light = vl[fi];
    }
  } else light = vl[i];
  int u, v;
  switch (f) {
    case 0: case 1: u = (int)(lx * 16); v = (int)(lz * 16); break;
    case 2: u = 15 - (int)(lx * 16); v = 15 - (int)(ly * 16); break;
    case 3: u = (int)(lx * 16); v = 15 - (int)(ly * 16); break;
    case 4: u = (int)(lz * 16); v = 15 - (int)(ly * 16); break;
    default: u = 15 - (int)(lz * 16); v = 15 - (int)(ly * 16); break;
  }
  if ((unsigned)u > 15 || (unsigned)v > 15) return -1;
  int tc = texel565(b, blk_front[b] ? face_tex(b, f, x, y, z) : blk_tex[b][f], u, v, z * VCX + x);
  if (tc < 0 && m == M_LEAVES && !opt.fancy) tc = 0;
  if (tc < 0) return -1;
  if (cube) tc = cracked(tc, i, u, v);
  last_t = t;
  float d2 = t * t * (dx * dx + dy * dy + dz * dz);
  return lit565((uint16_t)tc, f, light, d2 > fog0 * fog0 ? sqrtf(d2) : 0);
}

/* the samples of one row of the coarse grid (every other pixel, and one past the right edge) */
typedef struct {
  uint16_t c;
  uint8_t kind, f;
  int16_t plane;  /* a face's plane: its coordinate along the face's axis (sixteenths) */
  int16_t w;      /* (kind 4) the water's top plane, sixteenths */
  uint16_t cell;  /* (kinds 1, 3) the cell met */
  uint8_t x, y, z, light;   /* (kind 1) the cell's place and the face's light */
  /* (kind 1) what the face's pixels need: its texture, its tint (the biome's), the shade its light
   * gives, and: 1 nothing stands in front, 2 a whole block (cracks show), 4 Fast leaves (holes black) */
  uint16_t tex, tint;
  uint8_t shade, flags;
  float t;
} Sample;
/* (the ray of picture column px across: rgx, rgz times this, on top of the row's base) */
static float col_su[RW + 1];

/* A 4 x 4 cell of daytime sky. Along a row of it where the sun and the moon cannot be (the
 * brighter of its ends too far from them) the sky's gradient goes from what it is at one end to
 * what it is at the other (worked out there, the rest in between: it bends little over 4
 * pixels), its colours packed once a frame (sky_pk); only a pixel with a cloud in front is
 * worked out on its own */
static uint16_t sky_pk[129];   /* (render_frame) */
static __attribute__((unused)) void sky_fill(uint16_t (*cb)[RW], float (*zb)[RW], float (*rb)[3], const SkyRow *skr, int x0) {
  float s0 = col_su[x0], s3 = col_su[x0 + 3], lo2 = fminf(s0 * s0, s3 * s3);
  bool mid = s0 < 0 && s3 > 0;   /* (the elevation is highest where su is 0: not between its ends) */
  for (int r = 0; r < SR; r++) {
    const SkyRow *k = &skr[r];
    float dy = k->dy, l2 = k->len2 + lo2;
    bool plain = !under_water && !mid;
    if (plain && sun_a > 0) {
      float a = fabsf(k->sd0 + k->sds * s0), b = fabsf(k->sd0 + k->sds * s3), m = fmaxf(a, b);
      plain = m * m <= 0.81f * l2;
    }
    if (!plain) {
      for (int x = x0; x < x0 + 4; x++) cb[r][x] = sky_px(k, col_su[x], rb[r][2] + rgz * col_su[x]);
    } else {
      /* the gradient's place (320 a unit of elevation) at the row's ends, and its step a pixel */
      float g0 = 0, gs = 0;
      if (dy > 0) {
        g0 = dy / sqrtf(k->len2 + s0 * s0) * 320;
        gs = (dy / sqrtf(k->len2 + s3 * s3) * 320 - g0) * (1 / 3.0f);
      }
      bool cloud = dy > 0.01f && k->ct > 0 && opt.clouds && k->ct * k->ct * l2 < 200 * 200;
      for (int q = 0; q < 4; q++) {
        int x = x0 + q;
        float g = g0 + gs * q;
        uint16_t c = sky_pk[g >= 128 ? 128 : (int)g];
        if (cloud) {
          float su = col_su[x];
          int ix = (int)floorf((k->cx0 + k->cxs * su) / 12) & 255, iz = (int)floorf((k->cz0 + k->czs * su) / 12) & 255;
          extern const uint8_t clouds[256 * 256 / 8];
          if (clouds[(iz * 256 + ix) >> 3] & (1 << (ix & 7))) {
            float pl2 = k->len2 + su * su;
            if (k->ct * k->ct * pl2 < 200 * 200) c = sky_px(k, su, rb[r][2] + rgz * su);
          }
        }
        cb[r][x] = c;
      }
    }
    for (int x = x0; x < x0 + 4; x++) zb[r][x] = 1e9f;
  }
}

/* a pixel between samples that all met the same face of the same block: that face there */
static int face_cell(float dx, float dy, float dz, const Sample *s) {
  if (!(s->flags & 1)) return -1;   /* something in front the samples went past (grass): trace */
  int f = s->f, a = f >> 1;
  float t = (s->plane * (1 / 16.0f) - (a == 0 ? oy : a == 1 ? oz : ox)) / (a == 0 ? dy : a == 1 ? dz : dx);
  float lx = ox + dx * t - s->x, ly = oy + dy * t - s->y, lz = oz + dz * t - s->z;
  int u, v;
  switch (f) {
    case 0: case 1: u = (int)(lx * 16); v = (int)(lz * 16); break;
    case 2: u = 15 - (int)(lx * 16); v = 15 - (int)(ly * 16); break;
    case 3: u = (int)(lx * 16); v = 15 - (int)(ly * 16); break;
    case 4: u = (int)(lz * 16); v = 15 - (int)(ly * 16); break;
    default: u = 15 - (int)(lz * 16); v = 15 - (int)(ly * 16); break;
  }
  if ((unsigned)u > 15 || (unsigned)v > 15) return -1;
  int tex = s->tex, tx = texel(tex, u, v), fl = tex_flags[tex];
  uint16_t c;
  if (!tx && (fl & 0x20)) {
    if (!(s->flags & 4)) return -1;
    c = 0;   /* (Fast leaves: black) */
  } else {
    c = tex_pal[tex][tx];
    if (tx >= (fl & 0x1F)) {
      uint16_t k = s->tint;
      c = pack(r5(c) * r5(k) >> 8, g6(c) * g6(k) >> 8, b5(c) * b5(k) >> 8);
    }
    if (s->flags & 2) c = (uint16_t)cracked(c, s->cell, u, v);
  }
  last_t = t;
  c = shade565(c, s->shade);
  float d2 = t * t * (dx * dx + dy * dy + dz * dz);
  if (d2 > fog0 * fog0) {
    float dist = sqrtf(d2);
    c = mix565(c, fog565, dist >= fog1 ? 32 : (int)((dist - fog0) * fog_k * 32));
  }
  return c;
}

/* A 4 x 4 cell whose corners and middle all met the same face of the same block, nothing in
 * front: that face over the whole cell. Its texture's palette comes tinted and shaded once
 * (kept for the next cells, mostly the same); a top or bottom face is met as far all along a
 * row, its texels then a line across it. False for a pixel it could not draw (traced). */
static __attribute__((unused)) void face_fill(uint16_t (*cb)[RW], float (*zb)[RW], float (*rb)[3], int x0, const Sample *s) {
  static uint16_t pal[16], k_tex = 0xFFFF, k_tint;
  static uint8_t k_shade;
  int tex = s->tex, fl = tex_flags[tex];
  if (tex != k_tex || s->tint != k_tint || s->shade != k_shade) {
    k_tex = (uint16_t)tex, k_tint = s->tint, k_shade = s->shade;
    for (int n = 0; n < 16; n++) {
      uint16_t c = tex_pal[tex][n];
      if (n >= (fl & 0x1F)) c = pack(r5(c) * r5(k_tint) >> 8, g6(c) * g6(k_tint) >> 8, b5(c) * b5(k_tint) >> 8);
      pal[n] = shade565(c, k_shade);
    }
  }
  /* the plane's axis, and the axes across it the texture's u and v go along (15 - when flipped) */
  static const uint8_t KA[3] = {1, 2, 0}, KU[3] = {0, 0, 2}, KV[3] = {2, 1, 1};
  int f = s->f, a = f >> 1, ka = KA[a], ku = KU[a], kv = KV[a];
  int fu = f == 2 || f == 5 ? 15 : 0, fv = a ? 15 : 0;
  const float o[3] = {ox, oy, oz}, sc[3] = {s->x, s->y, s->z};
  float K = s->plane * (1 / 16.0f) - o[ka], U0 = o[ku] - sc[ku], V0 = o[kv] - sc[kv];
  const uint8_t *px = tex_px[tex];
  bool see = fl & 0x20, black = s->flags & 4;
  for (int r = 0; r < SR; r++) {
    const float *b = rb[r];
    float tr = a == 0 ? K / b[1] : 0;   /* (a top or bottom face: the same all along the row) */
    for (int x = x0; x < x0 + 4; x++) {
      float su = col_su[x], d[3] = {b[0] + rgx * su, b[1], b[2] + rgz * su};
      float t = tr;
      if (a) t = K / d[ka];
      int iu = (int)((U0 + d[ku] * t) * 16), iv = (int)((V0 + d[kv] * t) * 16);
      uint16_t c;
      if ((unsigned)iu > 15 || (unsigned)iv > 15) goto miss;
      int u = iu ^ fu, v = iv ^ fv, p = px[(v * 16 + u) >> 1], tx = (u & 1) ? p >> 4 : p & 15;
      if (!tx && see) {
        if (!black) goto miss;
        c = 0;   /* (Fast leaves: black) */
      } else c = pal[tx];
      float d2 = t * t * (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (d2 > fog0 * fog0) {
        float dist = sqrtf(d2);
        c = mix565(c, fog565, dist >= fog1 ? 32 : (int)((dist - fog0) * fog_k * 32));
      }
      cb[r][x] = c, zb[r][x] = t;
      continue;
    miss:
      cb[r][x] = trace(d[0], d[1], d[2]), zb[r][x] = last_t;
    }
  }
}

/* A 4 x 4 cell whose corners and middle all met faces of the same plane, of different blocks:
 * each pixel the face there if its block shows it (face_pixel's checks, made once a block,
 * kept while the next pixels are on the same one). A pixel it could not draw is traced. */
static __attribute__((unused)) void plane_fill(uint16_t (*cb)[RW], float (*zb)[RW], float (*rb)[3], int x0, const Sample *s) {
  static const uint8_t KA[3] = {1, 2, 0}, KU[3] = {0, 0, 2}, KV[3] = {2, 1, 1}, side[6] = {3, 0, 1, 1, 2, 2};
  static const int8_t nx[6] = {0, 0, 0, 0, -1, 1}, ny[6] = {-1, 1, 0, 0, 0, 0}, nz[6] = {0, 0, -1, 1, 0, 0};
  int f = s->f, a = f >> 1, ka = KA[a], ku = KU[a], kv = KV[a];
  int fu = f == 2 || f == 5 ? 15 : 0, fv = a ? 15 : 0;
  int plane = s->plane, cp = (f & 1) ? (plane - 1) >> 4 : plane >> 4, edge = plane - cp * 16;
  bool wet = is_water(cam_b);
  const float o[3] = {ox, oy, oz};
  float K = plane * (1 / 16.0f) - o[ka];
  /* the block last met: whether it shows the face, and how */
  int last = -1, ok = 0, tex = 0, fl = 0, shd = 0;
  bool black = false, crack = false;
  uint16_t tint = 0;
  const int8_t *q = NULL;
  for (int r = 0; r < SR; r++) {
    const float *b = rb[r];
    float tr = a == 0 ? K / b[1] : 0;   /* (a top or bottom face: as far all along the row) */
    for (int x = x0; x < x0 + 4; x++) {
      float su = col_su[x], d[3] = {b[0] + rgx * su, b[1], b[2] + rgz * su};
      float t = tr;
      if (a) t = K / d[ka];
      if (!(t > 0)) goto miss;
      float hu = o[ku] + d[ku] * t, hv = o[kv] + d[kv] * t;
      if (hu < 0 || hv < 0) goto miss;
      int c[3];
      c[ka] = cp, c[ku] = (int)hu, c[kv] = (int)hv;
      if ((unsigned)c[0] >= VCX || (unsigned)c[1] >= VCY || (unsigned)c[2] >= VCZ) goto miss;
      int i = VC_I(c[0], c[1], c[2]);
      if (i != last) {
        last = i;
        int bk = vc[i], m = blk_model[bk];
        bool cube = m == M_CUBE || m == M_LEAVES || m == M_GLASS;
        q = NULL;
        if (cube) ok = !(edge & 15);
        else ok = one_box(bk) && blk_box[bk][0][ka + ((f & 1) ? 3 : 0)] == edge, q = blk_box[bk][0];
        if (!ok) goto miss;
        int light = vl[i];
        if (!(edge & 15)) {
          /* on the cell's side: lit by the cell in front, which must be clear */
          int px = c[0] + nx[f], py = c[1] + ny[f], pz = c[2] + nz[f];
          light = 15;
          if ((unsigned)px < VCX && (unsigned)py < VCY && (unsigned)pz < VCZ) {
            int fi = VC_I(px, py, pz);
            if (vc[fi] != B_AIR && !(wet && is_water(vc[fi]))) {
              ok = 0;
              goto miss;
            }
            light = vl[fi];
          }
        }
        tex = blk_front[bk] ? face_tex(bk, f, c[0], c[1], c[2]) : blk_tex[bk][f];
        fl = tex_flags[tex];
        if ((fl & 0x1F) < 16) tint = tint_of(bk, c[2] * VCX + c[0]);
        shd = light_shade(side[f], light);
        black = m == M_LEAVES && !opt.fancy;
        crack = cube && i == brk_i;
      }
      if (!ok) goto miss;
      float lu = hu - c[ku], lv = hv - c[kv];
      if (q && (lu * 16 < q[ku] || lu * 16 > q[ku + 3] || lv * 16 < q[kv] || lv * 16 > q[kv + 3])) goto miss;
      int iu = (int)(lu * 16), iv = (int)(lv * 16);
      if ((unsigned)iu > 15 || (unsigned)iv > 15) goto miss;
      int u = iu ^ fu, v = iv ^ fv, tx = texel(tex, u, v);
      uint16_t col;
      if (!tx && (fl & 0x20)) {
        if (!black) goto miss;
        col = 0;
      } else {
        col = tex_pal[tex][tx];
        if (tx >= (fl & 0x1F)) col = pack(r5(col) * r5(tint) >> 8, g6(col) * g6(tint) >> 8, b5(col) * b5(tint) >> 8);
      }
      if (crack) col = (uint16_t)cracked(col, i, u, v);
      col = shade565(col, shd);
      float d2 = t * t * (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (d2 > fog0 * fog0) {
        float dist = sqrtf(d2);
        col = mix565(col, fog565, dist >= fog1 ? 32 : (int)((dist - fog0) * fog_k * 32));
      }
      cb[r][x] = col, zb[r][x] = t;
      continue;
    miss:
      cb[r][x] = trace(d[0], d[1], d[2]), zb[r][x] = last_t;
    }
  }
}

/* a pixel between samples that all saw faces of the same plane through the
 * same plane of water's top: the water there, over the face beneath it */
static int water_pixel(float dx, float dy, float dz, int wplane, int plane, int f) {
  if (!(dy < 0)) return -1;
  float t = (wplane / 16.0f - oy) / dy;
  if (!(t > 0)) return -1;
  float hx = ox + dx * t, hz = oz + dz * t;
  int x = (int)hx, y = (wplane - 1) >> 4, z = (int)hz;
  if (hx < 0 || hz < 0 || (unsigned)x >= VCX || (unsigned)y >= VCY - 1 || (unsigned)z >= VCZ) return -1;
  int i = VC_I(x, y, z), b = vc[i], above = vc[i + VCX * VCZ];
  if (!is_water(b) || above != B_AIR) return -1;
  static const int8_t LIQH[9] = {14, 12, 11, 9, 7, 5, 4, 2, 16};
  if (y * 16 + LIQH[blk_meta[b] > 8 ? 8 : blk_meta[b]] != wplane) return -1;   /* (not this height here) */
  int u = (int)((hx - x) * 16), v = (int)((hz - z) * 16), r, g, bb;
  u = u < 0 ? 0 : u > 15 ? 15 : u, v = v < 0 ? 0 : v > 15 ? 15 : v;
  if (texel_rgb(b, face_tex(b, 1, x, y, z), u, v, z * VCX + x, &r, &g, &bb) < 0) return -1;
  uint16_t w = lit(r, g, bb, 1, vl[i], t * sqrtf(dx * dx + dy * dy + dz * dz));
  int c = face_pixel(dx, dy, dz, plane, f, true);
  if (c < 0) return -1;
  return pack((r5((uint16_t)c) * 106 + r5(w) * 150) >> 8, (g6((uint16_t)c) * 106 + g6(w) * 150) >> 8,
              (b5((uint16_t)c) * 106 + b5(w) * 150) >> 8);
}


/* trace picture pixel (px, py) as a sample */
static void sample_at(Sample *s, int px, int py) {
  float sv = (1 - 2 * (py + 0.5f) / RH) * TAN_V;
  float bx = fwx + upx * sv, by = fwy + upy * sv, bz = fwz + upz * sv, su = col_su[px];
  s->c = trace(bx + rgx * su, by, bz + rgz * su);
  s->kind = (uint8_t)hit_kind;
  s->f = (uint8_t)hit_f;
  s->plane = (int16_t)hit_p;
  s->cell = (uint16_t)hit_i;
  if (hit_kind == 1) {
    int x = hit_i % VCX, z = hit_i / VCX % VCZ, y = hit_i / (VCX * VCZ), b = vc[hit_i], m = blk_model[b], f = hit_f;
    s->x = (uint8_t)x, s->z = (uint8_t)z, s->y = (uint8_t)y;
    s->light = (uint8_t)hit_l;
    s->tex = (uint16_t)(blk_front[b] ? face_tex(b, f, x, y, z) : blk_tex[b][f]);
    s->tint = tint_of(b, z * VCX + x);
    static const uint8_t side[6] = {3, 0, 1, 1, 2, 2};
    s->shade = (uint8_t)light_shade(side[f], hit_l);
    int flags = 0;
    if (hit_p & 15) flags |= 1;   /* (inside its cell: nothing else there) */
    else {
      int fb = vc[came_from(hit_i, f, x, y, z)];
      if (fb == B_AIR || (is_water(fb) && is_water(cam_b))) flags |= 1;
    }
    if (m == M_CUBE || m == M_LEAVES || m == M_GLASS) flags |= 2;
    if (m == M_LEAVES && !opt.fancy) flags |= 4;
    s->flags = (uint8_t)flags;
  }
  s->w = (int16_t)hit_w;
  s->t = last_t;
}

static inline bool same(const Sample *a, const Sample *b) {
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case 1: return a->f == b->f && a->plane == b->plane;
    case 2: return true;
    case 3: return a->cell == b->cell;
    case 4: return a->f == b->f && a->plane == b->plane && a->w == b->w;
  }
  return false;
}

/* the pixel (px, py) knowing the samples around it agree (a, b) or not */
/* the pixel of ray (dx, dy, dz) between samples that agree (one: they met the same cell) */
static uint16_t agreed(float dx, float dy, float dz, const Sample *a, bool one) {
  if (a->kind == 1) {
    int r = one ? face_cell(dx, dy, dz, a) : face_pixel(dx, dy, dz, a->plane, a->f, is_water(cam_b));
    if (r >= 0) return ST(st_fpok++), (uint16_t)r;
    ST(st_fpfail++);
  }
  if (a->kind == 4) {
    int r = water_pixel(dx, dy, dz, a->w, a->plane, a->f);
    if (r >= 0) return ST(st_wok++), (uint16_t)r;
  }
  if (a->kind == 3) {
    /* the same plant: its two planes, from the camera */
    int i = a->cell, x = i % VCX, z = i / VCX % VCZ, y = i / (VCX * VCZ), b = vc[i];
    Hit h;
    if (hit_cross(blk_tex[b][3], ox - x, oy - y, oz - z, dx, dy, dz, 0, 1e9f, &h)) {
      last_t = h.t;
      int tc = texel565(b, blk_tex[b][3], h.u, h.v, z * VCX + x);
      float d2 = h.t * h.t * (dx * dx + dy * dy + dz * dz);
      return lit565((uint16_t)tc, 1, vl[i], d2 > fog0 * fog0 ? sqrtf(d2) : 0);
    }
  }
  last_t = 1e9f;
  if (a->kind == 2) return ST(st_sky++), sky(dx, dy, dz, sqrtf(dx * dx + dy * dy + dz * dz));
  return trace(dx, dy, dz);
}

static uint16_t between(const float *row, int px, const Sample *a, const Sample *b, const Sample *c, const Sample *d) {
  float su = col_su[px], dx = row[0] + rgx * su, dy = row[1], dz = row[2] + rgz * su;
#ifdef FULL_TRACE
  return trace(dx, dy, dz);   /* the reference picture, for the tests */
#endif
  bool agree = same(a, b) && (!c || (same(a, c) && same(a, d)));
  ST(agree ? 0 : st_dis++);
  ST(agree && a->kind == 0 ? st_k0++ : 0);
  if (!agree) return last_t = 1e9f, trace(dx, dy, dz);
  return agreed(dx, dy, dz, a, a->cell == b->cell && (!c || (a->cell == c->cell && a->cell == d->cell)));
}

/* the 2 x 2 pixels from (x, r) of the strip, corners tl tr bl br traced (tl is the first) */
static void quad(uint16_t (*cb)[RW], float (*zb)[RW], float (*rb)[3], int r, int x, const Sample *tl, const Sample *tr,
                 const Sample *bl, const Sample *br) {
  cb[r][x] = tl->c, zb[r][x] = tl->t;
  cb[r][x + 1] = between(rb[r], x + 1, tl, tr, NULL, NULL), zb[r][x + 1] = last_t;
  cb[r + 1][x] = between(rb[r + 1], x, tl, bl, NULL, NULL), zb[r + 1][x] = last_t;
  cb[r + 1][x + 1] = between(rb[r + 1], x + 1, tl, tr, bl, br), zb[r + 1][x + 1] = last_t;
}

/* ---------------------------------------------------------------- entities
 * Mobs are their models' boxes, each ray tested in the box's own frame and
 * textured from the skin as Minecraft maps a box (ModelBox, TexturedQuad);
 * dropped blocks are small spinning cubes, dropped items flat pictures facing
 * the camera, as Fast graphics draws them. A pixel shows the nearest hit that
 * is closer than the world behind it. */
#include "mob.h"

float tick_frac;   /* how far between the last two ticks this frame is (main.c) */
#define N_DRAW 12
typedef struct {
  const Entity *e;
  int x0, y0, x1, y1;          /* its rectangle on the picture (internal pixels) */
  float ex, ey, ez;            /* where it is (cache coordinates) */
  int light;
  float dist;
} Draw;
static Draw draws[N_DRAW];
static int ndraws;

static inline void ray_dir(int px, int py, float *d) {
  float sv = (1 - 2 * (py + 0.5f) / RH) * TAN_V, su = (2 * (px + 0.5f) / RW - 1) * TAN_H;
  d[0] = fwx + upx * sv + rgx * su;
  d[1] = fwy + upy * sv;
  d[2] = fwz + upz * sv + rgz * su;
}

/* a point (cache coordinates) on the picture; false if behind the camera */
static bool project(float x, float y, float z, float *sx, float *sy) {
  float rx = x - ox, ry = y - oy, rz = z - oz;
  float cz = rx * fwx + ry * fwy + rz * fwz;
  if (cz < 0.05f) return false;
  float cx = rx * rgx + rz * rgz, cy = rx * upx + ry * upy + rz * upz;
  *sx = (cx / cz / TAN_H + 1) * RW / 2;
  *sy = (1 - cy / cz / TAN_V) * RH / 2;
  return true;
}

/* the rectangle a box (its 8 corners, cache coordinates) covers on the picture: what of it is in front of
 * the camera, its edges cut where they pass behind (false: none of it is) */
static bool rect_of(const float (*p)[3], int *r) {
  const float NEAR = 0.05f;
  float cx[8], cy[8], cz[8], u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
  bool any = false;
  for (int k = 0; k < 8; k++) {
    float rx = p[k][0] - ox, ry = p[k][1] - oy, rz = p[k][2] - oz;
    cz[k] = rx * fwx + ry * fwy + rz * fwz;
    cx[k] = rx * rgx + rz * rgz, cy[k] = rx * upx + ry * upy + rz * upz;
  }
  for (int k = 0; k < 8; k++)
    for (int bit = 0; bit <= 4; bit = bit ? bit * 2 : 1) {
      int j = k ^ bit;
      float x, y, z;
      if (!bit) {
        if (cz[k] < NEAR) continue;
        x = cx[k], y = cy[k], z = cz[k];
      } else {
        if (j < k || (cz[k] >= NEAR) == (cz[j] >= NEAR)) continue;
        float t = (NEAR - cz[k]) / (cz[j] - cz[k]);
        x = cx[k] + (cx[j] - cx[k]) * t, y = cy[k] + (cy[j] - cy[k]) * t, z = NEAR;
      }
      float u = x / z, v = y / z;
      if (u < u0) u0 = u;
      if (u > u1) u1 = u;
      if (v < v0) v0 = v;
      if (v > v1) v1 = v;
      any = true;
    }
  if (!any) return false;
  float sx0 = (u0 / TAN_H + 1) * RW / 2, sx1 = (u1 / TAN_H + 1) * RW / 2;
  float sy0 = (1 - v1 / TAN_V) * RH / 2, sy1 = (1 - v0 / TAN_V) * RH / 2;
  if (sx1 < 0 || sx0 >= RW || sy1 < 0 || sy0 >= RH) return false;
  r[0] = sx0 < 0 ? 0 : (int)sx0, r[2] = sx1 >= RW ? RW : (int)sx1 + 1;
  r[1] = sy0 < 0 ? 0 : (int)sy0, r[3] = sy1 >= RH ? RH : (int)sy1 + 1;
  return true;
}

/* a block's texel as the hand holds it (tinted as where the player stands); -1: see-through */
int held_texel(int b, int face, int u, int v) {
  int col = cam_i >= 0 ? cam_i % (VCX * VCZ) : 0;
  return texel565(b, blk_tex[b][face], u, v, col);
}

/* how lit the player's hand is (0..32): the light where the eyes are */
int view_light(void) { return light_shade(0, cam_i >= 0 ? vl[cam_i] : 15); }

static void ents_frame(void) {
  ndraws = 0;
  for (int i = 0; i < N_ENT && ndraws < N_DRAW; i++) {
    const Entity *e = &ents[i];
    if (e->type == E_NONE) continue;
    float t = tick_frac;
    float ex = e->px + (e->x - e->px) * t - vc_x0, ey = e->py + (e->y - e->py) * t - vc_y0,
          ez = e->pz + (e->z - e->pz) * t - vc_z0;
    float dx = ex - ox, dy = ey - oy, dz = ez - oz;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist > MAX_T + 1) continue;
    float w, h;
    if (e->type == E_ITEM) w = 0.5f, h = 0.6f;
    else if (e->type == E_TNT) w = 1.2f, h = 1.1f;
    else if (e->type == E_ARROW || e->type == E_BOBBER) w = 0.6f, h = 0.6f, ey -= 0.3f;
    else w = mob_width(e->type) * 2.2f, h = mob_height(e->type) * 1.2f;
    if (e->type >= E_ZOMBIE && e->type <= E_CHICKEN && e->state == 255) w = h = 2.4f;   /* dying: lying down */
    float c[8][3];
    for (int k = 0; k < 8; k++)
      c[k][0] = ex + ((k & 1) ? w : -w) / 2, c[k][1] = ey + ((k & 2) ? h : 0), c[k][2] = ez + ((k & 4) ? w : -w) / 2;
    Draw *d = &draws[ndraws];
    int r[4];
    if (!rect_of(c, r)) continue;
    d->x0 = r[0], d->y0 = r[1], d->x1 = r[2], d->y1 = r[3];
    d->e = e, d->ex = ex, d->ey = ey, d->ez = ez, d->dist = dist;
    int cx = (int)floorf(ex), cy = (int)floorf(ey + 0.2f), cz = (int)floorf(ez);
    d->light = (unsigned)cx < VCX && (unsigned)cy < VCY && (unsigned)cz < VCZ ? vl[VC_I(cx, cy, cz)] : 15;
    ndraws++;
  }
}

/* a part, ready to test: world (cache) to box frame */
typedef struct {
  float A[9], o[3];      /* box-frame ray: origin o, direction A d */
  float x0, y0, z0, x1, y1, z1;
  uint8_t u, v, w, h, d, mirror, skin;
  uint8_t shade[6];      /* each face's shade (0..32) */
  int16_t r[4];          /* its rectangle on the picture (x0 y0 x1 y1; empty: not seen, or the camera in it) */
} Box;

static inline void mul3(const float *a, const float *b, float *o) {   /* o = a b */
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) o[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}
static void rot(float *m, int axis, float a) {   /* m = rotation about the axis */
  float c = cosf(a), s = sinf(a);
  for (int i = 0; i < 9; i++) m[i] = (i % 4 == 0);
  int i = (axis + 1) % 3, j = (axis + 2) % 3;
  m[i * 3 + i] = c, m[i * 3 + j] = -s, m[j * 3 + i] = s, m[j * 3 + j] = c;
}

/* M: model frame to world (Ry (180 - yaw) . death turn . S(-1,-1,1) . scale) */
static int make_boxes(const Draw *dr, Box *bx) {
  const Entity *e = dr->e;
  Part parts[MAX_PARTS];
  int n = mob_parts(e, tick_frac, parts);
  float yaw = e->yaw * 0.017453292f;
  float M[9], R[9], Z[9];
  rot(R, 1, 3.14159265f - yaw);
  /* dying: falls on its side over a second (RendererLivingEntity.rotateCorpse) */
  float death = e->state == 255 ? (e->timer + tick_frac) / 20.0f * 1.6f : 0;
  if (death > 0) {
    death = sqrtf(death);
    if (death > 1) death = 1;
  }
  rot(Z, 2, death * 1.5707963f);
  mul3(R, Z, M);
  float sc = mob_scale(e, tick_frac) / 16;
  for (int i = 0; i < 9; i++) M[i] *= sc;
  for (int i = 0; i < 9; i += 3) M[i + 1] = -M[i + 1], M[i] = -M[i];   /* S: x and y flipped */
  float light_sh[6];
  static const float face_shade[6] = {0.6f, 0.6f, 1.0f, 0.5f, 0.8f, 0.8f};   /* +x -x top bottom front back */
  for (int p = 0; p < n; p++) {
    const Part *pt = &parts[p];
    float P[9], Ry[9], Rx[9], Rz[9], T[9];
    rot(Rz, 2, pt->rz);
    rot(Ry, 1, pt->ry);
    rot(Rx, 0, pt->rx);
    mul3(Rz, Ry, T);
    mul3(T, Rx, P);
    float W[9];   /* part frame to world */
    mul3(M, P, W);
    /* world = E + M (T0 + rp) + W v, T0 = (0, -24.125) model pixels: the feet on the ground */
    float c[3] = {pt->px, pt->py - 24.125f, pt->pz};
    float base[3];
    for (int r = 0; r < 3; r++) base[r] = M[r * 3] * c[0] + M[r * 3 + 1] * c[1] + M[r * 3 + 2] * c[2];
    base[0] += dr->ex, base[1] += dr->ey, base[2] += dr->ez;
    /* the inverse of W: its transpose over its scale squared */
    float k = 1 / (sc * sc);
    Box *b = &bx[p];
    for (int r = 0; r < 3; r++)
      for (int q = 0; q < 3; q++) b->A[r * 3 + q] = W[q * 3 + r] * k;
    float rel[3] = {ox - base[0], oy - base[1], oz - base[2]};
    for (int r = 0; r < 3; r++) b->o[r] = b->A[r * 3] * rel[0] + b->A[r * 3 + 1] * rel[1] + b->A[r * 3 + 2] * rel[2];
    b->x0 = pt->x - pt->grow, b->y0 = pt->y - pt->grow, b->z0 = pt->z - pt->grow;
    b->x1 = pt->x + pt->w + pt->grow, b->y1 = pt->y + pt->h + pt->grow, b->z1 = pt->z + pt->d + pt->grow;
    b->u = pt->u, b->v = pt->v, b->w = pt->w, b->h = pt->h, b->d = pt->d, b->mirror = pt->mirror, b->skin = pt->skin;
    /* where it is on the picture: none of it with the camera inside (its faces turn away: culled) */
    b->r[0] = b->r[2] = 0;
    if (!(b->o[0] > b->x0 && b->o[0] < b->x1 && b->o[1] > b->y0 && b->o[1] < b->y1 && b->o[2] > b->z0 && b->o[2] < b->z1)) {
      float cn[8][3];
      int r[4];
      for (int k = 0; k < 8; k++) {
        float v[3] = {(k & 1) ? b->x1 : b->x0, (k & 2) ? b->y1 : b->y0, (k & 4) ? b->z1 : b->z0};
        for (int q = 0; q < 3; q++) cn[k][q] = base[q] + W[q * 3] * v[0] + W[q * 3 + 1] * v[1] + W[q * 3 + 2] * v[2];
      }
      if (rect_of(cn, r))
        for (int q = 0; q < 4; q++) b->r[q] = (int16_t)r[q];
    }
    /* each face lit by how it faces in the world (like blocks: up 1.0, down 0.5, sides 0.8 and 0.6) */
    for (int f = 0; f < 6; f++) {
      int ax = f < 2 ? 0 : f < 4 ? 1 : 2;
      float sgn = (f & 1) ? -1.0f : 1.0f;
      if (ax == 1) sgn = -sgn;   /* model y is down: face 2 (y0) points up */
      float nx = W[ax] * sgn / sc, ny = W[3 + ax] * sgn / sc, nz = W[6 + ax] * sgn / sc;
      float sh = ny > 0.7f ? 1.0f : ny < -0.7f ? 0.5f : fabsf(nz) > fabsf(nx) ? 0.8f : 0.6f;
      (void)face_shade;
      light_sh[f] = sh;
      b->shade[f] = (uint8_t)(light_shade(0, dr->light) * sh + 0.5f);
    }
  }
  (void)light_sh;
  return n;
}

/* the skin texel the ray meets on a box face (ModelBox's layout), -1 if see-through */
static int box_texel(const Box *b, int face, float x, float y, float z) {
  if (b->mirror) {
    x = b->x0 + b->x1 - x;
    if (face < 2) face ^= 1;
  }
  /* pixel coordinates inside the box (the inflation of wool keeps the layout) */
  float sx = (b->x1 - b->x0) / b->w, sy = (b->y1 - b->y0) / b->h, sz = (b->z1 - b->z0) / b->d;
  int lx = (int)((x - b->x0) / sx), ly = (int)((y - b->y0) / sy), lz = (int)((z - b->z0) / sz);
  if (lx >= b->w) lx = b->w - 1;
  if (ly >= b->h) ly = b->h - 1;
  if (lz >= b->d) lz = b->d - 1;
  if (lx < 0) lx = 0;
  if (ly < 0) ly = 0;
  if (lz < 0) lz = 0;
  int U = b->u, V = b->v, w = b->w, d = b->d, u, v;
  switch (face) {
    case 0: u = U + d + w + lz, v = V + d + ly; break;              /* +x */
    case 1: u = U + (d - 1 - lz), v = V + d + ly; break;            /* -x */
    case 2: u = U + d + lx, v = V + (d - 1 - lz); break;            /* y0 (the top) */
    case 3: u = U + d + w + lx, v = V + d - 1 - lz; break;          /* y1 (the bottom) */
    case 4: u = U + d + lx, v = V + d + ly; break;                  /* z0 (the front) */
    default: u = U + d + w + d + (w - 1 - lx), v = V + d + ly; break;   /* z1 (the back) */
  }
  if ((unsigned)u >= 64 || (unsigned)v >= 32) return -1;
  uint8_t p = skin_px[b->skin][(v * 64 + u) >> 1];
  int i = (u & 1) ? p >> 4 : p & 15;
  return i ? skin_pal[b->skin][i] : -1;
}

/* the nearest box the ray meets (t > 0), its colour; false if none */
static bool boxes_hit(const Box *bx, int n, const float *d, float *best_t, uint16_t *col) {
  bool any = false;
  for (int p = 0; p < n; p++) {
    const Box *b = &bx[p];
    float dl[3];
    for (int r = 0; r < 3; r++) dl[r] = b->A[r * 3] * d[0] + b->A[r * 3 + 1] * d[1] + b->A[r * 3 + 2] * d[2];
    float t0 = 0, t1 = *best_t;
    int f0 = -1;
    const float lo[3] = {b->x0, b->y0, b->z0}, hi[3] = {b->x1, b->y1, b->z1};
    bool miss = false;
    for (int a = 0; a < 3 && !miss; a++) {
      if (fabsf(dl[a]) < 1e-9f) {
        if (b->o[a] < lo[a] || b->o[a] > hi[a]) miss = true;
        continue;
      }
      float inv = 1 / dl[a];
      float ta = (lo[a] - b->o[a]) * inv, tb = (hi[a] - b->o[a]) * inv;
      int fa = a * 2 + 1, fb = a * 2;   /* entering through the low side: -x (1), y0 (2)... */
      if (a == 1) fa = 2, fb = 3;
      if (a == 2) fa = 4, fb = 5;
      if (ta > tb) {
        float t = ta;
        ta = tb, tb = t;
        int f = fa;
        fa = fb, fb = f;
      }
      if (ta > t0) t0 = ta, f0 = fa;
      if (tb < t1) t1 = tb;
      if (t0 > t1) miss = true;
    }
    if (miss || f0 < 0) continue;
    float hx = b->o[0] + dl[0] * t0, hy = b->o[1] + dl[1] * t0, hz = b->o[2] + dl[2] * t0;
    int c = box_texel(b, f0, hx, hy, hz);
    if (c < 0) continue;   /* (see-through: the ray is not followed further in this box) */
    *best_t = t0;
    *col = shade565((uint16_t)c, b->shade[f0]);
    any = true;
  }
  return any;
}

/* a dropped item: a small cube of its block, or its picture */
static bool item_hit(const Draw *dr, const float *d, float *best_t, uint16_t *col) {
  const Entity *e = dr->e;
  int id = e->item.id;
  float age = e->age + tick_frac;
  bool tnt = e->type == E_TNT;
  float bob = tnt ? 0 : sinf(age / 10.0f + e->yaw) * 0.1f + 0.1f;
  bool cube = id < 256 && (blk_model[id] == M_CUBE || blk_model[id] == M_LEAVES || blk_model[id] == M_GLASS ||
                           blk_model[id] == M_SLAB);
  float hs = tnt ? 0.49f : 0.125f;
  float cx = dr->ex, cy = dr->ey + bob + hs, cz = dr->ez;
  if (e->type == E_BOBBER) cy = dr->ey + 0.3f - 0.125f, cube = false;   /* RenderFish: centred where it is */
  if (cube) {
    /* EntityItem render: a quarter block, turning (lit TNT: a whole block, still) */
    float a = tnt ? 0 : age / 20.0f + e->yaw, ca = cosf(a), sa = sinf(a);
    float rel[3] = {ox - cx, oy - cy, oz - cz};
    float o[3] = {ca * rel[0] + sa * rel[2], rel[1], -sa * rel[0] + ca * rel[2]};
    float dl[3] = {ca * d[0] + sa * d[2], d[1], -sa * d[0] + ca * d[2]};
    float t0 = 0, t1 = *best_t;
    int f0 = -1;
    for (int k = 0; k < 3; k++) {
      if (fabsf(dl[k]) < 1e-9f) {
        if (fabsf(o[k]) > hs) return false;
        continue;
      }
      float ta = (-hs - o[k]) / dl[k], tb = (hs - o[k]) / dl[k];
      static const int8_t lowf[3] = {4, 0, 2};   /* block faces: -x 4, -y 0, -z 2 */
      int fa = lowf[k], fb = lowf[k] + 1;
      if (ta > tb) {
        float t = ta;
        ta = tb, tb = t;
        int f = fa;
        fa = fb, fb = f;
      }
      if (ta > t0) t0 = ta, f0 = fa;
      if (tb < t1) t1 = tb;
      if (t0 > t1) return false;
    }
    if (f0 < 0) return false;
    float hx = o[0] + dl[0] * t0 + hs, hy = o[1] + dl[1] * t0 + hs, hz = o[2] + dl[2] * t0 + hs;
    int u, v;
    float k4 = 16 / (2 * hs);
    switch (f0) {
      case 0: case 1: u = (int)(hx * k4); v = (int)(hz * k4); break;
      case 2: case 3: u = (int)(hx * k4); v = 15 - (int)(hy * k4); break;
      default: u = (int)(hz * k4); v = 15 - (int)(hy * k4); break;
    }
    u &= 15, v &= 15;
    int tc = texel565(id, blk_tex[id][f0], u, v, 0);
    if (tc < 0) tc = 0;
    *best_t = t0;
    *col = lit565((uint16_t)tc, f0, dr->light, t0 * sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    if (tnt && (e->delay / 5) % 2 == 0) *col = mix565(*col, 0xFFFF, 16);   /* RenderTNTPrimed: flashing */
    return true;
  }
  /* a flat picture half a block wide, facing the camera */
  int ic = e->type == E_BOBBER ? SP_BOBBER : item_icon(id);
  if (ic < 0) return false;
  float rel[3] = {cx - ox, cy + 0.125f - oy, cz - oz};
  float den = d[0] * fwx + d[1] * fwy + d[2] * fwz;
  float t = (rel[0] * fwx + rel[1] * fwy + rel[2] * fwz) / den;
  if (!(t > 0) || t >= *best_t) return false;
  float hx = d[0] * t - rel[0], hy = d[1] * t - rel[1], hz = d[2] * t - rel[2];
  float u = (hx * rgx + hz * rgz) / 0.5f + 0.5f, v = 0.5f - (hx * upx + hy * upy + hz * upz) / 0.5f;
  if (u < 0 || u >= 1 || v < 0 || v >= 1) return false;
  int w = spr_w[ic], hh = spr_h[ic], pxu = (int)(u * w), pyv = (int)(v * hh);
  const uint8_t *px = spr_px + spr_off[ic] + pyv * ((w + 1) / 2);
  int i = (pxu & 1) ? px[pxu >> 1] >> 4 : px[pxu >> 1] & 15;
  if (!i) return false;
  *best_t = t;
  *col = lit565(spr_pal[ic][i], 1, dr->light, t * sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
  return true;
}

/* the particles: little squares, a tenth of a block, lit where they are */
static void particles_strip(int py0, int rows, uint16_t (*cb)[RW], float (*zb)[RW]) {
  for (int i = 0; i < N_PART; i++) {
    const Particle *p = &parts[i];
    if (p->age >= (p->life & P_LIFE)) continue;
    float x = p->x - vc_x0, y = p->y - vc_y0, z = p->z - vc_z0, sx, sy;
    if (!project(x, y, z, &sx, &sy)) continue;
    float rx = x - ox, ry = y - oy, rz = z - oz;
    float t = rx * fwx + ry * fwy + rz * fwz;   /* (the ray's length along the view, as zbuf keeps it) */
    int half = p->life & (RAIN_DROP | P_FLOAT) ? 1 : (int)(0.05f / t / TAN_V * RH / 2 + 0.5f);   /* (drops: a texel or two) */
    if (half < 1) half = 1;
    int cx = (int)floorf(x), cy = (int)floorf(y), cz = (int)floorf(z);
    int lb = (unsigned)cx < VCX && (unsigned)cy < VCY && (unsigned)cz < VCZ ? vl[VC_I(cx, cy, cz)] : 15;
    uint16_t c = shade565(p->c, light_shade(0, lb));
    for (int yy = (int)sy - half + 1; yy <= (int)sy + half - 1 + (half == 1); yy++) {
      if (yy < py0 || yy >= py0 + rows || yy < 0) continue;
      for (int xx = (int)sx - half + 1; xx <= (int)sx + half - 1 + (half == 1); xx++) {
        if ((unsigned)xx >= RW || zb[yy - py0][xx] <= t) continue;
        cb[yy - py0][xx] = c;
      }
    }
  }
}

/* ---------------------------------------------------------------- rain and snow */
/* EntityRenderer.renderRainSnow: around the player (10 blocks with Fancy
 * graphics, 5 with Fast), every column where it rains or snows has a sheet one
 * block wide turned towards the player, from the ground (or 10 blocks down)
 * to 10 blocks up, with rain.png scrolling down it, or snow.png drifting.
 * The textures' streaks and flakes are drawn as thin lines, fainter where
 * they are thinner than a pixel. The sheets live in the chunk buffer, which
 * nothing else uses while a frame is drawn. */
typedef struct {
  float cx, cz, hx, hz;   /* centre (cache coordinates), half the width across */
  float v, u;             /* texture row at y = 0 (scrolling), snow's drift across */
  int16_t y0, y1;         /* bottom, top (cache y) */
  uint16_t col;           /* lit colour */
  uint8_t alpha, snow;
  uint8_t sx0, sx1, sy0, sy1;   /* where it is on the picture */
} Sheet;
extern uint32_t game_time;
static Sheet *sheets;
static int nsheets;

static uint32_t col_hash(int x, int z) {
  uint32_t h = (uint32_t)x * 3121u * (uint32_t)x + (uint32_t)x * 45238971u;
  h ^= (uint32_t)z * (uint32_t)z * 418711u + (uint32_t)z * 13761u;
  h = (h ^ (h >> 15)) * 2246822519u;
  return h ^ (h >> 13);
}

static void rain_frame(void) {
  nsheets = 0;
  if (rain_str <= 0 || under_water) return;
  int nmax = 6144 / (int)sizeof(Sheet);
  sheets = (Sheet *)world_scratch(nmax * sizeof(Sheet));
  if (!sheets) return;
  int r = opt.fancy ? 10 : 5;
  int px = (int)floorf(pl.x), pz = (int)floorf(pl.z), j = (int)floorf(pl.y), l = (int)floorf(oy + vc_y0 - 1.62f);
  float cnt = (float)(game_time % 100000) + tick_frac;
  /* nearest first, ring by ring, in case they do not all fit */
  for (int d = 1; d <= r; d++)
    for (int dz = -d; dz <= d; dz++)
      for (int dx = -d; dx <= d; dx += (dz == -d || dz == d) ? 1 : 2 * d) {
        if (nsheets >= nmax) return;
        int wx = px + dx, wz = pz + dz, lx = wx - vc_x0, lz = wz - vc_z0;
        if ((unsigned)lx >= VCX || (unsigned)lz >= VCZ || !(biome_rain[vbiome[lz * VCX + lx]] & 1)) continue;
        int top = world_rain_top(wx, wz);
        if (top > WORLD_H) continue;
        int y0 = j - r > top ? j - r : top, y1 = j + r > top ? j + r : top;
        if (y0 == y1) continue;
        Sheet *s = &sheets[nsheets];
        float n = 0.5f / sqrtf((float)(dx * dx + dz * dz));
        s->cx = lx + 0.5f, s->cz = lz + 0.5f, s->hx = -dz * n, s->hz = dx * n;
        /* in front of the camera at all? */
        float sx[4], sy[4];
        int in = 0;
        for (int k = 0; k < 4; k++) {
          float x = s->cx + (k & 1 ? s->hx : -s->hx), z = s->cz + (k & 1 ? s->hz : -s->hz);
          float y = (float)((k & 2 ? y1 : y0) - vc_y0);
          in += project(x, y, z, &sx[k], &sy[k]);
        }
        if (!in) continue;
        float ax = RW, bx = -1, ay = RH, by = -1;
        if (in < 4) ax = 0, bx = RW - 1, ay = 0, by = RH - 1;
        else
          for (int k = 0; k < 4; k++) {
            if (sx[k] < ax) ax = sx[k];
            if (sx[k] > bx) bx = sx[k];
            if (sy[k] < ay) ay = sy[k];
            if (sy[k] > by) by = sy[k];
          }
        if (bx < 0 || ax >= RW || by < 0 || ay >= RH) continue;
        s->sx0 = (uint8_t)(ax < 0 ? 0 : ax), s->sx1 = (uint8_t)(bx >= RW - 1 ? RW - 1 : bx);
        s->sy0 = (uint8_t)(ay < 0 ? 0 : ay), s->sy1 = (uint8_t)(by >= RH - 1 ? RH - 1 : by);
        s->y0 = (int16_t)(y0 - vc_y0), s->y1 = (int16_t)(y1 - vc_y0);
        float fx = wx + 0.5f - pl.x, fz = wz + 0.5f - pl.z, f = (fx * fx + fz * fz) / (float)(r * r);
        uint32_t h = col_hash(wx, wz);
        float rnd1 = (h & 0xFFFF) / 65536.0f, rnd2 = (h >> 16) / 65536.0f;
        /* the light where the sheet meets the player's height, or the ground */
        int ly = (top > l ? top : l) - vc_y0, lb = 15;
        if ((unsigned)ly < VCY) lb = vl[VC_I(lx, ly, lz)];
        s->snow = temp_at(wx, y0, wz) < 0.15f;
        if (!s->snow) {
          /* rows scroll down 3 to 4 blocks a second... (d5 in renderRainSnow) */
          float d5 = ((float)(((uint32_t)cnt + h) & 31) + (cnt - floorf(cnt))) / 32 * (3 + rnd1);
          s->v = d5 * 256, s->u = 0;
          s->alpha = (uint8_t)(((1 - f) * 0.5f + 0.5f) * rain_str * 255);
          s->col = shade565(pack(70, 103, 195), light_shade(0, lb));
        } else {
          /* ...snow drifts slowly, and sideways */
          float g1 = rnd2 * 2 - 1, g2 = rnd1 * 2 - 1;
          float d8 = -((float)((uint32_t)cnt & 511) + (cnt - floorf(cnt))) / 512;
          float d9 = rnd1 + cnt * 0.01f * g1, d10 = rnd2 + cnt * g2 * 0.001f;
          s->v = (d8 + d10) * 256, s->u = d9 - floorf(d9);
          s->alpha = (uint8_t)(((1 - f) * 0.3f + 0.5f) * rain_str * 255);
          int sk = lb & 15, bk = lb >> 4;
          s->col = shade565(0xFFFF, light_shade(0, ((sk * 3 + 15) / 4) | ((bk * 3 + 15) / 4) << 4));
        }
        nsheets++;
      }
}

/* the height (cache y) on the line down through (x, z) seen at picture row py */
static float row_height(float x, float z, float py) {
  float k = (1 - 2 * py / RH) * TAN_V, rx = x - ox, rz = z - oz;
  float d = upy - k * fwy;
  if (fabsf(d) < 1e-4f) return 1e9f;
  return oy + (k * (rx * fwx + rz * fwz) - (rx * upx + rz * upz)) / d;
}

static void rain_strip(int py0, int rows, uint16_t (*cb)[RW], float (*zb)[RW]) {
  const float KX = RW / 2 / TAN_H, KY = RH / 2 / TAN_V;
  for (int n = 0; n < nsheets; n++) {
    const Sheet *s = &sheets[n];
    if (s->sy1 < py0 || s->sy0 >= py0 + rows) continue;
    const uint8_t(*run)[4] = s->snow ? snow_run : rain_run;
    const uint8_t *idx = s->snow ? snow_idx : rain_idx;
    /* the heights this strip shows, at both edges of the sheet */
    float ylo = 1e9f, yhi = -1e9f;
    for (int e = -1; e <= 1; e += 2)
      for (int k = 0; k <= 1; k++) {
        float y = row_height(s->cx + e * s->hx, s->cz + e * s->hz, (float)(py0 + k * rows));
        if (y < ylo) ylo = y;
        if (y > yhi) yhi = y;
      }
    if (ylo < s->y0) ylo = s->y0;
    if (yhi > s->y1) yhi = s->y1;
    if (ylo >= yhi) continue;
    /* far away the streaks are much thinner than a pixel: every other one, twice as strong */
    float tc = (s->cx - ox) * fwx + (s->cz - oz) * fwz + ((ylo + yhi) / 2 - oy) * fwy;
    int step = tc * 64 > KX * 3.5f ? 2 : 1;
    /* texture rows: row = y * 64 + v; runs are at most 16 rows long */
    int r0 = (int)floorf(ylo * 64 + s->v) - 16, r1 = (int)ceilf(yhi * 64 + s->v);
    for (int base = (int)floorf(r0 / 256.0f) * 256; base <= r1; base += 256) {
      int lo = r0 - base < 0 ? 0 : r0 - base, hi = r1 - base > 255 ? 255 : r1 - base;
      for (int i = idx[lo]; i < idx[hi + 1]; i += step) {
        int a = run[i][3] * s->alpha;
        if (a <= 25 * 255) continue;   /* 1.8's alpha test */
        float ya = (base + run[i][0] - s->v) * (1 / 64.0f), yb = ya + run[i][2] * (1 / 64.0f);
        if (ya < s->y0) ya = s->y0;
        if (yb > s->y1) yb = s->y1;
        if (yb <= ya) continue;
        float u = (run[i][1] + 0.5f) * (1 / 64.0f) + s->u;
        u = (u >= 1 ? u - 1 : u) * 2 - 1;
        float x = s->cx + s->hx * u - ox, z = s->cz + s->hz * u - oz;
        float bz = x * fwx + z * fwz;
        float za = bz + (ya - oy) * fwy, zb2 = bz + (yb - oy) * fwy;
        if (za < 0.05f || zb2 < 0.05f) continue;
        float t = (za + zb2) * 0.5f, it = 1 / t;
        int sx = (int)((x * rgx + z * rgz) * it * KX + RW / 2);
        if ((unsigned)sx >= RW) continue;
        float by = x * upx + z * upz;
        float top = RH / 2 - (by + (yb - oy) * upy) / zb2 * KY;
        float bot = RH / 2 - (by + (ya - oy) * upy) / za * KY;
        if (bot <= py0 || top >= py0 + rows) continue;
        /* how much of a pixel the texels cover: across, and down when shorter than one */
        float cover = KX * it * (1 / 64.0f);
        if (cover > 1) cover = 1;
        float ak = a * cover * step * (32 / 65025.0f);
        int k0 = (int)(top + 0.5f), k1 = (int)(bot - 0.5f);
        if (k1 < k0) k0 = k1 = (int)((top + bot) * 0.5f), ak *= bot - top;
        int k = (int)(ak + 0.5f);
        if (k <= 0) continue;
        if (k > 32) k = 32;
        for (int py = k0 < py0 ? py0 : k0; py <= k1 && py < py0 + rows; py++)
          if (zb[py - py0][sx] > t) cb[py - py0][sx] = mix565(cb[py - py0][sx], s->col, k);
      }
    }
  }
}

/* ---------------------------------------------------------------- lightning */
/* RenderLightningBolt: a path 128 blocks up in 16-block steps that wander up
 * to 5 blocks, and two branches that wander up to 15; each drawn four times,
 * 0.1, 0.3, 0.5 and 0.7 blocks wide, adding (0.45, 0.45, 0.5) x 0.3 of light */
static float bolt_off[3][9][2];   /* the path's offsets from its foot at heights 16 i; then the branches' */

static void bolt_frame(void) {
  if (!bolt.on) return;
  uint32_t r = bolt.seed;
#define BR(n) ((int)(((r = r * 1664525u + 1013904223u) >> 8) % (n)))
  bolt_off[0][0][0] = bolt_off[0][0][1] = 0;
  for (int i = 1; i <= 8; i++)
    bolt_off[0][i][0] = bolt_off[0][i - 1][0] + BR(11) - 5, bolt_off[0][i][1] = bolt_off[0][i - 1][1] + BR(11) - 5;
  for (int j = 1; j <= 2; j++) {
    bolt_off[j][8 - j][0] = bolt_off[0][8 - j][0], bolt_off[j][8 - j][1] = bolt_off[0][8 - j][1];
    for (int i = 7 - j; i >= 5 - j; i--)
      bolt_off[j][i][0] = bolt_off[j][i + 1][0] + BR(31) - 15, bolt_off[j][i][1] = bolt_off[j][i + 1][1] + BR(31) - 15;
  }
#undef BR
}

static inline uint16_t add565(uint16_t c, int k) {   /* the bolt's light, k/32 of a layer */
  return pack(r5(c) + 34 * k / 32, g6(c) + 34 * k / 32, b5(c) + 38 * k / 32);
}

/* one piece of the bolt between heights ya and yb (cache coordinates), its widths scaled by ma, mb */
static void bolt_piece(int py0, int rows, uint16_t (*cb)[RW], float (*zb)[RW], const float *a, const float *b, float ma,
                       float mb) {
  float p[2][3] = {{a[0] - ox, a[1] - oy, a[2] - oz}, {b[0] - ox, b[1] - oy, b[2] - oz}}, sx[2], sy[2], iz[2];
  float cz[2];
  for (int e = 0; e < 2; e++) cz[e] = p[e][0] * fwx + p[e][1] * fwy + p[e][2] * fwz;
  if (cz[0] < 0.05f && cz[1] < 0.05f) return;
  if (cz[0] < 0.05f || cz[1] < 0.05f) {
    /* cut where it passes the camera */
    int e = cz[0] < 0.05f ? 0 : 1;
    float s = (0.05f - cz[e]) / (cz[1 - e] - cz[e]);
    for (int k = 0; k < 3; k++) p[e][k] += (p[1 - e][k] - p[e][k]) * s;
    if (e == 0) ma += (mb - ma) * s; else mb += (ma - mb) * s;
    cz[e] = 0.05f;
  }
  for (int e = 0; e < 2; e++) {
    float cx = p[e][0] * rgx + p[e][2] * rgz, cy = p[e][0] * upx + p[e][1] * upy + p[e][2] * upz;
    sx[e] = (cx / cz[e] / TAN_H + 1) * RW / 2;
    sy[e] = (1 - cy / cz[e] / TAN_V) * RH / 2;
    iz[e] = 1 / cz[e];
  }
  if (fabsf(sx[1] - sx[0]) > fabsf(sy[1] - sy[0])) {
    /* more across than down: column by column */
    float lo = sx[0] < sx[1] ? sx[0] : sx[1], hi = sx[0] < sx[1] ? sx[1] : sx[0];
    int c0 = (int)ceilf(lo - 0.5f), c1 = (int)floorf(hi - 0.5f);
    for (int px = c0 < 0 ? 0 : c0; px <= c1 && px < RW; px++) {
      float s = (px + 0.5f - sx[0]) / (sx[1] - sx[0]);
      float y = sy[0] + (sy[1] - sy[0]) * s, t = 1 / (iz[0] + (iz[1] - iz[0]) * s), m = ma + (mb - ma) * s;
      for (int k = 0; k < 4; k++) {
        float hw = (0.1f + 0.2f * k) * m * 1.2f * (RW / 2 / TAN_H / t);
        int y0 = (int)floorf(y - hw + 0.5f), y1 = (int)floorf(y + hw - 0.5f), cov = 32;
        if (y1 < y0) y0 = y1 = (int)floorf(y), cov = (int)(hw * 64);
        if (cov <= 0) continue;
        for (int py = y0 < py0 ? py0 : y0; py <= y1 && py < py0 + rows; py++)
          if (zb[py - py0][px] > t) cb[py - py0][px] = add565(cb[py - py0][px], cov);
      }
    }
    return;
  }
  float lo = sy[0] < sy[1] ? sy[0] : sy[1], hi = sy[0] < sy[1] ? sy[1] : sy[0];
  int r0 = (int)ceilf(lo - 0.5f), r1 = (int)floorf(hi - 0.5f);
  if (r0 < py0) r0 = py0;
  if (r1 >= py0 + rows) r1 = py0 + rows - 1;
  for (int py = r0; py <= r1; py++) {
    float s = hi - lo < 1e-3f ? 0.5f : (py + 0.5f - sy[0]) / (sy[1] - sy[0]);
    float x = sx[0] + (sx[1] - sx[0]) * s, t = 1 / (iz[0] + (iz[1] - iz[0]) * s), m = ma + (mb - ma) * s;
    float px_per = RW / 2 / TAN_H / t;
    for (int k = 0; k < 4; k++) {
      float hw = (0.1f + 0.2f * k) * m * 1.2f * px_per;
      int x0 = (int)floorf(x - hw + 0.5f), x1 = (int)floorf(x + hw - 0.5f), cov = 32;
      if (x1 < x0) x0 = x1 = (int)floorf(x), cov = (int)(hw * 64);
      if (cov <= 0) continue;
      for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < RW; px++)
        if (zb[py - py0][px] > t) cb[py - py0][px] = add565(cb[py - py0][px], cov);
    }
  }
}

static void bolt_strip(int py0, int rows, uint16_t (*cb)[RW], float (*zb)[RW]) {
  if (!bolt.on) return;
  float bx = bolt.x - vc_x0, by = bolt.y - vc_y0, bz = bolt.z - vc_z0;
  for (int j = 0; j < 3; j++) {
    int lo = j ? 5 - j : 0, hi = j ? 8 - j : 8;
    for (int i = lo; i < hi; i++)
      for (int q = 0; q < 4; q++) {
        /* four pieces a step: the path is straight, the picture's perspective is not */
        float f0 = q / 4.0f, f1 = (q + 1) / 4.0f;
        const float *o0 = bolt_off[j][i], *o1 = bolt_off[j][i + 1];
        float a[3] = {bx + o0[0] + (o1[0] - o0[0]) * f0, by + 16 * (i + f0), bz + o0[1] + (o1[1] - o0[1]) * f0};
        float b[3] = {bx + o0[0] + (o1[0] - o0[0]) * f1, by + 16 * (i + f1), bz + o0[1] + (o1[1] - o0[1]) * f1};
        float ma = j ? 1 : (i - 1 + f0) * 0.1f + 1, mb = j ? 1 : (i - 1 + f1) * 0.1f + 1;
        bolt_piece(py0, rows, cb, zb, a, b, ma, mb);
      }
  }
}

/* ---------------------------------------------------------------- the fishing line */
/* RenderFish: from the rod's tip (in the right hand, 0.4 ahead of the eyes)
 * to the hook in 16 pieces, sagging, black */
static float line_pt[17][3];   /* picture x, y, 1 / depth (0: behind the camera) */
static bool line_on;

static void line_frame(void) {
  const Entity *e = bobber();
  line_on = e != NULL;
  if (!e) return;
  float t = tick_frac;
  float bx = e->px + (e->x - e->px) * t - vc_x0, by = e->py + (e->y - e->py) * t - vc_y0;
  float bz = e->pz + (e->z - e->pz) * t - vc_z0;
  /* the tip: (-0.36, -0.045, 0.4) x fov / 100, turned as the view is */
  float p = pl.pitch * 0.017453292f, yw = pl.yaw * 0.017453292f;
  float vx = -0.36f * 0.7f, vy = 0, vz = 0.4f;   /* (raised to the tip of the rod as drawn here) */
  float y1 = vy * cosf(p) - vz * sinf(p), z1 = vz * cosf(p) + vy * sinf(p);
  float x2 = vx * cosf(yw) - z1 * sinf(yw), z2 = z1 * cosf(yw) + vx * sinf(yw);
  float dx = ox + x2 - bx, dy = oy + y1 - by - 0.25f, dz = oz + z2 - bz;
  for (int l = 0; l <= 16; l++) {
    float f = l / 16.0f, *q = line_pt[l];
    float rx = bx + dx * f - ox, ry = by + dy * (f * f + f) / 2 + 0.25f - oy, rz = bz + dz * f - oz;
    float cz = rx * fwx + ry * fwy + rz * fwz;
    q[2] = 0;
    if (cz < 0.05f) continue;
    float cx = rx * rgx + rz * rgz, cy = rx * upx + ry * upy + rz * upz;
    q[0] = (cx / cz / TAN_H + 1) * RW / 2, q[1] = (1 - cy / cz / TAN_V) * RH / 2, q[2] = 1 / cz;
  }
}

static void line_strip(int py0, int rows, uint16_t (*cb)[RW], float (*zb)[RW]) {
  if (!line_on) return;
  for (int l = 0; l < 16; l++) {
    const float *a = line_pt[l], *b = line_pt[l + 1];
    if (a[2] <= 0 || b[2] <= 0) continue;
    float dx = b[0] - a[0], dy = b[1] - a[1];
    if ((a[1] < py0 && b[1] < py0) || (a[1] >= py0 + rows && b[1] >= py0 + rows)) continue;
    int n = (int)ceilf(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
    if (n < 1) n = 1;
    for (int k = 0; k <= n; k++) {
      float s = (float)k / n;
      int x = (int)floorf(a[0] + dx * s), y = (int)floorf(a[1] + dy * s);
      if (y < py0 || y >= py0 + rows || (unsigned)x >= RW) continue;
      if (zb[y - py0][x] * (a[2] + (b[2] - a[2]) * s) > 1) cb[y - py0][x] = 0;
    }
  }
}

/* ---------------------------------------------------------------- the block looked at */
/* RenderGlobal.drawSelectionBox: the edges of the block's selection box (its shape, grown
 * by 0.002) in black at 40%, hidden where something is in front of them */
static float sel_pt[8][3];   /* the corners on the picture: x, y, 1 / depth (0: behind the camera) */
static float sel_w[8][3];    /* and in the cache */
static bool sel_on;

static void select_frame(void) {
  sel_on = false;
  if (pl.hit_face < 0 || gui != GUI_NONE || !world_loaded(pl.hit_x, pl.hit_y, pl.hit_z)) return;
  int b = world_get(pl.hit_x, pl.hit_y, pl.hit_z);
  if (b == B_AIR || blk_model[b] == M_LIQUID) return;
  float s[6];
  select_box(b, pl.hit_x, pl.hit_y, pl.hit_z, s);
  for (int i = 0; i < 3; i++) s[i] -= 0.002f, s[i + 3] += 0.002f;
  float bx = (float)(pl.hit_x - vc_x0), by = (float)(pl.hit_y - vc_y0), bz = (float)(pl.hit_z - vc_z0);
  for (int k = 0; k < 8; k++) {
    float *w = sel_w[k], *q = sel_pt[k];
    w[0] = bx + s[k & 1 ? 3 : 0], w[1] = by + s[k & 2 ? 4 : 1], w[2] = bz + s[k & 4 ? 5 : 2];
    float rx = w[0] - ox, ry = w[1] - oy, rz = w[2] - oz, cz = rx * fwx + ry * fwy + rz * fwz;
    q[2] = 0;
    if (cz < 0.05f) continue;
    float cx = rx * rgx + rz * rgz, cy = rx * upx + ry * upy + rz * upz;
    q[0] = (cx / cz / TAN_H + 1) * RW / 2, q[1] = (1 - cy / cz / TAN_V) * RH / 2, q[2] = 1 / cz;
  }
  sel_on = true;
}

static void select_strip(int py0, int rows, uint16_t (*cb)[RW], float (*zb)[RW]) {
  if (!sel_on) return;
  static const uint8_t edge[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                      {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (int e = 0; e < 12; e++) {
    const float *a = sel_pt[edge[e][0]], *b = sel_pt[edge[e][1]];
    if (a[2] <= 0 || b[2] <= 0) continue;   /* (an edge reaching behind the camera: not drawn) */
    if ((a[1] < py0 - 1 && b[1] < py0 - 1) || (a[1] > py0 + rows && b[1] > py0 + rows)) continue;
    float dx = b[0] - a[0], dy = b[1] - a[1];
    int n = (int)ceilf(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
    if (n < 1) n = 1;
    int lx = -1, ly = -1;
    for (int k = 0; k <= n; k++) {
      float s = (float)k / n;
      int x = (int)floorf(a[0] + dx * s), y = (int)floorf(a[1] + dy * s);
      if ((x == lx && y == ly) || y < py0 || y >= py0 + rows || (unsigned)x >= RW) continue;
      lx = x, ly = y;
      /* in front of what was drawn there (or the face it outlines: a little give) */
      float t = 1 / (a[2] + (b[2] - a[2]) * s);
      if (zb[y - py0][x] * 1.02f + 0.02f > t) cb[y - py0][x] = mix565(cb[y - py0][x], 0, 13);
    }
  }
}

static void ents_strip(int py0, int rows, uint16_t (*cb)[RW], float (*zb)[RW]) {
  Box bx[MAX_PARTS];
  particles_strip(py0, rows, cb, zb);
  for (int k = 0; k < ndraws; k++) {
    const Draw *dr = &draws[k];
    if (dr->y1 <= py0 || dr->y0 >= py0 + rows) continue;
    const Entity *e = dr->e;
    bool mob = e->type >= E_ZOMBIE && e->type <= E_CHICKEN;
    int n = mob ? make_boxes(dr, bx) : 0;
    /* hurt (and dying): a red tint; a creeper about to blow flashes white */
    int red = mob && (e->hurt > 0 || e->state == 255) ? 10 : 0;
    int white = e->type == E_CREEPER && e->delay > 0 && ((e->delay / 3) & 1) ? 16 : 0;
    /* each part over its own rectangle (the depth buffer keeps the nearest); an item over the draw's */
    for (int p = 0; p < (mob ? n : 1); p++) {
      int x0 = dr->x0, y0 = dr->y0, x1 = dr->x1, y1 = dr->y1;
      if (mob) x0 = bx[p].r[0], y0 = bx[p].r[1], x1 = bx[p].r[2], y1 = bx[p].r[3];
      for (int py = py0 > y0 ? py0 : y0; py < y1 && py < py0 + rows; py++)
        for (int px = x0; px < x1; px++) {
          float d[3];
          ray_dir(px, py, d);
          float t = zb[py - py0][px];
          uint16_t c;
          bool hit = mob ? boxes_hit(&bx[p], 1, d, &t, &c) : item_hit(dr, d, &t, &c);
          if (!hit) continue;
          if (mob) {
            if (red) c = mix565(c, 0xF800, red);
            if (white) c = mix565(c, 0xFFFF, white);
            float dist = t * sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (dist > fog0) c = mix565(c, fog565, dist >= fog1 ? 32 : (int)((dist - fog0) * fog_k * 32));
          }
          cb[py - py0][px] = c;
          zb[py - py0][px] = t;
        }
    }
  }
  select_strip(py0, rows, cb, zb);
  line_strip(py0, rows, cb, zb);
  rain_strip(py0, rows, cb, zb);
  bolt_strip(py0, rows, cb, zb);
}

/* ---------------------------------------------------------------- the frame */
static void setup_light(float sun) {
  /* Minecraft's light table (gamma "Moody"): (1 - f) / (3f + 1), f = 1 - level / 15,
   * sky light scaled by the sun, then brightened to at least 0.03 */
  static const float face_shade[4] = {1.0f, 0.8f, 0.6f, 0.5f};
  for (int l = 0; l < 16; l++) {
    float f = 1 - l / 15.0f;
    float bri = (1 - f) / (f * 3 + 1);
    bri = bri * (sun * 0.95f + 0.05f);
    bri = bri * 0.96f + 0.03f;
    for (int s = 0; s < 4; s++) shade[s][l] = (int)(bri * face_shade[s] * 32 + 0.5f);
    /* block light does not follow the sun */
    float blk = (1 - f) / (f * 3 + 1) * 0.96f + 0.03f;
    for (int s = 0; s < 4; s++) shade_blk[s][l] = (int)(blk * face_shade[s] * 32 + 0.5f);
  }
}

/* What each strip on screen shows, as a hash. On a screen that holds still (a menu, or the
 * world paused under one) a strip goes to the screen only when it changed: the calculator's
 * screen shows a picture as it arrives, so sending all of it again made a change (a button lit,
 * a menu opening) sweep down the screen. */
static uint32_t shown[RH / SR];
static bool shown_ok;
static uint32_t strip_hash(void) {
  const uint32_t *w = (const uint32_t *)strip;
  uint32_t h = 2166136261u;
  for (unsigned k = 0; k < sizeof strip / 4; k++) h = (h ^ w[k]) * 16777619u;
  return h;
}
static bool still_screen(void) {
  return gui == GUI_PAUSE || gui == GUI_OPTIONS || gui == GUI_CONTROLS || gui == GUI_WORLDS || gui == GUI_CREATE ||
         gui == GUI_CONFIRM || gui == GUI_RENAME || gui == GUI_LOADING || gui == GUI_LAN || gui == GUI_KEYS;
}
static void send_strip(int py, bool still) {
  if (still) {
    uint32_t h = strip_hash();
    if (shown_ok && shown[py / SR] == h) return;
    shown[py / SR] = h;
  }
  plat_push(0, py * 2, SCREEN_W, SR * 2, strip);
}
static void strips_sent(bool still) { shown_ok = still; }

void render_frame(const Camera *c, uint32_t tod) {
  bool still = still_screen();
  static int last_gui;
  bool opening_pause = gui == GUI_PAUSE && last_gui == GUI_NONE && c;
  last_gui = gui;
  if (opening_pause) {
    /* the game paused: its picture stays as it is on screen, darkened under the menu (read back
     * from the screen a strip at a time, not drawn again, so the menu comes up at once) */
    for (int py = 0; py < RH; py += SR) {
      plat_pull(0, py * 2, SCREEN_W, SR * 2, strip);
      /* (the crosshair, drawn inverting what was behind it, inverted back: menus have none) */
      for (int k = -4; k <= 4; k++) {
        int pts[2][2] = {{SCREEN_W / 2 + k, SCREEN_H / 2}, {SCREEN_W / 2, SCREEN_H / 2 + k}};
        for (int q = 0; q < 2; q++) {
          int x = pts[q][0], y = pts[q][1] - py * 2;
          if ((q == 1 && k == 0) || y < 0 || y >= SR * 2) continue;
          strip[y * SCREEN_W + x] = (uint16_t)~strip[y * SCREEN_W + x];
        }
      }
      hud_strip(strip, py * 2, SR * 2);
      send_strip(py, false);
      shown[py / SR] = strip_hash();
    }
    shown_ok = true;
    return;
  }
  if (!c) {
    /* no world: only what the screens draw */
    for (int py = 0; py < RH; py += SR) {
      memset(strip, 0, sizeof strip);
      hud_strip(strip, py * 2, SR * 2);
      send_strip(py, still);
    }
    strips_sent(still);
    return;
  }
  float yaw = c->yaw * 0.017453292f, pitch = c->pitch * 0.017453292f;
  float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  fwx = -sy * cp;
  fwy = -sp;
  fwz = cy * cp;
  rgx = -cy;
  rgz = -sy;
  /* up = right x forward */
  upx = 0 * fwz - rgz * fwy;
  upy = rgz * fwx - rgx * fwz;
  upz = rgx * fwy - 0 * fwx;
  ox = c->x - vc_x0;
  oy = c->y - vc_y0;
  oz = c->z - vc_z0;
  /* the time of day: 0 sunrise, 6000 noon, 18000 midnight (Minecraft's ticks) */
  float ang = (tod % 24000) / 24000.0f - 0.25f;
  if (ang < 0) ang += 1;
  ang = ang + ((1 - (cosf(ang * 3.14159265f) + 1) / 2) - ang) / 3;
  float day = cosf(ang * 6.2831853f) * 2 + 0.5f;
  if (day < 0) day = 0;
  if (day > 1) day = 1;
  /* World.getSunBrightness: dimmer in rain, dimmer still in a storm */
  float rs = rain_str, ts = thunder_str * rain_str;
  float sunb = 1 - (cosf(ang * 6.2831853f) * 2 + 0.2f);
  sunb = sunb < 0 ? 0 : sunb > 1 ? 1 : sunb;
  sunb = (1 - sunb) * (1 - rs * 5 / 16) * (1 - ts * 5 / 16) * 0.8f + 0.2f;
  setup_light(last_bolt > 0 ? 1 : sunb);   /* (a lightning flash lights everything) */
  night = day < 0.5f;
  /* the sun's direction: it rises in the east (+X) */
  float sa = ang * 6.2831853f;
  sun_x = -sinf(sa);
  sun_y = cosf(sa);
  sun_z = 0;
  sun_tx = -cosf(sa), sun_ty = -sinf(sa);   /* (the sky's turn) */
  cos_c = cosf(sa), sin_c = sinf(sa);
  moon_phase = (int)((tod / 24000) % 8);
  /* World.getStarBrightness, added as RenderGlobal blends it (its square) */
  {
    float f = 1 - (cosf(sa) * 2 + 0.25f);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    float k = f * f * 0.5f * (1 - rain_str);
    star_k = (int)(k * k * 255);
  }
  /* the sky's colour from the biome's temperature (1.8: HSB), and the fog's */
  int col = (int)(oz) * VCX + (int)(ox);
  if (col < 0 || col >= VCX * VCZ) col = 0;
  float temp = biome_temp[vbiome[col]] / 50.0f / 3.0f;
  if (temp < -1) temp = -1;
  if (temp > 1) temp = 1;
  float h = 0.62222224f - temp * 0.05f, s = 0.5f + temp * 0.1f;
  /* HSB to RGB, brightness 1 */
  float hh = (h - floorf(h)) * 6, ff = hh - floorf(hh);
  float p = 1 - s, q = 1 - s * ff, tt = 1 - s * (1 - ff);
  float R, G, B;
  switch ((int)hh) {
    case 0: R = 1, G = tt, B = p; break;
    case 1: R = q, G = 1, B = p; break;
    case 2: R = p, G = 1, B = tt; break;
    case 3: R = p, G = q, B = 1; break;
    case 4: R = tt, G = p, B = 1; break;
    default: R = 1, G = p, B = q; break;
  }
  float sr = R * day, sg = G * day, sbl = B * day;
  /* World.getSkyColor: rain and storms turn it grey */
  if (rs > 0) {
    float lum = (sr * 0.3f + sg * 0.59f + sbl * 0.11f) * 0.6f, k = 1 - rs * 0.75f;
    sr = sr * k + lum * (1 - k), sg = sg * k + lum * (1 - k), sbl = sbl * k + lum * (1 - k);
  }
  if (ts > 0) {
    float lum = (sr * 0.3f + sg * 0.59f + sbl * 0.11f) * 0.2f, k = 1 - ts * 0.75f;
    sr = sr * k + lum * (1 - k), sg = sg * k + lum * (1 - k), sbl = sbl * k + lum * (1 - k);
  }
  if (last_bolt > 0) {
    /* the flash: towards a pale blue */
    float k = last_bolt - tick_frac;
    k = (k > 1 ? 1 : k) * 0.45f;
    sr = sr * (1 - k) + 0.8f * k, sg = sg * (1 - k) + 0.8f * k, sbl = sbl * (1 - k) + k;
  }
  sky_r = (int)(sr * 255);
  sky_g = (int)(sg * 255);
  sky_b = (int)(sbl * 255);
  float fr = 0.7529412f * (day * 0.94f + 0.06f), fg = 0.84705883f * (day * 0.94f + 0.06f), fb = 1.0f * (day * 0.91f + 0.09f);
  /* 1.8 mixes the fog towards the sky at short view distances, then darkens it in rain and storms */
  /* WorldProvider.calcSunriseSunsetColors; facing the sun, the fog takes the colour */
  ss_a = 0;
  {
    float f1 = cosf(ang * 6.2831853f);
    if (f1 >= -0.4f && f1 <= 0.4f) {
      float f3 = f1 / 0.4f * 0.5f + 0.5f, f4 = 1 - (1 - sinf(f3 * 3.14159265f)) * 0.99f;
      ss_a = f4 * f4;
      ss_r = (int)((f3 * 0.3f + 0.7f) * 255), ss_g = (int)((f3 * f3 * 0.7f + 0.2f) * 255), ss_b = (int)(0.2f * 255);
      ss_side = -sinf(ang * 6.2831853f) > 0 ? 1 : -1;
      float f5 = fwx * ss_side;
      if (f5 > 0) {
        f5 *= ss_a;
        fr = fr * (1 - f5) + ss_r / 255.0f * f5, fg = fg * (1 - f5) + ss_g / 255.0f * f5;
        fb = fb * (1 - f5) + ss_b / 255.0f * f5;
      }
    }
  }
  fr += (sr - fr) * 0.26f;
  fg += (sg - fg) * 0.26f;
  fb += (sbl - fb) * 0.26f;
  fr *= (1 - rs * 0.5f) * (1 - ts * 0.5f);
  fg *= (1 - rs * 0.5f) * (1 - ts * 0.5f);
  fb *= (1 - rs * 0.4f) * (1 - ts * 0.5f);
  /* World.getCloudColour */
  {
    float cr = 1, cg = 1, cbl = 1;
    if (rs > 0) {
      float lum = 0.6f, k = 1 - rs * 0.95f;
      cr = cg = cbl = k + lum * (1 - k);
    }
    cr *= day * 0.9f + 0.1f, cg *= day * 0.9f + 0.1f, cbl *= day * 0.85f + 0.15f;
    if (ts > 0) {
      float lum = (cr * 0.3f + cg * 0.59f + cbl * 0.11f) * 0.2f, k = 1 - ts * 0.95f;
      cr = cr * k + lum * (1 - k), cg = cg * k + lum * (1 - k), cbl = cbl * k + lum * (1 - k);
    }
    cloud_r = (int)(cr * 255), cloud_g = (int)(cg * 255), cloud_b = (int)(cbl * 255);
  }
  sun_a = 1 - rs;
  fog_r = (int)(fr * 255);
  fog_g = (int)(fg * 255);
  fog_b = (int)(fb * 255);
  fog1 = MAX_T;
  fog0 = MAX_T * 0.75f;
  cam_x = (int)floorf(ox), cam_y = (int)floorf(oy), cam_z = (int)floorf(oz);
  cam_i = (unsigned)cam_x < VCX && (unsigned)cam_y < VCY && (unsigned)cam_z < VCZ ? VC_I(cam_x, cam_y, cam_z) : -1;
  cam_b = cam_i < 0 ? B_AIR : vc[cam_i];
  cam_mi = cam_i < 0 ? 0 : MC_I(cam_x, cam_y, cam_z);
  cam_fr[0][0] = cam_x - ox, cam_fr[0][1] = cam_x + 1 - ox;
  cam_fr[1][0] = cam_y - oy, cam_fr[1][1] = cam_y + 1 - oy;
  cam_fr[2][0] = cam_z - oz, cam_fr[2][1] = cam_z + 1 - oz;
  under_water = is_water(cam_b);
  if (under_water) {
    fog_r = 10, fog_g = 30, fog_b = 110;
    fog0 = 0;
    fog1 = 9;
  }
  fog_k = 1 / (fog1 - fog0);
  fog565 = pack(fog_r, fog_g, fog_b);
  for (int k = 0; k <= 128; k++)
    sky_grad[k] = (uint32_t)(fog_r + (sky_r - fog_r) * k / 128) << 20 | (uint32_t)(fog_g + (sky_g - fog_g) * k / 128) << 10 |
                  (uint32_t)(fog_b + (sky_b - fog_b) * k / 128);
  /* the highest row with anything in it */
  sky_top = 0;
  for (int k = MCY * MCZ * MCX - 1; k >= 0; k--)
    if (vmac[k]) {
      sky_top = (k / (MCZ * MCX) + 1) * 4;
      break;
    }
  cloud_off = (tod % 24000) * 0.03f;

  /* the block being broken */
  brk_i = -1;
  if (pl.breaking > 0 && pl.hit_face >= 0 && world_loaded(pl.hit_x, pl.hit_y, pl.hit_z)) {
    brk_i = VC_I(pl.hit_x - vc_x0, pl.hit_y - vc_y0, pl.hit_z - vc_z0);
    brk_stage = (int)(pl.breaking * 10);
    if (brk_stage > 9) brk_stage = 9;
  }
  ents_frame();
  rain_frame();
  bolt_frame();
  line_frame();
  select_frame();
  /* The picture in 4 x 4 cells: their corners and middles are traced. A cell whose corners
   * and middle met the same thing (a face, the sky, a plant) is drawn from them; any other is
   * cut in four of 2 x 2, the points between traced as needed (once, for both sides) */
  for (int px = 0; px <= RW; px++) col_su[px] = (2 * (px + 0.5f) / RW - 1) * TAN_H;
  static bool tops;
  if (!tops) shape_tops(), tops = true;
  cam_liq = liquid[cam_b] ? liquid[cam_b] : 255;
  bool fast_sky = sky_fast();
  for (int k = 0; k <= 128; k++) {
    uint32_t gc = sky_grad[k];
    sky_pk[k] = pack((int)(gc >> 20), (int)(gc >> 10) & 1023, (int)gc & 1023);
  }
  enum { NC = RW / 4 };
  static Sample cn[2][NC + 1], ed[2][NC + 1], md[2 * NC + 1];   /* corners, edges' middles, the middle row */
  static uint8_t ed_ok[2][NC + 1], md_ok[2 * NC + 1];
  Sample *ct = cn[0], *cbt = cn[1], *et = ed[0], *eb = ed[1];
  uint8_t *et_ok = ed_ok[0], *eb_ok = ed_ok[1];
  for (int k = 0; k <= NC; k++) sample_at(&ct[k], 4 * k, 0);
  memset(et_ok, 0, NC + 1);
  for (int py = 0; py < RH; py += SR) {
    for (int k = 0; k <= NC; k++) sample_at(&cbt[k], 4 * k, py + 4);
    memset(eb_ok, 0, NC + 1);
    memset(md_ok, 0, sizeof md_ok);
    for (int k = 0; k < NC; k++) sample_at(&md[2 * k + 1], 4 * k + 2, py + 2), md_ok[2 * k + 1] = 1;
    /* the rays' bases for the strip's rows (the forward and up parts) */
    float rb[SR][3];
    SkyRow skr[SR];
    float dy_top = fwy + upy * (1 - 2 * (py + 0.5f) / RH) * TAN_V, dy_bot = fwy + upy * (1 - 2 * (py + SR + 0.5f) / RH) * TAN_V;
    (void)dy_top, (void)dy_bot;
    for (int r = 0; r < SR; r++) {
      float sv = (1 - 2 * (py + r + 0.5f) / RH) * TAN_V;
      rb[r][0] = fwx + upx * sv, rb[r][1] = fwy + upy * sv, rb[r][2] = fwz + upz * sv;
      if (fast_sky) sky_row(&skr[r], rb[r]);
    }
    for (int k = 0; k < NC; k++) {
      int x0 = 4 * k;
      const Sample *a = &ct[k], *b = &ct[k + 1], *c = &cbt[k], *d = &cbt[k + 1], *m = &md[2 * k + 1];
      if (same(a, b) && same(a, c) && same(a, d) && same(a, m)) {
        bool one = a->cell == b->cell && a->cell == c->cell && a->cell == d->cell && a->cell == m->cell;
        (void)one;
#ifndef FULL_TRACE
        if (a->kind == 1 && (one ? (a->flags & 1) && a->cell != brk_i : true)) {
          if (one) face_fill(cbuf, zbuf, rb, x0, a);
          else plane_fill(cbuf, zbuf, rb, x0, a);
          cbuf[0][x0] = a->c, zbuf[0][x0] = a->t;
          cbuf[2][x0 + 2] = m->c, zbuf[2][x0 + 2] = m->t;
          continue;
        }
        if (a->kind == 2 && fast_sky) {
          sky_fill(cbuf, zbuf, rb, skr, x0);
          cbuf[0][x0] = a->c, cbuf[2][x0 + 2] = m->c;
          continue;
        }
#endif
        for (int r = 0; r < SR; r++)
          for (int x = x0; x < x0 + 4; x++) {
            if (r == 0 && x == x0) cbuf[0][x] = a->c, zbuf[0][x] = a->t;
            else if (r == 2 && x == x0 + 2) cbuf[2][x] = m->c, zbuf[2][x] = m->t;
            else {
#ifdef FULL_TRACE
              cbuf[r][x] = between(rb[r], x, a, b, c, d);
#else
              float su = col_su[x];
              cbuf[r][x] = agreed(rb[r][0] + rgx * su, rb[r][1], rb[r][2] + rgz * su, a, one);
#endif
              zbuf[r][x] = last_t;
            }
          }
        continue;
      }
#ifndef FULL_TRACE
      /* Samples that all met blocks or plants from above: whatever the rays between them meet is
       * there too, no higher than the highest of those blocks (anything above it in the way would
       * show in a sample, as in a cell of samples that agree), so they start on that plane */
      if (dy_top < 0 && dy_bot < 0 && !is_water(cam_b)) {
        const Sample *v[5] = {a, b, c, d, m};
        int top = 0;
        for (int q = 0; q < 5 && top >= 0; q++) {
          int ty = v[q]->kind == 1 ? v[q]->y + 1 : v[q]->kind == 3 ? v[q]->cell / (VCX * VCZ) + 1 : -1;
          top = ty < 0 ? -1 : ty > top ? ty : top;
        }
        if (top > 0 && oy > top) trace_top = top;
      }
#endif
      if (!et_ok[k]) sample_at(&et[k], x0 + 2, py), et_ok[k] = 1;
      if (!md_ok[2 * k]) sample_at(&md[2 * k], x0, py + 2), md_ok[2 * k] = 1;
      if (!md_ok[2 * k + 2]) sample_at(&md[2 * k + 2], x0 + 4, py + 2), md_ok[2 * k + 2] = 1;
      if (!eb_ok[k]) sample_at(&eb[k], x0 + 2, py + 4), eb_ok[k] = 1;
      quad(cbuf, zbuf, rb, 0, x0, a, &et[k], &md[2 * k], m);
      quad(cbuf, zbuf, rb, 0, x0 + 2, &et[k], b, m, &md[2 * k + 2]);
      quad(cbuf, zbuf, rb, 2, x0, &md[2 * k], m, c, &eb[k]);
      quad(cbuf, zbuf, rb, 2, x0 + 2, m, &md[2 * k + 2], &eb[k], d);
      trace_top = -1;
    }
    Sample *ts = ct;
    ct = cbt, cbt = ts;
    ts = et, et = eb, eb = ts;
    uint8_t *to = et_ok;
    et_ok = eb_ok, eb_ok = to;
    ents_strip(py, SR, cbuf, zbuf);
    /* 2 x 2 screen pixels each */
    for (int r = 0; r < SR; r++) {
      /* (a word a picture pixel: the C library's memcpy here goes a byte at a time) */
      uint32_t *d = (uint32_t *)(strip + r * 2 * SCREEN_W), *d2 = d + SCREEN_W / 2;
      const uint16_t *c = cbuf[r];
      for (int x = 0; x < RW; x += 2) {
        uint32_t lo = c[x] * 0x10001u, hi = c[x + 1] * 0x10001u;
        d[x] = d2[x] = lo;
        d[x + 1] = d2[x + 1] = hi;
      }
    }
    hud_strip(strip, py * 2, SR * 2);
    send_strip(py, still);
  }
  strips_sent(still);
}
