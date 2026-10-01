/* Mobs: their models (mob.h), how they move, fight, spawn and die.
 *
 * Minecraft 1.8.8's numbers: health, speed, damage (scaled by difficulty),
 * the walk animation, zombies and skeletons burning in daylight, creepers'
 * 1.5 second fuse and power 3 explosion, skeletons' arrows, spiders neutral
 * in daylight, animals wandering and panicking when hit, and the loot. The
 * AI steers straight at its target instead of searching a path, jumping over
 * single blocks; mobs spawn in the dark from 14 blocks away (the calculator
 * keeps only about 20 blocks of the world around you). */
#include <math.h>
#include <stdlib.h>
#include "mob.h"

/* ---------------------------------------------------------------- what each mob is */
typedef struct {
  float w, h, speed;
  uint8_t health;
} MobInfo;
static const MobInfo info[] = {
    [E_ZOMBIE] = {0.6f, 1.95f, 0.23f, 20}, [E_SKELETON] = {0.6f, 1.99f, 0.25f, 20},
    [E_CREEPER] = {0.6f, 1.7f, 0.25f, 20},  [E_SPIDER] = {1.4f, 0.9f, 0.3f, 16},
    [E_PIG] = {0.9f, 0.9f, 0.25f, 10},      [E_COW] = {0.9f, 1.3f, 0.2f, 10},
    [E_SHEEP] = {0.9f, 1.3f, 0.23f, 8},     [E_CHICKEN] = {0.4f, 0.7f, 0.25f, 4},
};
float mob_width(int t) { return t >= E_ZOMBIE && t <= E_CHICKEN ? info[t].w : 0.25f; }
float mob_height(int t) { return t >= E_ZOMBIE && t <= E_CHICKEN ? info[t].h : 0.25f; }
static bool hostile(int t) { return t >= E_ZOMBIE && t <= E_SPIDER; }

float mob_scale(const Entity *e, float pt) {
  if (e->growth < 0) return 0.5f;   /* a baby */
  if (e->type != E_CREEPER || e->delay <= 0) return 1;
  /* RenderCreeper: swells by up to 40% (and flickers) over the fuse */
  float f = (e->delay + pt) / 28.0f;
  if (f > 1) f = 1;
  return 1 + f * f * f * f * 0.4f;
}

/* ---------------------------------------------------------------- models */
static int np;
static Part *out_parts;
static Part *box(float x, float y, float z, int w, int h, int d, int u, int v, float px, float py, float pz, int skin) {
  Part *p = &out_parts[np++];
  *p = (Part){x, y, z, (uint8_t)w, (uint8_t)h, (uint8_t)d, (uint8_t)u, (uint8_t)v, 0, (uint8_t)skin, px, py, pz, 0, 0,
              0, 0};
  return p;
}

#define PI 3.14159265f
int mob_parts(const Entity *e, float pt, Part *o) {
  np = 0;
  out_parts = o;
  float f = e->limb + e->limb_amt * pt, a = e->limb_amt;   /* limbSwing, limbSwingAmount */
  if (a > 1) a = 1;
  float pitch = e->pitch * 0.017453292f, age = e->age + pt;
  float sw = cosf(f * 0.6662f), swo = cosf(f * 0.6662f + PI);
  switch (e->type) {
    case E_ZOMBIE:
    case E_SKELETON: {
      int sk = e->type == E_ZOMBIE ? SKIN_ZOMBIE : SKIN_SKELETON;
      box(-4, -8, -4, 8, 8, 8, 0, 0, 0, 0, 0, sk)->rx = pitch;
      box(-4, 0, -2, 8, 12, 4, 16, 16, 0, 0, 0, sk);
      Part *ra, *la, *rl, *ll;
      if (e->type == E_ZOMBIE) {
        ra = box(-3, -2, -2, 4, 12, 4, 40, 16, -5, 2, 0, sk);
        la = box(-1, -2, -2, 4, 12, 4, 40, 16, 5, 2, 0, sk);
        rl = box(-2, 0, -2, 4, 12, 4, 0, 16, -1.9f, 12, 0, sk);
        ll = box(-2, 0, -2, 4, 12, 4, 0, 16, 1.9f, 12, 0, sk);
        /* ModelZombie: arms held out, swaying a little */
        ra->rx = la->rx = -PI / 2 + sinf(age * 0.067f) * 0.05f;
        ra->rz = cosf(age * 0.09f) * 0.05f + 0.05f;
        la->rz = -ra->rz;
      } else {
        ra = box(-1, -2, -1, 2, 12, 2, 40, 16, -5, 2, 0, sk);
        la = box(-1, -2, -1, 2, 12, 2, 40, 16, 5, 2, 0, sk);
        rl = box(-1, 0, -1, 2, 12, 2, 0, 16, -2, 12, 0, sk);
        ll = box(-1, 0, -1, 2, 12, 2, 0, 16, 2, 12, 0, sk);
        /* aiming its bow */
        ra->rx = la->rx = -PI / 2 + pitch;
        ra->ry = -0.1f;
        la->ry = 0.5f;
      }
      la->mirror = ll->mirror = 1;
      rl->rx = sw * 1.4f * a;
      ll->rx = swo * 1.4f * a;
      break;
    }
    case E_CREEPER: {
      box(-4, -8, -4, 8, 8, 8, 0, 0, 0, 6, 0, SKIN_CREEPER)->rx = pitch;
      box(-4, 0, -2, 8, 12, 4, 16, 16, 0, 6, 0, SKIN_CREEPER);
      box(-2, 0, -2, 4, 6, 4, 0, 16, -2, 18, 4, SKIN_CREEPER)->rx = sw * 1.4f * a;
      box(-2, 0, -2, 4, 6, 4, 0, 16, 2, 18, 4, SKIN_CREEPER)->rx = swo * 1.4f * a;
      box(-2, 0, -2, 4, 6, 4, 0, 16, -2, 18, -4, SKIN_CREEPER)->rx = swo * 1.4f * a;
      box(-2, 0, -2, 4, 6, 4, 0, 16, 2, 18, -4, SKIN_CREEPER)->rx = sw * 1.4f * a;
      break;
    }
    case E_SPIDER: {
      box(-4, -4, -8, 8, 8, 8, 32, 4, 0, 15, -3, SKIN_SPIDER)->rx = pitch;
      box(-3, -3, -3, 6, 6, 6, 0, 0, 0, 15, 0, SKIN_SPIDER);
      box(-5, -4, -6, 10, 8, 12, 0, 12, 0, 15, 9, SKIN_SPIDER);
      /* ModelSpider: eight legs fanned out, swinging in pairs */
      static const float rz[8] = {-0.7853982f, 0.7853982f, -0.5811946f, 0.5811946f, -0.5811946f, 0.5811946f,
                                  -0.7853982f, 0.7853982f};
      static const float ry[8] = {0.7853982f, -0.7853982f, 0.3926991f, -0.3926991f, -0.3926991f, 0.3926991f,
                                  -0.7853982f, 0.7853982f};
      static const float pzs[4] = {2, 1, 0, -1};
      float g = f * 0.6662f * 2;
      float swy[4] = {-cosf(g) * 0.4f * a, -cosf(g + PI) * 0.4f * a, -cosf(g + PI / 2) * 0.4f * a,
                      -cosf(g + PI * 1.5f) * 0.4f * a};
      float swz[4] = {fabsf(sinf(f * 0.6662f)) * 0.4f * a, fabsf(sinf(f * 0.6662f + PI)) * 0.4f * a,
                      fabsf(sinf(f * 0.6662f + PI / 2)) * 0.4f * a, fabsf(sinf(f * 0.6662f + PI * 1.5f)) * 0.4f * a};
      for (int l = 0; l < 8; l++) {
        bool right = !(l & 1);
        Part *p = box(right ? -15 : -1, -1, -1, 16, 2, 2, 18, 0, right ? -4 : 4, 15, pzs[l >> 1], SKIN_SPIDER);
        p->rz = rz[l] + (right ? swz[l >> 1] : -swz[l >> 1]);
        p->ry = ry[l] + (right ? swy[l >> 1] : -swy[l >> 1]);
      }
      break;
    }
    case E_PIG:
    case E_COW:
    case E_SHEEP: {
      int sk = e->type == E_PIG ? SKIN_PIG : e->type == E_COW ? SKIN_COW : SKIN_SHEEP;
      float lh = e->type == E_PIG ? 6 : 12;
      Part *body;
      if (e->type == E_PIG) {
        box(-4, -4, -8, 8, 8, 8, 0, 0, 0, 12, -6, sk)->rx = pitch;
        box(-2, 0, -9, 4, 3, 1, 16, 16, 0, 12, -6, sk)->rx = pitch;   /* the snout */
        body = box(-5, -10, -7, 10, 16, 8, 28, 8, 0, 11, 2, sk);
      } else if (e->type == E_COW) {
        box(-4, -4, -6, 8, 8, 6, 0, 0, 0, 4, -8, sk)->rx = pitch;
        box(-5, -5, -4, 1, 3, 1, 22, 0, 0, 4, -8, sk)->rx = pitch;   /* horns */
        box(4, -5, -4, 1, 3, 1, 22, 0, 0, 4, -8, sk)->rx = pitch;
        body = box(-6, -10, -7, 12, 18, 10, 18, 4, 0, 5, 2, sk);
        box(-2, 2, -8, 4, 6, 1, 52, 0, 0, 5, 2, sk)->rx = PI / 2;   /* the udder */
      } else {
        box(-3, -4, -6, 6, 6, 8, 0, 0, 0, 6, -8, sk)->rx = pitch;
        body = box(-4, -10, -7, 8, 16, 6, 28, 8, 0, 5, 2, sk);
      }
      body->rx = PI / 2;
      float lx = e->type == E_COW ? 4 : 3, lz0 = 7, lz1 = e->type == E_COW ? -6 : -5;
      float ly = 24 - lh;
      Part *l1 = box(-2, 0, -2, 4, (int)lh, 4, 0, 16, -lx, ly, lz0, sk);
      Part *l2 = box(-2, 0, -2, 4, (int)lh, 4, 0, 16, lx, ly, lz0, sk);
      Part *l3 = box(-2, 0, -2, 4, (int)lh, 4, 0, 16, -lx, ly, lz1, sk);
      Part *l4 = box(-2, 0, -2, 4, (int)lh, 4, 0, 16, lx, ly, lz1, sk);
      l1->rx = l4->rx = sw * 1.4f * a;
      l2->rx = l3->rx = swo * 1.4f * a;
      if (e->type == E_SHEEP && !e->sheared) {
        /* the wool (ModelSheep1), inflated */
        Part *h = box(-3, -4, -4, 6, 6, 6, 0, 0, 0, 6, -8, SKIN_SHEEP_FUR);
        h->grow = 0.6f, h->rx = pitch;
        Part *b = box(-4, -10, -7, 8, 16, 6, 28, 8, 0, 5, 2, SKIN_SHEEP_FUR);
        b->grow = 1.75f, b->rx = PI / 2;
        Part *ls[4] = {l1, l2, l3, l4};
        for (int k = 0; k < 4; k++) {
          Part *w = box(-2, 0, -2, 4, 6, 4, 0, 16, ls[k]->px, ls[k]->py, ls[k]->pz, SKIN_SHEEP_FUR);
          w->grow = 0.5f, w->rx = ls[k]->rx;
        }
      }
      break;
    }
    case E_CHICKEN: {
      float hp = pitch;
      box(-2, -6, -2, 4, 6, 3, 0, 0, 0, 15, -4, SKIN_CHICKEN)->rx = hp;
      box(-2, -4, -4, 4, 2, 2, 14, 0, 0, 15, -4, SKIN_CHICKEN)->rx = hp;   /* the bill */
      box(-1, -2, -3, 2, 2, 2, 14, 4, 0, 15, -4, SKIN_CHICKEN)->rx = hp;   /* the wattle */
      box(-3, -4, -3, 6, 8, 6, 0, 9, 0, 16, 0, SKIN_CHICKEN)->rx = PI / 2;
      box(-1, 0, -3, 3, 5, 3, 26, 0, -2, 19, 1, SKIN_CHICKEN)->rx = sw * 1.4f * a;
      box(-1, 0, -3, 3, 5, 3, 26, 0, 1, 19, 1, SKIN_CHICKEN)->rx = swo * 1.4f * a;
      float flap = e->on_ground ? 0 : (sinf(age * 1.5f) + 1) * 0.6f;
      box(0, 0, -3, 1, 4, 6, 24, 13, -4, 13, 0, SKIN_CHICKEN)->rz = flap;
      box(-1, 0, -3, 1, 4, 6, 24, 13, 4, 13, 0, SKIN_CHICKEN)->rz = -flap;
      break;
    }
  }
  return np;
}

/* ---------------------------------------------------------------- the world around a mob */
extern uint32_t game_time;
static inline int ifl(float v) { return (int)floorf(v); }

/* World.isDaytime */
static bool daylight(void) { return sky_sub() < 4; }
/* the light where a mob would stand: sky light less the night's darkness */
static int light_there(int x, int y, int z) {
  if (!world_loaded(x, y, z)) return 15;
  int i = VC_I(x - vc_x0, y - vc_y0, z - vc_z0);
  int sky = light_at(i), blk = block_light_at(i);
  sky -= sky_sub();
  return sky > blk ? sky : blk;
}
static bool can_stand(int x, int y, int z) {
  int below = world_get(x, y - 1, z), feet = world_get(x, y, z), head = world_get(x, y + 1, z);
  return (blk_flags[below] & BF_OPAQUE) && !(blk_flags[feet] & BF_SOLID) && blk_model[feet] != M_LIQUID &&
         !(blk_flags[head] & BF_SOLID) && blk_model[head] != M_LIQUID;
}

/* ---------------------------------------------------------------- spawning */
static int count(bool host) {
  int n = 0;
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type >= E_ZOMBIE && ents[i].type <= E_CHICKEN && hostile(ents[i].type) == host) n++;
  return n;
}

static Entity *spawn(int type, float x, float y, float z) {
  Entity *e = ent_new(type, x, y, z);
  if (!e) return NULL;
  e->health = info[type].health;
  e->yaw = rndf() * 360;
  e->gx = x, e->gz = z;
  return e;
}
Entity *mob_summon(int type, float x, float y, float z) { return spawn(type, x, y, z); }

/* SpawnerAnimals, made small: a try every second */
void mobs_spawn(void) {
  if (pl.dead) return;
  for (int k = 0; k < 4; k++) {
    int x = ifl(pl.x) + rnd(41) - 20, z = ifl(pl.z) + rnd(41) - 20;
    float dx = x + 0.5f - pl.x, dz = z + 0.5f - pl.z;
    if (dx * dx + dz * dz < 14 * 14) continue;
    int y0 = ifl(pl.y) + 8;
    for (int y = y0; y > y0 - 20; y--) {
      if (!world_loaded(x, y + 1, z) || !world_loaded(x, y - 1, z)) continue;
      if (!can_stand(x, y, z)) continue;
      int below = world_get(x, y - 1, z);
      int light = light_there(x, y, z);
      /* EntityMob.isValidLightLevel: in a storm, the sky counts as night's */
      if (thunder_str * rain_str > 0.9f) {
        int i = VC_I(x - vc_x0, y - vc_y0, z - vc_z0), sky = light_at(i) - 10, blk = block_light_at(i);
        light = sky > blk ? sky : blk;
      }
      if (opt.difficulty > 0 && count(true) < 4 && light <= rnd(8) && below != B_BEDROCK) {
        static const uint8_t kinds[4] = {E_ZOMBIE, E_SKELETON, E_CREEPER, E_SPIDER};
        int t = kinds[rnd(4)];
        if (t == E_SPIDER && !(can_stand(x + 1, y, z) && can_stand(x, y, z + 1))) t = E_ZOMBIE;
        spawn(t, x + 0.5f, (float)y, z + 0.5f);
        return;
      }
      if (below == B_GRASS && light > 8 && count(false) < 4 && rnd(4) == 0) {
        /* sheep 12, pig 10, chicken 10, cow 8 */
        int r = rnd(40);
        int t = r < 12 ? E_SHEEP : r < 22 ? E_PIG : r < 32 ? E_CHICKEN : E_COW;
        int n = 1 + rnd(2);
        for (int i = 0; i < n; i++) spawn(t, x + 0.5f + i * 0.3f, (float)y, z + 0.5f);
        return;
      }
      break;
    }
  }
}

/* ---------------------------------------------------------------- dying, loot */
static void drop(int id, int n, const Entity *e) {
  if (n > 0) ent_drop(id, n, 0, e->x, e->y + 0.5f, e->z, false);
}
static void loot(Entity *e) {
  if (e->growth < 0 || !rule(GR_MOB_LOOT)) return;   /* babies drop nothing */
  switch (e->type) {
    case E_ZOMBIE: drop(I_ROTTEN_FLESH, rnd(3), e); break;
    case E_SKELETON:
      drop(I_BONE, rnd(3), e);
      drop(I_ARROW, rnd(3), e);
      break;
    case E_CREEPER: drop(I_GUNPOWDER, rnd(3), e); break;
    case E_SPIDER:
      drop(I_STRING, rnd(3), e);
      if (rnd(3) == 0) drop(I_SPIDER_EYE, 1, e);
      break;
    case E_PIG: drop(e->fire > 0 ? I_COOKED_PORKCHOP : I_PORKCHOP, 1 + rnd(3), e); break;
    case E_COW:
      drop(e->fire > 0 ? I_COOKED_BEEF : I_BEEF, 1 + rnd(3), e);
      drop(I_LEATHER, rnd(3), e);
      break;
    case E_SHEEP:
      if (!e->sheared) drop(B_WOOL_WHITE, 1, e);
      drop(e->fire > 0 ? I_COOKED_MUTTON : I_MUTTON, 1 + rnd(2), e);
      break;
    case E_CHICKEN:
      drop(I_FEATHER, rnd(3), e);
      drop(e->fire > 0 ? I_COOKED_CHICKEN : I_CHICKEN, 1, e);
      break;
  }
  player_add_xp(hostile(e->type) ? 5 : 1 + rnd(3));
}

static void hurt(Entity *e, float dmg, float kx, float kz) {
  if (e->state == 255 || e->invuln > 10) return;
  e->health = (int16_t)(e->health - (int)ceilf(dmg));
  e->invuln = 20;
  e->hurt = 10;
  /* EntityLivingBase.knockBack */
  float d = sqrtf(kx * kx + kz * kz);
  if (d > 0) {
    e->vx = e->vx / 2 + kx / d * 0.4f;
    e->vz = e->vz / 2 + kz / d * 0.4f;
    e->vy = e->vy / 2 + 0.4f;
    if (e->vy > 0.4f) e->vy = 0.4f;
  }
  if (!hostile(e->type)) e->panic = 100;
  else if (kx != 0 || kz != 0) e->panic = e->panic > 100 ? e->panic : 100;   /* EntityAIHurtByTarget */
  if (e->type == E_SPIDER) e->panic = 600;   /* a hit spider fights back, even by day */
  if (e->health <= 0) {
    e->state = 255;
    e->timer = 0;
  }
}

/* ---------------------------------------------------------------- the player and mobs */
Entity *entity_looked_at(float reach, float block_t) {
  float ex = look_ray[0], ey = look_ray[1], ez = look_ray[2], *d = look_ray + 3;
  float best = reach < block_t ? reach : block_t;
  Entity *hit = NULL;
  for (int i = 0; i < N_ENT; i++) {
    Entity *e = &ents[i];
    if (e->type < E_ZOMBIE || e->type > E_CHICKEN || e->state == 255) continue;
    float w = info[e->type].w / 2 + 0.1f, h = info[e->type].h + 0.1f;
    float lo[3] = {e->x - w, e->y, e->z - w}, hi[3] = {e->x + w, e->y + h, e->z + w}, o[3] = {ex, ey, ez};
    float t0 = 0, t1 = best;
    bool miss = false;
    for (int a = 0; a < 3 && !miss; a++) {
      if (fabsf(d[a]) < 1e-6f) {
        if (o[a] < lo[a] || o[a] > hi[a]) miss = true;
        continue;
      }
      float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
      if (ta > tb) {
        float t = ta;
        ta = tb, tb = t;
      }
      if (ta > t0) t0 = ta;
      if (tb < t1) t1 = tb;
      if (t0 > t1) miss = true;
    }
    if (!miss) best = t0, hit = e;
  }
  return hit;
}

bool mob_attack(const Entity *ce) {
  Entity *e = (Entity *)ce;
  Stack *h = held();
  int k = item_kind(h->id);
  float dmg = h->id >= 256 && k >= IK_PICKAXE && k <= IK_SWORD ? it_a[h->id - 256] : 1;
  /* a critical hit: falling, not on a ladder or in water */
  if (pl.vy < 0 && !pl.on_ground && !pl.in_water) dmg *= 1.5f;
  float kx = e->x - pl.x, kz = e->z - pl.z;
  if (pl.sprinting) {
    /* a sprinting hit knocks harder, slows you and ends the sprint */
    e->vx += kx * 0.2f, e->vz += kz * 0.2f;
    pl.vx *= 0.6f, pl.vz *= 0.6f;
    pl.sprinting = false;
  }
  hurt(e, dmg, kx, kz);
  if (pl.mode == 0) {
    if (k == IK_SWORD) stack_wear(h, 1);
    else if (k >= IK_PICKAXE && k <= IK_HOE) stack_wear(h, 2);
  }
  pl.exhaustion += 0.3f;
  return true;
}

/* what each animal is fed to breed (EntityAnimal.isBreedingItem) */
static int food_of(int type) {
  switch (type) {
    case E_COW: case E_SHEEP: return I_WHEAT;
    case E_PIG: return I_CARROT;
    case E_CHICKEN: return I_WHEAT_SEEDS;
  }
  return 0;
}

/* hit by a fishing hook: no damage, but it flinches (and runs, if it can) */
void mob_hooked(Entity *e) { hurt(e, 0, 0, 0); }

/* Entity.onStruckByLightning */
void mob_struck(Entity *e) {
  hurt(e, 5, 0, 0);
  if (e->fire < 160) e->fire = 160;
}

bool mob_use(Entity *e) {
  Stack *h = held();
  if (h->id && h->id == food_of(e->type) && e->state != 255 && e->growth >= 0 && !e->love && e->timer <= 0) {
    /* fed: in love for 30 seconds */
    e->love = 600;
    if (pl.mode == 0) stack_take(h, 1);
    return true;
  }
  if (h->id && h->id == food_of(e->type) && e->growth < 0) {
    /* a baby fed grows up a tenth faster */
    e->growth = (int16_t)(e->growth / 10 * 9);
    if (pl.mode == 0) stack_take(h, 1);
    return true;
  }
  if (e->type == E_SHEEP && h->id == I_SHEARS && !e->sheared && e->state != 255) {
    e->sheared = 1;
    drop(B_WOOL_WHITE, 1 + rnd(3), e);
    if (pl.mode == 0) stack_wear(h, 1);
    return true;
  }
  if (e->type == E_COW && h->id == I_BUCKET) {
    if (pl.mode == 1) return true;
    if (h->aux <= 1) h->id = I_MILK_BUCKET, h->aux = 1;
    else {
      h->aux--;
      if (inv_add(pl.inv, 36, I_MILK_BUCKET, 1, 0)) ent_drop(I_MILK_BUCKET, 1, 0, pl.x, pl.y + 1.3f, pl.z, true);
    }
    return true;
  }
  return false;
}

/* difficulty scales what mobs do to the player (EntityPlayer.attackEntityFrom) */
static float scaled(float d) {
  switch (opt.difficulty) {
    case 0: return 0;
    case 1: return d / 2 + 1 < d ? d / 2 + 1 : d;
    case 3: return d * 1.5f;
  }
  return d;
}

static void push_player(float kx, float kz, float s) {
  float d = sqrtf(kx * kx + kz * kz);
  if (d <= 0) return;
  pl.vx += kx / d * s;
  pl.vz += kz / d * s;
  pl.vy += 0.4f * (s / 0.4f);
  if (pl.vy > 0.4f) pl.vy = 0.4f;
}

/* ---------------------------------------------------------------- explosions (Explosion, power 3) */
/* EntityTNTPrimed: lit TNT hops up, a little to one side, and blows when its fuse is out */
void tnt_light(int x, int y, int z, int fuse) {
  Entity *t = ent_new(E_TNT, x + 0.5f, (float)y, z + 0.5f);
  if (!t) return;
  float a = rndf() * 6.2831853f;
  t->delay = (int16_t)fuse, t->item.id = B_TNT;
  t->vx = -sinf(a) * 0.02f, t->vy = 0.2f, t->vz = -cosf(a) * 0.02f;
}

void explode(float x, float y, float z, float power, bool blocks) {
  int r = blocks ? (int)ceilf(power) : -1;
  for (int dy = -r; dy <= r; dy++)
    for (int dz = -r; dz <= r; dz++)
      for (int dx = -r; dx <= r; dx++) {
        float d = sqrtf((float)(dx * dx + dy * dy + dz * dz));
        /* the blast fades with distance and randomly (0.7 to 1.3 of the power), blocks resist it */
        float f = power * (0.7f + rndf() * 0.6f) - d * 0.9f;
        int bx = ifl(x) + dx, by = ifl(y) + dy, bz = ifl(z) + dz;
        int b = world_get(bx, by, bz);
        if (b == B_AIR || blk_model[b] == M_LIQUID || blk_hard[b] == 255 || b == B_OBSIDIAN) continue;
        float resist = blk_hard[b] / 20.0f * 5 / 5 + 0.3f;   /* (blast resistance, about the hardness) */
        if (f - resist * 0.3f <= 0) continue;
        if (b == B_CHEST || b == B_FURNACE || b == B_FURNACE_LIT) tiles_removed(bx, by, bz);
        world_set(bx, by, bz, B_AIR);
        if (b == B_TNT) {
          /* TNT caught in a blast is lit, with a shorter fuse */
          tnt_light(bx, by, bz, 10 + rnd(20));
          continue;
        }
        /* one in power of the blocks drop */
        if (rnd((int)power) == 0 && rule(GR_TILE_DROPS)) {
          Stack out[2];
          int n = block_drops(b, I_DIAMOND_PICKAXE, out);
          for (int i = 0; i < n; i++) ent_drop(out[i].id, out[i].aux, 0, bx + 0.5f, by + 0.5f, bz + 0.5f, false);
        }
      }
  /* the player: up to 2 x power blocks away, (impact^2 + impact) / 2 x 7 x 2 power + 1 */
  float px = pl.x - x, py = pl.y + 0.9f - y, pz = pl.z - z;
  float dist = sqrtf(px * px + py * py + pz * pz) / (power * 2);
  if (dist <= 1) {
    float impact = 1 - dist;
    float dmg = (impact * impact + impact) / 2 * 7 * power * 2 + 1;
    player_hurt(scaled(dmg), DMG_EXPLOSION);
    push_player(px, pz, impact);
  }
  for (int i = 0; i < N_ENT; i++) {
    Entity *e = &ents[i];
    if (e->type < E_ZOMBIE || e->type > E_CHICKEN) continue;
    float ex = e->x - x, ez = e->z - z, ey = e->y - y;
    float d = sqrtf(ex * ex + ey * ey + ez * ez) / (power * 2);
    if (d <= 1) hurt(e, ((1 - d) * (1 - d) + (1 - d)) / 2 * 7 * power * 2 + 1, ex, ez);
  }
}

/* ---------------------------------------------------------------- arrows */
static void shoot(const Entity *from) {
  Entity *a = ent_new(E_ARROW, from->x, from->y + 1.5f, from->z);
  if (!a) return;
  a->item.id = I_ARROW;
  float dx = pl.x - a->x, dy = pl.y + 0.6f - a->y, dz = pl.z - a->z;
  float h = sqrtf(dx * dx + dz * dz);
  dy += h * 0.2f;   /* aimed a little up, to arc */
  float d = sqrtf(dx * dx + dy * dy + dz * dz);
  /* EntityArrow.setThrowableHeading: speed 1.6, spread 14 - difficulty x 4 */
  float spread = (14 - opt.difficulty * 4) * 0.0075f;
  a->vx = (dx / d + (rndf() - 0.5f) * 2 * spread) * 1.6f;
  a->vy = (dy / d + (rndf() - 0.5f) * 2 * spread) * 1.6f;
  a->vz = (dz / d + (rndf() - 0.5f) * 2 * spread) * 1.6f;
  a->yaw = atan2f(-a->vx, a->vz) * 57.29578f;
}

/* the player throws or shoots: from the eyes, the way they look (EntityThrowable, EntityArrow) */
void throw_item(int id, float speed, bool from_player) {
  float yaw = pl.yaw * 0.017453292f, pitch = pl.pitch * 0.017453292f;
  Entity *a = ent_new(E_ARROW, pl.x - cosf(yaw) * 0.16f, pl.y + 1.52f, pl.z - sinf(yaw) * 0.16f);
  if (!a) return;
  a->item.id = (uint16_t)id;
  a->item.aux = from_player;   /* the player's arrows can be picked up again */
  float dx = -sinf(yaw) * cosf(pitch), dy = -sinf(pitch), dz = cosf(yaw) * cosf(pitch);
  a->vx = (dx + (rndf() - 0.5f) * 0.015f) * speed + pl.vx;
  a->vy = (dy + (rndf() - 0.5f) * 0.015f) * speed + (pl.on_ground ? 0 : pl.vy);
  a->vz = (dz + (rndf() - 0.5f) * 0.015f) * speed + pl.vz;
  a->yaw = pl.yaw;
}

static void arrow_tick(Entity *e) {
  bool arrow = e->item.id == I_ARROW;
  if (e->state) {   /* stuck in a block: the player's can be picked up */
    float px = pl.x - e->x, py = pl.y + 0.9f - e->y, pz = pl.z - e->z;
    if (e->item.aux && px * px + py * py + pz * pz < 2.0f && !inv_add(pl.inv, 36, I_ARROW, 1, 0)) e->type = E_NONE;
    if (++e->timer > 1200) e->type = E_NONE;
    return;
  }
  float nx = e->x + e->vx, ny = e->y + e->vy, nz = e->z + e->vz;
  float sp = sqrtf(e->vx * e->vx + e->vy * e->vy + e->vz * e->vz);
  /* the player in the way of a mob's arrow */
  float px = pl.x - nx, pz = pl.z - nz;
  if (!e->item.aux && !pl.dead && px * px + pz * pz < 0.5f && ny > pl.y && ny < pl.y + 1.8f) {
    player_hurt(scaled(ceilf(sp * 2.2f)), DMG_ARROW);
    push_player(e->vx, e->vz, 0.4f);
    e->type = E_NONE;
    return;
  }
  /* a mob in the way of the player's */
  if (e->item.aux)
    for (int i = 0; i < N_ENT; i++) {
      Entity *m = &ents[i];
      if (m->type < E_ZOMBIE || m->type > E_CHICKEN || m->state == 255) continue;
      float w = info[m->type].w / 2 + 0.15f, mx = m->x - nx, mz = m->z - nz;
      if (fabsf(mx) < w && fabsf(mz) < w && ny > m->y - 0.1f && ny < m->y + info[m->type].h) {
        /* EntityArrow: damage 2 x speed, rounded up (a full draw sometimes more) */
        float dmg = arrow ? ceilf(sp * 2) + (sp > 2.9f ? rnd((int)(sp * 2) / 2 + 2) : 0) : 0;
        hurt(m, dmg, e->vx, e->vz);
        if (!arrow && dmg == 0) m->vx += e->vx * 0.2f, m->vz += e->vz * 0.2f;
        e->type = E_NONE;
        return;
      }
    }
  int b = world_get(ifl(nx), ifl(ny), ifl(nz));
  if (blk_flags[b] & BF_SOLID) {
    if (!arrow) {
      /* EntityEgg: one in eight hatches a chick (a chicken here) */
      if (e->item.id == I_EGG && rnd(8) == 0) spawn(E_CHICKEN, e->x, e->y, e->z);
      e->type = E_NONE;
      return;
    }
    e->state = 1;
    e->vx = e->vy = e->vz = 0;
    return;
  }
  e->x = nx, e->y = ny, e->z = nz;
  bool water = blk_model[b] == M_LIQUID;
  float drag = water ? 0.8f : 0.99f;
  e->vx *= drag, e->vy *= drag, e->vz *= drag;
  e->vy -= arrow ? 0.05f : 0.03f;
  if (++e->timer > 400) e->type = E_NONE;
}

/* lit TNT: falls, flashes, and blows with power 4 after 80 ticks (EntityTNTPrimed) */
static void tnt_tick(Entity *e) {
  float p[3] = {e->x, e->y, e->z}, v[3] = {e->vx, e->vy - 0.04f, e->vz};
  bool ground = phys_move(p, v, 0.98f, 0.98f);
  e->x = p[0], e->y = p[1], e->z = p[2];
  e->vx = v[0] * 0.98f, e->vy = v[1] * 0.98f, e->vz = v[2] * 0.98f;
  if (ground) e->vx *= 0.7f, e->vz *= 0.7f, e->vy *= -0.5f;
  if (--e->delay <= 0) {
    e->type = E_NONE;
    explode(e->x, e->y + 0.49f, e->z, 4, true);
  }
}

/* ---------------------------------------------------------------- a tick */
/* ---------------------------------------------------------------- finding the way */
/* EntitySenses.canSee: nothing opaque between its eyes and the player's */
static bool sees(const Entity *e) {
  float x = e->x, y = e->y + info[e->type].h * 0.85f, z = e->z;
  float dx = pl.x - x, dy = pl.y + 1.62f - y, dz = pl.z - z, d = sqrtf(dx * dx + dy * dy + dz * dz);
  int n = (int)(d / 0.4f);
  for (int k = 1; k < n; k++) {
    float t = (float)k / n;
    if (blk_flags[world_get(ifl(x + dx * t), ifl(y + dy * t), ifl(z + dz * t))] & BF_OPAQUE) return false;
  }
  return true;
}
/* where a mob as tall as h can stand, going from height y onto the column (x, z): up a step, level,
 * or down up to three (PathFinder.getSafePoint); -1: nowhere */
static bool clear(int b) { return !(blk_flags[b] & BF_SOLID) && !is_lava(b) && b != B_FIRE && b != B_CACTUS; }
static int stand_at(int x, int y, int z, int tall) {
  for (int k = 0; k < 5; k++) {
    int ny = k == 0 ? y + 1 : y - k + 1;
    int below = world_get(x, ny - 1, z);
    bool water = is_water(world_get(x, ny, z));
    if (!clear(world_get(x, ny, z)) || (tall && !clear(world_get(x, ny + 1, z)))) {
      if (k > 1) return -1;   /* (going down: something in the way) */
      continue;
    }
    if (k == 0 && !clear(world_get(x, y + 1 + tall, z))) continue;   /* (no room over its head to step up) */
    if (water || ((blk_flags[below] & BF_SOLID) && blk_model[below] != M_FENCE && !is_lava(below) && below != B_CACTUS))
      return ny;
  }
  return -1;
}
/* PathFinder: the way to its goal over the blocks about it (A*, four ways from each); the nearest it can
 * get if it cannot get there. It steers to the end of the way's first straight */
#define PG 32
typedef struct { uint8_t y, g, from; } PNode;   /* from: the way it came (4: the start), +8 done */
static void find_way(Entity *e, float h) {
  PNode *n = (PNode *)world_scratch(PG * PG * sizeof(PNode) + PG * PG * 2);
  if (!n) return;
  uint16_t *heap = (uint16_t *)(n + PG * PG);
  memset(n, 0, PG * PG * sizeof(PNode));
  int x0 = ifl(e->x) - PG / 2, z0 = ifl(e->z) - PG / 2, tall = h > 1;
  int tx = e->tx - x0, tz = e->tz - z0;
  tx = tx < 0 ? 0 : tx >= PG ? PG - 1 : tx, tz = tz < 0 ? 0 : tz >= PG ? PG - 1 : tz;
  int s = PG / 2 * PG + PG / 2, nh = 0, best = s, bestd = 1 << 20;
  n[s].y = (uint8_t)ifl(e->y + 0.01f), n[s].from = 4;
  heap[nh++] = (uint16_t)s;
  static const int8_t DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
  #define F(i) (n[i].g + abs((i) % PG - tx) + abs((i) / PG - tz))
  for (int done = 0; nh && done < 300;) {
    /* the open one nearest by its cost and what is left (a heap) */
    int c = heap[0];
    heap[0] = heap[--nh];
    for (int i = 0;;) {
      int l = 2 * i + 1, r = l + 1, m = i;
      if (l < nh && F(heap[l]) < F(heap[m])) m = l;
      if (r < nh && F(heap[r]) < F(heap[m])) m = r;
      if (m == i) break;
      uint16_t t = heap[i];
      heap[i] = heap[m], heap[m] = t, i = m;
    }
    if (n[c].from & 8) continue;
    n[c].from |= 8, done++;
    int cx = c % PG, cz = c / PG, d = abs(cx - tx) + abs(cz - tz);
    if (d < bestd) bestd = d, best = c;
    if (!d) break;
    for (int k = 0; k < 4; k++) {
      int nx = cx + DX[k], nz = cz + DZ[k];
      if (nx < 0 || nx >= PG || nz < 0 || nz >= PG) continue;
      int i = nz * PG + nx;
      if (n[i].from & 8) continue;
      int ny = stand_at(x0 + nx, n[c].y, z0 + nz, tall);
      if (ny < 1 || ny > 255) continue;
      int g = n[c].g + 1 + is_water(world_get(x0 + nx, ny, z0 + nz));
      if (g > 250 || (n[i].y && n[i].g <= g)) continue;
      n[i].y = (uint8_t)ny, n[i].g = (uint8_t)g, n[i].from = (uint8_t)k;
      if (nh < PG * PG) {
        int j = nh++;
        heap[j] = (uint16_t)i;
        while (j && F(heap[(j - 1) / 2]) > F(heap[j])) {
          uint16_t t = heap[j];
          heap[j] = heap[(j - 1) / 2], heap[(j - 1) / 2] = t, j = (j - 1) / 2;
        }
      }
    }
  }
  #undef F
  /* the way, from its end back to the start (in the heap's room); then from the start, as far as its
   * first straight goes */
  int len = 0;
  for (int c = best; c != s && len < PG * PG; c = (c % PG - DX[n[c].from & 7]) + (c / PG - DZ[n[c].from & 7]) * PG)
    heap[len++] = (uint16_t)c;
  if (!len) {
    e->gx = e->x, e->gz = e->z;
    return;
  }
  int k0 = n[heap[len - 1]].from & 7, end = len - 1;
  while (end > 0 && (n[heap[end - 1]].from & 7) == k0) end--;
  e->gx = x0 + heap[end] % PG + 0.5f, e->gz = z0 + heap[end] / PG + 0.5f;
}
/* RandomPositionGenerator.findRandomTarget: of ten places within 10 blocks, the one it likes best */
static void wander_to(Entity *e) {
  int best = -1000;
  for (int k = 0; k < 10; k++) {
    int x = ifl(e->x) + rnd(21) - 10, z = ifl(e->z) + rnd(21) - 10, y = ifl(e->y) + rnd(7) - 3;
    if (!world_loaded(x, y, z)) continue;
    int w = hostile(e->type) ? -light_there(x, y, z) : world_get(x, y - 1, z) == B_GRASS ? 10 : light_there(x, y, z) - 8;
    if (w > best) best = w, e->tx = (int16_t)x, e->tz = (int16_t)z;
  }
}

static float wrap(float a) {
  while (a > 180) a -= 360;
  while (a < -180) a += 360;
  return a;
}

void mob_tick(Entity *e) {
  if (e->type == E_ARROW) {
    arrow_tick(e);
    return;
  }
  if (e->type == E_TNT) {
    tnt_tick(e);
    return;
  }
  const MobInfo *mi = &info[e->type];
  if (e->hurt > 0) e->hurt--;
  if (e->invuln > 0) e->invuln--;
  if (e->state == 255) {
    if (++e->timer >= 20) {
      loot(e);
      e->type = E_NONE;
    }
    e->vx *= 0.5f, e->vz *= 0.5f;
  }
  float dx = pl.x - e->x, dz = pl.z - e->z, dy = pl.y - e->y;
  float dist = sqrtf(dx * dx + dz * dz);
  /* far away: gone (EntityLiving.despawnEntity) */
  if (hostile(e->type) && dist > 32) {
    e->type = E_NONE;
    return;
  }
  int bx = ifl(e->x), by = ifl(e->y), bz = ifl(e->z);
  bool in_water = blk_model[world_get(bx, ifl(e->y + 0.4f), bz)] == M_LIQUID;
  bool in_lava = is_lava(world_get(bx, ifl(e->y + 0.4f), bz));
  /* daylight burns zombies and skeletons (EntityZombie.onLivingUpdate) */
  if ((e->type == E_ZOMBIE || e->type == E_SKELETON) && daylight() && !in_water &&
      light_there(bx, ifl(e->y + 1.6f), bz) >= 15 && world_get(bx, ifl(e->y + 1.6f), bz) == B_AIR) {
    if (e->fire < 160) e->fire = 160;
  }
  if (in_lava) e->fire = 300, hurt(e, 4, 0, 0);
  if (world_get(bx, by, bz) == B_FIRE || world_get(bx, ifl(e->y + 1), bz) == B_FIRE) {
    hurt(e, 1, 0, 0);
    if (e->fire < 160) e->fire = 160;
  }
  if (in_water || rain_at(bx, by, bz) || rain_at(bx, ifl(e->y + 1.6f), bz)) e->fire = 0;   /* Entity.isWet */
  if (e->fire > 0) {
    if (e->fire % 20 == 0) hurt(e, 1, 0, 0);
    e->fire--;
  }
  if (e->panic > 0) e->panic--;
  if (e->growth < 0) e->growth++;
  /* (animals: the time before breeding again; in love, how long it has been by its mate) */
  if (e->timer > 0 && !hostile(e->type) && e->state != 255 && !e->love) e->timer--;
  if (e->love > 0) {
    e->love--;
    /* EntityAIMate: to the nearest other one in love; close together for 3 seconds: a baby */
    for (int i = 0; i < N_ENT; i++) {
      Entity *o = &ents[i];
      if (o == e || o->type != e->type || o->love <= 0 || o->state == 255) continue;
      float mx = o->x - e->x, mz = o->z - e->z;
      if (mx * mx + mz * mz > 64) continue;
      e->gx = o->x, e->gz = o->z;
      if (mx * mx + mz * mz < 2.5f && ++e->timer >= 60) {
        Entity *b = spawn(e->type, e->x, e->y, e->z);
        if (b) b->growth = -24000;
        e->love = o->love = 0;
        e->timer = o->timer = 6000;
        player_add_xp(1 + rnd(7));
      }
      break;
    }
  }
  if (e->delay > 0 && e->type != E_CREEPER) e->delay--;
  /* what it wants (EntityAITasks, by priority): a hostile mob the player it saw lately or that hit it;
   * an animal running when hit, going to its mate, after the food in the player's hand; then looking
   * about and wandering */
  bool chase = false, go = false, look = false;
  float speed = mi->speed;
  if (e->state != 255 && hostile(e->type)) {
    bool can = opt.difficulty > 0 && !pl.dead && pl.mode == 0;
    /* EntityAINearestAttackableTarget: seen within 16 (a spider only in the dark); EntityAITarget: kept
     * while seen in the last 3 seconds, within 16 (zombies 35) */
    bool dark = e->type != E_SPIDER || light_there(bx, by + 1, bz) < 8 || !daylight();
    if (can && dist < 16 && (dark || e->panic > 60) && (ticks_run + (unsigned)(e - ents)) % 4 == 0 && sees(e))
      e->panic = e->panic > 60 ? e->panic : 60;
    chase = can && e->panic > 0 && dist < (e->type == E_ZOMBIE ? 35 : 16);
    if (!can) e->panic = 0;
  }
  if (chase) {
    e->tx = (int16_t)ifl(pl.x), e->tz = (int16_t)ifl(pl.z);
    go = true;
    if (e->type == E_SKELETON && dist < 10 && sees(e)) go = false;   /* EntityAIArrowAttack: in range, it stands */
    if (e->type == E_CREEPER) {
      /* EntityAICreeperSwell: within 3, it stops and swells; past 7, or out of sight, it lets go */
      if (dist < 3 && fabsf(dy) < 3) e->delay++, go = false;
      else if (e->delay > 0 && dist > 7) e->delay--;
      else if (e->delay > 0) e->delay++, go = dist > 2;
      if (e->delay >= 30) {
        e->type = E_NONE;
        explode(e->x, e->y + 0.85f, e->z, opt.difficulty ? 3 : 0, rule(GR_MOB_GRIEFING));
        return;
      }
    }
  } else if (e->state != 255) {
    if (e->type == E_CREEPER && e->delay > 0) e->delay--;
    int food = food_of(e->type);
    if (e->panic > 0 && !hostile(e->type)) {
      /* EntityAIPanic: somewhere within 5 blocks, fast, again and again */
      if (e->panic % 20 == 19 || (ifl(e->x) == e->tx && ifl(e->z) == e->tz))
        e->tx = (int16_t)(ifl(e->x) + rnd(11) - 5), e->tz = (int16_t)(ifl(e->z) + rnd(11) - 5);
      go = true;
      speed *= e->type == E_COW ? 2.0f : e->type == E_CHICKEN ? 1.4f : 1.25f;
    } else if (e->love > 0 && e->gx != e->x) {
      go = true;   /* (to its mate: set above) */
      e->tx = (int16_t)ifl(e->gx), e->tz = (int16_t)ifl(e->gz);
    } else if (food && held()->id == food && dist < 10 && !pl.dead) {
      /* EntityAITempt: after the player holding its food, up to 2.5 blocks away, looking at them */
      e->tx = (int16_t)ifl(pl.x), e->tz = (int16_t)ifl(pl.z);
      go = dist > 2.5f, look = true;
    } else {
      /* EntityAIWander: one in 120 ticks somewhere within 10 blocks (RandomPositionGenerator: of ten
       * tries, for an animal grass, for a monster the darkest); EntityAIWatchClosest: the player near */
      if (rnd(120) == 0) wander_to(e);
      int tx = e->tx - ifl(e->x), tz = e->tz - ifl(e->z);
      go = tx * tx + tz * tz > 0;
      look = !go && dist < 6 && !pl.dead;
    }
  }
  if (go) {
    /* the way there, now and then or at each turn (PathNavigate): steer to its next turn */
    float gx = e->gx - e->x, gz = e->gz - e->z;
    if ((ticks_run + (unsigned)(e - ents)) % 10 == 0 || gx * gx + gz * gz < 0.09f) find_way(e, mi->h);
    gx = e->gx - e->x, gz = e->gz - e->z;
    if (gx * gx + gz * gz < 0.04f) {
      go = false;
      if (!chase) e->tx = (int16_t)ifl(e->x), e->tz = (int16_t)ifl(e->z);   /* (there) */
    } else {
      float want = atan2f(-gx, gz) * 57.29578f;
      float turn = wrap(want - e->yaw);
      if (turn > 30) turn = 30;
      if (turn < -30) turn = -30;
      e->yaw += turn;
      /* do not walk off a cliff or into lava or fire (the way avoids them; a push may not) */
      float yr = e->yaw * 0.017453292f;
      int ax = ifl(e->x - sinf(yr) * (mi->w / 2 + 0.4f)), az = ifl(e->z + cosf(yr) * (mi->w / 2 + 0.4f));
      bool ground = false;
      for (int k = 1; k <= 4 && !ground; k++) ground = (blk_flags[world_get(ax, by - k + 1, az)] & BF_SOLID) != 0;
      int ahead = world_get(ax, by, az);
      if ((!ground && !in_water) || is_lava(ahead) || ahead == B_FIRE) go = false, e->gx = e->x, e->gz = e->z;
    }
  }
  if (look && !chase) {
    /* EntityLookHelper: turned towards the player, the head up or down to their eyes */
    float turn = wrap(atan2f(-dx, dz) * 57.29578f - e->yaw);
    e->yaw += turn > 10 ? 10 : turn < -10 ? -10 : turn;
    e->pitch = -atan2f(dy + 1.62f - mi->h * 0.85f, dist) * 57.29578f;
  }
  if (chase) {
    float want = atan2f(-dx, dz) * 57.29578f;
    float turn = wrap(want - e->yaw);
    if (fabsf(turn) < 60) e->yaw += turn * 0.5f;
    e->pitch = -atan2f(dy + 1.0f - mi->h * 0.85f, dist) * 57.29578f;
  } else if (!look)
    e->pitch *= 0.8f;
  /* moving: EntityLivingBase.moveEntityWithHeading, the AI's speed twice (as forward and as friction) */
  float fwd = go && e->state != 255 ? speed : 0;
  float yr = e->yaw * 0.017453292f;
  if (in_water) {
    if (rndf() < 0.8f) e->vy += 0.04f;   /* EntityAISwimming */
    e->vx += -sinf(yr) * fwd * 0.02f / (fwd > 0 ? fwd : 1);
    e->vz += cosf(yr) * fwd * 0.02f / (fwd > 0 ? fwd : 1);
  } else {
    float fr = e->on_ground ? 0.546f : 0.91f;
    float f = e->on_ground ? fwd * (0.16277136f / (fr * fr * fr)) : 0.02f;
    e->vx += -sinf(yr) * fwd * f;
    e->vz += cosf(yr) * fwd * f;
  }
  float p[3] = {e->x, e->y, e->z}, v[3] = {e->vx, e->vy, e->vz};
  float w = mi->w;
  bool ground = phys_move(p, v, w, mi->h);
  bool blocked = (v[0] != e->vx) || (v[2] != e->vz);
  float moved = sqrtf((p[0] - e->x) * (p[0] - e->x) + (p[2] - e->z) * (p[2] - e->z));
  e->x = p[0], e->y = p[1], e->z = p[2];
  e->vx = v[0], e->vy = v[1], e->vz = v[2];
  e->on_ground = ground;
  if (blocked && go && e->type == E_SPIDER) e->vy = 0.2f;   /* climbing */
  if (in_water) e->vx *= 0.8f, e->vy *= 0.8f, e->vz *= 0.8f, e->vy -= 0.02f;
  else {
    e->vy -= 0.08f;
    e->vy *= 0.98f;
    float fr = ground ? 0.546f : 0.91f;
    e->vx *= fr, e->vz *= fr;
    if (e->type == E_CHICKEN && !ground && e->vy < 0) e->vy *= 0.6f;   /* flapping */
  }
  /* against a wall: a jump (EntityJumpHelper), set for the next move as Minecraft sets it before
   * moving, so the first rise is the whole 0.42 and a block is climbed */
  if (blocked && go && ground && e->type != E_SPIDER) e->vy = 0.42f;
  /* the walk's swing (limbSwingAmount follows how fast it moves) */
  float target = moved * 4;
  if (target > 1) target = 1;
  e->limb_amt += (target - e->limb_amt) * 0.4f;
  e->limb += e->limb_amt;
  if (e->state == 255 || e->type == E_CREEPER || e->type == E_SKELETON) {}
  /* attacks */
  if (chase && e->state != 255) {
    float reach = mi->w * 2 * mi->w * 2 + 0.6f;
    if ((e->type == E_ZOMBIE || e->type == E_SPIDER) && dist * dist < reach + 0.5f && fabsf(dy) < 1.5f && !e->delay) {
      player_hurt(scaled(e->type == E_ZOMBIE ? 3 : 2), DMG_MOB);
      push_player(dx, dz, 0.4f);
      e->delay = 20;
    }
    /* a spider leaps (EntityAILeapAtTarget) */
    if (e->type == E_SPIDER && ground && dist > 2 && dist < 4 && rnd(5) == 0) {
      e->vx += dx / dist * 0.4f;
      e->vz += dz / dist * 0.4f;
      e->vy = 0.4f;
    }
    if (e->type == E_SKELETON && dist < 15 && !e->delay) {
      shoot(e);
      e->delay = (int16_t)(20 + dist / 15 * 40);
    }
  }
  if (e->y < -64) e->type = E_NONE;
}
