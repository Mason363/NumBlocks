/* The player's changes to the world, saved as they happen.
 *
 * The world is cut in regions of 4 x 4 chunks (64 x 64 columns). Each region
 * the player changed has a record in the calculator's storage, read where it
 * lies: sorted 32-bit entries, chunk (4 bits), x and z in it (8), y (7), then
 * the block (8, the top byte). New changes go to a small journal in RAM, kept
 * in the same order; when it fills up (and when the game saves), its entries
 * are merged into the records. */
#include "edits.h"
#include "nb.h"
#pragma GCC optimize("Os")   /* (not where the time goes: small) */

typedef struct {
  int16_t rx, rz;
  uint32_t e;   /* the entry, as in the records */
} Journal;

#define J_MAX 256
static Journal jn[J_MAX];
static int nj;
static char prefix[8] = "nb1";
static bool full;   /* a record could not grow: storage is full */
static uint32_t *scratch;
static uint32_t scratch_n;

void edits_setup(const char *world, uint32_t *buf, uint32_t n) {
  int i = 0;
  for (; world[i] && i < 7; i++) prefix[i] = world[i];
  prefix[i] = 0;
  scratch = buf;
  scratch_n = n;
}
bool edits_full(void) { return full; }

static inline int floordiv(int a, int d) { return a >= 0 ? a / d : -((-a + d - 1) / d); }

/* "nb1r-3_12.nbe" */
static void region_name(char *out, int rx, int rz) {
  char *p = out;
  for (const char *s = prefix; *s;) *p++ = *s++;
  *p++ = 'r';
  int v[2] = {rx, rz};
  for (int k = 0; k < 2; k++) {
    if (k) *p++ = '_';
    int n = v[k];
    if (n < 0) *p++ = '-', n = -n;
    char t[8];
    int m = 0;
    do t[m++] = (char)('0' + n % 10), n /= 10;
    while (n);
    while (m) *p++ = t[--m];
  }
  const char *ext = ".nbe";
  while (*ext) *p++ = *ext++;
  *p = 0;
}

/* a region's record (the last one looked up is remembered until records change) */
static bool cache_ok;
static int cache_rx, cache_rz, cache_n;
static const uint8_t *cache_r;
static const uint8_t *region(int rx, int rz, int *n) {
  if (!cache_ok || rx != cache_rx || rz != cache_rz) {
    char name[32];
    region_name(name, rx, rz);
    uint32_t len = 0;
    const uint8_t *d = plat_load(name, &len);
    cache_r = d;
    cache_n = d ? (int)(len / 4) : 0;
    cache_rx = rx, cache_rz = rz, cache_ok = true;
  }
  *n = cache_n;
  return cache_r;
}

void edits_clear(void) { nj = 0, full = false, cache_ok = false; }
void edits_forget(void) { cache_ok = false; }

/* records are not aligned in storage: entries are read a byte at a time */
static inline uint32_t rd32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
#define R(i) rd32(r + 4 * (i))

/* entries are sorted by their low 24 bits */
#define KEY(e) ((e) & 0xFFFFFF)
static inline uint32_t entry_key(int x, int y, int z) {
  int cx = floordiv(x, 16), cz = floordiv(z, 16);
  int chunk = (cx & 3) | (cz & 3) << 2;
  int xz = (x & 15) | (z & 15) << 4;
  return (uint32_t)(chunk << 15 | xz << 7 | (y & 127));
}

static int jcmp(int rx, int rz, uint32_t key, const Journal *j) {
  if (rz != j->rz) return rz < j->rz ? -1 : 1;
  if (rx != j->rx) return rx < j->rx ? -1 : 1;
  uint32_t k = KEY(j->e);
  return key < k ? -1 : key > k;
}
static int jlower(int rx, int rz, uint32_t key) {
  int lo = 0, hi = nj;
  while (lo < hi) {
    int m = (lo + hi) / 2;
    if (jcmp(rx, rz, key, &jn[m]) > 0) lo = m + 1;
    else hi = m;
  }
  return lo;
}
static int rlower(const uint8_t *r, int n, uint32_t key) {
  int lo = 0, hi = n;
  while (lo < hi) {
    int m = (lo + hi) / 2;
    if (KEY(R(m)) < key) lo = m + 1;
    else hi = m;
  }
  return lo;
}

/* the journal's entries go into their regions' records */
bool edits_flush(void) {
  bool ok = true;
  int i = 0;
  while (i < nj) {
    int rx = jn[i].rx, rz = jn[i].rz, j = i;
    while (j < nj && jn[j].rx == rx && jn[j].rz == rz) j++;
    int n;
    const uint8_t *r = region(rx, rz, &n);
    /* merge the record and the journal's entries for it into the scratch buffer */
    uint32_t m = 0;
    int a = 0, b = i;
    while ((a < n || b < j) && m < scratch_n) {
      if (b >= j || (a < n && KEY(R(a)) < KEY(jn[b].e))) scratch[m++] = R(a), a++;
      else {
        if (a < n && KEY(R(a)) == KEY(jn[b].e)) a++;
        scratch[m++] = jn[b++].e;
      }
    }
    char name[32];
    region_name(name, rx, rz);
    cache_ok = false;
    if (a < n || b < j || !plat_save(name, scratch, m * 4)) {
      /* no room: these changes stay in the journal */
      ok = false;
      full = true;
      i = j;
      continue;
    }
    memmove(&jn[i], &jn[j], (size_t)(nj - j) * sizeof jn[0]);
    nj -= j - i;
  }
  return ok;
}

bool edits_put(int x, int y, int z, int b) {
  int rx = floordiv(x, 64), rz = floordiv(z, 64);
  uint32_t key = entry_key(x, y, z);
  int i = jlower(rx, rz, key);
  if (i < nj && jcmp(rx, rz, key, &jn[i]) == 0) {
    jn[i].e = key | (uint32_t)b << 24;
    return true;
  }
  if (nj >= J_MAX) {
    edits_flush();
    if (nj >= J_MAX) return false;
    i = jlower(rx, rz, key);
  }
  memmove(&jn[i + 1], &jn[i], (size_t)(nj - i) * sizeof jn[0]);
  jn[i] = (Journal){(int16_t)rx, (int16_t)rz, key | (uint32_t)b << 24};
  nj++;
  return true;
}

/* onto a chunk from gen_slab: the record's entries, then the journal's */
void edits_apply(int cx, int cz, int y0, int h, uint8_t *slab) {
  int rx = floordiv(cx, 4), rz = floordiv(cz, 4), chunk = (cx & 3) | (cz & 3) << 2;
  uint32_t k0 = (uint32_t)chunk << 15, k1 = k0 + (1 << 15);
  int n;
  const uint8_t *r = region(rx, rz, &n);
  for (int i = rlower(r, n, k0); i < n && KEY(R(i)) < k1; i++) {
    uint32_t e = R(i);
    int y = (int)(e & 127) - y0, xz = (int)(e >> 7) & 255;
    if (y >= 0 && y < h) slab[(y * 16 + (xz >> 4)) * 16 + (xz & 15)] = (uint8_t)(e >> 24);
  }
  for (int i = jlower(rx, rz, k0); i < nj && jn[i].rx == rx && jn[i].rz == rz && KEY(jn[i].e) < k1; i++) {
    int y = (int)(jn[i].e & 127) - y0, xz = (int)(jn[i].e >> 7) & 255;
    if (y >= 0 && y < h) slab[(y * 16 + (xz >> 4)) * 16 + (xz & 15)] = (uint8_t)(jn[i].e >> 24);
  }
}

/* the column's sky top (1 + the highest opaque y) with the changes */
int edits_top(int x, int z, int top) {
  int rx = floordiv(x, 64), rz = floordiv(z, 64);
  uint32_t k0 = entry_key(x, 0, z), k1 = k0 + 128;
  int n;
  const uint8_t *r = region(rx, rz, &n);
  for (int i = rlower(r, n, k0); i < n && KEY(R(i)) < k1; i++) {
    uint32_t e = R(i);
    if ((blk_flags[e >> 24] & BF_OPAQUE) && (int)(e & 127) + 1 > top) top = (int)(e & 127) + 1;
  }
  for (int i = jlower(rx, rz, k0); i < nj && jn[i].rx == rx && jn[i].rz == rz && KEY(jn[i].e) < k1; i++)
    if ((blk_flags[jn[i].e >> 24] & BF_OPAQUE) && (int)(jn[i].e & 127) + 1 > top) top = (int)(jn[i].e & 127) + 1;
  return top;
}

int edits_count(void) { return nj; }

/* the merge buffer, lent out between merges (n bytes at most) */
void *edits_scratch(uint32_t n) { return n <= scratch_n * 4 ? scratch : NULL; }
