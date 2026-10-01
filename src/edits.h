/* The player's changes to the world (the generator makes everything else). */
#ifndef NB_EDITS_H
#define NB_EDITS_H
#include <stdbool.h>
#include <stdint.h>

void edits_setup(const char *world, uint32_t *scratch, uint32_t n);   /* the world's record prefix; a merge buffer */
void edits_clear(void);
bool edits_put(int x, int y, int z, int b);      /* false: no room left to keep it */
bool edits_flush(void);                          /* the journal into storage; false if some did not fit */
bool edits_full(void);
void edits_forget(void);   /* storage changed: records may have moved */
void edits_apply(int cx, int cz, int y0, int h, uint8_t *slab);   /* onto a chunk from gen_slab */
int edits_top(int x, int z, int top);            /* the column's sky top with the edits (1 + y) */
int edits_count(void);
void *edits_scratch(uint32_t n);   /* in the journal */
#endif
