/* NumBlocks world generator: Minecraft 1.8.8's overworld generator in C.
 *
 * Ported from the decompiled 1.8.8 server (Spigot v1_8_R3 names in comments):
 * GenLayer* (biomes), ChunkProviderGenerate (terrain noise, surface),
 * WorldGenCaves / WorldGenCanyon (caves, ravines), BiomeDecorator and the
 * WorldGen* features (population), WorldServer's spawn search.
 *
 * ---------------------------------------------------------------------------
 * DEVIATIONS FROM MINECRAFT 1.8.8 (everything else is meant to be exact)
 * ---------------------------------------------------------------------------
 * Arithmetic
 *  - java.util.Random, GenLayer LCGs and all integer logic are bit exact, and so
 *    is MathHelper.sin/cos (its table value is rebuilt from a float kernel plus a
 *    correction table, checked for all 65536 indices). Draws are made in Java's
 *    left-to-right order (C leaves the order inside one expression open).
 *  - Perlin/simplex noise: the cell index and the fraction of every sample are
 *    computed exactly (fixed point setup per octave), but the gradient, fade and
 *    lerp math, the octave sums and the density formula run in float instead of
 *    double, so a block can flip only where the density is within ~1e-6 of 0
 *    (tests/gen/t_refcmp: 0 blocks in 500+ chunks against a double transcription).
 *    Java's quirk of reusing stale x-lerps while the y cell does not change is kept.
 *    Noise is only evaluated where it decides a block: the density is bounded
 *    without noise, then with the 6 lowest-frequency octaves (each octave is < 2 in
 *    magnitude), and only points of grid cells whose sign is not settled get the
 *    exact Java-order sum.
 *  - Terrain interpolation is done per column (bilinear then linear, float) rather
 *    than with Java's incremental double steps: same maths, different rounding.
 *  - Caves, ravines, ore veins, lakes, big oak trees, swamp/flower-forest noise use
 *    float where Java uses double (cave positions are kept in 32.32 fixed point so
 *    they do not drift; the few tests too close to call are redone in double).
 *  - Voronoi biome zoom uses exact integer distances (double only for ties).
 *  - Bedrock: nextInt(5) is drawn 256 times per column; the draws whose value is
 *    unused are skipped with an LCG jump (identical unless a draw would have hit
 *    Java's rejection loop, probability 1.4e-9 per draw).
 * Session-dependent behaviour of Minecraft, made deterministic
 *  - WorldGenBigTree keeps the height of the first big oak of a biome for the
 *    whole server session (and shortens it when blocked): here every big oak
 *    draws its own height, as the first one of a session would.
 *  - The decorator's huge mushroom keeps the first mushroom's colour for the
 *    session: here each one draws its colour.
 *  - Mutable biome top blocks (mega taiga podzol, extreme hills stone...) used
 *    when caves expose dirt: the biome's default top block is used.
 * Population (decoration)
 *  - Minecraft populates chunk P (writing into the 32x32 area P+8..P+24 that
 *    spans four chunks) once P and its +x/+z neighbours exist, reading the live
 *    world, so the result depends on chunk load order. Here gen_slab(C) replays
 *    the four populations that write into C ((cx-1,cz-1), (cx,cz-1), (cx-1,cz),
 *    (cx,cz)); each population P decides everything that affects its random
 *    stream from a "view" that is identical whatever chunk is being generated:
 *    the undecorated terrain of the 3x3 chunks around (a cached per-column
 *    summary: top block, water floor, filler depth; no caves) plus P's own
 *    earlier writes (height map, top block, plants). So objects crossing chunk
 *    borders are always complete and consistent, but a population does not see
 *    the other populations' trees, and does not see caves except where
 *    Minecraft's outcome needs them: dungeons and lakes query the real caves
 *    and ravines of their box (exact walk). Writes landing in C are re-checked
 *    against C's real blocks (caves included), e.g. leaves never replace logs.
 *  - Light is not computed: mushrooms need "shade" = an opaque block above in
 *    the view; snow and ice ignore block light.
 *  - Dungeon chests and spawners are placed; chest loot is not stored. The loot's
 *    item draws are replayed, but not the enchanted book's enchantment draws
 *    (TODO, see dungeon_book), so after a dungeon chest the rest of that
 *    population draws differently from Minecraft (still a valid decoration).
 *  - Structures are not generated (see TODO below), which matches Minecraft
 *    only where no structure exists.
 * Slabs (gen_slab's y range)
 *  - Everything is computed for the whole height except where only the slab's
 *    rows can be affected; reads of C outside its slab come from the summary. The
 *    blocks of a slab can therefore differ in rare cases with the y range asked
 *    (e.g. a plant at the slab's bottom row over ground that a lake removed in
 *    the row below; snowy grass under a snow layer above the slab's top row).
 *    c_top (gen_top) follows the writes of the populations including those above
 *    and below the slab, without their replace rules.
 * TODO: structures (villages, strongholds, mineshafts, temples, monuments);
 *       the enchanted book draws of dungeon chests.
 *
 * ---------------------------------------------------------------------------
 * HOW IT STAYS FAST AND SMALL (Cortex-M7, ~28 KB of static RAM, no malloc)
 * ---------------------------------------------------------------------------
 *  - Noise permutations are rebuilt per octave from saved random states (68 per
 *    chunk) instead of being kept (17 KB).
 *  - Column summaries (2 bytes per column) of up to 20 chunks are cached, so the
 *    chunks around the ones the game asks for are computed once per burst.
 *  - Biomes: an 18 x 18 window of 1:4 cells is kept (the layers cost about the
 *    same for one chunk as for 3 x 3); the 18 base layers run once per window.
 *  - Caves: one walk records, for every tunnel that can reach a 9 x 9 chunk area,
 *    its name and the box it may change; carving the chunk asked for and the
 *    populations' cave queries replay only the tunnels whose box meets theirs,
 *    each walked once for all its target chunks.
 *  - Populations remember (per chunk, 16 kept) which lakes and dungeons passed;
 *    a known failure only makes its random draws.
 *  - The file asks for -Os, IEEE float semantics (no fast-math, no FMA
 *    contraction, whatever the build flags) and -O2 for the hot loops (HOT).
 * ---------------------------------------------------------------------------
 */
#include "gen.h"

#include <math.h>
#include <string.h>

#include "blocks.h"

/* The float arithmetic here must be done as written (IEEE single precision, each operation
 * rounded): the game builds with -ffast-math, which would fuse multiply-adds, reassociate sums
 * and turn divisions into multiplications, and change the terrain. Size first by default; the
 * hot loops ask for O2 (HOT). Constants meant as doubles are written so that
 * -fsingle-precision-constant cannot change them (exact in float, or computed). */
#if defined(__clang__)
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
#elif defined(__GNUC__)
#pragma GCC optimize("no-fast-math", "fp-contract=off", "Os")
#endif
#if defined(__GNUC__) && !defined(__clang__)
#define HOT __attribute__((hot, optimize("O2", "no-fast-math", "fp-contract=off")))
#else
#define HOT
#endif

/* small helpers of the hot loops: inlined even when the file is compiled for size */
#if defined(__GNUC__)
#define INLINE static inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
#else
#define INLINE static inline
#define NOINLINE
#endif

typedef int64_t i64;
typedef uint64_t u64;
typedef int32_t i32;
typedef uint32_t u32;

#define SEA 63

/* left shifts of possibly negative values, without undefined behaviour */
#define SHL(v, k) ((i64)((u64)(i64)(v) << (k)))

/* ======================================================================== */
/* java.util.Random                                                          */
/* ======================================================================== */

#define JMUL 0x5DEECE66DULL
#define JADD 0xBULL
#define JMASK ((1ULL << 48) - 1)

typedef struct {
    u64 s;
} JRand;

static void jr_seed(JRand *r, i64 seed) { r->s = ((u64)seed ^ JMUL) & JMASK; }

/* next(bits); the _i helpers are the inlined twins used by the hot loops (the file is compiled
 * for size, and the compiler does not inline across optimisation levels) */
INLINE i32 jr_next_i(JRand *r, int bits) {
    r->s = (r->s * JMUL + JADD) & JMASK;
    return (i32)(u32)(r->s >> (48 - bits));
}
static i32 jr_next(JRand *r, int bits) { return jr_next_i(r, bits); }

/* float <-> int64 conversions done with integer operations: the C casts are exact and
 * cheap on a 64-bit host but go through soft double arithmetic on the Cortex-M7. */
typedef union {
    float f;
    u32 u;
} FBits;

/* (i64)(v * 2^k) for |v * 2^k| < 2^62: scaling by 2^k is exact, the cast truncates toward 0 */
INLINE i64 f2i64_scaled(float v, int k) {
    FBits c = {v};
    int e = (int)((c.u >> 23) & 255) - 150 + k; /* v * 2^k = m * 2^e */
    if (e <= -25) return 0;
    u64 m = (c.u & 0x7fffffu) | 0x800000u;
    u64 a = e >= 0 ? m << e : m >> -e;
    return (c.u >> 31) ? -(i64)a : (i64)a;
}

/* (float)v * 2^-k, rounded to nearest even like the cast */
static float i64_to_f_big(i64 v, int k);
INLINE float i64_to_f_scaled(i64 v, int k) {
    FBits s;
    if (v >= INT32_MIN && v <= INT32_MAX) {
        s.u = (u32)(127 - k) << 23;
        return (float)(i32)v * s.f;
    }
    return i64_to_f_big(v, k);
}

HOT static float i64_to_f_big(i64 v, int k) {
    FBits s;
    u64 a = v < 0 ? 0 - (u64)v : (u64)v;
    int sh = 40 - __builtin_clzll(a); /* bits below the 24 kept, >= 8 here */
    u32 m = (u32)(a >> sh);
    u64 rem = a & ((1ULL << sh) - 1), half = 1ULL << (sh - 1);
    if (rem > half || (rem == half && (m & 1))) m++;
    s.u = (u32)(127 + sh - k) << 23;
    float f = (float)m * s.f;
    return v < 0 ? -f : f;
}

/* nextInt(n), inlined in the hot loops (jr_int is the same, called) */
INLINE i32 jr_int_i(JRand *r, i32 n) {
    if ((n & -n) == n) return (i32)(((i64)n * (i64)jr_next_i(r, 31)) >> 31);
    i32 bits, val;
    do {
        bits = jr_next_i(r, 31);
        val = bits % n;
    } while ((i32)((u32)bits - (u32)val + (u32)(n - 1)) < 0);
    return val;
}

static i32 jr_int(JRand *r, i32 n) {
    if ((n & -n) == n) return (i32)(((i64)n * (i64)jr_next(r, 31)) >> 31);
    i32 bits, val;
    do {
        bits = jr_next(r, 31);
        val = bits % n;
    } while ((i32)((u32)bits - (u32)val + (u32)(n - 1)) < 0);
    return val;
}

static i64 jr_long(JRand *r) {
    i64 hi = jr_next(r, 32);
    i64 lo = jr_next(r, 32);
    return (i64)((u64)hi << 32) + lo;
}

static inline int jr_bool(JRand *r) { return jr_next(r, 1) != 0; }

INLINE float jr_float_i(JRand *r) { return (float)jr_next_i(r, 24) * (1.0f / 16777216.0f); }
static float jr_float(JRand *r) { return jr_float_i(r); }

/* nextDouble as the exact 53-bit integer numerator (value = n / 2^53) */
static i64 jr_double_bits(JRand *r) {
    i64 a = jr_next(r, 26);
    i64 b = jr_next(r, 27);
    return (a << 27) + b;
}


/* nextDouble rounded to float (one rounding from the exact value) */
static float jr_doublef(JRand *r) { return i64_to_f_scaled(jr_double_bits(r), 53); }
INLINE float jr_doublef_i(JRand *r) {
    i64 a = jr_next_i(r, 26), b = jr_next_i(r, 27);
    return i64_to_f_scaled((a << 27) + b, 53);
}

/* LCG jump: advance by n steps (n < 1024) */
static u64 jump_mul[10], jump_add[10];

static void jr_jump_init(void) {
    u64 m = JMUL, a = JADD;
    for (int i = 0; i < 10; i++) {
        jump_mul[i] = m;
        jump_add[i] = a;
        a = (a * m + a) & JMASK;
        m = (m * m) & JMASK;
    }
}

static void jr_skip(JRand *r, unsigned n) {
    for (int i = 0; n; i++, n >>= 1)
        if (n & 1) r->s = (r->s * jump_mul[i] + jump_add[i]) & JMASK;
}

/* ======================================================================== */
/* MathHelper                                                                */
/* ======================================================================== */

/* sin of table index i (0..65535): (float)Math.sin(i * 2pi / 65536), as MathHelper's table.
 * A float kernel (fdlibm's sinf/cosf kernels) on the first quadrant gets within an ulp; a
 * 2-bit-per-entry correction table (generated on a host from the exact values) makes it exact.
 * The kernels must not be contracted into FMAs, or the corrections would not apply. */
#if defined(__GNUC__) && !defined(__clang__)
#define NO_CONTRACT __attribute__((optimize("fp-contract=off")))
#else
#define NO_CONTRACT
#endif
/* MathHelper sin table corrections (generated by tests/gen/t_sintab2.c): 2 bits per
 * first-quadrant index, value = correction in ulps + 1, 3 = see SIN_EXC */
static const uint8_t SIN_CORR[4097] = {
85,20,84,69,80,84,21,84,21,21,68,84,1,20,96,21,85,68,21,84,84,84,84,21,
69,65,84,85,85,69,65,65,85,65,69,20,21,69,21,81,68,21,85,65,84,5,69,65,
85,81,81,20,5,85,85,21,84,5,22,68,21,84,1,20,1,21,65,21,81,5,84,16,
21,64,85,20,0,85,129,85,81,165,85,21,4,86,21,69,84,85,68,64,85,69,1,100,
69,85,65,69,21,5,85,69,4,69,85,5,16,85,21,5,85,21,5,16,85,21,4,101,
85,1,4,5,20,1,80,69,65,68,21,81,5,85,85,20,80,85,20,1,84,85,1,65,
85,0,17,84,85,5,80,85,85,1,21,81,65,80,21,21,0,85,5,150,85,85,69,17,
20,84,153,21,80,17,5,0,81,86,85,85,85,80,0,160,89,85,84,65,68,0,84,85,
21,85,20,64,16,164,86,21,85,20,5,0,101,85,21,69,21,68,0,84,85,85,68,0,
16,80,85,85,85,17,1,64,0,106,85,85,69,84,21,16,85,85,85,85,1,16,65,85,
85,64,81,0,1,148,85,85,17,1,16,1,64,85,21,81,21,17,64,84,85,69,21,21,
0,17,144,85,80,85,21,16,64,80,85,85,21,1,0,0,84,85,85,1,69,65,0,84,
85,80,20,20,1,0,84,85,85,4,1,1,0,85,21,85,68,5,1,0,85,17,21,81,
21,1,4,85,84,69,84,1,65,64,85,81,84,64,86,85,89,85,21,85,85,17,85,85,
68,80,84,84,89,85,85,85,85,21,81,85,81,20,0,4,0,170,85,85,85,85,85,65,
69,20,0,68,4,0,65,149,101,85,69,85,81,85,21,17,84,65,4,85,64,101,85,85,
85,85,69,1,84,20,84,16,65,21,80,101,85,85,85,69,21,4,85,1,80,4,85,0,
84,86,85,85,85,85,85,17,85,5,65,85,16,4,100,101,85,85,85,21,81,64,17,1,
16,16,64,16,105,86,85,85,85,85,81,65,65,68,85,64,0,64,149,85,85,85,17,85,
21,5,64,81,64,17,5,65,85,101,85,85,68,85,69,1,80,81,21,17,0,80,85,85,
85,85,85,21,81,21,20,0,1,85,16,164,85,85,85,85,85,81,85,20,0,81,0,17,
0,148,85,81,85,69,81,17,1,16,1,64,16,4,1,85,85,85,85,85,85,80,4,0,
65,21,16,64,0,85,85,85,85,85,85,84,20,64,4,64,4,0,80,89,85,85,85,21,
21,85,84,17,1,69,0,1,100,85,85,21,69,85,21,65,69,20,4,84,1,0,80,85,
85,85,21,80,20,20,1,16,5,1,85,64,84,85,21,85,81,1,68,0,17,0,65,5,
0,0,85,85,85,20,69,21,20,85,17,4,0,64,16,0,85,21,21,81,21,5,68,0,
21,64,4,16,68,64,85,85,85,85,21,21,21,20,1,68,64,0,64,80,85,85,20,80,
1,69,85,20,89,169,149,101,149,85,85,85,85,85,85,85,81,69,17,21,64,17,81,85,
16,81,0,5,64,1,88,102,149,85,85,101,85,85,85,85,84,85,85,17,69,21,84,20,
0,68,4,20,16,65,85,65,4,64,101,86,85,89,85,85,85,85,69,85,85,85,69,69,
85,81,85,0,20,69,85,16,81,65,1,80,64,96,86,90,85,85,85,85,85,69,81,85,
85,85,85,21,81,84,80,21,0,20,65,85,16,1,0,1,1,84,102,165,86,85,85,85,
21,85,85,21,64,21,5,80,68,1,20,17,0,4,4,0,1,1,64,64,0,101,101,85,
149,85,101,85,85,85,85,65,69,84,80,17,85,21,81,0,4,4,0,17,5,4,17,0,
148,101,101,85,85,85,85,85,85,85,85,81,1,85,21,64,81,84,81,20,64,17,5,20,
16,0,68,64,85,102,85,85,85,85,85,85,21,85,85,17,85,81,81,21,21,85,69,84,
80,68,0,4,4,64,64,0,89,89,85,89,85,85,85,21,85,65,85,5,80,85,64,17,
85,69,20,80,68,1,69,80,5,65,64,96,89,85,85,85,85,85,85,69,81,20,69,21,
85,69,84,21,81,0,17,5,69,4,64,4,21,0,80,84,101,85,85,85,85,85,85,85,
85,81,85,85,84,20,21,20,81,85,68,85,17,5,5,4,64,4,0,85,85,85,86,85,
85,85,84,21,85,85,85,5,20,65,69,69,21,4,85,16,84,65,80,65,4,0,144,85,
85,85,85,85,85,85,85,5,65,69,85,68,85,81,84,16,21,5,4,16,81,21,16,0,
1,0,85,153,85,85,85,85,21,69,81,84,85,20,69,81,64,17,0,69,65,65,1,68,
80,65,64,64,16,0,85,85,85,86,85,85,85,69,85,20,21,85,84,0,69,1,69,1,
68,84,16,1,4,20,4,20,5,68,85,85,85,85,85,85,85,85,85,21,80,84,84,84,
84,85,85,20,80,20,80,20,80,4,64,4,64,84,85,89,85,81,21,85,85,69,65,84,
84,81,69,21,20,65,64,80,65,80,65,0,16,1,4,80,16,85,86,85,85,65,85,85,
85,85,81,85,85,85,20,80,4,64,20,16,65,65,80,65,0,16,84,16,148,85,85,85,
85,85,85,21,80,84,85,81,81,81,17,85,69,1,17,21,4,5,17,64,16,16,1,0,
84,85,85,85,85,85,85,69,85,69,69,84,69,80,20,20,68,85,68,5,65,69,4,4,
84,16,20,64,85,85,85,85,85,85,85,1,85,85,68,21,69,69,84,65,1,5,85,4,
0,1,0,0,16,84,0,64,85,85,85,69,85,84,0,85,80,20,20,5,84,0,5,21,
81,5,5,68,0,4,21,16,16,1,0,84,85,85,85,85,85,85,69,81,80,5,80,1,
68,64,81,85,80,69,0,68,21,4,16,0,16,0,4,85,85,85,85,84,85,20,80,17,
84,64,81,21,1,20,85,68,38,18,17,22,34,81,17,17,18,18,17,17,81,17,17,17,
17,17,17,209,209,29,17,17,93,72,149,137,152,84,132,68,84,69,69,68,68,68,69,68,
68,68,4,68,68,68,68,68,68,7,67,48,87,101,21,37,101,85,22,18,17,81,17,81,
17,81,85,21,81,85,81,21,69,21,17,21,20,85,81,21,85,85,85,85,85,69,85,85,
68,84,68,69,68,85,20,85,84,68,68,69,84,68,68,65,21,5,64,84,97,85,18,97,
85,85,21,86,82,21,85,85,81,81,80,85,81,21,17,21,81,17,21,81,17,81,21,69,
84,68,69,84,88,84,69,85,73,68,85,85,68,85,69,68,69,85,85,84,65,68,68,68,
68,4,68,85,21,81,85,97,85,17,86,21,85,17,81,17,85,85,85,81,21,17,21,81,
16,84,21,85,65,1,81,85,85,68,85,73,85,85,84,68,84,69,85,84,85,85,85,85,
85,69,85,81,84,69,85,0,84,68,85,82,85,85,85,85,85,82,21,17,21,85,85,17,
81,85,17,21,16,85,81,21,85,81,21,21,17,80,69,69,84,68,85,85,85,68,84,85,
69,69,84,68,69,84,68,85,5,64,85,69,84,68,5,84,4,69,85,85,85,82,85,81,
85,81,85,85,85,85,85,17,81,85,21,21,81,17,85,21,16,69,21,81,1,89,69,85,
84,73,68,85,73,84,68,69,88,85,68,84,69,85,84,69,68,85,20,69,85,84,84,84,
68,85,85,21,85,21,81,85,17,21,81,17,85,21,81,17,21,81,85,21,21,81,69,1,
81,21,85,16,145,149,84,84,85,89,85,69,84,149,84,69,68,85,85,69,85,68,69,68,
64,69,5,68,68,69,68,5,37,85,85,37,21,86,85,21,38,85,85,85,85,21,21,81,
85,17,21,81,81,1,81,17,17,85,65,5,85,152,148,85,85,85,69,84,132,85,73,84,
68,85,85,84,85,0,84,68,68,85,84,69,68,64,68,80,85,81,37,21,85,21,85,21,
85,21,17,85,85,21,21,85,81,81,85,80,81,81,21,21,5,84,21,85,84,84,69,68,
85,84,85,84,84,85,85,85,85,84,145,69,65,69,69,69,80,21,4,69,69,84,68,102,
97,81,85,85,81,86,85,85,85,17,86,37,81,81,85,81,81,85,81,17,17,81,65,17,
85,81,85,85,148,84,69,84,85,85,69,84,84,68,85,73,84,85,85,85,85,85,85,81,
84,84,69,68,69,68,84,85,37,81,33,101,85,21,21,37,85,85,21,81,81,17,21,21,
17,81,17,21,5,65,21,85,1,21,85,69,85,84,84,133,73,85,68,69,88,69,85,85,
85,85,85,85,85,68,68,20,69,69,81,84,20,69,85,21,38,85,81,82,21,81,17,85,
81,81,21,21,20,85,84,5,65,85,85,1,16,81,21,17,0,69,84,69,133,149,68,69,
84,85,84,69,85,21,69,20,85,84,68,69,69,84,85,81,68,1,69,68,4,85,85,85,
18,85,21,97,21,85,85,81,21,81,21,17,17,85,81,81,85,69,85,85,17,17,1,16,
132,149,84,84,84,85,69,85,85,85,85,69,84,85,84,84,69,69,84,69,68,68,85,68,
4,20,5,68,20,38,85,85,85,85,37,85,85,85,21,85,81,81,21,81,85,17,85,17,
17,81,21,81,17,17,21,85,153,105,170,154,153,153,153,85,149,149,165,89,89,153,85,85,
89,149,89,149,85,85,149,165,148,85,85,85,86,102,169,101,86,102,86,102,102,85,89,86,
86,101,86,85,85,102,85,101,86,82,165,81,85,85,102,85,102,154,150,149,149,89,153,153,
154,85,85,105,88,153,85,149,149,89,149,149,149,85,85,85,149,88,84,101,85,89,86,86,
105,85,22,86,86,166,85,85,85,85,101,22,149,101,85,85,85,101,21,85,85,85,169,105,
150,105,89,89,153,153,149,85,101,85,149,89,149,152,153,153,85,149,85,85,90,85,85,85,
85,85,101,85,102,86,154,85,101,102,90,22,86,102,86,102,85,85,85,101,85,85,85,101,
105,85,85,22,85,85,102,154,150,89,101,85,149,89,149,89,105,89,106,137,85,153,85,85,
89,89,133,85,85,84,149,149,68,85,102,101,101,102,146,165,86,105,101,86,86,102,85,85,
86,101,85,85,85,85,101,21,85,85,97,101,101,101,101,149,149,153,166,85,153,85,153,85,
85,85,101,85,85,149,105,165,89,85,85,85,85,85,69,153,85,153,86,101,105,154,101,90,
101,101,102,102,102,101,101,85,102,85,102,85,25,86,102,86,86,85,85,97,86,165,150,153,
89,85,149,89,85,85,89,85,149,149,149,148,85,85,89,149,85,89,85,85,153,84,85,85,
104,86,85,101,86,85,85,101,101,102,101,85,85,85,102,85,85,166,85,102,85,85,102,81,
85,85,17,85,86,149,153,85,89,153,150,85,85,89,105,86,85,85,85,149,149,89,85,149,
89,85,149,85,149,85,84,84,90,102,102,105,85,86,102,89,85,101,85,102,85,102,85,102,
85,86,85,85,102,85,85,102,81,21,86,101,85,153,85,85,154,85,85,153,85,153,85,85,
85,85,154,85,154,85,153,85,153,85,152,85,152,84,152,100,169,101,165,102,102,102,102,102,
102,102,102,102,101,101,101,101,105,85,85,85,85,85,85,86,86,101,85,153,149,85,153,153,
85,85,89,153,85,100,85,85,85,85,72,89,89,89,85,85,85,85,85,85,73,85,85,85,
86,150,102,102,101,101,101,101,102,86,82,85,85,101,90,85,101,86,101,86,101,86,85,101,
18,85,89,165,85,153,86,153,85,105,149,85,85,89,153,153,153,149,89,89,85,85,149,101,
85,85,85,85,133,85,85,101,86,85,85,101,85,101,86,150,85,85,85,85,85,86,102,85,
85,102,85,85,85,85,85,86,86,150,150,89,85,165,150,149,149,85,85,85,153,85,153,85,
85,154,149,149,148,85,89,73,85,85,85,85,149,85,101,85,101,90,21,101,101,86,86,101,
85,89,102,85,85,102,85,85,85,85,101,85,21,21,21,86,85,85,153,85,89,153,153,149,
149,89,85,85,85,85,85,149,133,85,85,85,85,85,85,89,149,149,85,89,85,85,85,85,
101,86,85,85,85,169,101,101,85,85,102,85,85,86,85,86,86,21,85,21,85,85,81,85,
85,85,85,85,85,85,85,89,85,133,85,85,85,153,85,85,89,89,85,85,149,73,85,148,
149,85,69,85,101,101,85,86,85,102,101,89,101,150,149,21,101,86,86,102,85,85,86,37,
81,21,101,85,85,82,85,149,85,85,90,89,85,150,89,85,85,85,101,153,85,89,153,153,
85,84,133,85,85,69,84,149,85,89,149,85,85,85,85,85,85,85,101,85,86,21,85,85,
85,89,150,86,89,85,105,86,86,101,85,101,149,85,85,149,86,85,101,105,85,85,86,85,
85,85,149,21,85,85,85,149,149,149,89,101,89,85,101,85,85,86,101,85,86,101,85,89,
85,89,85,85,85,85,85,85,81,85,85,86,89,85,85,149,85,90,149,86,85,89,85,85,
85,85,85,149,86,85,85,85,89,85,85,85,101,85,101,89,89,86,85,85,106,89,85,85,
149,149,85,85,85,86,85,85,149,85,85,85,85,85,85,85,149,85,85,149,85,85,85,150,
149,101,85,85,85,85,149,101,85,85,85,85,85,149,85,85,85,81,81,85,85,85,85,85,
101,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,80,85,85,105,
86,85,85,89,85,149,85,89,165,85,105,85,106,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,149,165,86,85,85,85,101,85,149,85,85,85,85,85,85,101,85,89,
85,85,85,85,85,85,85,85,86,86,85,86,85,86,85,86,85,89,85,85,85,85,85,85,
85,85,85,85,85,85,85,81,85,85,149,89,85,85,85,101,85,85,85,85,86,85,85,85,
85,86,89,89,85,85,85,85,85,85,85,85,69,85,85,85,85,89,85,85,85,85,85,85,
85,85,86,85,86,89,85,85,85,85,85,85,85,85,85,85,85,85,85,101,86,85,85,85,
85,86,149,149,85,105,85,85,85,149,85,85,85,85,85,85,89,85,85,85,86,85,86,85,
85,101,85,150,86,85,89,85,85,85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,
85,149,85,85,85,101,85,85,153,85,101,149,85,85,85,85,85,85,85,85,85,85,85,85,
84,85,85,85,85,89,85,85,85,86,86,85,101,85,86,85,85,85,85,86,85,85,149,85,
85,85,85,85,85,85,85,85,149,85,85,85,86,85,86,101,105,85,85,85,85,85,85,101,
89,85,85,85,85,85,85,85,85,85,85,85,85,149,101,86,85,85,85,85,89,101,85,85,
85,85,85,86,149,85,89,85,85,85,85,85,85,85,85,85,85,85,86,85,89,85,85,85,
85,101,85,85,85,85,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,149,89,85,
85,85,85,85,86,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
150,85,85,85,85,85,85,85,85,85,89,85,85,89,85,85,85,85,69,85,85,85,85,85,
85,85,85,149,85,89,86,101,105,149,85,149,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,84,101,89,149,85,86,85,149,89,85,101,101,85,89,85,85,86,85,149,
85,85,85,85,85,85,85,85,85,85,85,85,85,149,149,85,85,85,149,85,85,149,85,85,
85,85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,101,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,149,85,101,85,85,85,85,85,85,85,85,85,85,85,85,101,85,
85,85,85,85,85,85,85,85,85,85,101,85,85,85,149,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,101,85,85,89,85,85,85,85,85,85,85,85,85,
85,101,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,101,85,
85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,81,85,85,85,85,101,85,85,85,85,85,85,85,85,85,85,85,
101,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,149,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,149,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,69,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,81,85,
85,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,101,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,1,};
/* entries whose correction is beyond one ulp: index, exact float bits */
static const u32 SIN_EXC[][2] = {{5295, 0x3ef8e592u},{5299, 0x3ef9117eu},{5301, 0x3ef92773u},{5313, 0x3ef9ab25u},{5412, 0x3efde662u},{5416, 0x3efe1207u},{5422, 0x3efe537au},{5424, 0x3efe694au},{65536, 0}};

HOT static float sin_quadrant(u32 r) { /* r in [0, 16384] */
    if (r <= 8192) {
        float x = (float)r * 9.58737992428526e-05f;
        float z = x * x, v = z * x;
        float rr = 8.3333337680e-03f + z * (-1.9841270114e-04f + z * 2.7183114939e-06f);
        return x + v * (-1.6666667163e-01f + z * rr);
    } else {
        float x = (float)(16384 - r) * 9.58737992428526e-05f;
        float z = x * x;
        float rr = z * (4.1666667908e-02f + z * (-1.3888889225e-03f + z * (2.4801587642e-05f + z * -2.7557314297e-07f)));
        float hz = 0.5f * z, w = 1.0f - hz;
        return w + (((1.0f - w) - hz) + z * rr);
    }
}

HOT static float sin_index(u32 i) {
    i &= 65535;
    if (i == 32768) return 1.2246467991473532e-16f;
    u32 q = i >> 14, r = i & 16383;
    if (q & 1) r = 16384 - r;
    float v = sin_quadrant(r);
    int d = (int)((SIN_CORR[r >> 2] >> ((r & 3) * 2)) & 3) - 1;
    if (d == 2) {
        for (int k = 0;; k++)
            if (SIN_EXC[k][0] == r || SIN_EXC[k][0] == 65536) {
                memcpy(&v, &SIN_EXC[k][1], 4);
                break;
            }
    } else if (d) {
        u32 b;
        memcpy(&b, &v, 4);
        b = (u32)((i32)b + d);
        memcpy(&v, &b, 4);
    }
    return (q & 2) ? -v : v;
}

INLINE float mh_sin_i(float f) { return sin_index((u32)(i32)(f * 10430.378f)); }
INLINE float mh_cos_i(float f) { return sin_index((u32)(i32)(f * 10430.378f + 16384.0f)); }
static float mh_sin(float f) { return mh_sin_i(f); }
static float mh_cos(float f) { return mh_cos_i(f); }

INLINE int floor_f_i(float f) {
    int i = (int)f;
    return f < (float)i ? i - 1 : i;
}
static int floor_f(float f) {
    int i = (int)f;
    return f < (float)i ? i - 1 : i;
}


/* ======================================================================== */
/* Biomes                                                                    */
/* ======================================================================== */

enum {
    BI_OCEAN = 0, BI_PLAINS = 1, BI_DESERT = 2, BI_EXTREME_HILLS = 3, BI_FOREST = 4, BI_TAIGA = 5, BI_SWAMP = 6,
    BI_RIVER = 7, BI_FROZEN_OCEAN = 10, BI_FROZEN_RIVER = 11, BI_ICE_PLAINS = 12, BI_ICE_MOUNTAINS = 13,
    BI_MUSHROOM = 14, BI_MUSHROOM_SHORE = 15, BI_BEACH = 16, BI_DESERT_HILLS = 17, BI_FOREST_HILLS = 18,
    BI_TAIGA_HILLS = 19, BI_EH_EDGE = 20, BI_JUNGLE = 21, BI_JUNGLE_HILLS = 22, BI_JUNGLE_EDGE = 23,
    BI_DEEP_OCEAN = 24, BI_STONE_BEACH = 25, BI_COLD_BEACH = 26, BI_BIRCH = 27, BI_BIRCH_HILLS = 28,
    BI_ROOFED = 29, BI_COLD_TAIGA = 30, BI_COLD_TAIGA_HILLS = 31, BI_MEGA_TAIGA = 32, BI_MEGA_TAIGA_HILLS = 33,
    BI_EH_PLUS = 34, BI_SAVANNA = 35, BI_SAVANNA_PLATEAU = 36, BI_MESA = 37, BI_MESA_PLATEAU_F = 38,
    BI_MESA_PLATEAU = 39
};

/* Biome class (getClass(), with BiomeBaseSub reporting its parent's class) */
enum {
    C_OCEAN, C_PLAINS, C_DESERT, C_HILLS, C_FOREST, C_TAIGA, C_SWAMP, C_RIVER, C_ICE, C_MUSHROOM, C_BEACH,
    C_JUNGLE, C_STONEBEACH, C_SAVANNA, C_MESA
};

/* Surface builders */
enum { S_DEFAULT, S_HILLS, S_TAIGA_MEGA, S_SWAMP, S_SAVANNA_M, S_MESA };

/* Decoration (biome .a(World, Random, BlockPosition)) kinds */
enum {
    D_PLAIN,       /* decorator only */
    D_PLAINS,      /* BiomePlains */
    D_DESERT,      /* BiomeDesert (well) */
    D_HILLS,       /* BiomeBigHills (emerald, silverfish) */
    D_FOREST,      /* BiomeForest */
    D_TAIGA,       /* BiomeTaiga */
    D_ICE,         /* BiomeIcePlains */
    D_JUNGLE,      /* BiomeJungle (melon, vine draws) */
    D_SAVANNA,     /* BiomeSavanna (double grass) */
    D_ROOFED_SUB   /* Roofed Forest M: parent's BiomeForest.a with the parent biome */
};

/* Tree selector kinds (biome.a(Random)) */
enum {
    T_DEFAULT, T_HILLS, T_FOREST0, T_FOREST1, T_BIRCH, T_ROOFED, T_BIRCH_SUB, T_TAIGA0, T_TAIGA1, T_TAIGA2,
    T_SWAMP, T_ICE, T_JUNGLE, T_JUNGLE_EDGE, T_SAVANNA, T_MESA
};

/* Flower selector kinds; grass selector kinds */
enum { F_DEFAULT, F_PLAINS, F_FLOWER_FOREST, F_SWAMP };
enum { G_DEFAULT, G_TAIGA, G_JUNGLE };

typedef struct {
    uint8_t id;
    uint8_t cls, surf, deco, tree, flower, grass;
    uint8_t snowy;   /* ax: BiomeBase.c() */
    uint8_t temp_cat; /* 0 ocean 1 cold 2 medium 3 warm */
    uint8_t top, filler;
    uint8_t mode;    /* hills: aI; taiga: aI; forest: aG; mesa: bit0 bryce, bit1 plateau F; plains: sunflower; ice: spikes; jungle: edge */
    float depth, scale, temp;
    /* decorator: A trees, B flowers, C grass, D deadbush, E mushrooms, F reeds, G cactus, H gravel,
     * I sand, J clay, K big mushrooms, Z lily pads */
    int16_t dA;
    int8_t dB, dC, dD, dE, dF, dG, dH, dI, dJ, dK, dZ;
    u32 color;
} Biome;

#define DEC(A, B, C, D, E, F, G, H, I, J, K, Z) A, B, C, D, E, F, G, H, I, J, K, Z
#define DDEF DEC(0, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0)

static const Biome BIOMES[] = {
    {0, C_OCEAN, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 0, B_GRASS, B_DIRT, 0, -1.0f, 0.1f, 0.5f, DDEF, 0x000070},
    {1, C_PLAINS, S_DEFAULT, D_PLAINS, T_DEFAULT, F_PLAINS, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.125f, 0.05f, 0.8f, DEC(-999, 4, 10, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x8DB360},
    {2, C_DESERT, S_DEFAULT, D_DESERT, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 3, B_SAND, B_SAND, 0, 0.125f, 0.05f, 2.0f, DEC(-999, 2, 1, 2, 0, 50, 10, 1, 3, 1, 0, 0), 0xFA9418},
    {3, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 1.0f, 0.5f, 0.2f, DDEF, 0x606060},
    {4, C_FOREST, S_DEFAULT, D_FOREST, T_FOREST0, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.1f, 0.2f, 0.7f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x056621},
    {5, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 0, 0.2f, 0.2f, 0.25f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x0B6659},
    {6, C_SWAMP, S_SWAMP, D_PLAIN, T_SWAMP, F_SWAMP, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, -0.2f, 0.1f, 0.8f, DEC(2, 1, 5, 1, 8, 10, 0, 0, 0, 1, 0, 4), 0x07F9B2},
    {7, C_RIVER, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, -0.5f, 0.0f, 0.5f, DDEF, 0x0000FF},
    {10, C_OCEAN, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 1, 0, B_GRASS, B_DIRT, 0, -1.0f, 0.1f, 0.0f, DDEF, 0x9090A0},
    {11, C_RIVER, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, -0.5f, 0.0f, 0.0f, DDEF, 0xA0A0FF},
    {12, C_ICE, S_DEFAULT, D_ICE, T_ICE, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, 0.125f, 0.05f, 0.0f, DDEF, 0xFFFFFF},
    {13, C_ICE, S_DEFAULT, D_ICE, T_ICE, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.0f, DDEF, 0xA0A0A0},
    {14, C_MUSHROOM, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_MYCELIUM, B_DIRT, 0, 0.2f, 0.3f, 0.9f, DEC(-100, -100, -100, 0, 1, 0, 0, 1, 3, 1, 1, 0), 0xFF00FF},
    {15, C_MUSHROOM, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_MYCELIUM, B_DIRT, 0, 0.0f, 0.025f, 0.9f, DEC(-100, -100, -100, 0, 1, 0, 0, 1, 3, 1, 1, 0), 0xA000FF},
    {16, C_BEACH, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_SAND, B_SAND, 0, 0.0f, 0.025f, 0.8f, DEC(-999, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xFADE55},
    {17, C_DESERT, S_DEFAULT, D_DESERT, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 3, B_SAND, B_SAND, 0, 0.45f, 0.3f, 2.0f, DEC(-999, 2, 1, 2, 0, 50, 10, 1, 3, 1, 0, 0), 0xD25F12},
    {18, C_FOREST, S_DEFAULT, D_FOREST, T_FOREST0, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.7f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x22551C},
    {19, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.25f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x163933},
    {20, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.8f, 0.3f, 0.2f, DEC(3, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x72789A},
    {21, C_JUNGLE, S_DEFAULT, D_JUNGLE, T_JUNGLE, F_DEFAULT, G_JUNGLE, 0, 2, B_GRASS, B_DIRT, 0, 0.1f, 0.2f, 0.95f, DEC(50, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x537B09},
    {22, C_JUNGLE, S_DEFAULT, D_JUNGLE, T_JUNGLE, F_DEFAULT, G_JUNGLE, 0, 2, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.95f, DEC(50, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x2C4205},
    {23, C_JUNGLE, S_DEFAULT, D_JUNGLE, T_JUNGLE_EDGE, F_DEFAULT, G_JUNGLE, 0, 2, B_GRASS, B_DIRT, 1, 0.1f, 0.2f, 0.95f, DEC(2, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x628B17},
    {24, C_OCEAN, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 0, B_GRASS, B_DIRT, 0, -1.8f, 0.1f, 0.5f, DDEF, 0x000030},
    {25, C_STONEBEACH, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_STONE, B_STONE, 0, 0.1f, 0.8f, 0.2f, DEC(-999, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xA2A284},
    {26, C_BEACH, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 1, 1, B_SAND, B_SAND, 0, 0.0f, 0.025f, 0.05f, DEC(-999, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xFAF0C0},
    {27, C_FOREST, S_DEFAULT, D_FOREST, T_BIRCH, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.1f, 0.2f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x307444},
    {28, C_FOREST, S_DEFAULT, D_FOREST, T_BIRCH, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.45f, 0.3f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x1F5F32},
    {29, C_FOREST, S_DEFAULT, D_FOREST, T_ROOFED, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 3, 0.1f, 0.2f, 0.7f, DEC(-999, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x40511A},
    {30, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 1, 1, B_GRASS, B_DIRT, 0, 0.2f, 0.2f, -0.5f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x31554A},
    {31, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 1, 1, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, -0.5f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x243F36},
    {32, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA1, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 1, 0.2f, 0.2f, 0.3f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x596651},
    {33, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA1, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 1, 0.45f, 0.3f, 0.3f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x454F3E},
    {34, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 1.0f, 0.5f, 0.2f, DEC(3, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x507050},
    {35, C_SAVANNA, S_DEFAULT, D_SAVANNA, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 3, B_GRASS, B_DIRT, 0, 0.125f, 0.05f, 1.2f, DEC(1, 4, 20, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xBDB25F},
    {36, C_SAVANNA, S_DEFAULT, D_SAVANNA, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 3, B_GRASS, B_DIRT, 0, 1.5f, 0.025f, 1.0f, DEC(1, 4, 20, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xA79D64},
    {37, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 0, 0.1f, 0.2f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xD94515},
    {38, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 2, 1.5f, 0.025f, 2.0f, DEC(5, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xB09765},
    {39, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 0, 1.5f, 0.025f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xCA8C65},
    /* mutated variants */
    {129, C_PLAINS, S_DEFAULT, D_PLAINS, T_DEFAULT, F_PLAINS, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.125f, 0.05f, 0.8f, DEC(-999, 4, 10, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xB5DB88},
    {130, C_DESERT, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 3, B_SAND, B_SAND, 0, 0.225f, 0.25f, 2.0f, DEC(-999, 2, 1, 2, 0, 50, 10, 1, 3, 1, 0, 0), 0xFFBC40},
    {131, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 1.0f, 0.5f, 0.2f, DDEF, 0x888888},
    {132, C_FOREST, S_DEFAULT, D_FOREST, T_FOREST1, F_FLOWER_FOREST, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.1f, 0.4f, 0.7f, DEC(6, 100, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x6A7425},
    {133, C_TAIGA, S_DEFAULT, D_PLAIN, T_TAIGA0, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.3f, 0.4f, 0.25f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x338E81},
    {134, C_SWAMP, S_SWAMP, D_PLAIN, T_SWAMP, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, -0.1f, 0.3f, 0.8f, DEC(2, 1, 5, 1, 8, 10, 0, 0, 0, 1, 0, 4), 0x2FFFDA},
    {140, C_ICE, S_DEFAULT, D_ICE, T_ICE, F_DEFAULT, G_DEFAULT, 1, 1, B_SNOW, B_DIRT, 1, 0.425f, 0.45f, 0.0f, DDEF, 0xB4DCDC},
    {149, C_JUNGLE, S_DEFAULT, D_PLAIN, T_JUNGLE, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.2f, 0.4f, 0.95f, DEC(50, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x7BA331},
    {151, C_JUNGLE, S_DEFAULT, D_PLAIN, T_JUNGLE_EDGE, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.2f, 0.4f, 0.95f, DEC(2, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x8AB33F},
    {155, C_FOREST, S_DEFAULT, D_PLAIN, T_BIRCH_SUB, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.2f, 0.4f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x589C6C},
    {156, C_FOREST, S_DEFAULT, D_PLAIN, T_BIRCH_SUB, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.55f, 0.5f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x47875A},
    {157, C_FOREST, S_DEFAULT, D_ROOFED_SUB, T_ROOFED, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 3, 0.2f, 0.4f, 0.7f, DEC(-999, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x687942},
    {158, C_TAIGA, S_DEFAULT, D_PLAIN, T_TAIGA0, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, 0.3f, 0.4f, -0.5f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x597D72},
    {160, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA2, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 2, 0.2f, 0.2f, 0.25f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x818E79},
    {161, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA2, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 2, 0.2f, 0.2f, 0.25f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x6D7766},
    {162, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 1.0f, 0.5f, 0.2f, DDEF, 0x789878},
    {163, C_SAVANNA, S_SAVANNA_M, D_PLAIN, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 3, B_GRASS, B_DIRT, 0, 0.3625f, 1.225f, 1.1f, DEC(2, 2, 5, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xE5DA87},
    {164, C_SAVANNA, S_SAVANNA_M, D_PLAIN, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 1.05f, 1.2125f, 1.0f, DEC(2, 2, 5, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xCFC58C},
    {165, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 1, 0.1f, 0.2f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xFF6D3D},
    {166, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 2, 0.45f, 0.3f, 2.0f, DEC(5, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xD8BF8D},
    {167, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 0, 0.45f, 0.3f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xF2B48D},
};
#define NBIOMES ((int)(sizeof(BIOMES) / sizeof(BIOMES[0])))

static uint8_t biome_index[256]; /* id -> index in BIOMES, 255 = none */

static const Biome *bio(int id) {
    if (id < 0 || id > 255 || biome_index[id] == 255) return &BIOMES[0]; /* BiomeBase.getBiome(id, OCEAN) */
    return &BIOMES[biome_index[id]];
}

static int bio_exists(int id) { return id >= 0 && id < 256 && biome_index[id] != 255; }

/* GenLayer.a(int, int): same biome "kind" */
static int bio_same(int i, int j) {
    if (i == j) return 1;
    if (i == BI_MESA_PLATEAU_F || i == BI_MESA_PLATEAU) return j == BI_MESA_PLATEAU_F || j == BI_MESA_PLATEAU;
    if (!bio_exists(i) || !bio_exists(j)) return 0;
    return bio(i)->cls == bio(j)->cls;
}

static int is_ocean_id(int i) { return i == BI_OCEAN || i == BI_DEEP_OCEAN || i == BI_FROZEN_OCEAN; }

/* ======================================================================== */
/* GenLayer stack                                                            */
/* ======================================================================== */

#define LMUL 6364136223846793005ULL
#define LADD 1442695040888963407ULL

enum {
    L_ISLAND0, L_ZOOM, L_FUZZY, L_ISLAND, L_ICEPLAINS, L_TOPSOIL, L_SPEC1, L_SPEC2, L_SPEC3, L_MUSHISLAND,
    L_DEEPOCEAN, L_CLEANER, L_BIOME, L_DESERT, L_HILLS, L_PLAINS, L_MUSHSHORE, L_RIVER, L_SMOOTH
};

typedef struct {
    uint8_t type;
    int16_t seed;
    uint8_t noworld; /* never initialised with the world seed (c stays 0) */
} LayerDef;

static const LayerDef LBASE[] = {
    {L_ISLAND0, 1, 0},    {L_FUZZY, 2000, 0},  {L_ISLAND, 1, 0},     {L_ZOOM, 2001, 0},  {L_ISLAND, 2, 0},
    {L_ISLAND, 50, 0},    {L_ISLAND, 70, 0},   {L_ICEPLAINS, 2, 0},  {L_TOPSOIL, 2, 0},  {L_ISLAND, 3, 0},
    {L_SPEC1, 2, 0},      {L_SPEC2, 2, 0},     {L_SPEC3, 3, 0},      {L_ZOOM, 2002, 0},  {L_ZOOM, 2003, 0},
    {L_ISLAND, 4, 0},     {L_MUSHISLAND, 5, 0}, {L_DEEPOCEAN, 4, 0},
};
#define NBASE 18
/* the river noise used by GenLayerRegionHills: its two zoom layers are never seeded with the world seed */
static const LayerDef LHILLSRIVER[] = {{L_CLEANER, 100, 0}, {L_ZOOM, 1000, 1}, {L_ZOOM, 1001, 1}};
static const LayerDef LRIVER[] = {{L_CLEANER, 100, 0}, {L_ZOOM, 1000, 0}, {L_ZOOM, 1001, 0}, {L_ZOOM, 1000, 0},
                                  {L_ZOOM, 1001, 0},   {L_ZOOM, 1002, 0}, {L_ZOOM, 1003, 0}, {L_RIVER, 1, 0},
                                  {L_SMOOTH, 1000, 0}};
static const LayerDef LBIOME1[] = {{L_BIOME, 200, 0}, {L_ZOOM, 1000, 0}, {L_ZOOM, 1001, 0}, {L_DESERT, 1000, 0}};
static const LayerDef LBIOME2[] = {{L_HILLS, 1000, 0}, {L_PLAINS, 1001, 0}, {L_ZOOM, 1000, 0},
                                   {L_ISLAND, 3, 0},   {L_ZOOM, 1001, 0},   {L_MUSHSHORE, 1000, 0},
                                   {L_ZOOM, 1002, 0},  {L_ZOOM, 1003, 0},   {L_SMOOTH, 1000, 0}};

static i64 g_seed;
static i64 lay_wseed[19 + 32]; /* world-seeded "c" per distinct layer seed, see layer_c() */

static i64 layer_base(i64 s) {
    u64 b = (u64)s;
    for (int i = 0; i < 3; i++) {
        b *= b * LMUL + LADD;
        b += (u64)s;
    }
    return (i64)b;
}

static i64 layer_world(i64 s) {
    u64 b = (u64)layer_base(s);
    u64 c = (u64)g_seed;
    for (int i = 0; i < 3; i++) {
        c *= c * LMUL + LADD;
        c += b;
    }
    return (i64)c;
}

/* cache of world-seeded c by layer seed (the seeds used are few) */
static const int16_t LSEEDS[] = {1, 2, 3, 4, 5, 10, 50, 70, 100, 200, 1000, 1001, 1002, 1003, 2000, 2001, 2002, 2003};
#define NLSEEDS 18

static i64 layer_c(int seed, int noworld) {
    if (noworld) return 0;
    for (int i = 0; i < NLSEEDS; i++)
        if (LSEEDS[i] == seed) return lay_wseed[i];
    return layer_world(seed);
}

/* layer RNG state */
typedef struct {
    i64 c, d;
} LRand;

INLINE void lr_chunk(LRand *r, i64 x, i64 z) {
    u64 d = (u64)r->c;
    d *= d * LMUL + LADD;
    d += (u64)x;
    d *= d * LMUL + LADD;
    d += (u64)z;
    d *= d * LMUL + LADD;
    d += (u64)x;
    d *= d * LMUL + LADD;
    d += (u64)z;
    r->d = (i64)d;
}

/* (d >> 24) mod n in [0, n) (Java's % then + n). The 40-bit dividend is split so that
 * only 32-bit divisions are needed (a 64-bit one is a slow library call on the M7). */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
HOT static int lr_int(LRand *r, int n) {
    i64 x = r->d >> 24;
    int j;
    if ((n & (n - 1)) == 0) {
        j = (int)((u64)x & (u64)(n - 1));
    } else if (n <= 65536) {
        u32 un = (u32)n, lo = (u32)x;
        i32 h = (i32)(x >> 32) % n;
        if (h < 0) h += n;
        u32 t = (((u32)h << 16) | (lo >> 16)) % un;
        j = (int)(((t << 16) | (lo & 0xffffu)) % un);
    } else {
        j = (int)(x % (i64)n);
        if (j < 0) j += n;
    }
    u64 d = (u64)r->d;
    d *= d * LMUL + LADD;
    d += (u64)r->c;
    r->d = (i64)d;
    return j;
}

static inline int sel_mode(LRand *r, int i, int j, int k, int l) {
    if (j == k && k == l) return j;
    if (i == j && i == k) return i;
    if (i == j && i == l) return i;
    if (i == k && i == l) return i;
    if (i == j && k != l) return i;
    if (i == k && j != l) return i;
    if (i == l && j != k) return i;
    if (j == k && i != l) return j;
    if (j == l && i != k) return j;
    if (k == l && i != j) return k;
    int a[4] = {i, j, k, l};
    return a[lr_int(r, 4)];
}

/* Layer scratch: one bump-allocated int32 area */
static i32 *lay_mem; /* points into the shared scratch union */
static int lay_top, lay_peak, lay_cap;

static i32 *lay_alloc(int n) {
    i32 *p = lay_mem + lay_top;
    lay_top += n;
    if (lay_top > lay_peak) lay_peak = lay_top;
    if (lay_top > lay_cap) {
        /* should not happen with the request sizes used; clamp to avoid corruption */
        lay_top = lay_cap;
        return lay_mem + lay_cap - n;
    }
    return p;
}

/* parent window of a layer for an output window */
static void layer_parent_win(int type, int x, int z, int w, int h, int *px, int *pz, int *pw, int *ph) {
    switch (type) {
    case L_ZOOM:
    case L_FUZZY:
        *px = x >> 1;
        *pz = z >> 1;
        *pw = (w >> 1) + 2;
        *ph = (h >> 1) + 2;
        break;
    case L_SPEC3:
    case L_CLEANER:
    case L_BIOME:
        *px = x;
        *pz = z;
        *pw = w;
        *ph = h;
        break;
    default:
        *px = x - 1;
        *pz = z - 1;
        *pw = w + 2;
        *ph = h + 2;
        break;
    }
}

/* Applies one layer: in = parent window (px,pz,pw,ph), out = (x,z,w,h). in2 is the second
 * input (hills river noise, same window as in). */
static void layer_apply(const LayerDef *L, const i32 *in, const i32 *in2, i32 *out, int x, int z, int w, int h) {
    LRand r;
    r.c = layer_c(L->seed, L->noworld);
    int pw = w + 2;
    switch (L->type) {
    case L_ISLAND0:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                out[i + j * w] = lr_int(&r, 10) == 0 ? 1 : 0;
            }
        if (x > -w && x <= 0 && z > -h && z <= 0) out[-x + -z * w] = 1;
        break;
    case L_ZOOM:
    case L_FUZZY: {
        int px = x >> 1, pz = z >> 1, ppw = (w >> 1) + 2;
        int cx0 = x >> 1, cx1 = (x + w - 1) >> 1, cz0 = z >> 1, cz1 = (z + h - 1) >> 1;
        for (int cz = cz0; cz <= cz1; cz++)
            for (int cx = cx0; cx <= cx1; cx++) {
                int ix = cx - px, iz = cz - pz;
                int a = in[ix + iz * ppw], b = in[ix + (iz + 1) * ppw];
                int c = in[ix + 1 + iz * ppw], d = in[ix + 1 + (iz + 1) * ppw];
                lr_chunk(&r, SHL(cx, 1), SHL(cz, 1));
                int v00 = a;
                int v01 = lr_int(&r, 2) ? b : a;
                int v10 = lr_int(&r, 2) ? c : a;
                int v11;
                if (L->type == L_FUZZY) {
                    int arr[4] = {a, c, b, d};
                    v11 = arr[lr_int(&r, 4)];
                } else {
                    v11 = sel_mode(&r, a, c, b, d);
                }
                int bx = cx * 2 - x, bz = cz * 2 - z;
                if (bx >= 0 && bz >= 0) out[bx + bz * w] = v00;
                if (bx >= 0 && bz + 1 < h) out[bx + (bz + 1) * w] = v01;
                if (bx + 1 < w && bz >= 0) out[bx + 1 + bz * w] = v10;
                if (bx + 1 < w && bz + 1 < h) out[bx + 1 + (bz + 1) * w] = v11;
            }
        break;
    }
    case L_ISLAND:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int a = in[i + j * pw], b = in[i + 2 + j * pw], c = in[i + (j + 2) * pw], d = in[i + 2 + (j + 2) * pw];
                int k = in[i + 1 + (j + 1) * pw];
                lr_chunk(&r, x + i, z + j);
                int v;
                if (k == 0 && (a != 0 || b != 0 || c != 0 || d != 0)) {
                    int l3 = 1, i4 = 1;
                    if (a != 0 && lr_int(&r, l3++) == 0) i4 = a;
                    if (b != 0 && lr_int(&r, l3++) == 0) i4 = b;
                    if (c != 0 && lr_int(&r, l3++) == 0) i4 = c;
                    if (d != 0 && lr_int(&r, l3++) == 0) i4 = d;
                    if (lr_int(&r, 3) == 0) v = i4;
                    else if (i4 == 4) v = 4;
                    else v = 0;
                } else if (k > 0 && (a == 0 || b == 0 || c == 0 || d == 0)) {
                    if (lr_int(&r, 5) == 0) v = k == 4 ? 4 : 0;
                    else v = k;
                } else {
                    v = k;
                }
                out[i + j * w] = v;
            }
        break;
    case L_ICEPLAINS:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw];
                int s = in[i + 1 + (j + 2) * pw], k = in[i + 1 + (j + 1) * pw];
                out[i + j * w] = k;
                lr_chunk(&r, x + i, z + j);
                if (k == 0 && n == 0 && e == 0 && ww == 0 && s == 0 && lr_int(&r, 2) == 0) out[i + j * w] = 1;
            }
        break;
    case L_TOPSOIL:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int k = in[i + 1 + (j + 1) * pw];
                lr_chunk(&r, x + i, z + j);
                if (k == 0) {
                    out[i + j * w] = 0;
                } else {
                    int l = lr_int(&r, 6);
                    out[i + j * w] = l == 0 ? 4 : (l <= 1 ? 3 : 1);
                }
            }
        break;
    case L_SPEC1:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                if (k == 1) {
                    int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                        s = in[i + 1 + (j + 2) * pw];
                    if (n == 3 || e == 3 || ww == 3 || s == 3 || n == 4 || e == 4 || ww == 4 || s == 4) k = 2;
                }
                out[i + j * w] = k;
            }
        break;
    case L_SPEC2:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int k = in[i + 1 + (j + 1) * pw];
                if (k == 4) {
                    int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                        s = in[i + 1 + (j + 2) * pw];
                    if (n == 2 || e == 2 || ww == 2 || s == 2 || n == 1 || e == 1 || ww == 1 || s == 1) k = 3;
                }
                out[i + j * w] = k;
            }
        break;
    case L_SPEC3:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + j * w];
                if (k != 0 && lr_int(&r, 13) == 0) k |= (1 + lr_int(&r, 15)) << 8 & 3840;
                out[i + j * w] = k;
            }
        break;
    case L_MUSHISLAND:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int a = in[i + j * pw], b = in[i + 2 + j * pw], c = in[i + (j + 2) * pw], d = in[i + 2 + (j + 2) * pw];
                int k = in[i + 1 + (j + 1) * pw];
                lr_chunk(&r, x + i, z + j);
                if (k == 0 && a == 0 && b == 0 && c == 0 && d == 0 && lr_int(&r, 100) == 0) out[i + j * w] = BI_MUSHROOM;
                else out[i + j * w] = k;
            }
        break;
    case L_DEEPOCEAN:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                    s = in[i + 1 + (j + 2) * pw], k = in[i + 1 + (j + 1) * pw];
                int c = (n == 0) + (e == 0) + (ww == 0) + (s == 0);
                out[i + j * w] = (k == 0 && c > 3) ? BI_DEEP_OCEAN : k;
            }
        break;
    case L_CLEANER:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                out[i + j * w] = in[i + j * w] > 0 ? lr_int(&r, 299999) + 2 : 0;
            }
        break;
    case L_BIOME: {
        static const uint8_t warm[6] = {BI_DESERT, BI_DESERT, BI_DESERT, BI_SAVANNA, BI_SAVANNA, BI_PLAINS};
        static const uint8_t medium[6] = {BI_FOREST, BI_ROOFED, BI_EXTREME_HILLS, BI_PLAINS, BI_BIRCH, BI_SWAMP};
        static const uint8_t cold[4] = {BI_FOREST, BI_EXTREME_HILLS, BI_TAIGA, BI_PLAINS};
        static const uint8_t ice[4] = {BI_ICE_PLAINS, BI_ICE_PLAINS, BI_ICE_PLAINS, BI_COLD_TAIGA};
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + j * w];
                int sp = (k & 3840) >> 8;
                k &= ~3840;
                int v;
                if (is_ocean_id(k) || k == BI_MUSHROOM) v = k;
                else if (k == 1) v = sp > 0 ? (lr_int(&r, 3) == 0 ? BI_MESA_PLATEAU : BI_MESA_PLATEAU_F) : warm[lr_int(&r, 6)];
                else if (k == 2) v = sp > 0 ? BI_JUNGLE : medium[lr_int(&r, 6)];
                else if (k == 3) v = sp > 0 ? BI_MEGA_TAIGA : cold[lr_int(&r, 4)];
                else if (k == 4) v = ice[lr_int(&r, 4)];
                else v = BI_MUSHROOM;
                out[i + j * w] = v;
            }
        break;
    }
    case L_DESERT:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                    s = in[i + 1 + (j + 2) * pw];
                int v;
                /* a(...): extreme hills edge */
                if (bio_same(k, BI_EXTREME_HILLS)) {
                    /* b(int,int): same kind, or compatible temperatures */
#define TEMPOK(q, t) (bio_same(q, t) || (bio_exists(q) && bio_exists(t) && (bio(q)->temp_cat == bio(t)->temp_cat || bio(q)->temp_cat == 2 || bio(t)->temp_cat == 2)))
                    v = (TEMPOK(n, BI_EXTREME_HILLS) && TEMPOK(e, BI_EXTREME_HILLS) && TEMPOK(ww, BI_EXTREME_HILLS) &&
                         TEMPOK(s, BI_EXTREME_HILLS))
                            ? k
                            : BI_EH_EDGE;
                } else if (k == BI_MESA_PLATEAU_F) {
                    v = (bio_same(n, k) && bio_same(e, k) && bio_same(ww, k) && bio_same(s, k)) ? k : BI_MESA;
                } else if (k == BI_MESA_PLATEAU) {
                    v = (bio_same(n, k) && bio_same(e, k) && bio_same(ww, k) && bio_same(s, k)) ? k : BI_MESA;
                } else if (k == BI_MEGA_TAIGA) {
                    v = (bio_same(n, k) && bio_same(e, k) && bio_same(ww, k) && bio_same(s, k)) ? k : BI_TAIGA;
                } else if (k == BI_DESERT) {
                    v = (n != BI_ICE_PLAINS && e != BI_ICE_PLAINS && ww != BI_ICE_PLAINS && s != BI_ICE_PLAINS) ? k : BI_EH_PLUS;
                } else if (k == BI_SWAMP) {
                    if (n != BI_DESERT && e != BI_DESERT && ww != BI_DESERT && s != BI_DESERT && n != BI_COLD_TAIGA &&
                        e != BI_COLD_TAIGA && ww != BI_COLD_TAIGA && s != BI_COLD_TAIGA && n != BI_ICE_PLAINS &&
                        e != BI_ICE_PLAINS && ww != BI_ICE_PLAINS && s != BI_ICE_PLAINS)
                        v = (n != BI_JUNGLE && s != BI_JUNGLE && e != BI_JUNGLE && ww != BI_JUNGLE) ? k : BI_JUNGLE_EDGE;
                    else
                        v = BI_PLAINS;
                } else {
                    v = k;
                }
                out[i + j * w] = v;
            }
        break;
    case L_HILLS:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                int l = in2[i + 1 + (j + 1) * pw];
                int flag = (l - 2) % 29 == 0;
                int v;
                if (k != 0 && l >= 2 && (l - 2) % 29 == 1 && k < 128) {
                    v = bio_exists(k + 128) ? k + 128 : k;
                } else if (lr_int(&r, 3) != 0 && !flag) {
                    v = k;
                } else {
                    int i2 = k;
                    if (k == BI_DESERT) i2 = BI_DESERT_HILLS;
                    else if (k == BI_FOREST) i2 = BI_FOREST_HILLS;
                    else if (k == BI_BIRCH) i2 = BI_BIRCH_HILLS;
                    else if (k == BI_ROOFED) i2 = BI_PLAINS;
                    else if (k == BI_TAIGA) i2 = BI_TAIGA_HILLS;
                    else if (k == BI_MEGA_TAIGA) i2 = BI_MEGA_TAIGA_HILLS;
                    else if (k == BI_COLD_TAIGA) i2 = BI_COLD_TAIGA_HILLS;
                    else if (k == BI_PLAINS) i2 = lr_int(&r, 3) == 0 ? BI_FOREST_HILLS : BI_FOREST;
                    else if (k == BI_ICE_PLAINS) i2 = BI_ICE_MOUNTAINS;
                    else if (k == BI_JUNGLE) i2 = BI_JUNGLE_HILLS;
                    else if (k == BI_OCEAN) i2 = BI_DEEP_OCEAN;
                    else if (k == BI_EXTREME_HILLS) i2 = BI_EH_PLUS;
                    else if (k == BI_SAVANNA) i2 = BI_SAVANNA_PLATEAU;
                    else if (bio_same(k, BI_MESA_PLATEAU_F)) i2 = BI_MESA;
                    else if (k == BI_DEEP_OCEAN && lr_int(&r, 3) == 0) i2 = lr_int(&r, 2) == 0 ? BI_PLAINS : BI_FOREST;
                    if (flag && i2 != k) i2 = bio_exists(i2 + 128) ? i2 + 128 : k;
                    if (i2 == k) {
                        v = k;
                    } else {
                        int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                            s = in[i + 1 + (j + 2) * pw];
                        int c = bio_same(n, k) + bio_same(e, k) + bio_same(ww, k) + bio_same(s, k);
                        v = c >= 3 ? i2 : k;
                    }
                }
                out[i + j * w] = v;
            }
        break;
    case L_PLAINS:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                out[i + j * w] = (lr_int(&r, 57) == 0 && k == BI_PLAINS) ? BI_PLAINS + 128 : k;
            }
        break;
    case L_MUSHSHORE:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                    s = in[i + 1 + (j + 2) * pw];
                int v;
                const Biome *b = bio_exists(k) ? bio(k) : 0;
#define JUNGLEOK(q) ((bio_exists(q) && bio(q)->cls == C_JUNGLE) || (q) == BI_JUNGLE_EDGE || (q) == BI_JUNGLE || (q) == BI_JUNGLE_HILLS || (q) == BI_FOREST || (q) == BI_TAIGA || is_ocean_id(q))
#define ISMESA(q) (bio_exists(q) && bio(q)->cls == C_MESA)
                if (k == BI_MUSHROOM) {
                    v = (n != BI_OCEAN && e != BI_OCEAN && ww != BI_OCEAN && s != BI_OCEAN) ? k : BI_MUSHROOM_SHORE;
                } else if (b && b->cls == C_JUNGLE) {
                    if (JUNGLEOK(n) && JUNGLEOK(e) && JUNGLEOK(ww) && JUNGLEOK(s))
                        v = (!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_BEACH;
                    else
                        v = BI_JUNGLE_EDGE;
                } else if (k != BI_EXTREME_HILLS && k != BI_EH_PLUS && k != BI_EH_EDGE) {
                    if (b && b->snowy) {
                        v = is_ocean_id(k) ? k : ((!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_COLD_BEACH);
                    } else if (k != BI_MESA && k != BI_MESA_PLATEAU_F) {
                        if (k != BI_OCEAN && k != BI_DEEP_OCEAN && k != BI_RIVER && k != BI_SWAMP)
                            v = (!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_BEACH;
                        else
                            v = k;
                    } else {
                        if (!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s))
                            v = (ISMESA(n) && ISMESA(e) && ISMESA(ww) && ISMESA(s)) ? k : BI_DESERT;
                        else
                            v = k;
                    }
                } else {
                    v = is_ocean_id(k) ? k : ((!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_STONE_BEACH);
                }
                out[i + j * w] = v;
            }
        break;
    case L_RIVER:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
#define RC(q) ((q) >= 2 ? 2 + ((q) & 1) : (q))
                int a = RC(in[i + (j + 1) * pw]), b = RC(in[i + 2 + (j + 1) * pw]);
                int c = RC(in[i + 1 + j * pw]), d = RC(in[i + 1 + (j + 2) * pw]);
                int k = RC(in[i + 1 + (j + 1) * pw]);
                out[i + j * w] = (k == a && k == c && k == b && k == d) ? -1 : BI_RIVER;
            }
        break;
    case L_SMOOTH:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int a = in[i + (j + 1) * pw], b = in[i + 2 + (j + 1) * pw];
                int c = in[i + 1 + j * pw], d = in[i + 1 + (j + 2) * pw];
                int k = in[i + 1 + (j + 1) * pw];
                if (a == b && c == d) {
                    lr_chunk(&r, x + i, z + j);
                    k = lr_int(&r, 2) == 0 ? a : c;
                } else {
                    if (a == b) k = a;
                    if (c == d) k = c;
                }
                out[i + j * w] = k;
            }
        break;
    }
}

/* Runs a chain of layers (the last one producing window x,z,w,h), starting from the
 * output of `base` layers (computed recursively by run_base) or from `start`.
 * Returns a pointer to the result (allocated at the bump allocator). */
typedef struct {
    int x, z, w, h;
} Win;

static i32 *run_chain(const LayerDef *const *defs, int n, int x, int z, int w, int h, const i32 *second, const i32 *in0) {
    /* windows, top down */
    Win win[40];
    win[n - 1] = (Win){x, z, w, h};
    for (int i = n - 1; i > 0; i--)
        layer_parent_win(defs[i]->type, win[i].x, win[i].z, win[i].w, win[i].h, &win[i - 1].x, &win[i - 1].z,
                         &win[i - 1].w, &win[i - 1].h);
    /* bottom up: each layer's output is allocated right after its input, then moved down
     * over it (peak = largest input + output pair); in0: input of the first layer, if any
     * (it is overwritten) */
    int mark = in0 ? (int)(in0 - lay_mem) : lay_top;
    const i32 *cur = in0;
    for (int i = 0; i < n; i++) {
        i32 *dst = lay_alloc(win[i].w * win[i].h);
        layer_apply(defs[i], cur, (defs[i]->type == L_HILLS) ? second : 0, dst, win[i].x, win[i].z, win[i].w,
                    win[i].h);
        memmove(lay_mem + mark, dst, sizeof(i32) * (size_t)(win[i].w * win[i].h));
        cur = lay_mem + mark;
        lay_top = mark + win[i].w * win[i].h;
    }
    return lay_mem + mark;
}

/* window at the input of the first of n layers whose last outputs `top` */
static Win chain_in_win(const LayerDef *defs, int n, Win top) {
    for (int i = n - 1; i >= 0; i--) layer_parent_win(defs[i].type, top.x, top.z, top.w, top.h, &top.x, &top.z, &top.w, &top.h);
    return top;
}

/* sub-window sw of the base layers' output (window bw at base) copied to a new block */
static i32 *base_sub(const i32 *base, Win bw, Win sw) {
    i32 *p = lay_alloc(sw.w * sw.h);
    for (int j = 0; j < sw.h; j++)
        memcpy(p + j * sw.w, base + (sw.z - bw.z + j) * bw.w + (sw.x - bw.x), sizeof(i32) * (size_t)sw.w);
    return p;
}

static const LayerDef *chain_buf[40];

static int chain_build(const LayerDef *tail, int ntail, int with_base) {
    int n = 0;
    if (with_base)
        for (int i = 0; i < NBASE; i++) chain_buf[n++] = &LBASE[i];
    for (int i = 0; i < ntail; i++) chain_buf[n++] = &tail[i];
    return n;
}

/* GenLayerRiverMix output for window (x,z,w,h) into dst (biome ids) */
static void layers_rivermix(int x, int z, int w, int h, i32 *dst) {
    int mark0 = lay_top, mark = mark0;
    /* biome branch: base + biome1 to desert, then hills (needs the hills river noise), then biome2 */
    /* window of the hills layer input */
    const LayerDef *b2[9];
    for (int i = 0; i < 9; i++) b2[i] = &LBIOME2[i];
    Win wb2[9];
    wb2[8] = (Win){x - 1, z - 1, w + 2, h + 2}; /* the smooth layer input is fed by b2[7]; RiverMix needs smooth(x,z,w,h) */
    /* compute windows for b2 chain from its top (smooth outputs x,z,w,h) */
    wb2[8] = (Win){x, z, w, h};
    for (int i = 8; i > 0; i--)
        layer_parent_win(b2[i]->type, wb2[i].x, wb2[i].z, wb2[i].w, wb2[i].h, &wb2[i - 1].x, &wb2[i - 1].z,
                         &wb2[i - 1].w, &wb2[i - 1].h);
    Win hin; /* input window of the hills layer */
    layer_parent_win(L_HILLS, wb2[0].x, wb2[0].z, wb2[0].w, wb2[0].h, &hin.x, &hin.z, &hin.w, &hin.h);
    /* the base layers (shared by the three branches; each cell's value only depends on the
     * cell) once, over the union of the windows the branches need from them */
    Win w1 = chain_in_win(LHILLSRIVER, 3, hin), w2 = chain_in_win(LBIOME1, 4, hin);
    Win w3 = chain_in_win(LRIVER, 9, (Win){x, z, w, h});
    Win bw = w1;
    for (int k = 0; k < 2; k++) {
        Win o = k ? w3 : w2;
        int x1 = bw.x + bw.w > o.x + o.w ? bw.x + bw.w : o.x + o.w, z1 = bw.z + bw.h > o.z + o.h ? bw.z + bw.h : o.z + o.h;
        bw.x = bw.x < o.x ? bw.x : o.x;
        bw.z = bw.z < o.z ? bw.z : o.z;
        bw.w = x1 - bw.x;
        bw.h = z1 - bw.z;
    }
    int n = chain_build(0, 0, 1);
    i32 *base = run_chain(chain_buf, n, bw.x, bw.z, bw.w, bw.h, 0, 0);
    /* river branch first (its result stays below the biome branch's work) */
    n = chain_build(LRIVER, 9, 0);
    i32 *river = run_chain(chain_buf, n, x, z, w, h, 0, base_sub(base, bw, w3));
    /* hills river noise */
    n = chain_build(LHILLSRIVER, 3, 0);
    i32 *hr = run_chain(chain_buf, n, hin.x, hin.z, hin.w, hin.h, 0, base_sub(base, bw, w1));
    /* biome chain up to desert */
    n = chain_build(LBIOME1, 4, 0);
    i32 *bd = run_chain(chain_buf, n, hin.x, hin.z, hin.w, hin.h, 0, base_sub(base, bw, w2));
    mark = (int)(hr - lay_mem); /* the biome branch's results go over hr and bd */
    /* hills .. smooth, fed by bd (the hills layer also reads hr) */
    {
        int mark2 = (int)(bd - lay_mem);
        const i32 *cur = bd;
        for (int i = 0; i < 9; i++) {
            i32 *dst = lay_alloc(wb2[i].w * wb2[i].h);
            layer_apply(b2[i], cur, hr, dst, wb2[i].x, wb2[i].z, wb2[i].w, wb2[i].h);
            if (i == 0) {
                /* hr is no longer needed: move the hills output down to mark (over hr and bd) */
                memmove(lay_mem + mark, dst, sizeof(i32) * (size_t)(wb2[0].w * wb2[0].h));
                cur = lay_mem + mark;
                lay_top = mark + wb2[0].w * wb2[0].h;
            } else {
                memmove(lay_mem + mark, dst, sizeof(i32) * (size_t)(wb2[i].w * wb2[i].h));
                cur = lay_mem + mark;
                lay_top = mark + wb2[i].w * wb2[i].h;
            }
        }
        (void)mark2;
    }
    i32 *biomes = lay_mem + mark;
    for (int i = 0; i < w * h; i++) {
        int b = biomes[i], rv = river[i];
        int v;
        if (b != BI_OCEAN && b != BI_DEEP_OCEAN) {
            if (rv == BI_RIVER) {
                if (b == BI_ICE_PLAINS) v = BI_FROZEN_RIVER;
                else if (b != BI_MUSHROOM && b != BI_MUSHROOM_SHORE) v = rv & 255;
                else v = BI_MUSHROOM_SHORE;
            } else {
                v = b;
            }
        } else {
            v = b;
        }
        dst[i] = v;
    }
    lay_top = mark0;
}

static i64 vor_c;

/* jitter values for the 4 corner points of a cell, as integers u = a(1024) - 512 */
static void vor_cell(i64 cx4, i64 cz4, int u[8]) {
    LRand r;
    r.c = vor_c;
    lr_chunk(&r, SHL(cx4, 2), SHL(cz4, 2));
    u[0] = lr_int(&r, 1024) - 512; /* d1 x */
    u[1] = lr_int(&r, 1024) - 512; /* d2 z */
    lr_chunk(&r, SHL(cx4 + 1, 2), SHL(cz4, 2));
    u[2] = lr_int(&r, 1024) - 512; /* d3 x (+4) */
    u[3] = lr_int(&r, 1024) - 512; /* d4 z */
    lr_chunk(&r, SHL(cx4, 2), SHL(cz4 + 1, 2));
    u[4] = lr_int(&r, 1024) - 512; /* d5 x */
    u[5] = lr_int(&r, 1024) - 512; /* d6 z (+4) */
    lr_chunk(&r, SHL(cx4 + 1, 2), SHL(cz4 + 1, 2));
    u[6] = lr_int(&r, 1024) - 512; /* d7 x (+4) */
    u[7] = lr_int(&r, 1024) - 512; /* d8 z (+4) */
}

/* which corner (0: x0z0, 1: x1z0, 2: x0z1, 3: x1z1) sub-position (sx, sz) of a cell picks */
static int vor_pick(const int u[8], int sx, int sz) {
    /* (i - d) * 2560 = 2560 * (i - off) - 9u, exact */
    static const int offx[4] = {0, 4, 0, 4}, offz[4] = {0, 0, 4, 4};
    i64 dd[4];
    for (int k = 0; k < 4; k++) {
        i64 nx = 2560LL * (sx - offx[k]) - 9LL * u[2 * k];
        i64 nz = 2560LL * (sz - offz[k]) - 9LL * u[2 * k + 1];
        dd[k] = nx * nx + nz * nz;
    }
    if (dd[0] == dd[1] || dd[0] == dd[2] || dd[0] == dd[3] || dd[1] == dd[2] || dd[1] == dd[3] || dd[2] == dd[3]) {
        /* exact tie: replay Java's double arithmetic */
        double d[8];
        for (int k = 0; k < 8; k++) {
            d[k] = ((double)(u[k] + 512) / 1024 - (double)1 / 2) * ((double)36 / 10); /* 3.6 */
            if ((k == 2) || (k == 5) || (k == 6) || (k == 7)) d[k] += 4;
        }
        double i4 = sz, k4 = sx;
        double d9 = (i4 - d[1]) * (i4 - d[1]) + (k4 - d[0]) * (k4 - d[0]);
        double d10 = (i4 - d[3]) * (i4 - d[3]) + (k4 - d[2]) * (k4 - d[2]);
        double d11 = (i4 - d[5]) * (i4 - d[5]) + (k4 - d[4]) * (k4 - d[4]);
        double d12 = (i4 - d[7]) * (i4 - d[7]) + (k4 - d[6]) * (k4 - d[6]);
        if (d9 < d10 && d9 < d11 && d9 < d12) return 0;
        if (d10 < d9 && d10 < d11 && d10 < d12) return 1;
        if (d11 < d9 && d11 < d10 && d11 < d12) return 2;
        return 3;
    }
    if (dd[0] < dd[1] && dd[0] < dd[2] && dd[0] < dd[3]) return 0;
    if (dd[1] < dd[0] && dd[1] < dd[2] && dd[1] < dd[3]) return 1;
    if (dd[2] < dd[0] && dd[2] < dd[1] && dd[2] < dd[3]) return 2;
    return 3;
}

/* GenLayerZoomVoronoi for the block window (x, z, w, h); rm = rivermix window starting at
 * ((x-2)>>2, (z-2)>>2), row stride rw. Output uint8 biome ids, row stride ostride. */
static void voronoi(const i32 *rm, int rx, int rz, int rw, int x, int z, int w, int h, uint8_t *out, int ostride) {
    int u[8];
    i64 lastx = INT64_MIN, lastz = INT64_MIN;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int X = x + i - 2, Z = z + j - 2;
            int cx4 = X >> 2, cz4 = Z >> 2;
            if (cx4 != lastx || cz4 != lastz) {
                vor_cell(cx4, cz4, u);
                lastx = cx4;
                lastz = cz4;
            }
            int k = vor_pick(u, X & 3, Z & 3);
            int ix = cx4 - rx + (k & 1), iz = cz4 - rz + (k >> 1);
            out[i + j * ostride] = (uint8_t)(rm[ix + iz * rw] & 255);
        }
}

/* ======================================================================== */
/* Noise                                                                     */
/* ======================================================================== */

/* A NoiseGeneratorPerlin / NoiseGenerator3Handler is fully determined by the
 * java.util.Random state at its construction: 3 nextDouble (offsets) then 256
 * swaps. We keep only that state and rebuild the permutation when needed. */
typedef struct {
    u64 st;
} Octave;

static const uint8_t PERM_ID[256] = {
#define R16(b) b, b + 1, b + 2, b + 3, b + 4, b + 5, b + 6, b + 7, b + 8, b + 9, b + 10, b + 11, b + 12, b + 13, b + 14, b + 15
    R16(0), R16(16), R16(32), R16(48), R16(64), R16(80), R16(96), R16(112),
    R16(128), R16(144), R16(160), R16(176), R16(192), R16(208), R16(224), R16(240)
#undef R16
};

HOT static void perm_build(u64 st, uint8_t *perm, i64 off[3]) {
    JRand r = {st};
    for (int k = 0; k < 3; k++) {
        i64 n = jr_double_bits(&r);
        if (off) off[k] = n >> 13; /* offset * 2^32 (offset = n / 2^53 * 256), Q32 */
    }
    memcpy(perm, PERM_ID, 256);
    for (int i = 0; i < 256; i++) {
        int j = jr_int_i(&r, 256 - i) + i;
        uint8_t t = perm[i];
        perm[i] = perm[j];
        perm[j] = t;
    }
}

static void perm_skip(JRand *r) {
    for (int k = 0; k < 6; k++) jr_next(r, 27);
    for (int i = 0; i < 256; i++) jr_int(r, 256 - i);
}

INLINE float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
INLINE float lerp(float t, float a, float b) { return a + t * (b - a); }

INLINE float grad3(int h, float x, float y, float z) {
    switch (h & 15) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x + z;
    case 5: return -x + z;
    case 6: return x - z;
    case 7: return -x - z;
    case 8: return y + z;
    case 9: return -y + z;
    case 10: return y - z;
    case 11: return -y - z;
    case 12: return x + y;
    case 13: return -y + z;
    case 14: return -x + y;
    default: return -y - z;
    }
}

/* Coordinates of samples: for octave `oct` (frequency 2^-oct) of a generator whose
 * scale (Java double from a float) is S_fx = scale * 2^32, the sample index n of a
 * request starting at integer position base gives
 *   coord = (base * scale + n * scale) * 2^-oct + offset
 * We compute it modulo 2^32 * 256 in Q32 fixed point: cell = bits 32..39, frac = bits 0..31. */
INLINE u64 coord_q32(i64 base, u64 S_fx, int oct, i64 n, i64 off) {
    u64 b = ((u64)base * S_fx) >> oct;
    return b + (u64)n * (S_fx >> oct) + (u64)off;
}

INLINE void coord_split(u64 c, int *cell, float *frac) {
    *cell = (int)((c >> 32) & 255);
    *frac = (float)(u32)c * (1.0f / 4294967296.0f);
}

/* Fixed-point scales (value * 2^32) of the generators' sample spacings: Java passes
 * (double)float, and these floats are exactly representable in Q32. */
#define FX(f) ((u64)((double)(f) * (double)(1ULL << 32)))
static u64 SC_LIMIT, SC_MAINXZ, SC_MAINY, SC_DEPTH;

/* One 3D octave over the chunk grid (5 x 33 x 5, index (x*5+z)*33+y), Java's
 * NoiseGeneratorPerlin.a with its stale-cache quirk: the four x-lerps are recomputed only
 * when the y cell changes, with the y fraction of the first sample of that run of samples in
 * the same cell. need: bitmask per point or NULL; rng: per column, the range [lo, hi] of y
 * holding the points needed (or NULL: all); mul: per-point multiplier array (or NULL for 1). */
HOT static void perlin3_octave(const uint8_t *P, float *acc, const uint8_t *need, int bit, const uint8_t *rng,
                               const float *mul, const int *xc, const float *xf, const int *zc, const float *zf,
                               const int *yc, const float *yf, float amp) {
    float uyv[33];
    uint8_t run[33]; /* first y of the run of samples in the same cell as y */
    for (int y = 0; y < 33; y++) {
        uyv[y] = fade(yf[y]);
        run[y] = (uint8_t)(y > 0 && yc[y] == yc[y - 1] ? run[y - 1] : y);
    }
    for (int x = 0; x < 5; x++) {
        float fx = xf[x], ux = fade(fx), gx1 = fx - 1;
        int l1 = xc[x], pa = P[l1], pb = P[(l1 + 1) & 255];
        for (int z = 0; z < 5; z++) {
            int col = x * 5 + z, lo = rng ? rng[col * 2] : 0, hi = rng ? rng[col * 2 + 1] : 32;
            if (lo > hi) continue;
            float fz = zf[z], uz = fade(fz), gz1 = fz - 1;
            int j4 = zc[z], cur = -1;
            float d16 = 0, d7 = 0, d17 = 0, d8 = 0;
            for (int y = lo; y <= hi; y++) {
                int idx = col * 33 + y;
                if (need && !(need[idx] & bit)) continue;
                if (run[y] != cur) {
                    cur = run[y];
                    float fy = yf[cur], gy1 = fy - 1;
                    int i5 = yc[cur];
                    int j5 = pa + i5;
                    int k5 = P[j5 & 255] + j4;
                    int l5 = P[(j5 + 1) & 255] + j4;
                    int i6 = pb + i5;
                    int i2 = P[i6 & 255] + j4;
                    int j6 = P[(i6 + 1) & 255] + j4;
                    d16 = lerp(ux, grad3(P[k5 & 255], fx, fy, fz), grad3(P[i2 & 255], gx1, fy, fz));
                    d7 = lerp(ux, grad3(P[l5 & 255], fx, gy1, fz), grad3(P[j6 & 255], gx1, gy1, fz));
                    d17 = lerp(ux, grad3(P[(k5 + 1) & 255], fx, fy, gz1), grad3(P[(i2 + 1) & 255], gx1, fy, gz1));
                    d8 = lerp(ux, grad3(P[(l5 + 1) & 255], fx, gy1, gz1), grad3(P[(j6 + 1) & 255], gx1, gy1, gz1));
                }
                float uy = uyv[y];
                float v = lerp(uz, lerp(uy, d16, d7), lerp(uy, d17, d8));
                if (mul) acc[idx] += v * amp * mul[idx];
                else acc[idx] += v * amp;
            }
        }
    }
}

/* per grid column, the range [lo, hi] of y whose points have one of the bits (lo > hi: none);
 * returns whether any point has them */
static int need_ranges(const uint8_t *cls, int bits, uint8_t *rng) {
    int any = 0;
    for (int c = 0; c < 25; c++) {
        int lo = 33, hi = -1;
        for (int y = 0; y < 33; y++)
            if (cls[c * 33 + y] & bits) {
                if (lo == 33) lo = y;
                hi = y;
            }
        rng[c * 2] = (uint8_t)(lo == 33 ? 1 : lo);
        rng[c * 2 + 1] = (uint8_t)(lo == 33 ? 0 : hi);
        any |= lo != 33;
    }
    return any;
}

/* 2D octave (Java's j == 1 path) over 5 x 5 */
HOT static void perlin2_octave(const uint8_t *P, float *acc, const int *xc, const float *xf, const int *zc,
                           const float *zf, float amp) {
    int idx = 0;
    for (int x = 0; x < 5; x++) {
        float fx = xf[x], ux = fade(fx);
        int i3 = xc[x];
        for (int z = 0; z < 5; z++, idx++) {
            float fz = zf[z], uz = fade(fz);
            int l1 = zc[z];
            int l = P[i3];
            int j3 = P[l & 255] + l1;
            int k3 = P[(i3 + 1) & 255];
            int i1 = P[k3 & 255] + l1;
            float d11 = lerp(ux, grad3(P[j3 & 255], fx, 0, fz), grad3(P[i1 & 255], fx - 1, 0, fz));
            float d12 = lerp(ux, grad3(P[(j3 + 1) & 255], fx, 0, fz - 1), grad3(P[(i1 + 1) & 255], fx - 1, 0, fz - 1));
            acc[idx] += lerp(uz, d11, d12) * amp;
        }
    }
}

/* ---- simplex (NoiseGenerator3Handler) ---- */
/* constants as Q64 fractions for exact integer setup */
#define F2D 0.36602540378443864676 /* 0.5 * (sqrt(3) - 1) */
#define G2D 0.21132486540518711775 /* (3 - sqrt(3)) / 6 */
#define H2D 0.57735026918962576451 /* 1 - 2 * G2 = sqrt(3) / 3 */
static const u64 F2_Q64 = 0x5DB3D742C265539DULL; /* F2 * 2^64 */

/* (k * frac) in Q32 (k signed, frac = q/2^64), exact to ~2^-32 */
static inline i64 mul_q64(i64 k, u64 q) {
    u64 hi = q >> 32, lo = q & 0xFFFFFFFFULL;
    i64 a = k * (i64)hi;               /* Q32 */
    i64 b = (k * (i64)lo) >> 32;       /* Q32 (|k| < 2^31) */
    return a + b;
}

static const float SGRAD[12][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {1, 0}, {-1, 0},
                                   {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0, 1}, {0, -1}};

/* simplex value at (xi + xf, zi + zf); xf, zf small (|.| < ~4) */
HOT static float simplex2(const uint8_t *P, i32 xi, float xf, i32 zi, float zf) {
    i32 K = xi + zi;
    i64 sq = mul_q64(K, F2_Q64); /* K*F2, Q32 */
    i32 si = (i32)(sq >> 32);
    float sf = (float)(u32)sq * (1.0f / 4294967296.0f);
    float t = (xf + zf) * (float)F2D;
    float sx = xf + sf + t, sz = zf + sf + t;
    int fsx = floor_f_i(sx), fsz = floor_f_i(sz);
    i32 ci = xi + si + fsx, cj = zi + si + fsz;
    /* unskew: x0 = xf - fsx + (fsx + fsz) * G2 + (K * G2 - si * (1 - 2 G2)), and the last
     * term equals sf * (1 - 2 G2) because F2 * (1 - 2 G2) = G2 */
    float u = (float)(fsx + fsz) * (float)G2D + sf * (float)H2D;
    float x0 = (xf - (float)fsx) + u, z0 = (zf - (float)fsz) + u;
    int b0, b1;
    if (x0 > z0) {
        b0 = 1;
        b1 = 0;
    } else {
        b0 = 0;
        b1 = 1;
    }
    float x1 = x0 - (float)b0 + (float)G2D, z1 = z0 - (float)b1 + (float)G2D;
    float x2 = x0 - 1.0f + 2.0f * (float)G2D, z2 = z0 - 1.0f + 2.0f * (float)G2D;
    int k = ci & 255, l = cj & 255;
    int g0 = P[(k + P[l]) & 255] % 12;
    int g1 = P[(k + b0 + P[(l + b1) & 255]) & 255] % 12;
    int g2 = P[(k + 1 + P[(l + 1) & 255]) & 255] % 12;
    float n = 0;
    float t0 = 0.5f - x0 * x0 - z0 * z0;
    if (t0 >= 0) {
        t0 *= t0;
        n += t0 * t0 * (SGRAD[g0][0] * x0 + SGRAD[g0][1] * z0);
    }
    float t1 = 0.5f - x1 * x1 - z1 * z1;
    if (t1 >= 0) {
        t1 *= t1;
        n += t1 * t1 * (SGRAD[g1][0] * x1 + SGRAD[g1][1] * z1);
    }
    float t2 = 0.5f - x2 * x2 - z2 * z2;
    if (t2 >= 0) {
        t2 *= t2;
        n += t2 * t2 * (SGRAD[g2][0] * x2 + SGRAD[g2][1] * z2);
    }
    return 70.0f * n;
}

/* Java's single-point NoiseGenerator3Handler.a(double, double) in double: used only to
 * settle values the float version puts within 1e-5 of a threshold that matters. */
static int jfloor_s(double d) { return d > 0 ? (int)d : (int)d - 1; }

static double simplex2_d(const uint8_t *P, double d0, double d1) {
    const double s3 = sqrt((double)3), F = (s3 - 1) / 2, G = (3 - s3) / 6;
    double d3 = (d0 + d1) * F;
    int i = jfloor_s(d0 + d3), j = jfloor_s(d1 + d3);
    double d5 = (double)(i + j) * G;
    double d8 = d0 - ((double)i - d5), d9 = d1 - ((double)j - d5);
    int b0 = d8 > d9, b1 = !b0;
    double d10 = d8 - b0 + G, d11 = d9 - b1 + G, d12 = d8 - 1 + 2 * G, d13 = d9 - 1 + 2 * G;
    int k = i & 255, l = j & 255;
    int g0 = P[(k + P[l]) & 255] % 12, g1 = P[(k + b0 + P[(l + b1) & 255]) & 255] % 12,
        g2 = P[(k + 1 + P[(l + 1) & 255]) & 255] % 12;
    double n = 0, t;
    t = (double)1 / 2 - d8 * d8 - d9 * d9;
    if (t >= 0) {
        t *= t;
        n += t * t * ((double)SGRAD[g0][0] * d8 + (double)SGRAD[g0][1] * d9);
    }
    t = (double)1 / 2 - d10 * d10 - d11 * d11;
    if (t >= 0) {
        t *= t;
        n += t * t * ((double)SGRAD[g1][0] * d10 + (double)SGRAD[g1][1] * d11);
    }
    t = (double)1 / 2 - d12 * d12 - d13 * d13;
    if (t >= 0) {
        t *= t;
        n += t * t * ((double)SGRAD[g2][0] * d12 + (double)SGRAD[g2][1] * d13);
    }
    return 70 * n;
}

/* ======================================================================== */
/* World state                                                               */
/* ======================================================================== */

static Octave oc_min[16], oc_max[16], oc_main[8], oc_surf[4], oc_depth[16];
static uint8_t perm_temp[256], perm_grass[256]; /* BiomeBase.ae (1234), .af (2345) */
static Octave oc_mesa[6];                          /* aH, aF[0..3], aG */
static uint8_t mesa_bands[64];                     /* B_ ids */
static i64 pop_mul_x, pop_mul_z;                   /* population seed multipliers */
static i64 cave_mul_x, cave_mul_z;                 /* MapGenBase seed multipliers */

/* temperature noise (BiomeBase.ae) at block x, z: ae.a(x/8, z/8) */
static float temp_noise(int x, int z) {
    return simplex2(perm_temp, x >> 3, (float)(x & 7) * 0.125f, z >> 3, (float)(z & 7) * 0.125f);
}

/* BiomeBase.a(BlockPosition): temperature at height */
static float biome_temp_at(const Biome *b, int x, int y, int z) {
    if (y > 64) {
        float f = temp_noise(x, z) * 4.0f;
        return b->temp - (f + (float)y - 64.0f) * 0.05f / 30.0f;
    }
    return b->temp;
}

/* af.a(x / d, z / d) for an integer divisor d (exact split) */
static float grass_noise_div(int x, int z, int d) {
    int xi = x >= 0 ? x / d : -((-x + d - 1) / d), zi = z >= 0 ? z / d : -((-z + d - 1) / d);
    return simplex2(perm_grass, xi, (float)(x - xi * d) / (float)d, zi, (float)(z - zi * d) / (float)d);
}

/* ======================================================================== */
/* Scratch memory (phases never overlap)                                     */
/* ======================================================================== */

#define LAY_INTS 1650 /* the 18 x 18 biome window peaks at 1084 + its output; spawn_rows must fit in U */

typedef struct {
    float grid[825];      /* main noise, then density */
    union {
        float acc[825];   /* limit noise accumulator (density pass) */
        struct {          /* surface pass, after the density */
            float surf[256];       /* surface depth noise, index z*16+x */
            uint8_t mperm[6][256]; /* mesa noises (built when a mesa column shows up) */
            uint8_t col[256];
        };
    };
    uint8_t cls[825];     /* per grid point: sign decided by bounds / noise needed / limits needed */
    uint8_t perm[256];
    uint8_t mesa_ready;
} TerrainScratch;

typedef struct {
    int x, y, z; /* lake box origin (world) */
    uint8_t liquid;
    uint8_t shape[256]; /* 16x16x8 bits: index (x*16+z)*8+y */
} LakeRec;

typedef struct {
    int px, pz, ox, oz;            /* populated chunk, frame origin (frame = 32x32 columns) */
    uint8_t top[1024], tb[1024];   /* highest non-plant block of each frame column, and the block */
    uint8_t pl[1024];              /* plant-like block right above top (0 = none) */
    int nlake;
    LakeRec lake[2];
    /* cave query mask active for reads */
    int qon, qx0, qy0, qz0, qx1, qy1, qz1;
} View;

typedef struct {
    View v;
    uint8_t qmask[2048];
} PopScratch;

typedef struct {
    int x, y, z, q;
} BigPos;

/* Scratch memory. The phases never overlap: layers (in the terrain pass), terrain, then caves
 * and populations (PopScratch plus the cave walk's and big oak's tables after it). */
static union {
    TerrainScratch t;
    i32 lay[LAY_INTS];
    PopScratch p;
    struct {
        PopScratch p_;
        float canyon_w[256]; /* WorldGenCanyon.d */
        BigPos big_list[48]; /* WorldGenBigTree's branch ends */
    } x;
    uint8_t spawn_rows[sizeof(i32) * LAY_INTS + 8 * 129];
} U;
#define canyon_w (U.x.canyon_w)
#define big_list (U.x.big_list)

/* ======================================================================== */
/* Terrain                                                                   */
/* ======================================================================== */

static float bweights[25]; /* ChunkProviderGenerate.q */

/* biomes of chunk (cx, cz): 1:4 grid 10x10 at (cx*4-2, cz*4-2) and 16x16 blocks */
static uint8_t cb4[100], cb16[256];

static int bc_cx = 0x7fffffff, bc_cz; /* chunk whose biomes cb16 holds */

/* River-mix biomes (1:4 cells) of 3 x 3 chunks, kept between calls: running the layers costs
 * mostly the same for a chunk's 10 x 10 cells as for this 18 x 18 window, and the chunks whose
 * terrain a gen_slab call computes are around the same place. */
#define BG_W 18
static uint8_t bgrid[BG_W * BG_W];
static int bg_x, bg_z; /* cell of bgrid[0] */
static int64_t bg_seed;
static int bg_ok;

static void chunk_biomes(int cx, int cz) {
    bc_cx = cx;
    bc_cz = cz;
    int x0 = cx * 4 - 2, z0 = cz * 4 - 2;
    if (!bg_ok || bg_seed != g_seed || x0 < bg_x || z0 < bg_z || x0 + 10 > bg_x + BG_W || z0 + 10 > bg_z + BG_W) {
        bg_x = x0 - 4;
        bg_z = z0 - 4;
        bg_seed = g_seed;
        bg_ok = 1;
        lay_mem = U.lay + BG_W * BG_W;
        lay_cap = LAY_INTS - BG_W * BG_W;
        lay_top = 0;
        layers_rivermix(bg_x, bg_z, BG_W, BG_W, U.lay);
        for (int i = 0; i < BG_W * BG_W; i++) bgrid[i] = (uint8_t)U.lay[i];
    }
    i32 *rm = U.lay;
    for (int j = 0; j < 10; j++)
        for (int i = 0; i < 10; i++) rm[i + j * 10] = bgrid[(x0 - bg_x + i) + (z0 - bg_z + j) * BG_W];
    for (int i = 0; i < 100; i++) cb4[i] = (uint8_t)rm[i];
    /* voronoi window for blocks (cx*16, cz*16, 16, 16) is rivermix (cx*4-1, cz*4-1, 6, 6),
     * a sub-window of the 10x10 one at offset (1, 1) */
    voronoi(rm + 1 + 10, cx * 4 - 1, cz * 4 - 1, 10, cx * 16, cz * 16, 16, 16, cb16, 16);
}

static void octave_setup(const Octave *o, i64 off[3]) { perm_build(o->st, U.t.perm, off); }

/* octave o (generator state oc) of a 3D noise over the chunk grid at grid origin (bx, bz):
 * horizontal / vertical sample spacings sxz, sy (Q32), accumulated into acc as perlin3_octave */
HOT static void noise3_octave(const Octave *oc, int o, i64 bx, i64 bz, u64 sxz, u64 sy, float *acc, const uint8_t *need,
                          int bit, const uint8_t *rng, const float *mul) {
    int xc[5], zc[5], yc[33];
    float xf[5], zf[5], yf[33];
    i64 off[3];
    octave_setup(oc, off);
    for (int n = 0; n < 5; n++) {
        coord_split(coord_q32(bx, sxz, o, n, off[0]), &xc[n], &xf[n]);
        coord_split(coord_q32(bz, sxz, o, n, off[2]), &zc[n], &zf[n]);
    }
    for (int n = 0; n < 33; n++) coord_split(coord_q32(0, sy, o, n, off[1]), &yc[n], &yf[n]);
    perlin3_octave(U.t.perm, acc, need, bit, rng, mul, xc, xf, zc, zf, yc, yf, (float)(1 << o));
}

/* the density's height offset at grid level j2 (ChunkProviderGenerate: d4) */
INLINE float dens_offset(int j2, float d3, float d2) {
    float d4 = ((float)j2 - d3) * 12.0f * 128.0f / 256.0f / d2;
    return d4 < 0.0f ? d4 * 4.0f : d4;
}

/* noise grid for chunk (cx, cz) -> U.t.grid holds the 5x5x33 densities.
 * The density is lerp(min, max, main) / 512 - offset(y); each limit noise is a sum of 16
 * octaves of amplitude 2^o whose values are within [-2, 2] (gradient dots of fractions,
 * lerped), so the noise part is within +-256. Grid points where offset(y) alone decides the
 * sign for every cell around them do not need any noise: they get +-1 instead (cells whose
 * 8 corners all agree interpolate to the same sign whatever the values). */
HOT static void chunk_density(int cx, int cz) {
    TerrainScratch *T = &U.t;
    int xc[5], zc[5];
    float xf[5], zf[5];
    i64 off[3];
    i64 bx = (i64)cx * 4, bz = (i64)cz * 4;
    /* depth noise (2D, 16 octaves) */
    float depth[25];
    memset(depth, 0, sizeof depth);
    for (int o = 0; o < 16; o++) {
        octave_setup(&oc_depth[o], off);
        for (int n = 0; n < 5; n++) {
            coord_split(coord_q32(bx, SC_DEPTH, o, n, off[0]), &xc[n], &xf[n]);
            coord_split(coord_q32(bz, SC_DEPTH, o, n, off[2]), &zc[n], &zf[n]);
        }
        perlin2_octave(T->perm, depth, xc, xf, zc, zf, (float)(1 << o));
    }
    /* per grid column: height (d3) and scale (d2) from the blended biomes and the depth noise */
    float colD3[25], colD2[25];
    for (int j1 = 0, i1 = 0; j1 < 5; j1++)
        for (int k1 = 0; k1 < 5; k1++, i1++) {
            float f2 = 0, f3 = 0, f4 = 0;
            const Biome *bc = bio(cb4[j1 + 2 + (k1 + 2) * 10]);
            for (int l1 = -2; l1 <= 2; l1++)
                for (int i2 = -2; i2 <= 2; i2++) {
                    const Biome *b1 = bio(cb4[j1 + l1 + 2 + (k1 + i2 + 2) * 10]);
                    float f5 = 0.0f + b1->depth * 1.0f;
                    float f6 = 0.0f + b1->scale * 1.0f;
                    float f7 = bweights[l1 + 2 + (i2 + 2) * 5] / (f5 + 2.0f);
                    if (b1->depth > bc->depth) f7 /= 2.0f;
                    f2 += f6 * f7;
                    f3 += f5 * f7;
                    f4 += f7;
                }
            f2 /= f4;
            f3 /= f4;
            f2 = f2 * 0.9f + 0.1f;
            f3 = (f3 * 4.0f - 1.0f) / 8.0f;
            float d0 = depth[i1] / 8000.0f;
            if (d0 < 0.0f) d0 = -d0 * 0.3f;
            d0 = d0 * 3.0f - 2.0f;
            if (d0 < 0.0f) {
                d0 /= 2.0f;
                if (d0 < -1.0f) d0 = -1.0f;
                d0 /= 1.4f;
                d0 /= 2.0f;
            } else {
                if (d0 > 1.0f) d0 = 1.0f;
                d0 /= 8.0f;
            }
            float d1 = f3;
            d1 += d0 * 0.2f;
            d1 = d1 * 8.5f / 8.0f;
            colD3[i1] = 8.5f + d1 * 4.0f;
            colD2[i1] = f2;
        }
    /* T->cls per grid point: D_SOLID / D_EMPTY when bounds decide the sign of the density,
     * D_NEED when its exact value is needed, then D_MIN / D_MAX for the limit noises the
     * clamped lerp uses. Bounds: each octave of a limit noise is below 2 in magnitude (gradient
     * dots of fractions, lerped), times its amplitude 2^o; the clamped lerp lies between the two
     * limits. First with no noise at all (+-256 after the / 512), then, for the points still
     * undecided, with the sums of the 6 lowest-frequency octaves (10..15) of both limits, which
     * leave +-2 * (2^10 - 1) / 512 < 4 for the rest (4.05 with room for float rounding). */
    enum { D_MIN = 1, D_MAX = 2, D_NEED = 4, D_UND = 8, D_SOLID = 16, D_EMPTY = 32 };
    uint8_t *cls = T->cls;
    for (int c = 0; c < 25; c++)
        for (int j2 = 0; j2 < 33; j2++) {
            float d4 = dens_offset(j2, colD3[c], colD2[c]);
            float lo = -256.5f - d4, hi = 256.5f - d4;
            if (j2 > 29) {
                float d9 = (float)(j2 - 29) / 3.0f;
                lo = lo * (1.0f - d9) + -10.0f * d9;
                hi = hi * (1.0f - d9) + -10.0f * d9;
            }
            cls[c * 33 + j2] = lo > 0.0f ? D_SOLID : (hi <= 0.0f ? D_EMPTY : D_UND);
        }
    memset(T->grid, 0, sizeof T->grid); /* min partial sums */
    memset(T->acc, 0, sizeof T->acc);   /* max partial sums */
    uint8_t rng[50];
    if (need_ranges(cls, D_UND, rng))
        for (int o = 10; o < 16; o++) {
            noise3_octave(&oc_min[o], o, bx, bz, SC_LIMIT, SC_LIMIT, T->grid, cls, D_UND, rng, 0);
            noise3_octave(&oc_max[o], o, bx, bz, SC_LIMIT, SC_LIMIT, T->acc, cls, D_UND, rng, 0);
        }
    for (int i = 0; i < 825; i++) {
        if (!(cls[i] & D_UND)) continue;
        int j2 = i % 33;
        float a = T->grid[i], b = T->acc[i];
        float d4 = dens_offset(j2, colD3[i / 33], colD2[i / 33]);
        float lo = (a < b ? a : b) / 512.0f - 4.05f - d4, hi = (a < b ? b : a) / 512.0f + 4.05f - d4;
        if (j2 > 29) {
            float d9 = (float)(j2 - 29) / 3.0f;
            lo = lo * (1.0f - d9) + -10.0f * d9;
            hi = hi * (1.0f - d9) + -10.0f * d9;
        }
        cls[i] = lo > 0.0f ? D_SOLID : (hi <= 0.0f ? D_EMPTY : 0);
    }
    /* a point needs noise if one of the cells around it has corners of different classes */
    for (int gx = 0; gx < 4; gx++)
        for (int gz = 0; gz < 4; gz++)
            for (int gy = 0; gy < 32; gy++) {
                int c0 = (gx * 5 + gz) * 33 + gy, c1 = (gx * 5 + gz + 1) * 33 + gy;
                int c2 = ((gx + 1) * 5 + gz) * 33 + gy, c3 = ((gx + 1) * 5 + gz + 1) * 33 + gy;
                int k = cls[c0] & (D_SOLID | D_EMPTY);
                if (k && (cls[c1] & (D_SOLID | D_EMPTY)) == k && (cls[c2] & (D_SOLID | D_EMPTY)) == k &&
                    (cls[c3] & (D_SOLID | D_EMPTY)) == k && (cls[c0 + 1] & (D_SOLID | D_EMPTY)) == k &&
                    (cls[c1 + 1] & (D_SOLID | D_EMPTY)) == k && (cls[c2 + 1] & (D_SOLID | D_EMPTY)) == k &&
                    (cls[c3 + 1] & (D_SOLID | D_EMPTY)) == k)
                    continue;
                cls[c0] |= D_NEED, cls[c1] |= D_NEED, cls[c2] |= D_NEED, cls[c3] |= D_NEED;
                cls[c0 + 1] |= D_NEED, cls[c1 + 1] |= D_NEED, cls[c2 + 1] |= D_NEED, cls[c3 + 1] |= D_NEED;
            }
    /* main noise (8 octaves) */
    memset(T->grid, 0, sizeof T->grid);
    if (need_ranges(cls, D_NEED, rng))
        for (int o = 0; o < 8; o++) noise3_octave(&oc_main[o], o, bx, bz, SC_MAINXZ, SC_MAINY, T->grid, cls, D_NEED, rng, 0);
    /* which limits the clamped lerp needs: min only (d7 < 0), max only (d7 > 1) or both */
    for (int i = 0; i < 825; i++) {
        if (!(cls[i] & D_NEED)) continue;
        float d7 = (T->grid[i] / 10.0f + 1.0f) / 2.0f;
        T->grid[i] = d7;
        cls[i] |= d7 < 0.0f ? D_MIN : (d7 > 1.0f ? D_MAX : D_MIN | D_MAX);
    }
    memset(T->acc, 0, sizeof T->acc);
    if (need_ranges(cls, D_MIN, rng))
        for (int o = 0; o < 16; o++) noise3_octave(&oc_min[o], o, bx, bz, SC_LIMIT, SC_LIMIT, T->acc, cls, D_MIN, rng, 0);
    for (int i = 0; i < 825; i++) {
        int k = cls[i] & (D_MIN | D_MAX);
        if (k == (D_MIN | D_MAX)) T->acc[i] *= 1.0f - T->grid[i];
        if (k == D_MAX) T->grid[i] = 1.0f;
    }
    if (need_ranges(cls, D_MAX, rng))
        for (int o = 0; o < 16; o++) noise3_octave(&oc_max[o], o, bx, bz, SC_LIMIT, SC_LIMIT, T->acc, cls, D_MAX, rng, T->grid);
    /* density (decided points get +-1: only their sign matters) */
    for (int i = 0; i < 825; i++) {
        int j2 = i % 33;
        if (!(cls[i] & D_NEED)) {
            T->grid[i] = cls[i] & D_SOLID ? 1.0f : -1.0f;
            continue;
        }
        float d8 = T->acc[i] / 512.0f - dens_offset(j2, colD3[i / 33], colD2[i / 33]);
        if (j2 > 29) {
            float d9 = (float)(j2 - 29) / 3.0f;
            d8 = d8 * (1.0f - d9) + -10.0f * d9;
        }
        T->grid[i] = d8;
    }
}

/* surface depth noise (NoiseGenerator3, 4 octaves) for chunk -> U.t.surf[z*16+x] */
static void chunk_surface_noise(int cx, int cz) {
    TerrainScratch *T = &U.t;
    i64 off[3];
    memset(T->surf, 0, sizeof T->surf);
    for (int o = 0; o < 4; o++) {
        perm_build(oc_surf[o].st, T->perm, off);
        /* coordinate = (X + i) * 0.0625 * 2^-o + offset */
        int sh = 4 + o;
        float amp = 0.55f * (float)(1 << o);
        /* offsets b (x) = off[0], c (z) = off[1] in Q32 */
        i32 bxi = (i32)(off[0] >> 32), bzi = (i32)(off[1] >> 32);
        float bxf = (float)(u32)off[0] * (1.0f / 4294967296.0f), bzf = (float)(u32)off[1] * (1.0f / 4294967296.0f);
        float scale = 1.0f / (float)(1 << sh);
        for (int j = 0; j < 16; j++) {
            int Z = cz * 16 + j;
            i32 zi = (Z >> sh) + bzi;
            float zf = (float)(Z & ((1 << sh) - 1)) * scale + bzf;
            for (int i = 0; i < 16; i++) {
                int X = cx * 16 + i;
                i32 xi = (X >> sh) + bxi;
                float xf = (float)(X & ((1 << sh) - 1)) * scale + bxf;
                T->surf[j * 16 + i] += simplex2(T->perm, xi, xf, zi, zf) * amp;
            }
        }
    }
}

/* ---- surface builders (BiomeBase.b and the overrides) ---- */

/* Column summary of the undecorated, cave-less terrain, in 2 bytes. Below the sea a column
 * ends in water, so a column whose highest block is solid has it at y >= 62, and a column
 * whose highest block is liquid (water, ice, or water under a lily pad) has it at y = 62
 * with its floor (highest solid block below) at y <= 61. Hence:
 *   y >= 62: solid top at y; c = top block code << 4 | filler depth (non-stone blocks below)
 *   y <= 61: liquid top at 62, floor at y; c = liquid kind */
typedef struct {
    uint8_t y, c;
} ColSum;
static const uint8_t SUM_TOPS[16] = {B_STONE, B_GRASS, B_COARSE_DIRT, B_PODZOL, B_SAND, B_RED_SAND, B_GRAVEL, B_SNOW,
                                     B_MYCELIUM, B_HARDENED_CLAY, B_STAINED_CLAY_WHITE, B_STAINED_CLAY_ORANGE,
                                     B_STAINED_CLAY_YELLOW, B_STAINED_CLAY_BROWN, B_STAINED_CLAY_RED,
                                     B_STAINED_CLAY_SILVER};
static const uint8_t SUM_LIQ[4] = {B_WATER, B_ICE, B_LILY_PAD, B_LAVA};
static unsigned sum_odd; /* columns the encoding could not hold exactly (tests expect 0) */

INLINE int cs_top(const ColSum *s) { return s->y < 62 ? 62 : s->y; }
/* the top block; B_LILY_PAD means water at the top with a lily pad above */
INLINE uint8_t cs_blk(const ColSum *s) { return s->y < 62 ? SUM_LIQ[s->c & 3] : SUM_TOPS[s->c >> 4]; }

/* one step of BiomeBase.b's loop at height y (no bedrock); returns 1 if it needs a sandstone
 * draw (then the caller draws and calls sand_after) */
typedef struct {
    uint8_t top0, fill0, top, fill;
    int l, i1;
    float temp;
} SurfState;

INLINE int surf_step(SurfState *s, uint8_t *col, int y) {
    uint8_t blk = col[y];
    if (blk == B_AIR) {
        s->l = -1;
        return 0;
    }
    if (blk != B_STONE) return 0;
    if (s->l == -1) {
        if (s->i1 <= 0) {
            s->top = B_AIR;
            s->fill = B_STONE;
        } else if (y >= SEA - 4 && y <= SEA + 1) {
            s->top = s->top0;
            s->fill = s->fill0;
        }
        if (y < SEA && s->top == B_AIR) s->top = s->temp < 0.15f ? B_ICE : B_WATER;
        s->l = s->i1;
        if (y >= SEA - 1) {
            col[y] = s->top;
        } else if (y < SEA - 7 - s->i1) {
            s->top = B_AIR;
            s->fill = B_STONE;
            col[y] = B_GRAVEL;
        } else {
            col[y] = s->fill;
        }
    } else if (s->l > 0) {
        --s->l;
        col[y] = s->fill;
        if (s->l == 0 && (s->fill == B_SAND || s->fill == B_RED_SAND)) return 1;
    }
    return 0;
}

/* highest non-air y of the column being built (column_density's result) */
static int col_top;

HOT static void build_default(uint8_t *col, const Biome *b, JRand *r, float noise, uint8_t top0, uint8_t fill0) {
    SurfState s;
    s.top0 = s.top = top0;
    s.fill0 = s.fill = fill0;
    s.l = -1;
    s.temp = b->temp;
    s.i1 = (int)(noise / 3.0f + 3.0f + jr_doublef(r) * 0.25f);
    /* every y > 4 draws one bedrock nextInt(5) that is not used: count them, jump over them
     * before a draw that matters. Above the top all is air (nothing to do); in stone with no
     * filler left and in water, nothing happens either. */
    int y = col_top > 4 ? col_top : 4;
    unsigned skip = (unsigned)(255 - y);
    for (; y >= 0; y--) {
        if (y <= 4) {
            jr_skip(r, skip);
            skip = 0;
            if (y <= jr_int_i(r, 5)) {
                col[y] = B_BEDROCK;
                continue;
            }
        } else {
            skip++;
            uint8_t blk = col[y];
            if ((blk == B_STONE && s.l == 0) || blk == B_WATER) {
                int y2 = y - 1;
                /* 4 blocks at a time (col is word aligned) */
                u32 w4 = blk * 0x01010101u, word;
                while (y2 > 7 && (y2 & 3) != 3 && col[y2] == blk) y2--;
                if ((y2 & 3) == 3)
                    while (y2 > 7 && (memcpy(&word, col + y2 - 3, 4), word == w4)) y2 -= 4;
                while (y2 > 4 && col[y2] == blk) y2--;
                skip += (unsigned)(y - 1 - y2);
                y = y2 + 1;
                continue;
            }
        }
        if (surf_step(&s, col, y)) {
            jr_skip(r, skip);
            skip = 0;
            s.l = jr_int_i(r, 4) + (y - SEA > 0 ? y - SEA : 0);
            s.fill = s.fill == B_RED_SAND ? B_RED_SANDSTONE : B_SANDSTONE;
        }
    }
}

/* BiomeMesa band at (x, y): aD[(y + round(aH(x/512, x/512) * 2) + 64) % 64] */
static uint8_t mesa_band(int x, int y) {
    const uint8_t *P = U.t.mperm[0];
    float xf = (float)(x & 511) / 512.0f;
    float v = simplex2(P, x >> 9, xf, x >> 9, xf) * 2.0f;
    int l = (int)floor_f(v + 0.5f);
    return mesa_bands[(y + l + 64) % 64];
}

static void mesa_prepare(void) {
    if (U.t.mesa_ready) return;
    for (int i = 0; i < 6; i++) perm_build(oc_mesa[i].st, U.t.mperm[i], 0);
    U.t.mesa_ready = 1;
}

/* NoiseGenerator3.a(x, z) (single point, no offsets) over n octaves with perms P[o] */
static float simplex_oct_point(int n, uint8_t (*P)[256], i32 xi, float xf, i32 zi, float zf, int shift0) {
    /* coordinates (xi + xf) * 2^-shift0 * 2^-o */
    float sum = 0;
    for (int o = 0; o < n; o++) {
        int sh = shift0 + o;
        /* split (xi + xf) / 2^sh into integer + fraction exactly enough */
        i32 xa = xi >> sh, za = zi >> sh;
        float xr = ((float)(xi & ((1 << sh) - 1)) + xf) / (float)(1 << sh);
        float zr = ((float)(zi & ((1 << sh) - 1)) + zf) / (float)(1 << sh);
        sum += simplex2(P[o], xa, xr, za, zr) * (float)(1 << o);
    }
    return sum;
}

static void build_mesa(uint8_t *col, const Biome *b, JRand *r, int bx, int bz, float noise) {
    mesa_prepare();
    float d1 = 0.0f;
    if (b->mode & 1) { /* bryce */
        int k = (bx & -16) + (bz & 15), l = (bz & -16) + (bx & 15);
        /* aF: 4 octaves at (k*0.25, l*0.25): perms mperm[0..3] (aF[0] == aH) */
        uint8_t(*P)[256] = U.t.mperm;
        float d2 = simplex_oct_point(4, P, k, 0.0f, l, 0.0f, 2);
        float an = noise < 0 ? -noise : noise;
        if (an < d2) d2 = an;
        if (d2 > 0.0f) {
            float d4 = simplex_oct_point(1, &U.t.mperm[4], k, 0.0f, l, 0.0f, 9);
            if (d4 < 0) d4 = -d4;
            d1 = d2 * d2 * 2.5f;
            float c = d4 * 50.0f;
            int ci = (int)c;
            float d5 = (float)(c > (float)ci ? ci + 1 : ci) + 14.0f;
            if (d1 > d5) d1 = d5;
            d1 += 64.0f;
        }
    }
    int d1i = (int)d1;
    uint8_t top = B_STAINED_CLAY_WHITE, fill = b->filler;
    int j1 = (int)(noise / 3.0f + 3.0f + jr_doublef(r) * 0.25f);
    /* Math.cos(d0 / 3.0 * pi) > 0 */
    float ph = noise / 3.0f;
    float m = ph - 2.0f * (float)floor_f(ph * 0.5f); /* in [0, 2) */
    int flag = (m < 0.5f || m > 1.5f);
    int k1 = -1, flag1 = 0;
    unsigned skip = 0;
    for (int y = 255; y >= 0; y--) {
        if (col[y] == B_AIR && y < d1i) col[y] = B_STONE;
        if (y <= 4) {
            jr_skip(r, skip);
            skip = 0;
            if (y <= jr_int(r, 5)) {
                col[y] = B_BEDROCK;
                continue;
            }
        } else {
            skip++;
        }
        uint8_t blk = col[y];
        if (blk == B_AIR) {
            k1 = -1;
        } else if (blk == B_STONE) {
            if (k1 == -1) {
                flag1 = 0;
                if (j1 <= 0) {
                    top = B_AIR;
                    fill = B_STONE;
                } else if (y >= SEA - 4 && y <= SEA + 1) {
                    top = B_STAINED_CLAY_WHITE;
                    fill = b->filler;
                }
                if (y < SEA && top == B_AIR) top = B_WATER;
                k1 = j1 + (y - SEA > 0 ? y - SEA : 0);
                if (y >= SEA - 1) {
                    if ((b->mode & 2) && y > 86 + j1 * 2) {
                        col[y] = flag ? B_COARSE_DIRT : B_GRASS;
                    } else if (y > SEA + 3 + j1) {
                        col[y] = (y >= 64 && y <= 127) ? (flag ? B_HARDENED_CLAY : mesa_band(bx, y)) : B_STAINED_CLAY_ORANGE;
                    } else {
                        col[y] = b->top;
                        flag1 = 1;
                    }
                } else {
                    col[y] = fill == B_STAINED_CLAY_WHITE ? B_STAINED_CLAY_ORANGE : fill;
                }
            } else if (k1 > 0) {
                --k1;
                col[y] = flag1 ? B_STAINED_CLAY_ORANGE : mesa_band(bx, y);
            }
        }
    }
    (void)top;
}

static void build_surface(uint8_t *col, const Biome *b, JRand *r, int bx, int bz, float noise) {
    uint8_t top = b->top, fill = b->filler;
    switch (b->surf) {
    case S_HILLS:
        top = B_GRASS;
        fill = B_DIRT;
        if ((noise < -1.0f || noise > 2.0f) && b->mode == 2) top = fill = B_GRAVEL;
        else if (noise > 1.0f && b->mode != 1) top = fill = B_STONE;
        break;
    case S_TAIGA_MEGA:
        top = B_GRASS;
        fill = B_DIRT;
        if (noise > 1.75f) top = B_COARSE_DIRT;
        else if (noise > -0.95f) top = B_PODZOL;
        break;
    case S_SAVANNA_M:
        top = B_GRASS;
        fill = B_DIRT;
        if (noise > 1.75f) top = fill = B_STONE;
        else if (noise > -0.5f) top = B_COARSE_DIRT;
        break;
    case S_SWAMP: {
        /* Java: d1 = af.a(x * 0.25, z * 0.25) > 0, then < 0.12 for a lily pad. In float, the
         * value is recomputed in double when it is too close to either threshold; for a float,
         * "< 0.12" (the double) is "<= 0.12f". */
        float f1 = simplex2(perm_grass, bx >> 2, (float)(bx & 3) * 0.25f, bz >> 2, (float)(bz & 3) * 0.25f);
        int pos = f1 > 0.0f, lily = f1 <= 0.12f;
        if ((f1 > -1e-5f && f1 < 1e-5f) || (f1 > 0.12f - 1e-5f && f1 < 0.12f + 1e-5f)) {
            double d1 = simplex2_d(perm_grass, (double)bx / 4, (double)bz / 4);
            pos = d1 > 0;
            lily = d1 < (double)12 / 100; /* 0.12 */
        }
        if (pos) {
            for (int y = col_top; y >= 0; y--)
                if (col[y] != B_AIR) {
                    if (y == 62 && col[y] != B_WATER) {
                        col[y] = B_WATER;
                        if (lily) col[y + 1] = B_LILY_PAD;
                    }
                    break;
                }
        }
        break;
    }
    case S_MESA:
        build_mesa(col, b, r, bx, bz, noise);
        col_top = 255; /* bryce pillars rise above the terrain */
        return;
    }
    build_default(col, b, r, noise, top, fill);
}

/* ---- per-chunk terrain pipeline ---- */

INLINE int is_liquid_top(int blk) { return blk == B_WATER || blk == B_ICE || blk == B_LILY_PAD || blk == B_LAVA; }

/* column summary of the undecorated, cave-less terrain (see ColSum) */
HOT static void column_summary(const uint8_t *col, ColSum *s) {
    int y = col_top + 2 < 255 ? col_top + 2 : 255; /* builders add at most a lily pad above it */
    while (y > 0 && col[y] == B_AIR) y--;
    int lily = 0;
    if (col[y] == B_LILY_PAD) {
        lily = 1;
        y--;
    }
    uint8_t blk = lily ? B_LILY_PAD : col[y];
    if (is_liquid_top(blk)) {
        int f = y;
        while (f > 0 && (col[f] == B_WATER || col[f] == B_ICE || col[f] == B_LAVA || col[f] == B_AIR)) f--;
        int k = blk == B_WATER ? 0 : blk == B_ICE ? 1 : blk == B_LILY_PAD ? 2 : 3;
        if (y != 62 || f > 61) {
            sum_odd++;
            if (f > 61) f = 61;
        }
        s->y = (uint8_t)f;
        s->c = (uint8_t)k;
    } else {
        int d = 0;
        for (int k = y - 1; k > 0 && d < 15; k--, d++) {
            uint8_t b = col[k];
            if (b == B_STONE || b == B_AIR || b == B_BEDROCK || b == B_WATER) break;
        }
        int code = 0;
        while (code < 16 && SUM_TOPS[code] != blk) code++;
        if (code == 16 || y < 62) {
            sum_odd++;
            if (code == 16) code = 0;
            if (y < 62) y = 62;
        }
        s->y = (uint8_t)y;
        s->c = (uint8_t)(code << 4 | d);
    }
}

/* Sign classes of the density grid for column_density: for each of the 4 x 4 grid cells and
 * each level, 1 if its four corners are > 0, 2 if they are all <= 0, else 0 (kept in U.t.cls,
 * free after chunk_density). Bilinear values between corners of one sign keep it. */
static void column_signs(void) {
    const float *g = U.t.grid;
    uint8_t *sg = U.t.cls;
    for (int gx = 0; gx < 4; gx++)
        for (int gz = 0; gz < 4; gz++) {
            const float *c00 = g + (gx * 5 + gz) * 33, *c01 = c00 + 33, *c10 = c00 + 165, *c11 = c00 + 198;
            uint8_t *o = sg + (gx * 4 + gz) * 33;
            for (int k = 0; k < 33; k++) {
                int pos = (c00[k] > 0.0f) + (c01[k] > 0.0f) + (c10[k] > 0.0f) + (c11[k] > 0.0f);
                o[k] = (uint8_t)(pos == 4 ? 1 : pos == 0 ? 2 : 0);
            }
        }
}

/* Blocks of column (x, z) from the density grid: stone, water below the sea, air. Cells whose
 * two ends have the same sign are filled at once (the interpolated values between them keep
 * it: their distance to 0 is at least an eighth of an end's, far above rounding); levels are
 * interpolated only for the other cells. Returns the highest non-air y. */
HOT static int column_density(int x, int z, uint8_t *col) {
    const float *g = U.t.grid;
    int gx = x >> 2, gz = z >> 2;
    float fx = (float)(x & 3) * 0.25f, fz = (float)(z & 3) * 0.25f;
    const float *c00 = g + (gx * 5 + gz) * 33, *c01 = g + (gx * 5 + gz + 1) * 33;
    const float *c10 = g + ((gx + 1) * 5 + gz) * 33, *c11 = g + ((gx + 1) * 5 + gz + 1) * 33;
    const uint8_t *sg = U.t.cls + (gx * 4 + gz) * 33;
    float v0 = 0.0f, v1;
    int have = -1; /* level whose value v0 holds */
    int top = 0;
    for (int k2 = 0; k2 < 32; k2++) {
        int y = k2 * 8;
        if (sg[k2] == 1 && sg[k2 + 1] == 1) {
            memset(col + y, B_STONE, 8);
            top = y + 7;
            continue;
        }
        if (sg[k2] == 2 && sg[k2 + 1] == 2) {
            if (y >= SEA) {
                memset(col + y, B_AIR, 8);
                continue;
            }
            if (y + 8 <= SEA) {
                memset(col + y, B_WATER, 8);
                top = y + 7;
                continue;
            }
            for (int l2 = 0; l2 < 8; l2++, y++) col[y] = y < SEA ? B_WATER : B_AIR;
            top = SEA - 1;
            continue;
        }
        if (have != k2) {
            float a = c00[k2] + (c10[k2] - c00[k2]) * fx;
            float b = c01[k2] + (c11[k2] - c01[k2]) * fx;
            v0 = a + (b - a) * fz;
        }
        {
            int k = k2 + 1;
            float a = c00[k] + (c10[k] - c00[k]) * fx;
            float b = c01[k] + (c11[k] - c01[k]) * fx;
            v1 = a + (b - a) * fz;
        }
        float base = v0, step = (v1 - v0) * 0.125f;
        v0 = v1;
        have = k2 + 1;
        if (base > 0.0f && v1 > 0.0f) {
            memset(col + y, B_STONE, 8);
            top = y + 7;
            continue;
        }
        if (base <= 0.0f && v1 <= 0.0f) {
            if (y >= SEA) {
                memset(col + y, B_AIR, 8);
                continue;
            }
            if (y + 8 <= SEA) {
                memset(col + y, B_WATER, 8);
                top = y + 7;
                continue;
            }
        }
        for (int l2 = 0; l2 < 8; l2++, y++) {
            float val = base + (float)l2 * step;
            col[y] = val > 0.0f ? B_STONE : (y < SEA ? B_WATER : B_AIR);
            if (col[y] != B_AIR) top = y;
        }
    }
    return top;
}

/* Terrain and surface of chunk (cx, cz) before caves. Rows y0..y0+h-1 go to out (if out),
 * the column summaries to sum (if sum). Also leaves the biomes in cb4/cb16. */
HOT static void chunk_terrain(int cx, int cz, uint8_t *out, int y0, int h, ColSum *sum) {
    chunk_biomes(cx, cz);
    U.t.mesa_ready = 0;
    chunk_density(cx, cz);
    column_signs();
    chunk_surface_noise(cx, cz);
    JRand r;
    jr_seed(&r, (i64)((u64)(i64)cx * 341873128712ULL + (u64)(i64)cz * 132897987541ULL));
    uint8_t *col = U.t.col;
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            col_top = column_density(x, z, col);
            const Biome *b = bio(cb16[x + z * 16]);
            build_surface(col, b, &r, cx * 16 + z, cz * 16 + x, U.t.surf[z * 16 + x]);
            if (sum) column_summary(col, &sum[x + z * 16]);
            if (out)
                for (int y = 0; y < h; y++) out[y * 256 + z * 16 + x] = col[y0 + y];
        }
}

/* ======================================================================== */
/* Block properties                                                          */
/* ======================================================================== */

/* flags per block id */
enum {
    BF_OPAQUE = 1,    /* light opacity != 0: counts in the height map */
    BF_SOLID = 2,     /* Material.isSolid() */
    BF_BUILD = 4,     /* Material.isBuildable() */
    BF_LEAVES = 8,
    BF_LOG = 16,
    BF_PLANT = 32,    /* REPLACEABLE_PLANT or PLANT material (flowers, grass, saplings...) */
    BF_CUBE = 64,     /* Block.o(): opaque full cube (leaves included on the server) */
    BF_LIQUID = 128,
    BF_RPLANT = 256   /* Material.REPLACEABLE_PLANT (tall grass, ferns, double plants, vines, dead bush) */
};
static uint16_t bflags[256];

static void bflags_init(void) {
    for (int i = 0; i < 256; i++) bflags[i] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_CUBE;
    bflags[B_AIR] = 0;
    bflags[B_WATER] = bflags[B_FLOWING_WATER] = BF_OPAQUE | BF_LIQUID;
    bflags[B_LAVA] = bflags[B_FLOWING_LAVA] = BF_LIQUID;
    static const uint8_t leaves[] = {B_LEAVES_OAK, B_LEAVES_SPRUCE, B_LEAVES_BIRCH, B_LEAVES_JUNGLE, B_LEAVES_ACACIA,
                                     B_LEAVES_DARK_OAK};
    for (unsigned i = 0; i < sizeof leaves; i++) bflags[leaves[i]] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_LEAVES | BF_CUBE;
    static const uint8_t logs[] = {B_LOG_OAK, B_LOG_OAK_X, B_LOG_OAK_Z, B_LOG_OAK_BARK, B_LOG_SPRUCE, B_LOG_SPRUCE_X,
                                   B_LOG_SPRUCE_Z, B_LOG_SPRUCE_BARK, B_LOG_BIRCH, B_LOG_BIRCH_X, B_LOG_BIRCH_Z,
                                   B_LOG_BIRCH_BARK, B_LOG_JUNGLE, B_LOG_JUNGLE_X, B_LOG_JUNGLE_Z, B_LOG_JUNGLE_BARK,
                                   B_LOG_ACACIA, B_LOG_ACACIA_X, B_LOG_ACACIA_Z, B_LOG_ACACIA_BARK, B_LOG_DARK_OAK,
                                   B_LOG_DARK_OAK_X, B_LOG_DARK_OAK_Z, B_LOG_DARK_OAK_BARK};
    for (unsigned i = 0; i < sizeof logs; i++) bflags[logs[i]] |= BF_LOG;
    static const uint8_t plants[] = {B_SAPLING_OAK, B_SAPLING_SPRUCE, B_SAPLING_BIRCH, B_SAPLING_JUNGLE,
                                     B_SAPLING_ACACIA, B_SAPLING_DARK_OAK, B_DEAD_SHRUB, B_TALL_GRASS, B_FERN,
                                     B_DEAD_BUSH, B_DANDELION, B_POPPY, B_BLUE_ORCHID, B_ALLIUM, B_AZURE_BLUET,
                                     B_RED_TULIP, B_ORANGE_TULIP, B_WHITE_TULIP, B_PINK_TULIP, B_OXEYE_DAISY,
                                     B_BROWN_MUSHROOM, B_RED_MUSHROOM, B_SUGAR_CANE, B_VINE, B_LILY_PAD,
                                     B_SUNFLOWER_LOWER, B_SUNFLOWER_UPPER, B_LILAC_LOWER, B_LILAC_UPPER,
                                     B_DOUBLE_GRASS_LOWER, B_DOUBLE_GRASS_UPPER, B_LARGE_FERN_LOWER,
                                     B_LARGE_FERN_UPPER, B_ROSE_BUSH_LOWER, B_ROSE_BUSH_UPPER, B_PEONY_LOWER,
                                     B_PEONY_UPPER};
    for (unsigned i = 0; i < sizeof plants; i++) bflags[plants[i]] = BF_PLANT;
    static const uint8_t rplants[] = {B_DEAD_SHRUB, B_TALL_GRASS, B_FERN, B_DEAD_BUSH, B_VINE, B_SUNFLOWER_LOWER,
                                      B_SUNFLOWER_UPPER, B_LILAC_LOWER, B_LILAC_UPPER, B_DOUBLE_GRASS_LOWER,
                                      B_DOUBLE_GRASS_UPPER, B_LARGE_FERN_LOWER, B_LARGE_FERN_UPPER, B_ROSE_BUSH_LOWER,
                                      B_ROSE_BUSH_UPPER, B_PEONY_LOWER, B_PEONY_UPPER};
    for (unsigned i = 0; i < sizeof rplants; i++) bflags[rplants[i]] |= BF_RPLANT;
    bflags[B_SNOW_LAYER] = 0; /* not solid, opacity 0 */
    bflags[B_ICE] = BF_OPAQUE | BF_SOLID | BF_BUILD;
    bflags[B_PACKED_ICE] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_CUBE;
    bflags[B_CACTUS] = BF_SOLID | BF_BUILD;
    bflags[B_COBWEB] = BF_OPAQUE;
    bflags[B_MOB_SPAWNER] = BF_SOLID | BF_BUILD;
    bflags[B_CHEST] = BF_SOLID | BF_BUILD;
    bflags[B_GLASS] = BF_SOLID | BF_BUILD;
    bflags[B_PUMPKIN] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_CUBE;
    bflags[B_SANDSTONE_SLAB] = BF_SOLID | BF_BUILD;
    bflags[B_STONE_SLAB] = BF_SOLID | BF_BUILD;
}

#define IS_AIR(b) ((b) == B_AIR)
#define IS_LEAVES(b) (bflags[b] & BF_LEAVES)
#define IS_LOG(b) (bflags[b] & BF_LOG)
#define IS_SOLID(b) (bflags[b] & BF_SOLID)
#define IS_BUILD(b) (bflags[b] & BF_BUILD)
#define IS_OPAQUE(b) (bflags[b] & BF_OPAQUE)
#define IS_CUBE(b) (bflags[b] & BF_CUBE)
#define IS_PLANT(b) (bflags[b] & BF_PLANT)
#define IS_RPLANT(b) (bflags[b] & BF_RPLANT)
#define IS_WATER(b) ((b) == B_WATER || (b) == B_FLOWING_WATER)
#define IS_DIRTISH(b) ((b) == B_DIRT || (b) == B_COARSE_DIRT || (b) == B_PODZOL)
#define IS_CLAY(b) ((b) == B_HARDENED_CLAY || ((b) >= B_STAINED_CLAY_WHITE && (b) <= B_STAINED_CLAY_BLACK))

/* filler below a top block (for summary-based reads) */
static uint8_t filler_of(uint8_t top) {
    switch (top) {
    case B_SAND: return B_SAND;
    case B_RED_SAND: return B_STAINED_CLAY_ORANGE;
    case B_GRAVEL: return B_GRAVEL;
    case B_STONE: return B_STONE;
    default:
        if (IS_CLAY(top)) return B_STAINED_CLAY_ORANGE;
        return B_DIRT;
    }
}

/* block of a column from its summary (no caves, no decoration) */
HOT static uint8_t sum_block(const ColSum *s, int y) {
    if (y < 0) return B_BEDROCK;
    if (s->y < 62) { /* liquid top at 62, floor at s->y */
        uint8_t blk = SUM_LIQ[s->c & 3];
        if (y > 62) return y == 63 && blk == B_LILY_PAD ? B_LILY_PAD : B_AIR;
        if (y == 0) return B_BEDROCK;
        if (y == 62) return blk == B_LILY_PAD ? B_WATER : blk;
        if (y > s->y) return blk == B_LAVA ? B_LAVA : B_WATER;
        if (y == s->y) return B_SAND; /* floor (sand, gravel, dirt or clay: all buildable) */
        return B_STONE;
    }
    if (y > s->y) return B_AIR;
    if (y == 0) return B_BEDROCK;
    uint8_t blk = SUM_TOPS[s->c >> 4];
    if (y == s->y) return blk;
    if (y >= s->y - (s->c & 15)) return filler_of(blk);
    return B_STONE;
}

/* ======================================================================== */
/* Caves and ravines (WorldGenCaves, WorldGenCanyon)                         */
/* ======================================================================== */

/* A carve target: one chunk (tcx, tcz) and, inside it, the region we care about.
 * mode CAVE_OUT: the chunk being generated (blocks in out / its summary).
 * mode CAVE_MASK: a query box (world coords) whose carved blocks are marked in a bitmap;
 *   blocks come from the summary of the target chunk. */
enum { CAVE_OUT, CAVE_MASK };

typedef struct {
    int mode;
    int tcx, tcz;
    const ColSum *sum;  /* summary of the target chunk */
    /* CAVE_OUT */
    uint8_t *out;
    int y0, h;
    /* CAVE_MASK: box [bx0,bx1) x [by0,by1) x [bz0,bz1) in world coords, bitmap of carved cells */
    int bx0, bx1, by0, by1, bz0, bz1;
    uint8_t *mask;
    /* region of the chunk that matters (chunk-local, inclusive-exclusive), for early skips */
    int rx0, rx1, rz0, rz1, ry0, ry1;
} CaveCtx;

static CaveCtx *cv;
static uint8_t c_b16[256];  /* biomes of the chunk being generated */

INLINE int mask_index(int X, int Y, int Z) {
    return ((Y - cv->by0) * (cv->bz1 - cv->bz0) + (Z - cv->bz0)) * (cv->bx1 - cv->bx0) + (X - cv->bx0);
}

/* block at chunk-local (x, y, z) of the target, as the carver sees it */
HOT static uint8_t cv_get(int x, int y, int z) {
    if (y < 0 || y > 255) return B_AIR;
    if (cv->mode == CAVE_OUT) {
        if (y >= cv->y0 && y < cv->y0 + cv->h) return cv->out[(y - cv->y0) * 256 + z * 16 + x];
        return sum_block(&cv->sum[x + z * 16], y);
    }
    int X = cv->tcx * 16 + x, Z = cv->tcz * 16 + z;
    if (X >= cv->bx0 && X < cv->bx1 && y >= cv->by0 && y < cv->by1 && Z >= cv->bz0 && Z < cv->bz1) {
        uint8_t m = cv->mask[mask_index(X, y, Z)];
        if (m) return (uint8_t)(m - 1); /* mask stores block id + 1, 0 = untouched */
    }
    return sum_block(&cv->sum[x + z * 16], y);
}

/* chunk being generated: highest non-air y of each column (whole height, after caves and
 * decoration so far) and highest solid-or-liquid y (precipitation height - 1) */
static uint8_t c_top[256], c_sl[256];

static int cave_track_on;
static void cave_track(int x, int y, int z, uint8_t b);
/* carving the chunk being generated: the lowest row that can still matter (the slab's, or a
 * column's tracked top / surface, which only go down), checked before walking a tunnel */
static int crep_dyn, crep_lo;

HOT static void cv_set(int x, int y, int z, uint8_t b) {
    if (y < 0 || y > 255) return;
    if (cv->mode == CAVE_OUT) {
        if (y >= cv->y0 && y < cv->y0 + cv->h) cv->out[(y - cv->y0) * 256 + z * 16 + x] = b;
        if (cave_track_on) cave_track(x, y, z, b);
        return;
    }
    int X = cv->tcx * 16 + x, Z = cv->tcz * 16 + z;
    if (X >= cv->bx0 && X < cv->bx1 && y >= cv->by0 && y < cv->by1 && Z >= cv->bz0 && Z < cv->bz1)
        cv->mask[mask_index(X, y, Z)] = (uint8_t)(b + 1);
}

/* position in 32.32 fixed point (world coordinates) */
typedef i64 Q32;
#define Q32_ONE (1LL << 32)
INLINE Q32 q32_of_float(float v) { return f2i64_scaled(v, 32); }
INLINE float q32_to_float(Q32 v) { return i64_to_f_scaled(v, 32); }
INLINE int q32_floor(Q32 v) { return (int)(v >> 32); }

/* One block of a carve step: the body of WorldGenCaves / WorldGenCanyon's innermost loop at
 * chunk-local (x, y, z), with h2 the horizontal part of the ellipsoid. Returns flag3 (a grass
 * block was met above in this column). */
INLINE int carve_block(int x, int y, int z, float h2, Q32 py, float d7, int canyon, int flag3) {
    float d14 = q32_to_float(SHL(y - 1, 32) + (Q32_ONE >> 1) - py) / d7;
    if (canyon) {
        if (!(h2 * canyon_w[y - 1] + d14 * d14 / 6.0f < 1.0f)) return flag3;
        uint8_t b = cv_get(x, y, z);
        if (b == B_GRASS) flag3 = 1;
        if (b == B_STONE || IS_DIRTISH(b) || b == B_GRASS) {
            if (y - 1 < 10) {
                cv_set(x, y, z, B_LAVA);
            } else {
                cv_set(x, y, z, B_AIR);
                if (flag3 && IS_DIRTISH(cv_get(x, y - 1, z)))
                    cv_set(x, y - 1, z, bio(cv->mode == CAVE_OUT ? c_b16[x + z * 16] : BI_PLAINS)->top);
            }
        }
        return flag3;
    }
    if (!(d14 > -0.7f && h2 + d14 * d14 < 1.0f)) return flag3;
    uint8_t b = cv_get(x, y, z), up = cv_get(x, y + 1, z);
    if (b == B_GRASS || b == B_MYCELIUM) flag3 = 1;
    int carvable = b == B_STONE || IS_DIRTISH(b) || b == B_GRASS || IS_CLAY(b) || b == B_SANDSTONE ||
                   b == B_RED_SANDSTONE || b == B_MYCELIUM || b == B_SNOW_LAYER ||
                   ((b == B_SAND || b == B_RED_SAND || b == B_GRAVEL) && !IS_WATER(up));
    if (!carvable) return flag3;
    if (y - 1 < 10) {
        cv_set(x, y, z, B_LAVA);
    } else {
        cv_set(x, y, z, B_AIR);
        if (up == B_SAND) cv_set(x, y + 1, z, B_SANDSTONE);
        else if (up == B_RED_SAND) cv_set(x, y + 1, z, B_RED_SANDSTONE);
        if (flag3 && IS_DIRTISH(cv_get(x, y - 1, z))) {
            uint8_t t = bio(cv->mode == CAVE_OUT ? c_b16[x + z * 16] : BI_PLAINS)->top;
            if (t == B_PODZOL || t == B_COARSE_DIRT) t = B_DIRT; /* ak.getBlock().getBlockData() */
            cv_set(x, y - 1, z, t);
        }
    }
    return flag3;
}

/* Is there water among blocks y of column (x, z) of the target chunk, for y in [a, b] (an
 * empty range when a > b)? Caves add no water and never carve it, so this is the terrain's:
 * in the slab of the chunk being generated from its blocks, elsewhere from the summary. */
static int water_col(int x, int z, int a, int b) {
    if (a < 0) a = 0;
    if (b > 255) b = 255;
    if (a > b) return 0;
    const ColSum *s = &cv->sum[x + z * 16];
    int wlo = 256, whi = -1; /* the summary's water: (floor, 62], or (floor, 61] under ice; none */
    if (s->y < 62 && (s->c & 3) != 3) {
        wlo = s->y + 1;
        whi = (s->c & 3) == 1 ? 61 : 62;
    }
    if (cv->mode != CAVE_OUT) return a <= whi && b >= wlo;
    int s0 = cv->y0, s1 = cv->y0 + cv->h - 1;
    /* below and above the slab */
    int lo = a, hi = b < s0 - 1 ? b : s0 - 1;
    if (lo <= hi && hi >= wlo && lo <= whi) return 1;
    lo = a > s1 + 1 ? a : s1 + 1, hi = b;
    if (lo <= hi && hi >= wlo && lo <= whi) return 1;
    /* in the slab */
    lo = a > s0 ? a : s0, hi = b < s1 ? b : s1;
    for (int y = lo; y <= hi; y++) {
        uint8_t blk = cv->out[(y - s0) * 256 + z * 16 + x];
        if (blk == B_WATER || blk == B_FLOWING_WATER) return 1;
    }
    return 0;
}

/* One carve step of a tunnel (cave or canyon) into the target chunk.
 * px,py,pz: centre; r6, r7: horizontal and vertical radii (Q32). Returns 0 when water aborts
 * the step (only rooms care: Java's room stops after its first unaborted step). */
HOT static int cave_carve(Q32 px, Q32 py, Q32 pz, Q32 r6, Q32 r7, int canyon, int room) {
    int j = cv->tcx, k = cv->tcz;
    int l1 = q32_floor(px - r6) - j * 16 - 1, i2 = q32_floor(px + r6) - j * 16 + 1;
    int j2 = q32_floor(py - r7) - 1, k2 = q32_floor(py + r7) + 1;
    int l2 = q32_floor(pz - r6) - k * 16 - 1, i3 = q32_floor(pz + r6) - k * 16 + 1;
    if (l1 < 0) l1 = 0;
    if (i2 > 16) i2 = 16;
    if (j2 < 1) j2 = 1;
    if (k2 > 248) k2 = 248;
    if (l2 < 0) l2 = 0;
    if (i3 > 16) i3 = 16;
    /* outside the region we care about the step changes nothing we keep: only a room needs
     * to know whether water aborts it */
    int outside = l1 >= cv->rx1 || i2 <= cv->rx0 || l2 >= cv->rz1 || i3 <= cv->rz0 || j2 >= cv->ry1 || k2 + 1 < cv->ry0;
    if (outside && !room) return 1;
    /* water check (shell of the box plus top and bottom layers): water aborts this step */
    int out_mode = cv->mode == CAVE_OUT;
    for (int x = l1; x < i2; x++)
        for (int z = l2; z < i3; z++) {
            int edge = x == l1 || x == i2 - 1 || z == l2 || z == i3 - 1;
            if (edge ? water_col(x, z, j2 - 1, k2 + 1) : water_col(x, z, k2 + 1, k2 + 1) || water_col(x, z, j2 - 1, j2 - 1))
                return 0;
        }
    if (outside) return 1;
    float d6 = q32_to_float(r6), d7 = q32_to_float(r7);
    /* Only the rows of the region (and the one above and below, which a carve can change from
     * there) are carved; another visit only matters at the summary's grass top (it sets flag3:
     * blocks out of the region are read from the summary) or, in the chunk being generated, at
     * the column's tracked top / surface (c_top, c_sl). Columns out of a query box are skipped. */
    int wlo = out_mode ? cv->y0 - 1 : cv->ry0 - 1, whi = out_mode ? cv->y0 + cv->h : cv->ry1;
    int x0 = l1, x1 = i2, z0 = l2, z1 = i3;
    if (!out_mode) {
        if (x0 < cv->rx0) x0 = cv->rx0;
        if (x1 > cv->rx1) x1 = cv->rx1;
        if (z0 < cv->rz0) z0 = cv->rz0;
        if (z1 > cv->rz1) z1 = cv->rz1;
    }
    for (int x = x0; x < x1; x++) {
        float d12 = q32_to_float(SHL(j * 16 + x, 32) + (Q32_ONE >> 1) - px) / d6;
        for (int z = z0; z < z1; z++) {
            float d13 = q32_to_float(SHL(k * 16 + z, 32) + (Q32_ONE >> 1) - pz) / d6;
            float h2 = d12 * d12 + d13 * d13;
            if (h2 >= 1.0f) continue;
            int flag3 = 0, i = z * 16 + x, gtop = -1, t1 = -1, t2 = -1;
            uint8_t tb = cs_blk(&cv->sum[i]);
            if (tb == B_GRASS || (!canyon && tb == B_MYCELIUM)) gtop = cs_top(&cv->sum[i]);
            for (int y = k2; y > j2; --y) {
                if (out_mode) t1 = c_top[i], t2 = c_sl[i];
                if ((y < wlo || y > whi) && y != t1 && y != t2 && y != gtop) continue;
                flag3 = carve_block(x, y, z, h2, py, d7, canyon, flag3);
            }
        }
    }
    return 1;
}

/* MapGenCaves' "cannot reach the chunk any more" test, d8^2 + d9^2 - d10^2 > d11^2, in float,
 * settled in double when the float result is too close to call (the positions are exact) */
static int cave_too_far(Q32 dx, Q32 dz, int d10i, float d11) {
    float d8 = q32_to_float(dx), d9 = q32_to_float(dz), d10 = (float)d10i;
    float lhs = d8 * d8 + d9 * d9 - d10 * d10, rhs = d11 * d11;
    float diff = lhs - rhs;
    if (diff > 0.05f) return 1;
    if (diff < -0.05f) return 0;
    double e8 = (double)dx * (double)0x1p-32f, e9 = (double)dz * (double)0x1p-32f, e10 = d10i, e11 = (double)d11;
    return e8 * e8 + e9 * e9 - e10 * e10 > e11 * e11;
}

/* can a tunnel at (x, z) with `left` steps (and branches) still reach the region of interest?
 * Each step moves at most one block horizontally; radius <= 1.5 + 12 for caves, 1.5 + 6 for canyons. */
static Q32 reg_x0, reg_x1, reg_z0, reg_z1;

INLINE int cave_can_reach(Q32 x, Q32 z, int left) {
    Q32 dx = x < reg_x0 ? reg_x0 - x : (x > reg_x1 ? x - reg_x1 : 0);
    Q32 dz = z < reg_z0 ? reg_z0 - z : (z > reg_z1 ? z - reg_z1 : 0);
    Q32 d = dx > dz ? dx : dz; /* Chebyshev distance is <= the Euclidean one */
    return d <= SHL(left + 16, 32);
}

/* ---- several targets at once ----
 * Java carves one target chunk at a time: it replays the tunnels of every source chunk within
 * 8 chunks, and a tunnel stops for that target when it gets too far from it (a room also stops
 * after its first carve). Where a tunnel goes (its random numbers, its branches) does not depend
 * on the target, only where it stops does; so each tunnel is walked once for several targets,
 * with a bit mask of the targets it still runs for (branches inherit it: a branch only exists for
 * the targets its parent was still running for when it split).
 * Targets are indexed by their place around the chunk being generated: (dz + 1) * 3 + (dx + 1).
 * Bit T_REC of the mask is not a target: a recording walk (see the tunnel records below). */
#define NTGT 9
#define T_REC (1u << NTGT)
static CaveCtx *tg[NTGT];
static Q32 tg_cx[NTGT], tg_cz[NTGT]; /* target chunk centres, world coords */
/* recording: box (world blocks, inclusive) that the current top-level tunnel may change */
static int rb_x0, rb_x1, rb_y0, rb_y1, rb_z0, rb_z1;

/* One step of a tunnel for the targets in act: Java's "too far: stop" test and range test,
 * then the carve. Returns the targets still running. Both tests are first settled with integer
 * bounds from the whole-block distances (exactly where those decide them). */
HOT static unsigned cave_step(unsigned act, Q32 d0, Q32 d1, Q32 d2, Q32 r6, Q32 r7, int left, float f, int room,
                          int canyon) {
    if (act & T_REC) {
        /* blocks a carve step can change, or read for a room's water test (cave_carve) */
        int x0 = q32_floor(d0 - r6) - 1, x1 = q32_floor(d0 + r6) + 1;
        int z0 = q32_floor(d2 - r6) - 1, z1 = q32_floor(d2 + r6) + 1;
        int y0 = q32_floor(d1 - r7) - 2, y1 = q32_floor(d1 + r7) + 2;
        if (x0 < rb_x0) rb_x0 = x0;
        if (x1 > rb_x1) rb_x1 = x1;
        if (y0 < rb_y0) rb_y0 = y0;
        if (y1 > rb_y1) rb_y1 = y1;
        if (z0 < rb_z0) rb_z0 = z0;
        if (z1 > rb_z1) rb_z1 = z1;
    }
    int d11 = (int)(f + 18.0f);                /* d11 in [d11, d11 + 1] */
    int far_hi = (d11 + 1) * (d11 + 1) + left * left, far_lo = d11 * d11 + left * left;
    int lim = 16 + 2 * (int)(r6 >> 32);        /* range limit in [lim, lim + 2) */
    for (int ti = 0; ti < NTGT; ti++) {
        if (!(act >> ti & 1)) continue;
        Q32 dx = d0 - tg_cx[ti], dz = d2 - tg_cz[ti];
        int ax = (int)((dx < 0 ? -dx : dx) >> 32), az = (int)((dz < 0 ? -dz : dz) >> 32); /* |d| in [a, a+1) */
        if (ax > 255 || az > 255 || ax * ax + az * az > far_hi) { /* surely too far */
            act &= ~(1u << ti);
            continue;
        }
        int near = (ax + 1) * (ax + 1) + (az + 1) * (az + 1) <= far_lo;
        if (!near && cave_too_far(dx, dz, left, f + 2.0f + 16.0f)) {
            act &= ~(1u << ti);
            continue;
        }
        if (ax > lim + 2 || az > lim + 2) continue; /* surely out of range */
        if (!(ax + 1 <= lim && az + 1 <= lim)) {
            float flim = 16.0f + q32_to_float(r6) * 2.0f;
            float d8 = q32_to_float(dx), d9 = q32_to_float(dz);
            if (!(d8 >= -flim && d9 >= -flim && d8 <= flim && d9 <= flim)) continue;
        }
        cv = tg[ti];
        if (cave_carve(d0, d1, d2, r6, r7, canyon, room) && room) act &= ~(1u << ti);
    }
    return act;
}

/* tunnel stack (branches) */
typedef struct {
    i64 seed;
    Q32 x, y, z;
    float f, f1, f2;
    int16_t l, i1;
    uint8_t room;
    uint16_t act;
} Tunnel;
#define TSTACK 4 /* branches (width < 1) never branch again: depth <= 2 */
static Tunnel tstack[TSTACK];

/* Walks one cave tunnel (WorldGenCaves.a, 13-argument version) and its branches. */
NOINLINE HOT static void cave_tunnel(i64 seed0, Q32 x0, Q32 y0, Q32 z0, float f0, float f10, float f20, int l0, int i10, int room0,
                        unsigned act0) {
    int sp = 0;
    tstack[sp++] = (Tunnel){seed0, x0, y0, z0, f0, f10, f20, (int16_t)l0, (int16_t)i10, (uint8_t)room0, (uint16_t)act0};
    while (sp > 0) {
        Tunnel t = tstack[--sp];
        unsigned act = t.act;
        float f3 = 0, f4 = 0;
        JRand r;
        jr_seed(&r, t.seed);
        int i1 = t.i1, l = t.l;
        if (i1 <= 0) {
            int j1 = 8 * 16 - 16;
            i1 = j1 - jr_int_i(&r, j1 / 4);
        }
        int flag = 0;
        if (t.room) {
            l = i1 / 2;
            flag = 1;
        }
        int k1 = jr_int_i(&r, i1 / 2) + i1 / 4;
        int flag1 = jr_int_i(&r, 6) == 0;
        float f = t.f, f1 = t.f1, f2 = t.f2;
        Q32 d0 = t.x, d1 = t.y, d2 = t.z;
        for (; l < i1; ++l) {
            if (!cave_can_reach(d0, d2, i1 - l)) break; /* nothing left to carve here (no side effects) */
            float sv = mh_sin_i((float)l * 3.1415927f / (float)i1) * f * 1.0f;
            Q32 r6 = (Q32)(3LL << 31) + q32_of_float(sv); /* 1.5 + ... */
            Q32 r7 = t.room ? r6 / 2 : r6;
            float f5 = mh_cos_i(f2), f6 = mh_sin_i(f2);
            d0 += q32_of_float(mh_cos_i(f1) * f5);
            d1 += q32_of_float(f6);
            d2 += q32_of_float(mh_sin_i(f1) * f5);
            f2 *= flag1 ? 0.92f : 0.7f;
            f2 += f4 * 0.1f;
            f1 += f3 * 0.1f;
            f4 *= 0.9f;
            f3 *= 0.75f;
            {
                float a = jr_float_i(&r), b = jr_float_i(&r), c = jr_float_i(&r);
                f4 += (a - b) * c * 2.0f;
            }
            {
                float a = jr_float_i(&r), b = jr_float_i(&r), c = jr_float_i(&r);
                f3 += (a - b) * c * 4.0f;
            }
            if (!flag && l == k1 && f > 1.0f && i1 > 0) {
                if (sp + 2 <= TSTACK) {
                    i64 s1 = jr_long(&r);
                    float w1 = jr_float_i(&r) * 0.5f + 0.5f;
                    i64 s2 = jr_long(&r);
                    float w2 = jr_float_i(&r) * 0.5f + 0.5f;
                    /* second branch below the first: the first runs (with its sub-branches) first */
                    tstack[sp++] = (Tunnel){s2, d0, d1, d2, w2, f1 + 1.5707964f, f2 / 3.0f, (int16_t)l, (int16_t)i1, 0,
                                            (uint16_t)act};
                    tstack[sp++] = (Tunnel){s1, d0, d1, d2, w1, f1 - 1.5707964f, f2 / 3.0f, (int16_t)l, (int16_t)i1, 0,
                                            (uint16_t)act};
                }
                break;
            }
            if (flag || jr_int_i(&r, 4) != 0) {
                act = cave_step(act, d0, d1, d2, r6, r7, i1 - l, f, flag, 0);
                if (!act) break;
            }
        }
    }
}

NOINLINE HOT static void canyon_tunnel(i64 seed, Q32 d0, Q32 d1, Q32 d2, float f, float f1, float f2, unsigned act) {
    JRand r;
    jr_seed(&r, seed);
    float f3 = 0, f4 = 0;
    int j1 = 8 * 16 - 16;
    int i1 = j1 - jr_int_i(&r, j1 / 4);
    int l = 0;
    /* (the canyon has its own random: stopping before its width table changes nothing else) */
    if (!cave_can_reach(d0, d2, i1)) return;
    float f5 = 1.0f;
    for (int k1 = 0; k1 < 256; ++k1) {
        if (k1 == 0 || jr_int_i(&r, 3) == 0) {
            float a = jr_float_i(&r), b = jr_float_i(&r);
            f5 = 1.0f + a * b * 1.0f;
        }
        canyon_w[k1] = f5 * f5;
    }
    for (; l < i1; ++l) {
        if (!cave_can_reach(d0, d2, i1 - l)) return;
        float sv = mh_sin_i((float)l * 3.1415927f / (float)i1) * f * 1.0f;
        Q32 r6 = (Q32)(3LL << 31) + q32_of_float(sv);
        Q32 r7 = r6 * 3; /* d3 = 3.0 */
        /* d6 *= nextFloat * 0.25 + 0.75: nextFloat = n / 2^24, factor = (n + 3 * 2^24) / 2^26 */
        i64 n6 = jr_next_i(&r, 24), n7 = jr_next_i(&r, 24);
        r6 = (Q32)(((r6 >> 6) * (n6 + 3 * 16777216LL)) >> 20);
        r7 = (Q32)(((r7 >> 6) * (n7 + 3 * 16777216LL)) >> 20);
        float f6 = mh_cos_i(f2), f7 = mh_sin_i(f2);
        d0 += q32_of_float(mh_cos_i(f1) * f6);
        d1 += q32_of_float(f7);
        d2 += q32_of_float(mh_sin_i(f1) * f6);
        f2 *= 0.7f;
        f2 += f4 * 0.05f;
        f1 += f3 * 0.05f;
        f4 *= 0.8f;
        f3 *= 0.5f;
        {
            float a = jr_float_i(&r), b = jr_float_i(&r), c = jr_float_i(&r);
            f4 += (a - b) * c * 2.0f;
        }
        {
            float a = jr_float_i(&r), b = jr_float_i(&r), c = jr_float_i(&r);
            f3 += (a - b) * c * 4.0f;
        }
        if (jr_int_i(&r, 4) != 0) {
            act = cave_step(act, d0, d1, d2, r6, r7, i1 - l, f, 0, 1);
            if (!act) return;
        }
    }
}

/* ---- tunnel records ----
 * A recording walk goes once through every tunnel that can reach an area of 9 x 9 chunks and
 * keeps, for each top-level tunnel that comes near it, its name (source chunk, pass, rank among
 * the source's tunnels) and the box of blocks it may change there (with its branches). Carving
 * the chunk being generated and the populations' cave queries then replay only the tunnels whose
 * box meets theirs, with the exact rules. The area is recomputed when the 3 x 3 chunks around
 * the chunk being generated leave it, so neighbouring calls share it. */
typedef struct {
    int8_t sx, sz;           /* source chunk - area origin chunk */
    uint8_t pass, ord;
    uint8_t x0, x1, z0, z1;  /* box, blocks from the area origin (clipped to the area) */
    uint8_t y0, y1;
} CaveRec;
#define NCREC 192
#define CA_SIZE 9            /* area: chunks [crec_ax, crec_ax + 9) x [crec_az, crec_az + 9) */
static CaveRec crec[NCREC];
static int ncrec, crec_ok, crec_ax = 1 << 30, crec_az;
static int64_t crec_seed;

static void crec_add(int j1, int k1, int pass, int ord) {
    if (rb_x0 > rb_x1) return; /* nothing carved (and its INT32_MAX box would overflow below) */
    int ox = crec_ax * 16, oz = crec_az * 16, n = CA_SIZE * 16 - 1;
    int x0 = rb_x0 - ox, x1 = rb_x1 - ox, z0 = rb_z0 - oz, z1 = rb_z1 - oz;
    if (x1 < 0 || x0 > n || z1 < 0 || z0 > n || rb_y1 < 0 || rb_y0 > 255) return; /* never came near */
    if (ncrec == NCREC) {
        crec_ok = 0; /* too many: everything is walked again */
        return;
    }
    crec[ncrec++] = (CaveRec){(int8_t)(j1 - crec_ax),
                              (int8_t)(k1 - crec_az),
                              (uint8_t)pass,
                              (uint8_t)ord,
                              (uint8_t)(x0 < 0 ? 0 : x0),
                              (uint8_t)(x1 > n ? n : x1),
                              (uint8_t)(z0 < 0 ? 0 : z0),
                              (uint8_t)(z1 > n ? n : z1),
                              (uint8_t)(rb_y0 < 0 ? 0 : rb_y0),
                              (uint8_t)(rb_y1 > 255 ? 255 : rb_y1)};
}

static inline void rb_reset(void) {
    rb_x0 = rb_y0 = rb_z0 = INT32_MAX;
    rb_x1 = rb_y1 = rb_z1 = INT32_MIN;
}

/* The tunnels of source chunk (j1, k1) for one pass (MapGenCaves.a / MapGenCanyon.a), walked for
 * targets `act`. With recs (records of this source and pass, by rank), only those tunnels are
 * walked; a record whose ord is 255 is skipped. */
HOT static void cave_source(int j1, int k1, int pass, unsigned act, const CaveRec *recs, int nrec) {
    JRand r;
    jr_seed(&r, (i64)((u64)(i64)j1 * (u64)cave_mul_x) ^ (i64)((u64)(i64)k1 * (u64)cave_mul_z) ^ g_seed);
    int ord = 0, ri = 0;
    int rec = (act & T_REC) != 0;
    /* walk tunnel `ord`? */
#define WANT() (recs ? (ri < nrec && recs[ri].ord == ord ? (ri++, !crep_dyn || recs[ri - 1].y1 >= crep_lo) : 0) : 1)
    if (pass == 0) {
        int n = jr_int_i(&r, jr_int_i(&r, jr_int_i(&r, 15) + 1) + 1);
        if (jr_int_i(&r, 7) != 0) n = 0;
        for (int c = 0; c < n; ++c) {
            if (recs && ri == nrec) return;
            int xx = j1 * 16 + jr_int_i(&r, 16);
            int yy = jr_int_i(&r, jr_int_i(&r, 120) + 8);
            int zz = k1 * 16 + jr_int_i(&r, 16);
            Q32 d0 = SHL(xx, 32), d1 = SHL(yy, 32), d2 = SHL(zz, 32);
            int k = 1;
            if (jr_int_i(&r, 4) == 0) {
                i64 s = jr_long(&r);
                float w = 1.0f + jr_float_i(&r) * 6.0f;
                if (WANT()) {
                    rb_reset();
                    cave_tunnel(s, d0, d1, d2, w, 0.0f, 0.0f, -1, -1, 1, act);
                    if (rec) crec_add(j1, k1, pass, ord);
                }
                ord++;
                k += jr_int_i(&r, 4);
            }
            for (int l1 = 0; l1 < k; ++l1) {
                float f = jr_float_i(&r) * 3.1415927f * 2.0f;
                float f1 = (jr_float_i(&r) - 0.5f) * 2.0f / 8.0f;
                float f2 = jr_float_i(&r) * 2.0f;
                f2 += jr_float_i(&r); /* (draws in Java's left-to-right order) */
                if (jr_int_i(&r, 10) == 0) {
                    float a = jr_float_i(&r), b = jr_float_i(&r);
                    f2 *= a * b * 3.0f + 1.0f;
                }
                i64 s = jr_long(&r);
                if (WANT()) {
                    rb_reset();
                    cave_tunnel(s, d0, d1, d2, f2, f, f1, 0, 0, 0, act);
                    if (rec) crec_add(j1, k1, pass, ord);
                }
                ord++;
            }
        }
    } else if (jr_int_i(&r, 50) == 0) {
        int xx = j1 * 16 + jr_int_i(&r, 16);
        int yy = jr_int_i(&r, jr_int_i(&r, 40) + 8) + 20;
        int zz = k1 * 16 + jr_int_i(&r, 16);
        float f = jr_float_i(&r) * 3.1415927f * 2.0f;
        float f1 = (jr_float_i(&r) - 0.5f) * 2.0f / 8.0f;
        float f2 = jr_float_i(&r) * 2.0f;
        f2 = (f2 + jr_float_i(&r)) * 2.0f;
        i64 s = jr_long(&r);
        if (WANT()) {
            rb_reset();
            canyon_tunnel(s, SHL(xx, 32), SHL(yy, 32), SHL(zz, 32), f2, f, f1, act);
            if (rec) crec_add(j1, k1, pass, ord);
        }
    }
#undef WANT
}

/* targets (bits of tmask) whose range (8 chunks) holds source chunk (j1, k1) */
static unsigned src_targets(unsigned tmask, int j1, int k1) {
    unsigned act = 0;
    for (int ti = 0; ti < NTGT; ti++)
        if ((tmask >> ti & 1) && j1 >= tg[ti]->tcx - 8 && j1 <= tg[ti]->tcx + 8 && k1 >= tg[ti]->tcz - 8 &&
            k1 <= tg[ti]->tcz + 8)
            act |= 1u << ti;
    return act;
}

/* reach region: union of the targets' regions (and of the record area when recording) */
static void caves_setup(unsigned tmask) {
    int first = 1;
    for (int ti = 0; ti <= NTGT; ti++) {
        Q32 x0, x1, z0, z1;
        if (ti < NTGT) {
            if (!(tmask >> ti & 1)) continue;
            const CaveCtx *c = tg[ti];
            tg_cx[ti] = SHL(c->tcx * 16 + 8, 32);
            tg_cz[ti] = SHL(c->tcz * 16 + 8, 32);
            x0 = SHL(c->tcx * 16 + c->rx0, 32), x1 = SHL(c->tcx * 16 + c->rx1, 32);
            z0 = SHL(c->tcz * 16 + c->rz0, 32), z1 = SHL(c->tcz * 16 + c->rz1, 32);
        } else {
            if (!(tmask & T_REC)) continue;
            x0 = SHL(crec_ax * 16, 32), x1 = SHL((crec_ax + CA_SIZE) * 16, 32);
            z0 = SHL(crec_az * 16, 32), z1 = SHL((crec_az + CA_SIZE) * 16, 32);
        }
        if (first || x0 < reg_x0) reg_x0 = x0;
        if (first || x1 > reg_x1) reg_x1 = x1;
        if (first || z0 < reg_z0) reg_z0 = z0;
        if (first || z1 > reg_z1) reg_z1 = z1;
        first = 0;
    }
}

/* MapGenBase.a (caves, then canyons) for the targets tmask (and the record area if T_REC is
 * set): every source chunk within 8 chunks of a target, in Java's order (x outer, z inner),
 * each walked for the targets in its range */
static void caves_run(unsigned tmask) {
    int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);
    for (int ti = 0; ti < NTGT; ti++) {
        if (!(tmask >> ti & 1)) continue;
        if (tg[ti]->tcx < x0) x0 = tg[ti]->tcx;
        if (tg[ti]->tcx > x1) x1 = tg[ti]->tcx;
        if (tg[ti]->tcz < z0) z0 = tg[ti]->tcz;
        if (tg[ti]->tcz > z1) z1 = tg[ti]->tcz;
    }
    if (tmask & T_REC) {
        if (crec_ax < x0) x0 = crec_ax;
        if (crec_ax + CA_SIZE - 1 > x1) x1 = crec_ax + CA_SIZE - 1;
        if (crec_az < z0) z0 = crec_az;
        if (crec_az + CA_SIZE - 1 > z1) z1 = crec_az + CA_SIZE - 1;
    }
    caves_setup(tmask);
    for (int pass = 0; pass < 2; pass++)
        for (int j1 = x0 - 8; j1 <= x1 + 8; ++j1)
            for (int k1 = z0 - 8; k1 <= z1 + 8; ++k1) {
                unsigned act = src_targets(tmask, j1, k1) | (tmask & T_REC);
                if (act) cave_source(j1, k1, pass, act, 0, 0);
            }
}

/* the same for the box [x0, x1] x [y0, y1] x [z0, z1] (world, inclusive), from the records */
static void caves_replay(unsigned tmask, int x0, int y0, int z0, int x1, int y1, int z1) {
    caves_setup(tmask);
    x0 -= crec_ax * 16, x1 -= crec_ax * 16, z0 -= crec_az * 16, z1 -= crec_az * 16;
    CaveRec sel[16];
    for (int i = 0; i < ncrec;) {
        int j = i, n = 0;
        int j1 = crec_ax + crec[i].sx, k1 = crec_az + crec[i].sz;
        unsigned act = src_targets(tmask, j1, k1);
        for (; j < ncrec && crec[j].sx == crec[i].sx && crec[j].sz == crec[i].sz && crec[j].pass == crec[i].pass; j++) {
            const CaveRec *c = &crec[j];
            if (act && c->x1 >= x0 && c->x0 <= x1 && c->z1 >= z0 && c->z0 <= z1 && c->y1 >= y0 && c->y0 <= y1) {
                if (n == 16) { /* full: the ranks are increasing, so walking these first keeps the order */
                    cave_source(j1, k1, crec[i].pass, act, sel, n);
                    n = 0;
                }
                sel[n++] = *c;
            }
        }
        if (n) cave_source(j1, k1, crec[i].pass, act, sel, n);
        i = j;
    }
}

/* make the records cover the 3 x 3 chunks around (cx, cz) */
static void crec_cover(int cx, int cz) {
    if (crec_seed == g_seed && crec_ok && cx - 1 >= crec_ax && cx + 1 < crec_ax + CA_SIZE && cz - 1 >= crec_az &&
        cz + 1 < crec_az + CA_SIZE)
        return;
    crec_seed = g_seed;
    crec_ax = cx - CA_SIZE / 2;
    crec_az = cz - CA_SIZE / 2;
    ncrec = 0;
    crec_ok = 1;
    caves_run(T_REC);
}

/* ======================================================================== */
/* Summary cache                                                             */
/* ======================================================================== */

#define NSUM 25
typedef struct {
    int cx, cz;
    uint32_t age;
    uint8_t valid;
    uint8_t b00;  /* biome of block (0, 0): the biome of the population of chunk (cx-1, cz-1) */
    ColSum s[256];
} SumEntry;
static SumEntry sumc[NSUM];
static uint32_t sum_clock;

static SumEntry *sum_find(int cx, int cz) {
    for (int i = 0; i < NSUM; i++)
        if (sumc[i].valid && sumc[i].cx == cx && sumc[i].cz == cz) {
            sumc[i].age = ++sum_clock;
            return &sumc[i];
        }
    return 0;
}

/* entry to (re)fill: the least recently used one not pinned */
static uint8_t sum_pin[NSUM];
static SumEntry *sum_slot(int cx, int cz) {
    int best = -1;
    for (int i = 0; i < NSUM; i++) {
        if (sum_pin[i]) continue;
        if (!sumc[i].valid) {
            best = i;
            break;
        }
        if (best < 0 || sumc[i].age < sumc[best].age) best = i;
    }
    SumEntry *e = &sumc[best];
    e->cx = cx;
    e->cz = cz;
    e->valid = 1;
    e->age = ++sum_clock;
    return e;
}

static SumEntry *sum_get(int cx, int cz) {
    SumEntry *e = sum_find(cx, cz);
    if (e) return e;
    e = sum_slot(cx, cz);
    chunk_terrain(cx, cz, 0, 0, 0, e->s);
    e->b00 = cb16[0];
    return e;
}

/* the 3x3 summaries around the chunk being generated, pinned during gen_slab */
static SumEntry *nb[3][3]; /* [dz+1][dx+1] */
static int nb_cx, nb_cz;

static const ColSum *col_sum(int X, int Z) {
    int cx = X >> 4, cz = Z >> 4;
    int dx = cx - nb_cx + 1, dz = cz - nb_cz + 1;
    if (dx < 0 || dx > 2 || dz < 0 || dz > 2) return 0;
    return &nb[dz][dx]->s[(X & 15) + (Z & 15) * 16];
}

/* ======================================================================== */
/* Population: the chunk being generated (C), the view of a population (P)   */
/* ======================================================================== */

/* chunk C: blocks for y in [c_y0, c_y0 + c_h) in c_out; c_top: highest non-air y of each column
 * (whole height), c_sl: highest solid-or-liquid y (precipitation height - 1) */
static uint8_t *c_out;
static int c_y0, c_h, c_cx, c_cz;
/* (c_top, c_sl: declared with the caves, which keep them right) */

#define PV (U.p.v)

static inline int lake_bit(const LakeRec *L, int x, int y, int z) {
    int i = (x * 16 + z) * 8 + y;
    return (L->shape[i >> 3] >> (i & 7)) & 1;
}

/* view block at world (X, Y, Z) */
HOT static uint8_t vget(int X, int Y, int Z) {
    if (Y < 0) return B_BEDROCK;
    if (Y > 255) return B_AIR;
    View *v = &PV;
    int lx = X - v->ox, lz = Z - v->oz;
    if (lx < 0) lx = 0;
    if (lx > 31) lx = 31;
    if (lz < 0) lz = 0;
    if (lz > 31) lz = 31;
    X = v->ox + lx;
    Z = v->oz + lz;
    int i = lz * 32 + lx;
    if (Y > v->top[i]) return (Y == v->top[i] + 1 && v->pl[i]) ? v->pl[i] : B_AIR;
    if (Y == v->top[i]) return v->tb[i];
    for (int k = 0; k < v->nlake; k++) {
        const LakeRec *L = &v->lake[k];
        int ax = X - L->x, ay = Y - L->y, az = Z - L->z;
        if (ax >= 0 && ax < 16 && az >= 0 && az < 16 && ay >= 0 && ay < 8 && lake_bit(L, ax, ay, az))
            return ay >= 4 ? B_AIR : L->liquid;
    }
    const ColSum *s = col_sum(X, Z);
    if (!s) return Y < SEA ? B_STONE : B_AIR;
    if (Y > cs_top(s)) return B_AIR;
    if (v->qon && X >= v->qx0 && X < v->qx1 && Y >= v->qy0 && Y < v->qy1 && Z >= v->qz0 && Z < v->qz1) {
        uint8_t m = U.p.qmask[((Y - v->qy0) * (v->qz1 - v->qz0) + (Z - v->qz0)) * (v->qx1 - v->qx0) + (X - v->qx0)];
        if (m) return (uint8_t)(m - 1);
    }
    return sum_block(s, Y);
}

static inline int v_empty(int X, int Y, int Z) { return vget(X, Y, Z) == B_AIR; }

/* World.getHighestBlockYAt(...).getY(): first y above the highest opaque block */
static int hm(int X, int Z) {
    View *v = &PV;
    int lx = X - v->ox, lz = Z - v->oz;
    if (lx < 0) lx = 0;
    if (lx > 31) lx = 31;
    if (lz < 0) lz = 0;
    if (lz > 31) lz = 31;
    return v->top[lz * 32 + lx] + 1;
}

/* World.r(pos) (top solid-or-liquid... actually top solid non-leaves block) + 1 */
static int top_solid(int X, int Z) {
    int y = hm(X, Z) + 1;
    for (; y >= 0; y--) {
        uint8_t b = vget(X, y, Z);
        if (IS_SOLID(b) && !IS_LEAVES(b)) break;
    }
    return y + 1;
}

/* write rules: checked against C's real blocks before a write lands in C */
enum {
    R_ALWAYS, R_TREE,     /* air, leaves, replaceable plant */
    R_AIRLEAF, R_NOTCUBE, R_TREEOK, R_AIR, R_STONE, R_DIRTGRASS, R_DIRTCLAY, R_ICE, R_ICE2, R_BUILD,
    R_NOTCHEST, R_WALL, R_SAND_SANDY
};

static int rule_ok(int rule, uint8_t b) {
    switch (rule) {
    case R_ALWAYS: return 1;
    case R_TREE: return b == B_AIR || IS_LEAVES(b) || IS_RPLANT(b);
    case R_AIRLEAF: return b == B_AIR || IS_LEAVES(b);
    case R_NOTCUBE: return !IS_CUBE(b);
    case R_TREEOK:
        return b == B_AIR || IS_LEAVES(b) || b == B_GRASS || IS_DIRTISH(b) || IS_LOG(b) || b == B_VINE ||
               (b >= B_SAPLING_OAK && b <= B_SAPLING_DARK_OAK);
    case R_AIR: return b == B_AIR;
    case R_STONE: return b == B_STONE;
    case R_DIRTGRASS: return IS_DIRTISH(b) || b == B_GRASS;
    case R_DIRTCLAY: return IS_DIRTISH(b) || b == B_CLAY;
    case R_ICE: return b == B_AIR || IS_DIRTISH(b) || b == B_SNOW || b == B_ICE;
    case R_ICE2: return b == B_AIR || IS_DIRTISH(b) || b == B_SNOW || b == B_ICE || b == B_PACKED_ICE;
    case R_BUILD: return IS_BUILD(b);
    case R_NOTCHEST: return b != B_CHEST;
    case R_WALL: return IS_BUILD(b) && b != B_CHEST;
    }
    return 1;
}

static inline int is_plantish(uint8_t b) {
    return IS_PLANT(b) || IS_RPLANT(b) || b == B_CACTUS || b == B_SNOW_LAYER || b == B_SUGAR_CANE;
}

/* top of column (x, z) of C after removing the block at y: next non-air below */
static void c_lower_top(int i, int y) {
    int x = i & 15, z = i >> 4;
    const ColSum *s = &nb[1][1]->s[i];
    while (y > 0) {
        y--;
        uint8_t b = (y >= c_y0 && y < c_y0 + c_h) ? c_out[(y - c_y0) * 256 + z * 16 + x] : sum_block(s, y);
        if (b != B_AIR) break;
    }
    c_top[i] = (uint8_t)y;
}

/* chunk C bookkeeping for a block that was (or, out of the y range, presumably was) written */
static void c_track(int i, int Y, uint8_t b) {
    if (b == B_AIR) {
        if (Y == c_top[i]) c_lower_top(i, Y);
        if (Y == c_sl[i]) c_sl[i] = c_top[i] < c_sl[i] ? c_top[i] : (uint8_t)(Y - 1);
    } else {
        if (Y > c_top[i]) c_top[i] = (uint8_t)Y;
        if ((IS_SOLID(b) || (bflags[b] & BF_LIQUID)) && Y > c_sl[i]) c_sl[i] = (uint8_t)Y;
    }
}

static int put_skip_c; /* set: put() updates the view only */

/* writes block b at world (X, Y, Z): into the view, and into C if the rule holds there */
static void put(int X, int Y, int Z, uint8_t b, int rule) {
    if (Y < 0 || Y > 255) return;
    View *v = &PV;
    int lx = X - v->ox, lz = Z - v->oz;
    if (lx >= 0 && lx < 32 && lz >= 0 && lz < 32) {
        int i = lz * 32 + lx;
        if (b == B_AIR) {
            if (Y == v->top[i] + 1) v->pl[i] = 0;
            if (Y == v->top[i]) {
                v->pl[i] = 0;
                int y = Y;
                v->top[i] = (uint8_t)(Y - 1); /* provisional so vget sees below */
                while (y > 0) {
                    y--;
                    v->top[i] = (uint8_t)y;
                    v->tb[i] = B_AIR;
                    /* read what lies below with the column top above it */
                    v->top[i] = 255;
                    uint8_t u = vget(X, y, Z);
                    v->top[i] = (uint8_t)y;
                    if (u != B_AIR) {
                        v->tb[i] = u;
                        break;
                    }
                }
            }
        } else if (is_plantish(b)) {
            if (Y == v->top[i] + 1) v->pl[i] = b;
        } else {
            if (Y > v->top[i]) {
                v->top[i] = (uint8_t)Y;
                v->tb[i] = b;
                v->pl[i] = 0;
            } else if (Y == v->top[i]) {
                v->tb[i] = b;
            }
        }
    }
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    if (!put_skip_c && cx >= 0 && cx < 16 && cz >= 0 && cz < 16) {
        int i = cz * 16 + cx;
        if (Y >= c_y0 && Y < c_y0 + c_h) {
            uint8_t *o = &c_out[(Y - c_y0) * 256 + i];
            if (!rule_ok(rule, *o)) return;
            *o = b;
        }
        c_track(i, Y, b);
    }
}

/* only into C (features whose result never feeds a later decision: ores...) */
static void put_c(int X, int Y, int Z, uint8_t b, int rule) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    if (cx < 0 || cx > 15 || cz < 0 || cz > 15 || Y < c_y0 || Y >= c_y0 + c_h) return;
    uint8_t *o = &c_out[(Y - c_y0) * 256 + cz * 16 + cx];
    if (rule_ok(rule, *o)) {
        *o = b;
        c_track(cz * 16 + cx, Y, b);
    }
}

/* C's real block if (X, Y, Z) is in C and in range, else the view's */
static uint8_t cget(int X, int Y, int Z) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    if (cx >= 0 && cx < 16 && cz >= 0 && cz < 16 && Y >= c_y0 && Y < c_y0 + c_h)
        return c_out[(Y - c_y0) * 256 + cz * 16 + cx];
    return vget(X, Y, Z);
}

static inline int in_c_range(int X, int Y, int Z) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    return cx >= 0 && cx < 16 && cz >= 0 && cz < 16 && Y >= c_y0 && Y < c_y0 + c_h;
}

static inline int in_c_cols(int X, int Z) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    return cx >= 0 && cx < 16 && cz >= 0 && cz < 16;
}

/* ======================================================================== */
/* Population features                                                       */
/* ======================================================================== */

static JRand PR;           /* the population random (ChunkProviderGenerate.h) */
static int PK, PL;         /* origin of the populated chunk (k = px*16, l = pz*16) */

#define RI(n) jr_int(&PR, (n))
#define RF() jr_float(&PR)

/* ---- cave query: marks carved blocks of box [x0,x1)x[y0,y1)x[z0,z1) for vget ---- */
static void cave_query(int x0, int y0, int z0, int x1, int y1, int z1) {
    View *v = &PV;
    if (y0 < 0) y0 = 0;
    if (y1 > 256) y1 = 256;
    if ((x1 - x0) * (y1 - y0) * (z1 - z0) > (int)sizeof U.p.qmask || y1 <= y0) {
        v->qon = 0;
        return;
    }
    memset(U.p.qmask, 0, sizeof U.p.qmask);
    CaveCtx ctx[4];
    unsigned tmask = 0;
    int n = 0;
    for (int tcz = z0 >> 4; tcz <= (z1 - 1) >> 4; tcz++)
        for (int tcx = x0 >> 4; tcx <= (x1 - 1) >> 4; tcx++) {
            int dx = tcx - nb_cx + 1, dz = tcz - nb_cz + 1;
            if (dx < 0 || dx > 2 || dz < 0 || dz > 2 || n == 4) continue;
            CaveCtx *c = &ctx[n++];
            memset(c, 0, sizeof *c);
            c->mode = CAVE_MASK;
            c->bx0 = x0;
            c->bx1 = x1;
            c->by0 = y0;
            c->by1 = y1;
            c->bz0 = z0;
            c->bz1 = z1;
            c->mask = U.p.qmask;
            c->tcx = tcx;
            c->tcz = tcz;
            c->sum = nb[dz][dx]->s;
            c->rx0 = x0 - tcx * 16 < 0 ? 0 : x0 - tcx * 16;
            c->rx1 = x1 - tcx * 16 > 16 ? 16 : x1 - tcx * 16;
            c->rz0 = z0 - tcz * 16 < 0 ? 0 : z0 - tcz * 16;
            c->rz1 = z1 - tcz * 16 > 16 ? 16 : z1 - tcz * 16;
            c->ry0 = y0;
            c->ry1 = y1;
            tg[dz * 3 + dx] = c;
            tmask |= 1u << (dz * 3 + dx);
        }
    if (tmask) {
        if (crec_ok) caves_replay(tmask, x0, y0, z0, x1 - 1, y1 - 1, z1 - 1);
        else caves_run(tmask);
    }
    v->qon = 1;
    v->qx0 = x0;
    v->qx1 = x1;
    v->qy0 = y0;
    v->qy1 = y1;
    v->qz0 = z0;
    v->qz1 = z1;
}

/* ---- outcomes of a population's cave-dependent tests ----
 * Lakes and dungeons read the caves (cave queries). A population is replayed by the up to four
 * gen_slab calls whose chunk it writes into, and each time these tests give the same result;
 * a failed one draws the same random numbers whatever made it fail. So the outcomes are kept
 * per population and a known failure only makes its draws. */
typedef struct {
    int px, pz;
    uint8_t valid, lakes, dungeons; /* bit set: lake (0 water, 1 lava) placed, dungeon attempt passed */
} PopMemo;
#define NPMEMO 16
static PopMemo pmemo[NPMEMO];
static int pmemo_next;
static PopMemo *pm;   /* the current population's */
static int pm_known;  /* its outcomes are known */
static int dung_idx;  /* attempt being made */

static void pmemo_begin(int px, int pz) {
    for (int i = 0; i < NPMEMO; i++)
        if (pmemo[i].valid && pmemo[i].px == px && pmemo[i].pz == pz) {
            pm = &pmemo[i];
            pm_known = 1;
            return;
        }
    pm = &pmemo[pmemo_next];
    pmemo_next = (pmemo_next + 1) % NPMEMO;
    *pm = (PopMemo){px, pz, 0, 0, 0};
    pm_known = 0;
}

/* ---- lakes (WorldGenLakes) ---- */
static int lake_flag(const uint8_t *sh, int j, int k1, int j1) {
#define LB(x, z, y) ((sh[((((x) * 16 + (z)) * 8 + (y))) >> 3] >> ((((x) * 16 + (z)) * 8 + (y)) & 7)) & 1)
    return !LB(j, k1, j1) && ((j < 15 && LB(j + 1, k1, j1)) || (j > 0 && LB(j - 1, k1, j1)) ||
                              (k1 < 15 && LB(j, k1 + 1, j1)) || (k1 > 0 && LB(j, k1 - 1, j1)) ||
                              (j1 < 7 && LB(j, k1, j1 + 1)) || (j1 > 0 && LB(j, k1, j1 - 1)));
}

static int lake_border_ok(const LakeRec *L) {
    for (int j = 0; j < 16; j++)
        for (int k1 = 0; k1 < 16; k1++)
            for (int j1 = 0; j1 < 8; j1++)
                if (lake_flag(L->shape, j, k1, j1)) {
                    uint8_t b = vget(L->x + j, L->y + j1, L->z + k1);
                    if (j1 >= 4 && (bflags[b] & BF_LIQUID)) return 0;
                    if (j1 < 4 && !IS_BUILD(b) && b != L->liquid) return 0;
                }
    return 1;
}

static void lake(uint8_t liquid, int X, int Y, int Z) {
    View *v = &PV;
    X -= 8;
    Z -= 8;
    while (Y > 5 && v_empty(X, Y, Z)) Y--;
    if (Y <= 4) return;
    Y -= 4;
    int bit = liquid == B_LAVA ? 2 : 1;
    if (pm_known && !(pm->lakes & bit)) { /* failed before: only its draws */
        int n = RI(4) + 4;
        jr_skip(&PR, 12u * (unsigned)n); /* 6 nextDouble per blob */
        return;
    }
    LakeRec tmp, *L = v->nlake < 2 ? &v->lake[v->nlake] : &tmp;
    L->x = X;
    L->y = Y;
    L->z = Z;
    L->liquid = liquid;
    memset(L->shape, 0, sizeof L->shape);
    int n = RI(4) + 4;
    for (int j = 0; j < n; j++) {
        float d0 = jr_doublef(&PR) * 6.0f + 3.0f, d1 = jr_doublef(&PR) * 4.0f + 2.0f, d2 = jr_doublef(&PR) * 6.0f + 3.0f;
        float d3 = jr_doublef(&PR) * (16.0f - d0 - 2.0f) + 1.0f + d0 / 2.0f;
        float d4 = jr_doublef(&PR) * (8.0f - d1 - 4.0f) + 2.0f + d1 / 2.0f;
        float d5 = jr_doublef(&PR) * (16.0f - d2 - 2.0f) + 1.0f + d2 / 2.0f;
        for (int k = 1; k < 15; k++)
            for (int l = 1; l < 15; l++)
                for (int i1 = 1; i1 < 7; i1++) {
                    float d6 = ((float)k - d3) / (d0 / 2.0f), d7 = ((float)i1 - d4) / (d1 / 2.0f),
                          d8 = ((float)l - d5) / (d2 / 2.0f);
                    if (d6 * d6 + d7 * d7 + d8 * d8 < 1.0f) {
                        int idx = (k * 16 + l) * 8 + i1;
                        L->shape[idx >> 3] |= (uint8_t)(1 << (idx & 7));
                    }
                }
    }
    /* caves can only make the border test fail: test without, then with them */
    if (!lake_border_ok(L)) return;
    cave_query(X, Y, Z, X + 16, Y + 8, Z + 16);
    int ok = lake_border_ok(L);
    if (!ok) {
        v->qon = 0;
        return;
    }
    pm->lakes |= (uint8_t)bit;
    /* place: record the lake for the view, write into C */
    const uint8_t *sh = L->shape;
    if (L == &v->lake[v->nlake]) v->nlake++;
    for (int j = 0; j < 16; j++)
        for (int k1 = 0; k1 < 16; k1++) {
            int any = 0;
            for (int j1 = 0; j1 < 8; j1++)
                if (LB(j, k1, j1)) {
                    any = 1;
                    put_c(X + j, Y + j1, Z + k1, j1 >= 4 ? B_AIR : liquid, R_ALWAYS);
                }
            if (!any) continue;
            int lx = X + j - v->ox, lz = Z + k1 - v->oz;
            if (lx < 0 || lx > 31 || lz < 0 || lz > 31) continue;
            int i = lz * 32 + lx;
            if (v->top[i] <= Y + 7) {
                int y = v->top[i];
                v->pl[i] = 0;
                v->top[i] = 255; /* read the lake and terrain below */
                while (y > 0 && vget(X + j, y, Z + k1) == B_AIR) y--;
                v->tb[i] = vget(X + j, y, Z + k1);
                v->top[i] = (uint8_t)y;
            }
        }
    /* exposed dirt becomes grass (mycelium in mushroom biomes) */
    for (int j = 0; j < 16; j++)
        for (int k1 = 0; k1 < 16; k1++)
            for (int j1 = 4; j1 < 8; j1++)
                if (LB(j, k1, j1)) {
                    int x = X + j, y = Y + j1 - 1, z = Z + k1;
                    if (IS_DIRTISH(vget(x, y, z)) && y + 1 >= hm(x, z) - 1) {
                        const Biome *b = in_c_cols(x, z) ? bio(c_b16[(x & 15) + (z & 15) * 16]) : bio(BI_PLAINS);
                        uint8_t g = b->top == B_MYCELIUM ? B_MYCELIUM : B_GRASS;
                        put(x, y, z, g, R_DIRTGRASS);
                    }
                }
    if (liquid == B_LAVA) {
        for (int j = 0; j < 16; j++)
            for (int k1 = 0; k1 < 16; k1++)
                for (int j1 = 0; j1 < 8; j1++)
                    if (lake_flag(L->shape, j, k1, j1) && (j1 < 4 || RI(2) != 0) &&
                        IS_BUILD(vget(X + j, Y + j1, Z + k1)))
                        put(X + j, Y + j1, Z + k1, B_STONE, R_BUILD);
    }
    if (liquid == B_WATER) {
        for (int j = 0; j < 16; j++)
            for (int k1 = 0; k1 < 16; k1++) {
                int x = X + j, y = Y + 4, z = Z + k1;
                if (vget(x, y, z) != B_WATER) continue;
                const Biome *b = in_c_cols(x, z) ? bio(c_b16[(x & 15) + (z & 15) * 16]) : bio(BI_PLAINS);
                if (biome_temp_at(b, x, y, z) <= 0.15f) put(x, y, z, B_ICE, R_ALWAYS);
            }
    }
    v->qon = 0;
#undef LB
}

/* ---- dungeons (WorldGenDungeons) ---- */

/* RNG draws of the chest loot (StructurePieceTreasure.a with the dungeon list plus an
 * enchanted book) -- see dungeon_book() for the book */
static void dungeon_book(void);

static void dungeon_loot(void) {
    dungeon_book();
    for (int j = 0; j < 8; j++) {
        static const uint8_t W[16] = {10, 10, 10, 10, 10, 10, 10, 1, 10, 4, 4, 10, 2, 5, 1, 1};
        static const uint8_t MX[16] = {1, 4, 1, 4, 4, 4, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1};
        int r = RI(108), k = 0;
        while (k < 15 && (r -= W[k]) >= 0) k++;
        RI(MX[k]); /* count = min + nextInt(max - min + 1), min = 1 */
        RI(27);    /* slot */
    }
}

static void dungeon(int X, int Y, int Z) {
    int i = RI(2) + 2, j = -i - 1, k = i + 1;
    int l = RI(2) + 2, i1 = -l - 1, j1 = l + 1;
    if (pm_known && !(pm->dungeons >> dung_idx & 1)) return; /* failed before */
    /* floor and ceiling must be buildable: caves only remove blocks, so test the view first */
    for (int a = j; a <= k; a++)
        for (int c = i1; c <= j1; c++)
            if (!IS_BUILD(vget(X + a, Y - 1, Z + c)) || !IS_BUILD(vget(X + a, Y + 4, Z + c))) return;
    cave_query(X + j, Y - 2, Z + i1, X + k + 1, Y + 6, Z + j1 + 1);
    int k1 = 0;
    for (int a = j; a <= k; a++)
        for (int b = -1; b <= 4; b++)
            for (int c = i1; c <= j1; c++) {
                uint8_t m = vget(X + a, Y + b, Z + c);
                if ((b == -1 || b == 4) && !IS_BUILD(m)) {
                    PV.qon = 0;
                    return;
                }
                if ((a == j || a == k || c == i1 || c == j1) && b == 0 && m == B_AIR && v_empty(X + a, Y + 1, Z + c)) ++k1;
            }
    if (k1 < 1 || k1 > 5) {
        PV.qon = 0;
        return;
    }
    pm->dungeons |= (uint8_t)(1 << dung_idx);
    for (int a = j; a <= k; a++)
        for (int b = 3; b >= -1; b--)
            for (int c = i1; c <= j1; c++) {
                int x = X + a, y = Y + b, z = Z + c;
                uint8_t m = vget(x, y, z);
                if (a != j && b != -1 && c != i1 && a != k && b != 4 && c != j1) {
                    if (m != B_CHEST) put(x, y, z, B_AIR, R_NOTCHEST);
                } else if (y >= 0 && !IS_BUILD(vget(x, y - 1, z))) {
                    put(x, y, z, B_AIR, R_ALWAYS);
                } else if (IS_BUILD(m) && m != B_CHEST) {
                    if (b == -1 && RI(4) != 0) put(x, y, z, B_MOSSY_COBBLESTONE, R_WALL);
                    else put(x, y, z, B_COBBLESTONE, R_WALL);
                }
            }
    /* chests: candidates are interior cells (air after the loop above, or the first chest);
     * neighbours are interior cells or the wall ring, which keeps its buildability */
    int chx = 0x7fffffff, chz = 0;
    for (int a = 0; a < 2; a++)
        for (int t = 0; t < 3; t++) {
            int x = X + RI(i * 2 + 1) - i, y = Y, z = Z + RI(l * 2 + 1) - l;
            if (x == chx && z == chz) continue; /* not empty */
            int n = 0;
            static const int8_t D[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            for (int d = 0; d < 4; d++) {
                int nx = x + D[d][0], nz = z + D[d][1];
                if (nx > X + j && nx < X + k && nz > Z + i1 && nz < Z + j1) n += (nx == chx && nz == chz);
                else n += IS_BUILD(vget(nx, y, nz)) != 0;
            }
            if (n == 1) {
                put(x, y, z, B_CHEST, R_ALWAYS);
                chx = x;
                chz = z;
                dungeon_loot();
                break;
            }
        }
    put(X, Y, Z, B_MOB_SPAWNER, R_ALWAYS);
    RI(4); /* mob type */
    PV.qon = 0;
}

/* ---- ores (WorldGenMinable) ---- */
HOT static void minable(int X, int Y, int Z, int n, uint8_t blk) {
    float f = RF() * 3.1415927f;
    float s = mh_sin_i(f) * (float)n / 8.0f, c = mh_cos_i(f) * (float)n / 8.0f;
    /* Java: (float)(x + 8) + sin * n / 8, in float with absolute coordinates */
    float d0 = (float)(X + 8) + s, d1 = (float)(X + 8) - s, d2 = (float)(Z + 8) + c, d3 = (float)(Z + 8) - c;
    int d4 = Y + RI(3) - 2, d5 = Y + RI(3) - 2;
    /* can the vein touch C (and its y range)? radius <= n/16 + 1 around the segment */
    float rmax = (float)n / 8.0f + 2.0f;
    float minx = (d0 < d1 ? d0 : d1) - rmax, maxx = (d0 > d1 ? d0 : d1) + rmax;
    float minz = (d2 < d3 ? d2 : d3) - rmax, maxz = (d2 > d3 ? d2 : d3) + rmax;
    int miny = (d4 < d5 ? d4 : d5) - (int)rmax - 1, maxy = (d4 > d5 ? d4 : d5) + (int)rmax + 1;
    int touch = maxx >= (float)(c_cx * 16) && minx < (float)(c_cx * 16 + 16) && maxz >= (float)(c_cz * 16) &&
                minz < (float)(c_cz * 16 + 16) && maxy >= c_y0 && miny < c_y0 + c_h;
    if (!touch) {
        jr_skip(&PR, 2u * (unsigned)n); /* the n nextDouble (two steps each) */
        return;
    }
    /* positions relative to an integer base so float keeps its precision */
    int bx = floor_f(d0), bz = floor_f(d2);
    float r0 = d0 - (float)bx, r1 = d1 - (float)bx, r2 = d2 - (float)bz, r3 = d3 - (float)bz;
    /* C's blocks, relative to the base */
    int cx0 = c_cx * 16 - bx, cx1 = cx0 + 15, cz0 = c_cz * 16 - bz, cz1 = cz0 + 15;
    float hmax = (float)n / 16.0f + 1.0f; /* > the sphere radius, (sin + 1) * d9 / 2 + 1/2 */
    for (int i = 0; i < n; i++) {
        float f1 = (float)i / (float)n;
        float d6 = r0 + (r1 - r0) * f1, d7 = (float)d4 + (float)(d5 - d4) * f1, d8 = r2 + (r3 - r2) * f1;
        if (d6 + hmax < (float)cx0 || d6 - hmax > (float)(cx1 + 1) || d8 + hmax < (float)cz0 ||
            d8 - hmax > (float)(cz1 + 1) || d7 + hmax < (float)c_y0 || d7 - hmax > (float)(c_y0 + c_h)) {
            jr_skip(&PR, 2); /* its nextDouble */
            continue;
        }
        float d9 = jr_doublef_i(&PR) * (float)n / 16.0f;
        float d10 = (mh_sin_i(3.1415927f * f1) + 1.0f) * d9 + 1.0f, h = d10 / 2.0f;
        int j = floor_f_i(d6 - h), k = floor_f_i(d7 - h), l = floor_f_i(d8 - h);
        int i1 = floor_f_i(d6 + h), j1 = floor_f_i(d7 + h), k1 = floor_f_i(d8 + h);
        /* only C's columns and rows can be written */
        if (j < cx0) j = cx0;
        if (i1 > cx1) i1 = cx1;
        if (k < c_y0) k = c_y0;
        if (j1 > c_y0 + c_h - 1) j1 = c_y0 + c_h - 1;
        if (l < cz0) l = cz0;
        if (k1 > cz1) k1 = cz1;
        if (j > i1 || k > j1 || l > k1) continue;
        /* squares of the per-axis distances (the same operations as Java's, hoisted); the
         * writes replace stone only, which leaves C's column tracking as it is */
        float q14[16];
        for (int j2 = l; j2 <= k1; j2++) {
            float d14 = ((float)j2 + 0.5f - d8) / h;
            q14[j2 - l] = d14 * d14;
        }
        for (int l1 = j; l1 <= i1; l1++) {
            float d12 = ((float)l1 + 0.5f - d6) / h, q12 = d12 * d12;
            if (q12 >= 1.0f) continue;
            uint8_t *colp = c_out + ((bx + l1) & 15);
            for (int i2 = k; i2 <= j1; i2++) {
                float d13 = ((float)i2 + 0.5f - d7) / h, q = q12 + d13 * d13;
                if (q >= 1.0f) continue;
                uint8_t *row = colp + (i2 - c_y0) * 256;
                for (int j2 = l; j2 <= k1; j2++)
                    if (q + q14[j2 - l] < 1.0f) {
                        uint8_t *o = row + ((bz + j2) & 15) * 16;
                        if (*o == B_STONE) *o = blk;
                    }
            }
        }
    }
}

static void ores(void) {
    static const struct {
        uint8_t n, size, lo, hi, blk;
    } O[] = {{10, 33, 0, 255, 0}, {8, 33, 0, 255, 1}, {10, 33, 0, 80, 2}, {10, 33, 0, 80, 3},
             {10, 33, 0, 80, 4},  {20, 17, 0, 128, 5}, {20, 9, 0, 64, 6},  {2, 9, 0, 32, 7},
             {8, 8, 0, 16, 8},    {1, 8, 0, 16, 9}};
    const uint8_t blocks[10] = {B_DIRT, B_GRAVEL, B_DIORITE, B_GRANITE, B_ANDESITE,
                                B_COAL_ORE, B_IRON_ORE, B_GOLD_ORE, B_REDSTONE_ORE, B_DIAMOND_ORE};
    for (int t = 0; t < 10; t++) {
        int hi = O[t].hi == 255 ? 256 : O[t].hi;
        for (int c = 0; c < O[t].n; c++) {
            int x = RI(16), y = RI(hi - O[t].lo) + O[t].lo, z = RI(16);
            minable(PK + x, y, PL + z, O[t].size, blocks[O[t].blk]);
        }
    }
    /* lapis: b(1, lapis, 16, 16): y = nextInt(16) + nextInt(16) + 16 - 16 */
    int x = RI(16), y = RI(16) + RI(16), z = RI(16);
    minable(PK + x, y, PL + z, 7, B_LAPIS_ORE);
}

/* ---- disks (WorldGenSand, WorldGenClay) ---- */
static void disk(uint8_t blk, int size, int yr, int clay) {
    int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
    int y = top_solid(x, z);
    uint8_t m = vget(x, y, z);
    if (!(m == B_WATER || m == B_FLOWING_WATER)) return;
    int i = RI(size - 2) + 2;
    for (int a = x - i; a <= x + i; a++)
        for (int b = z - i; b <= z + i; b++) {
            int dx = a - x, dz = b - z;
            if (dx * dx + dz * dz > i * i) continue;
            for (int c = y - yr; c <= y + yr; c++) put_c(a, c, b, blk, clay ? R_DIRTCLAY : R_DIRTGRASS);
        }
}

/* ---- trees ---- */

enum {
    TR_OAK, TR_JUNGLE_SMALL, TR_BIG, TR_BIRCH, TR_BIRCH_TALL, TR_DARK, TR_TAIGA1, TR_TAIGA2, TR_MEGA_SPRUCE,
    TR_MEGA_SPRUCE_H, TR_SWAMP, TR_ACACIA, TR_JUNGLE_MEGA, TR_BUSH
};

/* WorldGenTreeAbstract.a(Block) */
static inline int tree_ok(uint8_t b) {
    return b == B_AIR || IS_LEAVES(b) || b == B_GRASS || IS_DIRTISH(b) || IS_LOG(b) || b == B_VINE ||
           (b >= B_SAPLING_OAK && b <= B_SAPLING_DARK_OAK);
}
#define AIR_OR_LEAVES(b) ((b) == B_AIR || IS_LEAVES(b))
#define TREE_REPL(b) ((b) == B_AIR || IS_LEAVES(b) || IS_RPLANT(b))

/* WorldGenTreeAbstract.a(world, pos): make the ground dirt */
static void tree_dirt(int x, int y, int z) {
    if (!IS_DIRTISH(vget(x, y, z))) put(x, y, z, B_DIRT, R_ALWAYS);
}

/* The current tree's own blocks around its trunk (7x7 columns, 32 high): vine passes and
 * trunk vines read them. 0 = untouched, else block id + 1. */
static uint8_t tov[7 * 7 * 32];
static int tov_on, tov_x, tov_y, tov_z;

static uint8_t tget(int x, int y, int z) {
    if (tov_on) {
        int dx = x - tov_x + 3, dz = z - tov_z + 3, dy = y - tov_y;
        if (dx >= 0 && dx < 7 && dz >= 0 && dz < 7 && dy >= 0 && dy < 32) {
            uint8_t m = tov[(dy * 7 + dz) * 7 + dx];
            if (m) return (uint8_t)(m - 1);
        }
    }
    return vget(x, y, z);
}

static void tput(int x, int y, int z, uint8_t b, int rule) {
    if (tov_on) {
        int dx = x - tov_x + 3, dz = z - tov_z + 3, dy = y - tov_y;
        if (dx >= 0 && dx < 7 && dz >= 0 && dz < 7 && dy >= 0 && dy < 32) tov[(dy * 7 + dz) * 7 + dx] = (uint8_t)(b + 1);
    }
    put(x, y, z, b, rule);
}

static void tov_begin(int x, int y, int z) {
    memset(tov, 0, sizeof tov);
    tov_on = 1;
    tov_x = x;
    tov_y = y;
    tov_z = z;
}

static const int8_t HDIR[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}; /* N, E, S, W */

/* hanging vine (WorldGenTrees.b / WorldGenSwampTree.a) */
static void vine_hang(int x, int y, int z) {
    tput(x, y, z, B_VINE, R_AIR);
    for (int i = 4; --y, tget(x, y, z) == B_AIR && i > 0; --i) tput(x, y, z, B_VINE, R_AIR);
}

/* WorldGenTrees (oak, small jungle): base height c, vines */
/* A tree's space test: every block of the columns at Chebyshev distance d <= rmax around
 * (x, z), from y = lo[d < 2 ? d : 2] up to yhi, must pass (kind 0: air or leaves, 1: tree_ok).
 * The block loops it replaces have no side effects, so the order does not matter; in the
 * view, a column above its top is air but for a plant right above it. */
static int col_pass(uint8_t b, int kind) { return kind ? tree_ok(b) : AIR_OR_LEAVES(b); }

static int space_ok(int x, int z, int rmax, const int lo[3], int yhi, int kind) {
    if (yhi >= 256) return 0;
    View *v = &PV;
    for (int dx = -rmax; dx <= rmax; dx++)
        for (int dz = -rmax; dz <= rmax; dz++) {
            int d = (dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz) ? (dx < 0 ? -dx : dx) : (dz < 0 ? -dz : dz);
            int ya = lo[d < 2 ? d : 2], yb = yhi;
            if (ya > yb) continue;
            int X = x + dx, Z = z + dz, lx = X - v->ox, lz = Z - v->oz;
            lx = lx < 0 ? 0 : lx > 31 ? 31 : lx;
            lz = lz < 0 ? 0 : lz > 31 ? 31 : lz;
            int i = lz * 32 + lx, top = v->top[i];
            if (yb > top) {
                if (v->pl[i] && ya <= top + 1 && !col_pass(v->pl[i], kind)) return 0;
                if (ya > top) continue;
                yb = top;
            }
            for (int y = ya; y <= yb; y++)
                if (!col_pass(vget(X, y, Z), kind)) return 0;
        }
    return 1;
}

static int gen_trees(int x, int y, int z, int c, uint8_t log, uint8_t leaf, int vines) {
    int i = RI(3) + c;
    if (y < 1 || y + i + 1 > 256) return 0;
    {
        int lo[3] = {y, y + 1, y + i - 1}; /* radius 0 at y, 1 above, 2 from y + i - 1 */
        if (!space_ok(x, z, 2, lo, y + 1 + i, 1)) return 0;
    }
    uint8_t below = vget(x, y - 1, z);
    if (!((below == B_GRASS || IS_DIRTISH(below) || below == B_FARMLAND) && y < 256 - i - 1)) return 0;
    if (vines) tov_begin(x, y, z);
    tree_dirt(x, y - 1, z);
    for (int j = y - 3 + i; j <= y + i; j++) {
        int k = j - (y + i), i1 = 1 - k / 2;
        for (int l1 = x - i1; l1 <= x + i1; l1++) {
            int j1 = l1 - x;
            for (int k1 = z - i1; k1 <= z + i1; k1++) {
                int i2 = k1 - z;
                if ((j1 < 0 ? -j1 : j1) != i1 || (i2 < 0 ? -i2 : i2) != i1 || (RI(2) != 0 && k != 0)) {
                    if (TREE_REPL(tget(l1, j, k1))) tput(l1, j, k1, leaf, R_TREE);
                }
            }
        }
    }
    for (int j = 0; j < i; j++) {
        if (!TREE_REPL(tget(x, y + j, z))) continue;
        tput(x, y + j, z, log, R_TREE);
        if (vines && j > 0) {
            if (RI(3) > 0 && tget(x - 1, y + j, z) == B_AIR) tput(x - 1, y + j, z, B_VINE, R_AIR);
            if (RI(3) > 0 && tget(x + 1, y + j, z) == B_AIR) tput(x + 1, y + j, z, B_VINE, R_AIR);
            if (RI(3) > 0 && tget(x, y + j, z - 1) == B_AIR) tput(x, y + j, z - 1, B_VINE, R_AIR);
            if (RI(3) > 0 && tget(x, y + j, z + 1) == B_AIR) tput(x, y + j, z + 1, B_VINE, R_AIR);
        }
    }
    if (vines) {
        for (int j = y - 3 + i; j <= y + i; j++) {
            int k = j - (y + i), i1 = 2 - k / 2;
            for (int j1 = x - i1; j1 <= x + i1; j1++)
                for (int k1 = z - i1; k1 <= z + i1; k1++) {
                    if (!IS_LEAVES(tget(j1, j, k1))) continue;
                    if (RI(4) == 0 && tget(j1 - 1, j, k1) == B_AIR) vine_hang(j1 - 1, j, k1);
                    if (RI(4) == 0 && tget(j1 + 1, j, k1) == B_AIR) vine_hang(j1 + 1, j, k1);
                    if (RI(4) == 0 && tget(j1, j, k1 - 1) == B_AIR) vine_hang(j1, j, k1 - 1);
                    if (RI(4) == 0 && tget(j1, j, k1 + 1) == B_AIR) vine_hang(j1, j, k1 + 1);
                }
        }
        if (RI(5) == 0 && i > 5) {
            for (int j = 0; j < 2; j++)
                for (int d = 0; d < 4; d++)
                    if (RI(4 - j) == 0) {
                        RI(3); /* cocoa age (cocoa is not a NumBlocks block: not placed) */
                    }
        }
        tov_on = 0;
    }
    return 1;
}

/* WorldGenForest (birch, tall birch) */
static int gen_birch(int x, int y, int z, int tall) {
    int i = RI(3) + 5;
    if (tall) i += RI(7);
    if (y < 1 || y + i + 1 > 256) return 0;
    {
        int lo[3] = {y, y + 1, y + i - 1}; /* radius 0 at y, 1 above, 2 from y + i - 1 */
        if (!space_ok(x, z, 2, lo, y + 1 + i, 1)) return 0;
    }
    uint8_t below = vget(x, y - 1, z);
    if (!((below == B_GRASS || IS_DIRTISH(below) || below == B_FARMLAND) && y < 256 - i - 1)) return 0;
    tree_dirt(x, y - 1, z);
    for (int i1 = y - 3 + i; i1 <= y + i; i1++) {
        int j1 = i1 - (y + i), j = 1 - j1 / 2;
        for (int k = x - j; k <= x + j; k++) {
            int k1 = k - x;
            for (int l1 = z - j; l1 <= z + j; l1++) {
                int i2 = l1 - z;
                if ((k1 < 0 ? -k1 : k1) != j || (i2 < 0 ? -i2 : i2) != j || (RI(2) != 0 && j1 != 0))
                    if (AIR_OR_LEAVES(vget(k, i1, l1))) put(k, i1, l1, B_LEAVES_BIRCH, R_AIRLEAF);
            }
        }
    }
    for (int i1 = 0; i1 < i; i1++)
        if (AIR_OR_LEAVES(vget(x, y + i1, z))) put(x, y + i1, z, B_LOG_BIRCH, R_AIRLEAF);
    return 1;
}

static int gen_taiga1(int x, int y, int z) {
    int i = RI(5) + 7, j = i - RI(2) - 3, k = i - j, l = 1 + RI(k + 1);
    if (y < 1 || y + i + 1 > 256) return 0;
    {
        int lo[3] = {y, y + (j > 0 ? j : 0), y + (j > 0 ? j : 0)}; /* radius 0 below y + j, l above */
        if (!space_ok(x, z, l, lo, y + 1 + i, 1)) return 0;
    }
    uint8_t below = vget(x, y - 1, z);
    if (!((below == B_GRASS || IS_DIRTISH(below)) && y < 256 - i - 1)) return 0;
    tree_dirt(x, y - 1, z);
    int k1 = 0;
    for (int i2 = y + i; i2 >= y + j; i2--) {
        for (int i1 = x - k1; i1 <= x + k1; i1++) {
            int j1 = i1 - x;
            for (int j2 = z - k1; j2 <= z + k1; j2++) {
                int k2 = j2 - z;
                if ((j1 < 0 ? -j1 : j1) != k1 || (k2 < 0 ? -k2 : k2) != k1 || k1 <= 0)
                    if (!IS_CUBE(vget(i1, i2, j2))) put(i1, i2, j2, B_LEAVES_SPRUCE, R_NOTCUBE);
            }
        }
        if (k1 >= 1 && i2 == y + j + 1) --k1;
        else if (k1 < l) ++k1;
    }
    for (int i2 = 0; i2 < i - 1; i2++)
        if (AIR_OR_LEAVES(vget(x, y + i2, z))) put(x, y + i2, z, B_LOG_SPRUCE, R_AIRLEAF);
    return 1;
}

static int gen_taiga2(int x, int y, int z) {
    int i = RI(4) + 6, j = 1 + RI(2), k = i - j, l = 2 + RI(2);
    if (y < 1 || y + i + 1 > 256) return 0;
    {
        int lo[3] = {y, y + j, y + j}; /* radius 0 below y + j, l above */
        if (!space_ok(x, z, l, lo, y + 1 + i, 0)) return 0;
    }
    uint8_t below = vget(x, y - 1, z);
    if (!((below == B_GRASS || IS_DIRTISH(below) || below == B_FARMLAND) && y < 256 - i - 1)) return 0;
    tree_dirt(x, y - 1, z);
    int j1 = RI(2), i2 = 1, b0 = 0;
    for (int i1 = 0; i1 <= k; i1++) {
        int j2 = y + i - i1;
        for (int k2 = x - j1; k2 <= x + j1; k2++) {
            int l2 = k2 - x;
            for (int i3 = z - j1; i3 <= z + j1; i3++) {
                int j3 = i3 - z;
                if ((l2 < 0 ? -l2 : l2) != j1 || (j3 < 0 ? -j3 : j3) != j1 || j1 <= 0)
                    if (!IS_CUBE(vget(k2, j2, i3))) put(k2, j2, i3, B_LEAVES_SPRUCE, R_NOTCUBE);
            }
        }
        if (j1 >= i2) {
            j1 = b0;
            b0 = 1;
            ++i2;
            if (i2 > l) i2 = l;
        } else {
            ++j1;
        }
    }
    int i1 = RI(3);
    for (int j2 = 0; j2 < i - i1; j2++)
        if (AIR_OR_LEAVES(vget(x, y + j2, z))) put(x, y + j2, z, B_LOG_SPRUCE, R_AIRLEAF);
    return 1;
}

static int gen_swamp_tree(int x, int y, int z) {
    int i = RI(4) + 5;
    while (IS_WATER(vget(x, y - 1, z))) y--;
    if (y < 1 || y + i + 1 > 256) return 0;
    for (int l = y; l <= y + 1 + i; l++) {
        int b0 = 1;
        if (l == y) b0 = 0;
        if (l >= y + 1 + i - 2) b0 = 3;
        for (int j = x - b0; j <= x + b0; j++)
            for (int k = z - b0; k <= z + b0; k++) {
                if (l < 0 || l >= 256) return 0;
                uint8_t b = vget(j, l, k);
                if (!AIR_OR_LEAVES(b)) {
                    if (!IS_WATER(b)) return 0;
                    if (l > y) return 0;
                }
            }
    }
    uint8_t below = vget(x, y - 1, z);
    if (!((below == B_GRASS || IS_DIRTISH(below)) && y < 256 - i - 1)) return 0;
    tov_begin(x, y, z);
    tree_dirt(x, y - 1, z);
    for (int j1 = y - 3 + i; j1 <= y + i; j1++) {
        int k1 = j1 - (y + i), j = 2 - k1 / 2;
        for (int k = x - j; k <= x + j; k++) {
            int l1 = k - x;
            for (int i1 = z - j; i1 <= z + j; i1++) {
                int i2 = i1 - z;
                if ((l1 < 0 ? -l1 : l1) != j || (i2 < 0 ? -i2 : i2) != j || (RI(2) != 0 && k1 != 0))
                    if (!IS_CUBE(tget(k, j1, i1))) tput(k, j1, i1, B_LEAVES_OAK, R_NOTCUBE);
            }
        }
    }
    for (int j1 = 0; j1 < i; j1++) {
        uint8_t b = tget(x, y + j1, z);
        if (AIR_OR_LEAVES(b) || IS_WATER(b)) tput(x, y + j1, z, B_LOG_OAK, R_ALWAYS);
    }
    for (int j1 = y - 3 + i; j1 <= y + i; j1++) {
        int k1 = j1 - (y + i), j = 2 - k1 / 2;
        for (int l1 = x - j; l1 <= x + j; l1++)
            for (int i1 = z - j; i1 <= z + j; i1++) {
                if (!IS_LEAVES(tget(l1, j1, i1))) continue;
                if (RI(4) == 0 && tget(l1 - 1, j1, i1) == B_AIR) vine_hang(l1 - 1, j1, i1);
                if (RI(4) == 0 && tget(l1 + 1, j1, i1) == B_AIR) vine_hang(l1 + 1, j1, i1);
                if (RI(4) == 0 && tget(l1, j1, i1 - 1) == B_AIR) vine_hang(l1, j1, i1 - 1);
                if (RI(4) == 0 && tget(l1, j1, i1 + 1) == B_AIR) vine_hang(l1, j1, i1 + 1);
            }
    }
    tov_on = 0;
    return 1;
}

static void acacia_leaf(int x, int y, int z) {
    if (AIR_OR_LEAVES(vget(x, y, z))) put(x, y, z, B_LEAVES_ACACIA, R_AIRLEAF);
}

static int gen_acacia(int x, int y, int z) {
    int i = RI(3) + RI(3) + 5;
    if (y < 1 || y + i + 1 > 256) return 0;
    for (int l = y; l <= y + 1 + i; l++) {
        int b0 = 1;
        if (l == y) b0 = 0;
        if (l >= y + 1 + i - 2) b0 = 2;
        for (int j = x - b0; j <= x + b0; j++)
            for (int k = z - b0; k <= z + b0; k++)
                if (l < 0 || l >= 256 || !tree_ok(vget(j, l, k))) return 0;
    }
    uint8_t below = vget(x, y - 1, z);
    if (!((below == B_GRASS || IS_DIRTISH(below)) && y < 256 - i - 1)) return 0;
    tree_dirt(x, y - 1, z);
    int dir = RI(4);
    int i1 = i - RI(4) - 1, j = 3 - RI(3), k = x, j1 = z, k1 = 0;
    for (int i2 = 0; i2 < i; i2++) {
        int l1 = y + i2;
        if (i2 >= i1 && j > 0) {
            k += HDIR[dir][0];
            j1 += HDIR[dir][1];
            --j;
        }
        if (AIR_OR_LEAVES(vget(k, l1, j1))) {
            put(k, l1, j1, B_LOG_ACACIA, R_ALWAYS);
            k1 = l1;
        }
    }
    for (int a = -3; a <= 3; a++)
        for (int b = -3; b <= 3; b++)
            if ((a < 0 ? -a : a) != 3 || (b < 0 ? -b : b) != 3) acacia_leaf(k + a, k1, j1 + b);
    for (int a = -1; a <= 1; a++)
        for (int b = -1; b <= 1; b++) acacia_leaf(k + a, k1 + 1, j1 + b);
    acacia_leaf(k + 2, k1 + 1, j1);
    acacia_leaf(k - 2, k1 + 1, j1);
    acacia_leaf(k, k1 + 1, j1 + 2);
    acacia_leaf(k, k1 + 1, j1 - 2);
    k = x;
    j1 = z;
    int dir1 = RI(4);
    if (dir1 != dir) {
        int l1 = i1 - RI(2) - 1, j2 = 1 + RI(3);
        k1 = 0;
        for (int l2 = l1; l2 < i && j2 > 0; --j2) {
            if (l2 >= 1) {
                int k2 = y + l2;
                k += HDIR[dir1][0];
                j1 += HDIR[dir1][1];
                if (AIR_OR_LEAVES(vget(k, k2, j1))) {
                    put(k, k2, j1, B_LOG_ACACIA, R_ALWAYS);
                    k1 = k2;
                }
            }
            ++l2;
        }
        if (k1 > 0) {
            for (int a = -2; a <= 2; a++)
                for (int b = -2; b <= 2; b++)
                    if ((a < 0 ? -a : a) != 2 || (b < 0 ? -b : b) != 2) acacia_leaf(k + a, k1, j1 + b);
            for (int a = -1; a <= 1; a++)
                for (int b = -1; b <= 1; b++) acacia_leaf(k + a, k1 + 1, j1 + b);
        }
    }
    return 1;
}

static void dark_log(int x, int y, int z) {
    if (tree_ok(vget(x, y, z))) put(x, y, z, B_LOG_DARK_OAK, R_TREEOK);
}
static void dark_leaf(int x, int y, int z) {
    if (vget(x, y, z) == B_AIR) put(x, y, z, B_LEAVES_DARK_OAK, R_AIR);
}

static int gen_dark_oak(int x, int y, int z) {
    int i = RI(3);
    i += RI(2) + 6;
    if (y < 1 || y + i + 1 >= 256) return 0;
    uint8_t below = vget(x, y - 1, z);
    if (below != B_GRASS && !IS_DIRTISH(below)) return 0;
    for (int i1 = 0; i1 <= i + 1; i1++) {
        int b0 = 1;
        if (i1 == 0) b0 = 0;
        if (i1 >= i - 1) b0 = 2;
        for (int j1 = -b0; j1 <= b0; j1++)
            for (int k1 = -b0; k1 <= b0; k1++)
                if (!tree_ok(vget(x + j1, y + i1, z + k1))) return 0;
    }
    tree_dirt(x, y - 1, z);
    tree_dirt(x + 1, y - 1, z);
    tree_dirt(x, y - 1, z + 1);
    tree_dirt(x + 1, y - 1, z + 1);
    int dir = RI(4);
    int i1 = i - RI(4), j1 = 2 - RI(3), k1 = x, l1 = z, i2 = y + i - 1;
    for (int j2 = 0; j2 < i; j2++) {
        if (j2 >= i1 && j1 > 0) {
            k1 += HDIR[dir][0];
            l1 += HDIR[dir][1];
            --j1;
        }
        int k2 = y + j2;
        if (AIR_OR_LEAVES(vget(k1, k2, l1))) {
            dark_log(k1, k2, l1);
            dark_log(k1 + 1, k2, l1);
            dark_log(k1, k2, l1 + 1);
            dark_log(k1 + 1, k2, l1 + 1);
        }
    }
    for (int j2 = -2; j2 <= 0; j2++)
        for (int k2 = -2; k2 <= 0; k2++) {
            dark_leaf(k1 + j2, i2 - 1, l1 + k2);
            dark_leaf(1 + k1 - j2, i2 - 1, l1 + k2);
            dark_leaf(k1 + j2, i2 - 1, 1 + l1 - k2);
            dark_leaf(1 + k1 - j2, i2 - 1, 1 + l1 - k2);
            if ((j2 > -2 || k2 > -1) && (j2 != -1 || k2 != -2)) {
                dark_leaf(k1 + j2, i2 + 1, l1 + k2);
                dark_leaf(1 + k1 - j2, i2 + 1, l1 + k2);
                dark_leaf(k1 + j2, i2 + 1, 1 + l1 - k2);
                dark_leaf(1 + k1 - j2, i2 + 1, 1 + l1 - k2);
            }
        }
    if (jr_bool(&PR)) {
        dark_leaf(k1, i2 + 2, l1);
        dark_leaf(k1 + 1, i2 + 2, l1);
        dark_leaf(k1 + 1, i2 + 2, l1 + 1);
        dark_leaf(k1, i2 + 2, l1 + 1);
    }
    for (int j2 = -3; j2 <= 4; j2++)
        for (int k2 = -3; k2 <= 4; k2++)
            if ((j2 != -3 || k2 != -3) && (j2 != -3 || k2 != 4) && (j2 != 4 || k2 != -3) && (j2 != 4 || k2 != 4) &&
                ((j2 < 0 ? -j2 : j2) < 3 || (k2 < 0 ? -k2 : k2) < 3))
                dark_leaf(k1 + j2, i2, l1 + k2);
    for (int j2 = -1; j2 <= 2; j2++)
        for (int k2 = -1; k2 <= 2; k2++)
            if ((j2 < 0 || j2 > 1 || k2 < 0 || k2 > 1) && RI(3) <= 0) {
                int l2 = RI(3) + 2;
                for (int i3 = 0; i3 < l2; i3++) dark_log(x + j2, i2 - i3 - 1, z + k2);
                for (int i3 = -1; i3 <= 1; i3++)
                    for (int j3 = -1; j3 <= 1; j3++) dark_leaf(k1 + j2 + i3, i2, l1 + k2 + j3);
                for (int i3 = -2; i3 <= 2; i3++)
                    for (int j3 = -2; j3 <= 2; j3++)
                        if ((i3 < 0 ? -i3 : i3) != 2 || (j3 < 0 ? -j3 : j3) != 2) dark_leaf(k1 + j2 + i3, i2 - 1, l1 + k2 + j3);
            }
    return 1;
}

/* WorldGenMegaTreeAbstract helpers */
static int mega_height(int a, int d) {
    int i = RI(3) + a;
    if (d > 1) i += RI(d);
    return i;
}

static int mega_check(int x, int y, int z, int i) {
    if (y < 1 || y + i + 1 > 256) return 0;
    for (int j = 0; j <= 1 + i; j++) {
        int b0 = 2;
        if (j == 0) b0 = 1;
        else if (j >= 1 + i - 2) b0 = 2;
        for (int k = -b0; k <= b0; k++)
            for (int l = -b0; l <= b0; l++) {
                if (y + j < 0 || y + j >= 256) return 0;
                if (!tree_ok(vget(x + k, y + j, z + l))) return 0;
            }
    }
    uint8_t below = vget(x, y - 1, z);
    if (!((below == B_GRASS || IS_DIRTISH(below)) && y >= 2)) return 0;
    tree_dirt(x, y - 1, z);
    tree_dirt(x + 1, y - 1, z);
    tree_dirt(x, y - 1, z + 1);
    tree_dirt(x + 1, y - 1, z + 1);
    return 1;
}

static void mega_leaves2(int x, int y, int z, int r, uint8_t leaf) { /* 2x2-centred blob */
    int j = r * r;
    for (int k = -r; k <= r + 1; k++)
        for (int l = -r; l <= r + 1; l++) {
            int i1 = k - 1, j1 = l - 1;
            if (k * k + l * l <= j || i1 * i1 + j1 * j1 <= j || k * k + j1 * j1 <= j || i1 * i1 + l * l <= j)
                if (AIR_OR_LEAVES(vget(x + k, y, z + l))) put(x + k, y, z + l, leaf, R_AIRLEAF);
        }
}

static void mega_leaves1(int x, int y, int z, int r, uint8_t leaf) { /* disc */
    int j = r * r;
    for (int k = -r; k <= r; k++)
        for (int l = -r; l <= r; l++)
            if (k * k + l * l <= j && AIR_OR_LEAVES(vget(x + k, y, z + l))) put(x + k, y, z + l, leaf, R_AIRLEAF);
}

static int gen_mega_spruce(int x, int y, int z, int h) {
    int i = mega_height(13, 15);
    if (!mega_check(x, y, z, i)) return 0;
    /* crown */
    int k = y + i;
    int i1 = RI(5) + (h ? 13 : 3), j1 = 0;
    for (int k1 = k - i1; k1 <= k; k1++) {
        int l1 = k - k1;
        int i2 = floor_f((float)l1 / (float)i1 * 3.5f);
        mega_leaves2(x, k1, z, i2 + (l1 > 0 && i2 == j1 && (k1 & 1) == 0 ? 1 : 0), B_LEAVES_SPRUCE);
        j1 = i2;
    }
    for (int j = 0; j < i; j++) {
        if (AIR_OR_LEAVES(vget(x, y + j, z))) put(x, y + j, z, B_LOG_SPRUCE, R_AIRLEAF);
        if (j < i - 1) {
            if (AIR_OR_LEAVES(vget(x + 1, y + j, z))) put(x + 1, y + j, z, B_LOG_SPRUCE, R_AIRLEAF);
            if (AIR_OR_LEAVES(vget(x + 1, y + j, z + 1))) put(x + 1, y + j, z + 1, B_LOG_SPRUCE, R_AIRLEAF);
            if (AIR_OR_LEAVES(vget(x, y + j, z + 1))) put(x, y + j, z + 1, B_LOG_SPRUCE, R_AIRLEAF);
        }
    }
    return 1;
}

static void podzol_col(int x, int y, int z) {
    for (int i = 2; i >= -3; i--) {
        uint8_t b = vget(x, y + i, z);
        if (b == B_GRASS || IS_DIRTISH(b)) {
            put(x, y + i, z, B_PODZOL, R_DIRTGRASS);
            break;
        }
        if (b != B_AIR && i < 0) break;
    }
}

static void podzol_patch(int x, int y, int z) {
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++)
            if ((i < 0 ? -i : i) != 2 || (j < 0 ? -j : j) != 2) podzol_col(x + i, y, z + j);
}

static void mega_spruce_post(int x, int y, int z) {
    podzol_patch(x - 1, y, z - 1);
    podzol_patch(x + 2, y, z - 1);
    podzol_patch(x - 1, y, z + 2);
    podzol_patch(x + 2, y, z + 2);
    for (int i = 0; i < 5; i++) {
        int j = RI(64), k = j % 8, l = j / 8;
        if (k == 0 || k == 7 || l == 0 || l == 7) podzol_patch(x - 3 + k, y, z - 3 + l);
    }
}

static void jvine(int x, int y, int z) {
    if (RI(3) > 0 && vget(x, y, z) == B_AIR) put(x, y, z, B_VINE, R_AIR);
}

static int gen_jungle_mega(int x, int y, int z) {
    int i = mega_height(10, 20);
    if (!mega_check(x, y, z, i)) return 0;
    for (int j = -2; j <= 0; j++) mega_leaves2(x, y + i + j, z, 2 + 1 - j, B_LEAVES_JUNGLE);
    for (int j = y + i - 2 - RI(4); j > y + i / 2; j -= 2 + RI(4)) {
        float f = RF() * 3.1415927f * 2.0f;
        int k = x + (int)(0.5f + mh_cos(f) * 4.0f), l = z + (int)(0.5f + mh_sin(f) * 4.0f);
        for (int i1 = 0; i1 < 5; i1++) {
            k = x + (int)(1.5f + mh_cos(f) * (float)i1);
            l = z + (int)(1.5f + mh_sin(f) * (float)i1);
            put(k, j - 3 + i1 / 2, l, B_LOG_JUNGLE, R_ALWAYS);
        }
        int i1 = 1 + RI(2), j1 = j;
        for (int k1 = j - i1; k1 <= j1; k1++) mega_leaves1(k, k1, l, 1 - (k1 - j1), B_LEAVES_JUNGLE);
    }
    for (int i2 = 0; i2 < i; i2++) {
        int yy = y + i2;
        if (tree_ok(vget(x, yy, z))) {
            put(x, yy, z, B_LOG_JUNGLE, R_TREEOK);
            if (i2 > 0) {
                jvine(x - 1, yy, z);
                jvine(x, yy, z - 1);
            }
        }
        if (i2 < i - 1) {
            if (tree_ok(vget(x + 1, yy, z))) {
                put(x + 1, yy, z, B_LOG_JUNGLE, R_TREEOK);
                if (i2 > 0) {
                    jvine(x + 2, yy, z);
                    jvine(x + 1, yy, z - 1);
                }
            }
            if (tree_ok(vget(x + 1, yy, z + 1))) {
                put(x + 1, yy, z + 1, B_LOG_JUNGLE, R_TREEOK);
                if (i2 > 0) {
                    jvine(x + 2, yy, z + 1);
                    jvine(x + 1, yy, z + 2);
                }
            }
            if (tree_ok(vget(x, yy, z + 1))) {
                put(x, yy, z + 1, B_LOG_JUNGLE, R_TREEOK);
                if (i2 > 0) {
                    jvine(x - 1, yy, z + 1);
                    jvine(x, yy, z + 2);
                }
            }
        }
    }
    return 1;
}

static int gen_bush(int x, int y, int z) {
    uint8_t b;
    while ((b = vget(x, y, z), AIR_OR_LEAVES(b)) && y > 0) y--;
    b = vget(x, y, z);
    if (!(IS_DIRTISH(b) || b == B_GRASS)) return 0;
    y++;
    put(x, y, z, B_LOG_JUNGLE, R_ALWAYS);
    for (int i = y; i <= y + 2; i++) {
        int j = i - y, k = 2 - j;
        for (int l = x - k; l <= x + k; l++) {
            int i1 = l - x;
            for (int j1 = z - k; j1 <= z + k; j1++) {
                int k1 = j1 - z;
                if ((i1 < 0 ? -i1 : i1) != k || (k1 < 0 ? -k1 : k1) != k || RI(2) != 0)
                    if (!IS_CUBE(vget(l, i, j1))) put(l, i, j1, B_LEAVES_OAK, R_NOTCUBE);
            }
        }
    }
    return 1;
}

/* ---- WorldGenBigTree (own java.util.Random; float instead of double) ---- */

/* line from a to b: -1 if every block is replaceable for a tree, else the index of the first that is not */
static int big_line_check(int ax, int ay, int az, int bx, int by, int bz) {
    int dx = bx - ax, dy = by - ay, dz = bz - az;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, adz = dz < 0 ? -dz : dz;
    int i = adz > adx && adz > ady ? adz : (ady > adx ? ady : adx);
    if (i == 0) return -1;
    float f = (float)dx / (float)i, f1 = (float)dy / (float)i, f2 = (float)dz / (float)i;
    for (int j = 0; j <= i; j++) {
        int px = ax + floor_f(0.5f + (float)j * f), py = ay + floor_f(0.5f + (float)j * f1),
            pz = az + floor_f(0.5f + (float)j * f2);
        if (!tree_ok(vget(px, py, pz))) return j;
    }
    return -1;
}

static void big_line_place(int ax, int ay, int az, int bx, int by, int bz) {
    int dx = bx - ax, dy = by - ay, dz = bz - az;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, adz = dz < 0 ? -dz : dz;
    int i = adz > adx && adz > ady ? adz : (ady > adx ? ady : adx);
    float f = (float)dx / (float)i, f1 = (float)dy / (float)i, f2 = (float)dz / (float)i;
    for (int j = 0; j <= i; j++) {
        int px = ax + floor_f(0.5f + (float)j * f), py = ay + floor_f(0.5f + (float)j * f1),
            pz = az + floor_f(0.5f + (float)j * f2);
        int ex = px - ax < 0 ? ax - px : px - ax, ez = pz - az < 0 ? az - pz : pz - az, m = ex > ez ? ex : ez;
        uint8_t b = B_LOG_OAK;
        if (m > 0) b = ex == m ? B_LOG_OAK_X : B_LOG_OAK_Z;
        put(px, py, pz, b, R_ALWAYS);
    }
}

static int gen_big_tree(int x, int y, int z) {
    JRand k;
    jr_seed(&k, jr_long(&PR));
    int a = 5 + jr_int(&k, 12); /* height (fresh each tree, see the deviations) */
    const int li = 5;           /* leaf distance (e() sets 5) */
    uint8_t below = vget(x, y - 1, z);
    if (!(IS_DIRTISH(below) || below == B_GRASS || below == B_FARMLAND)) return 0;
    int c = big_line_check(x, y, z, x, y + a - 1, z);
    if (c != -1) {
        if (c < 6) return 0;
        a = c;
    }
    /* prepare */
    int b = (int)((double)a * ((double)618 / 1000)); /* 0.618 */
    if (b >= a) b = a - 1;
    double pw = (double)a / 13;
    int ni = (int)((double)1382 / 1000 + pw * pw); /* 1.382 */
    if (ni < 1) ni = 1;
    int j = y + b, kk = a - li, n = 0;
    big_list[n++] = (BigPos){x, y + kk, z, j};
    for (; kk >= 0; --kk) {
        float fs; /* WorldGenBigTree.a(int): crown radius at this layer */
        if ((float)kk < (float)a * 0.3f) {
            fs = -1.0f;
        } else {
            float f = (float)a / 2.0f, f1 = f - (float)kk;
            if (f1 == 0.0f) fs = f * 0.5f;
            else if ((f1 < 0 ? -f1 : f1) >= f) fs = 0.0f;
            else fs = sqrtf(f * f - f1 * f1) * 0.5f;
        }
        if (fs < 0.0f) continue;
        for (int l = 0; l < ni; l++) {
            float d0 = fs * (jr_float(&k) + 0.328f);
            float d1 = jr_float(&k) * 2.0f * 3.14159265f;
            float d2 = d0 * sinf(d1) + 0.5f, d3 = d0 * cosf(d1) + 0.5f;
            int px = x + floor_f(d2), py = y + kk - 1, pz = z + floor_f(d3);
            if (big_line_check(px, py, pz, px, py + li, pz) == -1) {
                int i1 = x - px, j1 = z - pz;
                float d4 = (float)py - sqrtf((float)(i1 * i1 + j1 * j1)) * 0.381f;
                int k1 = d4 > (float)j ? j : (int)d4;
                if (big_line_check(x, k1, z, px, py, pz) == -1 && n < 48) big_list[n++] = (BigPos){px, py, pz, k1};
            }
        }
    }
    /* leaves */
    for (int p = 0; p < n; p++)
        for (int i = 0; i < li; i++) {
            int f = (i != 0 && i != li - 1) ? 3 : 2, r = (int)((float)f + 0.618f);
            for (int dx = -r; dx <= r; dx++)
                for (int dz = -r; dz <= r; dz++) {
                    int ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
                    if ((2 * ax + 1) * (2 * ax + 1) + (2 * az + 1) * (2 * az + 1) <= 4 * f * f) {
                        int px = big_list[p].x + dx, py = big_list[p].y + i, pz = big_list[p].z + dz;
                        if (AIR_OR_LEAVES(vget(px, py, pz))) put(px, py, pz, B_LEAVES_OAK, R_AIRLEAF);
                    }
                }
        }
    /* trunk */
    big_line_place(x, y, z, x, y + b, z);
    /* branches */
    for (int p = 0; p < n; p++) {
        int q = big_list[p].q;
        if ((q != big_list[p].y || x != big_list[p].x || z != big_list[p].z) && (double)(q - y) >= (double)a * ((double)2 / 10))
            big_line_place(x, q, z, big_list[p].x, big_list[p].y, big_list[p].z);
    }
    return 1;
}

/* ---- huge mushrooms ---- */
static int gen_huge_mushroom(int x, int y, int z) {
    int red = !jr_bool(&PR); /* nextBoolean ? brown : red */
    int i = RI(3) + 4;
    if (y < 1 || y + i + 1 >= 256) return 0;
    for (int l = y; l <= y + 1 + i; l++) {
        int b0 = 3;
        if (l <= y + 3) b0 = 0;
        for (int j = x - b0; j <= x + b0; j++)
            for (int k = z - b0; k <= z + b0; k++) {
                if (l < 0 || l >= 256) return 0;
                if (!AIR_OR_LEAVES(vget(j, l, k))) return 0;
            }
    }
    uint8_t below = vget(x, y - 1, z);
    if (!(IS_DIRTISH(below) || below == B_GRASS || below == B_MYCELIUM)) return 0;
    uint8_t cap = red ? B_RED_MUSHROOM_CAP : B_BROWN_MUSHROOM_CAP, stem = red ? B_RED_MUSHROOM_STEM : B_BROWN_MUSHROOM_STEM;
    int i1 = y + i;
    if (red) i1 = y + i - 3;
    for (int j1 = i1; j1 <= y + i; j1++) {
        int j = 1;
        if (j1 < y + i) ++j;
        if (!red) j = 3;
        int k = x - j, k1 = x + j, l1 = z - j, i2 = z + j;
        for (int j2 = k; j2 <= k1; j2++)
            for (int k2 = l1; k2 <= i2; k2++) {
                int l2 = 5;
                if (j2 == k) --l2;
                else if (j2 == k1) ++l2;
                if (k2 == l1) l2 -= 3;
                else if (k2 == i2) l2 += 3;
                if (!red || j1 < y + i) {
                    if ((j2 == k || j2 == k1) && (k2 == l1 || k2 == i2)) continue;
                    if (j2 == x - (j - 1) && k2 == l1) l2 = 1;
                    if (j2 == k && k2 == z - (j - 1)) l2 = 1;
                    if (j2 == x + (j - 1) && k2 == l1) l2 = 3;
                    if (j2 == k1 && k2 == z - (j - 1)) l2 = 3;
                    if (j2 == x - (j - 1) && k2 == i2) l2 = 7;
                    if (j2 == k && k2 == z + (j - 1)) l2 = 7;
                    if (j2 == x + (j - 1) && k2 == i2) l2 = 9;
                    if (j2 == k1 && k2 == z + (j - 1)) l2 = 9;
                }
                if (l2 == 5 && j1 < y + i) l2 = 0; /* ALL_INSIDE */
                if (l2 != 0 && !IS_CUBE(vget(j2, j1, k2))) put(j2, j1, k2, cap, R_NOTCUBE);
            }
    }
    for (int j1 = 0; j1 < i; j1++)
        if (!IS_CUBE(vget(x, y + j1, z))) put(x, y + j1, z, stem, R_NOTCUBE);
    return 1;
}

/* selection of a tree generator (biome.a(Random)) */
typedef struct {
    uint8_t type, c;
} TreeSel;

static TreeSel tree_select(const Biome *b) {
    TreeSel t = {TR_OAK, 4};
    switch (b->tree) {
    case T_DEFAULT: t.type = RI(10) == 0 ? TR_BIG : TR_OAK; break;
    case T_HILLS: t.type = RI(3) > 0 ? TR_TAIGA2 : (RI(10) == 0 ? TR_BIG : TR_OAK); break;
    case T_FOREST0:
    case T_FOREST1: t.type = RI(5) != 0 ? TR_OAK : TR_BIRCH; break;
    case T_BIRCH: t.type = TR_BIRCH; break;
    case T_ROOFED: t.type = RI(3) > 0 ? TR_DARK : (RI(5) != 0 ? TR_OAK : TR_BIRCH); break;
    case T_BIRCH_SUB: t.type = jr_bool(&PR) ? TR_BIRCH_TALL : TR_BIRCH; break;
    case T_TAIGA0: t.type = RI(3) == 0 ? TR_TAIGA1 : TR_TAIGA2; break;
    case T_TAIGA1:
        if (RI(3) == 0) t.type = RI(13) != 0 ? TR_MEGA_SPRUCE : TR_MEGA_SPRUCE_H;
        else t.type = RI(3) == 0 ? TR_TAIGA1 : TR_TAIGA2;
        break;
    case T_TAIGA2:
        if (RI(3) == 0) t.type = TR_MEGA_SPRUCE_H;
        else t.type = RI(3) == 0 ? TR_TAIGA1 : TR_TAIGA2;
        break;
    case T_SWAMP: t.type = TR_SWAMP; break;
    case T_ICE: t.type = TR_TAIGA2; break;
    case T_JUNGLE:
    case T_JUNGLE_EDGE:
        if (RI(10) == 0) t.type = TR_BIG;
        else if (RI(2) == 0) t.type = TR_BUSH;
        else if (b->tree == T_JUNGLE && RI(3) == 0) t.type = TR_JUNGLE_MEGA;
        else {
            t.type = TR_JUNGLE_SMALL;
            t.c = (uint8_t)(4 + RI(7));
        }
        break;
    case T_SAVANNA: t.type = RI(5) > 0 ? TR_ACACIA : TR_OAK; break;
    case T_MESA: t.type = TR_OAK; break;
    }
    return t;
}

/* generate() then, on success, the post step (podzol of mega spruces) */
static void tree_grow(TreeSel t, int x, int y, int z) {
    int ok = 0;
    switch (t.type) {
    case TR_OAK: ok = gen_trees(x, y, z, 4, B_LOG_OAK, B_LEAVES_OAK, 0); break;
    case TR_JUNGLE_SMALL: ok = gen_trees(x, y, z, t.c, B_LOG_JUNGLE, B_LEAVES_JUNGLE, 1); break;
    case TR_BIG: ok = gen_big_tree(x, y, z); break;
    case TR_BIRCH: ok = gen_birch(x, y, z, 0); break;
    case TR_BIRCH_TALL: ok = gen_birch(x, y, z, 1); break;
    case TR_DARK: ok = gen_dark_oak(x, y, z); break;
    case TR_TAIGA1: ok = gen_taiga1(x, y, z); break;
    case TR_TAIGA2: ok = gen_taiga2(x, y, z); break;
    case TR_MEGA_SPRUCE: ok = gen_mega_spruce(x, y, z, 0); break;
    case TR_MEGA_SPRUCE_H: ok = gen_mega_spruce(x, y, z, 1); break;
    case TR_SWAMP: ok = gen_swamp_tree(x, y, z); break;
    case TR_ACACIA: ok = gen_acacia(x, y, z); break;
    case TR_JUNGLE_MEGA: ok = gen_jungle_mega(x, y, z); break;
    case TR_BUSH: ok = gen_bush(x, y, z); break;
    }
    if (ok && (t.type == TR_MEGA_SPRUCE || t.type == TR_MEGA_SPRUCE_H)) mega_spruce_post(x, y, z);
}

/* ---- plants ---- */
enum { SOIL_PLANT, SOIL_DEADBUSH, SOIL_LILY, SOIL_MUSH, SOIL_GRASS, SOIL_ANY };

static inline int is_replaceable(uint8_t b) {
    return b == B_AIR || IS_RPLANT(b) || b == B_SNOW_LAYER || IS_WATER(b) || b == B_LAVA || b == B_FLOWING_LAVA;
}

/* can a plant of this soil kind stay at (x, y, z) given the block below and above, using rd() */
static int soil_ok(int soil, uint8_t below, int shaded) {
    switch (soil) {
    case SOIL_PLANT: return below == B_GRASS || IS_DIRTISH(below) || below == B_FARMLAND;
    case SOIL_DEADBUSH: return below == B_SAND || below == B_RED_SAND || IS_CLAY(below) || IS_DIRTISH(below);
    case SOIL_LILY: return below == B_WATER;
    case SOIL_MUSH: return below == B_MYCELIUM || below == B_PODZOL || (shaded && IS_CUBE(below));
    case SOIL_GRASS: return below == B_GRASS;
    }
    return 1;
}

/* a plant placed after a view decision: into the view; into C if C agrees */
static void put_plant(int x, int y, int z, uint8_t b, int soil) {
    if (in_c_range(x, y, z)) {
        uint8_t cur = c_out[(y - c_y0) * 256 + (z - c_cz * 16) * 16 + (x - c_cx * 16)];
        int shaded = y < c_top[(z & 15) * 16 + (x & 15)];
        uint8_t below = cget(x, y - 1, z);
        if (cur != B_AIR || !soil_ok(soil, below, shaded)) {
            /* only the view */
            put_skip_c = 1;
            put(x, y, z, b, R_ALWAYS);
            put_skip_c = 0;
            return;
        }
    }
    put(x, y, z, b, R_ALWAYS);
}

static inline int v_shaded(int x, int y, int z) { return y < hm(x, z) - 1; }

/* the 6-draw offset of most plant features */
/* the 6-draw offset of most plant features: nextInt(8) - nextInt(8), nextInt(4) - nextInt(4),
 * nextInt(8) - nextInt(8), drawn left to right (nextInt(2^k) is next(31) >> (31 - k)) */
HOT static void ofs8(int *dx, int *dy, int *dz) {
    int a = jr_next_i(&PR, 31) >> 28;
    *dx = a - (jr_next_i(&PR, 31) >> 28);
    a = jr_next_i(&PR, 31) >> 29;
    *dy = a - (jr_next_i(&PR, 31) >> 29);
    a = jr_next_i(&PR, 31) >> 28;
    *dz = a - (jr_next_i(&PR, 31) >> 28);
}
#define OFS8(dx, dy, dz) ofs8(&(dx), &(dy), &(dz))

/* v_empty(x, y, z) && soil_ok(soil, vget(x, y - 1, z), 0), soil not SOIL_ANY: in the view a
 * spot more than one block above its column's top is air over air, which no soil allows */
HOT static int plant_ok(int x, int y, int z, int soil) {
    View *v = &PV;
    int lx = x - v->ox, lz = z - v->oz;
    lx = lx < 0 ? 0 : lx > 31 ? 31 : lx;
    lz = lz < 0 ? 0 : lz > 31 ? 31 : lz;
    int i = lz * 32 + lx, top = v->top[i];
    if (y > top + 1) return 0;
    if (y == top + 1) return !v->pl[i] && soil_ok(soil, v->tb[i], 0);
    return v_empty(x, y, z) && soil_ok(soil, vget(x, y - 1, z), 0);
}

static void feat_flowers(int X, int Y, int Z, uint8_t b) {
    for (int i = 0; i < 64; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (plant_ok(x, y, z, SOIL_PLANT)) put_plant(x, y, z, b, SOIL_PLANT);
    }
}

static void scan_down(int X, int *Y, int Z) {
    uint8_t b;
    while ((b = vget(X, *Y, Z), AIR_OR_LEAVES(b)) && *Y > 0) (*Y)--;
}

static void feat_grass(int X, int Y, int Z, uint8_t b) {
    scan_down(X, &Y, Z);
    for (int i = 0; i < 128; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (plant_ok(x, y, z, SOIL_PLANT)) put_plant(x, y, z, b, SOIL_PLANT);
    }
}

static void feat_deadbush(int X, int Y, int Z) {
    scan_down(X, &Y, Z);
    for (int i = 0; i < 4; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (plant_ok(x, y, z, SOIL_DEADBUSH)) put_plant(x, y, z, B_DEAD_BUSH, SOIL_DEADBUSH);
    }
}

static void feat_lily(int X, int Y, int Z) {
    for (int i = 0; i < 10; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (plant_ok(x, y, z, SOIL_LILY)) put_plant(x, y, z, B_LILY_PAD, SOIL_LILY);
    }
}

static void feat_mushrooms(int X, int Y, int Z, uint8_t b) {
    for (int i = 0; i < 64; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (v_empty(x, y, z) && y < 255 && soil_ok(SOIL_MUSH, vget(x, y - 1, z), v_shaded(x, y, z)))
            put_plant(x, y, z, b, SOIL_MUSH);
    }
}

static int water_around(int x, int y, int z) {
    return IS_WATER(vget(x - 1, y, z)) || IS_WATER(vget(x + 1, y, z)) || IS_WATER(vget(x, y, z - 1)) ||
           IS_WATER(vget(x, y, z + 1));
}

static int reed_can(int x, int y, int z) {
    uint8_t b = vget(x, y - 1, z);
    if (b == B_SUGAR_CANE) return 1;
    if (b != B_GRASS && !IS_DIRTISH(b) && b != B_SAND && b != B_RED_SAND) return 0;
    return water_around(x, y - 1, z);
}

static void feat_reeds(int X, int Y, int Z) {
    for (int i = 0; i < 20; i++) {
        int dx = RI(4), dz;
        dx -= RI(4);
        dz = RI(4), dz -= RI(4);
        int x = X + dx, y = Y, z = Z + dz;
        if (!v_empty(x, y, z)) continue;
        if (!water_around(x, y - 1, z)) continue;
        int j = 2 + RI(RI(3) + 1);
        int can = reed_can(x, y, z);
        for (int k = 0; k < j; k++)
            if (can) put(x, y + k, z, B_SUGAR_CANE, R_AIR);
    }
}

static void feat_pumpkin(int X, int Y, int Z) {
    for (int i = 0; i < 64; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (v_empty(x, y, z) && vget(x, y - 1, z) == B_GRASS) {
            put_plant(x, y, z, B_PUMPKIN, SOIL_GRASS);
            RI(4); /* facing */
        }
    }
}

static int cactus_can(int x, int y, int z) {
    if (IS_BUILD(vget(x, y, z - 1)) || IS_BUILD(vget(x + 1, y, z)) || IS_BUILD(vget(x, y, z + 1)) ||
        IS_BUILD(vget(x - 1, y, z)))
        return 0;
    uint8_t b = vget(x, y - 1, z);
    return b == B_CACTUS || b == B_SAND || b == B_RED_SAND;
}

static void feat_cactus(int X, int Y, int Z) {
    for (int i = 0; i < 10; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (!v_empty(x, y, z)) continue;
        int j = 1 + RI(RI(3) + 1);
        int can = cactus_can(x, y, z);
        for (int k = 0; k < j; k++)
            if (can) put(x, y + k, z, B_CACTUS, R_AIR);
    }
}

static void feat_melon(int X, int Y, int Z) {
    for (int i = 0; i < 64; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (is_replaceable(vget(x, y, z)) && vget(x, y - 1, z) == B_GRASS) put(x, y, z, B_MELON, R_ALWAYS);
    }
}

/* WorldGenTallPlant: returns whether one was placed */
static int feat_tallplant(int X, int Y, int Z, uint8_t lower) {
    int placed = 0;
    for (int i = 0; i < 64; i++) {
        int dx, dy, dz;
        OFS8(dx, dy, dz);
        int x = X + dx, y = Y + dy, z = Z + dz;
        if (plant_ok(x, y, z, SOIL_PLANT) && v_empty(x, y + 1, z)) {
            int cok = 1;
            if (in_c_cols(x, z)) {
                if (y >= c_y0 && y < c_y0 + c_h && cget(x, y, z) != B_AIR) cok = 0;
                if (y + 1 >= c_y0 && y + 1 < c_y0 + c_h && cget(x, y + 1, z) != B_AIR) cok = 0;
                if (!soil_ok(SOIL_PLANT, cget(x, y - 1, z), 0)) cok = 0;
            }
            if (cok) {
                put(x, y, z, lower, R_ALWAYS);
                put(x, y + 1, z, (uint8_t)(lower + 1), R_ALWAYS);
            } else {
                put_skip_c = 1;
                put(x, y, z, lower, R_ALWAYS);
                put_skip_c = 0;
            }
            placed = 1;
        }
    }
    return placed;
}

/* ---- biome specials ---- */
static void feat_desert_well(int X, int Y, int Z) {
    while (v_empty(X, Y, Z) && Y > 2) Y--;
    if (vget(X, Y, Z) != B_SAND) return;
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++)
            if (v_empty(X + i, Y - 1, Z + j) && v_empty(X + i, Y - 2, Z + j)) return;
    for (int i = -1; i <= 0; i++)
        for (int j = -2; j <= 2; j++)
            for (int k = -2; k <= 2; k++) put(X + j, Y + i, Z + k, B_SANDSTONE, R_ALWAYS);
    put(X, Y, Z, B_WATER, R_ALWAYS);
    for (int d = 0; d < 4; d++) put(X + HDIR[d][0], Y, Z + HDIR[d][1], B_WATER, R_ALWAYS);
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++)
            if (i == -2 || i == 2 || j == -2 || j == 2) put(X + i, Y + 1, Z + j, B_SANDSTONE, R_ALWAYS);
    put(X + 2, Y + 1, Z, B_SANDSTONE_SLAB, R_ALWAYS);
    put(X - 2, Y + 1, Z, B_SANDSTONE_SLAB, R_ALWAYS);
    put(X, Y + 1, Z + 2, B_SANDSTONE_SLAB, R_ALWAYS);
    put(X, Y + 1, Z - 2, B_SANDSTONE_SLAB, R_ALWAYS);
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++) put(X + i, Y + 4, Z + j, (i == 0 && j == 0) ? B_SANDSTONE : B_SANDSTONE_SLAB, R_ALWAYS);
    for (int i = 1; i <= 3; i++) {
        put(X - 1, Y + i, Z - 1, B_SANDSTONE, R_ALWAYS);
        put(X - 1, Y + i, Z + 1, B_SANDSTONE, R_ALWAYS);
        put(X + 1, Y + i, Z - 1, B_SANDSTONE, R_ALWAYS);
        put(X + 1, Y + i, Z + 1, B_SANDSTONE, R_ALWAYS);
    }
}

static void feat_ice_disk(int X, int Y, int Z) { /* WorldGenPackedIce1(4) */
    while (v_empty(X, Y, Z) && Y > 2) Y--;
    if (vget(X, Y, Z) != B_SNOW) return;
    int i = RI(4 - 2) + 2;
    for (int a = X - i; a <= X + i; a++)
        for (int b = Z - i; b <= Z + i; b++) {
            int dx = a - X, dz = b - Z;
            if (dx * dx + dz * dz > i * i) continue;
            for (int y = Y - 1; y <= Y + 1; y++) {
                uint8_t m = vget(a, y, b);
                if (IS_DIRTISH(m) || m == B_SNOW || m == B_ICE) put(a, y, b, B_PACKED_ICE, R_ICE);
            }
        }
}

static void feat_ice_spike(int X, int Y, int Z) { /* WorldGenPackedIce2 */
    while (v_empty(X, Y, Z) && Y > 2) Y--;
    if (vget(X, Y, Z) != B_SNOW) return;
    Y += RI(4);
    int i = RI(4) + 7, j = i / 4 + RI(2);
    if (j > 1 && RI(60) == 0) Y += 10 + RI(30);
    for (int k = 0; k < i; k++) {
        float f = (1.0f - (float)k / (float)i) * (float)j;
        int l = (int)f;
        if (f > (float)l) l++; /* MathHelper.f: ceil */
        for (int i1 = -l; i1 <= l; i1++) {
            float f1 = (float)(i1 < 0 ? -i1 : i1) - 0.25f;
            for (int j1 = -l; j1 <= l; j1++) {
                float f2 = (float)(j1 < 0 ? -j1 : j1) - 0.25f;
                if (((i1 == 0 && j1 == 0) || f1 * f1 + f2 * f2 <= f * f) &&
                    ((i1 != -l && i1 != l && j1 != -l && j1 != l) || RF() <= 0.75f)) {
                    uint8_t b = vget(X + i1, Y + k, Z + j1);
                    if (b == B_AIR || IS_DIRTISH(b) || b == B_SNOW || b == B_ICE) put(X + i1, Y + k, Z + j1, B_PACKED_ICE, R_ICE);
                    if (k != 0 && l > 1) {
                        b = vget(X + i1, Y - k, Z + j1);
                        if (b == B_AIR || IS_DIRTISH(b) || b == B_SNOW || b == B_ICE) put(X + i1, Y - k, Z + j1, B_PACKED_ICE, R_ICE);
                    }
                }
            }
        }
    }
    int k = j - 1;
    if (k < 0) k = 0;
    else if (k > 1) k = 1;
    for (int k1 = -k; k1 <= k; k1++)
        for (int l = -k; l <= k;) {
            int y = Y - 1, l1 = 50;
            if ((k1 < 0 ? -k1 : k1) == 1 && (l < 0 ? -l : l) == 1) l1 = RI(5);
            while (y > 50) {
                uint8_t b = vget(X + k1, y, Z + l);
                if (!(b == B_AIR || IS_DIRTISH(b) || b == B_SNOW || b == B_ICE || b == B_PACKED_ICE)) break;
                put(X + k1, y, Z + l, B_PACKED_ICE, R_ICE2);
                y--;
                --l1;
                if (l1 <= 0) {
                    y -= RI(5) + 1;
                    l1 = RI(5);
                }
            }
            ++l;
        }
}

static void feat_boulder(int X, int Y, int Z) { /* WorldGenTaigaStructure(mossy cobblestone, 0) */
    for (;;) {
        if (Y > 3) {
            if (!v_empty(X, Y - 1, Z)) {
                uint8_t b = vget(X, Y - 1, Z);
                if (b == B_GRASS || IS_DIRTISH(b) || b == B_STONE) break;
            }
            Y--;
            continue;
        }
        break;
    }
    if (Y <= 3) return;
    int i = 0;
    for (int j = 0; i >= 0 && j < 3; ++j) {
        int k = i + RI(2), l = i + RI(2), i1 = i + RI(2);
        float f = (float)(k + l + i1) * 0.333f + 0.5f;
        for (int a = -k; a <= k; a++)
            for (int b = -l; b <= l; b++)
                for (int c = -i1; c <= i1; c++)
                    if ((float)(a * a + b * b + c * c) <= f * f) put(X + a, Y + b, Z + c, B_MOSSY_COBBLESTONE, R_ALWAYS);
        X += -(i + 1) + RI(2 + i * 2);
        Y += 0 - RI(2);
        Z += -(i + 1) + RI(2 + i * 2);
    }
}

/* ---- the decorator ---- */
static uint8_t flower_select(const Biome *b, int x, int z) {
    switch (b->flower) {
    case F_SWAMP: return B_BLUE_ORCHID;
    case F_FLOWER_FOREST: {
        double d0 = (1 + (double)grass_noise_div(x, z, 48)) / 2;
        if (d0 < 0) d0 = 0;
        if (d0 > (double)9999 / 10000) d0 = (double)9999 / 10000; /* 0.9999 */
        static const uint8_t V[10] = {B_DANDELION, B_POPPY, B_POPPY /* blue orchid -> poppy */, B_ALLIUM, B_AZURE_BLUET,
                                      B_RED_TULIP, B_ORANGE_TULIP, B_WHITE_TULIP, B_PINK_TULIP, B_OXEYE_DAISY};
        return V[(int)(d0 * 10)];
    }
    case F_PLAINS: {
        float d0 = grass_noise_div(x, z, 200);
        if (d0 < -0.8f) {
            static const uint8_t T[4] = {B_ORANGE_TULIP, B_RED_TULIP, B_PINK_TULIP, B_WHITE_TULIP};
            return T[RI(4)];
        }
        if (RI(3) > 0) {
            int i = RI(3);
            return i == 0 ? B_POPPY : (i == 1 ? B_AZURE_BLUET : B_OXEYE_DAISY);
        }
        return B_DANDELION;
    }
    default: return RI(3) > 0 ? B_DANDELION : B_POPPY;
    }
}

static uint8_t grass_select(const Biome *b) {
    switch (b->grass) {
    case G_TAIGA: return RI(5) > 0 ? B_FERN : B_TALL_GRASS;
    case G_JUNGLE: return RI(4) == 0 ? B_FERN : B_TALL_GRASS;
    default: return B_TALL_GRASS;
    }
}

static void spring(uint8_t still, uint8_t flowing, int x, int y, int z) {
    if (!in_c_cols(x, z) || y < 0 || y > 255) return; /* only the block itself matters */
    if (cget(x, y + 1, z) != B_STONE || cget(x, y - 1, z) != B_STONE) return;
    uint8_t m = cget(x, y, z);
    if (m != B_AIR && m != B_STONE) return;
    int st = 0, air = 0, ad = -1;
    for (int d = 0; d < 4; d++) {
        uint8_t n = cget(x + HDIR[d][0], y, z + HDIR[d][1]);
        st += n == B_STONE;
        if (n == B_AIR) {
            air++;
            ad = d;
        }
    }
    if (st == 3 && air == 1) {
        put_c(x, y, z, still, R_ALWAYS);
        put_c(x + HDIR[ad][0], y, z + HDIR[ad][1], flowing, R_AIR);
    }
}

static void decorate_core(const Biome *b, int nB, int nC) {
    ores();
    for (int i = 0; i < b->dI; i++) disk(B_SAND, 7, 2, 0);
    for (int i = 0; i < b->dJ; i++) disk(B_CLAY, 4, 1, 1);
    for (int i = 0; i < b->dH; i++) disk(B_GRAVEL, 6, 2, 0);
    int nt = b->dA;
    if (RI(10) == 0) ++nt;
    for (int j = 0; j < nt; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        TreeSel t = tree_select(b);
        tree_grow(t, x, hm(x, z), z);
    }
    for (int j = 0; j < b->dK; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        gen_huge_mushroom(x, hm(x, z), z);
    }
    for (int j = 0; j < nB; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) + 32;
        if (i1 > 0) {
            int y = RI(i1);
            uint8_t f = flower_select(b, x, z);
            feat_flowers(x, y, z, f);
        }
    }
    for (int j = 0; j < nC; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) {
            int y = RI(i1);
            uint8_t g = grass_select(b);
            feat_grass(x, y, z, g);
        }
    }
    for (int j = 0; j < b->dD; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) feat_deadbush(x, RI(i1), z);
    }
    for (int j = 0; j < b->dZ; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) {
            int y = RI(i1);
            while (y > 0 && v_empty(x, y - 1, z)) y--;
            feat_lily(x, y, z);
        }
    }
    for (int j = 0; j < b->dE; j++) {
        if (RI(4) == 0) {
            int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
            feat_mushrooms(x, hm(x, z), z, B_BROWN_MUSHROOM);
        }
        if (RI(8) == 0) {
            int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
            int i1 = hm(x, z) * 2;
            if (i1 > 0) feat_mushrooms(x, RI(i1), z, B_RED_MUSHROOM);
        }
    }
    if (RI(4) == 0) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) feat_mushrooms(x, RI(i1), z, B_BROWN_MUSHROOM);
    }
    if (RI(8) == 0) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) feat_mushrooms(x, RI(i1), z, B_RED_MUSHROOM);
    }
    for (int j = 0; j < b->dF + 10; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) feat_reeds(x, RI(i1), z);
    }
    if (RI(32) == 0) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) feat_pumpkin(x, RI(i1), z);
    }
    for (int j = 0; j < b->dG; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = hm(x, z) * 2;
        if (i1 > 0) feat_cactus(x, RI(i1), z);
    }
    for (int j = 0; j < 50; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int i1 = RI(248) + 8;
        spring(B_WATER, B_FLOWING_WATER, x, RI(i1), z);
    }
    for (int j = 0; j < 20; j++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int y = RI(RI(RI(240) + 8) + 8);
        spring(B_LAVA, B_FLOWING_LAVA, x, y, z);
    }
}

static void double_plants(uint8_t lower, int n) {
    for (int i = 0; i < n; i++) {
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int y = RI(hm(x, z) + 32);
        feat_tallplant(x, y, z, lower);
    }
}

/* BiomeForest.a */
static void decorate_forest(const Biome *b) {
    if (b->mode == 3) {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                int x = PK + i * 4 + 1 + 8 + RI(3), z = PL + j * 4 + 1 + 8 + RI(3);
                int y = hm(x, z);
                if (RI(20) == 0) {
                    gen_huge_mushroom(x, y, z);
                } else {
                    TreeSel t = tree_select(b);
                    tree_grow(t, x, y, z);
                }
            }
    }
    int n = RI(5) - 3;
    if (b->mode == 1) n += 2;
    for (int j = 0; j < n;) {
        int k = RI(3);
        uint8_t v = k == 0 ? B_LILAC_LOWER : (k == 1 ? B_ROSE_BUSH_LOWER : B_PEONY_LOWER);
        int l = 0;
        for (;;) {
            if (l < 5) {
                int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
                int y = RI(hm(x, z) + 32);
                if (!feat_tallplant(x, y, z, v)) {
                    ++l;
                    continue;
                }
            }
            ++j;
            break;
        }
    }
    decorate_core(b, b->dB, b->dC);
}

static void decorate(const Biome *b) {
    switch (b->deco) {
    case D_PLAINS: {
        float d0 = grass_noise_div(PK + 8, PL + 8, 200);
        int nB, nC;
        if (d0 < -0.8f) {
            nB = 15;
            nC = 5;
        } else {
            nB = 4;
            nC = 10;
            double_plants(B_DOUBLE_GRASS_LOWER, 7);
        }
        if (b->mode) double_plants(B_SUNFLOWER_LOWER, 10);
        decorate_core(b, nB, nC);
        break;
    }
    case D_DESERT:
        decorate_core(b, b->dB, b->dC);
        if (RI(1000) == 0) {
            int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
            feat_desert_well(x, hm(x, z) + 1, z);
        }
        break;
    case D_HILLS: {
        decorate_core(b, b->dB, b->dC);
        int n = 3 + RI(6);
        for (int j = 0; j < n; j++) {
            int x = PK + RI(16), y = RI(28) + 4, z = PL + RI(16);
            put_c(x, y, z, B_EMERALD_ORE, R_STONE);
        }
        for (int i = 0; i < 7; i++) {
            int x = RI(16), y = RI(64), z = RI(16);
            minable(PK + x, y, PL + z, 9, B_MONSTER_EGG_STONE);
        }
        break;
    }
    case D_FOREST: decorate_forest(b); break;
    case D_ROOFED_SUB: decorate_forest(bio(BI_ROOFED)); break;
    case D_TAIGA:
        if (b->mode == 1 || b->mode == 2) {
            int n = RI(3);
            for (int j = 0; j < n; j++) {
                int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
                feat_boulder(x, hm(x, z), z);
            }
        }
        double_plants(B_LARGE_FERN_LOWER, 7);
        decorate_core(b, b->dB, b->dC);
        break;
    case D_ICE:
        if (b->mode) {
            for (int i = 0; i < 3; i++) {
                int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
                feat_ice_spike(x, hm(x, z), z);
            }
            for (int i = 0; i < 2; i++) {
                int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
                feat_ice_disk(x, hm(x, z), z);
            }
        }
        decorate_core(b, b->dB, b->dC);
        break;
    case D_JUNGLE: {
        decorate_core(b, b->dB, b->dC);
        int x = PK + RI(16) + 8, z = PL + RI(16) + 8;
        int y = RI(hm(x, z) * 2);
        feat_melon(x, y, z);
        /* the 50 vine features start at y = 128 and place nothing; their draws come last */
        break;
    }
    case D_SAVANNA:
        double_plants(B_DOUBLE_GRASS_LOWER, 7);
        decorate_core(b, b->dB, b->dC);
        break;
    default: decorate_core(b, b->dB, b->dC); break;
    }
}

/* ---- one population ---- */

static void populate(int px, int pz, int biome) {
    View *v = &PV;
    v->px = px;
    v->pz = pz;
    v->ox = px * 16;
    v->oz = pz * 16;
    v->nlake = 0;
    v->qon = 0;
    for (int lz = 0; lz < 32; lz++)
        for (int lx = 0; lx < 32; lx++) {
            const ColSum *s = col_sum(v->ox + lx, v->oz + lz);
            int i = lz * 32 + lx;
            uint8_t blk = cs_blk(s);
            v->top[i] = (uint8_t)cs_top(s);
            v->tb[i] = blk == B_LILY_PAD ? B_WATER : blk;
            v->pl[i] = blk == B_LILY_PAD ? B_LILY_PAD : 0;
        }
    PK = px * 16;
    PL = pz * 16;
    const Biome *b = bio(biome);
    jr_seed(&PR, (i64)((u64)(i64)px * (u64)pop_mul_x + (u64)(i64)pz * (u64)pop_mul_z) ^ g_seed);
    pmemo_begin(px, pz);
    /* TODO: structures (mineshafts, villages, strongholds, temples, monuments) would draw here */
    if (b->id != BI_DESERT && b->id != BI_DESERT_HILLS && RI(4) == 0) {
        int x = PK + RI(16) + 8, y = RI(256), z = PL + RI(16) + 8;
        lake(B_WATER, x, y, z);
    }
    if (RI(8) == 0) {
        int x = PK + RI(16) + 8, y = RI(RI(248) + 8), z = PL + RI(16) + 8;
        if (y < SEA || RI(10) == 0) lake(B_LAVA, x, y, z);
    }
    for (int i = 0; i < 8; i++) {
        int x = PK + RI(16) + 8, y = RI(256), z = PL + RI(16) + 8;
        dung_idx = i;
        dungeon(x, y, z);
    }
    pm->valid = 1; /* outcomes complete */
    decorate(b);
    /* SpawnerCreature draws next: nothing below depends on them */
    /* freeze: ice on still water, snow on top, for the columns of this population inside C */
    for (int dz = 0; dz < 16; dz++)
        for (int dx = 0; dx < 16; dx++) {
            int x = PK + 8 + dx, z = PL + 8 + dz;
            if (!in_c_cols(x, z)) continue;
            int i = (z & 15) * 16 + (x & 15);
            int y1 = c_sl[i] + 1, y2 = c_sl[i];
            const Biome *cb = bio(c_b16[i]);
            if (biome_temp_at(cb, x, y2, z) <= 0.15f) {
                uint8_t w = in_c_range(x, y2, z) ? cget(x, y2, z) : B_AIR;
                if (w == B_WATER) put_c(x, y2, z, B_ICE, R_ALWAYS);
            }
            if (biome_temp_at(cb, x, y1, z) <= 0.15f && y1 < 256) {
                uint8_t a = in_c_range(x, y1, z) ? cget(x, y1, z) : (y1 > c_top[i] ? B_AIR : B_STONE);
                uint8_t u = in_c_range(x, y2, z) ? cget(x, y2, z) : vget(x, y2, z);
                if (a == B_AIR && u != B_ICE && u != B_PACKED_ICE && (IS_LEAVES(u) || (IS_CUBE(u) && IS_SOLID(u)))) {
                    if (in_c_range(x, y1, z)) {
                        put_c(x, y1, z, B_SNOW_LAYER, R_AIR);
                        if (u == B_GRASS && in_c_range(x, y2, z)) put_c(x, y2, z, B_GRASS_SNOWED, R_ALWAYS);
                    } else if (y1 > c_top[i]) {
                        c_top[i] = (uint8_t)y1;
                    }
                }
            }
        }
}

/* ======================================================================== */
/* Enchanted book draws (EnchantmentManager.a(random, book, 30))             */
/* ======================================================================== */
static void dungeon_book(void) {
    /* TODO: exact replay of the enchantment selection draws */
}

/* ======================================================================== */
/* Public API                                                                */
/* ======================================================================== */

static int last_cx = 0x7fffffff, last_cz; /* chunk of the last gen_slab: c_top, c_b16 valid */
static uint8_t col_tmp[256];

/* carve hook: keep c_top / c_sl of C right when caves open the surface */
static void cave_track(int x, int y, int z, uint8_t b) {
    int i = z * 16 + x;
    if (b == B_AIR && y == c_top[i]) c_lower_top(i, y);
    if (y == c_sl[i] && b == B_AIR) c_sl[i] = c_top[i];
    if (c_sl[i] < crep_lo) crep_lo = c_sl[i];
    if (c_top[i] < crep_lo) crep_lo = c_top[i];
}

void gen_init(int64_t seed) {
    jr_jump_init();
    bflags_init();
    g_seed = seed;
    memset(biome_index, 255, sizeof biome_index);
    for (int i = 0; i < NBIOMES; i++) biome_index[BIOMES[i].id] = (uint8_t)i;
    for (int i = 0; i < NLSEEDS; i++) lay_wseed[i] = layer_world(LSEEDS[i]);
    vor_c = layer_world(10);
    /* ChunkProviderGenerate's noise generators */
    JRand r;
    jr_seed(&r, seed);
    for (int i = 0; i < 16; i++) oc_min[i].st = r.s, perm_skip(&r);
    for (int i = 0; i < 16; i++) oc_max[i].st = r.s, perm_skip(&r);
    for (int i = 0; i < 8; i++) oc_main[i].st = r.s, perm_skip(&r);
    for (int i = 0; i < 4; i++) oc_surf[i].st = r.s, perm_skip(&r);
    for (int i = 0; i < 10; i++) perm_skip(&r); /* scale noise (unused) */
    for (int i = 0; i < 16; i++) oc_depth[i].st = r.s, perm_skip(&r);
    SC_LIMIT = FX(684.412f);
    SC_MAINXZ = FX(684.412f / 80.0f);
    SC_MAINY = FX(684.412f / 160.0f);
    SC_DEPTH = FX(200.0f);
    for (int j = -2; j <= 2; j++)
        for (int k = -2; k <= 2; k++)
            bweights[j + 2 + (k + 2) * 5] = 10.0f / sqrtf((float)(j * j + k * k) + 0.2f);
    JRand t;
    jr_seed(&t, 1234);
    perm_build(t.s, perm_temp, 0);
    jr_seed(&t, 2345);
    perm_build(t.s, perm_grass, 0);
    /* BiomeMesa: bands and noises */
    jr_seed(&r, seed);
    oc_mesa[0].st = r.s;
    perm_skip(&r);
    for (int j = 0; j < 64; j++) mesa_bands[j] = B_HARDENED_CLAY;
    for (int j = 0; j < 64; ++j) {
        j += jr_int(&r, 5) + 1;
        if (j < 64) mesa_bands[j] = B_STAINED_CLAY_ORANGE;
    }
    static const uint8_t col3[3] = {B_STAINED_CLAY_YELLOW, B_STAINED_CLAY_BROWN, B_STAINED_CLAY_RED};
    for (int c = 0; c < 3; c++) {
        int n = jr_int(&r, 4) + 2;
        for (int k = 0; k < n; ++k) {
            int len = jr_int(&r, 3) + (c == 1 ? 2 : 1), at = jr_int(&r, 64);
            for (int q = 0; at + q < 64 && q < len; ++q) mesa_bands[at + q] = col3[c];
        }
    }
    int n = jr_int(&r, 3) + 3, at = 0;
    for (int k = 0; k < n; ++k) {
        at += jr_int(&r, 16) + 4;
        if (at < 64) {
            mesa_bands[at] = B_STAINED_CLAY_WHITE;
            if (at > 1 && jr_bool(&r)) mesa_bands[at - 1] = B_STAINED_CLAY_SILVER;
            if (at < 63 && jr_bool(&r)) mesa_bands[at + 1] = B_STAINED_CLAY_SILVER;
        }
    }
    jr_seed(&r, seed);
    for (int o = 0; o < 4; o++) oc_mesa[o].st = r.s, perm_skip(&r);
    oc_mesa[4].st = r.s;
    /* population and cave seeds */
    jr_seed(&r, seed);
    pop_mul_x = jr_long(&r) / 2 * 2 + 1;
    pop_mul_z = jr_long(&r) / 2 * 2 + 1;
    jr_seed(&r, seed);
    cave_mul_x = jr_long(&r);
    cave_mul_z = jr_long(&r);
    /* caches */
    memset(sumc, 0, sizeof sumc);
    memset(pmemo, 0, sizeof pmemo);
    memset(sum_pin, 0, sizeof sum_pin);
    last_cx = bc_cx = 0x7fffffff;
}

/* Caves and ravines of chunk (cx, cz) into out (whole height: the c_top tracking needs it) */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void slab_caves(int cx, int cz, const ColSum *sum, uint8_t *out, int y0, int h) {
    crec_cover(cx, cz);
    CaveCtx c;
    memset(&c, 0, sizeof c);
    c.mode = CAVE_OUT;
    c.tcx = cx;
    c.tcz = cz;
    c.sum = sum;
    c.out = out;
    c.y0 = y0;
    c.h = h;
    c.rx1 = 16;
    c.rz1 = 16;
    c.ry1 = 256;
    tg[4] = &c;
    cave_track_on = 1;
    int hi = y0 + h;
    crep_lo = y0 - 1;
    for (int i = 0; i < 256; i++) {
        if (c_top[i] > hi) hi = c_top[i];
        if (c_sl[i] < crep_lo) crep_lo = c_sl[i];
        if (c_top[i] < crep_lo) crep_lo = c_top[i];
    }
    if (crec_ok) {
        crep_dyn = 1;
        caves_replay(1u << 4, cx * 16, 0, cz * 16, cx * 16 + 15, hi + 1, cz * 16 + 15);
        crep_dyn = 0;
    } else {
        caves_run(1u << 4);
    }
    cave_track_on = 0;
}

/* Ahead of gen_slab(cx, cz): one of the summaries of the chunks around it that it will need, if
 * one is not kept (each is a chunk's terrain, so the work of a chunk can be spread over frames).
 * 0 when they are all there. */
/* ---------------------------------------------------------------- superflat (ChunkProviderFlat) */
static int g_flat;
void gen_set_flat(int flat) { g_flat = flat; }
static int flat_block(int y) { return y == 0 ? B_BEDROCK : y < 3 ? B_DIRT : y == 3 ? B_GRASS : B_AIR; }

int gen_prepare(int cx, int cz) {
    if (g_flat) return 0;
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++)
            if ((dx || dz) && !sum_find(cx + dx, cz + dz)) {
                sum_get(cx + dx, cz + dz);
                return 1;
            }
    return 0;
}

void gen_slab(int cx, int cz, int y0, int h, uint8_t *out) {
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    }
    if (y0 + h > 128) h = 128 - y0;
    if (h <= 0) return;
    if (g_flat) {
        for (int y = 0; y < h; y++) memset(out + y * 256, flat_block(y0 + y), 256);
        return;
    }
    c_out = out;
    c_y0 = y0;
    c_h = h;
    c_cx = cx;
    c_cz = cz;
    nb_cx = cx;
    nb_cz = cz;
    /* terrain of C, and its summary */
    SumEntry *e = sum_find(cx, cz);
    if (!e) e = sum_slot(cx, cz);
    int ie = (int)(e - sumc);
    sum_pin[ie] = 1;
    chunk_terrain(cx, cz, out, y0, h, e->s);
    e->b00 = cb16[0];
    memcpy(c_b16, cb16, 256);
    for (int i = 0; i < 256; i++) {
        c_top[i] = (uint8_t)(cs_top(&e->s[i]) + (cs_blk(&e->s[i]) == B_LILY_PAD));
        c_sl[i] = (uint8_t)cs_top(&e->s[i]);
    }
    nb[1][1] = e;
    /* caves and ravines */
    slab_caves(cx, cz, e->s, out, y0, h);
    /* the 3x3 summaries */
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dz) continue;
            SumEntry *n = sum_get(cx + dx, cz + dz);
            sum_pin[n - sumc] = 1;
            nb[dz + 1][dx + 1] = n;
        }
    /* the four populations writing into C (their biome is the one at +16, +16) */
    int pb[4]; /* World.getBiome(population origin + (16, 16)): block (0, 0) of the next chunk */
    for (int k = 0; k < 4; k++) pb[k] = nb[1 + (k >> 1)][1 + (k & 1)]->b00;
    populate(cx - 1, cz - 1, pb[0]);
    populate(cx, cz - 1, pb[1]);
    populate(cx - 1, cz, pb[2]);
    populate(cx, cz, pb[3]);
    memset(sum_pin, 0, sizeof sum_pin);
    last_cx = cx;
    last_cz = cz;
}

int gen_biome(int x, int z) {
    if (g_flat) return BI_PLAINS;
    int cx = x >> 4, cz = z >> 4, i = (x & 15) + (z & 15) * 16;
    if (cx == last_cx && cz == last_cz) return c_b16[i];
    if (cx != bc_cx || cz != bc_cz) chunk_biomes(cx, cz);
    return cb16[i];
}

int gen_top(int x, int z) {
    if (g_flat) return 3;
    int cx = x >> 4, cz = z >> 4, i = (x & 15) + (z & 15) * 16;
    if (cx != last_cx || cz != last_cz) gen_slab(cx, cz, 127, 1, col_tmp);
    int t = c_top[i];
    return t > 127 ? 127 : t;
}

float gen_temp_noise(int x, int z) { return temp_noise(x, z); }

/* World.c(x, z) == GRASS on the undecorated terrain */
static int can_spawn(int x, int z) {
    SumEntry *e = sum_get(x >> 4, z >> 4);
    const ColSum *s = &e->s[(x & 15) + (z & 15) * 16];
    if (cs_top(s) < SEA) return 0;
    return cs_blk(s) == B_GRASS;
}

void gen_spawn(int *px, int *py, int *pz) {
    JRand r;
    jr_seed(&r, g_seed);
    if (g_flat) {
        /* WorldChunkManagerHell.findBiomePosition(0, 0, 256): anywhere within 256, all of it grass */
        *px = -256 + jr_int(&r, 513);
        *pz = -256 + jr_int(&r, 513);
        *py = 4;
        return;
    }
    /* WorldChunkManager.a(0, 0, 256, spawn biomes, random): 129 x 129 cells of rivermix */
    int found = 0, fx = 0, fz = 0, j2 = 0;
    uint8_t *rows = (uint8_t *)U.lay + sizeof(i32) * LAY_INTS; /* 8 rows x 129, after the layer scratch */
    for (int tz = 0; tz < 129; tz += 8) {
        int th = 129 - tz < 8 ? 129 - tz : 8;
        for (int tx = 0; tx < 129; tx += 43) { /* 43 x 8 tiles: the layers' cost is mostly per run */
            int tw = 43;
            i32 *rm = U.lay;
            lay_mem = U.lay + 43 * 8;
            lay_cap = LAY_INTS - 43 * 8;
            lay_top = 0;
            layers_rivermix(-64 + tx, -64 + tz, tw, th, rm);
            for (int j = 0; j < th; j++)
                for (int i = 0; i < tw; i++) rows[j * 129 + tx + i] = (uint8_t)rm[j * tw + i];
        }
        for (int j = 0; j < th; j++)
            for (int i = 0; i < 129; i++) {
                int id = rows[j * 129 + i];
                int ok = id == BI_FOREST || id == BI_PLAINS || id == BI_TAIGA || id == BI_TAIGA_HILLS ||
                         id == BI_FOREST_HILLS || id == BI_JUNGLE || id == BI_JUNGLE_HILLS;
                if (ok && (!found || jr_int(&r, j2 + 1) == 0)) {
                    fx = (-64 + i) * 4;
                    fz = (-64 + tz + j) * 4;
                    found = 1;
                    ++j2;
                }
            }
    }
    int x = found ? fx : 0, z = found ? fz : 0;
    for (int l = 0; !can_spawn(x, z);) {
        x += jr_int(&r, 64), x -= jr_int(&r, 64);
        z += jr_int(&r, 64), z -= jr_int(&r, 64);
        if (++l == 1000) break;
    }
    *px = x;
    *pz = z;
    *py = gen_top(x, z) + 1;
}

/* every static object of this file (the list follows the object file's symbols) */
unsigned gen_ram_bytes(void) {
    return (unsigned)(sizeof PK + sizeof PL + sizeof bc_cx + sizeof bc_cz + sizeof bg_ok + sizeof bg_x +
                      sizeof bg_z + sizeof c_cx + sizeof c_cz + sizeof c_h + sizeof c_out + sizeof c_y0 +
                      sizeof cave_track_on + sizeof col_top + sizeof crec_ax + sizeof crec_az +
                      sizeof crec_ok + sizeof crep_dyn + sizeof crep_lo + sizeof cv + sizeof dung_idx +
                      sizeof last_cx + sizeof last_cz + sizeof lay_cap + sizeof lay_mem + sizeof lay_peak +
                      sizeof lay_top + sizeof nb_cx + sizeof nb_cz + sizeof ncrec + sizeof pm +
                      sizeof pm_known + sizeof pmemo_next + sizeof put_skip_c + sizeof rb_x0 + sizeof rb_x1 +
                      sizeof rb_y0 + sizeof rb_y1 + sizeof rb_z0 + sizeof rb_z1 + sizeof sum_clock +
                      sizeof sum_odd + sizeof tov_on + sizeof tov_x + sizeof tov_y + sizeof tov_z +
                      sizeof PR + sizeof SC_DEPTH + sizeof SC_LIMIT + sizeof SC_MAINXZ + sizeof SC_MAINY +
                      sizeof bg_seed + sizeof cave_mul_x + sizeof cave_mul_z + sizeof crec_seed +
                      sizeof g_seed + sizeof pop_mul_x + sizeof pop_mul_z + sizeof reg_x0 + sizeof reg_x1 +
                      sizeof reg_z0 + sizeof reg_z1 + sizeof vor_c + sizeof sum_pin + sizeof oc_surf +
                      sizeof nb + sizeof tg + sizeof oc_mesa + sizeof mesa_bands + sizeof oc_main +
                      sizeof tg_cx + sizeof tg_cz + sizeof jump_add + sizeof jump_mul + sizeof bweights +
                      sizeof cb4 + sizeof oc_depth + sizeof oc_max + sizeof oc_min + sizeof chain_buf +
                      sizeof pmemo + sizeof tstack + sizeof biome_index + sizeof c_b16 + sizeof c_sl +
                      sizeof c_top + sizeof cb16 + sizeof col_tmp + sizeof perm_grass + sizeof perm_temp +
                      sizeof bgrid + sizeof lay_wseed + sizeof bflags + sizeof tov + sizeof crec + sizeof U +
                      sizeof sumc);
}
