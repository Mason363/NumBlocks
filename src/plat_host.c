/* A computer: the screen goes to a frame buffer that tests save as images,
 * keys come from a script, time is simulated (tests/play.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nb.h"

uint16_t host_fb[SCREEN_W * SCREEN_H];
uint32_t host_time;               /* ms */
uint32_t host_keys;

uint32_t plat_keys(void) { return host_keys; }
uint64_t host_raw;                 /* (tests: keys as the calculator numbers them) */
uint64_t plat_scan(void) { return host_raw; }
uint32_t plat_millis(void) { return host_time; }
void plat_sleep(uint32_t ms) { host_time += ms; }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if ((unsigned)(x + c) < SCREEN_W && (unsigned)(y + r) < SCREEN_H) host_fb[(y + r) * SCREEN_W + x + c] = px[r * w + c];
}

void plat_pull(int x, int y, int w, int h, uint16_t *px) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      px[r * w + c] = (unsigned)(x + c) < SCREEN_W && (unsigned)(y + r) < SCREEN_H ? host_fb[(y + r) * SCREEN_W + x + c] : 0;
}

static char save_dir[256] = "build/host-saves";
void host_save_dir(const char *d) { snprintf(save_dir, sizeof save_dir, "%s", d); }
bool plat_save(const char *name, const void *data, uint32_t len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", save_dir, name);
  FILE *f = fopen(p, "wb");
  if (!f) return false;
  fwrite(data, 1, len, f);
  fclose(f);
  return true;
}
static uint8_t loaded[65536];
const uint8_t *plat_load(const char *name, uint32_t *len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", save_dir, name);
  FILE *f = fopen(p, "rb");
  if (!f) return NULL;
  *len = (uint32_t)fread(loaded, 1, sizeof loaded, f);
  fclose(f);
  return loaded;
}
#include <dirent.h>
static uint8_t *open_buf;
static uint32_t open_len;
uint8_t *plat_save_open(const char *name, uint32_t len) {
  (void)name;
  free(open_buf);
  open_buf = calloc(1, len ? len : 1), open_len = len;
  return open_buf;
}
void plat_save_close(const char *name) { plat_save(name, open_buf, open_len); }
bool plat_record(int i, char *name, int max) {
  DIR *d = opendir(save_dir);
  if (!d) return false;
  struct dirent *e;
  bool found = false;
  while ((e = readdir(d)))
    if (e->d_name[0] != '.' && i-- == 0) {
      snprintf(name, (size_t)max, "%s", e->d_name);
      found = true;
      break;
    }
  closedir(d);
  return found;
}
bool host_launcher;
bool plat_in_launcher(void) { return host_launcher; }
void plat_remove_prefix(const char *prefix) {
  DIR *d = opendir(save_dir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)))
    if (!strncmp(e->d_name, prefix, strlen(prefix))) {
      char p[600];
      snprintf(p, sizeof p, "%s/%s", save_dir, e->d_name);
      remove(p);
    }
  closedir(d);
}
uint32_t plat_storage_free(void) { return 30000; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

void host_shot(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t c = host_fb[i];
    uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
