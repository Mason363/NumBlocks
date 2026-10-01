#!/usr/bin/env python3
"""The block states NumBlocks knows, in one table: tools/pack.py reads it for
textures, and `blocks.py --header` writes src/blocks.h (the B_ enum and the
per-state tables the game uses).

Each state is a Minecraft 1.8.8 block id and metadata. Fields:
  name      the enum name (B_<name>)
  id, meta  Minecraft's numeric id and metadata (for saves and the generator)
  model     cube, cross (plants), liquid, torch, slab, layer (snow), cactus,
            leaves (see-through cube), glass (see-through cube), flat (lily pad,
            rails), vine, pane, fence, door, ladder, none (air)
  tex       textures: one name for every face, or a dict with top/bottom/side
            (and front for furnaces, pumpkins...)
  tint      None, 'grass' or 'foliage' (biome colour), or 'water'
  light     light it gives (0..15)
  hard      hardness (seconds-ish, as Block.c(...) in the game; -1 unbreakable)
  tool      the tool that mines it fastest: pickaxe, axe, shovel, shears, sword, None
  level     the pickaxe level needed to get a drop (0 wood, 1 stone, 2 iron, 3 diamond)
"""
import sys

S = []


def b(name, id, meta=0, model='cube', tex=None, tint=None, light=0, hard=1.0, tool=None, level=0):
    S.append(dict(name=name, id=id, meta=meta, model=model, tex=tex, tint=tint, light=light, hard=hard, tool=tool,
                  level=level))


def tsb(top, side, bottom=None):
    return {'top': top, 'side': side, 'bottom': bottom or top}


# --- natural blocks (the generator places these)
b('AIR', 0, model='none', hard=0)
b('STONE', 1, 0, tex='stone', hard=1.5, tool='pickaxe')
b('GRANITE', 1, 1, tex='stone_granite', hard=1.5, tool='pickaxe')
b('POLISHED_GRANITE', 1, 2, tex='stone_granite_smooth', hard=1.5, tool='pickaxe')
b('DIORITE', 1, 3, tex='stone_diorite', hard=1.5, tool='pickaxe')
b('POLISHED_DIORITE', 1, 4, tex='stone_diorite_smooth', hard=1.5, tool='pickaxe')
b('ANDESITE', 1, 5, tex='stone_andesite', hard=1.5, tool='pickaxe')
b('POLISHED_ANDESITE', 1, 6, tex='stone_andesite_smooth', hard=1.5, tool='pickaxe')
b('GRASS', 2, tex={'top': 'grass_top', 'side': 'grass_side', 'bottom': 'dirt', 'overlay': 'grass_side_overlay'},
  tint='grass', hard=0.6, tool='shovel')
b('GRASS_SNOWED', 2, 8, tex={'top': 'grass_top', 'side': 'grass_side_snowed', 'bottom': 'dirt'}, tint='grass', hard=0.6,
  tool='shovel')
b('DIRT', 3, 0, tex='dirt', hard=0.5, tool='shovel')
b('COARSE_DIRT', 3, 1, tex='coarse_dirt', hard=0.5, tool='shovel')
b('PODZOL', 3, 2, tex=tsb('dirt_podzol_top', 'dirt_podzol_side', 'dirt'), hard=0.5, tool='shovel')
b('COBBLESTONE', 4, tex='cobblestone', hard=2.0, tool='pickaxe')
b('PLANKS_OAK', 5, 0, tex='planks_oak', hard=2.0, tool='axe')
b('PLANKS_SPRUCE', 5, 1, tex='planks_spruce', hard=2.0, tool='axe')
b('PLANKS_BIRCH', 5, 2, tex='planks_birch', hard=2.0, tool='axe')
b('PLANKS_JUNGLE', 5, 3, tex='planks_jungle', hard=2.0, tool='axe')
b('PLANKS_ACACIA', 5, 4, tex='planks_acacia', hard=2.0, tool='axe')
b('PLANKS_DARK_OAK', 5, 5, tex='planks_big_oak', hard=2.0, tool='axe')
b('SAPLING_OAK', 6, 0, model='cross', tex='sapling_oak', hard=0)
b('SAPLING_SPRUCE', 6, 1, model='cross', tex='sapling_spruce', hard=0)
b('SAPLING_BIRCH', 6, 2, model='cross', tex='sapling_birch', hard=0)
b('SAPLING_JUNGLE', 6, 3, model='cross', tex='sapling_jungle', hard=0)
b('SAPLING_ACACIA', 6, 4, model='cross', tex='sapling_acacia', hard=0)
b('SAPLING_DARK_OAK', 6, 5, model='cross', tex='sapling_roofed_oak', hard=0)
b('BEDROCK', 7, tex='bedrock', hard=-1)
b('WATER', 9, model='liquid', tex='water_still', tint='water', hard=100)
b('LAVA', 11, model='liquid', tex='lava_still', light=15, hard=100)
b('SAND', 12, 0, tex='sand', hard=0.5, tool='shovel')
b('RED_SAND', 12, 1, tex='red_sand', hard=0.5, tool='shovel')
b('GRAVEL', 13, tex='gravel', hard=0.6, tool='shovel')
b('GOLD_ORE', 14, tex='gold_ore', hard=3.0, tool='pickaxe', level=2)
b('IRON_ORE', 15, tex='iron_ore', hard=3.0, tool='pickaxe', level=1)
b('COAL_ORE', 16, tex='coal_ore', hard=3.0, tool='pickaxe')
# logs: meta 0-3 variant, +4 x axis, +8 z axis, 12 bark on every face
for v, (n, t) in enumerate([('OAK', 'oak'), ('SPRUCE', 'spruce'), ('BIRCH', 'birch'), ('JUNGLE', 'jungle')]):
    b(f'LOG_{n}', 17, v, tex=tsb(f'log_{t}_top', f'log_{t}'), hard=2.0, tool='axe')
    b(f'LOG_{n}_X', 17, v + 4, tex={'top': f'log_{t}', 'side': f'log_{t}', 'bottom': f'log_{t}', 'x': f'log_{t}_top'},
      hard=2.0, tool='axe')
    b(f'LOG_{n}_Z', 17, v + 8, tex={'top': f'log_{t}', 'side': f'log_{t}', 'bottom': f'log_{t}', 'z': f'log_{t}_top'},
      hard=2.0, tool='axe')
    b(f'LOG_{n}_BARK', 17, v + 12, tex=f'log_{t}', hard=2.0, tool='axe')
for v, (n, t, tint) in enumerate([('OAK', 'oak', 'foliage'), ('SPRUCE', 'spruce', 'spruce'), ('BIRCH', 'birch', 'birch'),
                                  ('JUNGLE', 'jungle', 'foliage')]):
    b(f'LEAVES_{n}', 18, v, model='leaves', tex=f'leaves_{t}', tint=tint, hard=0.2, tool='shears')
b('SPONGE', 19, tex='sponge', hard=0.6)
b('GLASS', 20, model='glass', tex='glass', hard=0.3)
b('LAPIS_ORE', 21, tex='lapis_ore', hard=3.0, tool='pickaxe', level=1)
b('LAPIS_BLOCK', 22, tex='lapis_block', hard=3.0, tool='pickaxe', level=1)
b('SANDSTONE', 24, 0, tex=tsb('sandstone_top', 'sandstone_normal', 'sandstone_bottom'), hard=0.8, tool='pickaxe')
b('SANDSTONE_CHISELED', 24, 1, tex=tsb('sandstone_top', 'sandstone_carved'), hard=0.8, tool='pickaxe')
b('SANDSTONE_SMOOTH', 24, 2, tex=tsb('sandstone_top', 'sandstone_smooth'), hard=0.8, tool='pickaxe')
b('COBWEB', 30, model='cross', tex='web', hard=4.0, tool='sword')
b('DEAD_SHRUB', 31, 0, model='cross', tex='deadbush', hard=0)
b('TALL_GRASS', 31, 1, model='cross', tex='tallgrass', tint='grass', hard=0)
b('FERN', 31, 2, model='cross', tex='fern', tint='grass', hard=0)
b('DEAD_BUSH', 32, model='cross', tex='deadbush', hard=0)
for v, c in enumerate(['white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray', 'silver', 'cyan',
                       'purple', 'blue', 'brown', 'green', 'red', 'black']):
    b(f'WOOL_{c.upper()}', 35, v, tex=f'wool_colored_{c}', hard=0.8, tool='shears')
b('DANDELION', 37, model='cross', tex='flower_dandelion', hard=0)
for v, (n, t) in enumerate([('POPPY', 'rose'), ('BLUE_ORCHID', 'blue_orchid'), ('ALLIUM', 'allium'),
                            ('AZURE_BLUET', 'houstonia'), ('RED_TULIP', 'tulip_red'), ('ORANGE_TULIP', 'tulip_orange'),
                            ('WHITE_TULIP', 'tulip_white'), ('PINK_TULIP', 'tulip_pink'),
                            ('OXEYE_DAISY', 'oxeye_daisy')]):
    b(n, 38, v, model='cross', tex=f'flower_{t}', hard=0)
b('BROWN_MUSHROOM', 39, model='cross', tex='mushroom_brown', light=1, hard=0)
b('RED_MUSHROOM', 40, model='cross', tex='mushroom_red', hard=0)
b('GOLD_BLOCK', 41, tex='gold_block', hard=3.0, tool='pickaxe', level=2)
b('IRON_BLOCK', 42, tex='iron_block', hard=5.0, tool='pickaxe', level=1)
b('STONE_SLAB_DOUBLE', 43, 0, tex=tsb('stone_slab_top', 'stone_slab_side'), hard=2.0, tool='pickaxe')
b('STONE_SLAB', 44, 0, model='slab', tex=tsb('stone_slab_top', 'stone_slab_side'), hard=2.0, tool='pickaxe')
b('SANDSTONE_SLAB', 44, 1, model='slab', tex=tsb('sandstone_top', 'sandstone_normal', 'sandstone_bottom'), hard=2.0,
  tool='pickaxe')
b('WOOD_SLAB_OLD', 44, 2, model='slab', tex='planks_oak', hard=2.0, tool='pickaxe')
b('COBBLESTONE_SLAB', 44, 3, model='slab', tex='cobblestone', hard=2.0, tool='pickaxe')
b('BRICK_SLAB', 44, 4, model='slab', tex='brick', hard=2.0, tool='pickaxe')
b('STONE_BRICK_SLAB', 44, 5, model='slab', tex='stonebrick', hard=2.0, tool='pickaxe')
b('BRICKS', 45, tex='brick', hard=2.0, tool='pickaxe')
b('TNT', 46, tex=tsb('tnt_top', 'tnt_side', 'tnt_bottom'), hard=0)
b('BOOKSHELF', 47, tex=tsb('planks_oak', 'bookshelf'), hard=1.5, tool='axe')
b('MOSSY_COBBLESTONE', 48, tex='cobblestone_mossy', hard=2.0, tool='pickaxe')
b('OBSIDIAN', 49, tex='obsidian', hard=50.0, tool='pickaxe', level=3)
b('TORCH', 50, 5, model='torch', tex='torch_on', light=14, hard=0)
b('TORCH_E', 50, 1, model='torch', tex='torch_on', light=14, hard=0)
b('TORCH_W', 50, 2, model='torch', tex='torch_on', light=14, hard=0)
b('TORCH_S', 50, 3, model='torch', tex='torch_on', light=14, hard=0)
b('TORCH_N', 50, 4, model='torch', tex='torch_on', light=14, hard=0)
b('MOB_SPAWNER', 52, model='glass', tex='mob_spawner', hard=5.0, tool='pickaxe')
b('OAK_STAIRS', 53, model='stairs', tex='planks_oak', hard=2.0, tool='axe')
b('CHEST', 54, 2, model='chest', tex={'top': '@chest_top', 'side': '@chest_side', 'front': '@chest_front',
                                     'bottom': '@chest_top'}, hard=2.5, tool='axe')
b('DIAMOND_ORE', 56, tex='diamond_ore', hard=3.0, tool='pickaxe', level=2)
b('DIAMOND_BLOCK', 57, tex='diamond_block', hard=5.0, tool='pickaxe', level=2)
b('CRAFTING_TABLE', 58, tex={'top': 'crafting_table_top', 'side': 'crafting_table_side', 'front': 'crafting_table_front',
                             'bottom': 'planks_oak'}, hard=2.5, tool='axe')
for st in range(8):
    b(f'WHEAT_{st}', 59, st, model='cross', tex=f'wheat_stage_{st}', hard=0)
b('FARMLAND', 60, 0, tex=tsb('farmland_dry', 'dirt'), hard=0.6, tool='shovel')
b('FARMLAND_WET', 60, 7, tex=tsb('farmland_wet', 'dirt'), hard=0.6, tool='shovel')
b('FURNACE', 61, 2, tex={'top': 'furnace_top', 'side': 'furnace_side', 'front': 'furnace_front_off',
                         'bottom': 'furnace_top'}, hard=3.5, tool='pickaxe')
b('FURNACE_LIT', 62, 2, tex={'top': 'furnace_top', 'side': 'furnace_side', 'front': 'furnace_front_on',
                             'bottom': 'furnace_top'}, light=13, hard=3.5, tool='pickaxe')
b('DOOR_OAK_LOWER', 64, 0, model='door', tex='door_wood_lower', hard=3.0, tool='axe')
b('DOOR_OAK_UPPER', 64, 8, model='door', tex='door_wood_upper', hard=3.0, tool='axe')
b('LADDER', 65, 2, model='ladder', tex='ladder', hard=0.4, tool='axe')
b('RAIL', 66, 0, model='flat', tex='rail_normal', hard=0.7, tool='pickaxe')
b('COBBLESTONE_STAIRS', 67, model='stairs', tex='cobblestone', hard=2.0, tool='pickaxe')
b('REDSTONE_ORE', 73, tex='redstone_ore', hard=3.0, tool='pickaxe', level=2)
b('SNOW_LAYER', 78, 0, model='layer', tex='snow', hard=0.1, tool='shovel')
b('ICE', 79, model='glass', tex='ice', hard=0.5, tool='pickaxe')
b('SNOW', 80, tex='snow', hard=0.2, tool='shovel')
b('CACTUS', 81, model='cactus', tex=tsb('cactus_top', 'cactus_side', 'cactus_bottom'), hard=0.4)
b('CLAY', 82, tex='clay', hard=0.6, tool='shovel')
b('SUGAR_CANE', 83, model='cross', tex='reeds', tint='grass', hard=0)
b('FENCE_OAK', 85, model='fence', tex='planks_oak', hard=2.0, tool='axe')
b('PUMPKIN', 86, 0, tex={'top': 'pumpkin_top', 'side': 'pumpkin_side', 'front': 'pumpkin_face_off',
                         'bottom': 'pumpkin_top'}, hard=1.0, tool='axe')
b('JACK_O_LANTERN', 91, 0, tex={'top': 'pumpkin_top', 'side': 'pumpkin_side', 'front': 'pumpkin_face_on',
                                'bottom': 'pumpkin_top'}, light=15, hard=1.0, tool='axe')
b('MONSTER_EGG_STONE', 97, 0, tex='stone', hard=0.75, tool='pickaxe')
b('STONE_BRICKS', 98, 0, tex='stonebrick', hard=1.5, tool='pickaxe')
b('MOSSY_STONE_BRICKS', 98, 1, tex='stonebrick_mossy', hard=1.5, tool='pickaxe')
b('CRACKED_STONE_BRICKS', 98, 2, tex='stonebrick_cracked', hard=1.5, tool='pickaxe')
b('CHISELED_STONE_BRICKS', 98, 3, tex='stonebrick_carved', hard=1.5, tool='pickaxe')
# huge mushrooms: the cap (skin outside, pores inside) and the stem
b('BROWN_MUSHROOM_CAP', 99, 14, tex='mushroom_block_skin_brown', hard=0.2, tool='axe')
b('BROWN_MUSHROOM_INSIDE', 99, 0, tex='mushroom_block_inside', hard=0.2, tool='axe')
b('BROWN_MUSHROOM_STEM', 99, 10, tex=tsb('mushroom_block_inside', 'mushroom_block_skin_stem'), hard=0.2, tool='axe')
b('RED_MUSHROOM_CAP', 100, 14, tex='mushroom_block_skin_red', hard=0.2, tool='axe')
b('RED_MUSHROOM_INSIDE', 100, 0, tex='mushroom_block_inside', hard=0.2, tool='axe')
b('RED_MUSHROOM_STEM', 100, 10, tex=tsb('mushroom_block_inside', 'mushroom_block_skin_stem'), hard=0.2, tool='axe')
b('GLASS_PANE', 102, model='pane', tex='glass', hard=0.3)
b('MELON', 103, tex=tsb('melon_top', 'melon_side'), hard=1.0, tool='axe')
b('VINE', 106, 0, model='vine', tex='vine', tint='foliage', hard=0.2, tool='shears')
b('MYCELIUM', 110, tex=tsb('mycelium_top', 'mycelium_side', 'dirt'), hard=0.6, tool='shovel')
b('LILY_PAD', 111, model='flat', tex='waterlily', tint='lily', hard=0)
b('EMERALD_ORE', 129, tex='emerald_ore', hard=3.0, tool='pickaxe', level=2)
b('EMERALD_BLOCK', 133, tex='emerald_block', hard=5.0, tool='pickaxe', level=2)
b('COBBLESTONE_WALL', 139, model='fence', tex='cobblestone', hard=2.0, tool='pickaxe')
b('HARDENED_CLAY', 172, tex='hardened_clay', hard=1.25, tool='pickaxe')
for v, c in enumerate(['white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray', 'silver', 'cyan',
                       'purple', 'blue', 'brown', 'green', 'red', 'black']):
    b(f'STAINED_CLAY_{c.upper()}', 159, v, tex=f'hardened_clay_stained_{c}', hard=1.25, tool='pickaxe')
for v, (n, t) in enumerate([('ACACIA', 'acacia'), ('DARK_OAK', 'big_oak')]):
    b(f'LOG_{n}', 162, v, tex=tsb(f'log_{t}_top', f'log_{t}'), hard=2.0, tool='axe')
    b(f'LOG_{n}_X', 162, v + 4, tex={'top': f'log_{t}', 'side': f'log_{t}', 'bottom': f'log_{t}', 'x': f'log_{t}_top'},
      hard=2.0, tool='axe')
    b(f'LOG_{n}_Z', 162, v + 8, tex={'top': f'log_{t}', 'side': f'log_{t}', 'bottom': f'log_{t}', 'z': f'log_{t}_top'},
      hard=2.0, tool='axe')
    b(f'LOG_{n}_BARK', 162, v + 12, tex=f'log_{t}', hard=2.0, tool='axe')
b('LEAVES_ACACIA', 161, 0, model='leaves', tex='leaves_acacia', tint='foliage', hard=0.2, tool='shears')
b('LEAVES_DARK_OAK', 161, 1, model='leaves', tex='leaves_big_oak', tint='foliage', hard=0.2, tool='shears')
b('HAY_BALE', 170, tex=tsb('hay_block_top', 'hay_block_side'), hard=0.5)
b('COAL_BLOCK', 173, tex='coal_block', hard=5.0, tool='pickaxe')
b('PACKED_ICE', 174, tex='ice_packed', hard=0.5, tool='pickaxe')
# double plants: lower and upper halves
for v, (n, t, tint) in enumerate([('SUNFLOWER', 'sunflower', None), ('LILAC', 'syringa', None),
                                  ('DOUBLE_GRASS', 'grass', 'grass'), ('LARGE_FERN', 'fern', 'grass'),
                                  ('ROSE_BUSH', 'rose', None), ('PEONY', 'paeonia', None)]):
    b(f'{n}_LOWER', 175, v, model='cross', tex=f'double_plant_{t}_bottom', tint=tint, hard=0)
    b(f'{n}_UPPER', 175, 8 + v, model='cross', tex=f'double_plant_{t}_top', tint=tint, hard=0)
b('RED_SANDSTONE', 179, 0, tex=tsb('red_sandstone_top', 'red_sandstone_normal', 'red_sandstone_bottom'), hard=0.8,
  tool='pickaxe')
b('GLOWSTONE', 89, tex='glowstone', light=15, hard=0.3)
b('BED_FOOT', 26, 0, model='bed', tex={'top': 'bed_feet_top', 'side': 'bed_feet_side', 'bottom': 'planks_oak',
                                      'front': 'bed_feet_end'}, hard=0.2)
b('BED_HEAD', 26, 8, model='bed', tex={'top': 'bed_head_top', 'side': 'bed_head_side', 'bottom': 'planks_oak',
                                      'front': 'bed_head_end'}, hard=0.2)
b('FLOWING_WATER', 8, 1, model='liquid', tex='water_flow', tint='water', hard=100)
b('FLOWING_LAVA', 10, 2, model='liquid', tex='lava_flow', light=15, hard=100)
# added after the first generator port: keep new states at the end so ids stay stable
for v, (n, t) in enumerate([('OAK', 'oak'), ('SPRUCE', 'spruce'), ('BIRCH', 'birch'), ('JUNGLE', 'jungle'),
                            ('ACACIA', 'acacia'), ('DARK_OAK', 'big_oak')]):
    b(f'WOOD_SLAB_{n}', 126, v, model='slab', tex=f'planks_{t}', hard=2.0, tool='axe')
# carrots and potatoes: 4 looks (Minecraft's ages 0-1, 2-3, 4-6, 7)
for st in range(4):
    b(f'CARROTS_{st}', 141, (0, 2, 4, 7)[st], model='cross', tex=f'carrots_stage_{st}', hard=0)
for st in range(4):
    b(f'POTATOES_{st}', 142, (0, 2, 4, 7)[st], model='cross', tex=f'potatoes_stage_{st}', hard=0)
# liquid levels (BlockDynamicLiquid: 1-7 away from the source, 8 falling); FLOWING_WATER is level 1,
# FLOWING_LAVA level 2
for lv in range(2, 8):
    b(f'FLOWING_WATER_{lv}', 8, lv, model='liquid', tex='water_flow', tint='water', hard=100)
b('FALLING_WATER', 8, 8, model='liquid', tex='water_flow', tint='water', hard=100)
for lv in (4, 6):
    b(f'FLOWING_LAVA_{lv}', 10, lv, model='liquid', tex='lava_flow', light=15, hard=100)
b('FALLING_LAVA', 10, 8, model='liquid', tex='lava_flow', light=15, hard=100)
# stairs facing the other ways (BlockStairs: 0 east, 1 west, 2 south, 3 north), doors (BlockDoor: lower half,
# facing 0 east 1 south 2 west 3 north, + 4 open; the upper half takes its shape from the lower)
for v, d in [(1, 'W'), (2, 'S'), (3, 'N')]:
    b(f'OAK_STAIRS_{d}', 53, v, model='stairs', tex='planks_oak', hard=2.0, tool='axe')
    b(f'COBBLESTONE_STAIRS_{d}', 67, v, model='stairs', tex='cobblestone', hard=2.0, tool='pickaxe')
for v, d in [(1, 'S'), (2, 'W'), (3, 'N'), (4, 'E_OPEN'), (5, 'S_OPEN'), (6, 'W_OPEN'), (7, 'N_OPEN')]:
    b(f'DOOR_OAK_LOWER_{d}', 64, v, model='door', tex='door_wood_lower', hard=3.0, tool='axe')
# (new blocks go last: worlds keep blocks by their place in this list)
b('FIRE', 51, 0, model='cross', tex='fire_layer_0', light=15, hard=0)

assert len(S) <= 255, len(S)

MODELS = ['none', 'cube', 'cross', 'liquid', 'torch', 'slab', 'layer', 'cactus', 'leaves', 'glass', 'flat', 'vine',
          'pane', 'fence', 'door', 'ladder', 'stairs', 'chest', 'bed']
TOOLS = [None, 'pickaxe', 'axe', 'shovel', 'shears', 'sword']


def header():
    out = ['/* Generated by tools/blocks.py: the block states NumBlocks knows. */',
           '#ifndef NB_BLOCKS_H', '#define NB_BLOCKS_H', '#include <stdint.h>', '',
           'enum {']
    for i, s in enumerate(S):
        out.append(f'  B_{s["name"]} = {i},   /* {s["id"]}:{s["meta"]} */')
    out += [f'  B_COUNT = {len(S)}', '};', '']
    out.append('enum { ' + ', '.join(f'M_{m.upper()}' for m in MODELS) + ' };')
    out.append('enum { T_NONE, ' + ', '.join(f'T_{t.upper()}' for t in TOOLS[1:]) + ' };')
    out += ['', '#endif', '']
    return '\n'.join(out)


if __name__ == '__main__':
    if '--header' in sys.argv:
        sys.stdout.write(header())
    else:
        for i, s in enumerate(S):
            print(i, s['name'], f"{s['id']}:{s['meta']}", s['model'])
