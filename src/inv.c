/* Items: what they are, stacks, the player's inventory, crafting, smelting
 * and what blocks drop (Minecraft 1.8.8: InventoryPlayer, ShapedRecipes,
 * ShapelessRecipes, the Block*.getItemDropped and quantityDropped methods). */
#include "nb.h"
#pragma GCC optimize("Os")   /* (not where the time goes: small) */

/* ---------------------------------------------------------------- items */
int item_max(int id) {
  if (id <= 0) return 0;
  return id < 256 ? 64 : it_stack[id - 256];
}
int item_dur(int id) { return id >= 256 ? it_dur[id - 256] : 0; }
int item_kind(int id) { return id >= 256 ? it_kind[id - 256] : (id > 0 ? IK_BLOCK : IK_NONE); }
int item_count(const Stack *s) { return !s->id ? 0 : item_dur(s->id) ? 1 : s->aux; }
const char *item_label(int id) {
  if (id >= 256) return it_label[id - 256];
  return id > 0 && blk_label[id] ? blk_label[id] : "";
}
int item_icon(int id) { return id >= 256 ? it_icon[id - 256] : id > 0 ? blk_icon[id] : -1; }
int item_fuel(int id) { return id >= 256 ? it_fuel[id - 256] : id > 0 ? blk_fuel[id] : 0; }

int smelt_of(int id) {
  for (int i = 0; i < N_SMELTING; i++)
    if (smelting[i][0] == id) return smelting[i][1];
  return 0;
}

/* how many of `id` can still go on the stack s (0 if another item) */
static int room(const Stack *s, int id) {
  if (!s->id) return item_max(id);
  if (s->id != id || item_dur(id)) return 0;
  return item_max(id) - s->aux;
}

/* InventoryPlayer.addItemStackToInventory: on stacks of the same item first
 * (hotbar first), then in the first empty slots; what does not fit is left */
int inv_add(Stack *inv, int n, int id, int count, int dmg) {
  if (item_dur(id)) {
    for (int i = 0; i < n && count; i++)
      if (!inv[i].id) inv[i].id = (uint16_t)id, inv[i].aux = (uint16_t)dmg, count--;
    return count;
  }
  for (int pass = 0; pass < 2 && count; pass++)
    for (int i = 0; i < n && count; i++) {
      if (pass == 0 && inv[i].id != id) continue;
      int r = room(&inv[i], id);
      if (r <= 0) continue;
      int k = r < count ? r : count;
      if (!inv[i].id) inv[i].id = (uint16_t)id, inv[i].aux = 0;
      inv[i].aux = (uint16_t)(inv[i].aux + k);
      count -= k;
    }
  return count;
}

/* takes one of the stack (the stack empties at 0) */
void stack_take(Stack *s, int k) {
  if (!s->id) return;
  if (item_dur(s->id) || s->aux <= k) s->id = 0, s->aux = 0;
  else s->aux = (uint16_t)(s->aux - k);
}

/* a tool or armour piece takes damage; false when it breaks */
bool stack_wear(Stack *s, int k) {
  int d = item_dur(s->id);
  if (!d) return true;
  if (s->aux + k > d) {
    s->id = 0, s->aux = 0;
    return false;
  }
  s->aux = (uint16_t)(s->aux + k);
  return true;
}

/* ---------------------------------------------------------------- crafting */
static bool in_group(const uint16_t *g, int id) {
  for (; *g != 0xFFFF; g++)
    if (*g == id) return true;
  return false;
}
static bool fits(int want, int have) {
  if (want == have) return true;
  switch (want) {
    case G_PLANKS: return have < 256 && in_group(group_planks, have);
    case G_WOOL: return have < 256 && in_group(group_wool, have);
    case G_LOG: return have < 256 && in_group(group_log, have);
    case G_SAPLING: return have < 256 && in_group(group_sapling, have);
  }
  return false;
}

/* the recipe the grid (w x w) makes, or -1 (ShapedRecipes.matches, mirrored too; ShapelessRecipes.matches) */
int craft_find(const Stack *grid, int w) {
  /* the grid's used rectangle */
  int x0 = w, y0 = w, x1 = -1, y1 = -1, used = 0;
  for (int y = 0; y < w; y++)
    for (int x = 0; x < w; x++)
      if (grid[y * w + x].id) {
        used++;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
  if (!used) return -1;
  int gw = x1 - x0 + 1, gh = y1 - y0 + 1;
  for (int r = 0; r < N_RECIPES; r++) {
    const Recipe *rc = &recipes[r];
    if (rc->w == 0) {
      /* shapeless: every ingredient matched by a different stack */
      if (rc->h != used) continue;
      bool taken[9] = {0};
      bool ok = true;
      for (int k = 0; k < rc->h && ok; k++) {
        ok = false;
        for (int i = 0; i < w * w; i++)
          if (grid[i].id && !taken[i] && fits(rc->in[k], grid[i].id)) {
            taken[i] = true;
            ok = true;
            break;
          }
      }
      if (ok) return r;
      continue;
    }
    if (rc->w != gw || rc->h != gh) continue;
    for (int mirror = 0; mirror < 2; mirror++) {
      bool ok = true;
      for (int y = 0; y < gh && ok; y++)
        for (int x = 0; x < gw && ok; x++) {
          int want = rc->in[y * rc->w + (mirror ? gw - 1 - x : x)];
          int have = grid[(y0 + y) * w + x0 + x].id;
          ok = want ? have && fits(want, have) : !have;
        }
      if (ok) return r;
    }
  }
  return -1;
}

/* ---------------------------------------------------------------- tools and blocks */
static int tool_kind_of(int b) {
  switch (blk_tool[b]) {
    case T_PICKAXE: return IK_PICKAXE;
    case T_AXE: return IK_AXE;
    case T_SHOVEL: return IK_SHOVEL;
  }
  return -1;
}

/* does the block need the right tool to drop anything (Material.isToolNotRequired false)? */
static bool needs_tool(int b) {
  if (b == B_ICE || b == B_PACKED_ICE) return false;
  return blk_tool[b] == T_PICKAXE || b == B_SNOW || b == B_SNOW_LAYER || b == B_COBWEB;
}

/* EntityPlayer.canHarvestBlock with the held item */
bool can_harvest(int b, int held) {
  if (!needs_tool(b)) return true;
  int k = item_kind(held);
  if (b == B_COBWEB) return k == IK_SWORD || k == IK_SHEARS;
  if (b == B_SNOW || b == B_SNOW_LAYER) return k == IK_SHOVEL;
  if (k != IK_PICKAXE) return false;
  static const uint8_t level[5] = {0, 1, 2, 3, 0};   /* wood stone iron diamond gold */
  return level[it_tier[held - 256]] >= blk_level[b];
}

/* ItemTool.getStrVsBlock and friends: how much faster than a hand */
float dig_speed(int b, int held) {
  static const uint8_t eff[5] = {2, 4, 6, 8, 12};
  int k = item_kind(held);
  if (k == IK_SWORD) {
    if (b == B_COBWEB) return 15;
    int m = blk_model[b];
    return m == M_CROSS || m == M_LEAVES || m == M_VINE || b == B_PUMPKIN || b == B_MELON ? 1.5f : 1;
  }
  if (k == IK_SHEARS) {
    if (b == B_COBWEB || blk_model[b] == M_LEAVES) return 15;
    return b >= B_WOOL_WHITE && b <= B_WOOL_BLACK ? 5 : 1;
  }
  if (k >= IK_PICKAXE && k <= IK_SHOVEL) {
    if (tool_kind_of(b) == k || (k == IK_PICKAXE && (b == B_ICE || b == B_PACKED_ICE))) return eff[it_tier[held - 256]];
  }
  return 1;
}

static uint32_t rng = 0x2545F491;
int rnd(int n) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (int)((rng >> 8) % (uint32_t)n);
}
float rndf(void) { return (float)rnd(1 << 20) / (float)(1 << 20); }

/* what breaking block b with `held` drops: up to 2 stacks, how many */
int block_drops(int b, int held, Stack *out) {
  int n = 0;
  int k = item_kind(held);
  bool shears = k == IK_SHEARS;
#define DROP(id_, c_) do { if ((c_) > 0) out[n].id = (uint16_t)(id_), out[n].aux = (uint16_t)(c_), n++; } while (0)
  if (!can_harvest(b, held)) return 0;
  switch (b) {
    case B_STONE: DROP(B_COBBLESTONE, 1); return n;
    case B_GRASS: case B_GRASS_SNOWED: case B_MYCELIUM: case B_PODZOL: case B_FARMLAND: case B_FARMLAND_WET:
      DROP(B_DIRT, 1);
      return n;
    case B_COAL_ORE: DROP(I_COAL, 1); return n;
    case B_DIAMOND_ORE: DROP(I_DIAMOND, 1); return n;
    case B_EMERALD_ORE: DROP(I_EMERALD, 1); return n;
    case B_LAPIS_ORE: DROP(I_DYE_BLUE, 4 + rnd(5)); return n;
    case B_REDSTONE_ORE: DROP(I_REDSTONE, 4 + rnd(2)); return n;
    case B_GRAVEL: DROP(rnd(10) == 0 ? I_FLINT : B_GRAVEL, 1); return n;
    case B_CLAY: DROP(I_CLAY_BALL, 4); return n;
    case B_GLOWSTONE: DROP(I_GLOWSTONE_DUST, 2 + rnd(3)); return n;
    case B_MELON: DROP(I_MELON_SLICE, 3 + rnd(5)); return n;
    case B_BOOKSHELF: DROP(I_BOOK, 3); return n;
    case B_SNOW: DROP(I_SNOWBALL, 4); return n;
    case B_SNOW_LAYER: DROP(I_SNOWBALL, 1); return n;
    case B_STONE_SLAB_DOUBLE: DROP(B_STONE_SLAB, 2); return n;
    case B_COBWEB: DROP(shears ? B_COBWEB : I_STRING, 1); return n;
    case B_GLASS: case B_GLASS_PANE: case B_ICE: case B_PACKED_ICE: case B_MOB_SPAWNER: case B_MONSTER_EGG_STONE:
    case B_BEDROCK:
      return 0;
    case B_TALL_GRASS: case B_FERN: case B_DOUBLE_GRASS_LOWER: case B_DOUBLE_GRASS_UPPER: case B_LARGE_FERN_LOWER:
    case B_LARGE_FERN_UPPER:
      if (shears) DROP(blk_item[b], b == B_DOUBLE_GRASS_LOWER || b == B_DOUBLE_GRASS_UPPER ? 2 : 1);
      else if (rnd(8) == 0) DROP(I_WHEAT_SEEDS, 1);
      return n;
    case B_DEAD_BUSH: case B_DEAD_SHRUB:
      if (shears) DROP(B_DEAD_BUSH, 1);
      else DROP(I_STICK, rnd(3));
      return n;
    case B_VINE:
      if (shears) DROP(B_VINE, 1);
      return n;
    case B_BROWN_MUSHROOM_CAP: case B_BROWN_MUSHROOM_INSIDE: case B_BROWN_MUSHROOM_STEM:
      DROP(B_BROWN_MUSHROOM, rnd(10) - 7);
      return n;
    case B_RED_MUSHROOM_CAP: case B_RED_MUSHROOM_INSIDE: case B_RED_MUSHROOM_STEM:
      DROP(B_RED_MUSHROOM, rnd(10) - 7);
      return n;
    case B_WHEAT_7:
      DROP(I_WHEAT, 1);
      DROP(I_WHEAT_SEEDS, 1 + rnd(4));   /* 0-3 more, and the one planted */
      return n;
    case B_CARROTS_3: DROP(I_CARROT, 1 + rnd(4)); return n;
    case B_POTATOES_3:
      DROP(I_POTATO, 1 + rnd(4));
      if (rnd(50) == 0) DROP(I_POISONOUS_POTATO, 1);
      return n;
  }
  if (blk_model[b] == M_LEAVES) {
    if (shears) {
      DROP(b, 1);
      return n;
    }
    /* BlockLeaves: a sapling 1 in 20 (jungle 1 in 40), an apple 1 in 200 (oak, dark oak) */
    static const uint16_t sap[6] = {B_SAPLING_OAK, B_SAPLING_SPRUCE, B_SAPLING_BIRCH, B_SAPLING_JUNGLE,
                                    B_SAPLING_ACACIA, B_SAPLING_DARK_OAK};
    int v = b == B_LEAVES_OAK ? 0 : b == B_LEAVES_SPRUCE ? 1 : b == B_LEAVES_BIRCH ? 2 : b == B_LEAVES_JUNGLE ? 3
            : b == B_LEAVES_ACACIA ? 4 : 5;
    if (rnd(v == 3 ? 40 : 20) == 0) DROP(sap[v], 1);
    if ((v == 0 || v == 5) && rnd(200) == 0) DROP(I_APPLE, 1);
    return n;
  }
  if (b == B_BED_HEAD) return 0;   /* the foot drops the bed */
  if (b == B_DOOR_OAK_UPPER) return 0;
  int it = blk_item[b];
  if (it != 0xFFFF) DROP(it, 1);
  return n;
#undef DROP
}
