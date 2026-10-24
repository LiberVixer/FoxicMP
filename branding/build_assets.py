#!/usr/bin/env python3
"""Render the three edition wordmarks from SVG; export Qt icon sizes.

Requires cairosvg, fonttools and Pillow. Artwork is already finalized in source/.
This script only lays out exact type and exports application resource formats.
"""
from pathlib import Path
import shutil
import base64
import cairosvg
from PIL import Image
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
FONTS = {
    'regular': TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'),
    'bold': TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'),
}
def lettering(text, x, y, size, fill, bold=False, spacing=0):
    # Outline the actual glyphs: reproducible on Windows/Linux and free of LCD
    # subpixel artifacts when transparent logos are composited on any theme.
    font = FONTS['bold' if bold else 'regular']
    glyphs, cmap = font.getGlyphSet(), font.getBestCmap()
    scale = size / font['head'].unitsPerEm
    paths = []
    for char in text:
        glyph = cmap[ord(char)]
        pen = SVGPathPen(glyphs)
        glyphs[glyph].draw(TransformPen(pen, (scale, 0, 0, -scale, x, y)))
        paths.append(pen.getCommands())
        x += font['hmtx'][glyph][0] * scale + spacing
    return f'<path aria-label="{text}" fill="{fill}" d="' + ''.join(paths) + '"/>'

EDITIONS = (
    ('foxicmp-windows-classic', 'FoxicMP', 'Windows Classic', 'foxicmp'),
    ('foxiqmp-windows-qt', 'FoxiqMP', 'Windows QT', 'foxiqmp'),
    ('foxiqmp-linux-qt', 'FoxiqMP', 'Linux QT', 'foxiqmp'),
)
for edition, name, platform, family in EDITIONS:
    for light in (False, True):
        suffix = '-light' if light else ''
        accent = ('#007c9f' if light else '#17c6f5') if family == 'foxicmp' else ('#07864e' if light else '#22d985')
        label = '#46525c' if light else '#e5e9ed'
        title_top, title_bottom = ('#b85300', '#d87200') if light else ('#ffd178', '#ff971d')
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="505" height="300" viewBox="0 0 505 300">
  <title>{name} — {platform} — #LiberVixer</title>
  <defs><linearGradient id="title" gradientUnits="userSpaceOnUse" x1="0" y1="87" x2="0" y2="130"><stop stop-color="{title_top}"/><stop offset="1" stop-color="{title_bottom}"/></linearGradient></defs>
  <image x="-5" y="22" width="256" height="256" xlink:href="../source/{family}-mark.png"/>
  {lettering(name, 230, 127, 51, 'url(#title)', bold=True, spacing=-1.1)}
  {lettering(platform, 233, 169, 24, label)}
  {lettering('#LiberVixer', 233, 210, 23, accent)}
</svg>'''
        target = ROOT / 'logos' / f'{edition}{suffix}.svg'
        target.write_text(svg, encoding='utf-8')
        image_uri = 'data:image/png;base64,' + base64.b64encode((ROOT / 'source' / f'{family}-mark.png').read_bytes()).decode('ascii')
        render_svg = svg.replace(f'../source/{family}-mark.png', image_uri)
        cairosvg.svg2png(bytestring=render_svg.encode(), write_to=str(target.with_suffix('.png')))
# Export sizes, with no creative changes to the approved Qt icon.
icon = Image.open(ROOT / 'source' / 'foxiqmp-icon.png').convert('RGBA')
icon.resize((256, 256), Image.Resampling.LANCZOS).save(REPO / 'qt/resources/icon.png')
icon.save(REPO / 'qt/resources/foxicmp.ico', sizes=[(16,16), (24,24), (32,32), (48,48), (64,64), (128,128), (256,256)])
for edition, _, _, _ in EDITIONS:
    for suffix in ('', '-light'):
        src = ROOT / 'logos' / f'{edition}{suffix}.png'
        if edition.endswith('classic'):
            dest = REPO / 'distrib/Defaults' / ('logo_black.png' if suffix else 'logo.png')
            shutil.copy2(src, dest)
            if not suffix:
                shutil.copy2(src, REPO / 'src/apps/mplayerc/res/logo.1.png')
        else:
            shutil.copy2(src, REPO / 'qt/resources' / src.name)
# Review sheets use the same final PNGs; no independent artwork or typography.
for light in (False, True):
    suffix = '-light' if light else ''
    background = '#eef0f2' if light else '#171b1f'
    gallery = '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1587" height="336" viewBox="0 0 1587 336">'
    gallery += f'<rect width="1587" height="336" fill="{background}"/>'
    for index, (edition, _, _, _) in enumerate(EDITIONS):
        gallery += f'<image x="{18 + index * 523}" y="18" width="505" height="300" xlink:href="logos/{edition}{suffix}.png"/>'
    gallery += '</svg>'
    (ROOT / f'preview{suffix}.svg').write_text(gallery, encoding='utf-8')
    for edition, _, _, _ in EDITIONS:
        path = f'logos/{edition}{suffix}.png'
        data = base64.b64encode((ROOT / path).read_bytes()).decode('ascii')
        gallery = gallery.replace(path, 'data:image/png;base64,' + data)
    cairosvg.svg2png(bytestring=gallery.encode(), write_to=str(ROOT / f'preview{suffix}.png'))
