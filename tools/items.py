#!/usr/bin/env python3
"""The items NumBlocks knows, their recipes, smelting and fuel, in one place
(Minecraft 1.8.8's values: Item.java, ItemFood, ItemArmor, ItemTool,
CraftingManager and the Recipes* classes, RecipesFurnace, TileEntityFurnace).

An item id is a block state (0 .. B_COUNT - 1, the blocks of tools/blocks.py
that can be held) or 256 + the index of an item below. tools/pack.py writes
the tables into src/data.c; `items.py --header` writes src/items.h."""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blocks  # noqa: E402

KINDS = ['none', 'block', 'pickaxe', 'axe', 'shovel', 'hoe', 'sword', 'shears', 'helmet', 'chestplate', 'leggings',
         'boots', 'food', 'stew', 'bucket', 'water_bucket', 'lava_bucket', 'milk_bucket', 'placer', 'flint_and_steel',
         'bow', 'bone_meal', 'snowball', 'egg', 'fishing_rod']
# tool materials: harvest level, uses, efficiency, attack bonus (Item.EnumToolMaterial)
TOOL_MAT = {'wood': (0, 59, 2, 0), 'stone': (1, 131, 4, 1), 'iron': (2, 250, 6, 2), 'diamond': (3, 1561, 8, 3),
            'gold': (0, 32, 12, 0)}
# armor materials: durability factor, points (helmet, chestplate, leggings, boots) (ItemArmor.EnumArmorMaterial)
ARMOR_MAT = {'leather': (5, (1, 3, 2, 1)), 'chainmail': (15, (2, 5, 4, 1)), 'iron': (15, (2, 6, 5, 2)),
             'gold': (7, (2, 5, 3, 1)), 'diamond': (33, (3, 8, 6, 3))}
ARMOR_BASE = (11, 16, 15, 13)
TOOL_DAMAGE = {'sword': 4, 'axe': 3, 'pickaxe': 2, 'shovel': 1, 'hoe': 0}

I = []


def item(name, label, tex, kind='none', stack=64, dur=0, tier=0, a=0, b=0, place=None, fuel=0):
    I.append(dict(name=name, label=label, tex=tex, kind=kind, stack=stack, dur=dur, tier=tier, a=a, b=b, place=place,
                  fuel=fuel))


def food(name, label, tex, hunger, sat, stack=64, kind='food', place=None):
    item(name, label, tex, kind, stack=stack, a=hunger, b=int(round(sat * 10)), place=place)


# --- tools: tier = the material's index in TOOL_MAT; a = attack damage (half hearts, with the fist's 1)
for mat, tex in [('wood', 'wood'), ('stone', 'stone'), ('iron', 'iron'), ('diamond', 'diamond'), ('gold', 'gold')]:
    lvl, uses, eff, dmg = TOOL_MAT[mat]
    up = {'wood': 'WOODEN', 'gold': 'GOLDEN'}.get(mat, mat.upper())
    lab = {'wood': 'Wooden', 'gold': 'Golden'}.get(mat, mat.capitalize())
    for kind in ['sword', 'shovel', 'pickaxe', 'axe', 'hoe']:
        item(f'{up}_{kind.upper()}', f'{lab} {kind.capitalize()}', f'{tex}_{kind}', kind, stack=1, dur=uses,
             tier=list(TOOL_MAT).index(mat), a=1 + (TOOL_DAMAGE[kind] + dmg if kind != 'hoe' else 0),
             fuel=200 if mat == 'wood' else 0)
item('SHEARS', 'Shears', 'shears', 'shears', stack=1, dur=238)
item('FLINT_AND_STEEL', 'Flint and Steel', 'flint_and_steel', 'flint_and_steel', stack=1, dur=64)
item('BOW', 'Bow', 'bow_standby', 'bow', stack=1, dur=384, fuel=0)
item('ARROW', 'Arrow', 'arrow')
# --- armor: tier = the material's index in ARMOR_MAT; a = armor points
for mat in ARMOR_MAT:
    fac, pts = ARMOR_MAT[mat]
    tex = {'gold': 'gold'}.get(mat, mat)
    up = {'gold': 'GOLDEN'}.get(mat, mat.upper())
    lab = {'gold': 'Golden', 'leather': 'Leather', 'chainmail': 'Chain'}.get(mat, mat.capitalize())
    for k, part in enumerate(['helmet', 'chestplate', 'leggings', 'boots']):
        plab = {'chestplate': 'Chestplate' if mat != 'leather' else 'Tunic',
                'leggings': 'Leggings' if mat != 'leather' else 'Pants',
                'helmet': 'Helmet' if mat != 'leather' else 'Cap', 'boots': 'Boots'}[part]
        item(f'{up}_{part.upper()}', f'{lab} {plab}', f'{tex}_{part}', part, stack=1, dur=ARMOR_BASE[k] * fac,
             tier=list(ARMOR_MAT).index(mat), a=pts[k])
# --- materials
item('STICK', 'Stick', 'stick', fuel=100)
item('COAL', 'Coal', 'coal', fuel=1600)
item('CHARCOAL', 'Charcoal', 'charcoal', fuel=1600)
item('DIAMOND', 'Diamond', 'diamond')
item('IRON_INGOT', 'Iron Ingot', 'iron_ingot')
item('GOLD_INGOT', 'Gold Ingot', 'gold_ingot')
item('GOLD_NUGGET', 'Gold Nugget', 'gold_nugget')
item('EMERALD', 'Emerald', 'emerald')
item('REDSTONE', 'Redstone', 'redstone_dust')
item('FLINT', 'Flint', 'flint')
item('FEATHER', 'Feather', 'feather')
item('STRING', 'String', 'string')
item('BONE', 'Bone', 'bone')
item('GUNPOWDER', 'Gunpowder', 'gunpowder')
item('LEATHER', 'Leather', 'leather')
item('WHEAT', 'Wheat', 'wheat')
item('WHEAT_SEEDS', 'Seeds', 'seeds_wheat', 'placer', place='WHEAT_0')
item('REEDS', 'Sugar Canes', 'reeds', 'placer', place='SUGAR_CANE')
item('SUGAR', 'Sugar', 'sugar')
item('PAPER', 'Paper', 'paper')
item('BOOK', 'Book', 'book_normal')
item('CLAY_BALL', 'Clay', 'clay_ball')
item('BRICK', 'Brick', 'brick')
item('BOWL', 'Bowl', 'bowl')
item('SNOWBALL', 'Snowball', 'snowball', 'snowball', stack=16)
item('EGG', 'Egg', 'egg', 'egg', stack=16)
item('SLIME_BALL', 'Slimeball', 'slimeball')
item('GLOWSTONE_DUST', 'Glowstone Dust', 'glowstone_dust')
item('BUCKET', 'Bucket', 'bucket_empty', 'bucket', stack=16)
item('WATER_BUCKET', 'Water Bucket', 'bucket_water', 'water_bucket', stack=1)
item('LAVA_BUCKET', 'Lava Bucket', 'bucket_lava', 'lava_bucket', stack=1, fuel=20000)
item('MILK_BUCKET', 'Milk', 'bucket_milk', 'milk_bucket', stack=1)
item('WOODEN_DOOR', 'Oak Door', 'door_wood', 'placer', place='DOOR_OAK_LOWER')
item('BED', 'Bed', 'bed', 'placer', stack=1, place='BED_FOOT')
item('COMPASS', 'Compass', 'compass')
item('FISHING_ROD', 'Fishing Rod', 'fishing_rod_uncast', 'fishing_rod', stack=1, dur=64)
item('CLOCK', 'Clock', 'clock')
# dyes, in Minecraft's damage order (0 black (ink sac) .. 15 white (bone meal))
DYES = [('BLACK', 'Ink Sac', 'black'), ('RED', 'Rose Red', 'red'), ('GREEN', 'Cactus Green', 'green'),
        ('BROWN', 'Cocoa Beans', 'brown'), ('BLUE', 'Lapis Lazuli', 'blue'), ('PURPLE', 'Purple Dye', 'purple'),
        ('CYAN', 'Cyan Dye', 'cyan'), ('SILVER', 'Light Gray Dye', 'silver'), ('GRAY', 'Gray Dye', 'gray'),
        ('PINK', 'Pink Dye', 'pink'), ('LIME', 'Lime Dye', 'lime'), ('YELLOW', 'Dandelion Yellow', 'yellow'),
        ('LIGHT_BLUE', 'Light Blue Dye', 'light_blue'), ('MAGENTA', 'Magenta Dye', 'magenta'),
        ('ORANGE', 'Orange Dye', 'orange'), ('WHITE', 'Bone Meal', 'white')]
for n, lab, t in DYES:
    item(f'DYE_{n}', lab, f'dye_powder_{t}', 'bone_meal' if n == 'WHITE' else 'none')
# --- food: hunger (half drumsticks), saturation modifier (ItemFood)
food('APPLE', 'Apple', 'apple', 4, 0.3)
food('GOLDEN_APPLE', 'Golden Apple', 'apple_golden', 4, 1.2)
food('BREAD', 'Bread', 'bread', 5, 0.6)
food('PORKCHOP', 'Raw Porkchop', 'porkchop_raw', 3, 0.3)
food('COOKED_PORKCHOP', 'Cooked Porkchop', 'porkchop_cooked', 8, 0.8)
food('BEEF', 'Raw Beef', 'beef_raw', 3, 0.3)
food('COOKED_BEEF', 'Steak', 'beef_cooked', 8, 0.8)
food('CHICKEN', 'Raw Chicken', 'chicken_raw', 2, 0.3)
food('COOKED_CHICKEN', 'Cooked Chicken', 'chicken_cooked', 6, 0.6)
food('MUTTON', 'Raw Mutton', 'mutton_raw', 2, 0.3)
food('COOKED_MUTTON', 'Cooked Mutton', 'mutton_cooked', 6, 0.8)
food('ROTTEN_FLESH', 'Rotten Flesh', 'rotten_flesh', 4, 0.1)
food('SPIDER_EYE', 'Spider Eye', 'spider_eye', 2, 0.8)
food('MUSHROOM_STEW', 'Mushroom Stew', 'mushroom_stew', 6, 0.6, stack=1, kind='stew')
food('COOKIE', 'Cookie', 'cookie', 2, 0.1)
food('MELON_SLICE', 'Melon', 'melon', 2, 0.3)
food('CARROT', 'Carrot', 'carrot', 3, 0.6, place='CARROTS_0')
food('POTATO', 'Potato', 'potato', 1, 0.3, place='POTATOES_0')
food('BAKED_POTATO', 'Baked Potato', 'potato_baked', 5, 0.6)
food('POISONOUS_POTATO', 'Poisonous Potato', 'potato_poisonous', 2, 0.3)
food('PUMPKIN_PIE', 'Pumpkin Pie', 'pumpkin_pie', 8, 0.3)
food('RAW_FISH', 'Raw Fish', 'fish_cod_raw', 2, 0.1)
food('RAW_SALMON', 'Raw Salmon', 'fish_salmon_raw', 2, 0.1)
food('CLOWNFISH', 'Clownfish', 'fish_clownfish_raw', 1, 0.1)
food('PUFFERFISH', 'Pufferfish', 'fish_pufferfish_raw', 1, 0.1)
food('COOKED_FISH', 'Cooked Fish', 'fish_cod_cooked', 5, 0.6)
food('COOKED_SALMON', 'Cooked Salmon', 'fish_salmon_cooked', 6, 0.8)

INAMES = [x['name'] for x in I]
assert len(set(INAMES)) == len(INAMES)

# --- the blocks you can hold: their names (en_US) and the item a block state counts as
LABELS = {
    'STONE': 'Stone', 'GRANITE': 'Granite', 'POLISHED_GRANITE': 'Polished Granite', 'DIORITE': 'Diorite',
    'POLISHED_DIORITE': 'Polished Diorite', 'ANDESITE': 'Andesite', 'POLISHED_ANDESITE': 'Polished Andesite',
    'GRASS': 'Grass Block', 'DIRT': 'Dirt', 'COARSE_DIRT': 'Coarse Dirt', 'PODZOL': 'Podzol',
    'COBBLESTONE': 'Cobblestone', 'BEDROCK': 'Bedrock', 'SAND': 'Sand', 'RED_SAND': 'Red Sand', 'GRAVEL': 'Gravel',
    'GOLD_ORE': 'Gold Ore', 'IRON_ORE': 'Iron Ore', 'COAL_ORE': 'Coal Ore', 'SPONGE': 'Sponge', 'GLASS': 'Glass',
    'LAPIS_ORE': 'Lapis Lazuli Ore', 'LAPIS_BLOCK': 'Lapis Lazuli Block', 'SANDSTONE': 'Sandstone',
    'SANDSTONE_CHISELED': 'Chiseled Sandstone', 'SANDSTONE_SMOOTH': 'Smooth Sandstone', 'COBWEB': 'Cobweb',
    'DEAD_SHRUB': 'Shrub', 'TALL_GRASS': 'Grass', 'FERN': 'Fern', 'DEAD_BUSH': 'Dead Bush', 'DANDELION': 'Dandelion',
    'POPPY': 'Poppy', 'BLUE_ORCHID': 'Blue Orchid', 'ALLIUM': 'Allium', 'AZURE_BLUET': 'Azure Bluet',
    'RED_TULIP': 'Red Tulip', 'ORANGE_TULIP': 'Orange Tulip', 'WHITE_TULIP': 'White Tulip', 'PINK_TULIP': 'Pink Tulip',
    'OXEYE_DAISY': 'Oxeye Daisy', 'BROWN_MUSHROOM': 'Mushroom', 'RED_MUSHROOM': 'Mushroom',
    'GOLD_BLOCK': 'Block of Gold', 'IRON_BLOCK': 'Block of Iron', 'STONE_SLAB_DOUBLE': 'Stone Slab',
    'STONE_SLAB': 'Stone Slab', 'SANDSTONE_SLAB': 'Sandstone Slab', 'WOOD_SLAB_OLD': 'Wooden Slab',
    'COBBLESTONE_SLAB': 'Cobblestone Slab', 'BRICK_SLAB': 'Bricks Slab', 'STONE_BRICK_SLAB': 'Stone Bricks Slab',
    'BRICKS': 'Bricks', 'TNT': 'TNT', 'BOOKSHELF': 'Bookshelf', 'MOSSY_COBBLESTONE': 'Moss Stone',
    'OBSIDIAN': 'Obsidian', 'TORCH': 'Torch', 'MOB_SPAWNER': 'Monster Spawner', 'OAK_STAIRS': 'Oak Wood Stairs',
    'CHEST': 'Chest', 'DIAMOND_ORE': 'Diamond Ore', 'DIAMOND_BLOCK': 'Block of Diamond',
    'CRAFTING_TABLE': 'Crafting Table', 'FARMLAND': 'Farmland', 'FURNACE': 'Furnace', 'LADDER': 'Ladder',
    'RAIL': 'Rail', 'COBBLESTONE_STAIRS': 'Cobblestone Stairs', 'REDSTONE_ORE': 'Redstone Ore', 'SNOW_LAYER': 'Snow',
    'ICE': 'Ice', 'SNOW': 'Snow', 'CACTUS': 'Cactus', 'CLAY': 'Clay', 'FENCE_OAK': 'Oak Fence', 'PUMPKIN': 'Pumpkin',
    'JACK_O_LANTERN': "Jack o'Lantern", 'STONE_BRICKS': 'Stone Bricks', 'MOSSY_STONE_BRICKS': 'Mossy Stone Bricks',
    'CRACKED_STONE_BRICKS': 'Cracked Stone Bricks', 'CHISELED_STONE_BRICKS': 'Chiseled Stone Bricks',
    'BROWN_MUSHROOM_CAP': 'Mushroom', 'RED_MUSHROOM_CAP': 'Mushroom', 'GLASS_PANE': 'Glass Pane', 'MELON': 'Melon',
    'VINE': 'Vines', 'MYCELIUM': 'Mycelium', 'LILY_PAD': 'Lily Pad', 'EMERALD_ORE': 'Emerald Ore',
    'EMERALD_BLOCK': 'Block of Emerald', 'COBBLESTONE_WALL': 'Cobblestone Wall', 'HARDENED_CLAY': 'Hardened Clay',
    'HAY_BALE': 'Hay Bale', 'COAL_BLOCK': 'Block of Coal', 'PACKED_ICE': 'Packed Ice', 'SUNFLOWER_LOWER': 'Sunflower',
    'LILAC_LOWER': 'Lilac', 'DOUBLE_GRASS_LOWER': 'Double Tallgrass', 'LARGE_FERN_LOWER': 'Large Fern',
    'ROSE_BUSH_LOWER': 'Rose Bush', 'PEONY_LOWER': 'Peony', 'RED_SANDSTONE': 'Red Sandstone', 'GLOWSTONE': 'Glowstone',
}
for n, t in [('OAK', 'Oak'), ('SPRUCE', 'Spruce'), ('BIRCH', 'Birch'), ('JUNGLE', 'Jungle'), ('ACACIA', 'Acacia'),
             ('DARK_OAK', 'Dark Oak')]:
    LABELS[f'PLANKS_{n}'] = f'{t} Wood Planks'
    LABELS[f'SAPLING_{n}'] = f'{t} Sapling'
    LABELS[f'LOG_{n}'] = f'{t} Wood'
    LABELS[f'LEAVES_{n}'] = f'{t} Leaves'
    LABELS[f'WOOD_SLAB_{n}'] = f'{t} Wood Slab'
COLORS = ['WHITE', 'ORANGE', 'MAGENTA', 'LIGHT_BLUE', 'YELLOW', 'LIME', 'PINK', 'GRAY', 'SILVER', 'CYAN', 'PURPLE',
          'BLUE', 'BROWN', 'GREEN', 'RED', 'BLACK']
CLAB = ['White', 'Orange', 'Magenta', 'Light Blue', 'Yellow', 'Lime', 'Pink', 'Gray', 'Light Gray', 'Cyan', 'Purple',
        'Blue', 'Brown', 'Green', 'Red', 'Black']
for c, l in zip(COLORS, CLAB):
    LABELS[f'WOOL_{c}'] = 'Wool' if c == 'WHITE' else f'{l} Wool'
    LABELS[f'STAINED_CLAY_{c}'] = f'{l} Stained Clay'

# block states that stand for another one in the inventory
ITEM_OF = {'OAK_STAIRS_W': 'OAK_STAIRS', 'OAK_STAIRS_S': 'OAK_STAIRS', 'OAK_STAIRS_N': 'OAK_STAIRS',
           'COBBLESTONE_STAIRS_W': 'COBBLESTONE_STAIRS', 'COBBLESTONE_STAIRS_S': 'COBBLESTONE_STAIRS',
           'COBBLESTONE_STAIRS_N': 'COBBLESTONE_STAIRS', 'GRASS_SNOWED': 'GRASS', 'FURNACE_LIT': 'FURNACE', 'TORCH_E': 'TORCH', 'TORCH_W': 'TORCH',
           'TORCH_S': 'TORCH', 'TORCH_N': 'TORCH', 'FARMLAND_WET': 'FARMLAND', 'DOOR_OAK_LOWER': 'I:WOODEN_DOOR',
           'DOOR_OAK_UPPER': 'I:WOODEN_DOOR', 'BED_FOOT': 'I:BED', 'BED_HEAD': 'I:BED', 'SUGAR_CANE': 'I:REEDS',
           'BROWN_MUSHROOM_INSIDE': 'BROWN_MUSHROOM_CAP', 'BROWN_MUSHROOM_STEM': 'BROWN_MUSHROOM_CAP',
           'RED_MUSHROOM_INSIDE': 'RED_MUSHROOM_CAP', 'RED_MUSHROOM_STEM': 'RED_MUSHROOM_CAP',
           'MONSTER_EGG_STONE': 'STONE'}
for v in ['OAK', 'SPRUCE', 'BIRCH', 'JUNGLE', 'ACACIA', 'DARK_OAK']:
    for s in ['_X', '_Z', '_BARK']:
        ITEM_OF[f'LOG_{v}{s}'] = f'LOG_{v}'
for n in ['SUNFLOWER', 'LILAC', 'DOUBLE_GRASS', 'LARGE_FERN', 'ROSE_BUSH', 'PEONY']:
    ITEM_OF[f'{n}_UPPER'] = f'{n}_LOWER'
for d in ['S', 'W', 'N', 'E_OPEN', 'S_OPEN', 'W_OPEN', 'N_OPEN']:
    ITEM_OF[f'DOOR_OAK_LOWER_{d}'] = 'I:WOODEN_DOOR'
for st in range(8):
    ITEM_OF[f'WHEAT_{st}'] = 'I:WHEAT_SEEDS'
for st in range(4):
    ITEM_OF[f'CARROTS_{st}'] = 'I:CARROT'
    ITEM_OF[f'POTATOES_{st}'] = 'I:POTATO'
for n in ['WATER', 'LAVA', 'FLOWING_WATER', 'FLOWING_LAVA']:
    ITEM_OF[n] = None
for s in blocks.S:
    if s['name'].startswith(('FLOWING_WATER', 'FLOWING_LAVA', 'FALLING_')):
        ITEM_OF[s['name']] = None
ITEM_OF['AIR'] = None

BNAMES = [s['name'] for s in blocks.S]


def resolve(n):
    """an item id from a name: 'I:NAME' or a known item name is an item, else a block state"""
    if n.startswith('I:'):
        return 256 + INAMES.index(n[2:])
    if n in INAMES:
        assert n not in BNAMES, n
        return 256 + INAMES.index(n)
    if n.startswith('G_'):
        return GROUPS[n]
    return BNAMES.index(n)


def block_item(name):
    """the item a block state counts as (None: none)"""
    if name in ITEM_OF:
        t = ITEM_OF[name]
        return None if t is None else resolve(t)
    return BNAMES.index(name)


# ingredient groups (Minecraft's "any damage" ingredients)
GROUPS = {'G_PLANKS': 0xF000, 'G_WOOL': 0xF001, 'G_LOG': 0xF002, 'G_SAPLING': 0xF003}
GROUP_OF = {
    'G_PLANKS': [n for n in BNAMES if n.startswith('PLANKS_')],
    'G_WOOL': [n for n in BNAMES if n.startswith('WOOL_')],
    'G_LOG': [n for n in BNAMES if n.startswith('LOG_') and not n.endswith(('_X', '_Z', '_BARK'))],
    'G_SAPLING': [n for n in BNAMES if n.startswith('SAPLING_')],
}

# --- crafting (CraftingManager and the Recipes* classes), only what NumBlocks has
R = []   # (out, count, rows, keys) shaped; (out, count, None, [ingredients]) shapeless


def shaped(out, n, rows, keys):
    R.append((out, n, rows, keys))


def shapeless(out, n, ins):
    R.append((out, n, None, ins))


TOOL_IN = {'WOODEN': 'G_PLANKS', 'STONE': 'COBBLESTONE', 'IRON': 'IRON_INGOT', 'DIAMOND': 'DIAMOND',
           'GOLDEN': 'GOLD_INGOT'}
for m, x in TOOL_IN.items():
    shaped(f'{m}_PICKAXE', 1, ['XXX', ' # ', ' # '], {'X': x, '#': 'STICK'})
    shaped(f'{m}_SHOVEL', 1, ['X', '#', '#'], {'X': x, '#': 'STICK'})
    shaped(f'{m}_AXE', 1, ['XX', 'X#', ' #'], {'X': x, '#': 'STICK'})
    shaped(f'{m}_HOE', 1, ['XX', ' #', ' #'], {'X': x, '#': 'STICK'})
    shaped(f'{m}_SWORD', 1, ['X', 'X', '#'], {'X': x, '#': 'STICK'})
shaped('SHEARS', 1, [' #', '# '], {'#': 'IRON_INGOT'})
shaped('BOW', 1, [' #X', '# X', ' #X'], {'X': 'STRING', '#': 'STICK'})
shaped('FISHING_ROD', 1, ['  #', ' #X', '# X'], {'#': 'STICK', 'X': 'STRING'})
shaped('ARROW', 4, ['X', '#', 'Y'], {'Y': 'FEATHER', 'X': 'FLINT', '#': 'STICK'})
for m, x in [('LEATHER', 'LEATHER'), ('IRON', 'IRON_INGOT'), ('DIAMOND', 'DIAMOND'), ('GOLDEN', 'GOLD_INGOT')]:
    shaped(f'{m}_HELMET', 1, ['XXX', 'X X'], {'X': x})
    shaped(f'{m}_CHESTPLATE', 1, ['X X', 'XXX', 'XXX'], {'X': x})
    shaped(f'{m}_LEGGINGS', 1, ['XXX', 'X X', 'X X'], {'X': x})
    shaped(f'{m}_BOOTS', 1, ['X X', 'X X'], {'X': x})
for blk, it in [('GOLD_BLOCK', 'GOLD_INGOT'), ('IRON_BLOCK', 'IRON_INGOT'), ('DIAMOND_BLOCK', 'DIAMOND'),
                ('EMERALD_BLOCK', 'EMERALD'), ('LAPIS_BLOCK', 'DYE_BLUE'), ('COAL_BLOCK', 'COAL'),
                ('HAY_BALE', 'WHEAT')]:
    shaped(blk, 1, ['###', '###', '###'], {'#': it})
    shaped(it, 9, ['#'], {'#': blk})
shaped('GOLD_INGOT', 1, ['###', '###', '###'], {'#': 'GOLD_NUGGET'})
shaped('GOLD_NUGGET', 9, ['#'], {'#': 'GOLD_INGOT'})
shapeless('MUSHROOM_STEW', 1, ['BROWN_MUSHROOM', 'RED_MUSHROOM', 'BOWL'])
shaped('COOKIE', 8, ['#X#'], {'X': 'DYE_BROWN', '#': 'WHEAT'})
shaped('MELON', 1, ['MMM', 'MMM', 'MMM'], {'M': 'MELON_SLICE'})
shapeless('PUMPKIN_PIE', 1, ['PUMPKIN', 'SUGAR', 'EGG'])
shaped('CHEST', 1, ['###', '# #', '###'], {'#': 'G_PLANKS'})
shaped('FURNACE', 1, ['###', '# #', '###'], {'#': 'COBBLESTONE'})
shaped('CRAFTING_TABLE', 1, ['##', '##'], {'#': 'G_PLANKS'})
shaped('SANDSTONE', 1, ['##', '##'], {'#': 'SAND'})
shaped('RED_SANDSTONE', 1, ['##', '##'], {'#': 'RED_SAND'})
shaped('SANDSTONE_SMOOTH', 4, ['##', '##'], {'#': 'SANDSTONE'})
shaped('SANDSTONE_CHISELED', 1, ['#', '#'], {'#': 'SANDSTONE_SLAB'})
shaped('STONE_BRICKS', 4, ['##', '##'], {'#': 'STONE'})
shaped('CHISELED_STONE_BRICKS', 1, ['#', '#'], {'#': 'STONE_BRICK_SLAB'})
shapeless('MOSSY_STONE_BRICKS', 1, ['STONE_BRICKS', 'VINE'])
shapeless('MOSSY_COBBLESTONE', 1, ['COBBLESTONE', 'VINE'])
shaped('GLASS_PANE', 16, ['###', '###'], {'#': 'GLASS'})
shaped('COARSE_DIRT', 4, ['DG', 'GD'], {'D': 'DIRT', 'G': 'GRAVEL'})
shaped('POLISHED_DIORITE', 4, ['SS', 'SS'], {'S': 'DIORITE'})
shaped('POLISHED_GRANITE', 4, ['SS', 'SS'], {'S': 'GRANITE'})
shaped('POLISHED_ANDESITE', 4, ['SS', 'SS'], {'S': 'ANDESITE'})
shapeless('ANDESITE', 2, ['DIORITE', 'COBBLESTONE'])
for i, (n, lab, t) in enumerate(DYES):
    wool = 'WOOL_' + COLORS[15 - i]
    if wool != 'WOOL_WHITE':
        shapeless(wool, 1, [f'DYE_{n}', 'WOOL_WHITE'])
    shaped('STAINED_CLAY_' + COLORS[15 - i], 8, ['###', '#X#', '###'], {'#': 'HARDENED_CLAY', 'X': f'DYE_{n}'})
shapeless('DYE_YELLOW', 1, ['DANDELION'])
shapeless('DYE_RED', 1, ['POPPY'])
shapeless('DYE_WHITE', 3, ['BONE'])
shapeless('DYE_PINK', 2, ['DYE_RED', 'DYE_WHITE'])
shapeless('DYE_ORANGE', 2, ['DYE_RED', 'DYE_YELLOW'])
shapeless('DYE_LIME', 2, ['DYE_GREEN', 'DYE_WHITE'])
shapeless('DYE_GRAY', 2, ['DYE_BLACK', 'DYE_WHITE'])
shapeless('DYE_SILVER', 2, ['DYE_GRAY', 'DYE_WHITE'])
shapeless('DYE_SILVER', 3, ['DYE_BLACK', 'DYE_WHITE', 'DYE_WHITE'])
shapeless('DYE_LIGHT_BLUE', 2, ['DYE_BLUE', 'DYE_WHITE'])
shapeless('DYE_CYAN', 2, ['DYE_BLUE', 'DYE_GREEN'])
shapeless('DYE_PURPLE', 2, ['DYE_BLUE', 'DYE_RED'])
shapeless('DYE_MAGENTA', 2, ['DYE_PURPLE', 'DYE_PINK'])
shapeless('DYE_MAGENTA', 3, ['DYE_BLUE', 'DYE_RED', 'DYE_PINK'])
shapeless('DYE_MAGENTA', 4, ['DYE_BLUE', 'DYE_RED', 'DYE_RED', 'DYE_WHITE'])
shapeless('DYE_LIGHT_BLUE', 1, ['BLUE_ORCHID'])
shapeless('DYE_MAGENTA', 1, ['ALLIUM'])
shapeless('DYE_SILVER', 1, ['AZURE_BLUET'])
shapeless('DYE_RED', 1, ['RED_TULIP'])
shapeless('DYE_ORANGE', 1, ['ORANGE_TULIP'])
shapeless('DYE_SILVER', 1, ['WHITE_TULIP'])
shapeless('DYE_PINK', 1, ['PINK_TULIP'])
shapeless('DYE_SILVER', 1, ['OXEYE_DAISY'])
shapeless('DYE_YELLOW', 2, ['SUNFLOWER_LOWER'])
shapeless('DYE_MAGENTA', 2, ['LILAC_LOWER'])
shapeless('DYE_RED', 2, ['ROSE_BUSH_LOWER'])
shapeless('DYE_PINK', 2, ['PEONY_LOWER'])
shaped('PAPER', 3, ['###'], {'#': 'REEDS'})
shapeless('BOOK', 1, ['PAPER', 'PAPER', 'PAPER', 'LEATHER'])
shaped('FENCE_OAK', 3, ['W#W', 'W#W'], {'#': 'STICK', 'W': 'PLANKS_OAK'})
shaped('COBBLESTONE_WALL', 6, ['###', '###'], {'#': 'COBBLESTONE'})
shaped('BOOKSHELF', 1, ['###', 'XXX', '###'], {'#': 'G_PLANKS', 'X': 'BOOK'})
shaped('SNOW', 1, ['##', '##'], {'#': 'SNOWBALL'})
shaped('SNOW_LAYER', 6, ['###'], {'#': 'SNOW'})
shaped('CLAY', 1, ['##', '##'], {'#': 'CLAY_BALL'})
shaped('BRICKS', 1, ['##', '##'], {'#': 'BRICK'})
shaped('GLOWSTONE', 1, ['##', '##'], {'#': 'GLOWSTONE_DUST'})
shaped('WOOL_WHITE', 1, ['##', '##'], {'#': 'STRING'})
shaped('TNT', 1, ['X#X', '#X#', 'X#X'], {'X': 'GUNPOWDER', '#': 'SAND'})
shaped('COBBLESTONE_SLAB', 6, ['###'], {'#': 'COBBLESTONE'})
shaped('STONE_SLAB', 6, ['###'], {'#': 'STONE'})
shaped('SANDSTONE_SLAB', 6, ['###'], {'#': 'SANDSTONE'})
shaped('BRICK_SLAB', 6, ['###'], {'#': 'BRICKS'})
shaped('STONE_BRICK_SLAB', 6, ['###'], {'#': 'STONE_BRICKS'})
for n in ['OAK', 'SPRUCE', 'BIRCH', 'JUNGLE', 'ACACIA', 'DARK_OAK']:
    if f'WOOD_SLAB_{n}' in BNAMES:
        shaped(f'WOOD_SLAB_{n}', 6, ['###'], {'#': f'PLANKS_{n}'})
    shaped(f'PLANKS_{n}', 4, ['#'], {'#': f'LOG_{n}'})
shaped('LADDER', 3, ['# #', '###', '# #'], {'#': 'STICK'})
shaped('WOODEN_DOOR', 3, ['##', '##', '##'], {'#': 'PLANKS_OAK'})
shaped('SUGAR', 1, ['#'], {'#': 'REEDS'})
shaped('STICK', 4, ['#', '#'], {'#': 'G_PLANKS'})
shaped('TORCH', 4, ['X', '#'], {'X': 'COAL', '#': 'STICK'})
shaped('TORCH', 4, ['X', '#'], {'X': 'CHARCOAL', '#': 'STICK'})
shaped('BOWL', 4, ['# #', ' # '], {'#': 'G_PLANKS'})
shaped('RAIL', 16, ['X X', 'X#X', 'X X'], {'X': 'IRON_INGOT', '#': 'STICK'})
shaped('JACK_O_LANTERN', 1, ['A', 'B'], {'A': 'PUMPKIN', 'B': 'TORCH'})
shaped('BUCKET', 1, ['# #', ' # '], {'#': 'IRON_INGOT'})
shapeless('FLINT_AND_STEEL', 1, ['IRON_INGOT', 'FLINT'])
shaped('BREAD', 1, ['###'], {'#': 'WHEAT'})
shaped('OAK_STAIRS', 4, ['#  ', '## ', '###'], {'#': 'PLANKS_OAK'})
shaped('COBBLESTONE_STAIRS', 4, ['#  ', '## ', '###'], {'#': 'COBBLESTONE'})
shaped('GOLDEN_APPLE', 1, ['###', '#X#', '###'], {'#': 'GOLD_INGOT', 'X': 'APPLE'})
shaped('CLOCK', 1, [' # ', '#X#', ' # '], {'#': 'GOLD_INGOT', 'X': 'REDSTONE'})
shaped('COMPASS', 1, [' # ', '#X#', ' # '], {'#': 'IRON_INGOT', 'X': 'REDSTONE'})
shaped('BED', 1, ['###', 'XXX'], {'#': 'G_WOOL', 'X': 'G_PLANKS'})

# --- smelting (RecipesFurnace): input, output
SMELT = [('IRON_ORE', 'IRON_INGOT'), ('GOLD_ORE', 'GOLD_INGOT'), ('DIAMOND_ORE', 'DIAMOND'), ('SAND', 'GLASS'),
         ('RED_SAND', 'GLASS'), ('PORKCHOP', 'COOKED_PORKCHOP'), ('BEEF', 'COOKED_BEEF'),
         ('CHICKEN', 'COOKED_CHICKEN'), ('MUTTON', 'COOKED_MUTTON'), ('COBBLESTONE', 'STONE'),
         ('STONE_BRICKS', 'CRACKED_STONE_BRICKS'), ('CLAY_BALL', 'BRICK'), ('CLAY', 'HARDENED_CLAY'),
         ('CACTUS', 'DYE_GREEN'), ('EMERALD_ORE', 'EMERALD'), ('POTATO', 'BAKED_POTATO'), ('COAL_ORE', 'COAL'),
         ('REDSTONE_ORE', 'REDSTONE'), ('LAPIS_ORE', 'DYE_BLUE'), ('RAW_FISH', 'COOKED_FISH'),
         ('RAW_SALMON', 'COOKED_SALMON')]
SMELT += [(n, 'CHARCOAL') for n in GROUP_OF['G_LOG']]


def fuel_of(iid):
    """burn time in ticks (TileEntityFurnace.fuelTime)"""
    if iid >= 256:
        return I[iid - 256]['fuel']
    n = BNAMES[iid]
    if n == 'COAL_BLOCK':
        return 16000
    if n.startswith('WOOD_SLAB'):
        return 150
    if n.startswith('SAPLING_'):
        return 100
    if n.startswith(('PLANKS_', 'LOG_')) or n in ('CRAFTING_TABLE', 'CHEST', 'BOOKSHELF', 'OAK_STAIRS', 'FENCE_OAK',
                                                   'DOOR_OAK_LOWER', 'BROWN_MUSHROOM_CAP', 'RED_MUSHROOM_CAP'):
        return 300
    return 0


# the creative inventory's tabs (CreativeModeTab), in Minecraft's order; what NumBlocks puts in each
TABS = ['Building Blocks', 'Decoration Blocks', 'Redstone', 'Transportation', 'Miscellaneous', 'Search Items',
        'Foodstuffs', 'Tools', 'Combat', 'Brewing', 'Materials', 'Survival Inventory']
DECO = ('SAPLING_', 'LEAVES_', 'COBWEB', 'DEAD_SHRUB', 'TALL_GRASS', 'FERN', 'DEAD_BUSH', 'DANDELION', 'POPPY',
        'BLUE_ORCHID', 'ALLIUM', 'AZURE_BLUET', '_TULIP', 'OXEYE_DAISY', 'BROWN_MUSHROOM', 'RED_MUSHROOM', 'TORCH',
        'CHEST', 'CRAFTING_TABLE', 'FURNACE', 'LADDER', 'SNOW_LAYER', 'CACTUS', 'FENCE_OAK', 'GLASS_PANE', 'VINE',
        'LILY_PAD', 'SUNFLOWER_LOWER', 'LILAC_LOWER', 'DOUBLE_GRASS_LOWER', 'LARGE_FERN_LOWER', 'ROSE_BUSH_LOWER',
        'PEONY_LOWER', 'MONSTER_EGG_STONE')


def tab_of(iid):
    if iid < 256:
        n = BNAMES[iid]
        if ITEM_OF.get(n, n) != n or n not in LABELS:
            return None   # not in the creative inventory
        if n in ('TNT',):
            return 2
        if n == 'RAIL':
            return 3
        if n in ('BROWN_MUSHROOM_CAP', 'RED_MUSHROOM_CAP'):
            return None
        if any(n.startswith(d) or n.endswith(d) or n == d for d in DECO):
            if n in ('COBBLESTONE_WALL',):
                return 0
            return 1
        if n == 'COBBLESTONE_WALL':
            return 0
        return 0
    it = I[iid - 256]
    k = it['kind']
    if k in ('sword', 'bow', 'helmet', 'chestplate', 'leggings', 'boots') or it['name'] == 'ARROW':
        return 8
    if k in ('pickaxe', 'axe', 'shovel', 'hoe', 'shears', 'flint_and_steel', 'fishing_rod') or \
            it['name'] in ('COMPASS', 'CLOCK'):
        return 7
    if k in ('food', 'stew'):
        return 6
    if k in ('bucket', 'water_bucket', 'lava_bucket', 'milk_bucket') or it['name'] in ('PAPER', 'BOOK', 'SNOWBALL',
                                                                                         'SLIME_BALL'):
        return 4
    if it['name'] == 'WOODEN_DOOR':
        return 2
    if it['name'] == 'BED':
        return 1
    return 10


def header():
    out = ['/* Generated by tools/items.py: the items NumBlocks knows (ids 256 and up; below 256 a block state). */',
           '#ifndef NB_ITEMS_H', '#define NB_ITEMS_H', '#include <stdint.h>', '#include "blocks.h"', '', 'enum {']
    for k, x in enumerate(I):
        out.append(f'  I_{x["name"]} = {256 + k},')
    out += [f'  I_END = {256 + len(I)}', '};', f'#define N_ITEMS {len(I)}', '']
    out.append('enum { ' + ', '.join(f'IK_{k.upper()}' for k in KINDS) + ' };')
    out.append('enum { ' + ', '.join(f'G_{g[2:]} = 0x{v:X}' for g, v in GROUPS.items()) + ' };')
    out += ['', '#endif', '']
    return '\n'.join(out)


if __name__ == '__main__':
    if '--header' in sys.argv:
        sys.stdout.write(header())
    else:
        for k, x in enumerate(I):
            print(256 + k, x['name'], x['label'])
        print(len(R), 'recipes')
