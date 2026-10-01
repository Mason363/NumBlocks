/* What is drawn over the 3D view, and the screens: the HUD (crosshair,
 * hotbar, hearts, food, armour, air, experience), the inventory, the
 * crafting table, the furnace and the chest, the game menu and the death
 * screen. All drawn strip by strip just before each strip goes to the
 * screen (render.c calls hud_strip), at Minecraft's GUI scale 1, which is
 * what 1.8 picks for a 320 x 240 window.
 *
 * In a container a cursor jumps between slots with the arrows: OK is the
 * left click, EXE the right click, shift + OK the shift click, a digit swaps
 * with that hotbar slot; var or Back closes. */
#include <math.h>
#include <stdlib.h>
#include "nb.h"
#pragma GCC optimize("Os")   /* (not where the time goes: small) */

int gui;
static int clip_y0, clip_y1;   /* the strip: screen rows [clip_y0, clip_y1) */
static uint16_t *clip_buf;
static uint32_t frame_no;

/* ---------------------------------------------------------------- drawing */
static inline uint16_t *row_at(int y) { return clip_buf + (y - clip_y0) * SCREEN_W; }

static void sprite_part(int id, int x, int y, int sx, int sy, int w, int h) {
  int stride = (spr_w[id] + 1) / 2;
  const uint8_t *px = spr_px + spr_off[id];
  const uint16_t *pal = spr_pal[id];
  /* (the rows in the strip, the columns on the screen) */
  int r0 = clip_y0 - y > 0 ? clip_y0 - y : 0, r1 = clip_y1 - y < h ? clip_y1 - y : h;
  int c0 = x < 0 ? -x : 0, c1 = x + w > SCREEN_W ? SCREEN_W - x : w;
  for (int r = r0; r < r1; r++) {
    uint16_t *d = row_at(y + r);
    const uint8_t *s = px + (sy + r) * stride;
    for (int c = c0; c < c1; c++) {
      int u = sx + c, i = (u & 1) ? s[u >> 1] >> 4 : s[u >> 1] & 15;
      if (i) d[x + c] = pal[i];
    }
  }
}
static void sprite(int id, int x, int y) { sprite_part(id, x, y, 0, 0, spr_w[id], spr_h[id]); }

/* a picture kept as runs (data.h: img_*) */
static void image(int id, int x, int y) {
  int w = img_w[id], h = img_h[id];
  const uint16_t *pal = img_pal[id];
  for (int r = 0; r < h; r++) {
    int ty = y + r;
    if (ty < clip_y0 || ty >= clip_y1) continue;
    uint16_t *d = row_at(ty) + x;
    const uint8_t *s = img_rle + img_rows[img_row0[id] + r];
    for (int c = 0; c < w;) {
      int v = *s++, n = (v & 15) + 1, i = v >> 4;
      if (n == 16) n = 16 + *s++;
      if (i)
        for (int k = 0; k < n; k++) d[c + k] = pal[i];
      c += n;
    }
  }
}

static void fill(int x, int y, int w, int h, uint16_t c) {
  for (int r = 0; r < h; r++) {
    int ty = y + r;
    if (ty < clip_y0 || ty >= clip_y1) continue;
    uint16_t *d = row_at(ty);
    for (int k = 0; k < w; k++)
      if ((unsigned)(x + k) < SCREEN_W) d[x + k] = c;
  }
}

/* c towards colour k by a / 32 */
static inline uint16_t blend(uint16_t c, uint16_t k, int a) {
  uint32_t A = (c | (uint32_t)c << 16) & 0x07E0F81F, B = (k | (uint32_t)k << 16) & 0x07E0F81F;
  uint32_t m = (A + (((B - A) * (uint32_t)a) >> 5)) & 0x07E0F81F;
  return (uint16_t)(m | m >> 16);
}
static void tint_rect(int x, int y, int w, int h, uint16_t k, int a) {
  for (int r = 0; r < h; r++) {
    int ty = y + r;
    if (ty < clip_y0 || ty >= clip_y1) continue;
    uint16_t *d = row_at(ty);
    for (int c = 0; c < w; c++)
      if ((unsigned)(x + c) < SCREEN_W) d[x + c] = blend(d[x + c], k, a);
  }
}

/* GuiScreen.drawDefaultBackground: the world behind, darkened (0xC0101010 to 0xD0101010) */
static void dark_background(void) {
  for (int y = clip_y0; y < clip_y1; y++) {
    int a = 24 + (y * 2) / SCREEN_H;
    uint16_t *d = row_at(y);
    for (int x = 0; x < SCREEN_W; x++) d[x] = blend(d[x], 0x1082, a);
  }
}

#define RGB(r, g, b) (uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3)

/* ---------------------------------------------------------------- text (FontRenderer) */
/* symbols the font lacks, for the key sheet: \1 pi, \2 the square root, \3 backspace, \4 times, \5 divided by */
static const uint8_t sym_bits[5][8] = {{0x00, 0x3F, 0x12, 0x12, 0x12, 0x12, 0x21, 0x00},
                                       {0x78, 0x08, 0x08, 0x08, 0x09, 0x0A, 0x04, 0x00},
                                       {0xFC, 0x82, 0xA9, 0x91, 0xA9, 0x82, 0xFC, 0x00},
                                       {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00},
                                       {0x00, 0x04, 0x00, 0x1F, 0x00, 0x04, 0x00, 0x00}};
static const uint8_t sym_w[5] = {7, 8, 9, 6, 6};
static int char_w(int c) { return c >= 1 && c <= 5 ? sym_w[c - 1] : c >= 32 && c < 127 ? font_w[c - 32] : 0; }
int text_width(const char *s) {
  int w = 0;
  for (; *s; s++) w += char_w((unsigned char)*s);
  return w;
}
static void glyphs(const char *s, int x, int y, uint16_t col, int scale) {
  for (; *s; s++) {
    int c = (unsigned char)*s;
    if (!char_w(c)) continue;
    const uint8_t *g = c < 32 ? sym_bits[c - 1] : font_bits + (c - 32) * 8;
    for (int r = 0; r < 8 * scale; r++) {
      int ty = y + r;
      if (ty < clip_y0 || ty >= clip_y1) continue;
      uint8_t bits = g[r / scale];
      if (!bits) continue;
      uint16_t *d = row_at(ty);
      for (int k = 0; k < 8 * scale; k++)
        if (bits & (1 << (k / scale)) && (unsigned)(x + k) < SCREEN_W) d[x + k] = col;
    }
    x += char_w(c) * scale;
  }
}
/* drawStringWithShadow: the shadow is the colour at a quarter, one pixel down and right */
static void text(const char *s, int x, int y, uint16_t col, bool shadow) {
  if (y + 9 <= clip_y0 || y >= clip_y1) return;
  if (shadow) glyphs(s, x + 1, y + 1, (uint16_t)((col >> 2) & 0x39E7), 1);
  glyphs(s, x, y, col, 1);
}
static void text_center(const char *s, int cx, int y, uint16_t col) { text(s, cx - text_width(s) / 2, y, col, true); }
static void text_big(const char *s, int cx, int y, uint16_t col, int scale) {
  int w = text_width(s) * scale;
  if (y + 9 * scale <= clip_y0 || y >= clip_y1) return;
  glyphs(s, cx - w / 2 + scale, y + scale, (uint16_t)((col >> 2) & 0x39E7), scale);
  glyphs(s, cx - w / 2, y, col, scale);
}

static void number(int n, char *buf) {
  char t[12];
  int k = 0;
  if (n < 0) *buf++ = '-', n = -n;
  do t[k++] = (char)('0' + n % 10), n /= 10;
  while (n);
  while (k) *buf++ = t[--k];
  *buf = 0;
}

/* ---------------------------------------------------------------- items (RenderItem.renderItemOverlays) */
static void draw_stack(const Stack *s, int x, int y) {
  if (!s->id) return;
  int ic = item_icon(s->id);
  if (ic >= 0) sprite(ic, x, y);
  int d = item_dur(s->id);
  if (d && s->aux) {
    /* the durability bar: 13 pixels, green to red */
    float f = 1 - s->aux / (float)d;
    int w = (int)roundf(f * 13);
    float hue = f / 3;   /* 0 red .. 1/3 green */
    int r = (int)(255 * (hue < 1.0f / 6 ? 1 : 2 - hue * 6)), g = (int)(255 * (hue < 1.0f / 6 ? hue * 6 : 1));
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    fill(x + 2, y + 13, 13, 2, 0);
    fill(x + 2, y + 13, 12, 1, RGB(r / 4, g / 4, 0));
    fill(x + 2, y + 13, w, 1, RGB(r, g, 0));
  } else if (!d && s->aux > 1) {
    char b[8];
    number(s->aux, b);
    text(b, x + 19 - 2 - text_width(b), y + 6 + 3, 0xFFFF, true);
  }
}

/* ---------------------------------------------------------------- the chat (GuiNewChat) */
/* what was said, oldest first, each: its colour, the tick it came (2 bytes), the text, 0 */
#define CHAT_BUF 512
static char chat[CHAT_BUF];
static int chat_used;
static uint16_t chat_clock;   /* ticks (GuiIngame.updateCounter) */
static bool chat_list;        /* the last line is a list of completions (the next one replaces it) */
static const uint16_t chat_col[5] = {0xFFFF, RGB(0xFF, 0x55, 0x55), RGB(0x55, 0xFF, 0x55), RGB(0x00, 0xAA, 0x00),
                                     RGB(0xAA, 0xAA, 0xAA)};
static int chat_size(int i) { return 3 + (int)strlen(chat + i + 3) + 1; }
static void chat_drop_last(void) {
  int i = 0, last = -1;
  for (; i < chat_used; i += chat_size(i)) last = i;
  if (last >= 0) chat_used = last;
}
void chat_add(const char *s, int color) {
  int n = (int)strlen(s);
  if (n > CHAT_BUF - 4) n = CHAT_BUF - 4;
  if (chat_list) chat_drop_last(), chat_list = false;
  while (chat_used + n + 4 > CHAT_BUF) {
    int l = chat_size(0);
    memmove(chat, chat + l, (size_t)(chat_used - l));
    chat_used -= l;
  }
  char *d = chat + chat_used;
  d[0] = (char)color, d[1] = (char)(chat_clock & 255), d[2] = (char)(chat_clock >> 8);
  memcpy(d + 3, s, (size_t)n), d[3 + n] = 0;
  chat_used += n + 4;
}
void gui_message(const char *s) { chat_add(s, CHAT_WHITE); }
static int chat_age(int i) { return (uint16_t)(chat_clock - ((uint8_t)chat[i + 1] | (uint8_t)chat[i + 2] << 8)); }

/* how much of s fits on a line w pixels wide: up to a space if it can (FontRenderer.sizeStringToWidth) */
static int wrap_len(const char *s, int w) {
  int x = 0, i = 0, sp = -1;
  for (; s[i]; i++) {
    if (s[i] == ' ') sp = i;
    x += char_w((unsigned char)s[i]);
    if (x > w) return sp > 0 ? sp : i > 0 ? i : 1;
  }
  return i;
}

/* GuiNewChat.drawChat: the newest at the bottom, 9 pixels a line on a dark band; each fades out after
 * 10 seconds (10 lines at most), or with the chat open all of them (20 lines) */
static void chat_lines(void) {
  bool open = gui == GUI_CHAT;
  if (SCREEN_H - 28 - (open ? 20 : 10) * 9 >= clip_y1 || SCREEN_H - 28 <= clip_y0) return;
  int at[64], n = 0;
  for (int i = 0; i < chat_used && n < 64; i += chat_size(i)) at[n++] = i;
  int line = 0, most = open ? 20 : 10;
  for (int m = n - 1; m >= 0 && line < most; m--) {
    int age = chat_age(at[m]), a = 32;
    if (!open) {
      if (age >= 200) break;
      float d = (1 - age / 200.0f) * 10;
      if (d > 1) d = 1;
      a = (int)(d * d * 32);
    }
    /* its lines, wrapped, drawn upwards from the last */
    const char *s = chat + at[m] + 3, *ls[8];
    int ll[8], k = 0;
    while (*s && k < 8) {
      int l = wrap_len(s, SCREEN_W - 4);
      ls[k] = s, ll[k++] = l, s += l;
      if (*s == ' ') s++;
    }
    for (int j = k - 1; j >= 0 && line < most; j--, line++) {
      int y = SCREEN_H - 28 - line * 9;
      if (y <= clip_y0 || y - 9 >= clip_y1) continue;
      tint_rect(2, y - 9, SCREEN_W - 2, 9, 0, a / 2);
      if (a > 3) {
        char t[64];
        int l = ll[j] < 63 ? ll[j] : 63;
        memcpy(t, ls[j], (size_t)l), t[l] = 0;
        text(t, 2, y - 8, chat_col[(int)chat[at[m]]], true);
      }
    }
  }
}

/* ---------------------------------------------------------------- the HUD (GuiIngame) */
static int name_timer, last_slot = -1, last_id;

static void hud(void) {
  int hx = (SCREEN_W - 182) / 2, hy = SCREEN_H - 22;
  if (gui == GUI_NONE || gui == GUI_CHAT) {
    /* Minecraft draws its crosshair inverting what is behind it */
    for (int k = -4; k <= 4; k++) {
      int pts[2][2] = {{SCREEN_W / 2 + k, SCREEN_H / 2}, {SCREEN_W / 2, SCREEN_H / 2 + k}};
      for (int p = 0; p < 2; p++) {
        if (p == 1 && k == 0) continue;
        int x = pts[p][0], y = pts[p][1];
        if (y < clip_y0 || y >= clip_y1) continue;
        uint16_t *d = row_at(y) + x;
        *d = (uint16_t)~*d;
      }
    }
  }
  if (clip_y1 <= SCREEN_H - 60) return;
  sprite(SP_HOTBAR, hx, hy);
  sprite(SP_HOTBAR_SEL, hx - 1 + pl.slot * 20, hy - 1);
  for (int i = 0; i < 9; i++) draw_stack(&pl.inv[i], hx + 3 + i * 20, hy + 3);
  if (pl.mode == 0) {
    /* experience: the bar and the level */
    int by = SCREEN_H - 32 + 3;
    sprite(SP_XP_BG, hx, by);
    int w = (int)(pl.xp * 183);
    if (w > 0) sprite_part(SP_XP, hx, by, 0, 0, w > 182 ? 182 : w, 5);
    if (pl.xp_level > 0) {
      char b[12];
      number(pl.xp_level, b);
      int tx = (SCREEN_W - text_width(b)) / 2, ty = SCREEN_H - 31 - 4;
      glyphs(b, tx + 1, ty, 0, 1);
      glyphs(b, tx - 1, ty, 0, 1);
      glyphs(b, tx, ty + 1, 0, 1);
      glyphs(b, tx, ty - 1, 0, 1);
      glyphs(b, tx, ty, RGB(0x80, 0xFF, 0x20), 1);
    }
    /* hearts, from the left; they shake at 2 hearts or less */
    int top = SCREEN_H - 39, left = SCREEN_W / 2 - 91, right = SCREEN_W / 2 + 91;
    int hp = (int)ceilf(pl.health);
    bool flash = pl.invuln > 10 && (pl.invuln / 3) % 2 == 1;
    /* poisoned: green; regenerating: a wave runs along them (GuiIngame.renderPlayerStats) */
    extern uint32_t game_time;
    bool poison = pl.eff[EF_POISON] > 0;
    int wave = pl.eff[EF_REGEN] ? (int)(game_time % 25) : -1;
    for (int i = 0; i < 10; i++) {
      int x = left + i * 8, y = top;
      if (hp <= 4) y += (int)((frame_no * 7 + i * 13) % 3) - 1;
      if (i == wave) y -= 2;
      sprite(flash ? SP_HEART_HIT : SP_HEART_BG, x, y);
      if (i * 2 + 1 < hp) sprite(poison ? SP_HEART_POISON : SP_HEART, x, y);
      else if (i * 2 + 1 == hp) sprite(poison ? SP_HEART_POISON_HALF : SP_HEART_HALF, x, y);
    }
    /* armour, over the hearts */
    int ap = 0;
    for (int i = 0; i < 4; i++)
      if (pl.armor[i].id >= 256) ap += it_a[pl.armor[i].id - 256];
    if (ap > 0)
      for (int i = 0; i < 10; i++) {
        int x = left + i * 8;
        sprite(i * 2 + 1 < ap ? SP_ARMOR_FULL : i * 2 + 1 == ap ? SP_ARMOR_HALF : SP_ARMOR_BG, x, top - 10);
      }
    /* food, from the right */
    for (int i = 0; i < 10; i++) {
      int x = right - i * 8 - 9, y = top;
      if (pl.sat <= 0 && (frame_no % (unsigned)(pl.food * 3 + 1)) == 0) y += (int)(frame_no % 3) - 1;
      bool hunger = pl.eff[EF_HUNGER] > 0;   /* (green with the hunger effect) */
      sprite(hunger ? SP_FOOD_HUNGER_BG : SP_FOOD_BG, x, y);
      if (i * 2 + 1 < pl.food) sprite(hunger ? SP_FOOD_HUNGER : SP_FOOD, x, y);
      else if (i * 2 + 1 == pl.food) sprite(hunger ? SP_FOOD_HUNGER_HALF : SP_FOOD_HALF, x, y);
    }
    /* air, over the food */
    if (pl.air < 300) {
      int full = (int)ceilf((pl.air - 2) * 10 / 300.0f), part = (int)ceilf(pl.air * 10 / 300.0f) - full;
      for (int i = 0; i < full + part; i++) sprite(i < full ? SP_BUBBLE : SP_BUBBLE_POP, right - i * 8 - 9, top - 10);
    }
  }
  /* the name of the item just picked, for two seconds */
  if (gui == GUI_NONE && name_timer > 0 && held()->id) {
    const char *n = item_label(held()->id);
    int y = SCREEN_H - 59 + (pl.mode ? 14 : 0);
    text(n, (SCREEN_W - text_width(n)) / 2, y, 0xFFFF, true);
  }
}

/* ---------------------------------------------------------------- chests and furnaces */
typedef struct { int16_t x, z; uint8_t y, used; Stack s[27]; } Chest;
typedef struct { int16_t x, z; uint8_t y, used; Stack s[3]; int16_t burn, burn_max, cook; } Furnace;
#define N_CHESTS 8
#define N_FURNACES 6
static struct {
  Chest c[N_CHESTS];
  Furnace f[N_FURNACES];
} tiles;
#define chests tiles.c
#define furnaces tiles.f
void *tiles_data(uint32_t *len) {
  *len = sizeof tiles;
  return &tiles;
}
static Chest *chest;
static Furnace *furnace;

static Chest *chest_at(int x, int y, int z, bool make) {
  for (int i = 0; i < N_CHESTS; i++)
    if (chests[i].used && chests[i].x == x && chests[i].y == y && chests[i].z == z) return &chests[i];
  if (!make) return NULL;
  for (int i = 0; i < N_CHESTS; i++)
    if (!chests[i].used) {
      memset(&chests[i], 0, sizeof chests[i]);
      chests[i].used = 1, chests[i].x = (int16_t)x, chests[i].y = (uint8_t)y, chests[i].z = (int16_t)z;
      return &chests[i];
    }
  return NULL;
}
static Furnace *furnace_at(int x, int y, int z, bool make) {
  for (int i = 0; i < N_FURNACES; i++)
    if (furnaces[i].used && furnaces[i].x == x && furnaces[i].y == y && furnaces[i].z == z) return &furnaces[i];
  if (!make) return NULL;
  for (int i = 0; i < N_FURNACES; i++)
    if (!furnaces[i].used) {
      memset(&furnaces[i], 0, sizeof furnaces[i]);
      furnaces[i].used = 1, furnaces[i].x = (int16_t)x, furnaces[i].y = (uint8_t)y, furnaces[i].z = (int16_t)z;
      return &furnaces[i];
    }
  return NULL;
}

void tiles_removed(int x, int y, int z) {
  Chest *c = chest_at(x, y, z, false);
  Stack *s = NULL;
  int n = 0;
  if (c) s = c->s, n = 27, c->used = 0;
  Furnace *f = furnace_at(x, y, z, false);
  if (f) s = f->s, n = 3, f->used = 0;
  for (int i = 0; i < n; i++)
    if (s[i].id) ent_drop(s[i].id, item_count(&s[i]), s[i].aux, x + 0.5f, y + 0.5f, z + 0.5f, false);
}

void tiles_forget(int x, int y, int z) {
  Chest *c = chest_at(x, y, z, false);
  if (c) c->used = 0;
  Furnace *f = furnace_at(x, y, z, false);
  if (f) f->used = 0;
}

/* TileEntityFurnace.update */
static bool can_smelt(const Furnace *f) {
  if (!f->s[0].id) return false;
  int out = smelt_of(f->s[0].id);
  if (!out) return false;
  if (!f->s[2].id) return true;
  return f->s[2].id == out && f->s[2].aux < item_max(out);
}
void gui_tick(void) {
  chat_clock++;
  /* (old lines kept old, as the clock comes round) */
  if (!(chat_clock & 255))
    for (int i = 0; i < chat_used; i += chat_size(i))
      if (chat_age(i) > 1000) chat[i + 1] = (char)((chat_clock - 1000) & 255), chat[i + 2] = (char)((chat_clock - 1000) >> 8);
  for (int i = 0; i < N_FURNACES; i++) {
    Furnace *f = &furnaces[i];
    if (!f->used) continue;
    bool was = f->burn > 0;
    if (f->burn > 0) f->burn--;
    if ((f->burn > 0 || f->s[1].id) && f->s[0].id) {
      if (f->burn == 0 && can_smelt(f)) {
        f->burn = f->burn_max = (int16_t)item_fuel(f->s[1].id);
        if (f->burn > 0) {
          if (f->s[1].id == I_LAVA_BUCKET) f->s[1].id = I_BUCKET;
          else stack_take(&f->s[1], 1);
        }
      }
      if (f->burn > 0 && can_smelt(f)) {
        if (++f->cook >= 200) {
          f->cook = 0;
          int out = smelt_of(f->s[0].id);
          if (!f->s[2].id) f->s[2].id = (uint16_t)out, f->s[2].aux = 1;
          else f->s[2].aux++;
          stack_take(&f->s[0], 1);
        }
      } else f->cook = 0;
    } else if (f->burn == 0 && f->cook > 0) f->cook = (int16_t)(f->cook > 2 ? f->cook - 2 : 0);
    if (was != (f->burn > 0) && world_loaded(f->x, f->y, f->z)) {
      int b = world_get(f->x, f->y, f->z);
      if (b == B_FURNACE || b == B_FURNACE_LIT) world_set(f->x, f->y, f->z, f->burn > 0 ? B_FURNACE_LIT : B_FURNACE);
    }
  }
}

/* ---------------------------------------------------------------- container screens */
enum { SL_NORMAL, SL_RESULT, SL_OUTPUT, SL_ARMOR, SL_FUEL, SL_CREATIVE, SL_TAB };
typedef struct { int16_t x, y; uint8_t kind, k; Stack *s; } Slot;
static Slot slots[72];
static int drag;                      /* spreading a stack (below): 0, 1 even shares (OK), 2 one each (EXE) */
static uint8_t dragged[72], ndragged;
static int nslots, cur, panel_x, panel_y, panel_h, grid_w;
static Stack result;           /* what the crafting grid makes */
static Stack table_grid[9];    /* the crafting table's grid */
static Stack *grid;

static void add_slot(int x, int y, int kind, int k, Stack *s) {
  slots[nslots++] = (Slot){(int16_t)x, (int16_t)y, (uint8_t)kind, (uint8_t)k, s};
}
static void add_player_slots(int y_main, int y_hot) {
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 9; c++) add_slot(8 + c * 18, y_main + r * 18, SL_NORMAL, 0, &pl.inv[9 + r * 9 + c]);
  for (int c = 0; c < 9; c++) add_slot(8 + c * 18, y_hot, SL_NORMAL, 0, &pl.inv[c]);
}

static void update_result(void) {
  result.id = 0, result.aux = 0;
  if (!grid) return;
  int r = craft_find(grid, grid_w);
  if (r >= 0) result.id = recipes[r].out, result.aux = recipes[r].n;
  if (result.id && item_dur(result.id)) result.aux = 0;
}

/* the creative inventory: the tab shown, how far down, what its 45 slots show */
static int ctab = 0, cscroll;
static Stack cshown[45], ctabs[12];
static const uint16_t tab_icon[12] = {B_BRICKS, B_PEONY_LOWER, I_REDSTONE, B_RAIL, I_LAVA_BUCKET, I_COMPASS,
                                      I_APPLE, I_IRON_AXE, I_GOLDEN_SWORD, 0, I_STICK, B_CHEST};
#define TAB_SEARCH 5
static char csearch[16];   /* Search Items: what is typed (GuiContainerCreative.searchField) */
/* an item whose name has what is typed in it (updateCreativeSearch) */
static bool found(int id) {
  const char *n = item_label(id);
  for (; *n; n++) {
    int k = 0;
    while (csearch[k] && n[k] && (n[k] | 32) == csearch[k]) k++;
    if (!csearch[k]) return true;
  }
  return !csearch[0];
}
static int tab_count(void) {
  int n = tab_start[ctab + 1] - tab_start[ctab];
  if (ctab != TAB_SEARCH || !csearch[0]) return n;
  int c = 0;
  for (int i = 0; i < n; i++) c += found(tab_items[tab_start[ctab] + i]);
  return c;
}
static int tab_rows(void) { return (tab_count() + 8) / 9; }
static void creative_refresh(void) {
  int n = tab_start[ctab + 1] - tab_start[ctab], skip = cscroll * 9, k = 0;
  for (int i = 0; i < n && k < 45; i++) {
    int id = tab_items[tab_start[ctab] + i];
    if (ctab == TAB_SEARCH && !found(id)) continue;
    if (skip) skip--;
    else cshown[k].id = (uint16_t)id, cshown[k++].aux = 1;
  }
  for (; k < 45; k++) cshown[k].id = 0;
  for (int t = 0; t < 12; t++) ctabs[t].id = tab_icon[t], ctabs[t].aux = 1;
}

static int origin_x, origin_y, origin_z;
void gui_open(int screen, int x, int y, int z) {
  gui = screen;
  nslots = 0;
  cur = 0, drag = 0, ndragged = 0;
  grid = NULL;
  origin_x = x, origin_y = y, origin_z = z;
  panel_h = 166;
  switch (screen) {
    case GUI_INVENTORY:
      add_slot(144, 36, SL_RESULT, 0, &result);
      for (int i = 0; i < 4; i++) add_slot(88 + (i & 1) * 18, 26 + (i >> 1) * 18, SL_NORMAL, 0, &pl.craft[i]);
      for (int i = 0; i < 4; i++) add_slot(8, 8 + i * 18, SL_ARMOR, (uint8_t)(3 - i), &pl.armor[3 - i]);
      add_player_slots(84, 142);
      grid = pl.craft, grid_w = 2;
      cur = 5 + 4 + 27;   /* the first hotbar slot */
      break;
    case GUI_CRAFTING:
      add_slot(124, 35, SL_RESULT, 0, &result);
      memset(table_grid, 0, sizeof table_grid);
      for (int i = 0; i < 9; i++) add_slot(30 + (i % 3) * 18, 17 + (i / 3) * 18, SL_NORMAL, 0, &table_grid[i]);
      add_player_slots(84, 142);
      grid = table_grid, grid_w = 3;
      cur = 10 + 27;
      break;
    case GUI_FURNACE:
      furnace = furnace_at(x, y, z, true);
      if (!furnace) {
        gui = GUI_NONE;
        return;
      }
      add_slot(56, 17, SL_NORMAL, 0, &furnace->s[0]);
      add_slot(56, 53, SL_FUEL, 0, &furnace->s[1]);
      add_slot(116, 35, SL_OUTPUT, 0, &furnace->s[2]);
      add_player_slots(84, 142);
      cur = 3 + 27;
      break;
    case GUI_CREATIVE:
      for (int i = 0; i < 45; i++) add_slot(9 + (i % 9) * 18, 18 + (i / 9) * 18, SL_CREATIVE, (uint8_t)i, &cshown[i]);
      for (int c = 0; c < 9; c++) add_slot(9 + c * 18, 112, SL_NORMAL, 0, &pl.inv[c]);
      for (int t = 0; t < 12; t++)
        if (t != 9) add_slot(28 * (t % 6) + 6, t < 6 ? -28 + 9 : 136 + 6, SL_TAB, (uint8_t)t, &ctabs[t]);
      cscroll = 0;
      creative_refresh();
      panel_h = 136;
      cur = 45;
      break;
    case GUI_CHEST:
      chest = chest_at(x, y, z, true);
      if (!chest) {
        gui = GUI_NONE;
        return;
      }
      for (int i = 0; i < 27; i++) add_slot(8 + (i % 9) * 18, 18 + (i / 9) * 18, SL_NORMAL, 0, &chest->s[i]);
      add_player_slots(85, 143);
      panel_h = 167;
      cur = 27 + 27;
      break;
  }
  panel_x = (SCREEN_W - (screen == GUI_CREATIVE ? 195 : 176)) / 2;
  panel_y = (SCREEN_H - panel_h) / 2;
  update_result();
}

void gui_close(void) {
  drag = 0, ndragged = 0;
  /* what is left in a crafting grid and on the cursor goes back to the inventory (or falls) */
  Stack *back[10];
  int n = 0;
  if (gui == GUI_INVENTORY)
    for (int i = 0; i < 4; i++) back[n++] = &pl.craft[i];
  if (gui == GUI_CRAFTING)
    for (int i = 0; i < 9; i++) back[n++] = &table_grid[i];
  back[n++] = &pl.cursor;
  for (int i = 0; i < n; i++) {
    Stack *s = back[i];
    if (!s->id) continue;
    int left = inv_add(pl.inv, 36, s->id, item_count(s), s->aux);
    if (left) ent_drop(s->id, left, s->aux, pl.x, pl.y + 1.32f, pl.z, true);
    s->id = 0, s->aux = 0;
  }
  gui = GUI_NONE;
  grid = NULL;
}

static bool armor_fits(int k, int id) {
  static const uint8_t want[4] = {IK_BOOTS, IK_LEGGINGS, IK_CHESTPLATE, IK_HELMET};
  return item_kind(id) == want[k];
}

/* crafting: take the result once, the grid gives one of each */
static void take_result(void) {
  for (int i = 0; i < grid_w * grid_w; i++)
    if (grid[i].id) {
      int id = grid[i].id;
      stack_take(&grid[i], 1);
      /* a milk or water bucket used in a recipe leaves its bucket */
      if (id == I_MILK_BUCKET || id == I_WATER_BUCKET || id == I_LAVA_BUCKET) grid[i].id = I_BUCKET, grid[i].aux = 1;
    }
}

static int smelt_xp_left;
static void furnace_xp(int id, int n) {
  /* RecipesFurnace's experience, x 100 */
  int x = 10;
  if (id == I_IRON_INGOT || id == I_REDSTONE) x = 70;
  else if (id == I_GOLD_INGOT || id == I_DIAMOND || id == I_EMERALD) x = 100;
  else if (id >= I_COOKED_PORKCHOP && id <= I_COOKED_FISH) x = 35;
  else if (id == I_CHARCOAL) x = 15;
  else if (id == I_DYE_GREEN || id == I_DYE_BLUE) x = 20;
  else if (id == I_BRICK) x = 30;
  else if (id == B_HARDENED_CLAY) x = 35;
  smelt_xp_left += x * n;
  player_add_xp(smelt_xp_left / 100);
  smelt_xp_left %= 100;
}

/* Container.slotClick: button 0 left, 1 right */
static void click(Slot *sl, int button) {
  Stack *s = sl->s, *c = &pl.cursor;
  if (sl->kind == SL_TAB) {
    if (sl->k == 11) {
      gui_close();
      gui_open(GUI_INVENTORY, 0, 0, 0);
      return;
    }
    ctab = sl->k, cscroll = 0, csearch[0] = 0;
    creative_refresh();
    return;
  }
  if (sl->kind == SL_CREATIVE) {
    /* GuiContainerCreative: a click takes a stack of it (right: one); a held stack clicked there is gone */
    if (c->id) c->id = 0, c->aux = 0;
    else if (s->id) {
      c->id = s->id;
      c->aux = (uint16_t)(item_dur(s->id) ? 0 : button == 0 ? item_max(s->id) : 1);
    }
    return;
  }
  if (sl->kind == SL_RESULT || sl->kind == SL_OUTPUT) {
    if (!s->id) return;
    int n = item_count(s);
    if (c->id && (c->id != s->id || item_dur(s->id) || c->aux + n > item_max(s->id))) return;
    if (!c->id) *c = *s;
    else c->aux = (uint16_t)(c->aux + n);
    if (sl->kind == SL_RESULT) {
      take_result();
      update_result();
    } else {
      furnace_xp(s->id, n);
      s->id = 0, s->aux = 0;
    }
    return;
  }
  if (sl->kind == SL_ARMOR && c->id && !armor_fits(sl->k, c->id)) return;
  if (button == 0) {
    if (!c->id) *c = *s, s->id = 0, s->aux = 0;
    else if (!s->id) {
      if (sl->kind == SL_ARMOR && item_count(c) > 1) return;
      *s = *c, c->id = 0, c->aux = 0;
    } else if (s->id == c->id && !item_dur(s->id)) {
      int room = item_max(s->id) - s->aux, k = c->aux < room ? c->aux : room;
      s->aux = (uint16_t)(s->aux + k);
      stack_take(c, k);
    } else {
      Stack t = *s;
      *s = *c, *c = t;
    }
  } else {
    if (!c->id) {
      if (!s->id) return;
      if (item_dur(s->id)) *c = *s, s->id = 0, s->aux = 0;
      else {
        int half = (s->aux + 1) / 2;
        c->id = s->id, c->aux = (uint16_t)half;
        stack_take(s, half);
      }
    } else if (!s->id || (s->id == c->id && !item_dur(c->id) && s->aux < item_max(c->id))) {
      if (!s->id) {
        s->id = c->id;
        s->aux = item_dur(c->id) ? c->aux : 0;
      }
      if (!item_dur(c->id)) s->aux++;
      stack_take(c, 1);
    } else {
      Stack t = *s;
      *s = *c, *c = t;
    }
  }
  if (grid) update_result();
}

/* spreading a held stack over slots (GuiContainer's drag): OK held while the cursor goes over slots
 * shares the stack out evenly between them, EXE held puts one in each; it happens when the key is
 * let go, what is left stays held. Over one slot only, it is a plain click. */
static bool can_drag(int i) {
  const Slot *sl = &slots[i];
  const Stack *s = sl->s, *c = &pl.cursor;
  if (sl->kind == SL_RESULT || sl->kind == SL_OUTPUT || sl->kind >= SL_CREATIVE) return false;
  if (sl->kind == SL_ARMOR && (s->id || !armor_fits(sl->k, c->id))) return false;
  return !s->id || (s->id == c->id && !item_dur(c->id));
}
static bool in_drag(int i) {
  for (int k = 0; k < ndragged; k++)
    if (dragged[k] == i) return true;
  return false;
}
static void drag_add(int i) {
  if (!in_drag(i) && can_drag(i) && item_count(&pl.cursor) > ndragged) dragged[ndragged++] = (uint8_t)i;
}
/* Container.computeStackSize: what slot i holds once shared out; returns how many it gained */
static int drag_share(int i, Stack *out) {
  const Stack *c = &pl.cursor;
  int had = slots[i].s->id ? item_count(slots[i].s) : 0;
  int max = slots[i].kind == SL_ARMOR ? 1 : item_max(c->id);
  int n = had + (drag == 1 ? item_count(c) / ndragged : 1);
  if (n > max) n = max;
  *out = *c;
  if (!item_dur(c->id)) out->aux = (uint16_t)n;
  return n - had;
}
static int drag_left(void) {
  int left = item_count(&pl.cursor);
  Stack t;
  for (int k = 0; k < ndragged; k++) left -= drag_share(dragged[k], &t);
  return left;
}
static void drag_end(void) {
  if (ndragged == 1) click(&slots[dragged[0]], drag - 1);
  else if (ndragged > 1) {
    int left = drag_left();
    for (int k = 0; k < ndragged; k++) drag_share(dragged[k], slots[dragged[k]].s);
    if (left > 0) pl.cursor.aux = (uint16_t)left;
    else pl.cursor.id = 0, pl.cursor.aux = 0;
    if (grid) update_result();
  }
  drag = 0, ndragged = 0;
}

/* moves a stack into slots [a, b) (stacks first, then empty slots); true if all of it went */
static bool merge(Stack *s, int a, int b, bool backwards) {
  for (int pass = 0; pass < 2 && s->id; pass++)
    for (int k = 0; k < b - a && s->id; k++) {
      Slot *t = &slots[backwards ? b - 1 - k : a + k];
      if (t->kind == SL_RESULT || t->kind == SL_OUTPUT) continue;
      if (t->kind == SL_ARMOR && !armor_fits(t->k, s->id)) continue;
      if (pass == 0) {
        if (t->s->id != s->id || item_dur(s->id)) continue;
        int room = item_max(s->id) - t->s->aux, n = s->aux < room ? s->aux : room;
        if (n <= 0) continue;
        t->s->aux = (uint16_t)(t->s->aux + n);
        stack_take(s, n);
      } else if (!t->s->id) {
        *t->s = *s;
        s->id = 0, s->aux = 0;
      }
    }
  return !s->id;
}

/* transferStackInSlot of each container: shift + OK */
static void shift_click(int i) {
  Slot *sl = &slots[i];
  if (!sl->s->id) return;
  if (sl->kind == SL_CREATIVE) {
    inv_add(pl.inv, 36, sl->s->id, item_dur(sl->s->id) ? 1 : item_max(sl->s->id), 0);
    return;
  }
  if (sl->kind == SL_TAB) return;
  if (gui == GUI_CREATIVE) {
    sl->s->id = 0, sl->s->aux = 0;   /* shift-click on the hotbar in creative clears it */
    return;
  }
  int pmain = nslots - 36, phot = nslots - 9;   /* the player's rows, then the hotbar */
  if (sl->kind == SL_RESULT) {
    /* craft as many as fit */
    for (int n = 0; n < 64 && result.id; n++) {
      Stack r = result;
      if (!merge(&r, pmain, nslots, true)) {
        if (r.id != result.id || r.aux != result.aux) {
          take_result();
        }
        break;
      }
      take_result();
      update_result();
    }
    update_result();
    return;
  }
  Stack s = *sl->s;
  int before = item_count(&s);
  if (i >= pmain) {
    bool done = false;
    if (gui == GUI_FURNACE) {
      if (smelt_of(s.id)) done = merge(&s, 0, 1, false);
      else if (item_fuel(s.id)) done = merge(&s, 1, 2, false);
    } else if (gui == GUI_CHEST) done = merge(&s, 0, 27, false);
    else if (gui == GUI_INVENTORY && item_kind(s.id) >= IK_HELMET && item_kind(s.id) <= IK_BOOTS)
      done = merge(&s, 5, 9, false);
    if (!done && s.id) {
      if (i >= phot) merge(&s, pmain, phot, false);
      else merge(&s, phot, nslots, false);
    }
  } else {
    if (sl->kind == SL_OUTPUT) furnace_xp(s.id, before);
    merge(&s, pmain, nslots, true);
  }
  *sl->s = s;
  if (grid) update_result();
}

/* the arrows: the nearest slot that way */
static void move_cursor(int dx, int dy) {
  int best = -1;
  float bd = 1e9f;
  int cx = slots[cur].x, cy = slots[cur].y;
  for (int i = 0; i < nslots; i++) {
    if (i == cur) continue;
    int ex = slots[i].x - cx, ey = slots[i].y - cy;
    int along = dx ? ex * dx : ey * dy, across = dx ? ey : ex;
    if (along <= 0) continue;
    float d = along + abs(across) * 2.5f;
    if (d < bd) bd = d, best = i;
  }
  if (best >= 0) cur = best;
}

/* ---------------------------------------------------------------- menus
 * GuiMainMenu, GuiSelectWorld, GuiCreateWorld, GuiYesNo, GuiOptions,
 * GuiIngameMenu, GuiGameOver: their buttons where 1.8 puts them. */
typedef struct { int16_t x, y, w; uint8_t on, id; char label[32]; } Button;
static Button buttons[12];
static int nbuttons, bcur;
enum { B_NO, B_SINGLE, B_OPTIONS, B_QUITAPP, B_PLAY, B_CREATE, B_DELETE, B_CANCEL, B_MODE, B_SEED, B_DOCREATE,
       B_DODELETE, B_DIFF, B_GFX, B_LOOK, B_CLOUDS, B_BOB, B_DONE, B_BACK_GAME, B_SAVEQUIT, B_RESPAWN, B_TITLE,
       B_CONTROLS, B_CONTROLS_DONE, B_NAME, B_RENAME, B_DORENAME, B_RECREATE, B_CHEATS, B_LAN, B_LAN_MODE,
       B_LAN_CHEATS, B_LAN_START, B_TYPE };
int menu_choice;            /* an action for main.c (ACT_*) */
int create_mode;            /* the new world's game mode */
bool create_cheats;         /* and whether it allows cheats */
int create_type;            /* and its type (WT_*) */
static bool cheats_set;     /* (chosen, not following the game mode) */
static bool lan_mode, lan_allow;   /* Open to LAN's settings: the game mode for others, cheats */
char seed_text[21];         /* the new world's seed, as typed */
char name_text[WORLD_NAME + 1];   /* the new world's name, or the world being renamed's */
int play_slot;              /* the world to play */
static int options_from;    /* the screen the options go back to */
/* the Controls list's selection: -1 the Key Sheet button, 0.. the actions (kcol 1: their Reset), N_ACTIONS Done
 * (kcol 1: Reset Keys); waiting for a key (GuiControls.buttonId) */
static int ksel = -1, kcol, kscroll;
static bool kwait;
static void keys_input(uint32_t pressed);
static int controls_from;   /* and the key sheet */

/* the worlds, the one played last first, and the one selected */
static uint8_t wlist[MAX_WORLDS], nworlds, wsel;
static void list_worlds(void) {
  uint32_t when[MAX_WORLDS];
  WorldInfo w;
  nworlds = 0;
  for (int s = 1; s <= MAX_WORLDS; s++)
    if (world_info(s, &w)) {
      int k = nworlds++;
      for (; k > 0 && when[k - 1] < w.played; k--) wlist[k] = wlist[k - 1], when[k] = when[k - 1];
      wlist[k] = (uint8_t)s, when[k] = w.played;
    }
  if (wsel >= nworlds) wsel = nworlds ? nworlds - 1 : 0;
}

/* Typing in a field: the calculator's keys as its own keyboard has them, letters (its alpha
 * letters: e^x is a, + is z, - a space) or digits; alpha switches between them, shift makes the
 * next letter a capital (twice: all of them), backspace deletes, OK or Back is done */
enum { F_NONE, F_NAME, F_SEED };
static int field;               /* the field being typed in */
static uint8_t f_digits, f_caps;   /* (f_caps: 1 the next letter, 2 all of them) */
static bool f_sel;              /* the text is selected: the first key typed replaces it (an arrow keeps it) */
static uint64_t raw_held;
static void press(int id);
static char key_char(int k) {
  static const char letters[53] = {[18] = 'a', [19] = 'b', [20] = 'c', [21] = 'd', [22] = 'e', [23] = 'f', [24] = 'g',
                                   [25] = 'h', [26] = 'i', [27] = 'j', [28] = 'k', [29] = 'l', [30] = 'm', [31] = 'n',
                                   [32] = 'o', [33] = 'p', [34] = 'q', [36] = 'r', [37] = 's', [38] = 't', [39] = 'u',
                                   [40] = 'v', [42] = 'w', [43] = 'x', [44] = 'y', [45] = 'z', [46] = ' ', [48] = '?',
                                   [49] = '!'};
  static const char digits[53] = {[42] = '1', [43] = '2', [44] = '3', [36] = '4', [37] = '5', [38] = '6', [30] = '7',
                                  [31] = '8', [32] = '9', [48] = '0', [49] = '.', [46] = '-', [45] = '+', [33] = '(',
                                  [34] = ')', [39] = '*', [40] = '/'};
  if (k < 0 || k > 52) return 0;
  char c = f_digits ? digits[k] : letters[k];
  if (c >= 'a' && c <= 'z' && f_caps) {
    c = (char)(c - 'a' + 'A');
    if (f_caps == 1) f_caps = 0;
  }
  return c;
}
static void field_open(int f) {
  field = f;
  f_digits = f == F_SEED, f_caps = 0;
  f_sel = (f == F_NAME ? name_text : seed_text)[0] != 0;
  raw_held = ~(uint64_t)0;   /* (the key that opened it is not typed) */
}
static void field_input(void) {
  uint64_t raw = plat_scan(), down = raw & ~raw_held;
  raw_held = raw;
  char *t = field == F_NAME ? name_text : seed_text;
  int max = field == F_NAME ? WORLD_NAME : 20, n = (int)strlen(t);
  for (int k = 0; k < 64; k++) {
    if (!(down >> k & 1)) continue;
    if (k == RK_ALPHA) f_digits = !f_digits;
    else if (k == RK_SHIFT) f_caps = (uint8_t)((f_caps + 1) % 3);
    else if (k <= 3) f_sel = false;   /* (the arrows) */
    else if (k == RK_BACKSPACE) {
      if (f_sel) t[n = 0] = 0, f_sel = false;
      else if (n > 0) t[--n] = 0;
    } else if (k == RK_OK || k == RK_EXE || k == RK_BACK) {
      if (gui == GUI_RENAME && k != RK_BACK && name_text[0]) {
        /* (OK in the rename screen's field: rename, as Enter does) */
        field = F_NONE;
        press(B_DORENAME);
        return;
      }
      field = F_NONE;
    } else {
      char c = key_char(k);
      if (c && f_sel) t[n = 0] = 0, f_sel = false;
      if (c && n < max) t[n++] = c, t[n] = 0;
    }
  }
}

/* ---------------------------------------------------------------- the chat line (GuiChat) */
/* typed as fields are, and some keys type what commands need: x,n,t ":", var "_", x10^x "~",
 * ans a space, and 0 among the letters "@"; toolbox finishes the word (as Tab does), up brings
 * back the line sent last; OK or EXE sends, Back closes */
#define CHAT_MAX 60
#define RK_TOOLBOX 16
static char chat_text[CHAT_MAX + 1], chat_last[CHAT_MAX + 1];
static char cmp_word[24];   /* completing: the word as it was typed, where it starts, the way shown */
static int cmp_at, cmp_k = -1;
static void chat_open(const char *s) {
  strcpy(chat_text, s);
  gui = GUI_CHAT;
  f_digits = 0, f_caps = 0, cmp_k = -1;
  raw_held = ~(uint64_t)0;
}
static char chat_key(int k) {
  switch (k) {
    case 14: return ':';
    case 15: return '_';
    case 50: return '~';
    case 51: return ' ';
    case 48: if (!f_digits) return '@'; break;
  }
  return key_char(k);
}
static void put_word(int at, const char *w) {
  int n = (int)strlen(w);
  if (at + n > CHAT_MAX) n = CHAT_MAX - at;
  memcpy(chat_text + at, w, (size_t)n), chat_text[at + n] = 0;
}
/* GuiChat.autocompletePlayerNames: the first press finishes the word as far as every way agrees,
 * else lists the ways and shows the first; each press after shows the next */
static void complete(void) {
  char line[CHAT_MAX + 25], w[40];
  int at;
  if (cmp_k >= 0) {
    memcpy(line, chat_text, (size_t)cmp_at);
    strcpy(line + cmp_at, cmp_word);
    if (!command_complete(line, cmp_k, &at, w, sizeof w)) cmp_k = 0, command_complete(line, 0, &at, w, sizeof w);
    put_word(cmp_at, w);
    cmp_k++;
    return;
  }
  char common[40];
  int n = 0;
  while (n < 400 && command_complete(chat_text, n, &at, w, sizeof w)) {
    if (!n) strcpy(common, w);
    else {
      int i = 0;
      while (common[i] && common[i] == w[i]) i++;
      common[i] = 0;
    }
    n++;
  }
  if (!n) return;
  if ((int)strlen(common) > (int)strlen(chat_text + at)) {
    put_word(at, common);
    return;
  }
  if (strlen(chat_text + at) >= sizeof cmp_word) return;
  strcpy(cmp_word, chat_text + at);
  cmp_at = at;
  if (n > 1) {
    /* the list, in the chat (as much as fits) */
    char list[200];
    int l = 0;
    for (int k = 0; k < n && command_complete(chat_text, k, &at, w, sizeof w); k++) {
      int wl = (int)strlen(w);
      if (l + wl + 7 > (int)sizeof list) {
        strcpy(list + l, ", ...");
        l += 5;
        break;
      }
      if (l) list[l++] = ',', list[l++] = ' ';
      memcpy(list + l, w, (size_t)wl), l += wl;
    }
    list[l] = 0;
    chat_add(list, CHAT_WHITE);
    chat_list = true;
  }
  memcpy(line, chat_text, (size_t)cmp_at);
  strcpy(line + cmp_at, cmp_word);
  command_complete(line, 0, &at, w, sizeof w);
  put_word(cmp_at, w);
  cmp_k = 1;
}
static void chat_input(void) {
  uint64_t raw = plat_scan(), down = raw & ~raw_held;
  raw_held = raw;
  for (int k = 0; k < 64; k++) {
    if (!(down >> k & 1)) continue;
    int n = (int)strlen(chat_text);
    if (k == RK_ALPHA) f_digits = !f_digits;
    else if (k == RK_SHIFT) f_caps = (uint8_t)((f_caps + 1) % 3);
    else if (k == RK_BACK) {
      gui = GUI_NONE;
      return;
    } else if (k == RK_OK || k == RK_EXE) {
      /* GuiChat.sendChatMessage: without the spaces around it */
      gui = GUI_NONE;
      char *t = chat_text;
      while (*t == ' ') t++;
      while (n > 0 && chat_text[n - 1] == ' ') chat_text[--n] = 0;
      if (*t) {
        strcpy(chat_last, t);
        command_run(t);
      }
      return;
    } else if (k == RK_TOOLBOX) complete();
    else {
      cmp_k = -1;
      if (k == 1 && chat_last[0]) strcpy(chat_text, chat_last);   /* up */
      else if (k == RK_BACKSPACE) {
        if (n) chat_text[n - 1] = 0;
      } else {
        char c = chat_key(k);
        if (c && n < CHAT_MAX) chat_text[n] = c, chat_text[n + 1] = 0;
      }
    }
  }
}
/* the line being typed, on a dark band at the bottom, and the keys that type what */
static void chat_screen(void) {
  if (clip_y0 < 22) {
    tint_rect(0, 0, SCREEN_W, 22, 0, 16);
    text_center("alpha: abc/123   shift: Abc   toolbox: finish the word", SCREEN_W / 2, 2, RGB(0xA0, 0xA0, 0xA0));
    text_center("x,n,t  :     var  _     x10^x  ~     ans  space     0  @", SCREEN_W / 2, 12, RGB(0xA0, 0xA0, 0xA0));
  }
  if (clip_y1 <= SCREEN_H - 14) return;
  tint_rect(2, SCREEN_H - 14, SCREEN_W - 4, 12, 0, 16);
  const char *t = chat_text, *m = f_digits ? "123" : f_caps == 2 ? "ABC" : f_caps ? "Abc" : "abc";
  while (text_width(t) > SCREEN_W - 40) t++;   /* (a long line: its end) */
  uint16_t c = RGB(0xE0, 0xE0, 0xE0);
  text(t, 4, SCREEN_H - 12, c, true);
  if (!((chat_clock / 6) & 1)) text("_", 4 + text_width(t), SCREEN_H - 12, c, true);
  text(m, SCREEN_W - 6 - text_width(m), SCREEN_H - 12, RGB(0x80, 0x80, 0x80), true);
}

static void add_button(int x, int y, int w, int on, int id, const char *label) {
  Button *b = &buttons[nbuttons++];
  *b = (Button){(int16_t)x, (int16_t)y, (int16_t)w, (uint8_t)on, (uint8_t)id, {0}};
  int i = 0;
  for (; label[i] && i < 31; i++) b->label[i] = label[i];
  b->label[i] = 0;
}
static void cat(char *d, const char *a, const char *b) {
  while (*a) *d++ = *a++;
  while (*b) *d++ = *b++;
  *d = 0;
}

static void menu(int screen) {
  nbuttons = 0;
  int w = SCREEN_W, h = SCREEN_H;
  char t[40];
  switch (screen) {
    case GUI_TITLE: {
      int j = h / 4 + 48;
      add_button(w / 2 - 100, j, 200, 1, B_SINGLE, "Singleplayer");
      add_button(w / 2 - 100, j + 24, 200, 0, B_NO, "Multiplayer");
      add_button(w / 2 - 100, j + 72 + 12 - 24, 98, 1, B_OPTIONS, "Options...");
      add_button(w / 2 + 2, j + 72 + 12 - 24, 98, 1, B_QUITAPP, "Quit Game");
      break;
    }
    case GUI_WORLDS: {
      bool any = nworlds > 0;
      add_button(w / 2 - 154, h - 52, 150, any, B_PLAY, "Play Selected World");
      add_button(w / 2 + 4, h - 52, 150, world_free_slot() != 0, B_CREATE, "Create New World");
      add_button(w / 2 - 154, h - 28, 72, any, B_RENAME, "Rename");
      add_button(w / 2 - 76, h - 28, 72, any, B_DELETE, "Delete");
      add_button(w / 2 + 4, h - 28, 72, any && world_free_slot() != 0, B_RECREATE, "Re-Create");
      add_button(w / 2 + 82, h - 28, 72, 1, B_CANCEL, "Cancel");
      break;
    }
    case GUI_CREATE:
      add_button(w / 2 - 100, 60, 200, 1, B_NAME, name_text);   /* (drawn as a field) */
      cat(t, "Game Mode: ", create_mode ? "Creative" : "Survival");
      add_button(w / 2 - 155, 100, 150, 1, B_MODE, t);
      cat(t, "Allow Cheats: ", create_cheats ? "ON" : "OFF");
      add_button(w / 2 + 5, 100, 150, 1, B_CHEATS, t);
      cat(t, "Seed: ", seed_text[0] ? seed_text : (field == F_SEED ? "" : "(random)"));
      add_button(w / 2 - 100, 146, 200, 1, B_SEED, t);
      cat(t, "World Type: ", create_type == WT_FLAT ? "Superflat" : "Default");
      add_button(w / 2 - 100, 182, 200, 1, B_TYPE, t);
      add_button(w / 2 - 155, h - 28, 150, 1, B_DOCREATE, "Create New World");
      add_button(w / 2 + 5, h - 28, 150, 1, B_CANCEL, "Cancel");
      break;
    case GUI_RENAME:
      /* GuiRenameWorld */
      add_button(w / 2 - 100, 60, 200, 1, B_NAME, name_text);
      add_button(w / 2 - 100, h / 4 + 96 + 12, 200, name_text[0] != 0, B_DORENAME, "Rename");
      add_button(w / 2 - 100, h / 4 + 120 + 12, 200, 1, B_CANCEL, "Cancel");
      break;
    case GUI_CONFIRM:
      add_button(w / 2 - 155, h / 6 + 96, 150, 1, B_DODELETE, "Delete");
      add_button(w / 2 + 5, h / 6 + 96, 150, 1, B_CANCEL, "Cancel");
      break;
    case GUI_OPTIONS: {
      static const char *const diff[4] = {"Peaceful", "Easy", "Normal", "Hard"};
      cat(t, "Difficulty: ", diff[opt.difficulty & 3]);
      add_button(w / 2 - 155, h / 6 - 12, 150, 1, B_DIFF, t);
      cat(t, "Graphics: ", opt.fancy ? "Fancy" : "Fast");
      add_button(w / 2 + 5, h / 6 - 12, 150, 1, B_GFX, t);
      char n[8];
      number(opt.look, n);
      cat(t, "Look Speed: ", n);
      int k = (int)strlen(t);
      t[k] = '%', t[k + 1] = 0;
      add_button(w / 2 - 155, h / 6 + 12, 150, 1, B_LOOK, t);
      cat(t, "Clouds: ", opt.clouds ? "ON" : "OFF");
      add_button(w / 2 + 5, h / 6 + 12, 150, 1, B_CLOUDS, t);
      cat(t, "View Bobbing: ", opt.bobbing ? "ON" : "OFF");
      add_button(w / 2 - 155, h / 6 + 36, 150, 1, B_BOB, t);
      add_button(w / 2 + 5, h / 6 + 36, 150, 1, B_CONTROLS, "Controls...");
      add_button(w / 2 - 100, h / 6 + 168, 200, 1, B_DONE, "Done");
      break;
    case GUI_CONTROLS:
      add_button(w / 2 - 100, h - 22, 200, 1, B_CONTROLS_DONE, "Done");
      break;
    }
    case GUI_PAUSE:
      add_button(w / 2 - 100, h / 4 + 24 - 16, 200, 1, B_BACK_GAME, "Back to Game");
      add_button(w / 2 - 100, h / 4 + 48 - 16, 98, 0, B_NO, "Achievements");
      add_button(w / 2 + 2, h / 4 + 48 - 16, 98, 0, B_NO, "Statistics");
      add_button(w / 2 - 100, h / 4 + 96 - 16, 98, 1, B_OPTIONS, "Options...");
      add_button(w / 2 + 2, h / 4 + 96 - 16, 98, !lan_open, B_LAN, "Open to LAN");
      add_button(w / 2 - 100, h / 4 + 120 - 16, 200, 1, B_SAVEQUIT, "Save and Quit to Title");
      break;
    case GUI_LAN:
      /* GuiShareToLan */
      cat(t, "Game Mode: ", lan_mode ? "Creative" : "Survival");
      add_button(w / 2 - 155, 100, 150, 1, B_LAN_MODE, t);
      cat(t, "Allow Cheats: ", lan_allow ? "ON" : "OFF");
      add_button(w / 2 + 5, 100, 150, 1, B_LAN_CHEATS, t);
      add_button(w / 2 - 155, h - 28, 150, 1, B_LAN_START, "Start LAN World");
      add_button(w / 2 + 5, h - 28, 150, 1, B_CANCEL, "Cancel");
      break;
    case GUI_DEATH:
      add_button(w / 2 - 100, h / 4 + 72, 200, 1, B_RESPAWN, "Respawn");
      add_button(w / 2 - 100, h / 4 + 96, 200, 1, B_TITLE, "Title screen");
      break;
  }
  if (bcur >= nbuttons || !buttons[bcur].on)
    for (bcur = 0; bcur < nbuttons - 1 && !buttons[bcur].on; bcur++) {}
}

void gui_menu(int screen) {
  if (screen == GUI_CONTROLS && gui != GUI_CONTROLS) controls_from = gui;   /* Options, or the title */
  gui = screen;
  bcur = 0;
  field = F_NONE;
  if (screen == GUI_WORLDS) list_worlds();
  if (screen == GUI_KEYS && controls_from != GUI_KEYS) ksel = -1, kcol = 0, kscroll = 0;
  kwait = false;
  menu(screen);
}

/* the world selected in the list */
static int selected(WorldInfo *w) { return nworlds && world_info(wlist[wsel], w) ? wlist[wsel] : 0; }

static void button(const Button *b, bool hover) {
  int sp = !b->on ? SP_BUTTON_OFF : hover ? SP_BUTTON_HOVER : SP_BUTTON;
  /* GuiButton.drawButton: the left half from the texture's left, the right half from its right */
  sprite_part(sp, b->x, b->y, 0, 0, b->w / 2, 20);
  sprite_part(sp, b->x + b->w / 2, b->y, 200 - b->w / 2, 0, b->w / 2, 20);
  uint16_t col = !b->on ? RGB(0xA0, 0xA0, 0xA0) : hover ? RGB(0xFF, 0xFF, 0xA0) : RGB(0xE0, 0xE0, 0xE0);
  text_center(b->label, b->x + b->w / 2, b->y + (20 - 8) / 2, col);
}

static void press(int id) {
  WorldInfo wi;
  switch (id) {
    case B_SINGLE: gui_menu(GUI_WORLDS); break;
    case B_OPTIONS:
      options_from = gui;
      gui_menu(GUI_OPTIONS);
      break;
    case B_QUITAPP: menu_choice = ACT_QUIT_APP; break;
    case B_PLAY:
      if (selected(&wi)) play_slot = wlist[wsel], menu_choice = ACT_PLAY;
      break;
    case B_CREATE:
      create_mode = 0, create_type = WT_DEFAULT;
      create_cheats = cheats_set = false;
      seed_text[0] = 0;
      world_unique_name("New World", name_text);
      gui_menu(GUI_CREATE);
      break;
    case B_RECREATE: {
      /* GuiCreateWorld.func_146318_a: a new world as the selected one began, "Copy of" its name */
      if (!selected(&wi)) break;
      char n[WORLD_NAME + 9];
      cat(n, "Copy of ", wi.name);
      n[WORLD_NAME] = 0;
      world_unique_name(n, name_text);
      create_mode = wi.mode;
      create_cheats = wi.cheats, cheats_set = false, create_type = wi.type;
      /* (its seed, as it would be typed) */
      uint64_t v = wi.seed < 0 ? 0 - (uint64_t)wi.seed : (uint64_t)wi.seed;
      char d[21];
      int k = 0;
      do d[k++] = (char)('0' + v % 10), v /= 10;
      while (v);
      int j = 0;
      if (wi.seed < 0) seed_text[j++] = '-';
      while (k) seed_text[j++] = d[--k];
      seed_text[j] = 0;
      gui_menu(GUI_CREATE);
      break;
    }
    case B_RENAME:
      if (!selected(&wi)) break;
      memcpy(name_text, wi.name, sizeof wi.name);
      gui_menu(GUI_RENAME);
      field_open(F_NAME);
      break;
    case B_DORENAME:
      if (name_text[0] && nworlds) rename_world(wlist[wsel], name_text);
      gui_menu(GUI_WORLDS);
      break;
    case B_DELETE: gui_menu(GUI_CONFIRM); break;
    case B_DODELETE:
      if (nworlds) delete_world(wlist[wsel]);
      gui_menu(GUI_WORLDS);
      break;
    case B_CANCEL: gui_menu(gui == GUI_WORLDS ? GUI_TITLE : gui == GUI_LAN ? GUI_PAUSE : GUI_WORLDS); break;
    case B_MODE:
      /* (cheats follow the game mode until they are chosen) */
      create_mode = !create_mode;
      if (!cheats_set) create_cheats = create_mode;
      break;
    case B_CHEATS: create_cheats = !create_cheats, cheats_set = true; break;
    case B_TYPE: create_type = create_type == WT_FLAT ? WT_DEFAULT : WT_FLAT; break;
    case B_LAN:
      lan_mode = lan_allow = false;
      gui_menu(GUI_LAN);
      break;
    case B_LAN_MODE: lan_mode = !lan_mode; break;
    case B_LAN_CHEATS: lan_allow = !lan_allow; break;
    case B_LAN_START:
      /* IntegratedServer.shareToLAN: the game mode is for players who join; cheats for everyone */
      lan_open = true, lan_cheats = lan_allow;
      gui = GUI_NONE;
      break;
    case B_NAME: field_open(F_NAME); break;
    case B_SEED: field_open(F_SEED); break;
    case B_DOCREATE: menu_choice = ACT_NEW; break;
    case B_DIFF: opt.difficulty = (uint8_t)((opt.difficulty + 1) & 3); break;
    case B_GFX: opt.fancy = !opt.fancy; break;
    case B_LOOK: opt.look = (uint8_t)(opt.look >= 200 ? 50 : opt.look + 25); break;
    case B_CLOUDS: opt.clouds = !opt.clouds; break;
    case B_BOB: opt.bobbing = !opt.bobbing; break;
    case B_DONE:
      save_options();
      gui_menu(options_from);
      break;
    case B_CONTROLS: gui_menu(GUI_KEYS); break;
    case B_CONTROLS_DONE:
      if (opt.keys_seen != KEY_SHEET) opt.keys_seen = KEY_SHEET, save_options();
      gui_menu(controls_from);
      break;
    case B_BACK_GAME: gui = GUI_NONE; break;
    case B_SAVEQUIT: menu_choice = ACT_SAVE_QUIT; break;
    case B_RESPAWN: menu_choice = ACT_RESPAWN; break;
    case B_TITLE: menu_choice = ACT_TITLE; break;
  }
  if (gui != GUI_NONE) menu(gui);
}

static void menu_input(uint32_t keys, uint32_t pressed) {
  if (gui == GUI_KEYS) {
    keys_input(pressed);
    return;
  }
  if (!nbuttons) menu(gui);
  if (field) {
    field_input();
    if (gui != GUI_NONE) menu(gui);
    return;
  }
  raw_held = plat_scan();
  if (gui == GUI_WORLDS && (pressed & (K_UP | K_DOWN)) && nworlds) {
    /* up and down: the world; left and right: the buttons */
    wsel = (uint8_t)((wsel + ((pressed & K_DOWN) ? 1 : nworlds - 1)) % nworlds);
    pressed &= ~(uint32_t)(K_UP | K_DOWN);
  }
  int dir = (pressed & (K_DOWN | K_RIGHT)) ? 1 : (pressed & (K_UP | K_LEFT)) ? -1 : 0;
  if (dir)
    for (int k = 0; k < nbuttons; k++) {
      bcur = (bcur + dir + nbuttons) % nbuttons;
      if (buttons[bcur].on) break;
    }
  if (pressed & (K_OK | K_EXE) && buttons[bcur].on) press(buttons[bcur].id);
  else if (pressed & K_BACK) {
    /* Back: the way out of each screen */
    if (gui == GUI_PAUSE) gui = GUI_NONE;
    else if (gui == GUI_OPTIONS) press(B_DONE);
    else if (gui == GUI_CONTROLS) press(B_CONTROLS_DONE);
    else if (gui == GUI_WORLDS || gui == GUI_CREATE || gui == GUI_CONFIRM || gui == GUI_RENAME || gui == GUI_LAN)
      press(B_CANCEL);
  }
}

/* the title's name, in stone, like the game's logo (but its own) */
static void logo(int y) {
  const char *s = "NumBlocks";
  const int sc = 4;
  int w = text_width(s) * sc, x0 = (SCREEN_W - w) / 2;
  if (y + 8 * sc + 4 <= clip_y0 || y >= clip_y1) return;
  for (int d = 3; d >= 1; d--) glyphs(s, x0 + d, y + d, RGB(0x30 + d * 8, 0x30 + d * 8, 0x30 + d * 8), sc);
  /* the face: stone texels, 4 screen pixels each */
  int tex = blk_tex[B_STONE][0];
  for (int r = 0; r < 8 * sc; r++) {
    int ty = y + r;
    if (ty < clip_y0 || ty >= clip_y1) continue;
    uint16_t *d = row_at(ty);
    int x = x0;
    for (const char *c = s; *c; c++) {
      const uint8_t *g = font_bits + (*c - 32) * 8;
      for (int k = 0; k < 8 * sc; k++)
        if (g[r / sc] & (1 << (k / sc))) {
          int u = ((x + k) / 2) & 15, v = (ty / 2) & 15;
          uint8_t p = tex_px[tex][(v * 16 + u) >> 1];
          uint16_t col = tex_pal[tex][(u & 1) ? p >> 4 : p & 15];
          if (r < sc) col = blend(col, 0xFFFF, 8);   /* a lit top edge */
          d[x + k] = col;
        }
      x += font_w[*c - 32] * sc;
    }
  }
}

/* the yellow splash, turned 20 degrees */
static void splash(const char *s, int cx, int cy, float scale) {
  float a = -20 * 0.017453292f, ca = cosf(a), sa = sinf(a);
  int tw = text_width(s);
  int half = (int)(tw * scale / 2) + 12;
  for (int y = cy - half; y < cy + half; y++) {
    if (y < clip_y0 || y >= clip_y1) continue;
    uint16_t *d = row_at(y);
    for (int x = cx - half; x < cx + half; x++) {
      if ((unsigned)x >= SCREEN_W) continue;
      float dx = (x - cx) / scale, dy = (y - cy) / scale;
      for (int sh = 1; sh >= 0; sh--) {
        float u = ca * (dx - sh) + sa * (dy - sh) + tw / 2.0f, v = -sa * (dx - sh) + ca * (dy - sh) + 4;
        if (u < 0 || v < 0 || u >= tw || v >= 8) continue;
        int px = (int)u, row = (int)v, cx2 = 0;
        const char *c = s;
        while (*c && cx2 + font_w[*c - 32] <= px) cx2 += font_w[*c - 32], c++;
        if (!*c) continue;
        if (font_bits[(*c - 32) * 8 + row] & (1 << (px - cx2))) d[x] = sh ? RGB(0x3F, 0x3F, 0) : RGB(0xFF, 0xFF, 0);
      }
    }
  }
}

static void dirt_background(void) {
  /* GuiScreen.drawBackground: the options background, 32 pixel tiles, darkened */
  int id = SP_DIRT_BG;
  const uint8_t *px = spr_px + spr_off[id];
  const uint16_t *pal = spr_pal[id];
  for (int y = clip_y0; y < clip_y1; y++) {
    uint16_t *d = row_at(y);
    const uint8_t *s = px + ((y / 2) & 15) * 8;
    for (int x = 0; x < SCREEN_W; x++) {
      int u = (x / 2) & 15;
      d[x] = pal[(u & 1) ? s[u >> 1] >> 4 : s[u >> 1] & 15];
    }
  }
}

/* ---------------------------------------------------------------- the key sheet */
/* a key's name, a character after ^ raised (x^y) */
static void key_text(const char *s, int cx, int y, uint16_t col) {
  int w = 0;
  for (const char *c = s; *c; c++) w += *c == '^' ? 0 : char_w((unsigned char)*c);
  int x = cx - w / 2;
  for (const char *c = s; *c; c++) {
    if (*c == '^') continue;
    char t[2] = {*c, 0};
    text(t, x, y - (c > s && c[-1] == '^' ? 3 : 0), col, true);
    x += char_w((unsigned char)*c);
  }
}
/* a key: bright with what it does, dim if the game doesn't use it */
static void key(int x, int y, int w, const char *cap, const char *act) {
  fill(x, y, w, 29, act ? RGB(0xA0, 0xA0, 0xA0) : RGB(0x50, 0x50, 0x50));
  fill(x + 1, y + 1, w - 2, 27, act ? RGB(0x28, 0x28, 0x28) : RGB(0x18, 0x18, 0x18));
  if (!act) {
    key_text(cap, x + w / 2, y + 11, RGB(0x70, 0x70, 0x70));
    return;
  }
  if (cap[0] == ',' && !cap[1]) {
    /* the comma, big enough to see: a dot and its tail */
    int cx = x + w / 2;
    fill(cx - 1, y + 6, 3, 3, 0xFFFF);
    fill(cx, y + 9, 2, 2, 0xFFFF);
    fill(cx - 1, y + 11, 2, 1, 0xFFFF);
  } else key_text(cap, x + w / 2, y + 5, 0xFFFF);
  text_center(act, x + w / 2, y + 17, RGB(0xFF, 0xFF, 0xA0));
}
/* the round pad of arrows */
static void arrow_pad(int cx, int cy, int r) {
  for (int dy = -r; dy < r; dy++) {
    float e = (float)(r * r) - (dy + 0.5f) * (dy + 0.5f);
    int hw = (int)sqrtf(e), hi = (int)sqrtf(e - 2 * r + 1);
    fill(cx - hw, cy + dy, 2 * hw, 1, RGB(0xA0, 0xA0, 0xA0));
    if (abs(dy) < r - 1) fill(cx - hi, cy + dy, 2 * hi, 1, RGB(0x28, 0x28, 0x28));
  }
  for (int i = 1; i <= 5; i++) {
    fill(cx - i, cy - r + 3 + i, 2 * i, 1, 0xFFFF);
    fill(cx - i, cy + r - 4 - i, 2 * i, 1, 0xFFFF);
    fill(cx - r + 3 + i, cy - i, 1, 2 * i, 0xFFFF);
    fill(cx + r - 4 - i, cy - i, 1, 2 * i, 0xFFFF);
  }
  text_center("Look", cx, cy - 4, RGB(0xFF, 0xFF, 0xA0));
}
/* ---------------------------------------------------------------- the keys (KeyBinding) */
typedef struct { uint32_t bit; uint8_t key; const char *name, *what; } Action;
/* (what: the key sheet's word for it; key: where it starts) */
static const Action actions[N_ACTIONS] = {
    {K_ATTACK, 4, "Attack/Destroy", "Mine, attack"}, {0, 20, "Pick Block", "Pick block"},
    {K_USE, 5, "Use Item/Place Block", "Place, use"}, {K_DROP, 14, "Drop Item", "Drop"},
    {K_SLOT1, 42, "Hotbar Slot 1", "Slot 1"}, {K_SLOT1 << 1, 43, "Hotbar Slot 2", "Slot 2"},
    {K_SLOT1 << 2, 44, "Hotbar Slot 3", "Slot 3"}, {K_SLOT1 << 3, 36, "Hotbar Slot 4", "Slot 4"},
    {K_SLOT1 << 4, 37, "Hotbar Slot 5", "Slot 5"}, {K_SLOT1 << 5, 38, "Hotbar Slot 6", "Slot 6"},
    {K_SLOT1 << 6, 30, "Hotbar Slot 7", "Slot 7"}, {K_SLOT1 << 7, 31, "Hotbar Slot 8", "Slot 8"},
    {K_SLOT1 << 8, 32, "Hotbar Slot 9", "Slot 9"}, {K_INV, 15, "Inventory", "Inventory"},
    {K_PAUSE, 51, "Pause", "Pause"}, {K_JUMP, 12, "Jump", "Jump"}, {K_SNEAK, 13, "Sneak", "Sneak"},
    {K_SPRINT, 17, "Sprint", "Sprint"}, {K_STRAFE_L, 27, "Strafe Left", "Left"},
    {K_STRAFE_R, 29, "Strafe Right", "Right"}, {K_BACKW, 28, "Walk Backwards", "Backward"},
    {K_FWD, 22, "Walk Forwards", "Forward"}, {K_CHAT, 39, "Open Chat", "Chat"},
    {K_COMMAND, 40, "Open Command", "Command"}};
static const char *const categories[5] = {"Gameplay", "Inventory", "Miscellaneous", "Movement", "Multiplayer"};
static const uint8_t category_at[5] = {A_ATTACK, A_DROP, A_PAUSE, A_JUMP, A_CHAT};

void keys_reset(void) {
  for (int a = 0; a < N_ACTIONS; a++) opt.keys[a] = actions[a].key;
}
/* Saved with the keys as they were first (Back mining, OK placing): as they are now, OK the left mouse
 * button and Back the right; keys someone set stay as they set them */
void keys_update(int seen) {
  if (seen < 4 && opt.keys[A_ATTACK] == 5 && opt.keys[A_USE] == 4) opt.keys[A_ATTACK] = 4, opt.keys[A_USE] = 5;
}
/* the keys that can be set: not the arrows (they look), Home (it leaves), On/Off or EXE (it does what OK does) */
static bool settable(int k) { return k == 4 || k == 5 || (k >= 12 && k <= 51 && k != 35 && k != 41 && k != 47); }
uint32_t keys_of(uint64_t raw) {
  uint32_t r = 0;
  static const uint32_t fixed[8] = {K_LEFT, K_UP, K_DOWN, K_RIGHT, K_OK, K_BACK, K_HOME, K_HOME};
  for (int k = 0; k < 8; k++)
    if (raw >> k & 1) r |= fixed[k];
  if (raw >> 12 & 1) r |= K_SHIFT;
  if (raw >> 52 & 1) r |= K_EXE, raw |= 1 << 4;   /* (EXE: what OK does) */
  for (int a = 0; a < N_ACTIONS; a++)
    if (opt.keys[a] < 64 && (raw >> opt.keys[a] & 1)) r |= actions[a].bit;
  return r;
}
/* the key's name, as the calculator writes it */
static const char *key_name(int k) {
  static const char *const names[52] = {
      [4] = "OK", [5] = "Back", [12] = "shift", [13] = "alpha", [14] = "x,n,t", [15] = "var", [16] = "toolbox",
      [17] = "\3", [18] = "e^x", [19] = "ln", [20] = "log", [21] = "i", [22] = ",", [23] = "x^y", [24] = "sin",
      [25] = "cos", [26] = "tan", [27] = "\1", [28] = "\2", [29] = "x^2", [30] = "7", [31] = "8", [32] = "9",
      [33] = "(", [34] = ")", [36] = "4", [37] = "5", [38] = "6", [39] = "\4", [40] = "\5", [42] = "1", [43] = "2",
      [44] = "3", [45] = "+", [46] = "-", [48] = "0", [49] = ".", [50] = "x10^x", [51] = "ans"};
  return k < 52 && names[k] ? names[k] : "NONE";
}
static const char *action_on(int k) {
  for (int a = 0; a < N_ACTIONS; a++)
    if (opt.keys[a] == k) return actions[a].what;
  return NULL;
}

static void key_sheet(void) {
  text_center("Controls", SCREEN_W / 2, 4, 0xFFFF);
  arrow_pad(40, 45, 27);
  key(130, 30, 60, "Home", "Save, quit");
  key(236, 14, 80, "OK", action_on(4));
  key(236, 46, 80, "Back", action_on(5));
  /* the three rows under them, as on the calculator (the two left columns narrower) */
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 6; c++) {
      int k = 12 + r * 6 + c;
      key(c < 2 ? 5 + c * 37 : 79 + (c - 2) * 60, 80 + r * 32, c < 2 ? 34 : 57, key_name(k), action_on(k));
    }
  /* the keys set further down, in words */
  char line[2][72];
  int n = 0, len = 0;
  line[0][0] = line[1][0] = 0;
  bool digits = true;
  for (int a = A_SLOT1; a < A_SLOT1 + 9; a++) digits &= opt.keys[a] == actions[a].key;
  for (int a = -1; a <= N_ACTIONS && n < 2; a++) {
    char t[32];
    if (a == -1) {
      if (!digits) continue;
      strcpy(t, "1 to 9: hotbar slot.");
    } else if (a == N_ACTIONS) strcpy(t, "EXE: as OK.");
    else {
      int k = opt.keys[a];
      if ((digits && a >= A_SLOT1 && a < A_SLOT1 + 9) || k == 4 || k == 5 || (k >= 12 && k < 30) || k > 51) continue;
      cat(t, key_name(k), ": ");
      cat(t + strlen(t), actions[a].what, ".");
      for (char *c = t + strlen(key_name(k)) + 2; *c; c++)
        if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    }
    if (len && text_width(line[n]) + text_width(t) + 12 > SCREEN_W - 8) {
      if (++n == 2) break;
      len = 0;
    }
    if (len) strcat(line[n], "   ");
    strcat(line[n], t);
    len++;
  }
  static const char *const notes[3] = {"Forward twice: sprint.   Jump twice: fly (Creative).",
                                       "Menus: OK takes or puts, EXE one, shift+OK moves.",
                                       "Hold OK and move over slots to spread a stack out."};
  int rows = line[1][0] ? 2 : 1;
  for (int i = 0; i < rows; i++) text_center(line[i], SCREEN_W / 2, 177 + i * 10, 0xFFFF);
  for (int i = 0; i + rows < 4; i++) text_center(notes[i], SCREEN_W / 2, 177 + (i + rows) * 10, 0xFFFF);
}

/* GuiControls: the actions by category, each with its key and Reset; Done and Reset Keys under them */
#define KLIST_Y 42
#define KLIST_ROWS 8
static int krow_of(int a) {
  int r = a;
  for (int c = 0; c < 5; c++) r += category_at[c] <= a;
  return r;
}
static void keys_screen(void) {
  int w = SCREEN_W, h = SCREEN_H;
  text_center("Controls", w / 2, 8, 0xFFFF);
  Button b = {(int16_t)(w / 2 - 100), 18, 200, 1, 0, "Key Sheet..."};
  button(&b, ksel == -1);
  /* the label column: as wide as the widest label */
  int lw = 0;
  for (int a = 0; a < N_ACTIONS; a++)
    if (text_width(actions[a].name) > lw) lw = text_width(actions[a].name);
  int x = w / 2 - 126 + 2;
  for (int a = 0, c = 0, row = 0; a < N_ACTIONS; row++) {
    int y = KLIST_Y + (row - kscroll) * 20;
    bool shown = row >= kscroll && row < kscroll + KLIST_ROWS;
    if (c < 5 && category_at[c] == a) {
      if (shown) text_center(categories[c], w / 2, y + 20 - 9 - 1, 0xFFFF);
      c++;
      continue;
    }
    if (shown) {
      text(actions[a].name, x + 90 - lw, y + 6, 0xFFFF, false);
      /* the key: yellow between > < while waiting for one, red if another action has it too */
      Button kb = {(int16_t)(x + 105), (int16_t)y, 75, 1, 0, ""};
      button(&kb, ksel == a && kcol == 0);
      bool clash = false;
      for (int o = 0; o < N_ACTIONS; o++) clash |= o != a && opt.keys[o] == opt.keys[a] && opt.keys[a] != 255;
      const char *n = key_name(opt.keys[a]);
      uint16_t col = kwait && ksel == a ? RGB(0xFF, 0xFF, 0x55) : clash ? RGB(0xFF, 0x55, 0x55)
                     : ksel == a && kcol == 0 ? RGB(0xFF, 0xFF, 0xA0) : RGB(0xE0, 0xE0, 0xE0);
      key_text(n, x + 105 + 37, y + 6, col);
      if (kwait && ksel == a) {
        int nw = text_width(n) / 2 + 6;
        text(">", x + 105 + 37 - nw - 6, y + 6, 0xFFFF, true);
        text("<", x + 105 + 37 + nw, y + 6, 0xFFFF, true);
      }
      Button rb = {(int16_t)(x + 190), (int16_t)y, 50, opt.keys[a] != actions[a].key, 0, "Reset"};
      button(&rb, ksel == a && kcol == 1);
    }
    a++;
  }
  Button done = {(int16_t)(w / 2 - 155), (int16_t)(h - 29), 150, 1, 0, "Done"};
  bool any = false;
  for (int a = 0; a < N_ACTIONS; a++) any |= opt.keys[a] != actions[a].key;
  Button all = {(int16_t)(w / 2 + 5), (int16_t)(h - 29), 150, any, 0, "Reset Keys"};
  button(&done, ksel == N_ACTIONS && kcol == 0);
  button(&all, ksel == N_ACTIONS && kcol == 1);
}
static void keys_input(uint32_t pressed) {
  if (kwait) {
    uint64_t raw = plat_scan(), down = raw & ~raw_held;
    raw_held = raw;
    for (int k = 0; k < 64; k++) {
      if (!(down >> k & 1)) continue;
      if (k <= 3) kwait = false;   /* (an arrow: as it was) */
      else if (settable(k)) opt.keys[ksel] = (uint8_t)k, kwait = false;
    }
    return;
  }
  raw_held = plat_scan();
  if (pressed & K_DOWN) ksel = ksel < N_ACTIONS ? ksel + 1 : N_ACTIONS;
  if (pressed & K_UP) ksel = ksel > -1 ? ksel - 1 : -1;
  if (pressed & (K_LEFT | K_RIGHT)) kcol = !kcol;
  if (ksel == -1) kcol = 0;
  /* the selected action in view */
  if (ksel >= 0 && ksel < N_ACTIONS) {
    int r = krow_of(ksel);
    if (ksel == category_at[0] || ksel == category_at[1] || ksel == category_at[2] || ksel == category_at[3] ||
        ksel == category_at[4])
      r--;   /* (its category's name with it) */
    if (r < kscroll) kscroll = r;
    if (krow_of(ksel) >= kscroll + KLIST_ROWS) kscroll = krow_of(ksel) - KLIST_ROWS + 1;
  }
  if (pressed & (K_OK | K_EXE)) {
    if (ksel == -1) gui_menu(GUI_CONTROLS);
    else if (ksel < N_ACTIONS && kcol == 0) kwait = true, raw_held = plat_scan();
    else if (ksel < N_ACTIONS) opt.keys[ksel] = actions[ksel].key;
    else if (kcol == 1) keys_reset();
    else {
      save_options();
      gui_menu(GUI_OPTIONS);
    }
  } else if (pressed & K_BACK) {
    save_options();
    gui_menu(GUI_OPTIONS);
  }
}

/* GuiTextField: grey framed (white when chosen), the text, and while typing a blinking cursor
 * and what the keys type (abc, Abc for one capital, ABC, 123) */
static void text_field(const Button *b, bool hover, bool typing) {
  fill(b->x - 1, b->y - 1, b->w + 2, 22, hover || typing ? 0xFFFF : RGB(0xA0, 0xA0, 0xA0));
  fill(b->x, b->y, b->w, 20, 0);
  if (typing && f_sel) fill(b->x + 3, b->y + 5, text_width(b->label) + 1, 10, RGB(0x00, 0x00, 0xA0));   /* (selected) */
  text(b->label, b->x + 4, b->y + 6, RGB(0xE0, 0xE0, 0xE0), true);
  if (typing) {
    if ((frame_no / 6) & 1) text("_", b->x + 4 + text_width(b->label), b->y + 6, RGB(0xE0, 0xE0, 0xE0), true);
    const char *m = f_digits ? "123" : f_caps == 2 ? "ABC" : f_caps ? "Abc" : "abc";
    text(m, b->x + b->w - 4 - text_width(m), b->y + 6, RGB(0x80, 0x80, 0x80), true);
  }
}

static void menu_screen(void) {
  int w = SCREEN_W, h = SCREEN_H;
  switch (gui) {
    case GUI_TITLE:
      logo(30);
      splash("Also try Minecraft!", w / 2 + 72, 72, 1.25f + 0.08f * fabsf(sinf((frame_no % 60) * 0.1047f)));
      text("NumBlocks 1.0", 2, h - 10, 0xFFFF, true);
      text("Inspired by Minecraft, not affiliated with Mojang",
           w - text_width("Inspired by Minecraft, not affiliated with Mojang") - 2, h - 20, 0xFFFF, true);
      text("Made by Mason Chen as part of NumPlay", w - text_width("Made by Mason Chen as part of NumPlay") - 2, h - 10,
           0xFFFF, true);
      break;
    case GUI_WORLDS: {
      text_center("Select World", w / 2, 20, 0xFFFF);
      /* GuiSlot: 36 pixels a world, the selected one framed */
      int first = wsel > 3 ? wsel - 3 : 0;
      for (int k = first; k < nworlds && k < first + 4; k++) {
        WorldInfo wi;
        if (!world_info(wlist[k], &wi)) continue;
        int x = w / 2 - 110, y = 32 + (k - first) * 36 + 2;
        if (k == wsel) {
          fill(x - 2, y - 2, 220, 36, RGB(0x80, 0x80, 0x80));
          fill(x - 1, y - 1, 218, 34, 0);
        }
        text(wi.name, x + 2, y + 1, 0xFFFF, true);
        char f[4] = {'n', 'b', (char)('0' + wlist[k]), 0};
        text(f, x + 2, y + 12, RGB(0x80, 0x80, 0x80), true);
        char m[24];
        cat(m, wi.mode == 1 ? "Creative Mode" : "Survival Mode", wi.cheats ? ", Cheats" : "");
        text(m, x + 2, y + 22, RGB(0x80, 0x80, 0x80), true);
      }
      if (!nworlds) text_center("No worlds yet", w / 2, 60, RGB(0x80, 0x80, 0x80));
      char t[40], n[12];
      number((int)(plat_storage_free() / 1024), n);
      cat(t, "Storage free: ", n);
      cat(t + strlen(t), " KB", "");
      text_center(t, w / 2, h - 63, RGB(0x80, 0x80, 0x80));
      break;
    }
    case GUI_CREATE:
      text_center("Create New World", w / 2, 20, 0xFFFF);
      text("World Name", w / 2 - 100, 47, RGB(0xA0, 0xA0, 0xA0), true);
      text_center(create_mode ? "Unlimited resources, free flying and" : "Search for resources, crafting, gain",
                  w / 2, 122, RGB(0xA0, 0xA0, 0xA0));
      text_center(create_mode ? "destroy blocks instantly" : "levels, health and hunger", w / 2, 134,
                  RGB(0xA0, 0xA0, 0xA0));
      text_center(field ? "alpha: letters or digits, shift: capitals, OK: done" : "Leave blank for a random seed",
                  w / 2, 169, RGB(0xA0, 0xA0, 0xA0));
      break;
    case GUI_RENAME:
      text_center("Rename World", w / 2, 20, 0xFFFF);
      text("Enter Name", w / 2 - 100, 47, RGB(0xA0, 0xA0, 0xA0), true);
      if (field) text_center("alpha: letters or digits, shift: capitals, OK: done", w / 2, 90, RGB(0xA0, 0xA0, 0xA0));
      break;
    case GUI_CONFIRM: {
      WorldInfo wi;
      char t[64];
      text_center("Are you sure you want to delete this world?", w / 2, 70, 0xFFFF);
      cat(t, "'", nworlds && world_info(wlist[wsel], &wi) ? wi.name : "");
      cat(t + strlen(t), "' will be lost forever! (A long time!)", "");
      text_center(t, w / 2, 90, 0xFFFF);
      break;
    }
    case GUI_OPTIONS: text_center("Options", w / 2, 15, 0xFFFF); break;
    case GUI_CONTROLS: key_sheet(); break;
    case GUI_PAUSE: text_center("Game menu", w / 2, 40, 0xFFFF); break;
    case GUI_LAN:
      text_center("LAN World", w / 2, 50, 0xFFFF);
      text_center("Settings for Other Players", w / 2, 82, 0xFFFF);
      break;
    case GUI_LOADING:
      text_center("Loading world", w / 2, h / 2 - 50, 0xFFFF);
      text_center("Building terrain", w / 2, h / 2 - 20, 0xFFFF);
      break;
  }
  for (int i = 0; i < nbuttons; i++)
    if (buttons[i].id == B_NAME) text_field(&buttons[i], i == bcur, field == F_NAME);
    else button(&buttons[i], i == bcur && !field);
  if (field == F_SEED && gui == GUI_CREATE) {
    /* the field being typed in: a white frame and a blinking cursor */
    Button *b = &buttons[0];
    while (b->id != B_SEED) b++;
    fill(b->x, b->y, b->w, 1, 0xFFFF);
    fill(b->x, b->y + 19, b->w, 1, 0xFFFF);
    fill(b->x, b->y, 1, 20, 0xFFFF);
    fill(b->x + b->w - 1, b->y, 1, 20, 0xFFFF);
    if ((frame_no / 6) & 1) {
      int tx = b->x + b->w / 2 + text_width(b->label) / 2 + 1;
      fill(tx, b->y + 6, 5, 1, 0xFFFF);
      fill(tx, b->y + 13, 5, 1, 0xFFFF);
    }
  }
}

/* ---------------------------------------------------------------- input */
void gui_input(uint32_t keys, uint32_t pressed) {
  if (gui >= GUI_PAUSE) {
    if (gui == GUI_PAUSE && (pressed & K_PAUSE)) {
      gui = GUI_NONE;
      return;
    }
    menu_input(keys, pressed);
    return;
  }
  if (gui == GUI_CHAT) {
    chat_input();
    return;
  }
  if (gui == GUI_NONE) {
    nbuttons = 0;
    if (pressed & K_INV && !pl.dead) gui_open(pl.mode == 1 ? GUI_CREATIVE : GUI_INVENTORY, 0, 0, 0);
    else if (pressed & K_PAUSE) gui_menu(GUI_PAUSE);
    else if (pressed & K_COMMAND && !pl.dead) chat_open("/");
    else if (pressed & K_CHAT && !pl.dead) chat_open("");
    if (held()->id != last_id || pl.slot != last_slot) name_timer = 40, last_id = held()->id, last_slot = pl.slot;
    if (name_timer) name_timer--;
    return;
  }
  /* a container */
  if (pressed & (K_INV | K_BACK | K_PAUSE)) {
    gui_close();
    return;
  }
  bool scrolled = false;
  if (gui == GUI_CREATIVE && (pressed & (K_UP | K_DOWN)) && (keys & K_SHIFT)) {
    /* shift and up or down: the top or the bottom of the tab */
    int last = tab_rows() > 5 ? tab_rows() - 5 : 0;
    cscroll = pressed & K_UP ? 0 : last;
    creative_refresh();
    scrolled = true;
  }
  if (gui == GUI_CREATIVE && ctab == TAB_SEARCH) {
    /* Search Items: the keys type (their letters, as alpha gives them), backspace takes one back */
    uint64_t raw = plat_scan(), down = raw & ~raw_held;
    raw_held = raw;
    int n = (int)strlen(csearch);
    bool typed = false;
    for (int k = 4; k < 64; k++) {
      if (!(down >> k & 1)) continue;
      uint8_t d = f_digits, c = f_caps;
      f_digits = 0, f_caps = 0;
      char ch = k == RK_BACKSPACE ? 0 : key_char(k);
      f_digits = d, f_caps = c;
      if (k == RK_BACKSPACE && n) csearch[--n] = 0, typed = true;
      else if (((ch >= 'a' && ch <= 'z') || ch == ' ') && n < (int)sizeof csearch - 1) csearch[n++] = ch, csearch[n] = 0, typed = true;
    }
    if (typed) cscroll = 0, creative_refresh();
    pressed &= ~(uint32_t)(K_SLOT1 * 511);   /* (digits are letters here) */
  }
  if (gui == GUI_CREATIVE && slots[cur].kind == SL_CREATIVE && !scrolled) {
    /* past the top or bottom row: scroll the tab; holding a stack, down goes to the hotbar instead */
    if ((pressed & K_DOWN) && cur >= 36 && cscroll + 5 < tab_rows() && !pl.cursor.id) cscroll++, scrolled = true;
    if ((pressed & K_UP) && cur < 9 && cscroll > 0) cscroll--, scrolled = true;
    if (scrolled) creative_refresh();
  }
  if (!scrolled) {
    if (pressed & K_LEFT) move_cursor(-1, 0);
    if (pressed & K_RIGHT) move_cursor(1, 0);
    if (pressed & K_UP) move_cursor(0, -1);
    if (pressed & K_DOWN) move_cursor(0, 1);
  }
  if (drag) {
    drag_add(cur);
    if (!(keys & (drag == 1 ? K_OK : K_EXE))) drag_end();
  } else if (pressed & K_OK && keys & K_SHIFT) shift_click(cur);
  else if (pressed & (K_OK | K_EXE)) {
    int b = pressed & K_OK ? 0 : 1;
    if (pl.cursor.id && can_drag(cur)) drag = b + 1, drag_add(cur);   /* (given out when let go) */
    else click(&slots[cur], b);
  }
  /* a digit: swap with that hotbar slot */
  for (int n = 0; n < 9; n++)
    if (pressed & (K_SLOT1 << n)) {
      Slot *sl = &slots[cur];
      if (sl->kind == SL_TAB) continue;
      if (sl->kind == SL_CREATIVE) {
        if (sl->s->id) pl.inv[n].id = sl->s->id, pl.inv[n].aux = (uint16_t)(item_dur(sl->s->id) ? 0 : item_max(sl->s->id));
        continue;
      }
      if (sl->kind == SL_RESULT || sl->kind == SL_OUTPUT) {
        if (!pl.inv[n].id) {
          pl.inv[n] = *sl->s;
          if (sl->kind == SL_RESULT) take_result(), update_result();
          else sl->s->id = 0, sl->s->aux = 0;
        }
      } else if (sl->s != &pl.inv[n] && (sl->kind != SL_ARMOR || !pl.inv[n].id || armor_fits(sl->k, pl.inv[n].id))) {
        Stack t = *sl->s;
        *sl->s = pl.inv[n];
        pl.inv[n] = t;
      }
      if (grid) update_result();
    }
  /* xnt: throw the hovered stack's one item */
  if (pressed & K_DROP) {
    Slot *sl = &slots[cur];
    if (sl->s->id && sl->kind != SL_RESULT && sl->kind < SL_CREATIVE) {
      ent_drop(sl->s->id, 1, sl->s->aux, pl.x, pl.y + 1.32f, pl.z, true);
      stack_take(sl->s, 1);
    }
  }
}

/* ---------------------------------------------------------------- drawing a screen */
static void tooltip(const char *s, int x, int y) {
  /* GuiScreen.drawHoveringText: dark violet box with a blue to purple frame */
  int w = text_width(s), h = 8;
  x += 12, y -= 12;
  if (x + w + 4 > SCREEN_W) x = SCREEN_W - w - 4;
  if (y < 4) y = 4;
  fill(x - 3, y - 4, w + 6, 1, RGB(0x10, 0, 0x10));
  fill(x - 3, y + h + 3, w + 6, 1, RGB(0x10, 0, 0x10));
  fill(x - 3, y - 3, w + 6, h + 6, RGB(0x10, 0, 0x10));
  fill(x - 4, y - 3, 1, h + 6, RGB(0x10, 0, 0x10));
  fill(x + w + 3, y - 3, 1, h + 6, RGB(0x10, 0, 0x10));
  fill(x - 3, y - 2, 1, h + 4, RGB(0x50, 0, 0xFF));
  fill(x + w + 2, y - 2, 1, h + 4, RGB(0x28, 0, 0x7F));
  fill(x - 3, y - 3, w + 6, 1, RGB(0x50, 0, 0xFF));
  fill(x - 3, y + h + 2, w + 6, 1, RGB(0x28, 0, 0x7F));
  text(s, x, y, 0xFFFF, true);
}

static void container(void) {
  int px = panel_x, py = panel_y;
  static const uint8_t img[] = {0, IMG_INVENTORY, IMG_CRAFTING_TABLE, IMG_FURNACE, IMG_CHEST, IMG_CREATIVE};
  if (gui == GUI_CREATIVE)
    for (int t = 0; t < 12; t++)
      if (t != ctab && t != 9) sprite(t < 6 ? SP_TAB_TOP : SP_TAB_BOTTOM, px + 28 * (t % 6), t < 6 ? py - 28 : py + 136 - 4);
  image(img[gui], px, py);
  if (gui == GUI_CREATIVE) {
    int t = ctab;
    sprite(t < 6 ? SP_TAB_TOP_SEL : SP_TAB_BOTTOM_SEL, px + 28 * (t % 6), t < 6 ? py - 28 : py + 136 - 4);
    text(tab_name[ctab], px + 8, py + 6, RGB(0x40, 0x40, 0x40), false);
    if (ctab == TAB_SEARCH) {
      /* the search box (tab_item_search.png), what is typed in it, and the cursor */
      fill(px + 80, py + 4, 90, 12, RGB(0x55, 0x55, 0x55));
      fill(px + 81, py + 5, 89, 11, 0xFFFF);
      fill(px + 81, py + 5, 88, 10, RGB(0x8B, 0x8B, 0x8B));
      text(csearch, px + 82, py + 6, 0xFFFF, false);
      if ((frame_no / 6) & 1) text("_", px + 82 + text_width(csearch), py + 6, 0xFFFF, false);
    }
    int rows = tab_rows(), y = 18;
    if (rows > 5) y += (112 - 17) * cscroll / (rows - 5);
    sprite(SP_SCROLLER, px + 175, py + y);
  }
  uint16_t label = RGB(0x40, 0x40, 0x40);
  switch (gui) {
    case GUI_INVENTORY:
      sprite(SP_STEVE, px + 51 - 13, py + 75 - 54 + 2);
      text("Crafting", px + 86, py + 16, label, false);
      for (int i = 0; i < 4; i++)
        if (!pl.armor[3 - i].id) sprite(SP_SLOT_HELMET + i, px + 8, py + 8 + i * 18);
      break;
    case GUI_CRAFTING:
      text("Crafting", px + 28, py + 6, label, false);
      text("Inventory", px + 8, py + panel_h - 96 + 2, label, false);
      break;
    case GUI_FURNACE: {
      text("Furnace", px + 88 - text_width("Furnace") / 2, py + 6, label, false);
      text("Inventory", px + 8, py + panel_h - 96 + 2, label, false);
      if (furnace->burn > 0) {
        int k = furnace->burn * 13 / (furnace->burn_max ? furnace->burn_max : 200);
        sprite_part(SP_FLAME, px + 56, py + 36 + 12 - k, 0, 12 - k, 14, k + 1);
      }
      int a = furnace->cook * 24 / 200;
      if (a > 0) sprite_part(SP_ARROW, px + 79, py + 34, 0, 0, a + 1, 16);
      break;
    }
    case GUI_CHEST:
      text("Chest", px + 8, py + 6, label, false);
      text("Inventory", px + 8, py + panel_h - 96 + 2, label, false);
      break;
  }
  bool spread = drag && ndragged > 1;
  for (int i = 0; i < nslots; i++) {
    const Stack *st = slots[i].s;
    Stack t;
    if (spread && in_drag(i)) {
      /* a slot being spread over: lightened, holding its share already */
      tint_rect(px + slots[i].x, py + slots[i].y, 16, 16, 0xFFFF, 16);
      drag_share(i, &t), st = &t;
    }
    draw_stack(st, px + slots[i].x, py + slots[i].y);
  }
  /* the slot under the cursor: lightened (drawGradientRect 0x80FFFFFF) */
  Slot *sl = &slots[cur];
  tint_rect(px + sl->x, py + sl->y, 16, 16, 0xFFFF, 16);
  Stack held_now = pl.cursor;
  if (spread) {
    int left = drag_left();
    if (left > 0) held_now.aux = (uint16_t)left;
    else held_now.id = 0;
  }
  if (held_now.id) draw_stack(&held_now, px + sl->x + 4, py + sl->y + 4);
  else if (sl->kind == SL_TAB) tooltip(tab_name[sl->k], px + sl->x + 8, py + sl->y + 8);
  else if (sl->s->id) tooltip(item_label(sl->s->id), px + sl->x + 8, py + sl->y + 8);
}

void hud_strip(uint16_t *buf, int y0, int rows) {
  clip_buf = buf;
  clip_y0 = y0;
  clip_y1 = y0 + rows;
  if (y0 == 0) frame_no++, hand_frame();
  hand_strip(buf, y0, rows);
  if (pl.sleep_timer) {
    /* falling asleep: the screen darkens (GuiIngame.renderSleep) */
    int a = pl.sleep_timer * 32 / 100;
    tint_rect(0, y0, SCREEN_W, rows, 0x0841, a > 30 ? 30 : a);
  }
  if (gui < GUI_PAUSE || gui == GUI_PAUSE || gui == GUI_DEATH || gui == GUI_LAN ||
      ((gui == GUI_OPTIONS || gui == GUI_KEYS) && options_from == GUI_PAUSE))
    hud();
  if (gui == GUI_NONE || gui == GUI_CHAT) chat_lines();
  if (pl.hurt_time > 0 && gui == GUI_NONE) {
    /* (Minecraft tilts the camera; a red flash says the same here) */
    tint_rect(0, y0, SCREEN_W, rows, RGB(0xFF, 0, 0), pl.hurt_time * 2 / 3);
  }
  switch (gui) {
    case GUI_INVENTORY: case GUI_CRAFTING: case GUI_FURNACE: case GUI_CHEST: case GUI_CREATIVE:
      dark_background();
      container();
      break;
    case GUI_PAUSE: case GUI_LAN:
      dark_background();
      menu_screen();
      break;
    case GUI_CHAT: chat_screen(); break;
    case GUI_OPTIONS: case GUI_CONTROLS: case GUI_KEYS:
      if (options_from == GUI_PAUSE) dark_background();
      else dirt_background();
      if (gui == GUI_KEYS) keys_screen();
      else menu_screen();
      break;
    case GUI_TITLE:
      menu_screen();
      break;
    case GUI_WORLDS: case GUI_CREATE: case GUI_CONFIRM: case GUI_LOADING: case GUI_RENAME:
      dirt_background();
      menu_screen();
      break;
    case GUI_DEATH: {
      /* GuiGameOver: red gradient 0x60500000 to 0xA0803030 */
      for (int y = y0; y < y0 + rows; y++) {
        int a = 12 + y * 8 / SCREEN_H;
        uint16_t k = blend(RGB(0x50, 0, 0), RGB(0x80, 0x30, 0x30), y * 32 / SCREEN_H);
        uint16_t *d = row_at(y);
        for (int x = 0; x < SCREEN_W; x++) d[x] = blend(d[x], k, a);
      }
      text_big("You died!", SCREEN_W / 2, 60 / 2 * 2, 0xFFFF, 2);
      char b[24] = "Score: ", n[12];
      number(pl.xp_total, n);
      memcpy(b + 7, n, strlen(n) + 1);
      text_center(b, SCREEN_W / 2, 100, 0xFFFF);
      for (int i = 0; i < nbuttons; i++) button(&buttons[i], i == bcur);
      break;
    }
  }
}
