#!/usr/bin/env python3
"""The app's icon (src/icon.png, 55 x 56): a grass block as Minecraft 1.8.8's inventory
draws it (the top lit, the left side at 0.8, the right at 0.6), on the NumWorks home
screen's white.

    python3 tools/icon.py CLIENT_JAR"""
import io
import os
import sys
import zipfile

from PIL import Image

W, H = 55, 56
GRASS = (0x91, 0xBD, 0x59)   # plains grass, as the inventory tints it


def tex(z, name):
    return Image.open(io.BytesIO(z.read(f'assets/minecraft/textures/blocks/{name}.png'))).convert('RGBA')


def main():
    z = zipfile.ZipFile(sys.argv[1])
    top, side, over = tex(z, 'grass_top'), tex(z, 'grass_side'), tex(z, 'grass_side_overlay')
    out = Image.new('RGB', (W, H), (255, 255, 255))
    px = out.load()
    half = 26          # half the cube's width (pixels)
    edge = 29          # its sides' height
    cx, y0 = W // 2, (H - (half + edge)) // 2   # the top corner

    def tinted(c, k):
        return tuple(int(v * t / 255) for v, t in zip(c[:3], k))

    def side_texel(u, v):
        r, g, b, a = side.getpixel((u, v))
        o = over.getpixel((u, v))
        if o[3] >= 128:
            r, g, b = tinted(o, GRASS)
        return r, g, b

    for y in range(H):
        for x in range(W):
            fx, fy = x + 0.5 - cx, y + 0.5 - y0
            # top: a rhombus; (a, b) along its two edges
            a = (fx / half + fy / (half / 2)) / 2
            b = (-fx / half + fy / (half / 2)) / 2
            if 0 <= a < 1 and 0 <= b < 1:
                c = top.getpixel((min(15, int(a * 16)), min(15, int(b * 16))))
                px[x, y] = tinted(c, GRASS)
                continue
            # the sides, below the top's lower edges
            if -half <= fx < 0:
                u = (fx + half) / half
                v = (fy - half / 2 - u * half / 2) / edge
                if 0 <= v < 1:
                    r, g, b2 = side_texel(min(15, int(u * 16)), min(15, int(v * 16)))
                    px[x, y] = (int(r * 0.8), int(g * 0.8), int(b2 * 0.8))
            elif 0 <= fx < half:
                u = fx / half
                v = (fy - half + u * half / 2) / edge
                if 0 <= v < 1:
                    r, g, b2 = side_texel(min(15, int(u * 16)), min(15, int(v * 16)))
                    px[x, y] = (int(r * 0.6), int(g * 0.6), int(b2 * 0.6))
    out.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'icon.png'))


if __name__ == '__main__':
    main()
