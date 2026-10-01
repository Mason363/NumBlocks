/* Plays NumBlocks on a computer with scripted keys and saves screenshots.
 *
 *   play [--frames N] [--ms-per-frame M] [--keys "10-40:fwd,50:use,..."] [--shots 30,60] [--out DIR]
 *
 * Keys: left right up down fwd back sleft sright jump use attack inv sneak sprint pause 1..9. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../src/nb.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void game_init(void);
bool game_frame(void);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[512];
static int nholds;

static uint32_t key_bit(const char *s) {
  static const struct { const char *n; uint32_t k; } t[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"fwd", K_FWD}, {"back", K_BACKW},
    {"sleft", K_STRAFE_L}, {"sright", K_STRAFE_R}, {"jump", K_JUMP}, {"use", K_USE}, {"attack", K_ATTACK},
    {"inv", K_INV}, {"sneak", K_SNEAK}, {"sprint", K_SPRINT}, {"pause", K_PAUSE}, {"home", K_HOME},
    {"ok", K_OK | K_USE}, {"exe", K_EXE | K_USE}, {"shift", K_SHIFT}, {"esc", K_BACK | K_ATTACK}, {"drop", K_DROP}};
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    if (!strcmp(s, t[i].n)) return t[i].k;
  if (s[0] >= '1' && s[0] <= '9' && !s[1]) return K_SLOT1 << (s[0] - '1');
  return 0;
}

int main(int argc, char **argv) {
  const char *out = "build/play", *shots = "";
  int give = 0, title_screen = 0, mobs = 0, start_time = -1, torches = 0, water = 0, creative = 0, build = 0, rain = 0;
  float hurt = 0;
  int frames = 60, mspf = 50;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--ms-per-frame") && i + 1 < argc) mspf = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
    else if (!strcmp(argv[i], "--give")) give = 1;
    else if (!strcmp(argv[i], "--title")) title_screen = 1;
    else if (!strcmp(argv[i], "--mobs")) mobs = 1;
    else if (!strcmp(argv[i], "--torches")) torches = 1;
    else if (!strcmp(argv[i], "--water")) water = 1;
    else if (!strcmp(argv[i], "--creative")) creative = 1;
    else if (!strcmp(argv[i], "--build")) build = 1;
    else if (!strcmp(argv[i], "--rain")) rain = 1;
    else if (!strcmp(argv[i], "--storm")) rain = 2;
    else if (!strcmp(argv[i], "--time") && i + 1 < argc) start_time = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
      extern int64_t start_seed;
      start_seed = strtoll(argv[++i], NULL, 10);
    }
    else if (!strcmp(argv[i], "--hurt") && i + 1 < argc) hurt = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
      char *s = strdup(argv[++i]);
      for (char *t = strtok(s, ","); t && nholds < 512; t = strtok(NULL, ",")) {
        int a, b;
        char k[16];
        if (sscanf(t, "%d-%d:%15s", &a, &b, k) == 3) holds[nholds++] = (Hold){a, b, key_bit(k)};
        else if (sscanf(t, "%d:%15s", &a, k) == 2) holds[nholds++] = (Hold){a, a, key_bit(k)};
      }
    }
  }
  mkdir(out, 0755);
  clock_t c0 = clock();
  extern bool start_in_world;
  start_in_world = !title_screen;
  game_init();
  if (give) {
    /* a test kit: tools, blocks, food, armour */
    static const int kit[][2] = {{I_IRON_PICKAXE, 1}, {I_DIAMOND_SWORD, 1}, {B_COBBLESTONE, 64}, {B_PLANKS_OAK, 23},
                                 {B_TORCH, 16}, {I_BREAD, 5}, {B_CRAFTING_TABLE, 1}, {B_FURNACE, 1}, {B_CHEST, 1},
                                 {B_LOG_OAK, 12}, {I_COAL, 9}, {B_IRON_ORE, 7}, {I_IRON_HELMET, 1},
                                 {I_IRON_CHESTPLATE, 1}, {I_STICK, 10}, {I_WHEAT_SEEDS, 3}, {I_WOODEN_HOE, 1},
                                 {I_BUCKET, 1}, {B_SAND, 20}, {I_APPLE, 2}};
    for (unsigned i = 0; i < sizeof kit / sizeof kit[0]; i++) inv_add(pl.inv, 36, kit[i][0], kit[i][1], 0);
    pl.inv[0].aux = 100;   /* a worn pickaxe */
  }
  if (start_time >= 0) {
    extern uint32_t game_time;
    game_time = (uint32_t)start_time;
  }
  if (creative) pl.mode = 1;
  if (rain) {
    /* raining (and storming) from the start, for long enough */
    weather.raining = 1, weather.rain_time = 24000, rain_str = 1;
    if (rain == 2) weather.thundering = 1, weather.thunder_time = 24000, thunder_str = 1;
  }
  if (build) {
    /* a cleared field with the shaped blocks in a row: stairs, door, bed, chest, furnace, fence, pane, ladder */
    int fy = (int)pl.y, px = (int)pl.x, pz = (int)pl.z;
    for (int z = pz - 3; z < pz + 12; z++)
      for (int x = px - 9; x < px + 10; x++)
        for (int y = fy - 1; y < fy + 8; y++) world_set(x, y, z, y == fy - 1 ? B_GRASS : B_AIR);
    int z = pz + 4;
    world_set(px + 5, fy, z, B_OAK_STAIRS);
    world_set(px + 4, fy, z, B_COBBLESTONE_STAIRS_S);
    world_set(px + 2, fy, z, B_DOOR_OAK_LOWER_N), world_set(px + 2, fy + 1, z, B_DOOR_OAK_UPPER);
    world_set(px + 1, fy, z, B_DOOR_OAK_LOWER_N_OPEN), world_set(px + 1, fy + 1, z, B_DOOR_OAK_UPPER);
    world_set(px - 1, fy, z, B_BED_FOOT), world_set(px - 1, fy, z + 1, B_BED_HEAD);
    world_set(px - 3, fy, z, B_CHEST);
    world_set(px - 4, fy, z, B_FURNACE);
    for (int k = 0; k < 3; k++) world_set(px - 6 - k, fy, z, B_FENCE_OAK);
    world_set(px - 6, fy, z + 1, B_FENCE_OAK);
    for (int k = 0; k < 2; k++) world_set(px + 7 + k, fy, z, B_GLASS_PANE), world_set(px + 7 + k, fy + 1, z, B_GLASS_PANE);
    world_set(px + 3, fy, z + 2, B_COBBLESTONE), world_set(px + 3, fy + 1, z + 2, B_COBBLESTONE);
    world_set(px + 3, fy, z + 1, B_LADDER), world_set(px + 3, fy + 1, z + 1, B_LADDER);
    world_set(px - 2, fy + 1, z + 3, B_TORCH_N);
    world_set(px - 2, fy + 1, z + 4, B_COBBLESTONE);
  }
  if (water) {
    /* a cleared field with a water source on a step */
    int fy = (int)pl.y;
    for (int z = (int)pl.z - 3; z < (int)pl.z + 12; z++)
      for (int x = (int)pl.x - 9; x < (int)pl.x + 10; x++)
        for (int y = fy - 1; y < fy + 8; y++) world_set(x, y, z, y == fy - 1 ? B_GRASS : B_AIR);
    world_set((int)pl.x, fy, (int)pl.z + 6, B_COBBLESTONE);
    world_set((int)pl.x, fy + 1, (int)pl.z + 6, B_WATER);
    neighbours_changed((int)pl.x, fy + 1, (int)pl.z + 6);
  }
  if (mobs) {
    /* a row of every mob, 4 blocks ahead, facing the player, on a cleared grass field */
    int fy = (int)pl.y;
    for (int z = (int)pl.z - 3; z < (int)pl.z + 9; z++)
      for (int x = (int)pl.x - 9; x < (int)pl.x + 10; x++)
        for (int y = fy - 1; y < fy + 8; y++) world_set(x, y, z, y == fy - 1 ? B_GRASS : B_AIR);
    for (int k = 0; k < 8; k++) {
      float x = pl.x - 5.6f + k * 1.6f, z = pl.z + 4.5f;
      Entity *e = ent_new(E_ZOMBIE + k, x, (float)fy, z);
      if (e) e->health = 20, e->yaw = 180, e->gx = e->x, e->gz = e->z;
    }
  }
  if (torches) {
    /* torches around, for the night */
    int fy = (int)pl.y;
    for (int k = 0; k < 3; k++) world_set((int)pl.x - 3 + k * 3, fy, (int)pl.z + 3 + k, B_TORCH);
    world_set((int)pl.x + 2, fy, (int)pl.z + 6, B_GLOWSTONE);
  }
  if (hurt > 0) pl.health -= hurt, pl.food -= 7, pl.xp_level = 7, pl.xp = 0.4f;
  double init_ms = (clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
  c0 = clock();
  double slowest = 0;
  int slowest_at = 0;
  for (int f = 0; f < frames; f++) {
    host_keys = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) host_keys |= holds[i].k;
    host_time += mspf;
    clock_t f0 = clock();
    if (!game_frame()) break;
    double fms = (clock() - f0) * 1000.0 / CLOCKS_PER_SEC;
    if (fms > slowest) slowest = fms, slowest_at = f;
    for (const char *s = shots; *s;) {
      if (atoi(s) == f) {
        char p[512];
        snprintf(p, sizeof p, "%s/shot_%d.ppm", out, f);
        host_shot(p);
      }
      const char *c = strchr(s, ',');
      if (!c) break;
      s = c + 1;
    }
  }
  extern unsigned long st_steps, st_texels, st_jumps, st_pixels;
  printf("per frame: %.0f traces, %.0f steps\n", (double)st_pixels / frames, (double)st_steps / frames);
  extern unsigned long st_dis, st_k0, st_fpfail, st_fpok, st_sky;
  printf("between per frame: %.0f disagree, %.0f kind0, %.0f face fail, %.0f face ok, %.0f sky\n", (double)st_dis / frames, (double)st_k0 / frames, (double)st_fpfail / frames, (double)st_fpok / frames, (double)st_sky / frames);
  printf("per pixel: %.1f steps, %.2f texels, %.2f jumps\n", (double)st_steps / st_pixels, (double)st_texels / st_pixels, (double)st_jumps / st_pixels);
  printf("init %.1f ms, %.2f ms a frame (host), the slowest %.1f ms (frame %d)\n", init_ms,
         (clock() - c0) * 1000.0 / CLOCKS_PER_SEC / frames, slowest, slowest_at);
  return 0;
}
