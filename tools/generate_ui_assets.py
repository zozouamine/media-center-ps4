#!/usr/bin/env python3
"""Build the project's small original vector transport icons and PNG textures."""
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/ui/transport'
SCALE = 16


def line(draw, points, width=1.8):
    points = [(round(x * SCALE), round(y * SCALE)) for x, y in points]
    thickness = round(width * SCALE)
    draw.line(points, fill='white', width=thickness, joint='curve')
    r = thickness / 2
    for x, y in points:
        draw.ellipse((x-r, y-r, x+r, y+r), fill='white')


def rounded(draw, box, radius):
    draw.rounded_rectangle(tuple(round(v*SCALE) for v in box), radius=round(radius*SCALE), fill='white')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(ROOT / 'assets/fonts/PS4MC-Sans.ttf'), round(6.5*SCALE))
    icons = {}
    for name in ['previous', 'backward-10', 'play', 'pause', 'forward-10', 'next', 'stop', 'back', 'aspect', 'options']:
        image = Image.new('RGBA', (24*SCALE, 24*SCALE))
        draw = ImageDraw.Draw(image)
        if name in ['previous', 'next']:
            polygon = [(16, 6), (8, 12), (16, 18)]
            draw.polygon([(x*SCALE, y*SCALE) for x, y in polygon], fill='white')
            line(draw, [polygon[-1], *polygon], .8)
            line(draw, [(5, 5), (5, 19)], 2)
            if name == 'next': image = ImageOps.mirror(image)
            body = '<path d="M5 5v14"/><path d="m16 6-8 6 8 6z" fill="currentColor"/>'
            if name == 'next': body = '<g transform="translate(24 0) scale(-1 1)">'+body+'</g>'
        elif name in ['backward-10', 'forward-10']:
            draw.arc((4*SCALE, 4*SCALE, 20*SCALE, 20*SCALE), 202, 516, fill='white', width=round(1.8*SCALE))
            line(draw, [(4.5, 4.5), (4.5, 9), (9, 9)], 1.8)
            if name == 'forward-10': image = ImageOps.mirror(image)
            draw = ImageDraw.Draw(image)
            draw.text((12*SCALE, 12.5*SCALE), '10', font=font, fill='white', anchor='mm')
            body = '<path d="M4.5 9a8 8 0 1 1 1 9"/><path d="M4.5 4.5V9H9"/>'
            if name == 'forward-10': body = '<g transform="translate(24 0) scale(-1 1)">'+body+'</g>'
            body += '<text x="12" y="14" text-anchor="middle" font-family="sans-serif" font-size="6.5" fill="currentColor" stroke="none">10</text>'
        elif name == 'play':
            polygon = [(8, 5), (8, 19), (19, 12)]
            draw.polygon([(x*SCALE, y*SCALE) for x, y in polygon], fill='white')
            line(draw, [polygon[-1], *polygon], .8)
            body = '<path d="m8 5 11 7-11 7z" fill="currentColor"/>'
        elif name == 'pause':
            rounded(draw, (6.5, 5, 10, 19), 1.2); rounded(draw, (14, 5, 17.5, 19), 1.2)
            body = '<rect x="6.5" y="5" width="3.5" height="14" rx="1.2" fill="currentColor"/><rect x="14" y="5" width="3.5" height="14" rx="1.2" fill="currentColor"/>'
        elif name == 'stop':
            rounded(draw, (6, 6, 18, 18), 2.2)
            body = '<rect x="6" y="6" width="12" height="12" rx="2.2" fill="currentColor"/>'
        elif name == 'back':
            line(draw, [(19, 12), (5, 12)]); line(draw, [(11, 6), (5, 12), (11, 18)])
            body = '<path d="M19 12H5m6-6-6 6 6 6"/>'
        elif name == 'aspect':
            for points in [[(4,9),(4,5),(9,5)],[(15,5),(20,5),(20,9)],
                           [(20,15),(20,19),(15,19)],[(9,19),(4,19),(4,15)]]:
                line(draw, points)
            body = '<path d="M4 9V5h5m6 0h5v4m0 6v4h-5m-6 0H4v-4"/>'
        else:
            for x in [6,12,18]:
                draw.ellipse(((x-1.5)*SCALE,10.5*SCALE,(x+1.5)*SCALE,13.5*SCALE),fill='white')
            body = '<g fill="currentColor" stroke="none"><circle cx="6" cy="12" r="1.5"/><circle cx="12" cy="12" r="1.5"/><circle cx="18" cy="12" r="1.5"/></g>'
        (OUT / (name+'.svg')).write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">'+body+'</svg>\n')
        image.resize((96, 96), Image.Resampling.LANCZOS).save(OUT / (name+'.png'))
        icons[name] = True
    manifest_path = ROOT / 'assets/ui/manifest.json'
    manifest = [entry for entry in json.loads(manifest_path.read_text()) if '/transport/' not in entry['file']]
    for path in sorted(OUT.iterdir()):
        manifest.append({'file':path.relative_to(ROOT).as_posix(), 'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                         'bytes':path.stat().st_size, 'source':'Original PS4MC geometric icon artwork; generated from tools/generate_ui_assets.py'})
    manifest_path.write_text(json.dumps(manifest, indent=2)+'\n')
    print(f'Generated {len(icons)} vector icons and native PNG textures.')


if __name__ == '__main__':
    main()
