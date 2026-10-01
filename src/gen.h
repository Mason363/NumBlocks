/* NumBlocks world generator: a port of Minecraft 1.8.8's default overworld
 * generator (ChunkProviderGenerate and everything it calls), sized for the
 * NumWorks calculator (all state static, no malloc, about 28 KB of RAM).
 * See the comment at the top of gen.c for what differs from Minecraft. */
#ifndef NB_GEN_H
#define NB_GEN_H
#include <stdint.h>

/* Starts a world: seed as a Java long (what /seed prints). Call before anything else. */
void gen_init(int64_t seed);

/* The world type: 0 Minecraft's default, 1 superflat (FlatGeneratorInfo's default preset
 * "2;7,2x3,2;1": bedrock, two dirt, grass, all plains, nothing else). Kept across gen_init. */
void gen_set_flat(int flat);

/* Blocks of chunk (cx, cz) for y in [y0, y0 + h), after terrain, caves, ravines
 * and the decoration (population) that Minecraft would put in this chunk:
 * out[(y - y0) * 256 + z * 16 + x], x and z in 0..15, values from blocks.h (B_*).
 * Only y < 128 is ever written (y0 + h must be <= 128).
 * Cost: work around the chunk is cached, so neighbouring chunks asked one after the
 * other are much cheaper than scattered ones (Cortex-M7 at 216 MHz, h = 24, rows of
 * 3-4 neighbours: about 60-80 ms per call; the first call after gen_init or after a
 * jump to a new place about 190 ms). */
void gen_slab(int cx, int cz, int y0, int h, uint8_t *out);

/* Minecraft biome id at block (x, z) (0 ocean, 1 plains, ... 129+ mutated).
 * Cheap for the chunk of the last gen_slab call, a few ms elsewhere. */
int gen_biome(int x, int z);

/* y (0..127) of the highest non-air block of column (x, z) after generation
 * (including trees and other decoration, whatever the y range of the slab asked).
 * Cheap for the chunk of the last gen_slab call; elsewhere it costs a gen_slab. */
int gen_top(int x, int z);

/* Temperature noise at block (x, z): Minecraft's BiomeBase.ae.a(x / 8.0, z / 8.0), the raw
 * simplex value. BiomeBase.a(BlockPosition) is then, for y > 64:
 *   temperature - (gen_temp_noise(x, z) * 4 + y - 64) * 0.05 / 30
 * (and the biome temperature for y <= 64); below 0.15 it snows and water freezes.
 * Cheap (one simplex sample), usable any time after gen_init. */
float gen_temp_noise(int x, int z);

/* The world spawn point, as Minecraft chooses it (WorldChunkManager.findBiomePosition
 * then the random walk until the top block is grass); y is the first air block above it.
 * About 0.4 s on the calculator. */
void gen_spawn(int *x, int *y, int *z);

/* Statistics for tests: bytes of static RAM the generator uses (27108 on the M7). */
unsigned gen_ram_bytes(void);

#endif
