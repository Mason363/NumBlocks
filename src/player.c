/* The player: Minecraft 1.8.8's movement (EntityLiving.moveEntityWithHeading,
 * Entity.moveEntity), the block looked at, mining and placing.
 *
 * Everything runs in game ticks, 20 a second, with the game's numbers:
 * gravity 0.08 and drag 0.98 a tick, a jump of 0.42, ground friction
 * 0.6 x 0.91, walking at 0.1 (x 1.3 sprinting, x 0.3 sneaking), 0.02 in the
 * air, a 0.6 x 1.8 box that steps up 0.6, eyes at 1.62. */
#include <math.h>
#include "nb.h"
#pragma GCC optimize("Os")   /* (not where the time goes: small) */

Player pl;

#define W 0.3f   /* half width */
#define H 1.8f

static inline int ifloor(float v) { int i = (int)v; return v < (float)i ? i - 1 : i; }

static void box_of(float x, float y, float z, float *a) {
  a[0] = x - W, a[1] = y, a[2] = z - W, a[3] = x + W, a[4] = y + H, a[5] = z + W;
}

/* Entity.moveEntity: y first, then x and z; step up 0.6 if that goes further */
static bool hit_wall;   /* the last move ran into something sideways (isCollidedHorizontally) */
static void move(float dx, float dy, float dz) {
  float a[6];
  box_of(pl.x, pl.y, pl.z, a);
  float ody = dy, odx = dx, odz = dz;
  /* sneaking on the ground: no walking off edges */
  if (pl.on_ground && pl.sneaking) {
    float t[6];
    for (; dx != 0; dx = fabsf(dx) < 0.05f ? 0 : dx - (dx > 0 ? 0.05f : -0.05f)) {
      memcpy(t, a, sizeof t);
      t[0] += dx, t[3] += dx, t[1] -= 1, t[4] = t[1] + 1;
      if (phys_clip(t, 1, -0.01f) > -0.01f) break;   /* something under */
    }
    for (; dz != 0; dz = fabsf(dz) < 0.05f ? 0 : dz - (dz > 0 ? 0.05f : -0.05f)) {
      memcpy(t, a, sizeof t);
      t[2] += dz, t[5] += dz, t[1] -= 1, t[4] = t[1] + 1;
      if (phys_clip(t, 1, -0.01f) > -0.01f) break;
    }
    odx = dx, odz = dz;
  }
  float mdy = phys_clip(a, 1, dy);
  a[1] += mdy, a[4] += mdy;
  float mdx = phys_clip(a, 0, dx);
  a[0] += mdx, a[3] += mdx;
  float mdz = phys_clip(a, 2, dz);
  a[2] += mdz, a[5] += mdz;
  bool grounded = ody != mdy && ody < 0;
  if ((grounded || pl.on_ground) && (odx != mdx || odz != mdz)) {
    /* try again 0.6 higher */
    float s[6];
    box_of(pl.x, pl.y, pl.z, s);
    float up = phys_clip(s, 1, 0.6f);
    s[1] += up, s[4] += up;
    float sx = phys_clip(s, 0, odx);
    s[0] += sx, s[3] += sx;
    float sz = phys_clip(s, 2, odz);
    s[2] += sz, s[5] += sz;
    float down = phys_clip(s, 1, -up);
    s[1] += down, s[4] += down;
    if (sx * sx + sz * sz > mdx * mdx + mdz * mdz) {
      memcpy(a, s, sizeof s);
      mdx = sx, mdz = sz, mdy = up + down;
    }
  }
  pl.x = (a[0] + a[3]) / 2;
  pl.y = a[1];
  pl.z = (a[2] + a[5]) / 2;
  pl.on_ground = ody != mdy && ody < 0;
  hit_wall = odx != mdx || odz != mdz;
  if (ody != mdy) pl.vy = 0;
  if (odx != mdx) pl.vx = 0;
  if (odz != mdz) pl.vz = 0;
}

/* Entity.moveFlying */
static void move_flying(float strafe, float fwd, float f) {
  float d = strafe * strafe + fwd * fwd;
  if (d < 1e-4f) return;
  d = sqrtf(d);
  if (d < 1) d = 1;
  d = f / d;
  strafe *= d;
  fwd *= d;
  float y = pl.yaw * 0.017453292f, s = sinf(y), c = cosf(y);
  pl.vx += strafe * c - fwd * s;
  pl.vz += fwd * c + strafe * s;
}

/* World.isAnyLiquid: a liquid in any block the box touches */
static bool liquid_in(const float *a) {
  for (int y = ifloor(a[1]); y <= ifloor(a[4]); y++)
    for (int z = ifloor(a[2]); z <= ifloor(a[5]); z++)
      for (int x = ifloor(a[0]); x <= ifloor(a[3]); x++)
        if (blk_model[world_get(x, y, z)] == M_LIQUID) return true;
  return false;
}

static bool in_liquid(void) {
  int b = world_get(ifloor(pl.x), ifloor(pl.y + 0.4f), ifloor(pl.z));
  return blk_model[b] == M_LIQUID;
}

/* ---------------------------------------------------------------- health, food, experience */
static float armor_points(void) {
  int n = 0;
  for (int i = 0; i < 4; i++)
    if (pl.armor[i].id >= 256) n += it_a[pl.armor[i].id - 256];
  return (float)n;
}

void player_add_xp(int n) {
  pl.xp_total += n;
  while (n > 0) {
    /* EntityPlayer.xpBarCap */
    int cap = pl.xp_level >= 30 ? 112 + (pl.xp_level - 30) * 9 : pl.xp_level >= 15 ? 37 + (pl.xp_level - 15) * 5
                                                                                   : 7 + pl.xp_level * 2;
    float need = (1 - pl.xp) * cap;
    if (n >= need) {
      n -= (int)ceilf(need);
      pl.xp = 0;
      pl.xp_level++;
    } else {
      pl.xp += n / (float)cap;
      n = 0;
    }
  }
}

static void exhaust(float f) { pl.exhaustion += f; }

/* EntityLivingBase.attackEntityFrom and EntityPlayer.damageEntity: 10 ticks of
 * grace after a hit (only more damage gets through), armour takes 4% a point */
void player_hurt(float amount, int kind) {
  if (pl.dead || amount <= 0) return;
  if (pl.mode == 1 && kind != DMG_VOID) return;
  if (pl.invuln > 10) {
    if (amount <= pl.last_damage) return;
    float a = amount;
    amount -= pl.last_damage;
    pl.last_damage = a;
  } else {
    pl.last_damage = amount;
    pl.invuln = 20;
    pl.hurt_time = 10;
  }
  bool armored = kind == DMG_MOB || kind == DMG_ARROW || kind == DMG_EXPLOSION || kind == DMG_LAVA ||
                 kind == DMG_FIRE || kind == DMG_CACTUS || kind == DMG_GENERIC || kind == DMG_LIGHTNING;
  if (armored) {
    float ap = armor_points();
    if (ap > 0) {
      int wear = (int)(amount / 4);
      if (wear < 1) wear = 1;
      for (int i = 0; i < 4; i++)
        if (pl.armor[i].id) stack_wear(&pl.armor[i], wear);
      amount = amount * (25 - ap) / 25;
    }
  }
  exhaust(0.3f);
  pl.health -= amount;
  if (pl.health <= 0) {
    pl.health = 0;
    pl.dead = true;
    /* everything falls out (InventoryPlayer.dropAllItems), unless the game rule keeps it */
    if (rule(GR_KEEP_INVENTORY)) return;
    for (int i = 0; i < 36; i++)
      if (pl.inv[i].id) ent_drop(pl.inv[i].id, item_count(&pl.inv[i]), pl.inv[i].aux, pl.x, pl.y + 1.3f, pl.z, false);
    for (int i = 0; i < 4; i++)
      if (pl.armor[i].id) ent_drop(pl.armor[i].id, 1, pl.armor[i].aux, pl.x, pl.y + 1.3f, pl.z, false);
    memset(pl.inv, 0, sizeof pl.inv);
    memset(pl.armor, 0, sizeof pl.armor);
  }
}

/* FoodStats.onUpdate (and on Peaceful, EntityPlayer.onLivingUpdate's healing and feeding) */
extern uint32_t game_time;
static void food_tick(void) {
  if (opt.difficulty == 0) {
    if (pl.health < 20 && ticks_run % 20 == 0 && rule(GR_NATURAL_REGEN)) pl.health = pl.health + 1 > 20 ? 20 : pl.health + 1;
    if (pl.food < 20 && ticks_run % 10 == 0) pl.food++;
    return;
  }
  if (pl.exhaustion > 4) {
    pl.exhaustion -= 4;
    if (pl.sat > 0) pl.sat = pl.sat > 1 ? pl.sat - 1 : 0;
    else if (pl.food > 0) pl.food--;
  }
  if (pl.food >= 18 && pl.health > 0 && pl.health < 20 && rule(GR_NATURAL_REGEN)) {
    if (++pl.food_timer >= 80) {
      pl.health += 1;
      if (pl.health > 20) pl.health = 20;
      exhaust(3);
      pl.food_timer = 0;
    }
  } else if (pl.food <= 0) {
    if (++pl.food_timer >= 80) {
      /* starving: Easy stops at 5 hearts, Normal at half a heart, Hard kills */
      if (pl.health > 10 || opt.difficulty == 3 || (pl.health > 1 && opt.difficulty == 2)) player_hurt(1, DMG_STARVE);
      pl.food_timer = 0;
    }
  } else pl.food_timer = 0;
}

/* ---------------------------------------------------------------- blocks */
static const int8_t NX[6] = {0, 0, 0, 0, -1, 1}, NY[6] = {-1, 1, 0, 0, 0, 0}, NZ[6] = {0, 0, -1, 1, 0, 0};

static bool replaceable(int b) {
  return b == B_AIR || blk_model[b] == M_LIQUID || b == B_TALL_GRASS || b == B_FERN || b == B_DEAD_SHRUB ||
         b == B_DEAD_BUSH || b == B_VINE || b == B_SNOW_LAYER || b == B_DOUBLE_GRASS_LOWER || b == B_LARGE_FERN_LOWER ||
         b == B_FIRE;
}
static bool solid_top(int b) { return (blk_flags[b] & BF_OPAQUE) != 0 || b == B_GLASS || b == B_LEAVES_OAK; }
static bool soil(int b) { return b == B_GRASS || b == B_DIRT || b == B_PODZOL || b == B_COARSE_DIRT || b == B_FARMLAND || b == B_FARMLAND_WET; }
static bool is_plant(int b) { return blk_model[b] == M_CROSS && b != B_COBWEB && b != B_FIRE; }
static bool near_water(int x, int y, int z) {
  for (int f = 2; f < 6; f++) {
    int n = world_get(x + NX[f], y, z + NZ[f]);
    if (is_water(n)) return true;
  }
  return false;
}

/* can block b stay at (x, y, z) (Block.canBlockStay / canPlaceBlockAt)? */
static bool can_stay(int b, int x, int y, int z) {
  int below = world_get(x, y - 1, z);
  int m = blk_model[b];
  if (b >= B_WHEAT_0 && b <= B_WHEAT_7) return below == B_FARMLAND || below == B_FARMLAND_WET;
  if ((b >= B_CARROTS_0 && b <= B_CARROTS_3) || (b >= B_POTATOES_0 && b <= B_POTATOES_3))
    return below == B_FARMLAND || below == B_FARMLAND_WET;
  if (b == B_SUGAR_CANE)
    return below == B_SUGAR_CANE || ((below == B_GRASS || below == B_DIRT || below == B_SAND || below == B_RED_SAND) &&
                                     near_water(x, y - 1, z));
  if (b == B_CACTUS) {
    for (int f = 2; f < 6; f++)
      if (blk_flags[world_get(x + NX[f], y, z + NZ[f])] & BF_SOLID) return false;
    return below == B_CACTUS || below == B_SAND || below == B_RED_SAND;
  }
  if (b == B_DEAD_BUSH) return below == B_SAND || below == B_RED_SAND || below == B_HARDENED_CLAY ||
                               (below >= B_STAINED_CLAY_WHITE && below <= B_STAINED_CLAY_BLACK) || soil(below);
  if (b == B_BROWN_MUSHROOM || b == B_RED_MUSHROOM) return solid_top(below);
  if (b == B_LILY_PAD) return below == B_WATER;
  if (b >= B_SUNFLOWER_LOWER && b <= B_PEONY_UPPER && ((b - B_SUNFLOWER_LOWER) & 1))
    return world_get(x, y - 1, z) == b - 1;
  if (b == B_FIRE) return fire_can_stay(x, y, z);
  if (is_plant(b)) return soil(below);
  if (b == B_TORCH) return solid_top(below);
  if (b == B_TORCH_E) return blk_flags[world_get(x - 1, y, z)] & BF_OPAQUE;
  if (b == B_TORCH_W) return blk_flags[world_get(x + 1, y, z)] & BF_OPAQUE;
  if (b == B_TORCH_S) return blk_flags[world_get(x, y, z - 1)] & BF_OPAQUE;
  if (b == B_TORCH_N) return blk_flags[world_get(x, y, z + 1)] & BF_OPAQUE;
  if (b == B_SNOW_LAYER || b == B_RAIL) return solid_top(below);
  if (b == B_DOOR_OAK_UPPER) return is_door_lower(world_get(x, y - 1, z));
  if (is_door_lower(b)) return solid_top(below) && world_get(x, y + 1, z) == B_DOOR_OAK_UPPER;
  if (b == B_BED_FOOT || b == B_BED_HEAD) {
    int other = b == B_BED_FOOT ? B_BED_HEAD : B_BED_FOOT;
    for (int f = 2; f < 6; f++)
      if (world_get(x + NX[f], y, z + NZ[f]) == other) return true;
    return false;
  }
  if (m == M_LADDER) return true;
  return true;
}

static void break_block(int x, int y, int z, bool drops);
void break_block_at(int x, int y, int z, bool drops) { break_block(x, y, z, drops); }

/* a block changed at (x, y, z): what stood on it or hung on it may fall off */
void neighbours_changed(int x, int y, int z) {
  fluid_schedule(x, y, z);
  for (int f = 0; f < 6; f++) {
    int nx = x + NX[f], ny = y + NY[f], nz = z + NZ[f];
    int b = world_get(nx, ny, nz);
    if (blk_model[b] == M_LIQUID) fluid_schedule(nx, ny, nz);
    if (b != B_AIR && blk_model[b] != M_LIQUID && !can_stay(b, nx, ny, nz)) break_block(nx, ny, nz, true);
    /* sand and gravel fall (straight down, at once) */
    if ((b == B_SAND || b == B_RED_SAND || b == B_GRAVEL) && replaceable(world_get(nx, ny - 1, nz)) && ny > 0) {
      int yy = ny;
      while (yy > 0 && replaceable(world_get(nx, yy - 1, nz))) yy--;
      world_set(nx, ny, nz, B_AIR);
      world_set(nx, yy, nz, b);
      neighbours_changed(nx, ny, nz);
    }
  }
}

static int depth;
static void break_block(int x, int y, int z, bool drops) {
  int b = world_get(x, y, z);
  if (b == B_AIR || depth > 8) return;
  depth++;
  if (b == B_CHEST || b == B_FURNACE || b == B_FURNACE_LIT) tiles_removed(x, y, z);
  if (depth == 1 && blk_model[b] != M_LIQUID) particles_break(x, y, z, b);
  world_set(x, y, z, B_AIR);
  if (drops && pl.mode == 0 && rule(GR_TILE_DROPS)) {
    Stack out[2];
    int n = block_drops(b, held()->id, out);
    for (int i = 0; i < n; i++)
      ent_drop(out[i].id, out[i].aux, 0, x + 0.25f + rndf() * 0.5f, y + 0.25f + rndf() * 0.5f, z + 0.25f + rndf() * 0.5f,
               false);
  }
  /* the other half of a two-block thing goes too */
  if (b >= B_SUNFLOWER_LOWER && b <= B_PEONY_UPPER && !((b - B_SUNFLOWER_LOWER) & 1))
    if (world_get(x, y + 1, z) == b + 1) world_set(x, y + 1, z, B_AIR);
  if (b >= B_SUNFLOWER_LOWER && b <= B_PEONY_UPPER && ((b - B_SUNFLOWER_LOWER) & 1))
    if (world_get(x, y - 1, z) == b - 1) world_set(x, y - 1, z, B_AIR);
  if (is_door_lower(b) && world_get(x, y + 1, z) == B_DOOR_OAK_UPPER) world_set(x, y + 1, z, B_AIR);
  if (b == B_DOOR_OAK_UPPER && is_door_lower(world_get(x, y - 1, z))) {
    if (drops && pl.mode == 0 && rule(GR_TILE_DROPS)) ent_drop(I_WOODEN_DOOR, 1, 0, x + 0.5f, y - 0.5f, z + 0.5f, false);
    world_set(x, y - 1, z, B_AIR);
  }
  if (b == B_BED_FOOT || b == B_BED_HEAD)
    for (int f = 2; f < 6; f++) {
      int o = world_get(x + NX[f], y, z + NZ[f]);
      if (o == (b == B_BED_FOOT ? B_BED_HEAD : B_BED_FOOT)) {
        if (b == B_BED_HEAD && drops && pl.mode == 0) ent_drop(I_BED, 1, 0, x + 0.5f, y + 0.5f, z + 0.5f, false);
        world_set(x + NX[f], y, z + NZ[f], B_AIR);
        break;
      }
    }
  neighbours_changed(x, y, z);
  depth--;
}

static void set_block(int x, int y, int z, int b) {
  world_set(x, y, z, b);
  neighbours_changed(x, y, z);
}

/* ---------------------------------------------------------------- looking */
/* the eyes' ray, as the last frame drew it (EntityRenderer.getMouseOver runs each frame, and the
 * clicks of the next ticks use what it found): the crosshair is where it points */
float look_ray[6];
float hit_t;   /* how far along it the block looked at is */

/* the ray against a box: where it comes in, and through which face */
static bool ray_box(const float *o, const float *d, const float *lo, const float *hi, float *t, int *face) {
  float t0 = -1e9f, t1 = 1e9f;
  int f = -1;
  for (int a = 0; a < 3; a++) {
    if (fabsf(d[a]) < 1e-7f) {
      if (o[a] < lo[a] || o[a] > hi[a]) return false;
      continue;
    }
    float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
    int fa = a == 0 ? 4 : a == 1 ? 0 : 2;   /* (coming in through the low side) */
    if (ta > tb) {
      float s = ta;
      ta = tb, tb = s, fa++;
    }
    if (ta > t0) t0 = ta, f = fa;
    if (tb < t1) t1 = tb;
  }
  if (t0 > t1 || t1 < 0) return false;
  *t = t0, *face = f;
  return true;
}

/* where the ray meets block b at (x, y, z): its selection box (Block.collisionRayTrace), the steps
 * of stairs one by one (BlockStairs); a ray passing beside a flower or over a slab goes on */
static bool ray_block(int b, int x, int y, int z, float *t, int *face) {
  float o[3] = {look_ray[0] - x, look_ray[1] - y, look_ray[2] - z}, s[6];
  if (blk_model[b] == M_STAIRS) {
    int8_t bx[5][6];
    int n = block_boxes(b, x, y, z, bx);
    bool hit = false;
    for (int k = 0; k < n; k++) {
      float lo[3] = {bx[k][0] / 16.0f, bx[k][1] / 16.0f, bx[k][2] / 16.0f}, hi[3] = {bx[k][3] / 16.0f, bx[k][4] / 16.0f, bx[k][5] / 16.0f};
      float kt;
      int kf;
      if (ray_box(o, look_ray + 3, lo, hi, &kt, &kf) && (!hit || kt < *t)) *t = kt, *face = kf, hit = true;
    }
    return hit;
  }
  select_box(b, x, y, z, s);
  return ray_box(o, look_ray + 3, s, s + 3, t, face);
}

/* the first block the eyes' ray meets within reach (4.5, 5 in creative) */
static void pick_block(void) {
  float ex = look_ray[0], ey = look_ray[1], ez = look_ray[2], dx = look_ray[3], dy = look_ray[4], dz = look_ray[5];
  int x = ifloor(ex), y = ifloor(ey), z = ifloor(ez);
  int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
  float idx = dx != 0 ? fabsf(1 / dx) : 1e9f, idy = dy != 0 ? fabsf(1 / dy) : 1e9f, idz = dz != 0 ? fabsf(1 / dz) : 1e9f;
  float tx = dx > 0 ? (x + 1 - ex) * idx : (ex - x) * idx, ty = dy > 0 ? (y + 1 - ey) * idy : (ey - y) * idy,
        tz = dz > 0 ? (z + 1 - ez) * idz : (ez - z) * idz;
  int face = -1;
  float t = 0, reach = pl.mode ? 5.0f : 4.5f;
  pl.hit_face = -1;
  while (t <= reach) {
    int b = world_get(x, y, z);
    if (b != B_AIR && blk_model[b] != M_LIQUID && b != B_FIRE) {
      float bt;
      int bf;
      if (ray_block(b, x, y, z, &bt, &bf)) {
        if (bt > reach) return;
        if (bt <= t) bt = t, bf = face;   /* (the eyes inside it: the side they came in by) */
        pl.hit_x = x, pl.hit_y = y, pl.hit_z = z, pl.hit_face = bf < 0 ? 1 : bf, hit_t = bt;
        return;
      }
    }
    if (tx < ty && tx < tz) t = tx, tx += idx, x += sx, face = sx > 0 ? 4 : 5;
    else if (ty < tz) t = ty, ty += idy, y += sy, face = sy > 0 ? 0 : 1;
    else t = tz, tz += idz, z += sz, face = sz > 0 ? 2 : 3;
  }
}

/* what the crosshair is on (EntityRenderer.getMouseOver): a mob in front of the block takes it, so the
 * block has no outline; past 3 blocks, in survival, nothing is in reach */
static void pick(void) {
  pick_block();
  if (entity_looked_at(pl.mode ? 5.0f : 4.5f, pl.hit_face >= 0 ? hit_t : 1e9f)) pl.hit_face = -1;
}

void player_look(float ex, float ey, float ez, float yaw, float pitch) {
  float yr = yaw * 0.017453292f, pr = pitch * 0.017453292f;
  look_ray[0] = ex, look_ray[1] = ey, look_ray[2] = ez;
  look_ray[3] = -sinf(yr) * cosf(pr), look_ray[4] = -sinf(pr), look_ray[5] = cosf(yr) * cosf(pr);
  pick();
}

/* the liquid source the eyes look at (for buckets), or false */
static bool pick_liquid(int *ox, int *oy, int *oz) {
  float ex = look_ray[0], ey = look_ray[1], ez = look_ray[2], dx = look_ray[3], dy = look_ray[4], dz = look_ray[5];
  for (float t = 0; t < 4.5f; t += 0.1f) {
    int x = ifloor(ex + dx * t), y = ifloor(ey + dy * t), z = ifloor(ez + dz * t);
    int b = world_get(x, y, z);
    if (b == B_WATER || b == B_LAVA) {   /* sources only */
      *ox = x, *oy = y, *oz = z;
      return true;
    }
    if (b != B_AIR && blk_model[b] != M_LIQUID) return false;
  }
  return false;
}

/* ---------------------------------------------------------------- using items */
static bool box_free(int x, int y, int z) {
  float a[6];
  box_of(pl.x, pl.y, pl.z, a);
  return !(x + 1 > a[0] && x < a[3] && y + 1 > a[1] && y < a[4] && z + 1 > a[2] && z < a[5]);
}

static void use_up(void) {
  if (pl.mode == 0) stack_take(held(), 1);
}

/* places the held block (or the block an item places) against the face looked at */
static bool place(int b) {
  int f = pl.hit_face;
  int x = pl.hit_x, y = pl.hit_y, z = pl.hit_z;
  int at = world_get(x, y, z);
  if (!(replaceable(at) && at != B_AIR)) x += NX[f], y += NY[f], z += NZ[f];
  else f = 1;
  if (y < 0 || y >= WORLD_H || !world_loaded(x, y, z) || !replaceable(world_get(x, y, z))) return false;
  /* the variant: logs lie along the face's axis, torches lean on the block */
  if (b >= B_LOG_OAK && b <= B_LOG_JUNGLE_BARK && (b - B_LOG_OAK) % 4 == 0) b += (f >> 1) == 2 ? 1 : (f >> 1) == 1 ? 2 : 0;
  if (b >= B_LOG_ACACIA && b <= B_LOG_DARK_OAK_BARK && (b - B_LOG_ACACIA) % 4 == 0)
    b += (f >> 1) == 2 ? 1 : (f >> 1) == 1 ? 2 : 0;
  if (b == B_TORCH) {
    static const uint8_t tf[6] = {0, B_TORCH, B_TORCH_N, B_TORCH_S, B_TORCH_W, B_TORCH_E};
    if (f == 0) return false;
    b = tf[f];
  }
  /* the way the player looks: 0 south, 1 west, 2 north, 3 east */
  int way = (int)floorf(pl.yaw / 90 + 0.5f) & 3;
  if (b == B_OAK_STAIRS || b == B_COBBLESTONE_STAIRS) {
    /* BlockStairs: rising the way the player looks */
    static const int8_t off[4] = {2, 1, 3, 0};   /* _S, _W, _N, (east) */
    if (way != 3) b = (b == B_OAK_STAIRS ? B_OAK_STAIRS_W : B_COBBLESTONE_STAIRS_W) + 2 * (off[way] - 1);
  }
  if (b == B_DOOR_OAK_LOWER) {
    static const uint8_t door[4] = {B_DOOR_OAK_LOWER_S, B_DOOR_OAK_LOWER_W, B_DOOR_OAK_LOWER_N, B_DOOR_OAK_LOWER};
    b = door[way];
  }
  bool door = is_door_lower(b);
  if (!can_stay(b, x, y, z) && !door && b != B_BED_FOOT) return false;
  if ((blk_flags[b] & BF_SOLID) && !box_free(x, y, z)) return false;
  if (door || (b >= B_SUNFLOWER_LOWER && b <= B_PEONY_UPPER)) {
    if (!replaceable(world_get(x, y + 1, z)) || !solid_top(world_get(x, y - 1, z))) return false;
    if (!box_free(x, y + 1, z) && door) return false;
    world_set(x, y + 1, z, door ? B_DOOR_OAK_UPPER : b + 1);
  }
  if (b == B_BED_FOOT) {
    /* ItemBed: the head one block further, the way the player looks */
    static const int8_t wx[4] = {0, -1, 0, 1}, wz[4] = {1, 0, -1, 0};
    int hx = x + wx[way], hz = z + wz[way];
    if (!replaceable(world_get(hx, y, hz)) || !solid_top(world_get(x, y - 1, z)) || !solid_top(world_get(hx, y - 1, hz)) ||
        !box_free(hx, y, hz))
      return false;
    world_set(hx, y, hz, B_BED_HEAD);
  }
  set_block(x, y, z, b);
  return true;
}

/* EntityLivingBase.addPotionEffect: a stronger or longer one replaces it */
static void add_effect(int k, int ticks, int amp) {
  if (amp > pl.eff_amp[k] || (amp == pl.eff_amp[k] && ticks > pl.eff[k]) || !pl.eff[k])
    pl.eff[k] = (uint16_t)ticks, pl.eff_amp[k] = (uint8_t)amp;
}

/* PotionEffect.onUpdate, Potion.isReady and performEffect */
static void effects_tick(void) {
  for (int k = 0; k < 3; k++) {
    if (!pl.eff[k]) continue;
    int a = pl.eff_amp[k], d = pl.eff[k], n = (k == EF_POISON ? 25 : 50) >> a;
    bool ready = n <= 0 || d % n == 0;
    if (k == EF_POISON && ready && pl.health > 1) player_hurt(1, DMG_MAGIC);
    else if (k == EF_REGEN && ready && pl.health < 20) pl.health = pl.health + 1 > 20 ? 20 : pl.health + 1;
    else if (k == EF_HUNGER) exhaust(0.025f * (a + 1));
    pl.eff[k]--;
  }
}

static void eat_done(void) {
  Stack *h = held();
  int i = h->id - 256;
  if (h->id == I_MILK_BUCKET) {
    /* ItemBucketMilk: drunk, the effects are gone */
    memset(pl.eff, 0, sizeof pl.eff);
    if (pl.mode == 0) h->id = I_BUCKET, h->aux = 1;
    return;
  }
  pl.food += it_a[i];
  if (pl.food > 20) pl.food = 20;
  pl.sat += it_a[i] * it_b[i] / 10.0f * 2;
  if (pl.sat > pl.food) pl.sat = (float)pl.food;
  if (it_kind[i] == IK_STEW) {
    h->id = I_BOWL, h->aux = 1;
    return;
  }
  /* ItemFood.onFoodEaten, ItemFishFood, ItemAppleGold: their effects (and how likely) */
  switch (h->id) {
    case I_GOLDEN_APPLE: add_effect(EF_REGEN, 100, 1); break;
    case I_ROTTEN_FLESH: if (rndf() < 0.8f) add_effect(EF_HUNGER, 600, 0); break;
    case I_CHICKEN: if (rndf() < 0.3f) add_effect(EF_HUNGER, 600, 0); break;
    case I_SPIDER_EYE: add_effect(EF_POISON, 100, 0); break;
    case I_POISONOUS_POTATO: if (rndf() < 0.6f) add_effect(EF_POISON, 100, 0); break;
    case I_PUFFERFISH: add_effect(EF_POISON, 1200, 3), add_effect(EF_HUNGER, 300, 2); break;
  }
  use_up();
}

/* EntityPlayer.trySleep: at night (or in a storm), with no monster within 8 blocks; the bed is the new spawn point */
static void sleep_in(int x, int y, int z) {
  if (sky_sub() < 4) {
    gui_message("You can only sleep at night");
    return;
  }
  for (int i = 0; i < N_ENT; i++) {
    const Entity *e = &ents[i];
    if (e->type >= E_ZOMBIE && e->type <= E_SPIDER && fabsf(e->x - x) < 8 && fabsf(e->y - y) < 5 &&
        fabsf(e->z - z) < 8) {
      gui_message("You may not rest now, there are monsters nearby");
      return;
    }
  }
  pl.spawn_x = x, pl.spawn_y = y + 1, pl.spawn_z = z;
  pl.x = x + 0.5f, pl.y = y + 0.5625f, pl.z = z + 0.5f;
  pl.vx = pl.vy = pl.vz = 0;
  pl.sleep_timer = 1;
}

/* OK pressed: use the block looked at, else the held item */
static void use(uint32_t pressed) {
  Stack *h = held();
  int k = item_kind(h->id);
  bool on_block = pl.hit_face >= 0;
  int x = pl.hit_x, y = pl.hit_y, z = pl.hit_z;
  int b = on_block ? world_get(x, y, z) : B_AIR;
  if (!(pressed & K_USE)) return;
  if (on_block && !pl.sneaking) {
    if (b == B_CRAFTING_TABLE) {
      gui_open(GUI_CRAFTING, x, y, z);
      return;
    }
    if (b == B_FURNACE || b == B_FURNACE_LIT) {
      gui_open(GUI_FURNACE, x, y, z);
      return;
    }
    if (b == B_CHEST) {
      gui_open(GUI_CHEST, x, y, z);
      return;
    }
    if (is_door_lower(b) || b == B_DOOR_OAK_UPPER) {
      /* BlockDoor.onBlockActivated: open or close (the lower half keeps it) */
      int ly = b == B_DOOR_OAK_UPPER ? y - 1 : y, lb = world_get(x, ly, z);
      if (!is_door_lower(lb)) return;
      int st = lb == B_DOOR_OAK_LOWER ? 0 : lb - B_DOOR_OAK_LOWER_S + 1;   /* 0-3 closed, 4-7 open */
      st ^= 4;
      set_block(x, ly, z, st == 0 ? B_DOOR_OAK_LOWER : B_DOOR_OAK_LOWER_S + st - 1);
      player_swing();
      return;
    }
    if (b == B_BED_FOOT || b == B_BED_HEAD) {
      sleep_in(x, y, z);
      return;
    }
  }
  switch (k) {
    case IK_BLOCK:
      if (on_block && place(h->id)) use_up(), player_swing();
      return;
    case IK_PLACER:
    case IK_FOOD:
      if (on_block && h->id >= 256 && it_place[h->id - 256] != 255) {
        int pb = it_place[h->id - 256];
        if ((k == IK_FOOD || pb == B_WHEAT_0) && !(pl.hit_face == 1 && (b == B_FARMLAND || b == B_FARMLAND_WET))) {
          if (k == IK_PLACER) return;
        } else if (place(pb)) {
          use_up();
          return;
        }
      }
      return;
    case IK_HOE:
      if (on_block && pl.hit_face != 0 && (b == B_GRASS || b == B_DIRT) && world_get(x, y + 1, z) == B_AIR) {
        set_block(x, y, z, B_FARMLAND);
        if (pl.mode == 0) stack_wear(h, 1);
      }
      return;
    case IK_BONE_MEAL:
      if (!on_block) return;
      if (b >= B_WHEAT_0 && b < B_WHEAT_7) {
        int st = b - B_WHEAT_0 + 2 + rnd(4);
        set_block(x, y, z, B_WHEAT_0 + (st > 7 ? 7 : st));
        use_up();
      } else if ((b >= B_CARROTS_0 && b < B_CARROTS_3) || (b >= B_POTATOES_0 && b < B_POTATOES_3)) {
        int base = b <= B_CARROTS_3 ? B_CARROTS_0 : B_POTATOES_0;
        set_block(x, y, z, b + 1 > base + 3 ? base + 3 : b + 1);
        use_up();
      } else if (b >= B_SAPLING_OAK && b <= B_SAPLING_DARK_OAK) {
        /* ItemDye.applyBonemeal: 45% of a step each time */
        if (rndf() < 0.45f) sapling_grow(x, y, z);
        use_up();
      } else if (b == B_GRASS && world_get(x, y + 1, z) == B_AIR) {
        /* ItemDye.applyBonemeal on grass: tall grass and flowers around */
        for (int i = 0; i < 64; i++) {
          int gx = x + rnd(7) - 3, gz = z + rnd(7) - 3, gy = y + rnd(3) - 1;
          if (world_get(gx, gy, gz) == B_GRASS && world_get(gx, gy + 1, gz) == B_AIR)
            set_block(gx, gy + 1, gz, rnd(8) ? B_TALL_GRASS : rnd(2) ? B_DANDELION : B_POPPY);
        }
        use_up();
      }
      return;
    case IK_BUCKET: {
      int lx, ly, lz;
      if (pick_liquid(&lx, &ly, &lz)) {
        int lb = world_get(lx, ly, lz);
        set_block(lx, ly, lz, B_AIR);
        int full = lb == B_WATER ? I_WATER_BUCKET : I_LAVA_BUCKET;
        if (pl.mode == 1) return;
        if (h->aux <= 1) h->id = (uint16_t)full, h->aux = 1;
        else {
          h->aux--;
          if (inv_add(pl.inv, 36, full, 1, 0)) ent_drop(full, 1, 0, pl.x, pl.y + 1.3f, pl.z, true);
        }
      }
      return;
    }
    case IK_WATER_BUCKET:
    case IK_LAVA_BUCKET:
      if (on_block) {
        int f = pl.hit_face, px = x + NX[f], py = y + NY[f], pz = z + NZ[f];
        if (replaceable(world_get(px, py, pz))) {
          set_block(px, py, pz, k == IK_WATER_BUCKET ? B_WATER : B_LAVA);
          if (pl.mode == 0) h->id = I_BUCKET, h->aux = 1;
        }
      }
      return;
    case IK_HELMET: case IK_CHESTPLATE: case IK_LEGGINGS: case IK_BOOTS: {
      /* ItemArmor.onItemRightClick: worn at once if that slot is free */
      int slot = IK_BOOTS - k;
      if (!pl.armor[slot].id) pl.armor[slot] = *h, h->id = 0, h->aux = 0;
      return;
    }
    case IK_SNOWBALL: case IK_EGG:
      throw_item(h->id, 1.5f, true);
      use_up();
      player_swing();
      return;
    case IK_FISHING_ROD: {
      /* ItemFishingRod.onItemRightClick: cast, or reel in (which wears it: 1 for a catch,
       * 2 off the ground, 3 with a creature) */
      Entity *bob = bobber();
      if (!bob) fish_cast();
      else {
        int w = fish_reel(bob);
        if (pl.mode == 0 && w) stack_wear(h, w);
      }
      player_swing();
      return;
    }
    case IK_FLINT_AND_STEEL:
      if (!on_block) return;
      if (b == B_TNT && !pl.sneaking) {
        /* BlockTNT.onBlockActivated: lit, it falls and blows after 4 seconds */
        world_set(x, y, z, B_AIR);
        neighbours_changed(x, y, z);
        tnt_light(x, y, z, 80);
      } else {
        /* ItemFlintAndSteel.onItemUse: fire in the air against the face (BlockFire.onBlockAdded: out at once
         * where it cannot stay) */
        int f = pl.hit_face, fx = x + NX[f], fy = y + NY[f], fz = z + NZ[f];
        if (world_get(fx, fy, fz) == B_AIR && fy >= 0 && fy < WORLD_H) {
          if (fire_can_stay(fx, fy, fz)) fire_set(fx, fy, fz, 0);
        }
      }
      if (pl.mode == 0) stack_wear(h, 1);
      player_swing();
      return;
  }
}

/* ---------------------------------------------------------------- a tick */
/* Minecraft.middleClickMouse: the block looked at in the hand, from the hotbar if it is there; in
 * Creative, else into the first empty hotbar slot, or the one held (InventoryPlayer.setCurrentItem) */
void player_pick_block(void) {
  if (pl.hit_face < 0) return;
  int id = blk_item[world_get(pl.hit_x, pl.hit_y, pl.hit_z)];
  if (id == 0xFFFF) return;
  for (int i = 0; i < 9; i++)
    if (pl.inv[i].id == id) {
      pl.slot = i;
      return;
    }
  if (pl.mode != 1) return;
  for (int i = 0; i < 9; i++)
    if (!pl.inv[i].id) {
      pl.slot = i;
      break;
    }
  pl.inv[pl.slot] = (Stack){(uint16_t)id, 1};
}

void player_spawn(void) {
  int x, y, z;
  gen_spawn(&x, &y, &z);
  memset(&pl, 0, sizeof pl);
  pl.spawn_x = x, pl.spawn_y = y, pl.spawn_z = z;
  pl.x = x + 0.5f;
  pl.y = (float)y;
  pl.z = z + 0.5f;
  pl.health = 20;
  pl.food = 20;
  pl.sat = 5;
  pl.air = 300;
}

void player_respawn(void) {
  pl.x = pl.spawn_x + 0.5f, pl.y = (float)pl.spawn_y, pl.z = pl.spawn_z + 0.5f;
  pl.vx = pl.vy = pl.vz = 0;
  pl.health = 20, pl.food = 20, pl.sat = 5, pl.exhaustion = 0, pl.air = 300, pl.fall = 0, pl.fire = 0;
  if (!rule(GR_KEEP_INVENTORY)) pl.xp = 0, pl.xp_level = 0, pl.xp_total = 0;
  pl.dead = false;
  pl.pitch = 0;
}

static int break_cooldown, jump_timer, sprint_timer;
static bool was_fwd;   /* forward was held last tick */
static uint32_t prev_keys;

/* the damage the world does: falls, drowning, lava, fire, cactus, suffocation, the void */
static void hazards(float fell_from) {
  int fx = ifloor(pl.x), fz = ifloor(pl.z);
  int feet = world_get(fx, ifloor(pl.y + 0.1f), fz), head = world_get(fx, ifloor(pl.y + 1.62f), fz);
  /* falling: the distance counts while going down, out of water */
  if (pl.in_water || pl.flying || feet == B_LADDER || feet == B_VINE || feet == B_COBWEB) pl.fall = 0;
  else if (pl.y < fell_from) pl.fall += fell_from - pl.y;
  if (pl.on_ground) {
    if (pl.fall > 3) player_hurt(ceilf(pl.fall - 3), DMG_FALL);
    pl.fall = 0;
  }
  /* air: 300 ticks under water, then 2 damage every 20 */
  if (is_water(head)) {
    if (pl.mode == 0 && --pl.air <= -20) {
      pl.air = 0;
      player_hurt(2, DMG_DROWN);
    }
  } else pl.air = 300;
  bool lava = is_lava(feet) || is_lava(head);
  if (lava) {
    player_hurt(4, DMG_LAVA);
    pl.fire = 300;
  }
  /* in fire (Entity.moveEntity, World.isFlammableWithin): it hurts, and after a second sets the player alight */
  static int in_fire;
  bool fire = false;
  for (int y = ifloor(pl.y + 0.001f); y <= ifloor(pl.y + 1.799f) && !fire; y++)
    for (int z = ifloor(pl.z - 0.299f); z <= ifloor(pl.z + 0.299f) && !fire; z++)
      for (int x = ifloor(pl.x - 0.299f); x <= ifloor(pl.x + 0.299f) && !fire; x++) fire = world_get(x, y, z) == B_FIRE;
  if (fire) {
    player_hurt(1, DMG_FIRE);
    if (++in_fire >= 20 && pl.fire < 160) pl.fire = 160;
  } else
    in_fire = 0;
  if (pl.in_water || rain_at((int)floorf(pl.x), (int)floorf(pl.y), (int)floorf(pl.z)) ||
      rain_at((int)floorf(pl.x), (int)floorf(pl.y + 1.8f), (int)floorf(pl.z)))
    pl.fire = 0;   /* Entity.isWet */
  if (pl.fire > 0) {
    if (pl.fire % 20 == 0) player_hurt(1, DMG_FIRE);
    pl.fire--;
  }
  if ((blk_flags[head] & BF_OPAQUE) && pl.mode == 0) player_hurt(1, DMG_WALL);
  /* cactus: touching one hurts */
  for (int f = 2; f < 6; f++)
    if (world_get(ifloor(pl.x + NX[f] * 0.31f), ifloor(pl.y + 0.5f), ifloor(pl.z + NZ[f] * 0.31f)) == B_CACTUS)
      player_hurt(1, DMG_CACTUS);
  if (pl.y < -64) player_hurt(4, DMG_VOID);
}

void player_tick(uint32_t keys, uint32_t pressed) {
  if (pl.dead) return;
  if (pl.sleep_timer) {
    /* asleep: after 100 ticks the night is over (WorldServer.wakeAllPlayers); a key gets you up */
    if (++pl.sleep_timer >= 100) {
      game_time += 24000 - game_time % 24000;
      weather_clear();
      pl.sleep_timer = 0;
      pl.y += 0.5f;
    } else if (pressed & (K_USE | K_ATTACK | K_JUMP)) pl.sleep_timer = 0, pl.y += 0.5f;
    return;
  }
  if (pl.invuln > 0) pl.invuln--;
  if (pl.hurt_time > 0) pl.hurt_time--;
  float fwd = (keys & K_FWD ? 1.0f : 0) - (keys & K_BACKW ? 1.0f : 0);
  float strafe = (keys & K_STRAFE_L ? 1.0f : 0) - (keys & K_STRAFE_R ? 1.0f : 0);
  pl.sneaking = (keys & K_SNEAK) != 0 && !pl.flying;
  fwd *= 0.98f;
  strafe *= 0.98f;
  if (pl.sneaking) fwd *= 0.3f, strafe *= 0.3f;
  if (pl.using_ticks) fwd *= 0.2f, strafe *= 0.2f, sprint_timer = 0;   /* eating slows you down */
  /* EntityPlayerSP.onLivingUpdate: forward pressed twice within 7 ticks on the ground, or with the
   * sprint key, sprints until forward is let go, you walk into a wall, sneak or get hungry */
  bool fed = pl.food > 6 || pl.mode == 1, went = fwd >= 0.8f * 0.98f;
  if (sprint_timer) sprint_timer--;
  if (pl.on_ground && !was_fwd && went && !pl.sprinting && fed && !pl.using_ticks) {
    if (!sprint_timer && !(keys & K_SPRINT)) sprint_timer = 7;
    else pl.sprinting = true;
  }
  if (!pl.sprinting && went && fed && !pl.using_ticks && (keys & K_SPRINT)) pl.sprinting = true;
  if (pl.sprinting && (!went || hit_wall || !fed)) pl.sprinting = false;
  was_fwd = went;
  pl.in_water = in_liquid();
  /* creative: jump twice quickly to fly or land */
  if (jump_timer) jump_timer--;
  if (pressed & K_JUMP && pl.mode == 1) {
    if (jump_timer) {
      pl.flying = !pl.flying;
      jump_timer = 0;
    } else jump_timer = 7;
  }
  float y0 = pl.y, ox = pl.x, oz = pl.z;
  if (pl.flying) {
    /* PlayerCapabilities: flying speed 0.05, x2 sprinting; up with jump, down with sneak */
    if (keys & K_JUMP) pl.vy += 0.15f;
    if (keys & K_SNEAK) pl.vy -= 0.15f;
    move_flying(strafe, fwd, pl.sprinting ? 0.1f : 0.05f);
    move(pl.vx, pl.vy, pl.vz);
    pl.vx *= 0.91f, pl.vy *= 0.6f, pl.vz *= 0.91f;
    if (pl.on_ground) pl.flying = false;
  } else {
    if (keys & K_JUMP) {
      if (pl.in_water) pl.vy += 0.04f;
      else if (pl.on_ground) {
        pl.vy = 0.42f;
        exhaust(pl.sprinting ? 0.8f : 0.2f);
        if (pl.sprinting) {
          float y = pl.yaw * 0.017453292f;
          pl.vx -= sinf(y) * 0.2f;
          pl.vz += cosf(y) * 0.2f;
        }
      }
    }
    int at = world_get(ifloor(pl.x), ifloor(pl.y), ifloor(pl.z));
    if (pl.in_water) {
      move_flying(strafe, fwd, 0.02f);
      float bvx = pl.vx, bvz = pl.vz;
      move(pl.vx, pl.vy, pl.vz);
      bool wall = bvx != pl.vx || bvz != pl.vz;
      pl.vx *= 0.8f, pl.vy *= 0.8f, pl.vz *= 0.8f;
      pl.vy -= 0.02f;
      /* EntityLivingBase.moveEntityWithHeading: swimming into a wall with room above it
       * (nothing solid, no water) lifts you out, onto the bank */
      if (wall) {
        float a[6], dy = pl.vy + 0.6f - pl.y + y0;
        box_of(pl.x + pl.vx, pl.y + dy, pl.z + pl.vz, a);
        if (phys_free(a) && !liquid_in(a)) pl.vy = 0.3f;
      }
    } else {
      float fr = pl.on_ground ? 0.6f * 0.91f : 0.91f;
      float speed = 0.1f * (pl.sprinting ? 1.3f : 1.0f);
      float f = pl.on_ground ? speed * (0.16277136f / (fr * fr * fr)) : (pl.sprinting ? 0.026f : 0.02f);
      move_flying(strafe, fwd, f);
      /* ladders and vines: slow falls, climb when pushing into them */
      bool ladder = at == B_LADDER || at == B_VINE;
      if (ladder) {
        if (pl.vx < -0.15f) pl.vx = -0.15f;
        if (pl.vx > 0.15f) pl.vx = 0.15f;
        if (pl.vz < -0.15f) pl.vz = -0.15f;
        if (pl.vz > 0.15f) pl.vz = 0.15f;
        if (pl.vy < -0.15f) pl.vy = -0.15f;
        if (pl.sneaking && pl.vy < 0) pl.vy = 0;
      }
      if (at == B_COBWEB) pl.vx *= 0.25f, pl.vy *= 0.05f, pl.vz *= 0.25f;
      float bvx = pl.vx, bvz = pl.vz;
      move(pl.vx, pl.vy, pl.vz);
      if (ladder && (bvx != pl.vx || bvz != pl.vz)) pl.vy = 0.2f;
      pl.vy -= 0.08f;
      pl.vy *= 0.98f;
      pl.vx *= fr;
      pl.vz *= fr;
    }
  }
  /* walking, sprinting and swimming tire (EntityPlayer.addMovementStat) */
  float moved = sqrtf((pl.x - ox) * (pl.x - ox) + (pl.z - oz) * (pl.z - oz));
  pl.prev_walked = pl.walked, pl.prev_bob = pl.bob;
  pl.walked += moved * 0.6f;
  float want = pl.on_ground ? (moved > 0.1f ? 0.1f : moved) : 0;
  pl.bob += (want - pl.bob) * 0.4f;
  if (pl.mode == 0) exhaust(moved * (pl.in_water ? 0.015f : pl.sprinting ? 0.1f : 0.0f));
  hazards(y0);
  effects_tick();
  if (pl.mode == 0) food_tick();
  if (pl.dead) return;
  pick();
  /* a mob under the crosshair, nearer than the block: attacking hits it, using uses on it */
  Entity *target = entity_looked_at(pl.mode ? 5.0f : 3.0f, pl.hit_face >= 0 ? hit_t : 1e9f);
  if (target) {
    if (pressed & K_ATTACK) mob_attack(target), player_swing();
    if ((pressed & K_USE) && mob_use(target)) pressed &= ~K_USE;
    keys &= ~K_ATTACK;
    pl.breaking = 0;
  }
  /* mining (attack held) */
  if (break_cooldown) break_cooldown--;
  if ((pressed & K_ATTACK) && pl.hit_face < 0 && !target) player_swing();
  if ((pressed & K_ATTACK) && pl.hit_face >= 0 && !target) {
    int f = pl.hit_face, fx = pl.hit_x + NX[f], fy = pl.hit_y + NY[f], fz = pl.hit_z + NZ[f];
    if (world_get(fx, fy, fz) == B_FIRE) {
      world_set(fx, fy, fz, B_AIR);
      player_swing();
      keys &= ~K_ATTACK;
      pl.breaking = 0;
    }
  }
  /* PlayerControllerMP.onPlayerDamageBlock: looking at another block starts over */
  static int bx_ = -1, by_, bz_;
  if (pl.hit_x != bx_ || pl.hit_y != by_ || pl.hit_z != bz_) pl.breaking = 0, bx_ = pl.hit_x, by_ = pl.hit_y, bz_ = pl.hit_z;
  if ((keys & K_ATTACK) && pl.hit_face >= 0 && !break_cooldown) {
    player_swing();
    int b = world_get(pl.hit_x, pl.hit_y, pl.hit_z);
    int hard = blk_hard[b];
    if (pl.mode == 1) {
      break_block(pl.hit_x, pl.hit_y, pl.hit_z, false);
      break_cooldown = 5;
    } else if (hard == 255) pl.breaking = 0;
    else {
      /* Block.getPlayerRelativeBlockHardness: speed / hardness / 30, or / 100 without the right tool */
      int h = held()->id;
      float speed = dig_speed(b, h);
      if (pl.in_water && ifloor(pl.y + 1.62f) >= 0 && blk_model[world_get(ifloor(pl.x), ifloor(pl.y + 1.62f), ifloor(pl.z))] == M_LIQUID)
        speed /= 5;
      if (!pl.on_ground) speed /= 5;
      float per = hard ? speed * 20.0f / hard / (can_harvest(b, h) ? 30.0f : 100.0f) : 1.0f;
      pl.breaking += per;
      if (pl.breaking >= 1) {
        break_block(pl.hit_x, pl.hit_y, pl.hit_z, true);
        exhaust(0.025f);
        /* experience from ores (Block.dropXpOnBlockBreak) */
        if (b == B_COAL_ORE) player_add_xp(rnd(3));
        else if (b == B_DIAMOND_ORE || b == B_EMERALD_ORE) player_add_xp(3 + rnd(5));
        else if (b == B_LAPIS_ORE) player_add_xp(2 + rnd(4));
        else if (b == B_REDSTONE_ORE) player_add_xp(1 + rnd(5));
        /* tools wear: 1 a block (swords 2), not for blocks that break at once */
        int k = item_kind(h);
        if (hard && k >= IK_PICKAXE && k <= IK_SHEARS && k != IK_HOE) stack_wear(held(), k == IK_SWORD ? 2 : 1);
        pl.breaking = 0;
        break_cooldown = 5;
      }
    }
  } else pl.breaking = 0;
  /* a bow: OK held draws it, letting go shoots (ItemBow.onPlayerStoppedUsing) */
  int k = item_kind(held()->id);
  static int draw;
  if (k == IK_BOW) {
    bool arrows = pl.mode == 1;
    for (int i = 0; i < 36 && !arrows; i++) arrows = pl.inv[i].id == I_ARROW;
    if ((keys & K_USE) && arrows) draw++;
    else if (draw) {
      float f = draw / 20.0f;
      f = (f * f + f * 2) / 3;
      if (f > 1) f = 1;
      if (f >= 0.1f) {
        throw_item(I_ARROW, f * 3, true);
        if (pl.mode == 0) {
          stack_wear(held(), 1);
          for (int i = 0; i < 36; i++)
            if (pl.inv[i].id == I_ARROW) {
              stack_take(&pl.inv[i], 1);
              break;
            }
        }
      }
      draw = 0;
    }
    pl.using_ticks = draw;   /* (slows the player, as drawing a bow does) */
    prev_keys = keys;
    for (int n = 0; n < 9; n++)
      if (pressed & (K_SLOT1 << n)) pl.slot = n, draw = 0;
    return;
  }
  draw = 0;
  /* eating (and drinking milk): OK held for 32 ticks */
  bool edible = k == IK_FOOD || k == IK_STEW || k == IK_MILK_BUCKET;
  if (edible && (keys & K_USE) && (pl.food < 20 || held()->id == I_GOLDEN_APPLE || k == IK_MILK_BUCKET || pl.mode == 1) &&
      !(pl.hit_face >= 0 && it_place[held()->id - 256] != 255 &&
        world_get(pl.hit_x, pl.hit_y, pl.hit_z) == B_FARMLAND)) {
    if (++pl.using_ticks >= 32) {
      eat_done();
      pl.using_ticks = 0;
    }
  } else {
    pl.using_ticks = 0;
    use(pressed);
  }
  /* dropping the held item: xnt (one; with square root, the stack) */
  if (pressed & K_DROP && held()->id) {
    Stack *h = held();
    int n = pl.sneaking ? item_count(h) : 1;
    ent_drop(h->id, n, h->aux, pl.x, pl.y + 1.32f, pl.z, true);
    stack_take(h, n);
  }
  for (int n = 0; n < 9; n++)
    if (pressed & (K_SLOT1 << n)) pl.slot = n;
  prev_keys = keys;
}
