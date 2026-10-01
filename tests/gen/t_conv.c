/* checks gen.c's integer-only float <-> int64 conversions against the C casts */
#include "../../src/gen.c"
#include <stdio.h>

int main(void) {
    JRand r;
    jr_seed(&r, 99);
    long bad = 0, n = 0;
    for (int i = 0; i < 20000000; i++) {
        /* random int64 of random magnitude, both signs, plus near-ties */
        int bits = jr_int(&r, 63) + 1;
        i64 v = jr_long(&r) >> (64 - bits);
        if (i % 7 == 0) v = (v | 1) << jr_int(&r, 8);
        for (int k = 0; k <= 53; k += 53 - 21) {
            float a = (float)v * (k == 32 ? (1.0f / 4294967296.0f) : k == 53 ? (1.0f / 9007199254740992.0f) : 1.0f);
            float b = i64_to_f_scaled(v, k);
            if (a != b) {
                if (bad < 10) printf("i2f %lld k %d: %.9g vs %.9g\n", (long long)v, k, a, b);
                bad++;
            }
            n++;
        }
        float f;
        u32 u = (u32)jr_next(&r, 32);
        memcpy(&f, &u, 4);
        if (!(f == f)) continue;
        for (int k = 0; k <= 32; k += 32) {
            float sc = f * (k ? 4294967296.0f : 1.0f);
            if (!(sc > -4.6e18f && sc < 4.6e18f)) continue;
            i64 a = (i64)sc, b = f2i64_scaled(f, k);
            if (a != b) {
                if (bad < 10) printf("f2i %.9g k %d: %lld vs %lld\n", f, k, (long long)a, (long long)b);
                bad++;
            }
            n++;
        }
    }
    printf("%ld / %ld mismatches\n", bad, n);
    return bad != 0;
}
