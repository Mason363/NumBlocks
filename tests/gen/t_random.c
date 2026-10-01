/* java.util.Random known values against gen.c's JRand (values from Java's documented
 * algorithm, cross-checked with an independent implementation) */
#include "../../src/gen.c"
#include <stdio.h>

static int bad;
static void check(const char *what, long long got, long long want) {
    if (got != want) {
        printf("FAIL %s: %lld, want %lld\n", what, got, want);
        bad++;
    }
}

int main(void) {
    jr_jump_init();
    JRand r;
    jr_seed(&r, 0);
    check("new Random(0).nextInt()", jr_next(&r, 32), -1155484576);
    jr_seed(&r, 0);
    check("new Random(0).nextLong()", jr_long(&r), -4962768465676381896LL);
    static const int want42[10] = {0, 3, 8, 4, 0, 5, 5, 8, 9, 3};
    jr_seed(&r, 42);
    for (int i = 0; i < 10; i++) check("new Random(42).nextInt(10)", jr_int(&r, 10), want42[i]);
    jr_seed(&r, 12345);
    check("Random(12345).nextInt(100)", jr_int(&r, 100), 51);
    check("then nextFloat * 2^24", (long long)(jr_float(&r) * 16777216.0f), (long long)(0.513209522 * 16777216.0 + 0.5));
    check("then nextDouble * 2^53", jr_double_bits(&r), (long long)(0.93299348528854098 * 9007199254740992.0));
    check("then nextInt(7)", jr_int(&r, 7), 2);
    check("then nextBoolean", jr_bool(&r), 0);
    jr_seed(&r, -8913681731385445214LL);
    check("Random(-8913681731385445214).nextLong()", jr_long(&r), -6777112709636957824LL);
    check("then nextInt(1000)", jr_int(&r, 1000), 571);
    /* LCG jumps: skipping n steps equals n calls */
    JRand a, b;
    jr_seed(&a, 777);
    jr_seed(&b, 777);
    for (int n = 0; n < 1000; n += 37) {
        for (int i = 0; i < n; i++) jr_next(&a, 32);
        jr_skip(&b, (unsigned)n);
        check("jr_skip", (long long)b.s, (long long)a.s);
    }
    printf(bad ? "%d failures\n" : "all Java Random checks pass\n", bad);
    return bad != 0;
}
