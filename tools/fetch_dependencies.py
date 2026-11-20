#!/usr/bin/env python3
"""Fetch public source dependencies once; subsequent builds are offline."""
import concurrent.futures
import hashlib
import json
import subprocess
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def get(url):
    return subprocess.check_output([
        '/usr/bin/curl', '-L', '--fail', '--retry', '2', '--silent',
        '--show-error', '--connect-timeout', '15', '--max-time', '90', url,
    ])


def revision(repo, path=None):
    url = f'https://api.github.com/repos/{repo}/commits?per_page=1'
    if path:
        url += '&path=' + urllib.parse.quote(path)
    return json.loads(get(url))[0]['sha']


def download(item):
    repo, commit, remote, local = item
    url = f'https://raw.githubusercontent.com/{repo}/{commit}/' + urllib.parse.quote(remote, safe='/')
    data = get(url)
    dest = ROOT / local
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    print(f'{local}: {len(data)} bytes', flush=True)
    return {'file': local, 'source': url, 'commit': commit,
            'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}


def main():
    if (ROOT / 'vendor/manifest.json').exists():
        print('Dependencies already pinned; verify using preflight.sh.')
        return
    jobs = []
    dr = 'c2d31fed078120f67263fd36237c0dcaae6bc07b'
    for name in ['dr_wav.h', 'dr_mp3.h', 'dr_flac.h']:
        jobs.append(('mackron/dr_libs', dr, name, 'vendor/' + name))
    jobs.append(('mackron/dr_libs', dr, 'LICENSE', 'licenses/dr_libs-LICENSE.txt'))
    stb = '9600522de53973336fa033a289858a4354cbb04c'
    for name in ['stb_image.h', 'stb_truetype.h', 'stb_vorbis.c']:
        jobs.append(('nothings/stb', stb, name, 'vendor/' + name))
    jobs.append(('nothings/stb', stb, 'LICENSE', 'licenses/stb-LICENSE.txt'))
    goldhen = 'bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7'
    for name in ['include/GoldHEN.h', 'include/Syscall.h', 'source/GoldHEN.c', 'source/Syscall.c', 'LICENSE', 'README.md']:
        jobs.append(('GoldHEN/GoldHEN_Plugins_SDK', goldhen, name, 'vendor/goldhen-sdk/' + name))
    jobs.append(('GoldHEN/GoldHEN_Plugins_SDK', goldhen, 'LICENSE', 'licenses/GoldHEN-SDK-LICENSE.txt'))
    for family in ['notosans', 'notosansarabic']:
        folder = 'ofl/' + family
        commit = '8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5'
        font = 'NotoSans[wdth,wght].ttf' if family == 'notosans' else 'NotoSansArabic[wdth,wght].ttf'
        jobs.append(('google/fonts', commit, folder + '/' + font, 'assets/fonts/' + font))
        jobs.append(('google/fonts', commit, folder + '/OFL.txt', 'licenses/' + family + '-OFL.txt'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        manifest = list(executor.map(download, jobs))
    (ROOT / 'vendor/manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
