/* checks that every column summary fits gen.c's 2-byte encoding (sum_odd stays 0),
 * over many chunks spread over several seeds; prints the biomes seen */
#include "../../src/gen.c"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 2000;
    long long seeds[] = {12345, 1, 777, -8913681731385445214LL, 42, 99999, 3, 123456789};
    static ColSum s[256];
    static long seen[256];
    long cols = 0;
    for (int si = 0; si < 8; si++) {
        gen_init(seeds[si]);
        for (int k = 0; k < n; k++) {
            int cx = (int)((k * 2654435761u) % 4000) - 2000, cz = (int)((k * 40503u + 17) % 4000) - 2000;
            chunk_terrain(cx, cz, 0, 0, 0, s);
            for (int i = 0; i < 256; i++) seen[cb16[i]]++;
            cols += 256;
        }
    }
    int nb = 0;
    for (int b = 0; b < 256; b++) nb += seen[b] != 0;
    printf("%ld columns, %d biomes seen, %u could not be encoded\n", cols, nb, sum_odd);
    return sum_odd != 0;
}
