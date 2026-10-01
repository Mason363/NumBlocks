/* NumBlocks: Minecraft 1.8.8 for the NumWorks calculator.
 *
 * The world around the player lives in a block cache (vc) of VCX x VCY x VCZ
 * blocks that follows the player: whatever leaves it is generated again from
 * the seed when it comes back, with the player's changes (the edit log, edits.c)
 * applied on top. Light is kept per block too: sky light and block light.
 *
 * Minecraft's axes: +X east, +Y up, +Z south. Yaw 0 faces south (+Z) and
 * grows clockwise seen from above (90 faces west); pitch > 0 looks down. */
#ifndef NB_H
#define NB_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "blocks.h"
#include "data.h"

/* ---------------------------------------------------------------- the platform (plat_eadk.c / plat_host.c) */
enum {
  K_LEFT = 1 << 0, K_RIGHT = 1 << 1, K_UP = 1 << 2, K_DOWN = 1 << 3,      /* arrows: look */
  K_FWD = 1 << 4, K_BACKW = 1 << 5, K_STRAFE_L = 1 << 6, K_STRAFE_R = 1 << 7,   /* comma, sqrt, pi, x^2 */
  K_JUMP = 1 << 8, K_USE = 1 << 9, K_ATTACK = 1 << 10, K_INV = 1 << 11,   /* shift, OK, Back, var */
  K_HOME = 1 << 12, K_SNEAK = 1 << 13, K_PAUSE = 1 << 14, K_DROP = 1 << 15,   /* Home, alpha, Toolbox, x,n,t */
  K_SLOT1 = 1 << 16,   /* K_SLOT1 << n: digit n + 1 */
  K_SPRINT = 1 << 25,    /* backspace (also deletes, typing a seed) */
  K_EXE = 1 << 26,       /* EXE alone (OK and EXE both give K_USE): right click in screens */
  K_SHIFT = 1 << 27,     /* shift (also K_JUMP): shift click in screens */
  K_BACK = 1 << 28,      /* Back alone: closes screens */
  K_OK = 1 << 29,        /* OK alone */
  K_CHAT = 1 << 30,      /* x: the chat */
  K_COMMAND = 1u << 31   /* division: the chat, with "/" typed */
};
uint32_t plat_keys(void);
uint64_t plat_scan(void);   /* the keys down as the calculator numbers them (bit RK_*), for typing */
enum {
  RK_OK = 4, RK_BACK = 5, RK_SHIFT = 12, RK_ALPHA = 13, RK_BACKSPACE = 17, RK_EXE = 52
};
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
void plat_pull(int x, int y, int w, int h, uint16_t *px);   /* what the screen shows there */
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);   /* unaligned, valid until storage changes */
void plat_remove_prefix(const char *prefix);
uint32_t plat_storage_free(void);
bool plat_record(int i, char *name, int max);   /* the i-th record's name; false past the last */
uint8_t *plat_save_open(const char *name, uint32_t len);   /* a record of len bytes to fill (NULL: no room, nothing changed) */
void plat_save_close(const char *name);         /* (done filling it) */
bool plat_in_launcher(void);                    /* running inside NumPlay (which keeps its own copy of the saves) */
void plat_begin(void);
int plat_end(void);

#define SCREEN_W 320
#define SCREEN_H 240

/* ---------------------------------------------------------------- liquids */
static inline bool is_water(int b) {
  return b == B_WATER || b == B_FLOWING_WATER || (b >= B_FLOWING_WATER_2 && b <= B_FALLING_WATER);
}
static inline bool is_door_lower(int b) {
  return b == B_DOOR_OAK_LOWER || (b >= B_DOOR_OAK_LOWER_S && b <= B_DOOR_OAK_LOWER_N_OPEN);
}
static inline bool is_lava(int b) { return b == B_LAVA || b == B_FLOWING_LAVA || (b >= B_FLOWING_LAVA_4 && b <= B_FALLING_LAVA); }

/* ---------------------------------------------------------------- the generator (gen.c) */
void gen_init(int64_t seed);
void gen_set_flat(int flat);   /* superflat (kept across gen_init) */
void gen_slab(int cx, int cz, int y0, int h, uint8_t *out);   /* out[(y - y0) * 256 + z * 16 + x] */
int gen_prepare(int cx, int cz);   /* a slice of the work for gen_slab(cx, cz) ahead; 0: nothing left */
int gen_biome(int x, int z);
int gen_top(int x, int z);
void gen_spawn(int *x, int *y, int *z);
float gen_temp_noise(int x, int z);   /* BiomeGenBase.temperatureNoise at (x / 8, z / 8) */

/* ---------------------------------------------------------------- the world (world.c) */
#define VCX 40
#define VCZ 40
#define VCY 24
#define WORLD_H 128
extern uint8_t vc[VCY * VCZ * VCX];      /* blocks: index (y * VCZ + z) * VCX + x */
extern uint8_t vl[VCY * VCZ * VCX];      /* light: sky light (low 4 bits), block light (high 4 bits) */
extern uint8_t vbiome[VCZ * VCX];        /* biome of each column */
extern int vc_x0, vc_y0, vc_z0;          /* world position of vc[0] */
#define VC_I(x, y, z) (((y) * VCZ + (z)) * VCX + (x))
/* 4 x 4 x 4 regions of the cache: 0 all air, 2 only water and air, 1 anything else (rays skip
 * the empty ones, and once in water, which sees through both, the water ones) */
#define MCX (VCX / 4)
#define MCY (VCY / 4)
#define MCZ (VCZ / 4)
extern uint8_t vmac[MCY * MCZ * MCX];
#define MC_I(x, y, z) ((((y) >> 2) * MCZ + ((z) >> 2)) * MCX + ((x) >> 2))
static inline int light_at(int i) { return vl[i] & 15; }          /* sky light */
static inline int block_light_at(int i) { return vl[i] >> 4; }   /* torches, lava, glowstone... */

void world_new(int64_t seed, const char *name);   /* name: the save's record prefix */
void world_follow(float x, float y, float z);   /* keeps the cache around (x, y, z) */
int world_get(int x, int y, int z);            /* B_AIR outside the cache, B_BEDROCK below 0 */
void world_set(int x, int y, int z, int b);     /* a player's change: kept in the edit log */
void world_light_flush(void);   /* the light of changed blocks (world_follow does it each frame) */
bool world_loaded(int x, int y, int z);
bool world_pending(void);                       /* chunks still being made after a move */
int world_rain_top(int x, int z);               /* the first y above what rain lands on, 255 if unknown */
void *world_scratch(uint32_t n);                /* n bytes free while a frame is drawn, or NULL */

/* ---------------------------------------------------------------- items (inv.c) */
typedef struct { uint16_t id, aux; } Stack;   /* aux: how many; for tools and armour, their damage (one) */
int item_max(int id);
int item_dur(int id);
int item_kind(int id);
int item_count(const Stack *s);
const char *item_label(int id);
int item_icon(int id);   /* its sprite, -1 if none */
int item_fuel(int id);
int smelt_of(int id);
int inv_add(Stack *inv, int n, int id, int count, int dmg);   /* what does not fit */
void stack_take(Stack *s, int k);
bool stack_wear(Stack *s, int k);   /* false: it broke */
int craft_find(const Stack *grid, int w);   /* a recipe, or -1 */
bool can_harvest(int b, int held);
float dig_speed(int b, int held);
int block_drops(int b, int held, Stack *out);   /* up to 2 */
int rnd(int n);
float rndf(void);

/* ---------------------------------------------------------------- collisions (phys.c) */
bool block_box(int x, int y, int z, float *b);
int block_boxes(int b, int x, int y, int z, int8_t (*o)[6]);   /* its shape, up to 5 boxes; 0: a whole cube */
void select_box(int b, int x, int y, int z, float *o);         /* its outline: x0 y0 z0 x1 y1 z1, in blocks */
float phys_clip(const float *box, int axis, float d);
bool phys_move(float *p, float *v, float w, float h);
bool phys_free(const float *a);   /* nothing solid overlaps the box {x0 y0 z0 x1 y1 z1} */

/* ---------------------------------------------------------------- the player (player.c) */
typedef struct {
  float x, y, z;          /* feet */
  float vx, vy, vz;       /* blocks a tick */
  float yaw, pitch;       /* degrees */
  bool on_ground, in_water, sneaking, sprinting, flying, dead;
  int hit_x, hit_y, hit_z, hit_face;   /* the block looked at (hit_face -1: none) */
  float breaking;         /* 0..1 of the block being mined */
  int slot;               /* the hotbar slot held */
  Stack inv[36];          /* 0-8 the hotbar, then the three rows */
  Stack armor[4];         /* boots, leggings, chestplate, helmet */
  Stack craft[4];         /* the inventory's 2 x 2 crafting grid */
  Stack cursor;           /* held by the cursor in a screen */
  float health, sat, exhaustion, fall, last_damage;
  int food, food_timer, air, invuln, hurt_time, fire, using_ticks;
  int xp_level, xp_total;
  int sleep_timer;        /* ticks asleep in a bed (0: awake) */
  float walked, bob, prev_walked, prev_bob;   /* view bobbing (EntityPlayer.cameraYaw) */
  float xp;               /* 0..1 of the way to the next level */
  int spawn_x, spawn_y, spawn_z;
  uint8_t mode;           /* 0 survival, 1 creative */
  uint8_t eff_amp[3];     /* effects: their amplifiers (0: level I) */
  uint16_t eff[3];        /* effects: ticks left (poison, hunger, regeneration) */
} Player;
enum { EF_POISON, EF_HUNGER, EF_REGEN };
extern Player pl;
void player_spawn(void);
void player_pick_block(void);   /* the block looked at, into the hand (Pick Block) */
extern float look_ray[6];   /* the eyes' ray as last drawn: from, direction */
void player_look(float ex, float ey, float ez, float yaw, float pitch);
void player_tick(uint32_t keys, uint32_t pressed);   /* 20 a second */
static inline Stack *held(void) { return &pl.inv[pl.slot]; }
void player_hurt(float amount, int kind);   /* kind: DMG_* */
enum { DMG_GENERIC, DMG_FALL, DMG_DROWN, DMG_LAVA, DMG_FIRE, DMG_STARVE, DMG_WALL, DMG_VOID, DMG_MOB, DMG_ARROW,
       DMG_EXPLOSION, DMG_CACTUS, DMG_LIGHTNING, DMG_MAGIC };
void player_add_xp(int n);
void player_swing(void);   /* the arm swings (hand.c) */
void hand_tick(void);
void hand_frame(void);     /* the hand and what it holds, once a frame, */
void hand_strip(uint16_t *buf, int y0, int rows);   /* then drawn on each strip */
void gui_message(const char *s);   /* a line at the bottom left, as Minecraft's chat shows */

/* ---------------------------------------------------------------- entities (entity.c) */
enum { E_NONE, E_ITEM, E_ZOMBIE, E_SKELETON, E_CREEPER, E_SPIDER, E_PIG, E_COW, E_SHEEP, E_CHICKEN, E_ARROW, E_TNT,
       E_BOBBER };
typedef struct {
  uint8_t type, on_ground, hurt, state;   /* hurt: ticks of red; state 255: dying (timer counts) */
  int16_t age, health, timer, delay;   /* delay: an item's pickup delay, a mob's attack wait, a creeper's fuse */
  float x, y, z, vx, vy, vz, yaw, pitch;
  float px, py, pz;       /* last tick's position */
  float limb, limb_amt;   /* the walk's swing */
  float gx, gz;           /* where it steers now (the next turn of its way) */
  int16_t panic, fire;
  int16_t love, growth;   /* animals: in love (ticks), a baby's growing up (negative: ticks left) */
  uint8_t invuln, sheared;
  union {
    Stack item;                  /* an item, an arrow, TNT: what it is */
    struct { int16_t tx, tz; };  /* a mob: where it is going in the end (a block) */
  };
} Entity;
#define N_ENT 24
extern Entity ents[N_ENT];
Entity *ent_new(int type, float x, float y, float z);
Entity *mob_summon(int type, float x, float y, float z);   /* a mob as one spawns */
Entity *ent_drop(int id, int count, int dmg, float x, float y, float z, bool thrown);
void ents_tick(void);
/* particles: a block's bits when it breaks; drops (a texel or two, falling
 * faster) and floating bits (wakes, bubbles): life (ticks, up to 63) + kind */
typedef struct { float x, y, z, vx, vy, vz; uint16_t c; uint8_t age, life; } Particle;
#define N_PART 32
#define RAIN_DROP 128
#define P_FLOAT 64
#define P_LIFE 63
extern Particle parts[N_PART];
void particles_break(int x, int y, int z, int b);
void particle_add(float x, float y, float z, float vx, float vy, float vz, uint16_t c, int life);
/* fishing (EntityFishHook) */
Entity *bobber(void);       /* the player's hook, if out */
void fish_cast(void);
int fish_reel(Entity *e);   /* how much it wears the rod */
void mob_tick(Entity *e);
void mobs_spawn(void);
void explode(float x, float y, float z, float power, bool blocks);
void tnt_light(int x, int y, int z, int fuse);
void throw_item(int id, float speed, bool from_player);   /* arrows, snowballs, eggs */
bool mob_attack(const Entity *e);   /* the player hits this mob (with the held item) */
bool mob_use(Entity *e);            /* the player uses the held item on it (shears, bucket) */
void mob_struck(Entity *e);         /* hit by lightning */
void mob_hooked(Entity *e);         /* hit by a fishing hook */
Entity *entity_looked_at(float reach, float block_t);   /* the mob under the crosshair, nearer than block_t */
extern float tick_frac;

void player_respawn(void);

/* ---------------------------------------------------------------- liquids and growing (tick.c) */
void fluid_schedule(int x, int y, int z);   /* a liquid there may move */
bool fire_can_stay(int x, int y, int z);    /* on a solid top, or by something that burns */
void fire_set(int x, int y, int z, int age);   /* fire there (age 0 to 15: older spreads less, goes out sooner) */
void bolt_start(float x, float y, float z);   /* lightning strikes there */
void world_tick(void);                      /* 20 a second: weather, liquids, random block ticks */

/* the weather (World.updateWeather), saved with the world */
typedef struct {
  int32_t rain_time, thunder_time;   /* ticks until it starts or stops */
  uint8_t raining, thundering, pad[2];
} Weather;
extern Weather weather;
extern float rain_str, thunder_str;   /* how hard it rains, how stormy: 0 to 1, 0.01 a tick */
void weather_clear(void);             /* after a night's sleep */
float celestial(uint32_t t);          /* the sun's angle: 0 noon, 0.5 midnight */
int sky_sub(void);                    /* how much darker the sky light is: 0 by day, 11 at night */
float temp_at(int x, int y, int z);   /* the biome's temperature there */
bool rain_at(int x, int y, int z);    /* rain (not snow) falls on it */
/* a bolt of lightning (EntityLightningBolt): it shows (and the sky flashes)
 * while state >= 0, and comes back `living` more times */
typedef struct { float x, y, z; int8_t state, living, on; uint32_t seed; } Bolt;
extern Bolt bolt;
extern int last_bolt;                 /* ticks of the sky's flash left (World.lastLightningBolt) */
void sapling_grow(int x, int y, int z);      /* a step towards a tree */
void neighbours_changed(int x, int y, int z);
void break_block_at(int x, int y, int z, bool drops);

/* ---------------------------------------------------------------- screens (gui.c) */
enum { GUI_NONE, GUI_INVENTORY, GUI_CRAFTING, GUI_FURNACE, GUI_CHEST, GUI_CREATIVE, GUI_CHAT,
       GUI_PAUSE, GUI_DEATH, GUI_OPTIONS, GUI_TITLE, GUI_WORLDS, GUI_CREATE, GUI_CONFIRM, GUI_LOADING, GUI_CONTROLS,
       GUI_RENAME, GUI_LAN, GUI_KEYS };
extern int gui;            /* the screen open */
void gui_open(int screen, int x, int y, int z);
void gui_close(void);
void gui_input(uint32_t keys, uint32_t pressed);
void gui_tick(void);       /* 20 a second: furnaces */
void tiles_removed(int x, int y, int z);   /* a chest or furnace broken: its items fall out */
void tiles_forget(int x, int y, int z);    /* (or replaced by a command: they are gone) */
void gui_menu(int screen);  /* opens a menu screen */
enum { ACT_NONE, ACT_PLAY, ACT_NEW, ACT_QUIT_APP, ACT_SAVE_QUIT, ACT_RESPAWN, ACT_TITLE };
extern int menu_choice;    /* what a menu asks main.c to do (ACT_*), 0 if nothing */
extern int play_slot;      /* (ACT_PLAY) the world chosen */
extern char name_text[];   /* (ACT_NEW) the new world's name, as typed */
extern int create_mode;
extern bool create_cheats;
extern int create_type;
extern char seed_text[21];

/* ---------------------------------------------------------------- saves and options (save.c) */
typedef struct {
  uint8_t difficulty;   /* 0 peaceful, 1 easy, 2 normal, 3 hard */
  uint8_t fancy;        /* graphics: 0 fast (solid leaves), 1 fancy */
  uint8_t look;         /* look speed, % */
  uint8_t clouds, bobbing;
  uint8_t keys_seen;    /* the key sheet's version last shown (it opens when the keys change) */
  uint8_t last_world;   /* the world played last (its slot, 0: none): the title's backdrop */
  uint32_t plays;       /* worlds opened so far: each world keeps the count when it was last played */
  uint8_t keys[24];     /* the key of each action (A_*): the calculator's number for it, 255 none */
} Options;
/* the actions keys are set to (KeyBinding), in the order of Minecraft's list: by category, then name */
enum { A_ATTACK, A_PICK, A_USE, A_DROP, A_SLOT1, A_INVENTORY = A_SLOT1 + 9, A_PAUSE, A_JUMP, A_SNEAK, A_SPRINT,
       A_LEFT, A_RIGHT, A_BACK, A_FORWARD, A_CHAT, A_COMMAND, N_ACTIONS };
uint32_t keys_of(uint64_t raw);   /* the K_ bits of the keys held (the calculator's keys, as bound) */
void keys_reset(void);            /* the keys as they start */
void keys_update(int seen);       /* keys saved by an older version (seen: its key sheet) brought up to date */
#define KEY_SHEET 4       /* (2: jump and sneak on shift and alpha; 3: chat and commands; 4: OK mines, Back places) */
extern Options opt;
extern int64_t world_seed;
enum { WT_DEFAULT, WT_FLAT };
extern uint8_t world_type;   /* the world type (WT_*): Default, or Superflat */
/* Worlds: up to MAX_WORLDS, each in a slot (1..MAX_WORLDS) whose records start "nb<slot>" */
#define MAX_WORLDS 9
#define WORLD_NAME 24   /* the longest name */
typedef struct {
  char name[WORLD_NAME + 1];
  int mode;          /* 0 survival, 1 creative */
  bool cheats;
  int type;          /* WT_* */
  int64_t seed;
  uint32_t played;   /* opt.plays when it was last played (the latest first in the list) */
} WorldInfo;
bool world_info(int slot, WorldInfo *w);   /* false: no world there */
int world_free_slot(void);                 /* 0: all taken */
void world_unique_name(const char *base, char *out);   /* base, or "base (2)"... if taken */
bool save_world(void);
bool load_world(int slot);
void new_world(int slot, int64_t seed, int mode, bool cheats, int type, const char *name);
bool rename_world(int slot, const char *name);
void delete_world(int slot);
void load_options(void);
void save_options(void);
void copy_restore(void);   /* the saves from numblocks_saves.py, when none are left (installed again) */
void copy_write(void);     /* and the copy, up to date (not inside NumPlay, which keeps its own) */
/* the world's settings, saved with it: cheats (Allow Cheats; or opened to LAN with them, until it
 * is left) and the game rules (GameRules) */
extern bool world_cheats, lan_open, lan_cheats;
#define cheats_on() (world_cheats || lan_cheats)
extern uint8_t game_rules;
enum { GR_KEEP_INVENTORY = 1, GR_DAYLIGHT_CYCLE = 2, GR_MOB_SPAWNING = 4, GR_MOB_GRIEFING = 8, GR_NATURAL_REGEN = 16,
       GR_TILE_DROPS = 32, GR_MOB_LOOT = 64, GR_FIRE_TICK = 128 };
#define GR_DEFAULT 0xFE   /* (all on but keepInventory) */
#define rule(r) ((game_rules & (r)) != 0)

/* ---------------------------------------------------------------- chat and commands (command.c) */
enum { CHAT_WHITE, CHAT_RED, CHAT_GREEN, CHAT_DARK_GREEN, CHAT_GRAY };
void chat_add(const char *s, int color);   /* a line in the chat (gui.c) */
void command_run(const char *line);        /* a line sent from the chat: a command ("/...") or said */
/* tab completion: the k-th way to finish the last word of line (the word starts at *at); false: no more */
bool command_complete(const char *line, int k, int *at, char *out, int max);
extern const char *const mc_block_name[198], *const mc_item_name[176];   /* (names.c) */
extern const uint8_t mc_block_item[25];
extern const uint16_t give_key[], give_id[];   /* Minecraft's id << 4 | data, and NumBlocks' id */
extern const int n_give;

/* ---------------------------------------------------------------- drawing (render.c, hud.c) */
#define RW 160
#define RH 120
typedef struct { float x, y, z, yaw, pitch; } Camera;
void render_frame(const Camera *c, uint32_t time_of_day);
/* the HUD and screens draw over each strip of the screen before it is sent */
void hud_strip(uint16_t *buf, int y0, int rows);
void camera_reset(void);
extern uint32_t ticks_run;   /* the player moved at once: the camera with them (main.c) */
#endif
