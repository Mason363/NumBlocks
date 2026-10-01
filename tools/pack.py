#!/usr/bin/env python3
"""Packs Minecraft 1.8.8's textures for NumBlocks into src/data.c and src/data.h.

    python3 tools/pack.py CLIENT_JAR

CLIENT_JAR is the official 1.8.8 client (client.jar, from Mojang's launcher
servers); only its pictures are read. Every texture is 16 x 16 with its own
palette of up to 16 colours (4 bits a texel): tinted ones (grass, leaves,
water) keep their grey levels, and the game multiplies them by the biome's
colour. Index 0 is transparent in textures that have see-through texels.

Also written: each block state's six faces, model, light, flags, hardness and
tool (from tools/blocks.py)."""
import io
import os
import sys
import zipfile

from PIL import Image


def pixels(im):
    return list(im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata())

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import blocks  # noqa: E402

TEX = 'assets/minecraft/textures/'


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


class Jar:
    def __init__(self, path):
        self.z = zipfile.ZipFile(path)

    def image(self, name):
        return Image.open(io.BytesIO(self.z.read(TEX + name + '.png'))).convert('RGBA')


def quantize(im, colors, keep_grey=False):
    """(indices 16x16, palette [(r,g,b)], transparent?): index 0 is transparent if any texel is."""
    px = pixels(im)
    alpha = any(a < 128 for _, _, _, a in px)
    opaque = [(r, g, b) for r, g, b, a in px if a >= 128]
    n = colors - (1 if alpha else 0)
    src = Image.new('RGB', (len(opaque), 1))
    src.putdata(opaque)
    q = src.quantize(n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:3 * n]
    pal = [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    idx = pixels(q)
    out, k = [], 0
    for r, g, b, a in px:
        if a < 128:
            out.append(0)
        else:
            out.append(idx[k] + (1 if alpha else 0))
            k += 1
    if alpha:
        pal = [(0, 0, 0)] + pal
    return out, pal, alpha


def first_frame(im):
    w, h = im.size
    return im.crop((0, 0, w, w)).resize((16, 16), Image.NEAREST) if w != 16 or h != 16 else im


def custom_texture(jar, n):
    """textures made from an entity's: the chest's faces (ModelChest: a 14 x 14 x 14 box)"""
    ch = jar.image('entity/chest/normal')
    out = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    if n == '@chest_top':
        out.paste(ch.crop((14, 0, 28, 14)), (1, 1))
    elif n in ('@chest_front', '@chest_side'):
        lid = ch.crop((14, 14, 28, 19) if n == '@chest_front' else (0, 14, 14, 19))
        base = ch.crop((14, 34, 28, 43) if n == '@chest_front' else (0, 34, 14, 43))
        out.paste(lid, (1, 2))
        out.paste(base, (1, 7))
        if n == '@chest_front':
            out.paste(ch.crop((1, 1, 3, 5)), (7, 5))   # the latch
    return out


def tex_image(jar, n):
    return custom_texture(jar, n) if n.startswith('@') else first_frame(jar.image('blocks/' + n))


def pack_textures(jar):
    """Textures in the order blocks.py first names them."""
    names = []
    for s in blocks.S:
        t = s['tex']
        if t is None:
            continue
        for n in ([t] if isinstance(t, str) else list(t.values())):
            if n and n not in names:
                names.append(n)
    tinted = set()
    for s in blocks.S:
        if s['tint'] and s['tex']:
            t = s['tex']
            if isinstance(t, str):
                tinted.add(t)
            else:
                tinted.add(t['top'] if s['name'].startswith('GRASS') else t.get('side'))
                if 'overlay' in t:
                    tinted.add(t['overlay'])
    tinted.discard('grass_side')   # its fringe comes from the overlay
    tinted.discard('grass_side_snowed')
    px, pals, flags = [], [], []
    for n in names:
        im = custom_texture(jar, n) if n.startswith('@') else first_frame(jar.image('blocks/' + n))
        if n == 'grass_side':
            # the dirt side keeps its colours; the fringe (the overlay's texels)
            # becomes grey levels tinted by the biome: palette indices from 8
            over = first_frame(jar.image('blocks/grass_side_overlay'))
            base = im.copy()
            ov = [a >= 128 for _, _, _, a in pixels(over)]
            dirt = Image.new('RGBA', (16, 16))
            dirt.putdata([p if not o else (0, 0, 0, 0) for p, o in zip(pixels(base), ov)])
            grey = Image.new('RGBA', (16, 16))
            grey.putdata([p if o else (0, 0, 0, 0) for p, o in zip(pixels(over), ov)])
            di, dp, _ = quantize(dirt, 9)
            gi, gp, _ = quantize(grey, 9)
            idx = [(gi[i] + 7) if ov[i] else di[i] for i in range(256)]
            pal = dp[:8] + gp[1:9]
            while len(pal) < 16:
                pal.append((0, 0, 0))
            px.append(idx)
            pals.append(pal)
            flags.append(8)   # tinted from index 8
            continue
        idx, pal, alpha = quantize(im, 16)
        while len(pal) < 16:
            pal.append((0, 0, 0))
        px.append(idx)
        pals.append(pal)
        tint_from = (1 if alpha else 0) if n in tinted else 16
        flags.append(tint_from | (0x20 if alpha else 0))
    return names, px, pals, flags


FACE = ['bottom', 'top', 'north', 'south', 'west', 'east']   # -Y +Y -Z +Z -X +X


def faces(s, names):
    t = s['tex']
    if t is None:
        return [0] * 6
    if isinstance(t, str):
        i = names.index(t)
        return [i] * 6
    side = t.get('side')
    out = []
    for f in FACE:
        n = t.get(f) or (t.get('top') if f == 'bottom' and 'bottom' not in t else side)
        if f in ('west', 'east') and 'x' in t:
            n = t['x']
        if f in ('north', 'south') and 'z' in t:
            n = t['z']
        out.append(names.index(n))
    return out


# shapes in sixteenths (x0 y0 z0 x1 y1 z1), up to two boxes; models not listed are whole cubes or
# drawn their own way (cross, liquid); ladders, vines, fences and panes are shaped by their neighbours
BOX = {'slab': [(0, 0, 0, 16, 8, 16)], 'layer': [(0, 0, 0, 16, 2, 16)], 'torch': [(7, 0, 7, 9, 10, 9)],
       'cactus': [(1, 0, 1, 15, 16, 15)], 'flat': [(0, 0, 0, 16, 1, 16)], 'fence': [(6, 0, 6, 10, 16, 10)],
       'pane': [(7, 0, 7, 9, 16, 9)], 'ladder': [(0, 0, 14, 16, 16, 16)], 'vine': [(0, 0, 15, 16, 16, 16)],
       'bed': [(0, 0, 0, 16, 9, 16)], 'chest': [(1, 0, 1, 15, 14, 15)], 'liquid': [(0, 0, 0, 16, 14, 16)]}
STAIR_TOP = [(8, 8, 0, 16, 16, 16), (0, 8, 0, 8, 16, 16), (0, 8, 8, 16, 16, 16), (0, 8, 0, 16, 16, 8)]
# BlockDoor's bounds: facing east, south, west, north; closed, then open
DOOR = [(0, 0, 0, 3, 16, 16), (0, 0, 0, 16, 16, 3), (13, 0, 0, 16, 16, 16), (0, 0, 13, 16, 16, 16),
        (0, 0, 13, 16, 16, 16), (0, 0, 0, 3, 16, 16), (0, 0, 0, 16, 16, 3), (13, 0, 0, 16, 16, 16)]
WALL_TORCH = {'TORCH_E': (1, 3, 7, 3, 14, 9), 'TORCH_W': (13, 3, 7, 15, 14, 9), 'TORCH_S': (7, 3, 1, 9, 14, 3),
              'TORCH_N': (7, 3, 13, 9, 14, 15)}


def boxes_of(s):
    m = s['model']
    if m == 'stairs':
        return [(0, 0, 0, 16, 8, 16), STAIR_TOP[s['meta'] & 3]]
    if m == 'door':
        return [DOOR[s['meta'] & 7]] if s['meta'] < 8 else [DOOR[0]]
    if s['name'] in WALL_TORCH:
        return [WALL_TORCH[s['name']]]
    return BOX.get(m, [])


TINT = {None: 0, 'grass': 1, 'foliage': 2, 'spruce': 3, 'birch': 4, 'water': 5, 'lily': 6}
OPAQUE = {'cube', 'slab'}   # (slabs and layers only hide what is under them)


# biome id: (temperature, rainfall) as BiomeBase sets them (1.8.8); mutated biomes (id + 128) share them
BIOMES = {0: (0.5, 0.5), 1: (0.8, 0.4), 2: (2.0, 0.0), 3: (0.2, 0.3), 4: (0.7, 0.8), 5: (0.25, 0.8), 6: (0.8, 0.9),
          7: (0.5, 0.5), 8: (2.0, 0.0), 9: (0.5, 0.5), 10: (0.0, 0.5), 11: (0.0, 0.5), 12: (0.0, 0.5), 13: (0.0, 0.5),
          14: (0.9, 1.0), 15: (0.9, 1.0), 16: (0.8, 0.4), 17: (2.0, 0.0), 18: (0.7, 0.8), 19: (0.25, 0.8),
          20: (0.2, 0.3), 21: (0.95, 0.9), 22: (0.95, 0.9), 23: (0.95, 0.8), 24: (0.5, 0.5), 25: (0.2, 0.3),
          26: (0.05, 0.3), 27: (0.6, 0.6), 28: (0.6, 0.6), 29: (0.7, 0.8), 30: (-0.5, 0.4), 31: (-0.5, 0.4),
          32: (0.3, 0.8), 33: (0.3, 0.8), 34: (0.2, 0.3), 35: (1.2, 0.0), 36: (1.0, 0.0), 37: (2.0, 0.0), 38: (2.0, 0.0),
          39: (2.0, 0.0)}
# (forest 4 is 0.7/0.8; birch forests 0.6/0.6; plains 0.8/0.4; mega spruce taiga mutated 160: 0.25/0.8)


def biome_colors(jar):
    grass = jar.image('colormap/grass').convert('RGB')
    foliage = jar.image('colormap/foliage').convert('RGB')

    def look(img, t, r):
        t = min(max(t, 0.0), 1.0)
        r = min(max(r, 0.0), 1.0) * t
        return img.getpixel((int((1 - t) * 255), int((1 - r) * 255)))

    g, f, w = [], [], []
    for i in range(256):
        base = i & 127 if i >= 128 else i
        t, r = BIOMES.get(base, (0.5, 0.5))
        if i == 160 or i == 161:
            t, r = 0.25, 0.8
        gc, fc = look(grass, t, r), look(foliage, t, r)
        if base == 6:   # swamp: fixed colours
            gc, fc = (0x6A, 0x70, 0x39), (0x6A, 0x70, 0x39)
        if base in (37, 38, 39):   # mesa
            gc, fc = (0x90, 0x81, 0x4D), (0x9E, 0x81, 0x4D)
        if base == 29:   # roofed forest: grass halfway to a dark green
            c = (gc[0] << 16) | (gc[1] << 8) | gc[2]
            c = ((c & 0xFEFEFE) + 0x28340A) >> 1
            gc = ((c >> 16) & 255, (c >> 8) & 255, c & 255)
        wc = (0xE0, 0xFF, 0xAE) if base == 6 else (0xFF, 0xFF, 0xFF)
        g.append(gc)
        f.append(fc)
        w.append(wc)
    return g, f, w


# GUI pieces: name, file, x, y, w, h
SPRITES = [
    ('hotbar', 'gui/widgets', 0, 0, 182, 22),
    ('hotbar_sel', 'gui/widgets', 0, 22, 24, 24),
    ('crosshair', 'gui/icons', 0, 0, 16, 16),
    ('heart_bg', 'gui/icons', 16, 0, 9, 9),
    ('heart', 'gui/icons', 52, 0, 9, 9),
    ('heart_half', 'gui/icons', 61, 0, 9, 9),
    ('food_bg', 'gui/icons', 16, 27, 9, 9),
    ('food', 'gui/icons', 52, 27, 9, 9),
    ('food_half', 'gui/icons', 61, 27, 9, 9),
    ('armor_bg', 'gui/icons', 16, 9, 9, 9),
    ('xp_bg', 'gui/icons', 0, 64, 182, 5),
    ('xp', 'gui/icons', 0, 69, 182, 5),
    ('bubble', 'gui/icons', 16, 18, 9, 9),
    ('heart_poison', 'gui/icons', 88, 0, 9, 9),
    ('heart_poison_half', 'gui/icons', 97, 0, 9, 9),
    ('food_hunger_bg', 'gui/icons', 133, 27, 9, 9),
    ('food_hunger', 'gui/icons', 88, 27, 9, 9),
    ('food_hunger_half', 'gui/icons', 97, 27, 9, 9),
    ('bobber', 'particle/particles', 8, 16, 8, 8),
    ('fishing_rod_cast', 'items/fishing_rod_cast', 0, 0, 16, 16),
    # the sun (its glow) and the moon's eight phases (their middles; drawn adding light)
    ('sun', 'environment/sun', 2, 2, 28, 28),
] + [(f'moon_{k}', 'environment/moon_phases', (k % 4) * 32 + 8, (k // 4) * 32 + 8, 16, 16) for k in range(8)]


# what the inventory tints its icons with: plains grass and foliage, and the fixed colours
# (ColorizerFoliage.getFoliageColorPine and getFoliageColorBirch, BlockLilyPad)
ICON_TINT = {'grass': (0x91, 0xBD, 0x59), 'foliage': (0x91, 0xBD, 0x59), 'spruce': (0x61, 0x99, 0x61),
             'birch': (0x80, 0xA7, 0x55), 'lily': (0x20, 0x80, 0x30)}


def iso_icon(jar, s, names_px):
    """A block as the inventory shows it: three faces, the top lit, the left 0.8, the right 0.6 (16 x 16)."""
    t = s['tex']
    k = ICON_TINT.get(s['tint'])
    if s['model'] in ('cross', 'flat', 'torch', 'vine', 'ladder', 'door', 'pane'):
        n = t if isinstance(t, str) else t.get('side')
        im = tex_image(jar, n)
        if k:
            im = im.convert('RGBA').copy()
            im.putdata([(r * k[0] // 255, g * k[1] // 255, b * k[2] // 255, a) for r, g, b, a in pixels(im)])
        return im
    top = t if isinstance(t, str) else t['top']
    left = t if isinstance(t, str) else t.get('front', t['side']) if s['meta'] in (0,) else t['side']
    right = t if isinstance(t, str) else t['side']
    out = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    timg = tex_image(jar, top)
    limg = tex_image(jar, (t.get('front', t['side']) if isinstance(t, dict) and 'front' in t else left))
    rimg = tex_image(jar, right)
    tint = s['tint']
    def tinted(im, shade, tint_on):
        px = []
        for r, g, b, a in pixels(im):
            if tint_on:
                r, g, b = r * k[0] // 255, g * k[1] // 255, b * k[2] // 255
            px.append((int(r * shade), int(g * shade), int(b * shade), a))
        o = Image.new('RGBA', im.size)
        o.putdata(px)
        return o
    timg = tinted(timg, 1.0, k is not None)
    limg = tinted(limg, 0.8, k is not None and tint != 'grass')
    rimg = tinted(rimg, 0.6, k is not None and tint != 'grass')
    h = 0.5 if s['model'] == 'slab' else 0.5625 if s['model'] == 'bed' else 1.0
    for y in range(16):
        for x in range(16):
            px, py = x + 0.5, y + 0.5
            # top face: (8,0) (16,4) (8,8) (0,4); u along (8,0)->(16,4), v along (8,0)->(0,4)
            oy = (1 - h) * 8
            a = ((px - 8) / 8 + (py - oy) / 4) / 2
            b = (-(px - 8) / 8 + (py - oy) / 4) / 2
            if 0 <= a < 1 and 0 <= b < 1:
                c = timg.getpixel((min(15, int(a * 16)), min(15, int(b * 16))))
                if c[3] >= 128:
                    out.putpixel((x, y), c)
                continue
            # left face: (0,4) (8,8) down 8: u = x/8, v = (y - 4 - x/2) / 8
            if px < 8:
                u, v = px / 8, (py - 4 - oy - px / 2) / (8 * h)
                if 0 <= u < 1 and 0 <= v < 1:
                    c = limg.getpixel((min(15, int(u * 16)), min(15, int(((1 - h) + v * h) * 16))))
                    if c[3] >= 128:
                        out.putpixel((x, y), c)
            else:
                u, v = (px - 8) / 8, (py - 8 - oy + (px - 8) / 2) / (8 * h)
                if 0 <= u < 1 and 0 <= v < 1:
                    c = rimg.getpixel((min(15, int(u * 16)), min(15, int(((1 - h) + v * h) * 16))))
                    if c[3] >= 128:
                        out.putpixel((x, y), c)
    return out


def pack_sprite(im):
    idx, pal, alpha = quantize(im, 16)
    if not alpha:   # keep index 0 free: a sprite is always drawn with 0 as see-through
        idx, pal, _ = quantize(im, 15)
        idx = [i + 1 for i in idx]
        pal = [(0, 0, 0)] + pal
    while len(pal) < 16:
        pal.append((0, 0, 0))
    w, h = im.size
    bs = []
    for y in range(h):
        row = idx[y * w:(y + 1) * w] + [0]
        bs += [row[i] | (row[i + 1] << 4) for i in range(0, w, 2)]
    return bs, pal, alpha


import items  # noqa: E402


def rle_image(im):
    """A picture as runs: each byte is a palette index (high nibble) and a run
    length - 1 (low nibble, 0-14); 15 means 16 + the next byte. Rows start at
    the offsets given. Index 0 is see-through if the picture has see-through
    pixels."""
    idx, pal, alpha = quantize(im, 16)
    if not alpha:
        idx, pal, _ = quantize(im, 15)
        idx = [i + 1 for i in idx]
        pal = [(0, 0, 0)] + pal
    while len(pal) < 16:
        pal.append((0, 0, 0))
    w, h = im.size
    data, rows = [], []
    for y in range(h):
        rows.append(len(data))
        row = idx[y * w:(y + 1) * w]
        x = 0
        while x < w:
            n = 1
            while x + n < w and n < 271 and row[x + n] == row[x]:
                n += 1
            if n < 16:
                data.append(row[x] << 4 | (n - 1))
            else:
                data += [row[x] << 4 | 15, n - 16]
            x += n
    return data, rows, pal


def leather(jar, part):
    """leather armour: the grey base dyed Minecraft's default leather colour, the overlay on top"""
    base = first_frame(jar.image('items/leather_' + part))
    over = first_frame(jar.image('items/leather_' + part + '_overlay'))
    out = Image.new('RGBA', (16, 16))
    px = []
    for (r, g, b, a), (r2, g2, b2, a2) in zip(pixels(base), pixels(over)):
        if a2 >= 128:
            px.append((r2, g2, b2, a2))
        else:
            px.append((r * 0xA0 // 255, g * 0x65 // 255, b * 0x40 // 255, a))
    out.putdata(px)
    return out


def item_icon(jar, it):
    t = it['tex']
    for part in ('helmet', 'chestplate', 'leggings', 'boots'):
        if t == 'leather_' + part:
            return leather(jar, part)
    return first_frame(jar.image('items/' + t))


def steve_front(jar):
    """the player as the inventory shows it, from the front (skin pixels scaled to 27 x 54)"""
    sk = jar.image('entity/steve')
    im = Image.new('RGBA', (16, 32), (0, 0, 0, 0))
    def put(sx, sy, w, h, dx, dy, over=False):
        part = sk.crop((sx, sy, sx + w, sy + h))
        if over:
            im.alpha_composite(part, (dx, dy))
        else:
            im.paste(part, (dx, dy))
    put(8, 8, 8, 8, 4, 0)            # head
    put(40, 8, 8, 8, 4, 0, True)     # hat
    put(20, 20, 8, 12, 4, 8)         # body
    put(44, 20, 4, 12, 0, 8)         # right arm (on the left)
    put(36, 52, 4, 12, 12, 8)        # left arm
    put(4, 20, 4, 12, 4, 20)         # right leg
    put(20, 52, 4, 12, 8, 20)        # left leg
    return im.resize((27, 54), Image.NEAREST)


def font_bits(jar):
    """ascii.png: 8 x 8 glyphs, a byte a row (bit 0 the left column); widths as FontRenderer measures them"""
    f = jar.image('font/ascii')
    bits, widths = [], []
    for c in range(32, 128):
        gx, gy = (c % 16) * 8, (c // 16) * 8
        rows = []
        right = -1
        for y in range(8):
            v = 0
            for x in range(8):
                if f.getpixel((gx + x, gy + y))[3] >= 128:
                    v |= 1 << x
                    right = max(right, x)
            rows.append(v)
        bits += rows
        widths.append(4 if c == 32 else right + 2)
    return bits, widths


# big GUI pictures, kept as runs: name, picture
def gui_images(jar):
    inv = jar.image('gui/container/inventory').crop((0, 0, 176, 166))
    craft = jar.image('gui/container/crafting_table').crop((0, 0, 176, 166))
    furn = jar.image('gui/container/furnace').crop((0, 0, 176, 166))
    g54 = jar.image('gui/container/generic_54')
    chest = Image.new('RGBA', (176, 71 + 96))
    chest.paste(g54.crop((0, 0, 176, 71)), (0, 0))
    chest.paste(g54.crop((0, 126, 176, 222)), (0, 71))
    w = jar.image('gui/widgets')
    tab = jar.image('gui/container/creative_inventory/tab_items').crop((0, 0, 195, 136))
    return [('inventory', inv), ('crafting_table', craft), ('furnace', furn), ('chest', chest), ('creative', tab)
]


GUI_SPRITES = [
    ('flame', 'gui/container/furnace', 176, 0, 14, 14),
    ('arrow', 'gui/container/furnace', 176, 14, 24, 17),
    ('armor_full', 'gui/icons', 34, 9, 9, 9),
    ('armor_half', 'gui/icons', 25, 9, 9, 9),
    ('bubble_pop', 'gui/icons', 25, 18, 9, 9),
    ('heart_hit', 'gui/icons', 25, 0, 9, 9),
    ('tab_top', 'gui/container/creative_inventory/tabs', 0, 0, 28, 32),
    ('tab_top_sel', 'gui/container/creative_inventory/tabs', 0, 32, 28, 32),
    ('tab_bottom', 'gui/container/creative_inventory/tabs', 0, 64, 28, 32),
    ('tab_bottom_sel', 'gui/container/creative_inventory/tabs', 0, 96, 28, 32),
    ('scroller', 'gui/container/creative_inventory/tabs', 232, 0, 12, 15),
    ('button', 'gui/widgets', 0, 66, 200, 20),
    ('button_hover', 'gui/widgets', 0, 86, 200, 20),
    ('button_off', 'gui/widgets', 0, 46, 200, 20),
]


def main():
    jar = Jar(sys.argv[1])
    names, px, pals, flags = pack_textures(jar)
    out = ['/* Generated by tools/pack.py from Minecraft 1.8.8\'s client.jar: textures and block tables. */',
           '#include "data.h"', '']
    out.append(f'const uint8_t tex_px[{len(names)}][128] = {{')
    for p in px:
        bs = [p[i] | (p[i + 1] << 4) for i in range(0, 256, 2)]
        out.append('  {' + ','.join(str(v) for v in bs) + '},')
    out.append('};')
    out.append(f'const uint16_t tex_pal[{len(names)}][16] = {{')
    for pal in pals:
        out.append('  {' + ','.join(f'0x{rgb565(*c):04X}' for c in pal) + '},')
    out.append('};')
    out.append(f'const uint8_t tex_flags[{len(names)}] = {{' + ','.join(str(f) for f in flags) + '};')
    out.append('')
    out.append(f'const uint8_t blk_tex[B_COUNT][6] = {{')
    for s in blocks.S:
        out.append('  {' + ','.join(str(v) for v in faces(s, names)) + '},   /* ' + s['name'] + ' */')
    out.append('};')
    models = blocks.MODELS
    out.append('const uint8_t blk_model[B_COUNT] = {' + ','.join(str(models.index(s['model'])) for s in blocks.S) + '};')
    out.append('const uint8_t blk_light[B_COUNT] = {' + ','.join(str(s['light']) for s in blocks.S) + '};')
    # shapes
    nb, bx = [], []
    for st in blocks.S:
        b = boxes_of(st)
        nb.append(len(b))
        b = (b + [(0, 0, 0, 0, 0, 0)] * 2)[:2]
        bx.append('{' + ','.join('{' + ','.join(str(v) for v in box) + '}' for box in b) + '}')
    out.append('const uint8_t blk_nbox[B_COUNT] = {' + ','.join(str(v) for v in nb) + '};')
    out.append('const int8_t blk_box[B_COUNT][2][6] = {' + ','.join(bx) + '};')
    # a front (furnaces, pumpkins, chests: turned to the open side) or a bed's end
    fr = []
    for st in blocks.S:
        t = st['tex']
        fr.append(names.index(t['front']) if isinstance(t, dict) and 'front' in t else 0)
    out.append('const uint8_t blk_front[B_COUNT] = {' + ','.join(str(v) for v in fr) + '};')
    fl = []
    for s in blocks.S:
        f = TINT[s['tint']]
        if s['model'] in ('cube',):
            f |= BF_OPAQUE
        if s['model'] in ('cube', 'leaves', 'glass', 'slab', 'cactus', 'fence', 'pane', 'door', 'layer', 'stairs',
                          'chest', 'bed'):
            f |= BF_SOLID
        fl.append(f)
    out.append('const uint8_t blk_flags[B_COUNT] = {' + ','.join(str(v) for v in fl) + '};')
    out.append('const uint8_t blk_tool[B_COUNT] = {' + ','.join(str(blocks.TOOLS.index(s['tool'])) for s in blocks.S) +
               '};')
    out.append('const uint8_t blk_level[B_COUNT] = {' + ','.join(str(s['level']) for s in blocks.S) + '};')
    # hardness in twentieths (255: unbreakable)
    out.append('const uint8_t blk_hard[B_COUNT] = {' +
               ','.join(str(255 if s['hard'] < 0 or s['hard'] >= 12.7 else int(round(s['hard'] * 20)))
                        for s in blocks.S) + '};')
    out.append('const uint8_t blk_id[B_COUNT] = {' + ','.join(str(s['id']) for s in blocks.S) + '};')
    out.append('const uint8_t blk_meta[B_COUNT] = {' + ','.join(str(s['meta']) for s in blocks.S) + '};')
    # GUI sprites and block icons: 4 bits a pixel, a palette each; index 0 see-through
    sprites = [(n, jar.image(f).crop((x, y, x + w, y + h))) for n, f, x, y, w, h in SPRITES]
    # the moon's faintest halo left out: cut to its middle, it would show as a square
    for k, (n, im) in enumerate(sprites):
        if n.startswith('moon_'):
            im = im.copy()
            im.putdata([(r, g, b, 0 if r + g + b < 60 else 255) for r, g, b, a in pixels(im)])
            sprites[k] = (n, im)
    for st in blocks.S:
        if st['tex'] is not None:
            sprites.append(('icon_' + st['name'].lower(), iso_icon(jar, st, None)))
    first_item = len(sprites)
    for it in items.I:
        sprites.append(('item_' + it['name'].lower(), item_icon(jar, it)))
    first_gui = len(sprites)
    sprites += [(n, jar.image(f).crop((x, y, x + w, y + h))) for n, f, x, y, w, h in GUI_SPRITES]
    for part in ('helmet', 'chestplate', 'leggings', 'boots'):
        sprites.append(('slot_' + part, first_frame(jar.image('items/empty_armor_slot_' + part))))
    sprites.append(('steve', steve_front(jar)))
    sprites.append(('arm', jar.image('entity/steve').crop((40, 16, 56, 32))))   # the right arm's box, all six faces
    bg = first_frame(jar.image('gui/options_background'))
    bg.putdata([(r * 0x40 // 255, g * 0x40 // 255, b * 0x40 // 255, 255) for r, g, b, a in pixels(bg)])
    sprites.append(('dirt_bg', bg))
    data, offs, pals, dims, flags2 = [], [], [], [], []
    for n, im in sprites:
        bs, pal, alpha = pack_sprite(im)
        offs.append(len(data))
        data += bs
        pals.append(pal)
        dims.append(im.size)
        flags2.append(1 if alpha else 0)
    out.append(f'const uint8_t spr_px[{len(data)}] = {{' + ','.join(str(v) for v in data) + '};')
    spr_bytes = len(data)
    out.append(f'const uint32_t spr_off[{len(sprites)}] = {{' + ','.join(str(v) for v in offs) + '};')
    out.append(f'const uint8_t spr_w[{len(sprites)}] = {{' + ','.join(str(w) for w, h in dims) + '};')
    out.append(f'const uint8_t spr_h[{len(sprites)}] = {{' + ','.join(str(h) for w, h in dims) + '};')
    out.append(f'const uint8_t spr_alpha[{len(sprites)}] = {{' + ','.join(str(v) for v in flags2) + '};')
    out.append(f'const uint16_t spr_pal[{len(sprites)}][16] = {{')
    for pal in pals:
        out.append('  {' + ','.join(f'0x{rgb565(*c):04X}' for c in pal) + '},')
    out.append('};')
    icon_of = []
    k = len(SPRITES)
    for st in blocks.S:
        if st['tex'] is not None:
            icon_of.append(k)
            k += 1
        else:
            icon_of.append(0xFFFF)
    out.append('const uint16_t blk_icon[B_COUNT] = {' + ','.join(str(v) for v in icon_of) + '};')
    # items: the block state a block counts as, then each item's tables
    def bi(n):
        v = items.block_item(n)
        return 0xFFFF if v is None else v
    out.append('const uint16_t blk_item[B_COUNT] = {' + ','.join(str(bi(st['name'])) for st in blocks.S) + '};')
    out.append('const uint16_t blk_fuel[B_COUNT] = {' +
               ','.join(str(items.fuel_of(i)) for i in range(len(blocks.S))) + '};')
    out.append('const char *const blk_label[B_COUNT] = {' +
               ','.join('"' + items.LABELS[st['name']] + '"' if st['name'] in items.LABELS else '0'
                        for st in blocks.S) + '};')
    I = items.I
    out.append('const char *const it_label[N_ITEMS] = {' + ','.join('"' + it['label'] + '"' for it in I) + '};')
    out.append('const uint16_t it_icon[N_ITEMS] = {' + ','.join(str(first_item + k) for k in range(len(I))) + '};')
    out.append('const uint8_t it_kind[N_ITEMS] = {' + ','.join(str(items.KINDS.index(it['kind'])) for it in I) + '};')
    out.append('const uint8_t it_stack[N_ITEMS] = {' + ','.join(str(it['stack']) for it in I) + '};')
    out.append('const uint16_t it_dur[N_ITEMS] = {' + ','.join(str(it['dur']) for it in I) + '};')
    out.append('const uint8_t it_tier[N_ITEMS] = {' + ','.join(str(it['tier']) for it in I) + '};')
    out.append('const uint8_t it_a[N_ITEMS] = {' + ','.join(str(it['a']) for it in I) + '};')
    out.append('const uint8_t it_b[N_ITEMS] = {' + ','.join(str(it['b']) for it in I) + '};')
    out.append('const uint8_t it_place[N_ITEMS] = {' +
               ','.join(str(items.BNAMES.index(it['place']) if it['place'] else 255) for it in I) + '};')
    out.append('const uint16_t it_fuel[N_ITEMS] = {' + ','.join(str(it['fuel']) for it in I) + '};')
    # crafting: out, count, width (0: shapeless), height (shapeless: how many), ingredients
    rec = []
    for o, n, rows, keys in items.R:
        if rows:
            w, hh = max(len(r) for r in rows), len(rows)
            ins = []
            for r in rows:
                for c in r.ljust(w):
                    ins.append(0 if c == ' ' else items.resolve(keys[c]))
        else:
            w, hh = 0, len(keys)
            ins = [items.resolve(k) for k in keys]
        ins += [0] * (9 - len(ins))
        rec.append('{%d,%d,%d,%d,{%s}}' % (items.resolve(o), n, w, hh, ','.join(str(v) for v in ins)))
    out.append(f'const Recipe recipes[{len(rec)}] = {{' + ','.join(rec) + '};')
    out.append(f'const uint16_t smelting[{len(items.SMELT)}][2] = {{' +
               ','.join('{%d,%d}' % (items.resolve(a), items.resolve(b)) for a, b in items.SMELT) + '};')
    for g, members in items.GROUP_OF.items():
        out.append(f'const uint16_t group_{g[2:].lower()}[] = {{' +
                   ','.join(str(items.BNAMES.index(m)) for m in members) + ',0xFFFF};')
    # mob skins: 64 x 32, 4 bits a texel, index 0 see-through
    SKINS = ['zombie/zombie', 'skeleton/skeleton', 'creeper/creeper', 'spider/spider', 'pig/pig', 'cow/cow',
             'sheep/sheep', 'sheep/sheep_fur', 'chicken']
    out.append(f'const uint8_t skin_px[{len(SKINS)}][1024] = {{')
    spal = []
    for n in SKINS:
        im = jar.image('entity/' + n).crop((0, 0, 64, 32))
        bs, pal, alpha = pack_sprite(im)
        out.append('  {' + ','.join(str(v) for v in bs) + '},')
        spal.append(pal)
    out.append('};')
    out.append(f'const uint16_t skin_pal[{len(SKINS)}][16] = {{')
    for pal in spal:
        out.append('  {' + ','.join(f'0x{rgb565(*c):04X}' for c in pal) + '},')
    out.append('};')
    # the cracks of a block being broken: 10 stages, 1 bit a texel (dark where set)
    cr = []
    for st in range(10):
        im = jar.image(f'blocks/destroy_stage_{st}')
        bits = [0] * 32
        for i, (r, g, b, a) in enumerate(pixels(im)):
            if a >= 100:
                bits[i >> 3] |= 1 << (i & 7)
        cr.append(bits)
    out.append('const uint8_t cracks[10][32] = {' + ','.join('{' + ','.join(str(v) for v in b) + '}' for b in cr) + '};')
    # the creative inventory: each tab's items, in id order (blocks, then items)
    tab_lists = [[] for _ in items.TABS]
    for iid in list(range(len(blocks.S))) + [256 + k for k in range(len(items.I))]:
        t = items.tab_of(iid)
        if t is not None:
            tab_lists[t].append(iid)
            tab_lists[5].append(iid)   # Search Items: everything
    flat, starts = [], []
    for L in tab_lists:
        starts.append(len(flat))
        flat += L
    starts.append(len(flat))
    out.append(f'const uint16_t tab_items[{len(flat)}] = {{' + ','.join(str(v) for v in flat) + '};')
    out.append(f'const uint16_t tab_start[{len(starts)}] = {{' + ','.join(str(v) for v in starts) + '};')
    out.append('const char *const tab_name[12] = {' + ','.join('"' + t + '"' for t in items.TABS) + '};')
    # the font
    fb, fw = font_bits(jar)
    out.append('const uint8_t font_bits[96 * 8] = {' + ','.join(str(v) for v in fb) + '};')
    out.append('const uint8_t font_w[96] = {' + ','.join(str(v) for v in fw) + '};')
    # GUI pictures as runs
    gi = gui_images(jar)
    allrle, offs, pals, sizes, rowoffs = [], [], [], [], []
    for n, im in gi:
        data, rows, pal = rle_image(im)
        offs.append(len(allrle))
        rowoffs.append(len(allrle))
        allrle += data
        pals.append(pal)
        sizes.append((im.size, rows))
    out.append(f'const uint8_t img_rle[{len(allrle)}] = {{' + ','.join(str(v) for v in allrle) + '};')
    rowtab, rowstart = [], []
    for (wh, rows), base in zip(sizes, offs):
        rowstart.append(len(rowtab))
        rowtab += [base + r for r in rows]
    out.append(f'const uint32_t img_rows[{len(rowtab)}] = {{' + ','.join(str(v) for v in rowtab) + '};')
    out.append(f'const uint16_t img_row0[{len(gi)}] = {{' + ','.join(str(v) for v in rowstart) + '};')
    out.append(f'const uint8_t img_w[{len(gi)}] = {{' + ','.join(str(wh[0]) for wh, r in sizes) + '};')
    out.append(f'const uint8_t img_h[{len(gi)}] = {{' + ','.join(str(wh[1]) for wh, r in sizes) + '};')
    out.append(f'const uint16_t img_pal[{len(gi)}][16] = {{')
    for pal in pals:
        out.append('  {' + ','.join(f'0x{rgb565(*c):04X}' for c in pal) + '},')
    out.append('};')
    gui_names = [n for n, im in gi]
    extra = [n for n, *_ in GUI_SPRITES] + ['slot_helmet', 'slot_chestplate', 'slot_leggings', 'slot_boots', 'steve',
                                            'arm', 'dirt_bg']
    # Minecraft's clouds: 256 x 256, 1 bit a cell
    cl = jar.image('environment/clouds')
    bits = bytearray(256 * 256 // 8)
    for i, (r, g, b, a) in enumerate(pixels(cl)):
        if a >= 128:
            bits[i >> 3] |= 1 << (i & 7)
    out.append('const uint8_t clouds[8192] = {' + ','.join(str(v) for v in bits) + '};')
    # rain and snow (64 x 256): their streaks and flakes as runs down a texel column,
    # {row, column, length, alpha}, by row; texels too faint to pass 1.8's alpha test (0.1) left out
    for name in ('rain', 'snow'):
        im = jar.image('environment/' + name)
        w, h = im.size
        px = im.load()
        runs = []
        for x in range(w):
            y = 0
            while y < h:
                if px[x, y][3] > 25:
                    y0, al = y, []
                    while y < h and px[x, y][3] > 25:
                        al.append(px[x, y][3])
                        y += 1
                    runs.append((y0, x, len(al), round(sum(al) / len(al))))
                else:
                    y += 1
        runs.sort()
        idx = [next((i for i, r in enumerate(runs) if r[0] >= row), len(runs)) for row in range(h + 1)]
        out.append(f'const uint8_t {name}_run[{len(runs)}][4] = {{' + ','.join('{%d,%d,%d,%d}' % r for r in runs) + '};')
        out.append(f'const uint8_t {name}_idx[{h + 1}] = {{' + ','.join(str(v) for v in idx) + '};')
    g, f, w = biome_colors(jar)
    out.append('const uint16_t biome_grass[256] = {' + ','.join(f'0x{rgb565(*c):04X}' for c in g) + '};')
    out.append('const uint16_t biome_foliage[256] = {' + ','.join(f'0x{rgb565(*c):04X}' for c in f) + '};')
    out.append('const uint16_t biome_water[256] = {' + ','.join(f'0x{rgb565(*c):04X}' for c in w) + '};')
    temps = [BIOMES.get(i & 127 if i >= 128 else i, (0.5, 0.5))[0] for i in range(256)]
    out.append('const int8_t biome_temp[256] = {' + ','.join(str(int(round(t * 50))) for t in temps) + '};   /* x 50 */')
    # BiomeGenBase.enableRain (off in deserts, savannas, mesas) and enableSnow; mutated biomes copy them
    no_rain, snowy = {2, 8, 9, 17, 35, 36, 37, 38, 39}, {10, 11, 12, 13, 26, 30, 31}
    rain = [(0 if (i & 127) in no_rain else 1) | (2 if (i & 127) in snowy else 0) for i in range(256)]
    out.append('const uint8_t biome_rain[256] = {' + ','.join(str(v) for v in rain) + '};')
    open(os.path.join(ROOT, 'src', 'data.c'), 'w').write('\n'.join(out) + '\n')

    h = ['/* Generated by tools/pack.py: textures and block tables (see data.c). */', '#ifndef NB_DATA_H',
         '#define NB_DATA_H', '#include <stdint.h>', '#include "blocks.h"', '',
         f'#define NTEX {len(names)}',
         '/* tex_flags: bits 0-4, the first palette index tinted by the biome (16: none); 0x20: has see-through texels */',
         'extern const uint8_t tex_px[NTEX][128];', 'extern const uint16_t tex_pal[NTEX][16];',
         'extern const uint8_t tex_flags[NTEX];', '',
         '/* faces: -Y +Y -Z +Z -X +X */', 'extern const uint8_t blk_tex[B_COUNT][6];',
         'extern const uint8_t blk_model[B_COUNT], blk_light[B_COUNT], blk_flags[B_COUNT], blk_tool[B_COUNT];',
         'extern const uint8_t blk_nbox[B_COUNT];   /* how many boxes it is made of (0: a whole cube) */',
         'extern const int8_t blk_box[B_COUNT][2][6];   /* the boxes, sixteenths: x0 y0 z0 x1 y1 z1 */',
         'extern const uint8_t blk_front[B_COUNT];   /* the texture of its front (0: none) */',
         'extern const uint8_t blk_level[B_COUNT], blk_hard[B_COUNT], blk_id[B_COUNT], blk_meta[B_COUNT];',
         '/* blk_flags: bits 0-2 the tint (1 grass, 2 foliage, 3 spruce, 4 birch, 5 water, 6 lily pad) */',
         'extern const uint16_t biome_grass[256], biome_foliage[256], biome_water[256];',
         'extern const int8_t biome_temp[256];   /* temperature x 50 */',
         'extern const uint8_t biome_rain[256];   /* 1: it rains (or snows), 2: a snowy biome */',
         '/* rain and snow: runs down a texel column {row, column, length, alpha}, by row; idx: the first run at or below a row */',
         'extern const uint8_t rain_run[][4], snow_run[][4], rain_idx[257], snow_idx[257];',
         f'#define BF_OPAQUE {BF_OPAQUE}', f'#define BF_SOLID {BF_SOLID}', '#define BF_TINT 7', '']
    for i, n in enumerate(names):
        h.append(f'#define TX_{n.upper().replace("@", "")} {i}')
    h += ['', 'extern const uint8_t spr_px[], spr_w[], spr_h[], spr_alpha[];', 'extern const uint32_t spr_off[];',
          'extern const uint16_t spr_pal[][16];', 'extern const uint16_t blk_icon[B_COUNT];   /* sprite of its icon */']
    for i, sp in enumerate(SPRITES):
        h.append(f'#define SP_{sp[0].upper()} {i}')
    for i, n in enumerate(extra):
        h.append(f'#define SP_{n.upper()} {first_gui + i}')
    h += ['', '#include "items.h"',
          'extern const uint16_t blk_item[B_COUNT];   /* the item a block state counts as (0xFFFF: none) */',
          'extern const uint16_t blk_fuel[B_COUNT];   /* ticks it burns in a furnace */',
          'extern const char *const blk_label[B_COUNT], *const it_label[N_ITEMS];',
          'extern const uint16_t it_icon[N_ITEMS], it_dur[N_ITEMS], it_fuel[N_ITEMS];',
          '/* it_tier: the tool or armour material; it_a: attack damage, armour points or food;'
          ' it_b: saturation x 10 */',
          'extern const uint8_t it_kind[N_ITEMS], it_stack[N_ITEMS], it_tier[N_ITEMS], it_a[N_ITEMS], it_b[N_ITEMS];',
          'extern const uint8_t it_place[N_ITEMS];   /* the block it places (255: none) */',
          'typedef struct { uint16_t out; uint8_t n, w, h; uint16_t in[9]; } Recipe;   /* w 0: shapeless, h of them */',
          f'#define N_RECIPES {len(items.R)}', 'extern const Recipe recipes[N_RECIPES];',
          f'#define N_SMELTING {len(items.SMELT)}', 'extern const uint16_t smelting[N_SMELTING][2];']
    for g in items.GROUP_OF:
        h.append(f'extern const uint16_t group_{g[2:].lower()}[];   /* ends with 0xFFFF */')
    h += ['extern const uint8_t skin_px[][1024];   /* mob skins, 64 x 32 */', 'extern const uint16_t skin_pal[][16];',
          'enum { SKIN_ZOMBIE, SKIN_SKELETON, SKIN_CREEPER, SKIN_SPIDER, SKIN_PIG, SKIN_COW, SKIN_SHEEP, SKIN_SHEEP_FUR,'
          ' SKIN_CHICKEN };',
          'extern const uint8_t cracks[10][32];   /* destroy stages, 1 bit a texel */']
    h += ['extern const uint16_t tab_items[], tab_start[13];   /* the creative tabs\' items */',
          'extern const char *const tab_name[12];']
    h += ['extern const uint8_t font_bits[96 * 8], font_w[96];   /* characters 32-127 */',
          'extern const uint8_t img_rle[], img_w[], img_h[];', 'extern const uint32_t img_rows[];',
          'extern const uint16_t img_row0[], img_pal[][16];']
    for i, n in enumerate(gui_names):
        h.append(f'#define IMG_{n.upper()} {i}')
    h += ['', '#endif', '']
    open(os.path.join(ROOT, 'src', 'data.h'), 'w').write('\n'.join(h))
    print(f'{len(names)} textures, {len(blocks.S)} block states, {len(items.I)} items, {len(items.R)} recipes, '
          f'{len(sprites)} sprites ({spr_bytes} bytes), GUI runs {len(allrle)} bytes')


BF_OPAQUE = 8
BF_SOLID = 16

if __name__ == '__main__':
    main()
