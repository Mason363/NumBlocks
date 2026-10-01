/* The world's own changes: liquids flowing (BlockDynamicLiquid), random
 * block ticks (WorldServer.updateBlocks: 3 random blocks a 16 x 16 x 16
 * section each tick): crops and saplings grow, grass spreads and dies,
 * leaves far from a log decay, sugar cane and cactus grow; and the weather
 * (World.updateWeather): rain, thunder, snow piling up, water freezing; and
 * fire (BlockFire): it spreads to what burns and burns it away. */
#include <math.h>
#include <stdlib.h>
#include "nb.h"

static const int8_t NX[6] = {0, 0, 0, 0, -1, 1}, NY[6] = {-1, 1, 0, 0, 0, 0}, NZ[6] = {0, 0, -1, 1, 0, 0};
extern uint32_t game_time;

/* ---------------------------------------------------------------- liquids */
typedef struct {
  int16_t x, z;
  uint8_t y, age;   /* (age: a fire's, 0 to 15: its block state's in Minecraft) */
  uint16_t due;     /* the tick it runs (low 16 bits of ticks_run) */
} Pending;
#define N_PENDING 160
static Pending pend[N_PENDING];
static int npend;

void fluid_schedule(int x, int y, int z) {
  int b = world_get(x, y, z);
  if (blk_model[b] != M_LIQUID) return;
  for (int i = 0; i < npend; i++)
    if (pend[i].x == x && pend[i].y == y && pend[i].z == z) return;
  if (npend >= N_PENDING) return;
  /* water moves every 5 ticks, lava every 30 */
  pend[npend++] = (Pending){(int16_t)x, (int16_t)z, (uint8_t)y, 0, (uint16_t)(ticks_run + (is_lava(b) ? 30 : 5))};
}

static int level_of(int b) { return blk_meta[b]; }   /* 0 source, 1-7 flowing, 8 falling */
static int liquid(int water, int level) {
  if (water) {
    if (level == 0) return B_WATER;
    if (level >= 8) return B_FALLING_WATER;
    return level == 1 ? B_FLOWING_WATER : B_FLOWING_WATER_2 + level - 2;
  }
  if (level == 0) return B_LAVA;
  if (level >= 8) return B_FALLING_LAVA;
  return level <= 2 ? B_FLOWING_LAVA : level <= 4 ? B_FLOWING_LAVA_4 : B_FLOWING_LAVA_6;
}
static bool same(int water, int b) { return water ? is_water(b) : is_lava(b); }

/* can the liquid flow into this block (BlockDynamicLiquid.canFlowInto)? */
static bool open_to(int water, int b) {
  if (same(water, b)) return false;
  if (water && is_lava(b)) return false;   /* water never flows into lava: the lava turns to stone */
  if (!water && is_water(b)) return true;
  if (b == B_AIR) return true;
  if (blk_flags[b] & BF_SOLID) return false;
  /* plants, torches, snow... are washed away (doors, ladders and signs stop it) */
  int m = blk_model[b];
  return m == M_CROSS || m == M_TORCH || m == M_FLAT || m == M_LAYER || m == M_VINE;   /* (and fire) */
}

static void set(int x, int y, int z, int b) {
  world_set(x, y, z, b);
  neighbours_changed(x, y, z);
}

/* lava touching water (BlockLiquid.checkForMixing): obsidian from a source, cobblestone from flowing */
static bool mix(int x, int y, int z, int b) {
  if (!is_lava(b)) return false;
  for (int f = 1; f < 6; f++)
    if (is_water(world_get(x + NX[f], y + NY[f], z + NZ[f]))) {
      int lv = level_of(b);
      if (lv == 0) set(x, y, z, B_OBSIDIAN);
      else if (lv <= 4) set(x, y, z, B_COBBLESTONE);
      else continue;
      return true;
    }
  return false;
}

static void flow_into(int water, int x, int y, int z, int level) {
  int b = world_get(x, y, z);
  if (!open_to(water, b)) return;
  if (!water && is_water(b)) {
    set(x, y, z, B_STONE);   /* lava flowing onto water */
    return;
  }
  if (b != B_AIR && !(water ? is_lava(b) : is_water(b))) break_block_at(x, y, z, true);
  set(x, y, z, liquid(water, level));
}

/* how far (up to 4 for water, 2 for lava) a way down is going this way, 1000 if none */
static int slope_dist(int water, int x, int y, int z, int dist, int from, int max) {
  int best = 1000;
  for (int f = 2; f < 6; f++) {
    if (f == (from ^ 1)) continue;
    int nx = x + NX[f], nz = z + NZ[f];
    int b = world_get(nx, y, nz);
    if ((blk_flags[b] & BF_SOLID) || (same(water, b) && level_of(b) == 0)) continue;
    if (open_to(water, world_get(nx, y - 1, nz)) || same(water, world_get(nx, y - 1, nz))) return dist;
    if (dist < max) {
      int d = slope_dist(water, nx, y, nz, dist + 1, f, max);
      if (d < best) best = d;
    }
  }
  return best;
}

static void fluid_update(int x, int y, int z) {
  int b = world_get(x, y, z);
  if (blk_model[b] != M_LIQUID) return;
  int water = is_water(b);
  if (mix(x, y, z, b)) return;
  int level = level_of(b), decay = water ? 1 : 2;
  if (level > 0) {
    /* the level it should have: from the lowest neighbour, or falling from above */
    int lowest = -1, sources = 0;
    for (int f = 2; f < 6; f++) {
      int n = world_get(x + NX[f], y, z + NZ[f]);
      if (!same(water, n)) continue;
      int l = level_of(n);
      if (l == 0) sources++;
      if (l >= 8) l = 0;
      if (lowest < 0 || l < lowest) lowest = l;
    }
    int nl = lowest < 0 ? -1 : lowest + decay;
    if (nl >= 8) nl = -1;
    int above = world_get(x, y + 1, z);
    if (same(water, above)) nl = 8;   /* falling */
    /* two sources beside it and something under: a new source (infinite water) */
    if (water && sources >= 2) {
      int under = world_get(x, y - 1, z);
      if ((blk_flags[under] & BF_SOLID) || (is_water(under) && level_of(under) == 0)) nl = 0;
    }
    if (nl != level) {
      if (nl < 0) {
        set(x, y, z, B_AIR);
        return;
      }
      level = nl;
      set(x, y, z, liquid(water, level));
    }
  }
  /* down first, then sideways towards the nearest way down */
  int under = world_get(x, y - 1, z);
  if (y > 0 && open_to(water, under)) {
    if (!water && is_water(under)) set(x, y - 1, z, B_STONE);
    else flow_into(water, x, y - 1, z, 8);
    return;
  }
  if (level == 0 || (!same(water, under) && !open_to(water, under))) {
    int next = level >= 8 ? decay : level + decay;
    if (next >= 8) return;
    int dist[4], best = 1000, max = water ? 4 : 2;
    for (int f = 2; f < 6; f++) {
      int nx = x + NX[f], nz = z + NZ[f];
      int n = world_get(nx, y, nz);
      dist[f - 2] = 1000;
      if ((blk_flags[n] & BF_SOLID) || (same(water, n) && level_of(n) == 0)) continue;
      dist[f - 2] = open_to(water, world_get(nx, y - 1, nz)) || same(water, world_get(nx, y - 1, nz))
                        ? 0
                        : slope_dist(water, nx, y, nz, 1, f, max);
      if (dist[f - 2] < best) best = dist[f - 2];
    }
    for (int f = 2; f < 6; f++)
      if (dist[f - 2] == best || best == 1000) flow_into(water, x + NX[f], y, z + NZ[f], next);
  }
}

static void fire_update(int x, int y, int z, int age);
static void fluids_tick(void) {
  uint16_t now = (uint16_t)ticks_run;
  int done = 0;
  for (int i = 0; i < npend && done < 24;) {
    if ((int16_t)(now - pend[i].due) < 0) {
      i++;
      continue;
    }
    Pending p = pend[i];
    pend[i] = pend[--npend];
    if (!world_loaded(p.x, p.y, p.z)) continue;
    if (world_get(p.x, p.y, p.z) == B_FIRE) fire_update(p.x, p.y, p.z, p.age);
    else fluid_update(p.x, p.y, p.z);
    done++;
  }
}

/* ---------------------------------------------------------------- fire */
/* Blocks.fire.setFireInfo: how readily a block sets fire to the air by it, and burns away (0: never) */
static int fire_enc(int b) {
  switch (blk_id[b]) {
    case 5: case 125: case 126: case 85: case 107: case 53: case 134: case 135: case 136: case 163: case 164:
    case 17: case 162: case 173: return 5;
    case 18: case 161: case 47: case 35: return 30;
    case 46: case 106: return 15;
    case 31: case 175: case 37: case 38: case 32: case 170: case 171: return 60;
  }
  return 0;
}
static int fire_flam(int b) {
  switch (blk_id[b]) {
    case 5: case 125: case 126: case 85: case 107: case 53: case 134: case 135: case 136: case 163: case 164:
    case 47: case 170: case 171: return 20;
    case 17: case 162: case 173: return 5;
    case 18: case 161: case 35: return 60;
    case 46: case 106: case 31: case 175: case 37: case 38: case 32: return 100;
  }
  return 0;
}
static int fire_near(int x, int y, int z) {   /* BlockFire.getNeighborEncouragement: the most of the six */
  int m = 0;
  for (int f = 0; f < 6; f++) {
    int e = fire_enc(world_get(x + NX[f], y + NY[f], z + NZ[f]));
    if (e > m) m = e;
  }
  return m;
}
/* BlockFire.canPlaceBlockAt: on a solid top, or by something that burns */
bool fire_can_stay(int x, int y, int z) {
  return (blk_flags[world_get(x, y - 1, z)] & BF_OPAQUE) || fire_near(x, y, z) > 0;
}
/* BlockFire.canDie: rain falls on it or by it */
static bool rained_on(int x, int y, int z) {
  return rain_at(x, y, z) || rain_at(x - 1, y, z) || rain_at(x + 1, y, z) || rain_at(x, y, z - 1) || rain_at(x, y, z + 1);
}
static void fire_schedule(int x, int y, int z, int age) {
  for (int i = 0; i < npend; i++)
    if (pend[i].x == x && pend[i].y == y && pend[i].z == z) return;
  if (npend >= N_PENDING) return;
  /* BlockFire.tickRate: 30, and up to 10 more */
  pend[npend++] = (Pending){(int16_t)x, (int16_t)z, (uint8_t)y, (uint8_t)age, (uint16_t)(ticks_run + 30 + rnd(10))};
}
void fire_set(int x, int y, int z, int age) {
  world_set(x, y, z, B_FIRE);
  fire_schedule(x, y, z, age);
}
/* BlockFire.catchOnFire: what burns may burn away (or catch, the fire young enough); TNT is lit */
static void catch_fire(int x, int y, int z, int chance, int age) {
  int b = world_get(x, y, z);
  if (rnd(chance) >= fire_flam(b)) return;
  if (rnd(age + 10) < 5 && !rain_at(x, y, z)) {
    int a = age + rnd(5) / 4;
    fire_set(x, y, z, a > 15 ? 15 : a);
  } else
    world_set(x, y, z, B_AIR);
  neighbours_changed(x, y, z);
  if (b == B_TNT) tnt_light(x, y, z, 80);
}
/* BlockFire.updateTick */
static void fire_update(int x, int y, int z, int age) {
  if (!rule(GR_FIRE_TICK)) return;
  bool raining = rain_str > 0.2f;
  if (!fire_can_stay(x, y, z) || (raining && rained_on(x, y, z) && rndf() < 0.2f + age * 0.03f)) {
    world_set(x, y, z, B_AIR);
    return;
  }
  if (age < 15) age += rnd(3) / 2;
  fire_schedule(x, y, z, age);
  if (!fire_near(x, y, z)) {
    /* nothing to burn: out, soon (at once off a solid top) */
    if (!(blk_flags[world_get(x, y - 1, z)] & BF_OPAQUE) || age > 3) world_set(x, y, z, B_AIR);
    return;
  }
  if (!fire_enc(world_get(x, y - 1, z)) && age == 15 && rnd(4) == 0) {
    world_set(x, y, z, B_AIR);
    return;
  }
  for (int f = 0; f < 6; f++) catch_fire(x + NX[f], y + NY[f], z + NZ[f], f < 2 ? 250 : 300, age);
  /* the air about it, above more than below: where it is next to something that burns */
  for (int dy = -1; dy <= 4; dy++)
    for (int dz = -1; dz <= 1; dz++)
      for (int dx = -1; dx <= 1; dx++) {
        if (!dx && !dy && !dz) continue;
        int nx = x + dx, ny = y + dy, nz = z + dz;
        if (world_get(nx, ny, nz) != B_AIR || !world_loaded(nx, ny, nz)) continue;
        int e = fire_near(nx, ny, nz);
        if (!e) continue;
        int k = (e + 40 + opt.difficulty * 7) / (age + 30);
        if (k > 0 && rnd(dy > 1 ? dy * 100 : 100) <= k && !(raining && rained_on(nx, ny, nz))) {
          int a = age + rnd(5) / 4;
          fire_set(nx, ny, nz, a > 15 ? 15 : a);
        }
      }
}
/* BlockStaticLiquid.updateTick: lava sets fire to what is about it, now and then */
static void lava_fire(int x, int y, int z) {
  int n = rnd(3);
  if (n > 0) {
    for (int j = 0; j < n; j++) {
      x += rnd(3) - 1, y++, z += rnd(3) - 1;
      int b = world_get(x, y, z);
      if (b == B_AIR) {
        if (fire_near(x, y, z) || fire_flam(world_get(x, y - 1, z))) {
          fire_set(x, y, z, 0);
          return;
        }
      } else if (blk_flags[b] & BF_SOLID)
        return;
    }
  } else
    for (int k = 0; k < 3; k++) {
      int bx = x + rnd(3) - 1, bz = z + rnd(3) - 1;
      if (world_get(bx, y + 1, bz) == B_AIR && fire_flam(world_get(bx, y, bz))) fire_set(bx, y + 1, bz, 0);
    }
}

/* ---------------------------------------------------------------- growing */
static bool soil(int b) { return b == B_GRASS || b == B_DIRT || b == B_PODZOL || b == B_COARSE_DIRT; }
static void grow_tree(int x, int y, int z, int sapling);

/* BlockSapling.grow: two stages, then a tree (the stage is not kept: half a chance) */
void sapling_grow(int x, int y, int z) {
  int b = world_get(x, y, z);
  if (b >= B_SAPLING_OAK && b <= B_SAPLING_DARK_OAK && soil(world_get(x, y - 1, z)) && rnd(2)) grow_tree(x, y, z, b);
}

/* WorldGenTrees: a small tree of this wood (oak, birch, spruce and the others alike), if there is room */
static void grow_tree(int x, int y, int z, int sapling) {
  int v = sapling - B_SAPLING_OAK;
  static const uint16_t logs[6] = {B_LOG_OAK, B_LOG_SPRUCE, B_LOG_BIRCH, B_LOG_JUNGLE, B_LOG_ACACIA, B_LOG_DARK_OAK};
  static const uint16_t leaves[6] = {B_LEAVES_OAK, B_LEAVES_SPRUCE, B_LEAVES_BIRCH, B_LEAVES_JUNGLE, B_LEAVES_ACACIA,
                                     B_LEAVES_DARK_OAK};
  int h = 4 + rnd(3) + (v == 2 ? 1 : 0) + (v == 1 ? 2 : 0);
  for (int dy = 1; dy <= h + 1; dy++)
    for (int dz = -1; dz <= 1; dz++)
      for (int dx = -1; dx <= 1; dx++) {
        int b = world_get(x + dx, y + dy, z + dz);
        if (b != B_AIR && blk_model[b] != M_LEAVES && blk_model[b] != M_CROSS) return;
      }
  if (y + h + 2 >= WORLD_H) return;
  if (v == 1) {
    /* spruce (WorldGenTaiga2): rings of leaves narrowing to the top */
    int r = 0;
    for (int dy = h; dy >= 2; dy--) {
      for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++)
          if ((abs(dx) != r || abs(dz) != r || r == 0) && world_get(x + dx, y + dy, z + dz) == B_AIR)
            world_set(x + dx, y + dy, z + dz, leaves[v]);
      r = r >= 2 ? 1 : r + 1;
    }
    world_set(x, y + h + 1, z, leaves[v]);
  } else {
    /* oak and the others: four layers of leaves, the corners left out at random */
    for (int dy = h - 3; dy <= h; dy++) {
      int r = dy >= h - 1 ? 1 : 2;
      for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
          if (abs(dx) == r && abs(dz) == r && (dy == h || rnd(2) == 0)) continue;
          if (world_get(x + dx, y + dy, z + dz) == B_AIR) world_set(x + dx, y + dy, z + dz, leaves[v]);
        }
    }
  }
  for (int dy = 0; dy < h; dy++) world_set(x, y + dy, z, logs[v]);
  world_set(x, y - 1, z, B_DIRT);
}

static void random_tick(int x, int y, int z) {
  int b = world_get(x, y, z);
  if (b == B_FIRE) {
    fire_schedule(x, y, z, 8);
    return;
  }
  if (b == B_LAVA && rule(GR_FIRE_TICK)) {
    lava_fire(x, y, z);
    return;
  }
  int i = VC_I(x - vc_x0, y - vc_y0, z - vc_z0);
  int above = world_get(x, y + 1, z);
  int light = i + VCX * VCZ < VCY * VCZ * VCX && y + 1 - vc_y0 < VCY ? light_at(i + VCX * VCZ) : 15;
  int blight = y + 1 - vc_y0 < VCY ? block_light_at(i + VCX * VCZ) : 0;
  int lit = light > blight ? light : blight;
  if (b == B_GRASS) {
    /* BlockGrass: dies under something dark, else spreads to dirt nearby */
    if (lit < 4 && (blk_flags[above] & BF_OPAQUE)) world_set(x, y, z, B_DIRT);
    else if (lit >= 9)
      for (int k = 0; k < 4; k++) {
        int gx = x + rnd(3) - 1, gy = y + rnd(5) - 3, gz = z + rnd(3) - 1;
        if (world_get(gx, gy, gz) == B_DIRT && !(blk_flags[world_get(gx, gy + 1, gz)] & BF_OPAQUE)) world_set(gx, gy, gz, B_GRASS);
      }
    return;
  }
  if (b >= B_WHEAT_0 && b < B_WHEAT_7) {
    /* BlockCrops: in the light, now and then, faster on wet farmland */
    if (lit >= 9 && rnd(world_get(x, y - 1, z) == B_FARMLAND_WET ? 7 : 13) == 0) world_set(x, y, z, b + 1);
    return;
  }
  if ((b >= B_CARROTS_0 && b < B_CARROTS_3) || (b >= B_POTATOES_0 && b < B_POTATOES_3)) {
    /* four looks for eight ages: half as likely to show a change */
    if (lit >= 9 && rnd(world_get(x, y - 1, z) == B_FARMLAND_WET ? 14 : 26) == 0) world_set(x, y, z, b + 1);
    return;
  }
  if (b == B_FARMLAND || b == B_FARMLAND_WET) {
    /* wet with water within 4 blocks */
    bool wet = false;
    for (int dz = -4; dz <= 4 && !wet; dz++)
      for (int dx = -4; dx <= 4 && !wet; dx++)
        for (int dy = 0; dy <= 1 && !wet; dy++) wet = is_water(world_get(x + dx, y + dy, z + dz));
    if (rain_at(x, y + 1, z)) wet = true;
    if (wet != (b == B_FARMLAND_WET)) world_set(x, y, z, wet ? B_FARMLAND_WET : B_FARMLAND);
    else if (!wet && !(above >= B_WHEAT_0 && above <= B_WHEAT_7) && rnd(4) == 0) world_set(x, y, z, B_DIRT);
    return;
  }
  if (b >= B_SAPLING_OAK && b <= B_SAPLING_DARK_OAK) {
    /* BlockSapling: 1 in 7 in the light, two stages */
    if (lit >= 9 && rnd(7) == 0) sapling_grow(x, y, z);
    return;
  }
  if (b == B_SUGAR_CANE || b == B_CACTUS) {
    /* up to three high, one block every 16 ticks of its */
    if (above == B_AIR && rnd(16) == 0) {
      int h = 1;
      while (h < 3 && world_get(x, y - h, z) == b) h++;
      if (h < 3) {
        world_set(x, y + 1, z, b);
        neighbours_changed(x, y + 1, z);
      }
    }
    return;
  }
  if (blk_model[b] == M_LEAVES) {
    /* BlockLeaves: no log within 4 blocks: it decays */
    for (int dy = -4; dy <= 4; dy++)
      for (int dz = -4; dz <= 4; dz++)
        for (int dx = -4; dx <= 4; dx++) {
          int n = world_get(x + dx, y + dy, z + dz);
          if ((n >= B_LOG_OAK && n <= B_LOG_JUNGLE_BARK) || (n >= B_LOG_ACACIA && n <= B_LOG_DARK_OAK_BARK)) return;
          if (!world_loaded(x + dx, y + dy, z + dz)) return;
        }
    break_block_at(x, y, z, true);
    return;
  }
  if (b == B_SNOW_LAYER && blight > 11) world_set(x, y, z, B_AIR);
  if (b == B_ICE && blight > 11) world_set(x, y, z, B_WATER);
}

/* ---------------------------------------------------------------- weather */
Weather weather;
float rain_str, thunder_str;

/* the sun's angle in the sky (WorldProvider.calculateCelestialAngle) */
float celestial(uint32_t t) {
  float a = (t % 24000) / 24000.0f - 0.25f;
  if (a < 0) a += 1;
  return a + ((1 - (cosf(a * 3.14159265f) + 1) / 2) - a) / 3;
}

/* World.calculateSkylightSubtracted: 0 by day, 11 at night, more in rain and storms */
int sky_sub(void) {
  float f = 1 - (cosf(celestial(game_time) * 6.2831853f) * 2 + 0.5f);
  f = f < 0 ? 0 : f > 1 ? 1 : f;
  f = (1 - f) * (1 - rain_str * 5 / 16) * (1 - thunder_str * rain_str * 5 / 16);
  return (int)((1 - f) * 11);
}

/* BiomeGenBase.getFloatTemperature: colder higher up */
float temp_at(int x, int y, int z) {
  int lx = x - vc_x0, lz = z - vc_z0;
  int b = (unsigned)lx < VCX && (unsigned)lz < VCZ ? vbiome[lz * VCX + lx] : 1;
  float t = biome_temp[b] / 50.0f;
  if (y > 64) t -= (gen_temp_noise(x, z) * 4 + y - 64) * 0.05f / 30;
  return t;
}

/* World.canLightningStrike: rain falls here (not snow), out under the sky */
bool rain_at(int x, int y, int z) {
  if (rain_str <= 0.2f) return false;
  int lx = x - vc_x0, lz = z - vc_z0;
  if ((unsigned)lx >= VCX || (unsigned)lz >= VCZ) return false;
  int b = vbiome[lz * VCX + lx];
  if (biome_rain[b] != 1 || world_rain_top(x, z) > y) return false;
  return temp_at(x, y, z) > 0.15f;
}

/* World.updateWeather: clear for 10 minutes to 2.5 hours, then rain for 10 to 20;
 * storms come and go on their own clock but only show while it rains */
void weather_tick(void) {
  if (weather.thunder_time <= 0) weather.thunder_time = weather.thundering ? rnd(12000) + 3600 : rnd(168000) + 12000;
  else if (--weather.thunder_time <= 0) weather.thundering = !weather.thundering;
  thunder_str += weather.thundering ? 0.01f : -0.01f;
  thunder_str = thunder_str < 0 ? 0 : thunder_str > 1 ? 1 : thunder_str;
  if (weather.rain_time <= 0) weather.rain_time = weather.raining ? rnd(12000) + 12000 : rnd(168000) + 12000;
  else if (--weather.rain_time <= 0) weather.raining = !weather.raining;
  rain_str += weather.raining ? 0.01f : -0.01f;
  rain_str = rain_str < 0 ? 0 : rain_str > 1 ? 1 : rain_str;
}

/* ---------------------------------------------------------------- lightning */
Bolt bolt;
int last_bolt;

/* EntityLightningBolt.onUpdate: what is within 3 blocks (and 6 up) is struck:
 * 5 damage, and set on fire for 8 seconds (Entity.onStruckByLightning) */
static void bolt_strikes(void) {
  if (fabsf(pl.x - bolt.x) < 3.3f && fabsf(pl.z - bolt.z) < 3.3f && pl.y + 1.8f > bolt.y - 3 && pl.y < bolt.y + 9) {
    player_hurt(5, DMG_LIGHTNING);
    if (pl.mode != 1 && pl.fire < 160) pl.fire = 160;
  }
  for (int i = 0; i < N_ENT; i++) {
    Entity *e = &ents[i];
    if (e->type >= E_ZOMBIE && e->type <= E_CHICKEN && fabsf(e->x - bolt.x) < 3.3f && fabsf(e->z - bolt.z) < 3.3f &&
        e->y > bolt.y - 4 && e->y < bolt.y + 9)
      mob_struck(e);
  }
}

/* EntityLightningBolt: on Normal and Hard it sets fire where it strikes (and the first time, about it) */
static void bolt_fire(int n) {
  if (!rule(GR_FIRE_TICK) || opt.difficulty < 2) return;
  int bx = (int)floorf(bolt.x), by = (int)floorf(bolt.y), bz = (int)floorf(bolt.z);
  for (int k = 0; k < n; k++) {
    int fx = bx, fy = by, fz = bz;
    if (k) fx += rnd(3) - 1, fy += rnd(3) - 1, fz += rnd(3) - 1;
    if (world_loaded(fx, fy, fz) && world_get(fx, fy, fz) == B_AIR && fire_can_stay(fx, fy, fz)) fire_set(fx, fy, fz, 0);
  }
}
void bolt_start(float x, float y, float z) {
  bolt.x = x, bolt.y = y, bolt.z = z;
  bolt.on = 1, bolt.state = 2, bolt.living = (int8_t)(rnd(3) + 1), bolt.seed = (uint32_t)rnd(1 << 30);
  bolt_fire(5);
}

static void bolt_tick(void) {
  if (last_bolt > 0) last_bolt--;
  /* WorldServer.updateBlocks: in a storm, each of the 225 chunks about the
   * player is struck one tick in 100000, where rain falls on it, and a
   * creature within 3 blocks draws it (adjustPosToNearbyEntity) */
  if (!bolt.on && rain_str > 0.2f && thunder_str * rain_str > 0.9f && rnd(100000) < 225) {
    int x = (int)floorf(pl.x) + rnd(225) - 112, z = (int)floorf(pl.z) + rnd(225) - 112, y = 64;
    if (world_loaded(x, vc_y0, z)) {
      y = world_rain_top(x, z);
      if (y > WORLD_H || !rain_at(x, y, z)) return;
    } else if (!rain_at((int)floorf(pl.x), WORLD_H, (int)floorf(pl.z)))
      return;   /* (too far to know: where it rains on the player) */
    float bx = x + 0.5f, by = (float)y, bz = z + 0.5f;
    if (fabsf(pl.x - bx) < 3.5f && fabsf(pl.z - bz) < 3.5f && !pl.dead &&
        pl.y >= world_rain_top((int)floorf(pl.x), (int)floorf(pl.z)))
      bx = pl.x, by = pl.y, bz = pl.z;
    bolt_start(bx, by, bz);
  }
  if (!bolt.on) return;
  if (--bolt.state < 0) {
    if (bolt.living == 0) {
      bolt.on = 0;
      return;
    }
    if (bolt.state < -rnd(10)) bolt.living--, bolt.state = 1, bolt.seed = (uint32_t)rnd(1 << 30), bolt_fire(1);
  }
  if (bolt.state >= 0) {
    last_bolt = 2;
    bolt_strikes();
  }
}

/* WorldProvider.resetRainAndThunder: after a night's sleep */
void weather_clear(void) {
  weather.rain_time = weather.thunder_time = 0;
  weather.raining = weather.thundering = 0;
}

/* WorldServer.updateBlocks, one column in 16 chunks a tick: still water
 * freezes where it is cold, and snow settles while it falls */
static void freeze_tick(void) {
  if (rnd(16) >= VCX * VCZ / 256) return;
  int x = vc_x0 + rnd(VCX), z = vc_z0 + rnd(VCZ);
  int y = world_rain_top(x, z);
  if (y > WORLD_H || !world_loaded(x, y - 1, z) || !world_loaded(x, y, z)) return;
  int i = VC_I(x - vc_x0, y - vc_y0, z - vc_z0), below = world_get(x, y - 1, z);
  /* World.canBlockFreeze: still water with something other than water beside it */
  if (below == B_WATER && temp_at(x, y - 1, z) <= 0.15f && block_light_at(i - VCX * VCZ) < 10 &&
      !(is_water(world_get(x - 1, y - 1, z)) && is_water(world_get(x + 1, y - 1, z)) &&
        is_water(world_get(x, y - 1, z - 1)) && is_water(world_get(x, y - 1, z + 1)))) {
    world_set(x, y - 1, z, B_ICE);
    below = B_ICE;
  }
  /* World.canSnowAt and BlockSnow.canPlaceBlockAt: on leaves or a solid cube, not on ice */
  if (rain_str > 0.2f && world_get(x, y, z) == B_AIR && temp_at(x, y, z) <= 0.15f && block_light_at(i) < 10 &&
      below != B_ICE && below != B_PACKED_ICE &&
      (blk_model[below] == M_LEAVES || ((blk_flags[below] & BF_OPAQUE) && blk_model[below] == M_CUBE)))
    world_set(x, y, z, B_SNOW_LAYER);
}

void world_tick(void) {
  weather_tick();
  bolt_tick();
  freeze_tick();
  fluids_tick();
  /* about 3 a section: the cache is VCX x VCY x VCZ blocks */
  int n = VCX * VCY * VCZ * 3 / 4096;
  for (int k = 0; k < n; k++) {
    int x = vc_x0 + rnd(VCX), y = vc_y0 + rnd(VCY), z = vc_z0 + rnd(VCZ);
    random_tick(x, y, z);
  }
}
