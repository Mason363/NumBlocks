#!/usr/bin/env python3
"""Minecraft 1.8.8's names for blocks and items, for the commands (/give, /setblock,
/clear) and their completion: writes src/names.c.

    python3 tools/names.py > src/names.c

Blocks go by their registry name or numeric id (Block.blockRegistry), items by
theirs (Item.itemRegistry, where a block's item has the block's name). Only what
NumBlocks has is listed."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blocks  # noqa: E402
import items  # noqa: E402

# Block.registerBlocks: the numeric ids 0..197
BLOCK = '''air stone grass dirt cobblestone planks sapling bedrock flowing_water water flowing_lava lava sand gravel
gold_ore iron_ore coal_ore log leaves sponge glass lapis_ore lapis_block dispenser sandstone noteblock bed golden_rail
detector_rail sticky_piston web tallgrass deadbush piston piston_head wool piston_extension yellow_flower red_flower
brown_mushroom red_mushroom gold_block iron_block double_stone_slab stone_slab brick_block tnt bookshelf
mossy_cobblestone obsidian torch fire mob_spawner oak_stairs chest redstone_wire diamond_ore diamond_block
crafting_table wheat farmland furnace lit_furnace standing_sign wooden_door ladder rail stone_stairs wall_sign lever
stone_pressure_plate iron_door wooden_pressure_plate redstone_ore lit_redstone_ore unlit_redstone_torch redstone_torch
stone_button snow_layer ice snow cactus clay reeds jukebox fence pumpkin netherrack soul_sand glowstone portal
lit_pumpkin cake unpowered_repeater powered_repeater stained_glass trapdoor monster_egg stonebrick
brown_mushroom_block red_mushroom_block iron_bars glass_pane melon_block pumpkin_stem melon_stem vine fence_gate
brick_stairs stone_brick_stairs mycelium waterlily nether_brick nether_brick_fence nether_brick_stairs nether_wart
enchanting_table brewing_stand cauldron end_portal end_portal_frame end_stone dragon_egg redstone_lamp
lit_redstone_lamp double_wooden_slab wooden_slab cocoa sandstone_stairs emerald_ore ender_chest tripwire_hook tripwire
emerald_block spruce_stairs birch_stairs jungle_stairs command_block beacon cobblestone_wall flower_pot carrots
potatoes wooden_button skull anvil trapped_chest light_weighted_pressure_plate heavy_weighted_pressure_plate
unpowered_comparator powered_comparator daylight_detector redstone_block quartz_ore hopper quartz_block quartz_stairs
activator_rail dropper stained_hardened_clay stained_glass_pane leaves2 log2 acacia_stairs dark_oak_stairs slime
barrier iron_trapdoor prismarine sea_lantern hay_block carpet hardened_clay coal_block packed_ice double_plant
standing_banner wall_banner daylight_detector_inverted red_sandstone red_sandstone_stairs double_stone_slab2
stone_slab2 spruce_fence_gate birch_fence_gate jungle_fence_gate dark_oak_fence_gate acacia_fence_gate spruce_fence
birch_fence jungle_fence dark_oak_fence acacia_fence spruce_door birch_door jungle_door acacia_door
dark_oak_door'''.split()
assert len(BLOCK) == 198, len(BLOCK)

# Item.registerItems: the numeric ids 256..431 (426 unused)
ITEM = '''iron_shovel iron_pickaxe iron_axe flint_and_steel apple bow arrow coal diamond iron_ingot gold_ingot iron_sword
wooden_sword wooden_shovel wooden_pickaxe wooden_axe stone_sword stone_shovel stone_pickaxe stone_axe diamond_sword
diamond_shovel diamond_pickaxe diamond_axe stick bowl mushroom_stew golden_sword golden_shovel golden_pickaxe
golden_axe string feather gunpowder wooden_hoe stone_hoe iron_hoe diamond_hoe golden_hoe wheat_seeds wheat bread
leather_helmet leather_chestplate leather_leggings leather_boots chainmail_helmet chainmail_chestplate
chainmail_leggings chainmail_boots iron_helmet iron_chestplate iron_leggings iron_boots diamond_helmet
diamond_chestplate diamond_leggings diamond_boots golden_helmet golden_chestplate golden_leggings golden_boots flint
porkchop cooked_porkchop painting golden_apple sign wooden_door bucket water_bucket lava_bucket minecart saddle
iron_door redstone snowball boat leather milk_bucket brick clay_ball reeds paper book slime_ball chest_minecart
furnace_minecart egg compass fishing_rod clock glowstone_dust fish cooked_fish dye bone sugar cake bed repeater cookie
filled_map shears melon pumpkin_seeds melon_seeds beef cooked_beef chicken cooked_chicken rotten_flesh ender_pearl
blaze_rod ghast_tear gold_nugget nether_wart potion glass_bottle spider_eye fermented_spider_eye blaze_powder
magma_cream brewing_stand cauldron ender_eye speckled_melon spawn_egg experience_bottle fire_charge writable_book
written_book emerald item_frame flower_pot carrot potato baked_potato poisonous_potato map golden_carrot skull
carrot_on_a_stick nether_star pumpkin_pie fireworks firework_charge enchanted_book comparator netherbrick quartz
tnt_minecart hopper_minecart prismarine_shard prismarine_crystals rabbit cooked_rabbit rabbit_stew rabbit_foot
rabbit_hide armor_stand iron_horse_armor golden_horse_armor diamond_horse_armor lead name_tag command_block_minecart
mutton cooked_mutton banner - spruce_door birch_door jungle_door acacia_door dark_oak_door'''.split()
assert len(ITEM) == 176, len(ITEM)

# NumBlocks' items that Minecraft names differently, or that share a name (told apart by their data value)
DYES = ['BLACK', 'RED', 'GREEN', 'BROWN', 'BLUE', 'PURPLE', 'CYAN', 'SILVER', 'GRAY', 'PINK', 'LIME', 'YELLOW',
        'LIGHT_BLUE', 'MAGENTA', 'ORANGE', 'WHITE']
RENAME = {'CHARCOAL': ('coal', 1), 'MELON_SLICE': ('melon', 0), 'RAW_FISH': ('fish', 0), 'RAW_SALMON': ('fish', 1),
          'CLOWNFISH': ('fish', 2), 'PUFFERFISH': ('fish', 3), 'COOKED_FISH': ('cooked_fish', 0),
          'COOKED_SALMON': ('cooked_fish', 1)}
for k, d in enumerate(DYES):
    RENAME['DYE_' + d] = ('dye', k)
# a block's item has data 0 where the block's data is only its facing
FACING_ONLY = {50, 54, 61, 65}


def main():
    give = []   # (mc id << 4 | data, NumBlocks id)
    have_block = set(s['id'] for s in blocks.S)
    item_block = set()
    for i, s in enumerate(blocks.S):
        if items.tab_of(i) is None:
            continue
        give.append(((s['id'] << 4) | (0 if s['id'] in FACING_ONLY else s['meta'] & 15), i))
        item_block.add(s['id'])
    have_item = set()
    for k, it in enumerate(items.I):
        name, data = RENAME.get(it['name'], (it['name'].lower(), 0))
        assert name in ITEM, name
        mid = 256 + ITEM.index(name)
        give.append(((mid << 4) | data, 256 + k))
        have_item.add(mid)
    give.sort()
    keys = [g[0] for g in give]
    assert len(keys) == len(set(keys)), 'two things with one id and data'
    out = ['/* Generated by tools/names.py: Minecraft 1.8.8\'s names for blocks and items (see nb.h). */',
           '#include "nb.h"', '']
    bl = [BLOCK[i] if i in have_block else '' for i in range(198)]
    il = [ITEM[i] if 256 + i in have_item else '' for i in range(176)]
    out.append('const char *const mc_block_name[198] = {' + ','.join(f'"{n}"' for n in bl) + '};')
    out.append('const char *const mc_item_name[176] = {' + ','.join(f'"{n}"' for n in il) + '};')
    out.append('/* the blocks that are items too (their names are items\' names) */')
    mask = [0] * 25
    for i in item_block:
        mask[i >> 3] |= 1 << (i & 7)
    out.append('const uint8_t mc_block_item[25] = {' + ','.join(str(v) for v in mask) + '};')
    out.append(f'const uint16_t give_key[{len(give)}] = {{' + ','.join(str(g[0]) for g in give) + '};')
    out.append(f'const uint16_t give_id[{len(give)}] = {{' + ','.join(str(g[1]) for g in give) + '};')
    out.append(f'const int n_give = {len(give)};')
    sys.stdout.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
