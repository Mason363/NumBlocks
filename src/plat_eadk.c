/* The calculator: EADK display, keyboard, time and files.
 *
 * The keys are as Options > Controls has them (gui.c, keys_of). */
#include <eadk.h>
#include "../common/epsilon_app.h"
#include "../common/epsilon_files.h"
#include "nb.h"

uint32_t plat_keys(void) { return keys_of(eadk_keyboard_scan()); }

uint64_t plat_scan(void) { return eadk_keyboard_scan(); }
uint32_t plat_millis(void) { return (uint32_t)eadk_timing_millis(); }
void plat_sleep(uint32_t ms) { eadk_timing_msleep(ms); }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
void plat_pull(int x, int y, int w, int h, uint16_t *px) {
  eadk_display_pull_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
/* (no room: the copies of the saves, only copies, make way for the saves themselves) */
bool plat_save(const char *name, const void *data, uint32_t len) {
  if (ef_write(name, data, len)) return true;
  ef_remove("numblocks_saves.py");
  ef_remove("numplay_saves.py");
  return ef_write(name, data, len);
}
uint8_t *plat_save_open(const char *name, uint32_t len) {
  uint32_t n;
  return ef_write(name, NULL, len) ? (uint8_t *)(uintptr_t)ef_read(name, &n) : NULL;
}
void plat_save_close(const char *name) { (void)name; }
bool plat_record(int i, char *name, int max) {
  ef_fs_t fs;
  if (!ef_open(&fs)) return false;
  int end = ef_end(&fs);
  for (uint32_t p = 0; end >= 0 && (int)p < end; p += ef_rd16(fs.buf + p))
    if (i-- == 0) {
      const char *n = (const char *)fs.buf + p + 2;
      int k = 0;
      for (; n[k] && k < max - 1; k++) name[k] = n[k];
      name[k] = 0;
      return true;
    }
  return false;
}
bool plat_in_launcher(void) { return numplay_launcher != NULL; }
const uint8_t *plat_load(const char *name, uint32_t *len) { return ef_read(name, len); }

/* removes every record whose name starts with `prefix` */
void plat_remove_prefix(const char *prefix) {
  for (bool again = true; again;) {
    again = false;
    ef_fs_t fs;
    if (!ef_open(&fs)) return;
    int end = ef_end(&fs);
    if (end < 0) return;
    for (uint32_t p = 0; (int)p < end; p += ef_rd16(fs.buf + p)) {
      const char *n = (const char *)fs.buf + p + 2;
      uint32_t i = 0;
      while (prefix[i] && n[i] == prefix[i]) i++;
      if (!prefix[i]) {
        char name[40];
        uint32_t k = 0;
        while (n[k] && k < sizeof name - 1) name[k] = n[k], k++;
        name[k] = 0;
        if (!ef_remove(name)) return;
        again = true;   /* records moved: start over */
        break;
      }
    }
  }
}

/* bytes left in the calculator's storage */
uint32_t plat_storage_free(void) {
  ef_fs_t fs;
  if (!ef_open(&fs)) return 0;
  int end = ef_end(&fs);
  return end < 0 ? 0 : fs.size - (uint32_t)end - 2;
}
void plat_begin(void) { np_app_begin(); }
int plat_end(void) { return np_app_end(); }
