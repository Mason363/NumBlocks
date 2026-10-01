/* Bare-metal Cortex-M7 benchmark of the generator, run by bench.py in Unicorn.
 * It follows the game's pattern: 24-high slabs (y0 a multiple of 4), a 4 x 3 chunk area at
 * world load, then moves that add a row of 3 or 4 neighbouring chunks.
 * phase is bumped after each call so the runner can count instructions per call. */
#include <stdint.h>
#include <string.h>
#include "../../../src/gen.h"

#ifndef Y0
#define Y0 56
#endif
#ifndef SEED
#define SEED 12345
#endif
#ifndef OX /* chunk offset of the area */
#define OX 0
#endif
#ifndef OZ
#define OZ 0
#endif
#ifndef H
#define H 24
#endif

volatile uint32_t phase;
volatile uint32_t sums[64];
volatile int32_t spawn[3];
static uint8_t out[H * 256];

static int k;
static void slab(int cx, int cz) {
    cx += OX, cz += OZ;
    gen_slab(cx, cz, Y0, H, out);
    uint32_t s = 0;
    for (unsigned i = 0; i < sizeof out; i++) s = s * 31 + out[i];
    for (int i = 0; i < 256; i++) s = s * 31 + (uint32_t)gen_top(cx * 16 + (i & 15), cz * 16 + (i >> 4));
    if (k < 64) sums[k] = s;
    k++;
    phase = phase + 1;
}

int main(void) {
    gen_init(SEED);
    phase = 1;
    /* world load: 4 x 3 chunks */
    for (int cz = -2; cz <= 0; cz++)
        for (int cx = -2; cx <= 1; cx++) slab(cx, cz);
    /* walking +x: rows of 3 chunks */
    for (int cx = 2; cx <= 4; cx++)
        for (int cz = -2; cz <= 0; cz++) slab(cx, cz);
    /* walking +z: rows of 4 chunks */
    for (int cz = 1; cz <= 2; cz++)
        for (int cx = 1; cx <= 4; cx++) slab(cx, cz);
#ifndef NO_SPAWN
    int x, y, z;
    gen_spawn(&x, &y, &z);
    spawn[0] = x, spawn[1] = y, spawn[2] = z;
    phase = phase + 1;
#endif
    return 0;
}

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler(void) {
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss;) *d++ = 0;
    main();
    for (;;) __asm volatile("bkpt #0");
}

__attribute__((section(".isr_vector"), used)) const void *const vectors[2] = {&_estack, (void *)Reset_Handler};
