/* test-only init of the parts of gen.c built so far (replaced by gen_init later) */
static void test_init(i64 seed) {
    jr_jump_init();
    g_seed = seed;
    memset(biome_index, 255, sizeof biome_index);
    for (int i = 0; i < NBIOMES; i++) biome_index[BIOMES[i].id] = (uint8_t)i;
    for (int i = 0; i < NLSEEDS; i++) lay_wseed[i] = layer_world(LSEEDS[i]);
    vor_c = layer_world(10);
    JRand r; jr_seed(&r, seed);
    for (int i = 0; i < 16; i++) { oc_min[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 16; i++) { oc_max[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 8; i++) { oc_main[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 4; i++) { oc_surf[i].st = r.s; perm_skip(&r); }
    for (int i = 0; i < 10; i++) perm_skip(&r);
    for (int i = 0; i < 16; i++) { oc_depth[i].st = r.s; perm_skip(&r); }
    SC_LIMIT = FX(684.412f); SC_MAINXZ = FX(684.412f / 80.0f); SC_MAINY = FX(684.412f / 160.0f); SC_DEPTH = FX(200.0f);
    for (int j = -2; j <= 2; j++) for (int k = -2; k <= 2; k++) bweights[j + 2 + (k + 2) * 5] = 10.0f / (float)sqrt((double)((float)(j * j + k * k) + 0.2f));
    JRand t; jr_seed(&t, 1234); perm_build(t.s, perm_temp, 0);
    jr_seed(&t, 2345); perm_build(t.s, perm_grass, 0);
    /* mesa */
    jr_seed(&r, seed);
    oc_mesa[0].st = r.s; perm_skip(&r);
    for (int j = 0; j < 64; j++) mesa_bands[j] = B_HARDENED_CLAY;
    int j;
    for (j = 0; j < 64; ++j) { j += jr_int(&r, 5) + 1; if (j < 64) mesa_bands[j] = B_STAINED_CLAY_ORANGE; }
    j = jr_int(&r, 4) + 2;
    for (int k = 0; k < j; ++k) { int l = jr_int(&r, 3) + 1, i1 = jr_int(&r, 64); for (int j1 = 0; i1 + j1 < 64 && j1 < l; ++j1) mesa_bands[i1 + j1] = B_STAINED_CLAY_YELLOW; }
    int k = jr_int(&r, 4) + 2;
    for (int l = 0; l < k; ++l) { int i1 = jr_int(&r, 3) + 2, j1 = jr_int(&r, 64); for (int k1 = 0; j1 + k1 < 64 && k1 < i1; ++k1) mesa_bands[j1 + k1] = B_STAINED_CLAY_BROWN; }
    int l = jr_int(&r, 4) + 2;
    for (int i1 = 0; i1 < l; ++i1) { int j1 = jr_int(&r, 3) + 1, k1 = jr_int(&r, 64); for (int l1 = 0; k1 + l1 < 64 && l1 < j1; ++l1) mesa_bands[k1 + l1] = B_STAINED_CLAY_RED; }
    int i1 = jr_int(&r, 3) + 3, j1 = 0;
    for (int k1 = 0; k1 < i1; ++k1) { j1 += jr_int(&r, 16) + 4; for (int i2 = 0; j1 + i2 < 64 && i2 < 1; ++i2) { mesa_bands[j1 + i2] = B_STAINED_CLAY_WHITE; if (j1 + i2 > 1 && jr_bool(&r)) mesa_bands[j1 + i2 - 1] = B_STAINED_CLAY_SILVER; if (j1 + i2 < 63 && jr_bool(&r)) mesa_bands[j1 + i2 + 1] = B_STAINED_CLAY_SILVER; } }
    jr_seed(&r, seed);
    for (int o = 0; o < 4; o++) { oc_mesa[o].st = r.s; perm_skip(&r); }
    oc_mesa[4].st = r.s;
}
