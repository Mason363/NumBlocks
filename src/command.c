/* The commands typed in the chat, as Minecraft 1.8.8 has them (net.minecraft.command): what they
 * do, what they answer (its words, from en_US.lang), and how the toolbox key finishes their words
 * (addTabCompletionOptions). Without cheats only /help, /me and /seed work, as in a world of one's
 * own that does not allow them. */
#include <math.h>
#include "nb.h"
#pragma GCC optimize("Os")   /* (not where the time goes: small) */

extern uint32_t game_time;
#define PLAYER "Player"
#define MAX_ARGS 8

/* ---------------------------------------------------------------- what is said back */
static char out[160];
static int out_n;
static void put(const char *s) {
  while (*s && out_n < (int)sizeof out - 1) out[out_n++] = *s++;
  out[out_n] = 0;
}
static void put_int(long long v) {
  char b[24];
  int n = 0;
  unsigned long long u = v < 0 ? 0 - (unsigned long long)v : (unsigned long long)v;
  do b[n++] = (char)('0' + u % 10), u /= 10;
  while (u);
  if (v < 0) b[n++] = '-';
  char r[24];
  for (int i = 0; i < n; i++) r[i] = b[n - 1 - i];
  r[n] = 0;
  put(r);
}
/* a double as Java writes one: at least one decimal (64.0, 100.5) */
static void put_num(float v) {
  long long w = (long long)roundf(v * 1000);
  if (w < 0) put("-"), w = -w;
  put_int(w / 1000);
  put(".");
  int f = (int)(w % 1000);
  char d[4] = {(char)('0' + f / 100), (char)('0' + f / 10 % 10), (char)('0' + f % 10), 0};
  int n = 3;
  while (n > 1 && d[n - 1] == '0') d[--n] = 0;
  put(d);
}
/* a translation: %s and %d take the arguments in turn (strings, then numbers) */
static void say(int color, const char *f, const char *s1, const char *s2, long long d1, long long d2) {
  out_n = 0, out[0] = 0;
  const char *s[2] = {s1, s2};
  long long d[2] = {d1, d2};
  int si = 0, di = 0;
  for (; *f; f++) {
    if (*f == '%' && f[1] == 's') put(si < 2 && s[si] ? s[si] : ""), si++, f++;
    else if (*f == '%' && f[1] == 'd') put_int(di < 2 ? d[di] : 0), di++, f++;
    else {
      char c[2] = {*f, 0};
      put(c);
    }
  }
  chat_add(out, color);
}
static void ok(const char *f, const char *s1, long long d1) { say(CHAT_WHITE, f, s1, NULL, d1, 0); }
static void fail(const char *f, const char *s1, long long d1, long long d2) { say(CHAT_RED, f, s1, NULL, d1, d2); }

/* CommandException: the command stops, the chat says why (in red) */
static bool failed;
static void usage(const char *u) {
  if (!failed) say(CHAT_RED, "Usage: %s", u, NULL, 0, 0);
  failed = true;
}
static void error(const char *f, const char *s1, long long d1, long long d2) {
  if (!failed) fail(f, s1, d1, d2);
  failed = true;
}

/* ---------------------------------------------------------------- reading the words */
static bool is_num(const char *s, bool dec) {
  if (*s == '-' || *s == '+') s++;
  if (!*s) return false;
  bool dot = false;
  for (; *s; s++) {
    if (*s == '.' && dec && !dot) dot = true;
    else if (*s < '0' || *s > '9') return false;
  }
  return true;
}
/* CommandBase.parseInt: a whole number from lo to hi */
static long long num(const char *s, long long lo, long long hi) {
  if (failed) return lo;
  if (!is_num(s, false) || strlen(s) > 11) {
    error("'%s' is not a valid number", s, 0, 0);
    return lo;
  }
  long long v = 0;
  const char *p = s + (*s == '-' || *s == '+');
  for (; *p; p++) v = v * 10 + (*p - '0');
  if (*s == '-') v = -v;
  if (v < lo) error("The number you have entered (%d) is too small, it must be at least %d", NULL, v, lo);
  else if (v > hi) error("The number you have entered (%d) is too big, it must be at most %d", NULL, v, hi);
  return v;
}
static float to_float(const char *s) {
  float v = 0, f = 0.1f;
  bool neg = *s == '-';
  if (*s == '-' || *s == '+') s++;
  for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
  if (*s == '.')
    for (s++; *s >= '0' && *s <= '9'; s++) v += (*s - '0') * f, f /= 10;
  return neg ? -v : v;
}
/* CommandBase.parseCoordinate: a number, or "~" (and "~5") from where the player is; whole numbers
 * of x and z at the middle of their block (center) */
static float coord(float base, const char *s, bool center) {
  if (failed) return base;
  bool rel = *s == '~';
  if (rel) s++;
  if (rel && !*s) return base;
  if (!is_num(s, true)) {
    error("'%s' is not a valid number", s - rel, 0, 0);
    return base;
  }
  float v = to_float(s);
  if (rel) return base + v;
  if (center && !strchr(s, '.')) v += 0.5f;
  return v;
}
/* the player named: "Player", or a selector (@p, @a, @r, @e: there is no one else) */
static bool player(const char *s) {
  if (failed) return false;
  if (!strcmp(s, PLAYER) || (s[0] == '@' && strchr("pare", s[1]) && (!s[2] || s[2] == '['))) return true;
  error("That player cannot be found", NULL, 0, 0);
  return false;
}
static int to_int(const char *s) {
  int v = 0;
  bool neg = *s == '-';
  if (*s == '-' || *s == '+') s++;
  for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
  return neg ? -v : v;
}
static const char *strip_ns(const char *s) { return !strncmp(s, "minecraft:", 10) ? s + 10 : s; }
/* a block's id by name or number (Block.getBlockFromName); -1: none */
static int block_named(const char *s) {
  const char *n = strip_ns(s);
  if (is_num(n, false)) {
    int v = to_int(n);
    return v >= 0 && v < 198 && mc_block_name[v][0] ? v : -1;
  }
  for (int i = 0; i < 198; i++)
    if (mc_block_name[i][0] && !strcmp(mc_block_name[i], n)) return i;
  return -1;
}
/* an item's id by name or number (Item.getByNameOrId): items' names first, then blocks that are items */
static int item_named(const char *s) {
  const char *n = strip_ns(s);
  if (is_num(n, false)) {
    int v = to_int(n);
    if (v >= 256 && v < 432 && mc_item_name[v - 256][0]) return v;
    return v >= 0 && v < 198 && (mc_block_item[v >> 3] >> (v & 7) & 1) ? v : -1;
  }
  for (int i = 0; i < 176; i++)
    if (mc_item_name[i][0] && !strcmp(mc_item_name[i], n)) return 256 + i;
  for (int i = 0; i < 198; i++)
    if ((mc_block_item[i >> 3] >> (i & 7) & 1) && !strcmp(mc_block_name[i], n)) return i;
  return -1;
}
/* NumBlocks' item for Minecraft's id and data; -1: none */
static int give_of(int id, int data) {
  int k = id << 4 | data;
  for (int i = 0; i < n_give; i++)
    if (give_key[i] == k) return give_id[i];
  return -1;
}

/* ---------------------------------------------------------------- the commands */
enum { C_CLEAR, C_DIFFICULTY, C_EFFECT, C_GAMEMODE, C_GAMERULE, C_GIVE, C_HELP, C_KILL, C_ME, C_SAY, C_SEED,
       C_SETBLOCK, C_SPAWNPOINT, C_SUMMON, C_TIME, C_TOGGLEDOWNFALL, C_TP, C_WEATHER, C_XP, N_COMMANDS };
static const char *const cmd_name[N_COMMANDS] = {"clear", "difficulty", "effect", "gamemode", "gamerule", "give",
                                                 "help", "kill", "me", "say", "seed", "setblock", "spawnpoint",
                                                 "summon", "time", "toggledownfall", "tp", "weather", "xp"};
static const char *const cmd_usage[N_COMMANDS] = {
    "/clear [player] [item] [data] [maxCount] [dataTag]",
    "/difficulty <new difficulty>",
    "/effect <player> <effect> [seconds] [amplifier] [hideParticles] OR /effect <player> clear",
    "/gamemode <mode> [player]",
    "/gamerule <rule name> [value]",
    "/give <player> <item> [amount] [data] [dataTag]",
    "/help [page|command name]",
    "/kill [player|entity]",
    "/me <action ...>",
    "/say <message ...>",
    "/seed",
    "/setblock <x> <y> <z> <TileName> [dataValue] [oldBlockHandling] [dataTag]",
    "/spawnpoint [player] [<x> <y> <z>]",
    "/summon <EntityName> [x] [y] [z] [dataTag]",
    "/time <set|add|query> <value>",
    "/toggledownfall",
    "/tp [target player] <destination player> OR /tp [target player] <x> <y> <z> [<y-rot> <x-rot>]",
    "/weather <clear|rain|thunder> [duration in seconds]",
    "/xp <amount> [player] OR /xp <amount>L [player]"};
/* (permission level 0, or /seed in a world of one's own) */
static bool allowed(int c) { return cheats_on() || c == C_HELP || c == C_ME || c == C_SEED; }

static const char *const mode_name[2] = {"Survival Mode", "Creative Mode"};
static const char *const modes[4] = {"survival", "creative", "adventure", "spectator"};
static const char *const diffs[4] = {"peaceful", "easy", "normal", "hard"};
static const char *const diff_name[4] = {"Peaceful", "Easy", "Normal", "Hard"};
static const char *const rule_name[8] = {"keepInventory", "doDaylightCycle", "doMobSpawning", "mobGriefing",
                                         "naturalRegeneration", "doTileDrops", "doMobLoot", "doFireTick"};
static const char *const mob_name[8] = {"Zombie", "Skeleton", "Creeper", "Spider", "Pig", "Cow", "Sheep", "Chicken"};
/* the effects NumBlocks has: Potion's ids and names */
static const uint8_t eff_id[3] = {19, 17, 10};
static const char *const eff_key[3] = {"poison", "hunger", "regeneration"};
static const char *const eff_name[3] = {"Poison", "Hunger", "Regeneration"};

/* /gamemode, /difficulty: a name, its first letter, or its number */
static int choice(const char *s, const char *const *names, int n, const char *const *letters) {
  for (int i = 0; i < n; i++)
    if (!strcmp(s, names[i]) || (letters && !strcmp(s, letters[i]))) return i;
  return (int)num(s, 0, n - 1);
}

static void cmd_gamemode(char **a, int n) {
  if (n < 1) return usage(cmd_usage[C_GAMEMODE]);
  static const char *const l[4] = {"s", "c", "a", "sp"};
  int m = choice(a[0], modes, 4, l);
  if (n > 1) player(a[1]);
  if (failed) return;
  if (m > 1) return error("'%s' is not a valid number", a[0], 0, 0);   /* (NumBlocks has no adventure or spectator) */
  pl.mode = (uint8_t)m;
  pl.fall = 0;
  if (m == 0) pl.flying = false;
  chat_add("Your game mode has been updated", CHAT_WHITE);
  ok("Set own game mode to %s", mode_name[m], 0);
}

static void cmd_time(char **a, int n) {
  if (n < 2) return usage(cmd_usage[C_TIME]);
  if (!strcmp(a[0], "set")) {
    long long v = !strcmp(a[1], "day") ? 1000 : !strcmp(a[1], "night") ? 13000 : num(a[1], 0, 0x7FFFFFFF);
    if (failed) return;
    game_time = (uint32_t)v;
    ok("Set the time to %d", NULL, v);
  } else if (!strcmp(a[0], "add")) {
    long long v = num(a[1], 0, 0x7FFFFFFF);
    if (failed) return;
    game_time += (uint32_t)v;
    ok("Added %d to the time", NULL, v);
  } else if (!strcmp(a[0], "query")) {
    if (!strcmp(a[1], "daytime")) ok("Time is %d", NULL, game_time % 24000);
    else if (!strcmp(a[1], "gametime")) ok("Time is %d", NULL, game_time);
    else usage(cmd_usage[C_TIME]);
  } else
    usage(cmd_usage[C_TIME]);
}

static void cmd_weather(char **a, int n) {
  if (n < 1 || n > 2) return usage(cmd_usage[C_WEATHER]);
  int t = (300 + rnd(600)) * 20;
  if (n == 2) t = (int)num(a[1], 1, 1000000) * 20;
  if (failed) return;
  if (!strcmp(a[0], "clear")) {
    weather.rain_time = weather.thunder_time = 0, weather.raining = weather.thundering = 0;
    ok("Changing to clear weather", NULL, 0);
  } else if (!strcmp(a[0], "rain")) {
    weather.rain_time = weather.thunder_time = t, weather.raining = 1, weather.thundering = 0;
    ok("Changing to rainy weather", NULL, 0);
  } else if (!strcmp(a[0], "thunder")) {
    weather.rain_time = weather.thunder_time = t, weather.raining = weather.thundering = 1;
    ok("Changing to rain and thunder", NULL, 0);
  } else
    usage(cmd_usage[C_WEATHER]);
}

static void cmd_give(char **a, int n) {
  if (n < 2) return usage(cmd_usage[C_GIVE]);
  player(a[0]);
  if (failed) return;
  int id = item_named(a[1]);
  if (id < 0) return error("There is no such item with name %s", a[1], 0, 0);
  int count = n > 2 ? (int)num(a[2], 1, 64) : 1, data = n > 3 ? (int)num(a[3], -2147483647, 2147483647) : 0;
  if (failed) return;
  /* data: which kind (wool's colour), or for tools how worn */
  int it = data >= 0 && data <= 15 ? give_of(id, data) : -1;
  if (it < 0) it = give_of(id, 0);
  if (it < 0) return error("There is no such item with name %s", a[1], 0, 0);
  int wear = item_dur(it) ? (data < 0 ? 0 : data > item_dur(it) ? item_dur(it) : data) : 0;
  /* InventoryPlayer.addItemStackToInventory; what does not fit falls at the player's feet */
  int left = inv_add(pl.inv, 36, it, count, wear);
  if (left) ent_drop(it, left, wear, pl.x, pl.y + 0.5f, pl.z, false);
  char lab[40] = "[";
  strcat(lab, item_label(it));
  strcat(lab, "]");
  say(CHAT_WHITE, "Given %s * %d to %s", lab, PLAYER, count, 0);
}

static void cmd_tp(char **a, int n) {
  if (n < 1) return usage(cmd_usage[C_TP]);
  int i = 0;
  if (n == 2 || n == 4 || n == 6) player(a[i++]);
  if (failed) return;
  if (n - i == 1) {
    /* to a player: the only one is here */
    player(a[i]);
    if (!failed) say(CHAT_WHITE, "Teleported %s to %s", PLAYER, PLAYER, 0, 0);
    return;
  }
  if (n - i != 3 && n - i != 5) return usage(cmd_usage[C_TP]);
  float x = coord(pl.x, a[i], true), y = coord(pl.y, a[i + 1], false), z = coord(pl.z, a[i + 2], true);
  float yaw = pl.yaw, pitch = pl.pitch;
  if (n - i == 5) yaw = coord(pl.yaw, a[i + 3], false), pitch = coord(pl.pitch, a[i + 4], false);
  if (failed) return;
  if (x < -30000000 || x > 30000000 || z < -30000000 || z > 30000000 || y < -512 || y > 512)
    return error("'%s' is not a valid number", a[i], 0, 0);
  pl.x = x, pl.y = y, pl.z = z, pl.vx = pl.vy = pl.vz = 0, pl.fall = 0;
  yaw = fmodf(yaw, 360);
  if (yaw >= 180) yaw -= 360;
  if (yaw < -180) yaw += 360;
  pl.yaw = yaw, pl.pitch = pitch < -90 ? -90 : pitch > 90 ? 90 : pitch;
  camera_reset();
  out_n = 0;
  put("Teleported " PLAYER " to "), put_num(x), put(", "), put_num(y), put(", "), put_num(z);
  chat_add(out, CHAT_WHITE);
}

static void cmd_kill(char **a, int n) {
  if (n > 0) player(a[0]);
  if (failed) return;
  /* EntityPlayer.onKillCommand: out of the world */
  player_hurt(3.4e38f, DMG_VOID);
  ok("Killed %s", PLAYER, 0);
}

static void cmd_difficulty(char **a, int n) {
  if (n < 1) return usage(cmd_usage[C_DIFFICULTY]);
  static const char *const l[4] = {"p", "e", "n", "h"};
  int d = choice(a[0], diffs, 4, l);
  if (failed) return;
  opt.difficulty = (uint8_t)d;
  save_options();
  ok("Set game difficulty to %s", diff_name[d], 0);
}

static void cmd_seed(void) {
  out_n = 0;
  put("Seed: "), put_int(world_seed);
  chat_add(out, CHAT_WHITE);
}

static void cmd_spawnpoint(char **a, int n) {
  if (n != 0 && n != 1 && n != 4) return usage(cmd_usage[C_SPAWNPOINT]);
  if (n) player(a[0]);
  int x = (int)floorf(pl.x), y = (int)floorf(pl.y + 0.5f), z = (int)floorf(pl.z);
  if (n == 4) x = (int)floorf(coord((float)x, a[1], false)), y = (int)floorf(coord((float)y, a[2], false)),
    z = (int)floorf(coord((float)z, a[3], false));
  if (failed) return;
  pl.spawn_x = x, pl.spawn_y = y, pl.spawn_z = z;
  out_n = 0;
  put("Set " PLAYER "'s spawn point to ("), put_int(x), put(", "), put_int(y), put(", "), put_int(z), put(")");
  chat_add(out, CHAT_WHITE);
}

static void cmd_xp(char **a, int n) {
  if (n < 1) return usage(cmd_usage[C_XP]);
  char v[16];
  strncpy(v, a[0], 15), v[15] = 0;
  int l = (int)strlen(v);
  bool levels = l && (v[l - 1] == 'l' || v[l - 1] == 'L');
  if (levels) v[l - 1] = 0;
  long long k = num(v, -2147483647, 2147483647);
  if (n > 1) player(a[1]);
  if (failed) return;
  if (levels) {
    /* EntityPlayer.addExperienceLevel */
    pl.xp_level += (int)k;
    if (pl.xp_level < 0) pl.xp_level = 0, pl.xp = 0, pl.xp_total = 0;
    if (k < 0) say(CHAT_WHITE, "Taken %d levels from %s", PLAYER, NULL, -k, 0);
    else say(CHAT_WHITE, "Given %d levels to %s", PLAYER, NULL, k, 0);
  } else {
    if (k < 0) return error("Cannot give player negative experience points", NULL, 0, 0);
    player_add_xp((int)k);
    say(CHAT_WHITE, "Given %d experience to %s", PLAYER, NULL, k, 0);
  }
}

static void cmd_clear(char **a, int n) {
  if (n > 0) player(a[0]);
  int id = -1, data = -1, most = -1;
  if (n > 1) {
    id = item_named(a[1]);
    if (id < 0 && !failed) error("There is no such item with name %s", a[1], 0, 0);
  }
  if (n > 2) data = (int)num(a[2], -1, 2147483647);
  if (n > 3) most = (int)num(a[3], -1, 2147483647);
  if (failed) return;
  /* InventoryPlayer.clearMatchingItems: the inventory, the armour, what the cursor holds */
  Stack *s[41];
  int k = 0;
  for (int i = 0; i < 36; i++) s[k++] = &pl.inv[i];
  for (int i = 0; i < 4; i++) s[k++] = &pl.armor[i];
  s[k++] = &pl.cursor;
  int gone = 0;
  for (int i = 0; i < k && (most < 0 || gone < most); i++) {
    Stack *t = s[i];
    if (!t->id) continue;
    if (id >= 0) {
      int want = data < 0 ? -1 : give_of(id, data & 15);
      bool match = false;
      for (int g = 0; g < n_give && !match; g++)
        if (give_id[g] == t->id && (give_key[g] >> 4) == id && (want < 0 || want == t->id)) match = true;
      if (!match) continue;
    }
    int c = item_count(t), take = most < 0 || most - gone >= c ? c : most - gone;
    gone += take;
    if (take == c) t->id = 0, t->aux = 0;
    else stack_take(t, take);
  }
  if (!gone) return error("Could not clear the inventory of %s, no items to remove", PLAYER, 0, 0);
  say(CHAT_WHITE, "Cleared the inventory of %s, removing %d items", PLAYER, NULL, gone, 0);
}

static void cmd_effect(char **a, int n) {
  if (n < 2) return usage(cmd_usage[C_EFFECT]);
  player(a[0]);
  if (failed) return;
  if (!strcmp(a[1], "clear")) {
    bool any = false;
    for (int i = 0; i < 3; i++) any |= pl.eff[i] > 0, pl.eff[i] = 0, pl.eff_amp[i] = 0;
    if (!any) return error("Couldn't take any effects from %s as they do not have any", PLAYER, 0, 0);
    ok("Took all effects from %s", PLAYER, 0);
    return;
  }
  const char *e = strip_ns(a[1]);
  int k = -1;
  for (int i = 0; i < 3; i++)
    if (!strcmp(e, eff_key[i]) || (is_num(e, false) && to_int(e) == eff_id[i])) k = i;
  if (k < 0) return error(is_num(e, false) ? "There is no such mob effect with ID %d" : "There is no such mob effect with ID %s",
                          e, is_num(e, false) ? to_int(e) : 0, 0);
  int secs = n > 2 ? (int)num(a[2], 0, 1000000) : 30, amp = n > 3 ? (int)num(a[3], 0, 255) : 0;
  if (failed) return;
  if (!secs) {
    if (!pl.eff[k]) {
      out_n = 0;
      put("Couldn't take "), put(eff_name[k]), put(" from " PLAYER " as they do not have the effect");
      return error("%s", out, 0, 0);
    }
    pl.eff[k] = 0;
    out_n = 0;
    put("Took "), put(eff_name[k]), put(" from " PLAYER);
    chat_add(out, CHAT_WHITE);
    return;
  }
  pl.eff[k] = (uint16_t)(secs * 20 > 65535 ? 65535 : secs * 20), pl.eff_amp[k] = (uint8_t)amp;
  out_n = 0;
  put("Given "), put(eff_name[k]), put(" (ID "), put_int(eff_id[k]), put(") * "), put_int(amp + 1);
  put(" to " PLAYER " for "), put_int(secs), put(" seconds");
  chat_add(out, CHAT_WHITE);
}

static void cmd_summon(char **a, int n) {
  if (n < 1) return usage(cmd_usage[C_SUMMON]);
  float x = pl.x, y = pl.y, z = pl.z;
  if (n >= 4) x = coord(x, a[1], true), y = coord(y, a[2], false), z = coord(z, a[3], true);
  if (failed) return;
  if (y < 0 || y >= WORLD_H || !world_loaded((int)floorf(x), (int)floorf(y), (int)floorf(z)))
    return error("Cannot summon the object out of the world", NULL, 0, 0);
  if (!strcmp(a[0], "LightningBolt")) {
    bolt_start(x, y, z);
    ok("Object successfully summoned", NULL, 0);
    return;
  }
  for (int i = 0; i < 8; i++)
    if (!strcmp(a[0], mob_name[i])) {
      if (!mob_summon(E_ZOMBIE + i, x, y, z)) return error("Unable to summon object", NULL, 0, 0);
      ok("Object successfully summoned", NULL, 0);
      return;
    }
  error("Unable to summon object", NULL, 0, 0);
}

static void cmd_setblock(char **a, int n) {
  if (n < 4) return usage(cmd_usage[C_SETBLOCK]);
  int x = (int)floorf(coord(floorf(pl.x), a[0], false)), y = (int)floorf(coord(floorf(pl.y + 0.5f), a[1], false)),
      z = (int)floorf(coord(floorf(pl.z), a[2], false));
  if (failed) return;
  int id = block_named(a[3]);
  if (id < 0) return error("There is no such block with ID/name %s", a[3], 0, 0);
  int data = n > 4 ? (int)num(a[4], 0, 15) : 0;
  if (failed) return;
  if (y < 0 || y >= WORLD_H || !world_loaded(x, y, z)) return error("Cannot place block outside of the world", NULL, 0, 0);
  /* the state: the block's with that data, else its first */
  int b = -1;
  for (int i = 0; i < B_COUNT && b < 0; i++)
    if (blk_id[i] == id && blk_meta[i] == data) b = i;
  for (int i = 0; i < B_COUNT && b < 0; i++)
    if (blk_id[i] == id) b = i;
  if (b < 0) return error("There is no such block with ID/name %s", a[3], 0, 0);
  int was = world_get(x, y, z);
  if (n > 5) {
    if (!strcmp(a[5], "keep") && was != B_AIR) return error("The block couldn't be placed", NULL, 0, 0);
    if (!strcmp(a[5], "destroy")) break_block_at(x, y, z, true);
  }
  if (world_get(x, y, z) == b) return error("The block couldn't be placed", NULL, 0, 0);
  /* (a chest or furnace replaced is emptied first, nothing falls out) */
  tiles_forget(x, y, z);
  world_set(x, y, z, b);
  neighbours_changed(x, y, z);
  ok("Block placed", NULL, 0);
}

static void cmd_gamerule(char **a, int n) {
  if (n == 0) {
    /* all of them */
    out_n = 0;
    for (int i = 0; i < 8; i++) put(i ? ", " : ""), put(rule_name[i]);
    chat_add(out, CHAT_WHITE);
    return;
  }
  int r = -1;
  for (int i = 0; i < 8; i++)
    if (!strcmp(a[0], rule_name[i])) r = i;
  if (r < 0) return error("No game rule called '%s' is available", a[0], 0, 0);
  if (n == 1) {
    out_n = 0;
    put(rule_name[r]), put(" = "), put(rule(1 << r) ? "true" : "false");
    chat_add(out, CHAT_WHITE);
    return;
  }
  /* (GameRules.setOrCreateGameRule: true is "true", anything else false) */
  if (!strcmp(a[1], "true")) game_rules |= (uint8_t)(1 << r);
  else game_rules &= (uint8_t)~(1 << r);
  ok("Game rule has been updated", NULL, 0);
}

static void cmd_help(char **a, int n) {
  /* the commands that may be used, by name, 7 a page */
  int list[N_COMMANDS], k = 0;
  for (int c = 0; c < N_COMMANDS; c++)
    if (allowed(c)) list[k++] = c;
  int pages = (k - 1) / 7, page = 0;
  if (n > 0) {
    for (int c = 0; c < N_COMMANDS; c++)
      if (!strcmp(a[0], cmd_name[c]) && allowed(c)) return usage(cmd_usage[c]);
    if (!is_num(a[0], false)) return error("Unknown command. Try /help for a list of commands", NULL, 0, 0);
    page = (int)num(a[0], 1, pages + 1) - 1;
    if (failed) return;
  }
  say(CHAT_DARK_GREEN, "--- Showing help page %d of %d (/help <page>) ---", NULL, NULL, page + 1, pages + 1);
  for (int i = page * 7; i < k && i < page * 7 + 7; i++) chat_add(cmd_usage[list[i]], CHAT_WHITE);
  if (page == 0)
    chat_add("Tip: Use the toolbox key while typing a command to auto-complete the command or its arguments",
             CHAT_GREEN);
}

/* ---------------------------------------------------------------- running a line */
/* the line cut into words (in place); how many */
static int words(char *s, char **w) {
  int n = 0;
  while (*s && n < MAX_ARGS + 1) {
    while (*s == ' ') *s++ = 0;
    if (!*s) break;
    w[n++] = s;
    while (*s && *s != ' ') s++;
  }
  return n;
}

void command_run(const char *line) {
  if (line[0] != '/') {
    /* said: "<Player> hello" */
    out_n = 0;
    put("<" PLAYER "> "), put(line);
    chat_add(out, CHAT_WHITE);
    return;
  }
  char b[64], *w[MAX_ARGS + 1];
  strncpy(b, line + 1, sizeof b - 1), b[sizeof b - 1] = 0;
  int n = words(b, w);
  int c = -1;
  for (int i = 0; i < N_COMMANDS && n; i++)
    if (!strcmp(w[0], cmd_name[i])) c = i;
  if (n && !strcmp(w[0], "?")) c = C_HELP;
  failed = false;
  if (c < 0) return error("Unknown command. Try /help for a list of commands", NULL, 0, 0);
  if (!allowed(c)) return error("You do not have permission to use this command", NULL, 0, 0);
  char **a = w + 1;
  n--;
  switch (c) {
    case C_CLEAR: cmd_clear(a, n); break;
    case C_DIFFICULTY: cmd_difficulty(a, n); break;
    case C_EFFECT: cmd_effect(a, n); break;
    case C_GAMEMODE: cmd_gamemode(a, n); break;
    case C_GAMERULE: cmd_gamerule(a, n); break;
    case C_GIVE: cmd_give(a, n); break;
    case C_HELP: cmd_help(a, n); break;
    case C_KILL: cmd_kill(a, n); break;
    case C_ME:
    case C_SAY:
      if (n < 1) return usage(cmd_usage[c]);
      out_n = 0;
      put(c == C_ME ? "* " PLAYER " " : "[" PLAYER "] ");
      for (int i = 0; i < n; i++) put(i ? " " : ""), put(a[i]);
      chat_add(out, CHAT_WHITE);
      break;
    case C_SEED: cmd_seed(); break;
    case C_SETBLOCK: cmd_setblock(a, n); break;
    case C_SPAWNPOINT: cmd_spawnpoint(a, n); break;
    case C_SUMMON: cmd_summon(a, n); break;
    case C_TIME: cmd_time(a, n); break;
    case C_TOGGLEDOWNFALL:
      weather.raining = !weather.raining;
      ok("Toggled downfall", NULL, 0);
      break;
    case C_TP: cmd_tp(a, n); break;
    case C_WEATHER: cmd_weather(a, n); break;
    case C_XP: cmd_xp(a, n); break;
  }
}

/* ---------------------------------------------------------------- finishing words */
/* CommandBase.getListOfStringsMatchingLastWord: the names that start as the word does; blocks and
 * items with "minecraft:" before them (found by the name without it too) */
static int cmp_n;   /* (the ways found so far, while looking for the k-th) */
static bool offer(const char *word, const char *name, const char *ns, int k, char *o, int max) {
  int nl = (int)strlen(ns);
  bool full = !strncmp(word, ns, strlen(word) < (size_t)nl ? strlen(word) : (size_t)nl) &&
              (strlen(word) <= (size_t)nl || !strncmp(word + nl, name, strlen(word) - nl));
  bool bare = !strncmp(word, name, strlen(word));
  if (!full && !bare) return false;
  if (cmp_n++ != k) return false;
  int i = 0;
  for (const char *p = ns; *p && i < max - 1; p++) o[i++] = *p;
  for (const char *p = name; *p && i < max - 1; p++) o[i++] = *p;
  o[i] = 0;
  return true;
}
static bool offer_list(const char *word, const char *const *names, int n, int k, char *o, int max) {
  for (int i = 0; i < n; i++)
    if (offer(word, names[i], "", k, o, max)) return true;
  return false;
}

bool command_complete(const char *line, int k, int *at, char *o, int max) {
  if (line[0] != '/') return false;
  /* the words before the last one, and the last (maybe empty) */
  int len = (int)strlen(line), st = len;
  while (st > 1 && line[st - 1] != ' ') st--;
  *at = st;
  const char *word = line + st;
  char b[64], *w[MAX_ARGS + 1];
  int pl_len = st - 1 < (int)sizeof b - 1 ? st - 1 : (int)sizeof b - 1;
  memcpy(b, line + 1, (size_t)(pl_len > 0 ? pl_len : 0)), b[pl_len > 0 ? pl_len : 0] = 0;
  int n = words(b, w);
  cmp_n = 0;
  static const char *const players[1] = {PLAYER};
  if (n == 0) {
    /* the command's name */
    for (int c = 0; c < N_COMMANDS; c++)
      if (allowed(c) && offer(word, cmd_name[c], "", k, o, max)) return true;
    return false;
  }
  int c = -1;
  for (int i = 0; i < N_COMMANDS; i++)
    if (!strcmp(w[0], cmd_name[i]) && allowed(i)) c = i;
  int arg = n - 1;   /* which argument the word is */
  static const char *const time_a[3] = {"set", "add", "query"}, *const time_set[2] = {"day", "night"},
                           *const time_q[2] = {"daytime", "gametime"}, *const weathers[3] = {"clear", "rain", "thunder"},
                           *const bools[2] = {"true", "false"}, *const handling[3] = {"replace", "destroy", "keep"};
  switch (c) {
    case C_GAMEMODE: return arg == 0 ? offer_list(word, modes, 4, k, o, max) : arg == 1 && offer_list(word, players, 1, k, o, max);
    case C_TIME:
      if (arg == 0) return offer_list(word, time_a, 3, k, o, max);
      if (arg == 1 && !strcmp(w[1], "set")) return offer_list(word, time_set, 2, k, o, max);
      if (arg == 1 && !strcmp(w[1], "query")) return offer_list(word, time_q, 2, k, o, max);
      return false;
    case C_WEATHER: return arg == 0 && offer_list(word, weathers, 3, k, o, max);
    case C_DIFFICULTY: return arg == 0 && offer_list(word, diffs, 4, k, o, max);
    case C_GAMERULE:
      if (arg == 0) return offer_list(word, rule_name, 8, k, o, max);
      return arg == 1 && offer_list(word, bools, 2, k, o, max);
    case C_EFFECT:
      if (arg == 0) return offer_list(word, players, 1, k, o, max);
      if (arg == 1) {
        for (int i = 0; i < 3; i++)
          if (offer(word, eff_key[i], "minecraft:", k, o, max)) return true;
        return offer(word, "clear", "", k, o, max);
      }
      return false;
    case C_SUMMON:
      if (arg == 0) {
        static const char *const bolt_name[1] = {"LightningBolt"};
        return offer_list(word, mob_name, 8, k, o, max) || offer_list(word, bolt_name, 1, k, o, max);
      }
      return false;
    case C_GIVE:
    case C_CLEAR:
      if (arg == 0) return offer_list(word, players, 1, k, o, max);
      if (arg == 1) {
        for (int i = 0; i < 198; i++)
          if ((mc_block_item[i >> 3] >> (i & 7) & 1) && offer(word, mc_block_name[i], "minecraft:", k, o, max)) return true;
        for (int i = 0; i < 176; i++)
          if (mc_item_name[i][0] && offer(word, mc_item_name[i], "minecraft:", k, o, max)) return true;
      }
      return false;
    case C_SETBLOCK:
      if (arg < 3) {
        /* the block looked at (CommandBase.func_175771_a) */
        if (pl.hit_face < 0) return false;
        int v = arg == 0 ? pl.hit_x : arg == 1 ? pl.hit_y : pl.hit_z;
        char t[12];
        out_n = 0, put_int(v), strcpy(t, out);
        return offer(word, t, "", k, o, max);
      }
      if (arg == 3) {
        for (int i = 0; i < 198; i++)
          if (mc_block_name[i][0] && offer(word, mc_block_name[i], "minecraft:", k, o, max)) return true;
        return false;
      }
      return arg == 5 && offer_list(word, handling, 3, k, o, max);
    case C_TP: case C_KILL: case C_SPAWNPOINT: case C_XP:
      return arg == (c == C_XP) && offer_list(word, players, 1, k, o, max);
    case C_HELP: {
      if (arg) return false;
      for (int i = 0; i < N_COMMANDS; i++)
        if (allowed(i) && offer(word, cmd_name[i], "", k, o, max)) return true;
      return false;
    }
  }
  return false;
}
