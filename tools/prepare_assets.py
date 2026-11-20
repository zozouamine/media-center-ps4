#!/usr/bin/env python3
"""Prepare bounded OFL font derivatives and original 2D application artwork."""
import argparse
import hashlib
import json
import subprocess
import unicodedata
from pathlib import Path
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def main(fetch_regular=False):
    manifest_path = ROOT / 'vendor/manifest.json'
    manifest = json.loads(manifest_path.read_text())
    latin = next(m for m in manifest if Path(m['file']).name.startswith(('NotoSans-', 'NotoSans[')))
    if fetch_regular and 'Italic' in latin['file']:
        name = 'NotoSans[wdth,wght].ttf'
        url = f"https://raw.githubusercontent.com/google/fonts/{latin['commit']}/ofl/notosans/NotoSans%5Bwdth,wght%5D.ttf"
        data = subprocess.check_output(['/usr/bin/curl', '-L', '--fail', '--silent', '--show-error', '--connect-timeout', '15', '--max-time', '90', url])
        (ROOT / latin['file']).unlink()
        latin.update(file='assets/fonts/' + name, source=url, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        (ROOT / latin['file']).write_bytes(data)
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    elif 'Italic' in latin['file']:
        raise SystemExit('Fetch the pinned upright source font using --fetch-regular before preparing assets.')
    generated = []
    codepoints = set(range(32, 0x250)) | set(range(0x600, 0x700)) | set(range(0x750, 0x780)) | set(range(0x8a0, 0x900))
    codepoints |= set(range(0x2000, 0x2070)) | set(range(0xfb50, 0xfe00)) | set(range(0xfe70, 0xff00)) | {0xfffd, 0x25a1}
    for family, output, label in [('NotoSans[', 'PS4MC-Sans.ttf', 'PS4MC Sans'), ('NotoSansArabic[', 'PS4MC-Arabic.ttf', 'PS4MC Arabic')]:
        entry = next(m for m in manifest if Path(m['file']).name.startswith(family))
        font = TTFont(ROOT / entry['file'], recalcTimestamp=False)
        font = instantiateVariableFont(font, {'wght': 400, 'wdth': 100}, inplace=True)
        options = subset.Options(); options.recalc_timestamp = False
        worker = subset.Subsetter(options=options); worker.populate(unicodes=codepoints); worker.subset(font)
        names = {1: label, 2: 'Regular', 3: label + ' Regular 1.0', 4: label + ' Regular', 6: label.replace(' ', '-') + '-Regular', 16: label, 17: 'Regular'}
        for record in list(font['name'].names):
            if record.nameID in names:
                font['name'].setName(names[record.nameID], record.nameID, record.platformID, record.platEncID, record.langID)
        dest = ROOT / 'assets/fonts' / output; font.save(dest)
        data = dest.read_bytes()
        generated.append({'file': 'assets/fonts/' + output, 'source_file': entry['file'], 'derivative_family': label,
                          'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data), 'license': 'OFL-1.1'})
    forms = {}
    for cp in range(0xfb50, 0xff00):
        parts = unicodedata.decomposition(chr(cp)).split()
        if len(parts) == 2 and parts[0] in ['<isolated>', '<final>', '<initial>', '<medial>']:
            base = int(parts[1], 16)
            if 0x600 <= base <= 0x8ff:
                forms.setdefault(base, {}).update({parts[0][1:-1]: cp})
    lines = ['// Generated from Unicode presentation-form decompositions; no external shaping library.', 'static const ArabicForm arabic_forms[] = {']
    for base, values in sorted(forms.items()):
        nums = [base] + [values.get(k, 0) for k in ['isolated', 'final', 'initial', 'medial']]
        lines.append('    {' + ', '.join(f'0x{c:04x}' for c in nums) + '},')
    lines.append('};\n'); (ROOT / 'src/ui/arabic_forms.inc').write_text('\n'.join(lines))
    image = Image.new('RGBA', (512, 512), (4, 12, 32, 255)); d = ImageDraw.Draw(image)
    d.rounded_rectangle((54, 54, 458, 458), radius=40, fill=(8, 77, 207), outline=(62, 150, 255), width=3)
    d.rounded_rectangle((112, 140, 400, 346), radius=12, outline='white', width=10)
    for x in (122, 370):
        for y in (154, 200, 246, 292): d.rectangle((x, y, x + 18, y + 25), fill='white')
    d.polygon([(217, 181), (217, 309), (310, 245)], fill='white')
    d.line((169, 389, 343, 389), fill=(161, 202, 255), width=10)
    d.line((196, 411, 316, 411), fill=(161, 202, 255), width=5)
    (ROOT / 'sce_sys').mkdir(exist_ok=True); image.save(ROOT / 'sce_sys/icon0.png', optimize=True)
    image.resize((96, 96)).save(ROOT / 'assets/icons/application.png', optimize=True)
    (ROOT / 'assets/fonts/manifest.json').write_text(json.dumps(generated, indent=2) + '\n')
    print(json.dumps(generated, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--fetch-regular', action='store_true')
    main(parser.parse_args().fetch_regular)
