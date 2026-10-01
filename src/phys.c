/* Collisions with blocks, for the player and every entity (Entity.moveEntity:
 * the box is moved along y, then x, then z, as far as block boxes let it). */
#include "nb.h"

static inline int ifloor(float v) { int i = (int)v; return v < (float)i ? i - 1 : i; }

/* the boxes (sixteenths) block b at (x, y, z) is made of: its own (blocks.py),
 * or for ladders and vines the side they hang on, for fences, walls and panes
 * their post and an arm to each neighbour they join, for a door's upper half
 * the lower half's; 0 if a whole cube (or nothing) */
int block_boxes(int b, int x, int y, int z, int8_t (*o)[6]) {
  int m = blk_model[b];
  if (m == M_LADDER || m == M_VINE) {
    int th = m == M_VINE ? 1 : 2;
    static const int8_t at[4][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}};
    for (int k = 0; k < 4; k++)
      if (blk_flags[world_get(x + at[k][0], y, z + at[k][2])] & BF_OPAQUE) {
        int8_t *q = o[0];
        q[0] = 0, q[1] = 0, q[2] = 0, q[3] = 16, q[4] = 16, q[5] = 16;
        if (k == 0) q[2] = (int8_t)(16 - th);
        if (k == 1) q[5] = (int8_t)th;
        if (k == 2) q[0] = (int8_t)(16 - th);
        if (k == 3) q[3] = (int8_t)th;
        return 1;
      }
    memcpy(o[0], blk_box[b][0], 6);
    return 1;
  }
  if (m == M_FENCE || m == M_PANE) {
    bool wall = b == B_COBBLESTONE_WALL;
    int p0 = m == M_PANE ? 7 : wall ? 4 : 6, p1 = 16 - p0, a0 = m == M_PANE ? 7 : wall ? 5 : 7, a1 = 16 - a0;
    int ay0 = m == M_PANE ? 0 : 6, ay1 = m == M_PANE ? 16 : wall ? 13 : 15;
    int n = 0;
    int8_t *q = o[n++];
    q[0] = (int8_t)p0, q[1] = 0, q[2] = (int8_t)p0, q[3] = (int8_t)p1, q[4] = 16, q[5] = (int8_t)p1;
    static const int8_t dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int k = 0; k < 4; k++) {
      int nb = world_get(x + dir[k][0], y, z + dir[k][1]);
      if (!(blk_model[nb] == m || (blk_flags[nb] & BF_OPAQUE))) continue;
      q = o[n++];
      q[1] = (int8_t)ay0, q[4] = (int8_t)ay1;
      if (dir[k][0]) q[0] = (int8_t)(dir[k][0] > 0 ? p1 : 0), q[3] = (int8_t)(dir[k][0] > 0 ? 16 : p0), q[2] = (int8_t)a0,
                     q[5] = (int8_t)a1;
      else q[2] = (int8_t)(dir[k][1] > 0 ? p1 : 0), q[5] = (int8_t)(dir[k][1] > 0 ? 16 : p0), q[0] = (int8_t)a0,
           q[3] = (int8_t)a1;
    }
    return n;
  }
  if (b == B_DOOR_OAK_UPPER) {
    int lo = world_get(x, y - 1, z);
    if (blk_model[lo] == M_DOOR && lo != B_DOOR_OAK_UPPER) b = lo;
  }
  int n = blk_nbox[b];
  memcpy(o, blk_box[b], (size_t)n * 6);
  return n;
}

/* the selection box of block b (Block.getSelectedBoundingBox, from its corner): its shape's bounds;
 * plants as BlockBush and its kin */
void select_box(int b, int x, int y, int z, float *o) {
  int8_t bx[5][6];
  int n = block_boxes(b, x, y, z, bx);
  float lo[3] = {0, 0, 0}, hi[3] = {16, 16, 16};
  if (n) {
    for (int i = 0; i < 3; i++) lo[i] = 16, hi[i] = 0;
    for (int k = 0; k < n; k++)
      for (int i = 0; i < 3; i++) {
        if (bx[k][i] < lo[i]) lo[i] = bx[k][i];
        if (bx[k][i + 3] > hi[i]) hi[i] = bx[k][i + 3];
      }
  } else if (blk_model[b] == M_CROSS) {
    float r = 6.4f, h = 12.8f;   /* tall grass, ferns, saplings, dead bushes: 0.4 around, 0.8 high */
    if (b >= B_DANDELION && b <= B_OXEYE_DAISY) r = 3.2f, h = 9.6f;                       /* flowers */
    else if (b == B_BROWN_MUSHROOM || b == B_RED_MUSHROOM) r = 3.2f, h = 6.4f;           /* mushrooms */
    else if (b == B_SUGAR_CANE) r = 6, h = 16;
    else if ((b >= B_WHEAT_0 && b <= B_WHEAT_7) || (b >= B_CARROTS_0 && b <= B_CARROTS_3) ||
             (b >= B_POTATOES_0 && b <= B_POTATOES_3))
      r = 8, h = 4;   /* crops */
    lo[0] = lo[2] = 8 - r, hi[0] = hi[2] = 8 + r, hi[1] = h;
  }
  for (int i = 0; i < 3; i++) o[i] = lo[i] / 16, o[i + 3] = hi[i] / 16;
}

/* the collision boxes of the block at (x, y, z), in world coordinates (b: up to 5 x 6) */
static int block_cboxes(int x, int y, int z, float *b) {
  int s = world_get(x, y, z);
  if (!(blk_flags[s] & BF_SOLID)) return 0;
  int8_t bx[5][6];
  int n = block_boxes(s, x, y, z, bx);
  if (!n) {
    b[0] = (float)x, b[1] = (float)y, b[2] = (float)z, b[3] = x + 1.0f, b[4] = y + 1.0f, b[5] = z + 1.0f;
    return 1;
  }
  for (int k = 0; k < n; k++) {
    float *q = b + k * 6;
    q[0] = x + bx[k][0] / 16.0f, q[1] = y + bx[k][1] / 16.0f, q[2] = z + bx[k][2] / 16.0f;
    q[3] = x + bx[k][3] / 16.0f, q[4] = y + bx[k][4] / 16.0f, q[5] = z + bx[k][5] / 16.0f;
    /* fences and walls are 1.5 blocks high to jump over (BlockFence) */
    if (blk_model[s] == M_FENCE) q[4] = y + 1.5f;
  }
  return n;
}

/* the block's first collision box (for "is something solid here"); false if none */
bool block_box(int x, int y, int z, float *b) {
  float all[30];
  int n = block_cboxes(x, y, z, all);
  if (!n) return false;
  memcpy(b, all, 6 * sizeof(float));
  for (int k = 1; k < n; k++) {
    for (int i = 0; i < 3; i++) if (all[k * 6 + i] < b[i]) b[i] = all[k * 6 + i];
    for (int i = 3; i < 6; i++) if (all[k * 6 + i] > b[i]) b[i] = all[k * 6 + i];
  }
  return true;
}

/* how far the box `a` can move along `axis` by `d` before touching block boxes */
float phys_clip(const float *a, int axis, float d) {
  int x0 = ifloor(a[0] - (axis == 0 && d < 0 ? -d : 0)) - 1, x1 = ifloor(a[3] + (axis == 0 && d > 0 ? d : 0)) + 1;
  int y0 = ifloor(a[1] - (axis == 1 && d < 0 ? -d : 0)) - 1, y1 = ifloor(a[4] + (axis == 1 && d > 0 ? d : 0)) + 1;
  int z0 = ifloor(a[2] - (axis == 2 && d < 0 ? -d : 0)) - 1, z1 = ifloor(a[5] + (axis == 2 && d > 0 ? d : 0)) + 1;
  for (int y = y0; y <= y1; y++)
    for (int z = z0; z <= z1; z++)
      for (int x = x0; x <= x1; x++) {
        float bs[30];
        int nb = block_cboxes(x, y, z, bs);
        for (int q = 0; q < nb; q++) {
          const float *b = bs + q * 6;
          /* overlapping on the other two axes? */
          bool ov = true;
          for (int k = 0; k < 3; k++)
            if (k != axis && (a[k + 3] <= b[k] || a[k] >= b[k + 3])) ov = false;
          if (!ov) continue;
          if (d > 0 && a[axis + 3] <= b[axis]) {
            float m = b[axis] - a[axis + 3];
            if (m < d) d = m;
          } else if (d < 0 && a[axis] >= b[axis + 3]) {
            float m = b[axis + 3] - a[axis];
            if (m > d) d = m;
          }
        }
      }
  return d;
}

/* nothing solid overlaps the box a (World.getCollidingBoundingBoxes is empty) */
bool phys_free(const float *a) {
  for (int y = ifloor(a[1]) - 1; y <= ifloor(a[4]); y++)
    for (int z = ifloor(a[2]); z <= ifloor(a[5]); z++)
      for (int x = ifloor(a[0]); x <= ifloor(a[3]); x++) {
        float bs[30];
        int nb = block_cboxes(x, y, z, bs);
        for (int q = 0; q < nb; q++) {
          const float *b = bs + q * 6;
          if (a[0] < b[3] && a[3] > b[0] && a[1] < b[4] && a[4] > b[1] && a[2] < b[5] && a[5] > b[2]) return false;
        }
      }
  return true;
}

/* moves a box (w wide, h high, feet at p) by v; stops on blocks, zeroes the
 * velocity along blocked axes; true if it landed on something */
bool phys_move(float *p, float *v, float w, float h) {
  float a[6] = {p[0] - w / 2, p[1], p[2] - w / 2, p[0] + w / 2, p[1] + h, p[2] + w / 2};
  float dy = phys_clip(a, 1, v[1]);
  a[1] += dy, a[4] += dy;
  float dx = phys_clip(a, 0, v[0]);
  a[0] += dx, a[3] += dx;
  float dz = phys_clip(a, 2, v[2]);
  a[2] += dz, a[5] += dz;
  bool ground = dy != v[1] && v[1] < 0;
  if (dy != v[1]) v[1] = 0;
  if (dx != v[0]) v[0] = 0;
  if (dz != v[2]) v[2] = 0;
  p[0] = (a[0] + a[3]) / 2, p[1] = a[1], p[2] = (a[2] + a[5]) / 2;
  return ground;
}
