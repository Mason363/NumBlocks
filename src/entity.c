/* Entities: dropped items (EntityItem) here, mobs and arrows in mob.c.
 *
 * An item falls (0.04 a tick, drag 0.98), slides (0.6 x 0.98 on the ground),
 * bounces a little, joins the same items near it, can be picked up after 10
 * ticks (40 when the player threw it) and disappears after 5 minutes. */
#include <math.h>
#include "nb.h"
#include "mob.h"

Entity ents[N_ENT];

/* ---------------------------------------------------------------- particles (EntityDiggingFX, EntityRainFX) */
Particle parts[N_PART];

/* breaking a block: its bits, coloured like its texture, fly out and fall (EffectRenderer.addBlockDestroyEffects) */
void particles_break(int x, int y, int z, int b) {
  int tex = blk_tex[b][2];
  for (int k = 0; k < 12; k++) {
    Particle *p = &parts[rnd(N_PART)];
    int u = rnd(16), v = rnd(16);
    uint8_t q = tex_px[tex][(v * 16 + u) >> 1];
    int i = (u & 1) ? q >> 4 : q & 15;
    if (!i && (tex_flags[tex] & 0x20)) continue;
    p->c = tex_pal[tex][i];
    if (i >= (tex_flags[tex] & 0x1F)) p->c = 0x5CA9;   /* (tinted: a grass green) */
    p->x = x + 0.2f + rndf() * 0.6f, p->y = y + 0.2f + rndf() * 0.6f, p->z = z + 0.2f + rndf() * 0.6f;
    p->vx = (p->x - x - 0.5f) * 0.3f + (rndf() - 0.5f) * 0.1f;
    p->vy = (p->y - y - 0.5f) * 0.3f + 0.1f + rndf() * 0.1f;
    p->vz = (p->z - z - 0.5f) * 0.3f + (rndf() - 0.5f) * 0.1f;
    p->age = 0;
    p->life = (uint8_t)(4 / (rndf() * 0.9f + 0.1f));
  }
}

/* a free particle, or else any */
static Particle *part_slot(void) {
  for (int i = 0; i < N_PART; i++)
    if (parts[i].age >= (parts[i].life & P_LIFE)) return &parts[i];
  return &parts[rnd(N_PART)];
}

void particle_add(float x, float y, float z, float vx, float vy, float vz, uint16_t c, int life) {
  Particle *p = part_slot();
  *p = (Particle){x, y, z, vx, vy, vz, c, 0, (uint8_t)life};
}

/* EntityRenderer.addRainParticles: drops splash on whatever the rain falls on
 * within 10 blocks, up to 100 a tick in a full downpour (half with Fast
 * graphics); here only as many as there are free particles */
static void rain_splashes(void) {
  float f = opt.fancy ? rain_str : rain_str / 2;
  int n = (int)(100 * f * f), px = (int)floorf(pl.x), py = (int)floorf(pl.y), pz = (int)floorf(pl.z);
  for (int i = 0; i < N_PART && n > 0; i++) {
    Particle *p = &parts[i];
    if (p->age < (p->life & P_LIFE)) continue;
    /* a free one: look for where it lands (a few tries, as most of the 100 would) */
    for (int k = 0; k < 4 && n > 0; k++, n--) {
      int x = px + rnd(10) - rnd(10), z = pz + rnd(10) - rnd(10), y = world_rain_top(x, z);
      if (y > py + 10 || y < py - 10 || !world_loaded(x, y - 1, z)) continue;
      int lx = x - vc_x0, lz = z - vc_z0, below = world_get(x, y - 1, z);
      if (biome_rain[vbiome[lz * VCX + lx]] != 1 || temp_at(x, y, z) < 0.15f || below == B_AIR || is_lava(below)) continue;
      float b[6], top = (float)y;
      if (block_box(x, y - 1, z, b)) top = b[4];
      p->x = x + rndf(), p->y = top + 0.1f, p->z = z + rndf();
      p->vx = (rndf() - 0.5f) * 0.03f, p->vz = (rndf() - 0.5f) * 0.03f, p->vy = rndf() * 0.2f + 0.1f;
      p->c = 0x1A59;   /* (24, 72, 204), the splash sprites' blue */
      p->age = 0;
      p->life = (uint8_t)(RAIN_DROP | (int)(8 / (rndf() * 0.8f + 0.2f)));
      break;
    }
  }
}

static void particles_tick(void) {
  if (rain_str > 0) rain_splashes();
  for (int i = 0; i < N_PART; i++) {
    Particle *p = &parts[i];
    bool drop = p->life & RAIN_DROP;
    if (p->age >= (p->life & P_LIFE)) continue;
    p->age++;
    p->vy -= p->life & P_FLOAT ? 0 : drop ? 0.06f : 0.04f;
    float np[3] = {p->x, p->y, p->z}, v[3] = {p->vx, p->vy, p->vz};
    bool ground = phys_move(np, v, 0.1f, 0.1f);
    p->x = np[0], p->y = np[1], p->z = np[2];
    p->vx = v[0] * 0.98f, p->vy = v[1] * 0.98f, p->vz = v[2] * 0.98f;
    if (ground) {
      p->vx *= 0.7f, p->vz *= 0.7f;
      if (drop && rnd(2)) p->age = p->life & P_LIFE;   /* half the drops are gone when they land */
    }
  }
}

Entity *ent_new(int type, float x, float y, float z) {
  Entity *e = NULL;
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type == E_NONE) {
      e = &ents[i];
      break;
    }
  if (!e) {
    /* full: the oldest item makes room */
    int best = -1;
    for (int i = 0; i < N_ENT; i++)
      if (ents[i].type == E_ITEM && (best < 0 || ents[i].age > ents[best].age)) best = i;
    if (best < 0) return NULL;
    e = &ents[best];
  }
  memset(e, 0, sizeof *e);
  e->type = (uint8_t)type;
  e->x = e->px = x;
  e->y = e->py = y;
  e->z = e->pz = z;
  e->gx = x, e->gz = z;
  if (type >= E_ZOMBIE && type <= E_CHICKEN) e->tx = (int16_t)floorf(x), e->tz = (int16_t)floorf(z);   /* (going nowhere yet) */
  return e;
}

/* Block.spawnAsEntity / EntityPlayer.dropItem */
Entity *ent_drop(int id, int count, int dmg, float x, float y, float z, bool thrown) {
  if (!id || count <= 0) return NULL;
  Entity *e = ent_new(E_ITEM, x, y, z);
  if (!e) return NULL;
  e->item.id = (uint16_t)id;
  e->item.aux = (uint16_t)(item_dur(id) ? dmg : count);
  if (thrown) {
    /* EntityPlayer.dropItem: 0.3 forwards, a little up, a little at random */
    float yaw = pl.yaw * 0.017453292f, pitch = pl.pitch * 0.017453292f;
    e->vx = -sinf(yaw) * cosf(pitch) * 0.3f;
    e->vz = cosf(yaw) * cosf(pitch) * 0.3f;
    e->vy = -sinf(pitch) * 0.3f + 0.1f;
    float a = rndf() * 6.2831853f, f = 0.02f * rndf();
    e->vx += cosf(a) * f;
    e->vy += (rndf() - rndf()) * 0.1f;
    e->vz += sinf(a) * f;
    e->delay = 40;
  } else {
    e->vx = rndf() * 0.2f - 0.1f;
    e->vy = 0.2f;
    e->vz = rndf() * 0.2f - 0.1f;
    e->delay = 10;
  }
  e->yaw = rndf() * 360;
  return e;
}

static void item_tick(Entity *e) {
  float p[3] = {e->x, e->y, e->z}, v[3] = {e->vx, e->vy - 0.04f, e->vz};
  /* stuck in a block: pushed up out of it */
  float b[6];
  if (block_box((int)floorf(e->x), (int)floorf(e->y + 0.125f), (int)floorf(e->z), b) && e->y + 0.125f < b[4])
    v[1] = 0.1f;
  bool ground = phys_move(p, v, 0.25f, 0.25f);
  e->x = p[0], e->y = p[1], e->z = p[2];
  float f = ground ? 0.6f * 0.98f : 0.98f;
  e->vx = v[0] * f;
  e->vy = v[1] * 0.98f;
  e->vz = v[2] * f;
  if (ground) e->vy *= -0.5f;
  e->on_ground = ground;
  if (e->delay > 0) e->delay--;
  if (e->age >= 6000) {
    e->type = E_NONE;
    return;
  }
  /* EntityItem.searchForOtherItemsNearby: the same items within half a block join */
  if ((e->age & 7) == 0 && !item_dur(e->item.id)) {
    for (int i = 0; i < N_ENT; i++) {
      Entity *o = &ents[i];
      if (o == e || o->type != E_ITEM || o->item.id != e->item.id) continue;
      if (fabsf(o->x - e->x) > 0.75f || fabsf(o->y - e->y) > 0.5f || fabsf(o->z - e->z) > 0.75f) continue;
      int room = item_max(e->item.id) - e->item.aux;
      if (room <= 0) break;
      int k = o->item.aux < room ? o->item.aux : room;
      e->item.aux = (uint16_t)(e->item.aux + k);
      o->item.aux = (uint16_t)(o->item.aux - k);
      if (!o->item.aux) o->type = E_NONE;
      if (o->delay > e->delay) e->delay = o->delay;
    }
  }
  /* picked up: the player's box grown by 1, 0.5, 1 touches it */
  if (!e->delay && !pl.dead && fabsf(e->x - pl.x) < 0.3f + 0.125f + 1 && e->y + 0.25f > pl.y - 0.5f &&
      e->y < pl.y + 1.8f + 0.5f && fabsf(e->z - pl.z) < 0.3f + 0.125f + 1) {
    int left = inv_add(pl.inv, 36, e->item.id, item_count(&e->item), item_dur(e->item.id) ? e->item.aux : 0);
    if (!left) e->type = E_NONE;
    else if (!item_dur(e->item.id)) e->item.aux = (uint16_t)left;
  }
}

/* ---------------------------------------------------------------- fishing (EntityFishHook) */
/* The hook flies, then floats where the water holds half of it. Under the
 * open sky (faster in rain, slower under a roof), a fish is on its way after
 * 5 to 45 seconds; its wake comes towards the hook for 1 to 4 seconds, the
 * hook dips, and for half a second to a second and a half reeling in brings
 * it out. Fields: timer the bite's time left (ticksCatchable), delay the
 * fish's way in (ticksCatchableDelay), panic the wait (ticksCaughtDelay), yaw
 * the way the fish comes, love the creature hooked (its index + 1). */
#define WAKE 0x9DBF   /* light blue, as the splash sprites look on water */

Entity *bobber(void) {
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type == E_BOBBER) return &ents[i];
  return NULL;
}

static float gauss(void) { return (rndf() + rndf() + rndf() + rndf() - 2) * 1.7320508f; }

void fish_cast(void) {
  float yaw = pl.yaw * 0.017453292f, pitch = pl.pitch * 0.017453292f;
  Entity *e = ent_new(E_BOBBER, pl.x - cosf(yaw) * 0.16f, pl.y + 1.62f - 0.1f, pl.z - sinf(yaw) * 0.16f);
  if (!e) return;
  /* 0.4 the way the player looks, then (handleHookCasting) 1.5 times that, a little off */
  float v[3] = {-sinf(yaw) * cosf(pitch), -sinf(pitch), cosf(yaw) * cosf(pitch)};
  for (int k = 0; k < 3; k++) v[k] = (v[k] + gauss() * 0.0075f) * 1.5f;
  e->vx = v[0], e->vy = v[1], e->vz = v[2];
  e->yaw = 0;
}

/* EntityFishHook.getFishingResult: 10% junk, 5% treasure, 85% fish (weights as in 1.8;
 * the junk and treasure NumBlocks does not have are left out) */
static Entity *fish_catch(float x, float y, float z) {
  static const uint16_t junk[][3] = {{I_LEATHER_BOOTS, 10, 1}, {I_LEATHER, 10, 1}, {I_BONE, 10, 1}, {I_STRING, 5, 1},
                                     {I_FISHING_ROD, 2, 1}, {I_BOWL, 10, 1}, {I_STICK, 5, 1}, {I_DYE_BLACK, 1, 10},
                                     {I_ROTTEN_FLESH, 10, 1}};
  static const uint16_t treasure[][3] = {{B_LILY_PAD, 1, 1}, {I_BOW, 1, 1}, {I_FISHING_ROD, 1, 1}, {I_BOOK, 1, 1}};
  static const uint16_t fish[][3] = {{I_RAW_FISH, 60, 1}, {I_RAW_SALMON, 25, 1}, {I_CLOWNFISH, 2, 1},
                                     {I_PUFFERFISH, 13, 1}};
  float f = rndf();
  const uint16_t(*t)[3] = f < 0.1f ? junk : f < 0.15f ? treasure : fish;
  int n = f < 0.1f ? 9 : 4, sum = 0;
  for (int i = 0; i < n; i++) sum += t[i][1];
  int r = rnd(sum), i = 0;
  while (r >= t[i][1]) r -= t[i++][1];
  int id = t[i][0], dmg = 0, max = item_dur(id);
  if (max) {
    /* WeightedRandomFishable: worn, up to 90% (junk) or 25% (treasure) */
    int most = (int)((f < 0.1f ? 0.9f : 0.25f) * max);
    dmg = max - rnd(rnd(most + 1) + 1);
    dmg = dmg > most ? most : dmg < 1 ? 1 : dmg;
  }
  return ent_drop(id, t[i][2], dmg, x, y, z, false);
}

int fish_reel(Entity *e) {
  int wear = 0;
  Entity *c = e->love ? &ents[e->love - 1] : NULL;
  float dx = pl.x - e->x, dy = pl.y - e->y, dz = pl.z - e->z, d = sqrtf(dx * dx + dy * dy + dz * dz);
  if (c && c->type != E_NONE) {
    /* a creature: pulled in */
    c->vx += (pl.x - c->x) * 0.1f, c->vy += (pl.y - c->y) * 0.1f + sqrtf(d) * 0.08f, c->vz += (pl.z - c->z) * 0.1f;
    wear = 3;
  } else if (e->timer > 0) {
    /* a bite: what it caught flies to the player, and some experience */
    Entity *it = fish_catch(e->x, e->y, e->z);
    if (it) it->vx = dx * 0.1f, it->vy = dy * 0.1f + sqrtf(d) * 0.08f, it->vz = dz * 0.1f, it->delay = 0;
    player_add_xp(rnd(6) + 1);
    wear = 1;
  }
  if (e->on_ground) wear = 2;   /* (stuck in the ground) */
  e->type = E_NONE;
  return wear;
}

static void bobber_tick(Entity *e) {
  float dx = e->x - pl.x, dy = e->y - pl.y, dz = e->z - pl.z;
  if (pl.dead || held()->id != I_FISHING_ROD || dx * dx + dy * dy + dz * dz > 1024) {
    e->type = E_NONE;
    return;
  }
  if (e->love) {
    /* on a creature: it goes where it goes */
    Entity *c = &ents[e->love - 1];
    if (c->type != E_NONE && c->state != 255) {
      e->x = c->x, e->y = c->y + mob_height(c->type) * 0.8f, e->z = c->z;
      return;
    }
    e->love = 0;
  }
  /* flying: does it hit a creature? */
  if (!e->on_ground)
    for (int i = 0; i < N_ENT; i++) {
      Entity *c = &ents[i];
      if (c->type < E_ZOMBIE || c->type > E_CHICKEN || c->state == 255) continue;
      float w = mob_width(c->type) / 2 + 0.125f;
      if (fabsf(c->x - e->x) < w && fabsf(c->z - e->z) < w && e->y + 0.25f > c->y && e->y < c->y + mob_height(c->type)) {
        mob_hooked(c);
        e->love = (int16_t)(i + 1);
        return;
      }
    }
  float p[3] = {e->x, e->y, e->z}, v[3] = {e->vx, e->vy, e->vz};
  bool ground = phys_move(p, v, 0.25f, 0.25f);
  bool wall = (v[0] == 0 && e->vx != 0) || (v[2] == 0 && e->vz != 0);
  e->x = p[0], e->y = p[1], e->z = p[2];
  e->vx = v[0], e->vy = v[1], e->vz = v[2];
  e->on_ground = ground;
  float f6 = ground || wall ? 0.5f : 0.92f;
  /* how much of it is in water: five slices of its 0.25 height */
  float wet = 0;
  for (int k = 0; k < 5; k++) {
    float y = e->y + 0.25f * (k + 0.5f) / 5;
    int bx = (int)floorf(e->x), by = (int)floorf(y), bz = (int)floorf(e->z), b = world_get(bx, by, bz);
    if (is_water(b) && y < by + 1 - (blk_meta[b] < 8 ? blk_meta[b] : 0) / 8.0f) wet += 0.2f;
  }
  if (wet > 0) {
    int bx = (int)floorf(e->x), by = (int)floorf(e->y) + 1, bz = (int)floorf(e->z);
    int l = 1;
    if (rndf() < 0.25f && rain_at(bx, by, bz)) l = 2;
    if (rndf() < 0.5f && world_rain_top(bx, bz) > by) l--;
    float surface = floorf(e->y) + 1.05f;   /* (just above the water) */
    if (e->timer > 0) {
      if (--e->timer <= 0) e->panic = 0, e->delay = 0;
    } else if (e->delay > 0) {
      e->delay = (int16_t)(e->delay - l);
      if (e->delay <= 0) {
        /* the bite: the hook dips, bubbles and a splash */
        e->vy -= 0.2f;
        for (int k = 0; k < 4; k++)
          particle_add(e->x + gauss() * 0.1f, surface - 0.1f, e->z + gauss() * 0.1f, gauss() * 0.05f, 0.1f, gauss() * 0.05f,
                       0xCEFF, P_FLOAT | 8);
        for (int k = 0; k < 6; k++)
          particle_add(e->x + gauss() * 0.1f, surface, e->z + gauss() * 0.1f, gauss() * 0.05f, 0.15f + rndf() * 0.1f,
                       gauss() * 0.05f, WAKE, RAIN_DROP | 12);
        e->timer = (int16_t)(10 + rnd(21));
      } else {
        /* the fish's wake, coming in (its way wanders) */
        e->yaw += gauss() * 4;
        float a = e->yaw * 0.017453292f, sa = sinf(a), ca = cosf(a);
        float wx = e->x + sa * e->delay * 0.1f, wz = e->z + ca * e->delay * 0.1f;
        if (is_water(world_get((int)floorf(wx), (int)floorf(surface) - 1, (int)floorf(wz)))) {
          if (rndf() < 0.15f) particle_add(wx, surface - 0.1f, wz, sa * 0.05f, 0.01f, ca * 0.05f, 0xCEFF, P_FLOAT | 6);
          particle_add(wx, surface, wz, ca * 0.04f, 0.01f, -sa * 0.04f, WAKE, P_FLOAT | (int)(8 / (rndf() * 0.8f + 0.2f)));
          particle_add(wx, surface, wz, -ca * 0.04f, 0.01f, sa * 0.04f, WAKE, P_FLOAT | (int)(8 / (rndf() * 0.8f + 0.2f)));
        }
      }
    } else if (e->panic > 0) {
      e->panic = (int16_t)(e->panic - l);
      /* now and then, more so as the fish nears, a splash somewhere about */
      float ch = 0.15f;
      if (e->panic < 20) ch += (20 - e->panic) * 0.05f;
      else if (e->panic < 40) ch += (40 - e->panic) * 0.02f;
      else if (e->panic < 60) ch += (60 - e->panic) * 0.01f;
      if (rndf() < ch) {
        float a = rndf() * 6.2831853f, r = (25 + rndf() * 35) * 0.1f;
        float sx = e->x + sinf(a) * r, sz = e->z + cosf(a) * r;
        if (is_water(world_get((int)floorf(sx), (int)floorf(surface) - 1, (int)floorf(sz))))
          for (int k = 0; k < 2 + rnd(2); k++)
            particle_add(sx + gauss() * 0.1f, surface, sz + gauss() * 0.1f, 0, 0.1f + rndf() * 0.1f, 0, WAKE, RAIN_DROP | 10);
      }
      if (e->panic <= 0) e->yaw = rndf() * 360, e->delay = (int16_t)(20 + rnd(61));
    } else
      e->panic = (int16_t)(100 + rnd(801));
    if (e->timer > 0) e->vy -= rndf() * rndf() * rndf() * 0.2f;
  }
  e->vy += 0.04f * (wet * 2 - 1);   /* falls in the air, floats in water */
  if (wall) e->vx *= rndf() * 0.2f, e->vz *= rndf() * 0.2f;
  if (wet > 0) f6 *= 0.9f, e->vy *= 0.8f;
  e->vx *= f6, e->vy *= f6, e->vz *= f6;
}

void ents_tick(void) {
  particles_tick();
  for (int i = 0; i < N_ENT; i++) {
    Entity *e = &ents[i];
    if (e->type == E_NONE) continue;
    e->px = e->x, e->py = e->y, e->pz = e->z;
    e->age++;
    /* far outside the loaded blocks: gone */
    if (!world_loaded((int)floorf(e->x), vc_y0, (int)floorf(e->z))) {
      e->type = E_NONE;
      continue;
    }
    if (e->type == E_ITEM) item_tick(e);
    else if (e->type == E_BOBBER) bobber_tick(e);
    else mob_tick(e);
    if (e->y < -64) e->type = E_NONE;
  }
  static int spawn_wait;
  if (++spawn_wait >= 20) {
    spawn_wait = 0;
    if (rule(GR_MOB_SPAWNING)) mobs_spawn();
  }
}
