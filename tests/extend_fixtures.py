"""Additional real format, artwork, folder-isolation and music-control fixtures."""
import base64
import hashlib
import io
import json
import os
import shutil
import struct
import wave
from pathlib import Path
import numpy as np
from PIL import Image, ImageOps


def syncsafe(value):
    return bytes((value >> shift) & 127 for shift in (21, 14, 7, 0))


def id3(cover, title='Artwork test track', version=3, corrupt=False):
    frames = []
    encoding = 3 if version == 4 else 0
    for key, text in [('TIT2', title), ('TPE1', 'PS4MC test fixtures'), ('TALB', 'Embedded artwork')]:
        body = bytes([encoding]) + text.encode('utf-8' if encoding == 3 else 'latin1')
        size = syncsafe(len(body)) if version == 4 else struct.pack('>I', len(body))
        frames.append(key.encode() + size + b'\0\0' + body)
    picture = b'corrupt cover bytes' if corrupt else cover
    body = bytes([encoding]) + b'image/png\0\x03\0' + picture
    size = syncsafe(len(body)) if version == 4 else struct.pack('>I', len(body))
    frames.append(b'APIC' + size + b'\0\0' + body)
    tag = b''.join(frames)
    return b'ID3' + bytes([version, 0, 0]) + syncsafe(len(tag)) + tag


def flac_picture(png, width, height):
    mime = b'image/png'
    return struct.pack('>II', 3, len(mime)) + mime + struct.pack('>I', 0) + struct.pack('>IIIII', width, height, 24, 0, len(png)) + png


def add_wav_tag(path, tag):
    data = path.read_bytes()
    chunk = b'id3 ' + struct.pack('<I', len(tag)) + tag + (b'\0' if len(tag) & 1 else b'')
    data += chunk
    path.write_bytes(data[:4] + struct.pack('<I', len(data)-8) + data[8:])


def extend(usb, base, root, encode_audio):
    marker = base / 'extended-fixtures.json'
    if marker.exists():
        previous = json.loads(marker.read_text())
        if previous.get('version') == 3 and all((base / p).exists() for p in previous['required']):
            return
    photos = usb / 'Photos'; music = usb / 'Music'; corrupt = usb / 'Corrupt'
    for folder in ['Videos', 'Videos/Nested', 'Music/Playlist']:
        (usb / folder).mkdir(parents=True, exist_ok=True)
    shutil.copy2(usb/'Sample Video.avi', usb/'Videos/Sample Video.avi')
    shutil.copy2(usb/'Movies/Silent MJPEG.avi', usb/'Videos/Nested/VideoOnly.avi')
    shutil.copy2(music/'Tone.wav', usb/'Videos/Audio.wav')
    shutil.copy2(photos/'Pattern.png', usb/'Videos/Image.png')
    shutil.copy2(usb/'Movies/Short RGB.avi', music/'Wrong category.avi')
    shutil.copy2(photos/'Pattern.png', music/'Wrong category.png')
    shutil.copy2(music/'Tone.wav', photos/'Wrong category.wav')
    (usb/'Readme.txt').write_text('A non-media file; visible in USB Files, information only.\n')
    (usb/'Videos/Readme.txt').write_text('This document must not appear in the Videos tab.\n')
    image = Image.open(photos/'Pattern.png').convert('RGB')
    image.save(photos/'Pattern.tga', compression='tga_rle')
    image.save(photos/'Pattern.ppm')
    image.getchannel('G').save(photos/'Pattern.pgm')
    shutil.copy2(photos/'Pattern.ppm', photos/'Pattern.pnm')
    image.save(photos/'Pattern lossless.webp', lossless=True)
    for name, compression in [('Pattern.tiff', 'raw'), ('Pattern LZW.tif', 'tiff_lzw'), ('Pattern PackBits.tif', 'packbits'), ('Pattern Deflate.tif', 'tiff_adobe_deflate')]:
        image.save(photos/name, compression=compression)
    image.save(corrupt/'Unsupported JPEG TIFF.tif', compression='jpeg')
    mono = image.getchannel('G').point(lambda p: 255 if p >= 40 else 0).convert('1')
    mono.save(photos/'Monochrome Group4.tif', compression='group4')
    rgb = np.asarray(image)
    # A valid PSD with an uncompressed, merged RGB image.
    psd = b'8BPS' + struct.pack('>H', 1) + bytes(6) + struct.pack('>HIIHH', 3, 80, 120, 8, 3)
    psd += struct.pack('>IIIH', 0, 0, 0, 0) + rgb.transpose(2, 0, 1).tobytes()
    (photos/'Pattern.psd').write_bytes(psd)
    # Radiance RGBE fixture with known values 1.0, 0.5 and 0.25.
    (photos/'Pattern.hdr').write_bytes(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 80 +X 120\n' + bytes((128, 64, 32, 129)) * (120*80))
    png = (photos/'Pattern.png').read_bytes()
    icon_header = struct.pack('<HHH', 0, 1, 1)
    (photos/'PNG icon.ico').write_bytes(icon_header + struct.pack('<BBBBHHII', 120, 80, 0, 0, 1, 32, len(png), 22) + png)
    bgra = np.concatenate([rgb[:, :, ::-1], np.full((80, 120, 1), 255, dtype=np.uint8)], axis=2)
    dib = struct.pack('<IiiHHIIiiII', 40, 120, 160, 1, 32, 0, 120*80*4, 0, 0, 0, 0)
    dib += bgra[::-1].tobytes() + bytes(((120+31)//32)*4*80)
    (photos/'Bitmap icon.ico').write_bytes(icon_header + struct.pack('<BBBBHHII', 120, 80, 0, 0, 1, 32, len(dib), 22) + dib)
    colors = [(230, 35, 35), (35, 230, 35), (35, 35, 230)]
    frames = [Image.new('RGB', (120, 80), color) for color in colors]
    frames[0].save(photos/'Animated.gif', save_all=True, append_images=frames[1:], duration=[120, 180, 240], loop=0, disposal=2, optimize=False)
    frames[0].save(photos/'Animated.webp', save_all=True, append_images=frames[1:], duration=[120, 180, 240], loop=0, lossless=True)
    # Transparent GIF subrectangles, including restore-to-previous disposal.
    palette = [0,0,0, 220,30,30, 30,30,220, 30,220,30] + [0]*756
    indexed = []
    arrays = [np.ones((24,32), dtype=np.uint8), np.zeros((24,32), dtype=np.uint8), np.zeros((24,32), dtype=np.uint8)]
    arrays[1][3:12, 5:14] = 2; arrays[2][12:21, 20:29] = 3
    for array in arrays:
        f = Image.fromarray(array).convert('P'); f.putpalette(palette); indexed.append(f)
    indexed[0].save(photos/'Disposal.gif', save_all=True, append_images=indexed[1:], transparency=0, disposal=[1,3,1], duration=[100,150,200], loop=0, optimize=False)
    with Image.open(photos/'Disposal.gif') as animation:
        golden = []
        for index in range(animation.n_frames):
            animation.seek(index); golden.append(animation.convert('RGBA').tobytes())
        (base/'expected-gif-disposal.rgba').write_bytes(b''.join(golden))
    # Structurally valid frame headers reject before any large pixel allocation.
    descriptor = b',' + struct.pack('<HHHHB', 0,0,1,1,0) + b'\x02\x02\x44\x01\0'
    gif_header = b'GIF89a' + struct.pack('<HHBBB',1,1,128,0,0) + bytes((0,0,0,255,255,255))
    (corrupt/'Too many frames.gif').write_bytes(gif_header + descriptor*257 + b';')
    large_header = b'GIF89a' + struct.pack('<HHBBB',512,512,128,0,0) + bytes((0,0,0,255,255,255))
    large_descriptor = b',' + struct.pack('<HHHHB',0,0,512,512,0) + b'\x02\x02\x44\x01\0'
    (corrupt/'Animation memory limit.gif').write_bytes(large_header + large_descriptor*65 + b';')
    good_gif = (photos/'Animated.gif').read_bytes()
    (corrupt/'Truncated.gif').write_bytes(good_gif[:-5])
    (corrupt/'Bad.gif').write_bytes(b'GIF89a broken')
    (corrupt/'Bad.webp').write_bytes(b'RIFF\x10\0\0\0WEBPinvalid')
    (corrupt/'Bad.ico').write_bytes(b'\0\0\x01\0\x01\0' + bytes(16))
    (corrupt/'Bad.tiff').write_bytes(b'II\x2a\0broken')
    anmf = b'ANMF' + struct.pack('<I',16) + bytes(16)
    payload = b'WEBP' + anmf*257
    (corrupt/'Too many frames.webp').write_bytes(b'RIFF'+struct.pack('<I',len(payload))+payload)
    # Native FLAC picture block and MP3 APIC tags carry actual PNG pixels.
    picture = flac_picture(png, 120, 80)
    mp3 = (music/'Tone.mp3').read_bytes()
    if mp3[:3] == b'ID3':
        size = sum((mp3[6+i] & 127) << (21-i*7) for i in range(4)); mp3 = mp3[10+size:]
    (music/'Artwork.mp3').write_bytes(id3(png)+mp3)
    (music/'Artwork v24.mp3').write_bytes(id3(png, version=4)+mp3)
    (music/'Artwork Arabic.mp3').write_bytes(id3(png, title='موسيقى عربية', version=4)+mp3)
    (music/'Corrupt artwork.mp3').write_bytes(id3(png, corrupt=True)+mp3)
    flac = bytearray((music/'Tone.flac').read_bytes()); offset = 4
    while True:
        last = bool(flac[offset] & 128); length = int.from_bytes(flac[offset+1:offset+4], 'big')
        flac[offset] &= 127; offset += 4+length
        if last: break
    (music/'Artwork.flac').write_bytes(bytes(flac[:offset])+b'\x86'+len(picture).to_bytes(3,'big')+picture+bytes(flac[offset:]))
    encode_audio(music/'Artwork.ogg', 'vorbis', 'ogg', {'title':'Artwork test track', 'METADATA_BLOCK_PICTURE':base64.b64encode(picture).decode()})
    encode_audio(music/'Tone.aac', 'aac', 'adts', {'title':'AAC tone'})
    encode_audio(music/'Tone.m4a', 'aac', 'ipod', {'title':'M4A tone'})
    encode_audio(music/'Tone.opus', 'libopus', 'ogg', {'title':'Opus tone'})
    encode_audio(music/'Tone.wma', 'wmav2', 'asf', {'title':'WMA tone'})
    encode_audio(music/'Tone.aiff', 'pcm_s16be', 'aiff', {'title':'AIFF tone'})
    shutil.copy2(music/'Tone.wav', music/'Artwork.wav'); add_wav_tag(music/'Artwork.wav', id3(png))
    cover = Image.open(usb/'Nature/Lake.png').convert('RGB') if (usb/'Nature/Lake.png').exists() else image
    cover = ImageOps.fit(cover, (512,512), Image.Resampling.LANCZOS); memory = io.BytesIO(); cover.save(memory, format='PNG'); cover_png = memory.getvalue()
    (base/'expected-artwork-cover.rgba').write_bytes(cover.convert('RGBA').tobytes())
    seconds = 35; rate = 48000; timeline = np.arange(seconds*rate) / rate
    for index, title in [(1,'First track'),(2,'Next track')]:
        signal = (np.sin(timeline * 2*np.pi*(220 if index == 1 else 330)) * 6500).astype(np.int16)
        stereo = np.stack([signal, signal], axis=1)
        path = music/'Playlist'/f'{index:02d} - {title}.wav'
        with wave.open(str(path),'wb') as out:
            out.setnchannels(2); out.setsampwidth(2); out.setframerate(rate); out.writeframes(stereo.tobytes())
        add_wav_tag(path, id3(cover_png, title))
    files = []
    for folder, directories, names in os.walk(usb, followlinks=False):
        directories[:] = [d for d in directories if d not in ['Large','Deep'] and not (Path(folder)/d).is_symlink()]
        for name in sorted(names):
            p = Path(folder)/name
            if not p.is_symlink(): files.append({'path':p.relative_to(usb).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    manifest_path = base/'manifest.json'; manifest = json.loads(manifest_path.read_text()); manifest['files'] = files
    manifest['extensions'] = 'Real GIF/WebP animations, TIFF/ICO/PSD/HDR/PNM, embedded artwork and 35-second PCM music'
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    required = [str(p.relative_to(base)) for p in [usb/'Videos/Sample Video.avi',photos/'Animated.gif',photos/'Animated.webp',music/'Playlist/01 - First track.wav',music/'Artwork.ogg',music/'Tone.aac',music/'Tone.m4a',music/'Tone.opus',music/'Tone.wma',music/'Tone.aiff',base/'expected-gif-disposal.rgba',base/'expected-artwork-cover.rgba']]
    marker.write_text(json.dumps({'version':3,'required':required},indent=2)+'\n')
    print(f'Added {len(files)} genuine media and negative fixtures; no production content.')
