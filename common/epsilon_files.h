/* Small files in Epsilon's file system, done carefully.
 *
 * Records are packed in a buffer: a 16-bit size, the name ("base.ext" and a
 * zero), then the content; a zero size ends the list. These helpers:
 * - rewrite a record of the same size in place, so nothing moves;
 * - always keep the list terminated when adding a record at the end;
 * - when a record has to be removed from the middle, slide the others down
 *   (like Epsilon does) and clear Epsilon's cache of the last record it looked
 *   up, which would otherwise point into moved data. The cache (a name
 *   checksum and a pointer, after the buffer) is only cleared once found and
 *   verified; if it cannot be, only the last record can be removed.
 *
 * Needs epsilon_app.h. Include in one file. */
#ifndef EPSILON_FILES_H
#define EPSILON_FILES_H
#include "epsilon_app.h"

typedef struct {
  uint8_t *buf;
  uint32_t size;
} ef_fs_t;

static uint16_t ef_rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void ef_wr16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static uint32_t ef_strlen(const char *s) {
  uint32_t n = 0;
  while (s[n]) n++;
  return n;
}

static bool ef_open(ef_fs_t *fs) {
  fs->buf = epsilon_storage(&fs->size);
  return fs->buf != NULL;
}

/* Offset of the terminating zero size, or -1 if the list looks corrupt. */
static int ef_end(const ef_fs_t *fs) {
  uint32_t p = 0;
  while (p + 2 <= fs->size) {
    uint16_t n = ef_rd16(fs->buf + p);
    if (!n) return (int)p;
    if (n < 4 || p + n > fs->size) return -1;
    p += n;
  }
  return -1;
}

/* Offset of the record named `name`, or -1. */
static int ef_find(const ef_fs_t *fs, const char *name, int end) {
  uint32_t len = ef_strlen(name) + 1;
  for (uint32_t p = 0; (int)p < end; p += ef_rd16(fs->buf + p)) {
    uint16_t n = ef_rd16(fs->buf + p);
    if (n < 2 + len) continue;
    uint32_t i = 0;
    while (i < len && fs->buf[p + 2 + i] == (uint8_t)name[i]) i++;
    if (i == len) return (int)p;
  }
  return -1;
}

#if PLATFORM_DEVICE && !defined(HOST)
static uint32_t ef_crc_bytes(const void *data, uint32_t len) {
  if (!len) return 0;
  register uint32_t r0 __asm__("r0") = (uint32_t)(uintptr_t)data;
  register uint32_t r1 __asm__("r1") = len;
  __asm__ volatile("svc 15" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "memory");
  return r0;
}
static uint32_t ef_crc_words(const uint32_t *data, uint32_t words) {
  register uint32_t r0 __asm__("r0") = (uint32_t)(uintptr_t)data;
  register uint32_t r1 __asm__("r1") = words;
  __asm__ volatile("svc 16" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "memory");
  return r0;
}
/* Epsilon's Record checksum: a CRC of the CRCs of the base name and extension. */
static uint32_t ef_name_crc(const char *full) {
  const char *dot = full;
  while (*dot && *dot != '.') dot++;
  if (!*dot) return 0;
  uint32_t parts[2] = {ef_crc_bytes(full, (uint32_t)(dot - full)), ef_crc_bytes(dot + 1, ef_strlen(dot + 1))};
  return ef_crc_words(parts, 2);
}
/* The (accessible size, cached checksum, cached pointer) triple after the
 * buffer's end magic: exactly one candidate must pass every check. */
static uint32_t *ef_cache(const ef_fs_t *fs, int end) {
  uint32_t *after = (uint32_t *)(void *)(fs->buf + fs->size + 4), *found = NULL;
  uint32_t base = (uint32_t)(uintptr_t)fs->buf;
  for (int i = 1; i < 96; i++) {
    uint32_t *w = after + i;
    uint32_t accessible = w[0], crc = w[1], ptr = w[2];
    if (accessible > fs->size || (int)accessible < end + 2 || accessible < fs->size / 2) continue;
    if (!ptr) {
      if (crc) continue;
    } else {
      if (ptr < base || ptr >= base + (uint32_t)end) continue;
      uint32_t p = 0;
      while ((int)p < end && p != ptr - base) p += ef_rd16(fs->buf + p);
      if (p != ptr - base) continue;
      if (!crc || crc != ef_name_crc((const char *)(uintptr_t)(ptr + 2))) continue;
    }
    if (found) return NULL;
    found = w;
  }
  return found;
}
#else
static uint32_t *ef_cache(const ef_fs_t *fs, int end) {
  (void)fs, (void)end;
  return NULL;
}
#endif

/* Removes a record; true if it is gone (or was never there). */
__attribute__((unused)) static bool ef_remove(const char *name) {
  ef_fs_t fs;
  if (!ef_open(&fs)) return false;
  int end = ef_end(&fs);
  if (end < 0) return false;
  int at = ef_find(&fs, name, end);
  if (at < 0) return true;
  uint16_t size = ef_rd16(fs.buf + at);
  if (at + size == end) {  /* the last one: nothing moves */
    ef_wr16(fs.buf + at, 0);
    return true;
  }
  uint32_t *cache = ef_cache(&fs, end);
  if (!cache) return false;
  uint8_t *d = fs.buf + at;
  const uint8_t *s = d + size;
  for (int n = end + 2 - (at + size); n > 0; n--) *d++ = *s++;
  for (int n = size; n > 0; n--) *d++ = 0;
  cache[1] = cache[2] = 0;
  return true;
}

/* Writes a record (creating it at the end if needed). */
__attribute__((unused)) static bool ef_write(const char *name, const void *content, uint32_t len) {
  ef_fs_t fs;
  if (!ef_open(&fs)) return false;
  int end = ef_end(&fs);
  if (end < 0) return false;
  uint32_t nlen = ef_strlen(name) + 1, total = 2 + nlen + len;
  if (total > 0xFFFF) return false;
  int at = ef_find(&fs, name, end);
  if (at >= 0 && ef_rd16(fs.buf + at) != total) {
    /* (no room for the new size: the record stays as it was, not removed first and lost) */
    if ((uint32_t)end - ef_rd16(fs.buf + at) + total + 2 > fs.size) return false;
    if (!ef_remove(name)) return false;
    end = ef_end(&fs);
    at = -1;
  }
  if (at < 0) {
    if ((uint32_t)end + total + 2 > fs.size) return false;
    at = end;
    ef_wr16(fs.buf + at + total, 0);  /* keep the list terminated */
    for (uint32_t i = 0; i < nlen; i++) fs.buf[at + 2 + i] = (uint8_t)name[i];
    ef_wr16(fs.buf + at, total);
  }
  const uint8_t *c = (const uint8_t *)content;
  for (uint32_t i = 0; i < len; i++) fs.buf[at + 2 + nlen + i] = c ? c[i] : 0;
  return true;
}

/* The content of a record (unaligned!) and its length, or NULL. */
__attribute__((unused)) static const uint8_t *ef_read(const char *name, uint32_t *len) {
  ef_fs_t fs;
  if (!ef_open(&fs)) return NULL;
  int end = ef_end(&fs);
  if (end < 0) return NULL;
  int at = ef_find(&fs, name, end);
  if (at < 0) return NULL;
  uint32_t nlen = ef_strlen(name) + 1;
  *len = ef_rd16(fs.buf + at) - 2 - nlen;
  return fs.buf + at + 2 + nlen;
}
#endif
