/* NumBlocks: the hand and what it holds, in 3D, as 1.8's ItemRenderer.renderItemInFirstPerson
 * places them: the arm is a box of the skin (RenderPlayer.renderRightArm), a block its model at
 * twice the size, an item its icon one pixel thick (ItemModelGenerator), each moved by the same
 * transforms (the swing, the dip when it changes, eating, drawing a bow, the view bobbing and a
 * sway when turning) and lit by the two item lights (RenderHelper.enableStandardItemLighting).
 * Each frame the model's transform is set up once; each pixel then traces its ray through the
 * model, the way the world is drawn. */
#include <math.h>
#include <string.h>
#include "nb.h"

int view_light(void);
int held_texel(int b, int face, int u, int v);

/* ---------------------------------------------------------------- the state (per tick) */
static int swing_t = -1;                        /* ticks into the swing (6), -1: none */
static float equipped = 1, prev_equipped = 1;   /* 1: raised, 0: lowered (changing what is held) */
static int shown = -1;                          /* the item drawn (the old one while lowering) */
static float arm_pitch, arm_yaw, prev_arm_pitch, prev_arm_yaw;   /* the view, lagging (the sway) */

void player_swing(void) {
  if (swing_t < 0 || swing_t >= 3) swing_t = 0;   /* EntityLivingBase.swingItem: 6 ticks */
}

void hand_tick(void) {
  if (swing_t >= 0 && ++swing_t >= 6) swing_t = -1;
  /* ItemRenderer.updateEquippedItem: down 0.4 a tick, the new item once below 0.1, up again */
  int id = held()->id;
  prev_equipped = equipped;
  float d = (id != shown ? 0.0f : 1.0f) - equipped;
  equipped += d < -0.4f ? -0.4f : d > 0.4f ? 0.4f : d;
  if (equipped < 0.1f) shown = id;
  /* EntityPlayerSP: renderArmPitch and renderArmYaw follow the view halfway each tick */
  prev_arm_pitch = arm_pitch, prev_arm_yaw = arm_yaw;
  float dy = pl.yaw - arm_yaw;
  while (dy >= 180) dy -= 360;
  while (dy < -180) dy += 360;
  arm_pitch += (pl.pitch - arm_pitch) * 0.5f;
  arm_yaw += dy * 0.5f;
}

/* ---------------------------------------------------------------- transforms (GlStateManager) */
typedef struct { float m[3][4]; } Mat;   /* affine: rows x, y, z; column 3 the translation */
static const Mat IDENT = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};

static void mul(Mat *a, const Mat *b) {   /* a = a b */
  Mat r;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 4; j++)
      r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + (j == 3 ? a->m[i][3] : 0);
  *a = r;
}
static void translate(Mat *a, float x, float y, float z) {
  Mat t = {{{1, 0, 0, x}, {0, 1, 0, y}, {0, 0, 1, z}}};
  mul(a, &t);
}
static void scale(Mat *a, float x, float y, float z) {
  Mat t = {{{x, 0, 0, 0}, {0, y, 0, 0}, {0, 0, z, 0}}};
  mul(a, &t);
}
static void rotate(Mat *a, float deg, int axis) {   /* glRotate about x (0), y (1) or z (2) */
  if (deg == 0) return;
  float r = deg * 0.017453292f, c = cosf(r), s = sinf(r);
  Mat t = IDENT;
  int i = (axis + 1) % 3, j = (axis + 2) % 3;
  t.m[i][i] = c, t.m[i][j] = -s, t.m[j][i] = s, t.m[j][j] = c;
  mul(a, &t);
}

/* ---------------------------------------------------------------- this frame's model */
enum { H_NONE, H_ARM, H_BLOCK, H_ITEM };
static int kind, model_id, icon;     /* what is drawn: the block, or the item's icon */
static int nbox;
static float box[5][6];              /* the model's boxes, in its pixels (x0 y0 z0 x1 y1 z1) */
static float org[3], dx0[3], ddx[3], ddy[3];   /* the eye and the ray of pixel (x, y), in the model */
static int face_sh[6];               /* each face's shade, 0..32: -x +x -y +y -z +z */
/* the faces turned to the eye (at most 3 a box), with their corners on the screen */
typedef struct { int8_t b, f; bool near; float sx[4], sy[4], ymin, ymax; } Face;
static Face faces[15];
static int nfaces;

#define FOCAL 171.38f   /* pixels: 120 / tan(35 degrees), 1.8's 70 degree field of view */
/* 1.8's window is 16:9: the hand is drawn as it is at the right of one as tall as the screen */
#define HAND_CX (SCREEN_W - SCREEN_H * 16 / 9.0f / 2)

/* ItemRenderer.transformFirstPersonItem */
static void first_person_item(Mat *m, float equip, float swing) {
  translate(m, 0.56f, -0.52f, -0.72f);
  translate(m, 0, equip * -0.6f, 0);
  rotate(m, 45, 1);
  float f = sinf(swing * swing * 3.14159265f), f1 = sinf(sqrtf(swing) * 3.14159265f);
  rotate(m, f * -20, 1);
  rotate(m, f1 * -20, 2);
  rotate(m, f1 * -80, 0);
  scale(m, 0.4f, 0.4f, 0.4f);
}

static bool flat_item(int id) {
  if (id >= 256) return true;
  int m = blk_model[id];
  return m == M_CROSS || m == M_TORCH || m == M_FLAT || m == M_VINE || m == M_LADDER;
}

void hand_frame(void) {
  kind = H_NONE;
  if (pl.dead || pl.sleep_timer || gui > GUI_PAUSE) return;
  float t = tick_frac;
  float swing = swing_t < 0 ? 0 : (swing_t + t) / 6.0f;
  if (swing > 1) swing = 1;
  float equip = 1 - (prev_equipped + (equipped - prev_equipped) * t);
  int id = shown < 0 ? held()->id : shown;
  Mat m = IDENT, bob = IDENT;
  if (opt.bobbing && !pl.flying) {
    /* EntityRenderer.setupViewBobbing, applied to the hand too */
    float w = -(pl.prev_walked + (pl.walked - pl.prev_walked) * t) * 3.14159265f;
    float b = pl.prev_bob + (pl.bob - pl.prev_bob) * t;
    translate(&bob, sinf(w) * b * 0.5f, -fabsf(cosf(w) * b), 0);
    rotate(&bob, sinf(w) * b * 3, 2);
    rotate(&bob, fabsf(cosf(w - 0.2f) * b) * 5, 0);
    m = bob;
  }
  /* rotateWithPlayerRotations: the sway */
  float ap = prev_arm_pitch + (arm_pitch - prev_arm_pitch) * t, ay = prev_arm_yaw + (arm_yaw - prev_arm_yaw) * t;
  float dyaw = pl.yaw - ay;
  while (dyaw >= 180) dyaw -= 360;
  while (dyaw < -180) dyaw += 360;
  rotate(&m, (pl.pitch - ap) * 0.1f, 0);
  rotate(&m, dyaw * 0.1f, 1);
  model_id = id;
  if (!id) {
    /* renderPlayerArm */
    float sq = sinf(sqrtf(swing) * 3.14159265f);
    translate(&m, -0.3f * sq, 0.4f * sinf(sqrtf(swing) * 6.2831853f), -0.4f * sinf(swing * 3.14159265f));
    translate(&m, 0.64f, -0.6f, -0.72f);
    translate(&m, 0, equip * -0.6f, 0);
    rotate(&m, 45, 1);
    rotate(&m, sq * 70, 1);
    rotate(&m, sinf(swing * swing * 3.14159265f) * -20, 2);
    translate(&m, -1, 3.6f, 3.5f);
    rotate(&m, 120, 2);
    rotate(&m, 200, 0);
    rotate(&m, -135, 1);
    translate(&m, 5.6f, 0, 0);
    /* ModelPlayer's right arm: at (-5, 2, 0), turned 0.1 radians about z, a 4 x 12 x 4 box */
    translate(&m, -5 / 16.0f, 2 / 16.0f, 0);
    rotate(&m, 5.729578f, 2);
    scale(&m, 1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
    kind = H_ARM, nbox = 1;
    static const float arm[6] = {-3, -2, -2, 1, 10, 2};
    memcpy(box[0], arm, sizeof arm);
  } else {
    bool block = !flat_item(id);
    int k = item_kind(id);
    if (pl.using_ticks > 0 && k == IK_BOW) {
      first_person_item(&m, equip, 0);
      /* doBowTransformations */
      rotate(&m, -18, 2);
      rotate(&m, -12, 1);
      rotate(&m, -8, 0);
      translate(&m, -0.9f, 0.2f, 0);
      float f = pl.using_ticks + t - 1, f1 = f / 20;
      f1 = (f1 * f1 + f1 * 2) / 3;
      if (f1 > 1) f1 = 1;
      if (f1 > 0.1f) translate(&m, 0, sinf((f - 0.1f) * 1.3f) * (f1 - 0.1f) * 0.01f, 0);
      translate(&m, 0, 0, f1 * 0.1f);
      scale(&m, 1, 1, 1 + f1 * 0.2f);
    } else if (pl.using_ticks > 0) {
      /* performDrinking: up to the mouth, bobbing */
      float f = 32 - pl.using_ticks - t + 1, f1 = f / 32;
      float f2 = f1 >= 0.8f ? 0 : fabsf(cosf(f / 4 * 3.14159265f) * 0.1f);
      translate(&m, 0, f2, 0);
      float f3 = 1 - powf(f1, 27);
      translate(&m, f3 * 0.6f, f3 * -0.5f, 0);
      rotate(&m, f3 * 90, 1);
      rotate(&m, f3 * 10, 0);
      rotate(&m, f3 * 30, 2);
      first_person_item(&m, equip, 0);
    } else {
      /* doItemUsedTransformations */
      float sq = sinf(sqrtf(swing) * 3.14159265f);
      translate(&m, -0.4f * sq, 0.2f * sinf(sqrtf(swing) * 6.2831853f), -0.2f * sinf(swing * 3.14159265f));
      first_person_item(&m, equip, swing);
    }
    if (block) {
      /* a block: twice the size, its model's first person transform none */
      scale(&m, 2, 2, 2);
    } else {
      /* an item: its model's first person transform (the rod's is turned the other way) */
      translate(&m, 0, 0.25f, 0.125f);
      rotate(&m, id == I_FISHING_ROD ? 45 : -135, 1);
      rotate(&m, 25, 2);
      scale(&m, 1.7f, 1.7f, 1.7f);
    }
    /* RenderItem.renderItem: half the size, centred */
    scale(&m, 0.5f, 0.5f, 0.5f);
    translate(&m, -0.5f, -0.5f, -0.5f);
    scale(&m, 1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
    if (block) {
      kind = H_BLOCK;
      nbox = blk_nbox[id];
      if (!nbox) {
        static const float cube[6] = {0, 0, 0, 16, 16, 16};
        memcpy(box[0], cube, sizeof cube), nbox = 1;
      } else
        for (int b = 0; b < nbox; b++)
          for (int c = 0; c < 6; c++) box[b][c] = blk_box[id][b][c];
    } else {
      kind = H_ITEM, nbox = 1;
      icon = id == I_FISHING_ROD && bobber() ? SP_FISHING_ROD_CAST : item_icon(id);   /* (the line out) */
      if (icon < 0) {
        kind = H_NONE;
        return;
      }
      static const float slab[6] = {0, 0, 7.5f, 16, 16, 8.5f};
      memcpy(box[0], slab, sizeof slab);
    }
  }
  /* the eye and the rays in the model: the inverse of the linear part */
  float (*a)[4] = m.m;
  float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
              a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
  if (fabsf(det) < 1e-12f) {
    kind = H_NONE;
    return;
  }
  float id_ = 1 / det, inv[3][3];
  inv[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * id_;
  inv[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id_;
  inv[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id_;
  inv[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * id_;
  inv[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id_;
  inv[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id_;
  inv[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * id_;
  inv[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id_;
  inv[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id_;
  for (int i = 0; i < 3; i++) {
    org[i] = -(inv[i][0] * a[0][3] + inv[i][1] * a[1][3] + inv[i][2] * a[2][3]);
    /* the ray of screen pixel (x, y): ((x + 0.5 - cx) / F, -(y + 0.5 - 120) / F, -1) */
    ddx[i] = inv[i][0] / FOCAL;
    ddy[i] = -inv[i][1] / FOCAL;
    dx0[i] = inv[i][0] * (0.5f - HAND_CX) / FOCAL - inv[i][1] * (0.5f - 120) / FOCAL - inv[i][2];
  }
  /* the lights: fixed in the world (turned by the view and the bobbing), ambient 0.4, each 0.6 */
  Mat lm = bob;
  rotate(&lm, pl.pitch, 0);
  rotate(&lm, pl.yaw, 1);
  static const float L[2][3] = {{0.2f, 1, -0.7f}, {-0.2f, 1, 0.7f}};
  float le[2][3];
  for (int l = 0; l < 2; l++) {
    float n = sqrtf(L[l][0] * L[l][0] + L[l][1] * L[l][1] + L[l][2] * L[l][2]);
    for (int i = 0; i < 3; i++) le[l][i] = (lm.m[i][0] * L[l][0] + lm.m[i][1] * L[l][1] + lm.m[i][2] * L[l][2]) / n;
  }
  int light = view_light();
  for (int f = 0; f < 6; f++) {
    /* the face's normal in the eye: a row of the inverse */
    float s = (f & 1) ? 1 : -1, n[3] = {inv[f >> 1][0] * s, inv[f >> 1][1] * s, inv[f >> 1][2] * s};
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    float k = 0.4f;
    for (int l = 0; l < 2; l++) {
      float d = (n[0] * le[l][0] + n[1] * le[l][1] + n[2] * le[l][2]) / len;
      if (d > 0) k += 0.6f * d;
    }
    if (k > 1) k = 1;
    face_sh[f] = (int)(k * light + 0.5f);
  }
  /* the faces the eye sees: outside the box on their side */
  nfaces = 0;
  for (int b = 0; b < nbox; b++)
    for (int f = 0; f < 6; f++) {
      int k = f >> 1;
      if ((f & 1) ? org[k] <= box[b][k + 3] : org[k] >= box[b][k]) continue;
      Face *fc = &faces[nfaces++];
      fc->b = (int8_t)b, fc->f = (int8_t)f, fc->near = false;
      fc->ymin = 1e9f, fc->ymax = -1e9f;
      int i = (k + 1) % 3, j = (k + 2) % 3;
      for (int c = 0; c < 4; c++) {
        float p[3], e[3];
        p[k] = box[b][k + ((f & 1) ? 3 : 0)];
        p[i] = box[b][i + ((c == 1 || c == 2) ? 3 : 0)];
        p[j] = box[b][j + (c >= 2 ? 3 : 0)];
        for (int r = 0; r < 3; r++) e[r] = a[r][0] * p[0] + a[r][1] * p[1] + a[r][2] * p[2] + a[r][3];
        if (e[2] > -0.01f) fc->near = true;   /* (up against the eye: every pixel is tried) */
        fc->sx[c] = HAND_CX + FOCAL * e[0] / -e[2], fc->sy[c] = 120 - FOCAL * e[1] / -e[2];
        if (fc->sy[c] < fc->ymin) fc->ymin = fc->sy[c];
        if (fc->sy[c] > fc->ymax) fc->ymax = fc->sy[c];
      }
      if (fc->near) fc->ymin = 0, fc->ymax = SCREEN_H;
    }
}

/* ---------------------------------------------------------------- drawing */
static inline uint16_t shade(uint16_t c, int s) {
  uint32_t rb = ((uint32_t)(c & 0xF81F) * (uint32_t)s >> 5) & 0xF81F, g = ((uint32_t)(c & 0x07E0) * (uint32_t)s >> 5) & 0x07E0;
  return (uint16_t)(rb | g);
}
static inline int sprite_texel(int id, int u, int v) {   /* -1: see-through */
  int w = spr_w[id], h = spr_h[id];
  u = u * w >> 4, v = v * h >> 4;
  const uint8_t *s = spr_px + spr_off[id] + v * ((w + 1) / 2);
  int i = (u & 1) ? s[u >> 1] >> 4 : s[u >> 1] & 15;
  return i ? spr_pal[id][i] : -1;
}
static inline int clamp15(float x) { int i = (int)x; return i < 0 ? 0 : i > 15 ? 15 : i; }   /* (below 0 is 0 anyway) */

/* a block face's texel at point p (Minecraft's default UVs) */
static int block_texel(int f, const float *p) {
  static const uint8_t tex_face[6] = {4, 5, 0, 1, 2, 3};   /* -x +x -y +y -z +z in blk_tex's order */
  float u, v;
  switch (f) {
    case 0: u = p[2], v = 16 - p[1]; break;
    case 1: u = 16 - p[2], v = 16 - p[1]; break;
    case 2: u = p[0], v = 16 - p[2]; break;
    case 3: u = p[0], v = p[2]; break;
    case 4: u = 16 - p[0], v = 16 - p[1]; break;
    default: u = p[0], v = 16 - p[1]; break;
  }
  int c = held_texel(model_id, tex_face[f], clamp15(u), clamp15(v));
  if (c < 0 && blk_model[model_id] == M_LEAVES && !opt.fancy) c = 0;   /* (fast leaves: black) */
  return c;
}

/* the arm's texel (ModelBox: the skin's 4 x 12 x 4 box unfolded, from (40, 16)) */
static int arm_texel(int f, const float *p) {
  float x = p[0] + 3, y = p[1] + 2, z = p[2] + 2, u, v;
  switch (f) {
    case 0: u = 4 - z, v = 4 + y; break;
    case 1: u = 8 + z, v = 4 + y; break;
    case 2: u = 4 + x, v = 4 - z; break;
    case 3: u = 8 + x, v = 4 - z; break;
    case 4: u = 4 + x, v = 4 + y; break;
    default: u = 16 - x, v = 4 + y; break;
  }
  return sprite_texel(SP_ARM, clamp15(u), clamp15(v));
}

/* an item: from where the ray enters its slab (point p, through face f, at t), through the texel
 * cells it crosses, a pixel thick, to the first solid one */
static int item_texel(const float *p, const float *d, int *f) {
  int cx = clamp15(p[0]), cy = clamp15(p[1]);
  int c = sprite_texel(icon, cx, 15 - cy);
  if (c >= 0) return c;
  /* see-through there: on through the slab */
  float ix = d[0] ? 1 / d[0] : 1e30f, iy = d[1] ? 1 / d[1] : 1e30f;
  float back = d[2] < 0 ? box[0][2] : box[0][5], tz = d[2] ? (back - p[2]) / d[2] : 1e30f;   /* (t from p) */
  int sx = d[0] > 0 ? 1 : -1, sy = d[1] > 0 ? 1 : -1;
  float tx = ((sx > 0 ? cx + 1 : cx) - p[0]) * ix, ty = ((sy > 0 ? cy + 1 : cy) - p[1]) * iy;
  float stx = fabsf(ix), sty = fabsf(iy);
  for (int n = 0; n < 40; n++) {
    if (tx < ty) {
      if (tx >= tz) return -1;
      cx += sx, tx += stx, *f = sx > 0 ? 0 : 1;
    } else {
      if (ty >= tz) return -1;
      cy += sy, ty += sty, *f = sy > 0 ? 2 : 3;
    }
    if ((unsigned)cx > 15 || (unsigned)cy > 15) return -1;
    c = sprite_texel(icon, cx, 15 - cy);
    if (c >= 0) return c;
  }
  return -1;
}

/* the pixels of a row a face covers: where the row's middle crosses its edges */
static bool face_span(const Face *fc, float yc, int *xa, int *xb) {
  if (fc->near) {
    *xa = 0, *xb = SCREEN_W;
    return true;
  }
  float lo = 1e9f, hi = -1e9f;
  for (int e = 0; e < 4; e++) {
    float x0 = fc->sx[e], y0 = fc->sy[e], x1 = fc->sx[(e + 1) & 3], y1 = fc->sy[(e + 1) & 3];
    if ((y0 <= yc) == (y1 <= yc)) continue;
    float x = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
    if (x < lo) lo = x;
    if (x > hi) hi = x;
  }
  if (lo > hi) return false;
  *xa = (int)lo - 1, *xb = (int)hi + 2;   /* (a pixel more each side: the test below decides) */
  if (*xa < 0) *xa = 0;
  if (*xb > SCREEN_W) *xb = SCREEN_W;
  return *xa < *xb;
}

void hand_strip(uint16_t *buf, int y0, int rows) {
  if (kind == H_NONE) return;
  static float zrow[SCREEN_W];   /* (several boxes: the nearest wins) */
  for (int y = y0; y < y0 + rows; y++) {
    uint16_t *row = buf + (y - y0) * SCREEN_W;
    float yc = y + 0.5f;
    if (nbox > 1)
      for (int x = 0; x < SCREEN_W; x++) zrow[x] = 1e30f;
    for (int n = 0; n < nfaces; n++) {
      const Face *fc = &faces[n];
      int xa, xb;
      if (yc < fc->ymin || yc > fc->ymax || !face_span(fc, yc, &xa, &xb)) continue;
      int f = fc->f, k = f >> 1, i = (k + 1) % 3, j = (k + 2) % 3;
      const float *bx = box[fc->b];
      float c = bx[k + ((f & 1) ? 3 : 0)], ck = c - org[k];
      float r[3] = {dx0[0] + ddy[0] * y, dx0[1] + ddy[1] * y, dx0[2] + ddy[2] * y};
      if (kind != H_ITEM && nbox == 1 && !fc->near) {
        /* Along the row the point on the face's plane is (A + B x) / (C + E x) on each axis
         * across it: its texel, and whether it is on the face, change only where it crosses a
         * whole pixel of the model (the boxes are whole pixels). So the row goes in runs of one
         * texel, each worked out once, the next crossing solved for. */
        float C = r[k], E = ddx[k];
        float Ai = org[i] * C + ck * r[i], Bi = org[i] * E + ck * ddx[i];
        float Aj = org[j] * C + ck * r[j], Bj = org[j] * E + ck * ddx[j];
        bool up_i = Bi * C - Ai * E > 0, up_j = Bj * C - Aj * E > 0;   /* (growing along the row) */
        for (int x = xa; x < xb;) {
          float D = C + E * x;
          int nx = x + 1;
          if (D != 0 && ck / D > 0) {
            float id = 1 / D, pi = (Ai + Bi * x) * id, pj = (Aj + Bj * x) * id;
            float ni = floorf(pi) + up_i, nj = floorf(pj) + up_j, di = Bi - ni * E, dj = Bj - nj * E;
            float xe = fminf(di != 0 ? (ni * C - Ai) / di : 1e9f, dj != 0 ? (nj * C - Aj) / dj : 1e9f);
            if (xe >= xb) nx = xb;
            else if (xe >= x + 1) nx = (int)xe + 1;
            if (pi >= bx[i] && pi <= bx[i + 3] && pj >= bx[j] && pj <= bx[j + 3]) {
              float p[3];
              p[k] = c, p[i] = pi, p[j] = pj;
              int col = kind == H_ARM ? arm_texel(f, p) : block_texel(f, p);
              if (col >= 0) {
                uint16_t s = shade((uint16_t)col, face_sh[f]);
                for (int q = x; q < nx; q++) row[q] = s;
              }
            }
          }
          x = nx;
        }
        continue;
      }
      for (int x = xa; x < xb; x++) {
        /* the ray meets the face's plane at t; inside the face? */
        float d[3] = {r[0] + ddx[0] * x, r[1] + ddx[1] * x, r[2] + ddx[2] * x};
        if (d[k] == 0) continue;
        float t = ck / d[k];
        if (t <= 0) continue;
        float p[3];
        p[k] = c, p[i] = org[i] + t * d[i], p[j] = org[j] + t * d[j];
        if (p[i] < bx[i] || p[i] > bx[i + 3] || p[j] < bx[j] || p[j] > bx[j + 3]) continue;
        if (nbox > 1 && t >= zrow[x]) continue;
        int ff = f, col = kind == H_ARM ? arm_texel(f, p) : kind == H_BLOCK ? block_texel(f, p) : item_texel(p, d, &ff);
        if (col < 0) continue;
        row[x] = shade((uint16_t)col, face_sh[ff]);
        if (nbox > 1) zrow[x] = t;
      }
    }
  }
}
