"""Upgrade the flat-color AC-130 cabin atlases with generated material detail.

The FlightGear atlases are mostly solid color blocks. Each block keeps its exact
place and mean color (so the model's UVs stay valid); generated seamless
material tiles (GPT Image via Codex) supply the surface detail at 4x resolution.

usage: enhance_ac130_textures.py <cabin dir> <tile dir>
  tile dir: insulation.png console.png fabric.png gunmetal.png
"""
import os, sys
import numpy as np
from PIL import Image, ImageStat

cabin, tiles = sys.argv[1:3]
SCALE = 4
# texture -> [(region as fractions x0,y0,x1,y1, tile, repeats)]; other areas are upscaled as-is.
PLAN = {
    'interior.png': [((0, 0, .5, .5), 'insulation', 1), ((.5, 0, 1, .5), 'gunmetal', 2),
                     ((0, .5, .5, 1), 'console', 2), ((.5, .5, 1, 1), 'console', 2)],
    'comp.png': [((0, 0, 1, .5), 'console', 2)],
    'chair.png': [((0, 0, 1, 1), 'fabric', 3)],
    'bms.png': [((0, 0, 1, 1), 'console', 2)],
    'keyboard.png': [((0, 0, 1, .5), 'console', 2), ((0, .5, .66, 1), 'gunmetal', 1)],
}

def luminance(im):
    return im.convert('L')

def detail(tile, size, repeats, mean_color):
    w, h = size
    cell = (max(1, w // repeats), max(1, h // repeats))
    t = tile.resize(cell, Image.LANCZOS)
    tiled = Image.new('RGB', (cell[0] * repeats, cell[1] * repeats))
    for y in range(repeats):
        for x in range(repeats):
            tiled.paste(t, (x * cell[0], y * cell[1]))
    tiled = tiled.resize((w, h), Image.LANCZOS)
    lum = np.asarray(luminance(tiled), dtype=np.float32)
    # Keep the block's color; modulate by the material's relative luminance.
    k = 0.35 + 0.65 * lum / max(float(lum.mean()), 1.0)
    rgb = np.clip(k[..., None] * np.asarray(mean_color, dtype=np.float32), 0, 255).astype(np.uint8)
    return Image.fromarray(rgb, 'RGB')

loaded = {n: Image.open(os.path.join(tiles, n + '.png')).convert('RGB') for n in ('insulation', 'console', 'fabric', 'gunmetal')}
for name, regions in PLAN.items():
    path = os.path.join(cabin, name)
    src = Image.open(path).convert('RGB')
    W, H = src.size
    out = src.resize((W * SCALE, H * SCALE), Image.NEAREST)
    for (x0, y0, x1, y1), tile, repeats in regions:
        box = (int(x0 * W), int(y0 * H), int(x1 * W), int(y1 * H))
        mean = tuple(int(c) for c in ImageStat.Stat(src.crop(box)).mean)
        size = ((box[2] - box[0]) * SCALE, (box[3] - box[1]) * SCALE)
        out.paste(detail(loaded[tile], size, repeats, mean), (box[0] * SCALE, box[1] * SCALE))
    out.save(path, optimize=True)
    print(name, src.size, '->', out.size)
